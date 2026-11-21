/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>

#include "jvm_util.h"
#include "jit.h"
#include "../opensillicon/profile.h"
#include <sys/mman.h>

void reg_awt_native_lib(MiniJVM *jvm);
void reg_lwjgl_native_lib(MiniJVM *jvm);
void reg_lwjgl_input_native_lib(MiniJVM *jvm);
void reg_imageio_native_lib(MiniJVM *jvm);
void reg_digest_native_lib(MiniJVM *jvm);
void reg_zip_stream_native_lib(MiniJVM *jvm);

static void usage(FILE *stream)
{
    fputs("usage: java [-Xint] [-Xmx<size>] [-Dkey=value] [-cp path] [-bootclasspath path] class [args...]\n"
          "       java -version\n", stream);
}

static int parse_heap_size(const char *text, s64 *result)
{
    /* Leave room for the collector's percentage arithmetic. */
    const uint64_t limit = INT64_MAX / 100;
    uint64_t size = 0, multiplier = 1;
    const char *cursor = text;
    while (*cursor >= '0' && *cursor <= '9') {
        unsigned digit = *cursor++ - '0';
        if (size > (limit - digit) / 10) return -1;
        size = size * 10 + digit;
    }
    if (cursor == text) return -1;
    if (*cursor) {
        switch (*cursor++) {
        case 'k': case 'K': multiplier = 1024; break;
        case 'm': case 'M': multiplier = 1024 * 1024; break;
        case 'g': case 'G': multiplier = 1024 * 1024 * 1024; break;
        default: return -1;
        }
    }
    if (*cursor || size > limit / multiplier || size * multiplier < 1024 * 1024) return -1;
    *result = size * multiplier;
    return 0;
}

int main(int argc, char **argv)
{
    char *bootclasspath = "/usr/share/minijvm/minijvm_rt.jar";
    char *classpath = ".";
    s64 heap_size = MAX_HEAP_SIZE_DEFAULT;
    int i = 1;

    while (i < argc && argv[i][0] == '-') {
        const char *option = argv[i++];
        if (!strcmp(option, "-Xint")) { minijvm_jit_enabled = 0; continue; }
        if (!strcmp(option, "-version")) {
            puts("miniJVM 2021.12 for OpenSillicon (Java 8 bytecode, bundled class library)");
            return 0;
        }
        if (!strcmp(option, "-help") || !strcmp(option, "--help")) {
            usage(stdout);
            return 0;
        }
        if (!strncmp(option, "-Xmx", 4)) {
            if (parse_heap_size(option + 4, &heap_size)) {
                fprintf(stderr, "java: invalid heap size %s (use at least 1m)\n", option + 4);
                return 2;
            }
            continue;
        }
        if (!strncmp(option, "-Dopensillicon.profile.log=", sizeof("-Dopensillicon.profile.log=")-1)) {
            if (setenv("OPEN_SILLICON_JVM_PROFILE_LOG", option+sizeof("-Dopensillicon.profile.log=")-1, 1)) return 1;
        }
        if (!strcmp(option, "-Dopensillicon.profile.methods=1")) {
            if (setenv("OPEN_SILLICON_JVM_PROFILE_METHODS", "1", 1)) return 1;
        }
        if (!strcmp(option, "-Dopensillicon.profile=1")) {
            if (setenv("OPEN_SILLICON_JVM_PROFILE", "1", 1)) {
                perror("java: enabling profile");
                return 1;
            }
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
    jvm_profile_init();
    MiniJVM *jvm = jvm_create();
    ArrayList *arguments = arraylist_create(0);
    if (!jvm || !arguments) {
        fputs("java: could not allocate virtual machine\n", stderr);
        return 1;
    }
    for (; i < argc; i++)
        arraylist_push_back(arguments, argv[i]);

    if (minijvm_jit_enabled) {
        void *probe = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (probe == MAP_FAILED || mprotect(probe, 4096, PROT_READ | PROT_EXEC)) {
            fputs("java: kernel lacks JIT memory support; using interpreter\n", stderr);
            minijvm_jit_enabled = 0;
        }
        if (probe != MAP_FAILED) munmap(probe, 4096);
    }
    fprintf(stderr, "java: execution mode: %s\n", minijvm_jit_enabled ? "JIT (W^X)" : "interpreter");
    jvm->max_heap_size = heap_size;
    if (jvm_init(jvm, bootclasspath, classpath) != 0) {
        fputs("java: could not initialize virtual machine\n", stderr);
        return 1;
    }
#ifdef OPEN_SILLICON
    /* The native allocator maps slabs through the kernel. Waiting ten minutes
     * between collections retains terrain-generation garbage and makes later
     * allocations increasingly expensive, well before Java's heap is full. */
    jvm->garbage_collect_period_ms = 10000;
#endif
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
        if (!strncmp(argv[option], "-Xmx", 4) || !strcmp(argv[option], "-Xint")) continue;
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
    jvm_profile_flush();
    arraylist_destory(arguments);
    fprintf(stderr, "java: JIT compiled methods: %lu\n", minijvm_jit_compiled);
    jvm_destroy(jvm);
    return result == RUNTIME_STATUS_NORMAL ? 0 : 1;
}
