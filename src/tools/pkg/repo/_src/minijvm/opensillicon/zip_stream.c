/* SPDX-License-Identifier: MIT */
#include <zlib.h>
#include <stdlib.h>
#include "jvm_util.h"

struct zip_stream {
    z_stream stream;
    int compress;
};

static s32 create_stream(Runtime *runtime, JClass *clazz)
{
    struct zip_stream *stream = calloc(1, sizeof(*stream));
    if (stream) {
        stream->compress = localvar_getInt(runtime->localvar, 0);
        int bits = localvar_getInt(runtime->localvar, 1);
        int status = stream->compress ?
            deflateInit2(&stream->stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, bits, 8, Z_DEFAULT_STRATEGY) :
            inflateInit2(&stream->stream, bits);
        if (status != Z_OK) {
            free(stream);
            stream = NULL;
        }
    }
    push_long(runtime->stack, (intptr_t) stream);
    return RUNTIME_STATUS_NORMAL;
}

static s32 process_stream(Runtime *runtime, JClass *clazz)
{
    struct zip_stream *stream = (void *) (intptr_t) localvar_getLong(runtime->localvar, 0);
    Instance *input = localvar_getRefer(runtime->localvar, 2);
    int offset = localvar_getInt(runtime->localvar, 3);
    int length = localvar_getInt(runtime->localvar, 4);
    Instance *output = localvar_getRefer(runtime->localvar, 5);
    int output_offset = localvar_getInt(runtime->localvar, 6);
    int output_length = localvar_getInt(runtime->localvar, 7);
    int flush = localvar_getInt(runtime->localvar, 8);
    Instance *progress = localvar_getRefer(runtime->localvar, 9);
    stream->stream.next_in = (Bytef *) input->arr_body + offset;
    stream->stream.avail_in = length;
    stream->stream.next_out = (Bytef *) output->arr_body + output_offset;
    stream->stream.avail_out = output_length;
    int status = stream->compress ? deflate(&stream->stream, flush) : inflate(&stream->stream, flush);
    int *values = (int *) progress->arr_body;
    values[0] = length - stream->stream.avail_in;
    values[1] = output_length - stream->stream.avail_out;
    stream->stream.next_in = stream->stream.next_out = NULL;
    push_int(runtime->stack, status);
    return RUNTIME_STATUS_NORMAL;
}

static s32 reset_stream(Runtime *runtime, JClass *clazz)
{
    struct zip_stream *stream = (void *) (intptr_t) localvar_getLong(runtime->localvar, 0);
    if (stream->compress) deflateReset(&stream->stream);
    else inflateReset(&stream->stream);
    return RUNTIME_STATUS_NORMAL;
}

static s32 release_stream(Runtime *runtime, JClass *clazz)
{
    struct zip_stream *stream = (void *) (intptr_t) localvar_getLong(runtime->localvar, 0);
    if (stream->compress) deflateEnd(&stream->stream);
    else inflateEnd(&stream->stream);
    free(stream);
    return RUNTIME_STATUS_NORMAL;
}

void reg_zip_stream_native_lib(MiniJVM *jvm)
{
    static java_native_method methods[] = {
        {"java/util/zip/ZipStream", "create", "(ZI)J", create_stream},
        {"java/util/zip/ZipStream", "process", "(J[BII[BIII[I)I", process_stream},
        {"java/util/zip/ZipStream", "reset", "(J)V", reset_stream},
        {"java/util/zip/ZipStream", "release", "(J)V", release_stream},
    };
    native_reg_lib(jvm, methods, sizeof(methods) / sizeof(methods[0]));
}
