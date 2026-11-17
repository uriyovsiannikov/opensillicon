/* SPDX-License-Identifier: MIT */
#include <png.h>
#include "jvm_util.h"

static s32 decode_png(Runtime *runtime, JClass *clazz)
{
    Instance *encoded = localvar_getRefer(runtime->localvar, 0);
    png_image image = {.version = PNG_IMAGE_VERSION};
    Instance *decoded = NULL;
    if (png_image_begin_read_from_memory(&image, encoded->arr_body, encoded->arr_length) &&
        image.width <= 4096 && image.height <= 4096 && image.width && image.height) {
        image.format = PNG_FORMAT_BGRA;
        int count = image.width * image.height;
        decoded = jarray_create_by_type_index(runtime, count + 2, DATATYPE_INT);
        if (decoded) {
            int *pixels = (int *) decoded->arr_body;
            pixels[0] = image.width;
            pixels[1] = image.height;
            if (!png_image_finish_read(&image, NULL, pixels + 2, 0, NULL)) {
                decoded = NULL;
            }
        }
    }
    png_image_free(&image);
    push_ref(runtime->stack, decoded);
    return RUNTIME_STATUS_NORMAL;
}

void reg_imageio_native_lib(MiniJVM *jvm)
{
    static java_native_method methods[] = {
        {"javax/imageio/ImageIO", "decodePNG", "([B)[I", decode_png}
    };
    native_reg_lib(jvm, methods, sizeof(methods) / sizeof(methods[0]));
}
