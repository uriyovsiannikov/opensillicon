#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int profile = 0, interpreted = 0, first = 1;
    while (first < argc) {
        if (!strcmp(argv[first], "--profile")) profile = 1;
        else if (!strcmp(argv[first], "--interpreter")) interpreted = 1;
        else break;
        ++first;
    }
    if (access("/usr/share/minecraft/client.jar", R_OK) != 0) {
        fputs("minecraft: /usr/share/minecraft/client.jar is missing\n", stderr);
        return 1;
    }
    if (access("/usr/share/minijvm/lwjgl.jar", R_OK) != 0) {
        fputs("minecraft: OpenSillicon LWJGL is missing; install minijvm\n", stderr);
        return 1;
    }
    const char *home = getenv("HOME");
    if (!home || !*home) home = "/tmp";
    size_t size = sizeof("-Duser.home=") + strlen(home);
    char *property = malloc(size);
    char **args = calloc((size_t)argc + 9, sizeof(*args));
    if (!property || !args) {
        fputs("minecraft: out of memory\n", stderr);
        free(property);
        free(args);
        return 1;
    }
    snprintf(property, size, "-Duser.home=%s", home);
    int count = 0;
    args[count++] = "/usr/bin/java";
    args[count++] = "-Xmx512m";
    args[count++] = property;
    if (profile) args[count++] = "-Dopensillicon.profile=1";
    if (interpreted) args[count++] = "-Xint";
    args[count++] = "-cp";
    args[count++] = "/usr/share/minecraft/compat.jar:/usr/share/minijvm/lwjgl.jar:/usr/share/minecraft/client.jar:/usr/share/minecraft/lwjgl-2.9.0.jar:/usr/share/minecraft/lwjgl_util-2.9.0.jar:/usr/share/minecraft/jinput-2.0.5.jar:/usr/share/minecraft/jutils-1.0.0.jar";
    args[count++] = "net.minecraft.client.Minecraft";
    for (int i = first; i < argc; ++i) args[count++] = argv[i];
    execv(args[0], args);
    perror("minecraft: cannot start /usr/bin/java (install minijvm)");
    free(property);
    free(args);
    return 127;
}
