#include "../../lib/confined_process.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 4 || strcmp(argv[1], "3") || !confined_process_worker_limits()) return 2;
    const char* mode = strrchr(argv[3], '/'); mode = mode ? mode + 1 : argv[3];
    if (!strcmp(mode, "wall.ls") || !strcmp(mode, "disconnect.ls")) {
        if (!strcmp(mode, "disconnect.ls")) close(3);
        sleep(10); return 0;
    }
    if (!strcmp(mode, "cpu.ls")) { volatile unsigned value = 0; while (true) value++; }
    if (!strcmp(mode, "memory.ls")) {
        size_t size = 128 * 1024 * 1024;
        volatile char* memory = (volatile char*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (memory == MAP_FAILED) return 2;
        for (size_t i = 0; i < size; i += 4096) memory[i] = 1;
        sleep(10); return 0;
    }
    if (!strcmp(mode, "output.ls")) {
        char bytes[4096] = {};
        while (write(1, bytes, sizeof(bytes)) > 0) {}
        return 0;
    }
    if (!strcmp(mode, "forged.ls")) {
        char frame[32] = {}; (void)write(3, frame, sizeof(frame)); return 0;
    }
    unsigned unexpected = 0;
    for (int fd = 4; fd < 1024; fd++) if (fcntl(fd, F_GETFD) >= 0) unexpected++;
    int fd = open("/dev/tty", O_RDONLY | O_NONBLOCK); int tty_error = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    fd = open(argv[3], O_RDONLY); int read_error = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    int net_error = 0;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) net_error = errno;
    else {
        sockaddr_in address = {}; address.sin_family = AF_INET; address.sin_port = htons(9);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (sockaddr*)&address, sizeof(address))) net_error = errno;
        close(fd);
    }
    pid_t child = fork(); int fork_error = child < 0 ? errno : 0;
    if (!child) _exit(0);
    if (child > 0) waitpid(child, nullptr, 0);
    char json[384];
    const char* home = getenv("HOME");
    bool container_home = home && strstr(home, "/Library/Containers/org.lambda-script.safety-worker/Data");
    int length = snprintf(json, sizeof(json),
        "{\"unexpected_fds\":%u,\"fresh_session\":%s,\"clean_env\":%s,\"system_container_home\":%s,\"tty_errno\":%d,"
        "\"read_errno\":%d,\"network_errno\":%d,\"fork_errno\":%d}\n", unexpected,
        getsid(0) == getpid() ? "true" : "false",
        !getenv("SAFETY_SENTINEL") && (!home || container_home) && !getenv("LAMBDA_HOME") && !getenv("DYLD_LIBRARY_PATH") ? "true" : "false",
        container_home ? "true" : "false",
        tty_error, read_error, net_error, fork_error);
    if (length <= 0 || (size_t)length >= sizeof(json)) return 2;
    return write(1, json, length) == length ? 0 : 2;
}
