// Test-only macOS interposer for publication barrier failures.
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "../lib/file.h"

static int injected_fcntl(int fd, int command, ...) {
    if (command == F_FULLFSYNC) {
        struct stat info;
        const char* target = getenv("LAMBDA_TEST_FULLSYNC_TARGET");
        const char* kind = getenv("LAMBDA_TEST_FULLSYNC_KIND");
        char path[1024];
        if (target && kind && !fstat(fd, &info) && !syscall(SYS_fcntl, fd, F_GETPATH, path) &&
                ((!strcmp(kind, "directory") && S_ISDIR(info.st_mode) && !strcmp(path, target)) ||
                 (!strcmp(kind, "file") && S_ISREG(info.st_mode) &&
                  !strncmp(path, target, strlen(target)) && path[strlen(target)] == '/' &&
                  !strncmp(path + strlen(target) + 1, FILE_ATOMIC_STAGE_PREFIX,
                           sizeof(FILE_ATOMIC_STAGE_PREFIX) - 1)))) {
            static const char marker[] = "FULLSYNC-FAULT-INJECTED\n";
            (void)write(STDERR_FILENO, marker, sizeof(marker) - 1);
            errno = EIO;
            return -1;
        }
        return (int)syscall(SYS_fcntl, fd, command);
    }
    // Direct syscalls avoid dyld resolving the interposed symbol back to itself.
    if (command == F_GETFD || command == F_GETFL || command == F_GETOWN)
        return (int)syscall(SYS_fcntl, fd, command);
    va_list args;
    va_start(args, command);
    int result;
    if (command == F_GETPATH || command == F_GETLK || command == F_SETLK || command == F_SETLKW)
        result = (int)syscall(SYS_fcntl, fd, command, va_arg(args, void*));
    else if (command == F_SETFD || command == F_SETFL || command == F_SETOWN ||
             command == F_DUPFD || command == F_DUPFD_CLOEXEC)
        result = (int)syscall(SYS_fcntl, fd, command, va_arg(args, int));
    else { errno = EINVAL; result = -1; }
    va_end(args);
    return result;
}

__attribute__((used)) static struct {
    const void* replacement;
    const void* original;
} interpose_fcntl __attribute__((section("__DATA,__interpose"))) = {
    (const void*)injected_fcntl, (const void*)fcntl
};
