#include <stddef.h>

#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/pipe.h"
#include "kernel/process.h"
#include "kernel/spinlock.h"

struct pipe_state {
	struct spinlock lock;
	struct pipe_buffer buffer;
	uint32_t readers;
	uint32_t writers;
	uint8_t read_wait_channel;
	uint8_t write_wait_channel;
};

enum pipe_end { PIPE_END_READ, PIPE_END_WRITE };
struct pipe_endpoint {
	struct pipe_state *pipe;
	enum pipe_end end;
};

static int pipe_object_read(void *, uint64_t, void *, size_t, size_t *);
static int pipe_object_write(void *, uint64_t, const void *, size_t, size_t *);
static void pipe_close(void *);
static uint32_t pipe_poll(void *, uint32_t);

static const struct fd_operations pipe_read_operations = {
	.read = pipe_object_read, .poll = pipe_poll, .close = pipe_close
};
static const struct fd_operations pipe_write_operations = {
	.write = pipe_object_write, .poll = pipe_poll, .close = pipe_close
};

static void
pipe_free(void *allocation)
{
	if (!kheap_free(allocation)) {
		klog("OpenSillicon panic: cannot release pipe allocation\n");
		kernel_halt();
	}
}

static void
pipe_close(void *context)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;
	uint64_t flags = spinlock_lock_irqsave(&pipe->lock);
	void *channel = endpoint->end == PIPE_END_READ ?
	    (void *)&pipe->write_wait_channel : (void *)&pipe->read_wait_channel;
	if (endpoint->end == PIPE_END_READ)
		pipe->readers--;
	else
		pipe->writers--;
	bool unused = pipe->readers == 0 && pipe->writers == 0;
	spinlock_unlock_irqrestore(&pipe->lock, flags);
	/* Another final close can now free pipe. Only the saved opaque channel
	 * value is used below; callbacks never dereference channel addresses. */
	process_wake_channel(channel);
	fd_readiness_wake();
	pipe_free(endpoint);
	if (unused)
		pipe_free(pipe);
}

static int
pipe_read_copy(struct pipe_state *pipe, size_t length, pipe_copy_function copy,
	void *context, size_t *read_length)
{
	uint8_t buffer[PIPE_CAPACITY];
	int result = 0;

	if (read_length == NULL || copy == NULL)
		return -EINVAL;
	*read_length = 0;
	if (length == 0)
		return 0;
	uint64_t flags = spinlock_lock_irqsave(&pipe->lock);
	if (pipe->buffer.count == 0) {
		result = pipe->writers == 0 ? 0 : -EAGAIN;
	} else {
		size_t amount = pipe_buffer_peek(&pipe->buffer, buffer, length);
		/* The copy must not sleep or allocate. Failure preserves the queue. */
		if (!copy(context, buffer, amount)) {
			result = -EFAULT;
		} else {
			(void)pipe_buffer_consume(&pipe->buffer, amount);
			*read_length = amount;
		}
	}
	spinlock_unlock_irqrestore(&pipe->lock, flags);
	if (*read_length != 0) {
		process_wake_channel(&pipe->write_wait_channel);
		fd_readiness_wake();
	}
	return result;
}

static bool
copy_kernel(void *destination, const void *source, size_t length)
{
	uint8_t *out = destination;
	const uint8_t *in = source;
	for (size_t i = 0; i < length; i++)
		out[i] = in[i];
	return true;
}

static int
pipe_object_read(void *context, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct pipe_endpoint *endpoint = context;
	(void)offset;
	if (buffer == NULL && length != 0)
		return -EINVAL;
	return pipe_read_copy(endpoint->pipe, length, copy_kernel, buffer, read_length);
}

static int
pipe_object_write(void *context, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;
	int result = 0;
	(void)offset;
	if (write_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*write_length = 0;
	if (length == 0)
		return 0;
	uint64_t flags = spinlock_lock_irqsave(&pipe->lock);
	if (pipe->readers == 0)
		result = -EPIPE;
	else if (pipe->buffer.count == PIPE_CAPACITY ||
	    (length <= PIPE_CAPACITY && PIPE_CAPACITY - pipe->buffer.count < length))
		/* Small writes are indivisible even with several writers. */
		result = -EAGAIN;
	else
		*write_length = pipe_buffer_write(&pipe->buffer, buffer, length);
	spinlock_unlock_irqrestore(&pipe->lock, flags);
	if (*write_length != 0) {
		process_wake_channel(&pipe->read_wait_channel);
		fd_readiness_wake();
	}
	return result;
}

int
pipe_create(struct fd_table *table, int *read_descriptor, int *write_descriptor)
{
	if (table == NULL || read_descriptor == NULL || write_descriptor == NULL)
		return -EINVAL;
	struct pipe_state *pipe = kheap_alloc(sizeof(*pipe));
	struct pipe_endpoint *read_endpoint = kheap_alloc(sizeof(*read_endpoint));
	struct pipe_endpoint *write_endpoint = kheap_alloc(sizeof(*write_endpoint));
	if (pipe == NULL || read_endpoint == NULL || write_endpoint == NULL) {
		if (pipe != NULL) pipe_free(pipe);
		if (read_endpoint != NULL) pipe_free(read_endpoint);
		if (write_endpoint != NULL) pipe_free(write_endpoint);
		return -ENOMEM;
	}
	pipe->lock = (struct spinlock)SPINLOCK_INITIALIZER;
	pipe_buffer_init(&pipe->buffer);
	pipe->readers = pipe->writers = 1;
	pipe->read_wait_channel = pipe->write_wait_channel = 0;
	*read_endpoint = (struct pipe_endpoint){pipe, PIPE_END_READ};
	*write_endpoint = (struct pipe_endpoint){pipe, PIPE_END_WRITE};
	struct fd_object *read_object = fd_object_create(&pipe_read_operations, read_endpoint);
	if (read_object == NULL) {
		pipe_free(read_endpoint);
		pipe_free(write_endpoint);
		pipe_free(pipe);
		return -ENOMEM;
	}
	struct fd_object *write_object = fd_object_create(&pipe_write_operations, write_endpoint);
	if (write_object == NULL) {
		pipe_close(write_endpoint);
		fd_object_put(read_object);
		return -ENOMEM;
	}
	read_object->status_flags = 0;
	write_object->status_flags = 1;
	int result = fd_install_pair(table, read_object, write_object,
	    read_descriptor, write_descriptor);
	if (result != 0) {
		fd_object_put(read_object);
		fd_object_put(write_object);
	}
	return result;
}

bool
pipe_is_read_endpoint(const struct fd_object *object)
{
	return object != NULL && object->operations == &pipe_read_operations;
}

bool
pipe_is_write_endpoint(const struct fd_object *object)
{
	return object != NULL && object->operations == &pipe_write_operations;
}

int
pipe_read_to(struct fd_object *object, size_t length, pipe_copy_function copy,
	void *context, size_t *read_length)
{
	if (!pipe_is_read_endpoint(object))
		return -EBADF;
	struct pipe_endpoint *endpoint = object->context;
	return pipe_read_copy(endpoint->pipe, length, copy, context, read_length);
}

int
pipe_write_try(struct fd_object *object, const void *buffer, size_t length,
	size_t *write_length)
{
	if (!pipe_is_write_endpoint(object))
		return -EBADF;
	return pipe_object_write(object->context, 0, buffer, length, write_length);
}

void *
pipe_wait_channel(struct fd_object *object, unsigned int operation)
{
	if (!pipe_is_read_endpoint(object) && !pipe_is_write_endpoint(object))
		return NULL;
	struct pipe_endpoint *endpoint = object->context;
	if (operation == PIPE_READ_WAIT && endpoint->end == PIPE_END_READ)
		return &endpoint->pipe->read_wait_channel;
	if (operation == PIPE_WRITE_WAIT && endpoint->end == PIPE_END_WRITE)
		return &endpoint->pipe->write_wait_channel;
	return NULL;
}

static uint32_t
pipe_poll(void *context, uint32_t events)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;
	uint32_t ready = 0;
	uint64_t flags = spinlock_lock_irqsave(&pipe->lock);
	if (endpoint->end == PIPE_END_READ && (events & FD_POLL_READ) != 0 &&
	    (pipe->buffer.count != 0 || pipe->writers == 0))
		ready |= FD_POLL_READ;
	if (endpoint->end == PIPE_END_WRITE) {
		if ((events & FD_POLL_WRITE) != 0 &&
		    (pipe->readers == 0 || pipe->buffer.count < PIPE_CAPACITY))
			ready |= FD_POLL_WRITE;
		if ((events & FD_POLL_ERROR) != 0 && pipe->readers == 0)
			ready |= FD_POLL_ERROR;
	}
	spinlock_unlock_irqrestore(&pipe->lock, flags);
	return ready;
}
