#pragma once
#ifndef _WIN32
#include "lib/time_util.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// Bounded observation and owned reaping for host crash/restart fixtures.
struct SafetyChild {
    SafetyChild() = default;
    SafetyChild(const SafetyChild&) = delete;
    SafetyChild& operator=(const SafetyChild&) = delete;
    pid_t pid = -1;
    int ready = -1;
    int start(int (*action)(void*, int), void* context) {
        if (pid > 0) return EBUSY;
        int channel[2];
        if (pipe(channel)) return errno;
        pid = fork();
        if (!pid) { close(channel[0]); _exit(action(context, channel[1])); }
        int error = pid < 0 ? errno : 0;
        close(channel[1]);
        if (error) close(channel[0]); else ready = channel[0];
        return error;
    }
    static bool publish(int fd, const void* bytes, size_t size) {
        const char* data = (const char*)bytes;
        while (size) {
            ssize_t count = write(fd, data, size);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            data += count; size -= (size_t)count;
        }
        return true;
    }
    bool receive(void* bytes, size_t size, uint32_t timeout_ms = 5000) {
        char* data = (char*)bytes; uint64_t deadline = time_now_ms() + timeout_ms;
        while (size) {
            uint64_t now = time_now_ms();
            if (now >= deadline) { errno = ETIMEDOUT; return false; }
            struct pollfd descriptor = {ready, POLLIN, 0};
            int result = poll(&descriptor, 1, (int)(deadline - now));
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0) { if (!result) errno = ETIMEDOUT; return false; }
            ssize_t count = read(ready, data, size);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { if (!count) errno = EPIPE; return false; }
            data += count; size -= (size_t)count;
        }
        return true;
    }
    int reap(int* status, bool terminate = false, uint32_t timeout_ms = 5000) {
        if (pid <= 0) return ECHILD;
        if (terminate && kill(pid, SIGKILL) && errno != ESRCH) return errno;
        uint64_t deadline = time_now_ms() + timeout_ms;
        while (true) {
            pid_t observed = waitpid(pid, status, WNOHANG);
            if (observed == pid) { pid = -1; if (ready >= 0) close(ready); ready = -1; return 0; }
            if (observed < 0 && errno != EINTR) { int error = errno; if (error == ECHILD) pid = -1; return error; }
            if (time_now_ms() >= deadline) return ETIMEDOUT;
            struct pollfd descriptor = {-1, 0, 0}; (void)poll(&descriptor, 1, 1);
        }
    }
    ~SafetyChild() {
        if (pid > 0) { int status; (void)reap(&status, true); }
        if (ready >= 0) close(ready);
    }
};
#endif
