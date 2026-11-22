#include "syscall_internal.h"
#include "abi/stat.h"
#include "abi/dirent.h"

#include "kernel/mount.h"
#include "kernel/process.h"
#include "kernel/vfs.h"
#include "kernel/vmm.h"

#define NATIVE_AT_SYMLINK_NOFOLLOW 0x100U


static int
stat_from_metadata(struct os_stat *value,
	const struct fd_metadata *metadata)
{
	if (value == NULL || metadata == NULL)
		return -EINVAL;
	if (metadata->size > INT64_MAX)
		return -EOVERFLOW;
	*value = (struct os_stat){0};
	value->st_ino = metadata->inode;
	value->st_size = (int64_t)metadata->size;
	value->st_mode = metadata->mode & 07777U;
	value->st_uid = metadata->uid;
	value->st_gid = metadata->gid;
	if (metadata->type == VNODE_REGULAR)
		value->st_mode |= OS_S_IFREG;
	else if (metadata->type == VNODE_DIRECTORY)
		value->st_mode |= OS_S_IFDIR;
	else if (metadata->type == VNODE_SOCKET)
		value->st_mode |= OS_S_IFSOCK;
	else if (metadata->type == VNODE_DEVICE)
		value->st_mode |= OS_S_IFCHR;
	else
		return -EOPNOTSUPP;
	return 0;
}

static struct interrupt_frame *
syscall_getdents(struct interrupt_frame *frame)
{
	uint64_t records[OS_GETDENTS_MAX / sizeof(uint64_t)];
	char *output = (char *)records;
	struct fd_table *table = current_fd_table();
	uint64_t descriptor = frame->rdi;
	uint64_t start_offset;
	size_t capacity;
	size_t written = 0;
	int result;

	if (fd_object_for_descriptor(table, descriptor) == NULL) {
		syscall_error(frame, EBADF);
		return frame;
	}
	if (frame->rdx == 0 || frame->rdx > OS_GETDENTS_MAX) {
		syscall_error(frame, EINVAL);
		return frame;
	}
	capacity = (size_t)frame->rdx;
	if (!vmm_validate_user_write(vmm_active_address_space(), frame->rsi,
	    capacity)) {
		syscall_error(frame, EFAULT);
		return frame;
	}
	start_offset = fd_get_offset(table, descriptor);

	while (written < capacity) {
		struct vfs_dirent entry;
		struct os_dirent_record *record;
		uint64_t before = fd_get_offset(table, descriptor);
		uint64_t after;
		size_t name_length = 0;
		size_t record_length;
		size_t index;
		uint8_t type;

		result = fd_readdir(table, descriptor, &entry);
		if (result < 0) {
			if (written == 0)
				syscall_error(frame, errno_from_kernel(result));
			break;
		}
		if (result == 0)
			break;
		after = fd_get_offset(table, descriptor);
		while (name_length < sizeof(entry.name) &&
		    entry.name[name_length] != '\0')
			name_length++;
		if (before == UINT64_MAX || after != before + 1U || after > INT64_MAX ||
		    entry.inode == 0 || name_length == 0 ||
		    name_length == sizeof(entry.name)) {
			(void)fd_set_offset(table, descriptor, before);
			if (written == 0)
				syscall_error(frame, EIO);
			break;
		}
		if (entry.type == VNODE_DIRECTORY)
			type = OS_DT_DIR;
		else if (entry.type == VNODE_REGULAR)
			type = OS_DT_REG;
		else if (entry.type == VNODE_SOCKET)
			type = OS_DT_SOCK;
		else if (entry.type == VNODE_DEVICE)
			type = OS_DT_CHR;
		else {
			(void)fd_set_offset(table, descriptor, before);
			if (written == 0)
				syscall_error(frame, EIO);
			break;
		}
		record_length = (offsetof(struct os_dirent_record, name) +
		    name_length + 1U + OS_DIRENT_ALIGNMENT - 1U) &
		    ~(size_t)(OS_DIRENT_ALIGNMENT - 1U);
		if (record_length > UINT16_MAX || record_length > capacity - written) {
			(void)fd_set_offset(table, descriptor, before);
			if (written == 0)
				syscall_error(frame, EINVAL);
			break;
		}
		for (index = 0; index < record_length; index++)
			output[written + index] = 0;
		record = (struct os_dirent_record *)(void *)(output + written);
		record->inode = entry.inode;
		record->offset = (int64_t)after;
		record->record_length = (uint16_t)record_length;
		record->type = type;
		for (index = 0; index <= name_length; index++)
			record->name[index] = entry.name[index];
		written += record_length;
	}
	if (written != 0 && !vmm_copy_to_user(vmm_active_address_space(),
	    frame->rsi, output, written)) {
		/* The full range was validated above; retain position on any fault. */
		(void)fd_set_offset(table, descriptor, start_offset);
		syscall_error(frame, EFAULT);
		return frame;
	}
	if ((frame->rflags & 1U) == 0)
		frame->rax = written;
	return frame;
}

struct interrupt_frame *
syscall_file_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;
	struct fd_table *table;

	switch (number) {
	case SYSCALL_MOUNT:
	case SYSCALL_UNMOUNT: {
		struct os_credentials credentials;
		char source[VFS_PATH_MAX], target[VFS_PATH_MAX];
		if (!process_current_credentials(&credentials) ||
		    credentials.effective_uid != 0) {
			syscall_error(frame, 1);
			break;
		}
		int result;
		if (number == SYSCALL_MOUNT) {
			result = frame->rdx ? -EINVAL
					    : copy_user_string(frame->rdi,
						  source, sizeof(source));
			if (!result)
				result = copy_user_string(
				    frame->rsi, target, sizeof(target));
			if (!result)
				result = process_resolve_path(
				    source, source, sizeof(source));
			if (!result)
				result = process_resolve_path(
				    target, target, sizeof(target));
			if (!result)
				result = filesystem_mount(source, target);
		} else {
			result = frame->rsi ? -EINVAL
					    : copy_user_string(frame->rdi,
						  target, sizeof(target));
			if (!result)
				result = process_resolve_path(
				    target, target, sizeof(target));
			if (!result)
				result = filesystem_unmount(target);
		}
		if (result)
			syscall_error(frame, errno_from_kernel(result));
		else
			frame->rax = 0;
	} break;
	case SYSCALL_FSYNC: {
		struct fd_object *object =
		    fd_object_for_descriptor(current_fd_table(), frame->rdi);
		int result = !object ? -EBADF
		    : !object->operations->sync
		    ? -EINVAL
		    : object->operations->sync(object->context);
		if (result)
			syscall_error(frame, errno_from_kernel(result));
		else
			frame->rax = 0;
	} break;
	case SYSCALL_SYNC: {
		int result = filesystem_sync_all();
		if (result)
			syscall_error(frame, errno_from_kernel(result));
		else
			frame->rax = 0;
	} break;
	case SYSCALL_CHMOD:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			struct os_credentials credentials;
			int result;
			if (frame->rsi > UINT32_MAX) {
				syscall_error(frame, EINVAL);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_resolve_path(path, canonical,
				    sizeof(canonical));
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ESRCH;
			if (result == 0)
				result = vfs_chmod_as(canonical, (uint32_t)frame->rsi,
				    credentials.effective_uid, credentials.effective_gid);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_MKDIR:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			struct os_credentials credentials;
			struct vnode *created = NULL;
			int result;
			if (frame->rsi > 07777U) {
				syscall_error(frame, EINVAL);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_resolve_path(path, canonical,
				    sizeof(canonical));
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ESRCH;
			if (result == 0)
				result = vfs_create_path_as(canonical, VNODE_DIRECTORY,
				    (uint32_t)frame->rsi & ~process_get_umask(),
				    credentials.effective_uid,
				    credentials.effective_gid, &created);
			if (created != NULL)
				vnode_put(created);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UNLINK:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			struct os_credentials credentials;
			int result;
			if (frame->rsi > 1U) {
				syscall_error(frame, EINVAL);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0 && vfs_path_has_dot_basename(path))
				result = frame->rsi != 0 ? -EINVAL : -EISDIR;
			if (result == 0) result = process_resolve_path(path, canonical,
			    sizeof(canonical));
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ESRCH;
			if (result == 0) result = vfs_remove_path_as(canonical,
			    frame->rsi != 0, credentials.effective_uid,
			    credentials.effective_gid);
			if (result != 0) syscall_error(frame, errno_from_kernel(result));
			else frame->rax = 0;
		}
		break;
	case SYSCALL_ACCESS:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			char base[VFS_PATH_MAX];
			struct os_credentials credentials;
			struct vnode *node = NULL;
			unsigned int requested;
			int result;
			if (frame->rsi > 7U) {
				syscall_error(frame, EINVAL);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ESRCH;
			if (result == 0 && path[0] != '/') {
				result = process_getcwd(base, sizeof(base));
				if (result > 0)
					result = 0;
			}
			if (result == 0)
				result = vfs_resolve_path_as(path[0] == '/' ? "/" : base,
				    path, canonical, sizeof(canonical),
				    credentials.real_uid, credentials.real_gid);
			if (result == 0)
				result = vfs_lookup_as(canonical, credentials.real_uid,
				    credentials.real_gid, &node);
			requested = ((frame->rsi & 4U) ? VFS_ACCESS_READ : 0U) |
			    ((frame->rsi & 2U) ? VFS_ACCESS_WRITE : 0U) |
			    ((frame->rsi & 1U) ? VFS_ACCESS_EXEC : 0U);
			if (result == 0 && !vfs_access(node, credentials.real_uid,
			    credentials.real_gid, requested))
				result = -13;
			if (node != NULL)
				vnode_put(node);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_FCHMOD:
		{
			struct os_credentials credentials;
			struct fd_metadata metadata;
			struct fd_object *object;
			int result;
			if (frame->rsi > UINT32_MAX) {
				syscall_error(frame, EINVAL);
				break;
			}
			object = fd_object_for_descriptor(current_fd_table(),
			    frame->rdi);
			result = object == NULL ? -EBADF :
			    fd_object_stat(object, &metadata);
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ESRCH;
			if (result == 0) {
				if (credentials.effective_uid != 0 &&
				    credentials.effective_uid != metadata.uid)
					result = -13;
				else if ((uint32_t)frame->rsi & 02000U &&
				    credentials.effective_uid != 0 &&
				    credentials.effective_gid != metadata.gid)
					result = -13;
				else
					result = fd_object_chmod(object,
					    (uint32_t)frame->rsi);
			}
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_CHOWN:
	case SYSCALL_FCHOWN:
		{
			struct os_credentials credentials;
			int result;
			if (frame->rsi > UINT32_MAX || frame->rdx > UINT32_MAX) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!process_current_credentials(&credentials)) {
				syscall_error(frame, ESRCH);
				break;
			}
			if (credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			if (number == SYSCALL_FCHOWN) {
				struct fd_object *object = fd_object_for_descriptor(
				    current_fd_table(), frame->rdi);
				result = fd_object_chown(object, (uint32_t)frame->rsi,
				    (uint32_t)frame->rdx);
			} else {
				char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
				result = copy_user_string(frame->rdi, path,
				    sizeof(path));
				if (result == 0)
					result = process_resolve_path(path, canonical,
					    sizeof(canonical));
				if (result == 0)
					result = vfs_chown_as(canonical,
					    (uint32_t)frame->rsi, (uint32_t)frame->rdx,
					    credentials.effective_uid,
					    credentials.effective_gid);
			}
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_CHDIR:
		{
			char path[VFS_PATH_MAX];
			int result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_chdir(path);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_GETCWD:
		{
			char path[VFS_PATH_MAX];
			int length;
			if (frame->rsi == 0) {
				syscall_error(frame, EINVAL);
				break;
			}
			length = process_getcwd(path, frame->rsi > sizeof(path) ?
			    sizeof(path) : (size_t)frame->rsi);
			if (length < 0) {
				syscall_error(frame, (uint64_t)-length);
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rdi,
			    path, (size_t)length)) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_OPENAT:
		{
			char path[VFS_PATH_MAX];
			char canonical[VFS_PATH_MAX];
			struct fd_object *file;
			struct os_credentials credentials;
			uint64_t flags = frame->rdx;
			uint64_t mode = frame->r10;
			int result = copy_user_string(frame->rsi, path, sizeof(path));

			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			if ((int64_t)frame->rdi != OS_AT_FDCWD && path[0] != '/')
				result = 0;
			else result = process_resolve_path(path, canonical, sizeof(canonical));
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (flags > UINT32_MAX || mode > UINT32_MAX) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!process_current_credentials(&credentials)) {
				syscall_error(frame, ESRCH);
				break;
			}
			/* The process owns its FD table throughout this syscall. Check
			 * capacity before VFS can create or truncate the requested file. */
			table = current_fd_table();
			result = fd_find_free_descriptor(table);
			if (result < 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if ((int64_t)frame->rdi != OS_AT_FDCWD && path[0] != '/') {
				struct vnode *directory = NULL;
				result = fd_object_lookup_at_as(fd_object_for_descriptor(table,frame->rdi),
				    ".",credentials.effective_uid,credentials.effective_gid,&directory);
				if (!result) {
					result = vfs_open_from_as(directory,path,(uint32_t)flags,
					    (uint32_t)mode & ~process_get_umask(),credentials.effective_uid,
					    credentials.effective_gid,&file);
					vnode_put(directory);
				}
			} else result = vfs_open_as(canonical,(uint32_t)flags,
			    (uint32_t)mode & ~process_get_umask(),credentials.effective_uid,
			    credentials.effective_gid,&file);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			result = fd_install(table, file);
			if (result < 0) {
				fd_object_put(file);
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (flags & OS_O_CLOEXEC)
				(void)fd_set_descriptor_flags(table, result, OS_FD_CLOEXEC);
			frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_FSTAT:
		{
			struct fd_metadata metadata;
			struct os_stat value;
			int result = fd_object_stat(fd_object_for_descriptor(
			    current_fd_table(), frame->rdi), &metadata);

			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			result = stat_from_metadata(&value, &metadata);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rsi, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rsi,
			    &value, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_FSTATAT:
		{
			char path[VFS_PATH_MAX];
			char base_path[VFS_PATH_MAX];
			struct fd_object *directory = NULL;
			struct vnode *node = NULL;
			struct fd_metadata metadata;
			struct os_stat value;
			struct os_credentials credentials;
			int64_t directory_fd = (int64_t)frame->rdi;
			uint32_t flags;
			int result;

			if (frame->r10 > UINT32_MAX) {
				syscall_error(frame, EINVAL);
				break;
			}
			flags = (uint32_t)frame->r10;
			/* No vnode type represents a symlink yet, so no-follow
			 * and ordinary lookup are equivalent. Keep all other
			 * flags rejected. */
			if ((flags & ~NATIVE_AT_SYMLINK_NOFOLLOW) != 0) {
				syscall_error(frame, EINVAL);
				break;
			}
			result = copy_user_string(frame->rsi, path, sizeof(path));
			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			if (path[0] != '/' && directory_fd != OS_AT_FDCWD) {
				directory = fd_object_for_descriptor(current_fd_table(),
				    frame->rdi);
				if (directory == NULL) {
					syscall_error(frame, EBADF);
					break;
				}
			} else if (path[0] != '/') {
				result = process_getcwd(base_path, sizeof(base_path));
				if (result < 0) {
					syscall_error(frame, (uint64_t)-result);
					break;
				}
			}
			if (!process_current_credentials(&credentials)) {
				syscall_error(frame, ESRCH);
				break;
			}
			if (directory != NULL)
				result = fd_object_lookup_at_as(directory, path,
				    credentials.effective_uid, credentials.effective_gid, &node);
			else
				result = vfs_lookup_at_as(base_path, path,
				    credentials.effective_uid, credentials.effective_gid, &node);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			metadata = (struct fd_metadata){
				.size = node->size,
				.inode = node->inode,
				.mode = node->mode,
				.type = (uint32_t)node->type,
				.uid = node->uid,
				.gid = node->gid
			};
			result = stat_from_metadata(&value, &metadata);
			vnode_put(node);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rdx, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rdx,
			    &value, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_GETDENTS:
		return syscall_getdents(frame);
	default:
		syscall_error(frame, ENOSYS);
		break;
	}
	return frame;
}
