#include "ipc_stream.h"
#include <errno.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
bool ipc_stream_is_connected_local(int fd) {
    int kind = 0;
    socklen_t length = sizeof(kind);
    struct sockaddr_un address = {0};
    socklen_t address_size = sizeof(address);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &kind, &length) || kind != SOCK_STREAM ||
            getpeername(fd, (struct sockaddr*)&address, &address_size) || address.sun_family != AF_UNIX) {
        errno = EINVAL; return false;
    }
    return true;
}
#else
bool ipc_stream_is_connected_local(int fd) { (void)fd; errno = ENOTSUP; return false; }
#endif
