#include "../../lambda/host/safety/worker.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Native attempts bypass the runtime's client, in the same executable that
// subsequently runs the real Lambda entry through broker IPC.
int main(int argc, char** argv) {
    if (argc != 8) return 2;
    char* end = nullptr;
    long fd_number = strtol(argv[1], &end, 10);
    if (!end || *end || fd_number < 3 || fd_number > 1048576) return 2;
    unsigned long long session = strtoull(argv[2], &end, 10);
    if (!end || *end || !session || argv[2][0] == '-') return 2;
    long port = strtol(argv[7], &end, 10);
    if (!end || *end || port <= 0 || port > 65535) return 2;
    int errors[4] = {};
    unsigned unexpected = 0;
    for (int candidate = 3; candidate < 1024; candidate++)
        if (candidate != fd_number && fcntl(candidate, F_GETFD) >= 0) unexpected++;
    int fd = open(argv[4], O_RDONLY | O_NOFOLLOW); errors[0] = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    fd = open(argv[5], O_WRONLY | O_NOFOLLOW); errors[1] = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    fd = open(argv[6], O_CREAT | O_EXCL | O_WRONLY, 0600); errors[2] = fd < 0 ? errno : 0;
    if (fd >= 0) { close(fd); unlink(argv[6]); }
    fd = socket(AF_INET, SOCK_STREAM, 0); errors[3] = fd < 0 ? errno : 0;
    if (fd >= 0) {
        sockaddr_in address = {}; address.sin_family = AF_INET;
        address.sin_port = htons((uint16_t)port); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (sockaddr*)&address, sizeof(address))) errors[3] = errno;
        close(fd);
    }
    char report[256];
    int size = snprintf(report, sizeof(report),
        "{\"read_errno\":%d,\"write_errno\":%d,\"create_errno\":%d,\"connect_errno\":%d,\"unexpected_fds\":%u}\n",
        errors[0], errors[1], errors[2], errors[3], unexpected);
    if (size <= 0 || (size_t)size >= sizeof(report) || write(STDOUT_FILENO, report, (size_t)size) != size) return 2;
    return lambda_safety_worker_run((int)fd_number, (uint64_t)session, argv[3]);
}
