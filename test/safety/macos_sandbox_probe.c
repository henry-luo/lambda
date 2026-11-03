#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Raw syscalls deliberately bypass Lambda. Pair with an unsandboxed control.
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    char* end = NULL;
    long port = strtol(argv[3], &end, 10);
    if (!end || *end || port <= 0 || port > 65535) return 2;
    int fd = open(argv[1], O_RDONLY | O_NOFOLLOW);
    int read_error = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    fd = open(argv[1], O_WRONLY | O_NOFOLLOW);
    int write_error = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    fd = open(argv[2], O_CREAT | O_EXCL | O_WRONLY, 0600);
    int create_error = fd < 0 ? errno : 0;
    if (fd >= 0) { close(fd); unlink(argv[2]); }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    int connect_error = fd < 0 ? errno : 0;
    if (fd >= 0) {
        struct sockaddr_in address = {0};
        address.sin_family = AF_INET;
        address.sin_port = htons((uint16_t)port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr*)&address, sizeof(address)) != 0) connect_error = errno;
        close(fd);
    }
    char result[256];
    int size = snprintf(result, sizeof(result),
        "{\"read_errno\":%d,\"write_errno\":%d,\"create_errno\":%d,\"connect_errno\":%d}\n",
        read_error, write_error, create_error, connect_error);
    return size > 0 && (size_t)size < sizeof(result) && write(STDOUT_FILENO, result, (size_t)size) == size ? 0 : 3;
}
