/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The kernel has no rename syscall. Java File.renameTo does not promise atomicity;
 * support regular files without overwriting an existing destination. */
int minijvm_rename(const char *source, const char *destination)
{
    int input = open(source, O_RDONLY);
    if (input < 0) return -1;
    struct stat info;
    if (fstat(input, &info) || !S_ISREG(info.st_mode) || info.st_size < 0) {
        close(input);
        errno = EOPNOTSUPP;
        return -1;
    }
    if (!strcmp(source, destination)) {
        close(input);
        return 0;
    }
    int output = open(destination, O_WRONLY | O_CREAT | O_EXCL, info.st_mode & 0777);
    if (output < 0) {
        close(input);
        return -1;
    }
    char buffer[4096];
    off_t offset = 0;
    int result = 0;
    while (offset < info.st_size) {
        size_t count = info.st_size - offset < sizeof(buffer) ? info.st_size - offset : sizeof(buffer);
        if (lseek(input, offset, SEEK_SET) != offset) { result = -1; break; }
        ssize_t got = read(input, buffer, count);
        if (got <= 0) { result = -1; break; }
        ssize_t written = 0;
        while (written < got) {
            if (lseek(output, offset + written, SEEK_SET) != offset + written) { result = -1; break; }
            ssize_t sent = write(output, buffer + written, got - written);
            if (sent <= 0) { result = -1; break; }
            written += sent;
        }
        if (result) break;
        offset += got;
    }
    if (close(output)) result = -1;
    close(input);
    if (!result && unlink(source)) result = -1;
    if (result) unlink(destination);
    return result;
}
