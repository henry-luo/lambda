#pragma once
#include <stddef.h>

// shared nonblocking socket mechanics; protocol and task ownership stay in rt.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NetworkSocket;
#define NETWORK_INVALID_SOCKET INVALID_SOCKET
static inline void network_socket_close(NetworkSocket s) { closesocket(s); }
static inline bool network_socket_nonblocking(NetworkSocket s) {
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
}
static inline bool network_socket_would_block(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static inline bool network_sockets_ready(void) {
    static bool ready = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    return ready;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NetworkSocket;
#define NETWORK_INVALID_SOCKET (-1)
static inline void network_socket_close(NetworkSocket s) { close(s); }
static inline bool network_socket_nonblocking(NetworkSocket s) {
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
static inline bool network_socket_would_block(void) {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
static inline bool network_sockets_ready(void) { return true; }
#endif

static inline void network_socket_no_sigpipe(NetworkSocket s) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&on, sizeof(on));
#else
    (void)s;
#endif
}

static inline long network_socket_send(NetworkSocket s, const char* bytes, size_t size) {
#ifdef MSG_NOSIGNAL
    return (long)send(s, bytes, (int)size, MSG_NOSIGNAL);
#else
    return (long)send(s, bytes, (int)size, 0);
#endif
}
