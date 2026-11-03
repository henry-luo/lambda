#include "ipc_stream.h"
#include "mem.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

#ifndef _WIN32
#include "time_util.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

struct IpcStream { int fd; };

IpcStream* ipc_stream_adopt(int fd) {
    int kind = 0;
    socklen_t length = sizeof(kind);
    struct sockaddr_un address = {0};
    socklen_t address_size = sizeof(address);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &kind, &length) || kind != SOCK_STREAM ||
            getpeername(fd, (struct sockaddr*)&address, &address_size) || address.sun_family != AF_UNIX) {
        errno = EINVAL; return NULL;
    }
    IpcStream* stream = (IpcStream*)mem_calloc(1, sizeof(*stream), MEM_CAT_SYSTEM);
    if (!stream) { errno = ENOMEM; return NULL; }
    int flags = fcntl(fd, F_GETFL), descriptor_flags = fcntl(fd, F_GETFD);
    int error = flags < 0 || descriptor_flags < 0 ? errno : 0;
    if (!error && (fcntl(fd, F_SETFL, flags | O_NONBLOCK) ||
            fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC))) error = errno;
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    if (!error && setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled))) error = errno;
#endif
    if (error) {
        if (flags >= 0) (void)fcntl(fd, F_SETFL, flags);
        if (descriptor_flags >= 0) (void)fcntl(fd, F_SETFD, descriptor_flags);
        mem_free(stream); errno = error; return NULL;
    }
    stream->fd = fd; return stream;
}
void ipc_stream_interrupt(IpcStream* stream) { if (stream) shutdown(stream->fd, SHUT_RDWR); }
void ipc_stream_close(IpcStream* stream) {
    if (!stream) return;
    close(stream->fd); mem_free(stream);
}

bool ipc_stream_transfer(IpcStream* stream, void* bytes, size_t size,
        bool sending, uint64_t deadline, bool* eof) {
    return ipc_stream_transfer_checked(stream, bytes, size, sending, deadline, eof, NULL, NULL);
}
bool ipc_stream_transfer_checked(IpcStream* stream, void* bytes, size_t size,
        bool sending, uint64_t deadline, bool* eof, IpcStreamTransferCheck check, void* context) {
    if (eof) *eof = false;
    if (!stream || (size && !bytes)) { errno = EINVAL; return false; }
    size_t offset = 0;
    while (offset < size) {
        if (check && !check(context)) { errno = EACCES; return false; }
        uint64_t now = time_now_ms();
        if (now >= deadline) { errno = ETIMEDOUT; return false; }
        uint64_t wait = deadline - now;
        if (check && wait > 25) wait = 25;
        struct pollfd descriptor = {stream->fd, (short)(sending ? POLLOUT : POLLIN), 0};
        int ready = poll(&descriptor, 1, wait > INT_MAX ? INT_MAX : (int)wait);
        if (ready < 0 && errno == EINTR) continue;
        if (!ready) continue; // a clamped poll interval is not the absolute deadline
        if (ready < 0) return false;
        if (check && !check(context)) { errno = EACCES; return false; }
        size_t chunk = size - offset > INT_MAX ? INT_MAX : size - offset;
        if (check && chunk > 64 * 1024) chunk = 64 * 1024;
        ssize_t count;
        if (sending) {
#ifdef MSG_NOSIGNAL
            count = send(stream->fd, (uint8_t*)bytes + offset, chunk, MSG_NOSIGNAL);
#else
            count = send(stream->fd, (uint8_t*)bytes + offset, chunk, 0);
#endif
        } else {
            union { struct cmsghdr alignment; uint8_t bytes[CMSG_SPACE(16 * sizeof(int))]; } control = {0};
            struct iovec vector = {(uint8_t*)bytes + offset, chunk};
            struct msghdr message = {0};
            message.msg_iov = &vector; message.msg_iovlen = 1;
            message.msg_control = control.bytes; message.msg_controllen = sizeof(control.bytes);
            count = recvmsg(stream->fd, &message, 0);
            bool ancillary = (message.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) != 0;
            for (struct cmsghdr* header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
                ancillary = true;
                if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
                        header->cmsg_len >= CMSG_LEN(0)) {
                    size_t descriptors = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                    for (size_t i = 0; i < descriptors; i++) {
                        int fd; memcpy(&fd, CMSG_DATA(header) + i * sizeof(int), sizeof(fd)); close(fd);
                    }
                }
            }
            if (count >= 0 && ancillary) { errno = EPROTO; return false; }
        }
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) {
            if (count == 0) {
                if (!sending && !offset && eof) *eof = true;
                errno = ECONNRESET;
            }
            return false;
        }
        offset += (size_t)count;
    }
    return true;
}
#else
IpcStream* ipc_stream_adopt(int fd) { (void)fd; errno = ENOTSUP; return NULL; }
void ipc_stream_interrupt(IpcStream* stream) { (void)stream; }
void ipc_stream_close(IpcStream* stream) { (void)stream; }
bool ipc_stream_transfer(IpcStream* stream, void* bytes, size_t size,
        bool sending, uint64_t deadline, bool* eof) {
    (void)stream; (void)bytes; (void)size; (void)sending; (void)deadline;
    if (eof) *eof = false;
    errno = ENOTSUP; return false;
}
bool ipc_stream_transfer_checked(IpcStream* stream, void* bytes, size_t size,
        bool sending, uint64_t deadline, bool* eof, IpcStreamTransferCheck check, void* context) {
    (void)check; (void)context;
    return ipc_stream_transfer(stream, bytes, size, sending, deadline, eof);
}
#endif
