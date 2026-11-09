// Test-only macOS interposer for publication barrier failures.
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "lib/file.h"

static int target_with_suffix(const char* path, const char* target, const char* suffix) {
    size_t length = strlen(target);
    return !strncmp(path, target, length) && !strcmp(path + length, suffix);
}

static void pause_at_sync_boundary(void) {
    static const char ready[] = "FULLSYNC-PAUSE-READY\n";
    (void)write(STDERR_FILENO, ready, sizeof(ready) - 1);
    for (;;) pause();
}

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
                           sizeof(FILE_ATOMIC_STAGE_PREFIX) - 1)) ||
                 (!strcmp(kind, "sqlite-db") && S_ISREG(info.st_mode) && target_with_suffix(path, target, "")) ||
                 (!strcmp(kind, "sqlite-journal") && S_ISREG(info.st_mode) && target_with_suffix(path, target, "-journal")) ||
                 (!strcmp(kind, "sqlite-wal") && S_ISREG(info.st_mode) && target_with_suffix(path, target, "-wal")))) {
            static unsigned int matched;
            const char* ordinal = getenv("LAMBDA_TEST_FULLSYNC_ORDINAL");
            unsigned int current = __atomic_add_fetch(&matched, 1, __ATOMIC_RELAXED);
            if (ordinal && strtoul(ordinal, NULL, 10) != current)
                return (int)syscall(SYS_fcntl, fd, command);
            const char* release_fd = getenv("LAMBDA_TEST_FULLSYNC_RELEASE_FD");
            if (release_fd) {
                // Hold publication at the sync boundary until the test changes the destination.
                static const char ready[] = "FULLSYNC-PAUSE-READY\n";
                (void)write(STDERR_FILENO, ready, sizeof(ready) - 1);
                char release;
                ssize_t received;
                do { received = read((int)strtol(release_fd, NULL, 10), &release, 1); }
                while (received < 0 && errno == EINTR);
                if (received != 1) { errno = EIO; return -1; }
                return (int)syscall(SYS_fcntl, fd, command);
            }
            if (getenv("LAMBDA_TEST_FULLSYNC_PAUSE")) {
                // The test can interrupt either side of the actual sync syscall.
                if (getenv("LAMBDA_TEST_SYNC_AFTER")) {
                    int result = (int)syscall(SYS_fcntl, fd, command);
                    if (result) return result;
                }
                pause_at_sync_boundary();
            }
            if (getenv("LAMBDA_TEST_FULLSYNC_TRACE")) {
                static const char observed[] = "FULLSYNC-OBSERVED\n";
                (void)write(STDERR_FILENO, observed, sizeof(observed) - 1);
                return (int)syscall(SYS_fcntl, fd, command);
            }
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

static int injected_fsync(int fd) {
    const char* target = getenv("LAMBDA_TEST_FSYNC_DIRECTORY");
    const char* mode = getenv("LAMBDA_TEST_FSYNC_MODE");
    struct stat info;
    char path[1024];
    if (target && mode && !fstat(fd, &info) && S_ISDIR(info.st_mode) &&
            !syscall(SYS_fcntl, fd, F_GETPATH, path) && !strcmp(path, target)) {
        static unsigned int matched;
        unsigned int current = __atomic_add_fetch(&matched, 1, __ATOMIC_RELAXED);
        if (!strcmp(mode, "trace")) {
            static const char seen[] = "FSYNC-DIRECTORY-OBSERVED\n";
            (void)write(STDERR_FILENO, seen, sizeof(seen) - 1);
        } else if (!strcmp(mode, "fail") || !strcmp(mode, "pause")) {
            const char* ordinal = getenv("LAMBDA_TEST_FSYNC_ORDINAL");
            if (current != (ordinal ? strtoul(ordinal, NULL, 10) : 2)) return (int)syscall(SYS_fsync, fd);
            if (!strcmp(mode, "pause")) {
                if (getenv("LAMBDA_TEST_SYNC_AFTER")) {
                    int result = (int)syscall(SYS_fsync, fd);
                    if (result) return result;
                }
                pause_at_sync_boundary();
            }
            static const char failed[] = "FSYNC-DIRECTORY-FAULT\n";
            (void)write(STDERR_FILENO, failed, sizeof(failed) - 1);
            errno = EIO;
            return -1;
        }
    }
    return (int)syscall(SYS_fsync, fd);
}

__attribute__((used)) static struct {
    const void* replacement;
    const void* original;
} interpose_fcntl __attribute__((section("__DATA,__interpose"))) = {
    (const void*)injected_fcntl, (const void*)fcntl
};

__attribute__((used)) static struct {
    const void* replacement;
    const void* original;
} interpose_fsync __attribute__((section("__DATA,__interpose"))) = {
    (const void*)injected_fsync, (const void*)fsync
};
