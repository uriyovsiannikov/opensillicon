/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>

#include "jvm_util.h"

void reg_awt_native_lib(MiniJVM *jvm);
void reg_lwjgl_native_lib(MiniJVM *jvm);
void reg_lwjgl_input_native_lib(MiniJVM *jvm);
void reg_imageio_native_lib(MiniJVM *jvm);
void reg_digest_native_lib(MiniJVM *jvm);
void reg_zip_stream_native_lib(MiniJVM *jvm);

static void usage(FILE *stream)
{
    fputs("usage: java [-Dkey=value] [-cp path] [-bootclasspath path] class [args...]\n"
          "       java -version\n", stream);
}

int main(int argc, char **argv)
{
    char *bootclasspath = "/usr/share/minijvm/minijvm_rt.jar";
    char *classpath = ".";
    int i = 1;

    while (i < argc && argv[i][0] == '-') {
        const char *option = argv[i++];
        if (!strcmp(option, "-version")) {
            puts("miniJVM 2021.12 for OpenSillicon (Java 8 bytecode, bundled class library)");
            return 0;
        }
        if (!strcmp(option, "-help") || !strcmp(option, "--help")) {
            usage(stdout);
            return 0;
        }
        if (!strncmp(option, "-D", 2)) {
            if (!option[2] || option[2] == '=') {
                fputs("java: system property name is empty\n", stderr);
                return 2;
            }
            continue;
        }
        if (i == argc) {
            usage(stderr);
            return 2;
        }
        if (!strcmp(option, "-cp") || !strcmp(option, "-classpath"))
            classpath = argv[i++];
        else if (!strcmp(option, "-bootclasspath"))
            bootclasspath = argv[i++];
        else {
            fprintf(stderr, "java: unsupported option %s\n", option);
            return 2;
        }
    }
    if (i == argc) {
        usage(stderr);
        return 2;
    }

    FILE *rt = fopen(bootclasspath, "rb");
    if (!rt) {
        fprintf(stderr, "java: could not open runtime class library %s\n", bootclasspath);
        return 1;
    }
    fclose(rt);

    int class_index = i;
    char *main_class = argv[i++];
    MiniJVM *jvm = jvm_create();
    ArrayList *arguments = arraylist_create(0);
    if (!jvm || !arguments) {
        fputs("java: could not allocate virtual machine\n", stderr);
        return 1;
    }
    for (; i < argc; i++)
        arraylist_push_back(arguments, argv[i]);

    jvm->max_heap_size = MAX_HEAP_SIZE_DEFAULT;
    if (jvm_init(jvm, bootclasspath, classpath) != 0) {
        fputs("java: could not initialize virtual machine\n", stderr);
        return 1;
    }
    reg_awt_native_lib(jvm);
    reg_lwjgl_native_lib(jvm);
    reg_imageio_native_lib(jvm);
    reg_digest_native_lib(jvm);
    reg_zip_stream_native_lib(jvm);
    reg_lwjgl_input_native_lib(jvm);
    char directory[4096] = "";
    if (getcwd(directory, sizeof(directory))) {
        sys_properties_set_c(jvm, "user.dir", directory);
    }
    const char *home = getenv("HOME");
    struct passwd *user = getpwuid(getuid());
    if ((!home || !*home) && user) {
        home = user->pw_dir;
    }
    if (!home || !*home) {
        home = directory;
    }
    if (home && *home) {
        sys_properties_set_c(jvm, "user.home", home);
    }
    for (int option = 1; option < class_index; option++) {
        if (strncmp(argv[option], "-D", 2)) {
            option++; // The classpath options each consume one argument.
            continue;
        }
        char *definition = strdup(argv[option] + 2);
        if (!definition) {
            fputs("java: could not allocate system property\n", stderr);
            return 1;
        }
        char *value = strchr(definition, '=');
        if (value) {
            *value++ = '\0';
        } else {
            value = "";
        }
        sys_properties_set_c(jvm, definition, value);
        free(definition);
    }
    int result = call_main(jvm, main_class, arguments);
    arraylist_destory(arguments);
    jvm_destroy(jvm);
    return result == RUNTIME_STATUS_NORMAL ? 0 : 1;
}
