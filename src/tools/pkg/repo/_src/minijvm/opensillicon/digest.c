/* SPDX-License-Identifier: MIT */
#include <mbedtls/md5.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include "jvm_util.h"

struct digest_state {
    int type;
    union {
        mbedtls_md5_context md5;
        mbedtls_sha1_context sha1;
        mbedtls_sha256_context sha256;
    } context;
};

static void reset_digest(struct digest_state *state)
{
    switch (state->type) {
    case 0: mbedtls_md5_starts(&state->context.md5); break;
    case 1: mbedtls_sha1_starts(&state->context.sha1); break;
    case 2: mbedtls_sha256_starts(&state->context.sha256, 0); break;
    }
}

static s32 create_state(Runtime *runtime, JClass *clazz)
{
    Instance *array = jarray_create_by_type_index(runtime, sizeof(struct digest_state), DATATYPE_BYTE);
    struct digest_state *state = (void *) array->arr_body;
    state->type = localvar_getInt(runtime->localvar, 0);
    switch (state->type) {
    case 0: mbedtls_md5_init(&state->context.md5); break;
    case 1: mbedtls_sha1_init(&state->context.sha1); break;
    case 2: mbedtls_sha256_init(&state->context.sha256); break;
    }
    reset_digest(state);
    push_ref(runtime->stack, array);
    return RUNTIME_STATUS_NORMAL;
}

static s32 update_state(Runtime *runtime, JClass *clazz)
{
    Instance *array = localvar_getRefer(runtime->localvar, 0);
    Instance *input = localvar_getRefer(runtime->localvar, 1);
    int offset = localvar_getInt(runtime->localvar, 2);
    int length = localvar_getInt(runtime->localvar, 3);
    struct digest_state *state = (void *) array->arr_body;
    const unsigned char *bytes = (unsigned char *) input->arr_body + offset;
    switch (state->type) {
    case 0: mbedtls_md5_update(&state->context.md5, bytes, length); break;
    case 1: mbedtls_sha1_update(&state->context.sha1, bytes, length); break;
    case 2: mbedtls_sha256_update(&state->context.sha256, bytes, length); break;
    }
    return RUNTIME_STATUS_NORMAL;
}

static s32 finish_state(Runtime *runtime, JClass *clazz)
{
    Instance *array = localvar_getRefer(runtime->localvar, 0);
    struct digest_state *state = (void *) array->arr_body;
    int length = state->type == 0 ? 16 : state->type == 1 ? 20 : 32;
    Instance *result = jarray_create_by_type_index(runtime, length, DATATYPE_BYTE);
    unsigned char *bytes = (void *) result->arr_body;
    switch (state->type) {
    case 0: mbedtls_md5_finish(&state->context.md5, bytes); break;
    case 1: mbedtls_sha1_finish(&state->context.sha1, bytes); break;
    case 2: mbedtls_sha256_finish(&state->context.sha256, bytes); break;
    }
    reset_digest(state);
    push_ref(runtime->stack, result);
    return RUNTIME_STATUS_NORMAL;
}

static s32 reset_state(Runtime *runtime, JClass *clazz)
{
    Instance *array = localvar_getRefer(runtime->localvar, 0);
    reset_digest((void *) array->arr_body);
    return RUNTIME_STATUS_NORMAL;
}

void reg_digest_native_lib(MiniJVM *jvm)
{
    static java_native_method methods[] = {
        {"java/security/MessageDigest", "createState", "(I)[B", create_state},
        {"java/security/MessageDigest", "updateState", "([B[BII)V", update_state},
        {"java/security/MessageDigest", "finishState", "([B)[B", finish_state},
        {"java/security/MessageDigest", "resetState", "([B)V", reset_state},
    };
    native_reg_lib(jvm, methods, sizeof(methods) / sizeof(methods[0]));
}
