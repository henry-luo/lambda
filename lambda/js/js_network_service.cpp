#include "js_network_service.h"
#include "js_runtime.h"

#include "js_event_loop.h"
#include "../jube/jube_registry.h"
#include "../../lib/mem.h"
#include "../../lib/uv_loop.h"

#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define JUBE_NODE_STREAM_TCP_MAGIC 0x4A535443u

struct JubeNodeStreamTcp {
    uv_tcp_t handle;
    uint32_t magic;
    bool initialized;
    bool close_started;
};

static void js_node_stream_tcp_close_complete(uv_handle_t* handle) {
    JubeNodeStreamTcp* stream = handle ? (JubeNodeStreamTcp*)handle->data : NULL;
    if (stream) mem_free(stream);
}

static void js_node_stream_tcp_close(void* user) {
    JubeNodeStreamTcp* stream = (JubeNodeStreamTcp*)user;
    if (!stream || stream->magic != JUBE_NODE_STREAM_TCP_MAGIC || stream->close_started) return;
    stream->close_started = true;
    if (!stream->initialized) {
        mem_free(stream);
        return;
    }
    // A resource slot releases its JS root during detach. Keep the native TCP
    // allocation alive until libuv acknowledges close, otherwise its callback
    // can dereference freed handle storage on the next loop turn.
    if (!uv_is_closing((uv_handle_t*)&stream->handle)) {
        uv_close((uv_handle_t*)&stream->handle, js_node_stream_tcp_close_complete);
    }
}

int js_node_stream_tcp_create(void* session, Item owner, uint32_t* out_resource_id) {
    if (out_resource_id) *out_resource_id = 0;
    if (!session || !owner.item || !out_resource_id) return UV_EINVAL;
    JubeNodeStreamTcp* stream = (JubeNodeStreamTcp*)mem_calloc(1, sizeof(JubeNodeStreamTcp),
        MEM_CAT_JS_RUNTIME);
    if (!stream) return UV_ENOMEM;
    stream->magic = JUBE_NODE_STREAM_TCP_MAGIC;
    uint32_t resource_id = jube_node_resource_add_with_close(session, owner, "TCPSocketWrap",
        js_node_stream_tcp_close, stream);
    if (!resource_id) {
        js_node_stream_tcp_close(stream);
        return UV_EINVAL;
    }
    *out_resource_id = resource_id;
    return 0;
}

static JubeNodeStreamTcp* js_node_stream_tcp_from_resource(void* session, uint32_t resource_id) {
    JubeNodeStreamTcp* stream = (JubeNodeStreamTcp*)
        jube_node_resource_user_data_for_session(session, resource_id);
    return stream && stream->magic == JUBE_NODE_STREAM_TCP_MAGIC ? stream : NULL;
}

static bool js_node_stream_tcp_has_live_listener(const struct sockaddr* address, int address_length) {
    if (!address || address_length <= 0) return false;
#if defined(_WIN32)
    (void)address_length;
    return false;
#else
    int descriptor = socket(address->sa_family, SOCK_STREAM, 0);
    if (descriptor < 0) return false;
    int status = connect(descriptor, address, (socklen_t)address_length);
    close(descriptor);
    return status == 0;
#endif
}

int js_node_stream_tcp_bind(void* session, uint32_t resource_id, const char* address,
        int port, bool ipv6_only, bool reuse_port) {
    JubeNodeStreamTcp* stream = js_node_stream_tcp_from_resource(session, resource_id);
    if (!stream || stream->close_started || !address || !address[0] || port < 0 || port > 65535) {
        return UV_EINVAL;
    }
    struct sockaddr_storage socket_address = {};
    int flags = 0;
#ifdef UV_TCP_REUSEPORT
    if (reuse_port) flags |= UV_TCP_REUSEPORT;
#else
    if (reuse_port) return UV_ENOSYS;
#endif
    int address_length = 0;
    int status = uv_ip4_addr(address, port, (struct sockaddr_in*)&socket_address);
    if (status == 0) {
        address_length = (int)sizeof(struct sockaddr_in);
    } else {
        status = uv_ip6_addr(address, port, (struct sockaddr_in6*)&socket_address);
        if (status != 0) return status;
        if (ipv6_only) flags |= UV_TCP_IPV6ONLY;
        address_length = (int)sizeof(struct sockaddr_in6);
    }
    // uv_tcp_bind may permit a second non-reuse listener on some platforms;
    // probe the endpoint so BoundSocket preserves Node's EADDRINUSE contract.
    if (!reuse_port && port > 0 && js_node_stream_tcp_has_live_listener(
            (const struct sockaddr*)&socket_address, address_length)) {
        return UV_EADDRINUSE;
    }
    if (!stream->initialized) {
        // A hosted transport leaf may run before console/timer setup. Use the
        // full JS loop initializer so libuv's task checkpoints and loop state
        // are established independently of the caller's stdout mode.
        js_event_loop_init();
        int loop_status = lambda_uv_init();
        if (loop_status != 0) return loop_status;
        uv_loop_t* loop = lambda_uv_loop();
        if (!loop) return UV_EINVAL;
        status = uv_tcp_init_ex(loop, &stream->handle, socket_address.ss_family);
        if (status != 0) return status;
        stream->handle.data = stream;
        stream->initialized = true;
        uv_unref((uv_handle_t*)&stream->handle);
    }
    return uv_tcp_bind(&stream->handle, (const struct sockaddr*)&socket_address,
        (unsigned int)flags);
}

int js_node_tcp_handle_address(uv_tcp_t* handle, char* address, size_t address_size,
                               int* out_port, int* out_family) {
    if (address && address_size > 0) address[0] = '\0';
    if (out_port) *out_port = 0;
    if (out_family) *out_family = 0;
    if (!handle || !address || address_size == 0) return UV_EINVAL;
    struct sockaddr_storage socket_address = {};
    int address_length = (int)sizeof(socket_address);
    int status = uv_tcp_getsockname(handle, (struct sockaddr*)&socket_address, &address_length);
    if (status != 0) return status;
    if (socket_address.ss_family == AF_INET) {
        const struct sockaddr_in* ipv4 = (const struct sockaddr_in*)&socket_address;
        if (uv_ip4_name(ipv4, address, address_size) != 0) return UV_EINVAL;
        if (out_port) *out_port = ntohs(ipv4->sin_port);
        if (out_family) *out_family = 4;
        return 0;
    }
    if (socket_address.ss_family == AF_INET6) {
        const struct sockaddr_in6* ipv6 = (const struct sockaddr_in6*)&socket_address;
        if (uv_ip6_name(ipv6, address, address_size) != 0) return UV_EINVAL;
        if (out_port) *out_port = ntohs(ipv6->sin6_port);
        if (out_family) *out_family = 6;
        return 0;
    }
    return UV_EAFNOSUPPORT;
}

Item js_node_tcp_server_address(uv_tcp_t* handle) {
    char address[128];
    int family = 0;
    int port = 0;
    if (js_node_tcp_handle_address(handle, address, sizeof(address), &port, &family) != 0) {
        return ItemNull;
    }
    Item result = js_new_object();
    js_set_key_default(result, make_string_item("address"), make_string_item(address));
    js_set_key_default(result, make_string_item("family"),
        make_string_item(family == 6 ? "IPv6" : "IPv4"));
    js_set_key_default(result, make_string_item("port"), (Item){.item = i2it(port)});
    return result;
}

int js_node_stream_tcp_address(void* session, uint32_t resource_id, char* address,
        size_t address_size, int* out_port, int* out_family) {
    if (address && address_size > 0) address[0] = '\0';
    if (out_port) *out_port = 0;
    if (out_family) *out_family = 0;
    JubeNodeStreamTcp* stream = js_node_stream_tcp_from_resource(session, resource_id);
    if (!stream || !stream->initialized || stream->close_started) return UV_EINVAL;
    return js_node_tcp_handle_address(&stream->handle, address, address_size,
        out_port, out_family);
}

int js_node_stream_tcp_fd(void* session, uint32_t resource_id, int* out_fd) {
    if (out_fd) *out_fd = -1;
    JubeNodeStreamTcp* stream = js_node_stream_tcp_from_resource(session, resource_id);
    if (!stream || !stream->initialized || stream->close_started || !out_fd) return UV_EINVAL;
#if defined(_WIN32)
    // libuv exposes Windows sockets through HANDLE-typed uv_os_fd_t, not int fds.
    return UV_ENOSYS;
#else
    uv_os_fd_t descriptor = -1;
    int status = uv_fileno((const uv_handle_t*)&stream->handle, &descriptor);
    if (status == 0) *out_fd = (int)descriptor;
    return status;
#endif
}

int js_node_stream_tcp_adopt_fd(void* session, uint32_t resource_id, int* out_fd) {
    if (out_fd) *out_fd = -1;
    JubeNodeStreamTcp* stream = js_node_stream_tcp_from_resource(session, resource_id);
    if (!stream || !stream->initialized || stream->close_started || !out_fd) return UV_EINVAL;
#if defined(_WIN32)
    return UV_ENOSYS;
#else
    uv_os_fd_t descriptor = -1;
    int status = uv_fileno((const uv_handle_t*)&stream->handle, &descriptor);
    if (status != 0) return status;
    int duplicate = dup((int)descriptor);
    if (duplicate < 0) return UV_EBADF;
    // Adoption consumes the original resource. Release its root only after
    // duplication so the recipient owns an independent descriptor.
    jube_node_resource_remove_for_session(session, resource_id);
    *out_fd = duplicate;
    return 0;
#endif
}

int js_node_stream_resource_close(void* session, uint32_t resource_id) {
    if (!js_node_stream_tcp_from_resource(session, resource_id)) return UV_EINVAL;
    jube_node_resource_remove_for_session(session, resource_id);
    return 0;
}

int js_node_stream_resource_ref(void* session, uint32_t resource_id, bool referenced) {
    JubeNodeStreamTcp* stream = js_node_stream_tcp_from_resource(session, resource_id);
    if (!stream || !stream->initialized || stream->close_started) return UV_EINVAL;
    if (referenced) {
        uv_ref((uv_handle_t*)&stream->handle);
    } else {
        uv_unref((uv_handle_t*)&stream->handle);
    }
    return 0;
}

bool js_node_stream_resource_is_live(void* session, uint32_t resource_id) {
    JubeNodeStreamTcp* stream = js_node_stream_tcp_from_resource(session, resource_id);
    return stream && stream->initialized && !stream->close_started;
}

int js_node_network_ip_family(const char* address) {
    if (!address || !address[0]) return 0;
    struct in_addr ipv4 = {};
    if (inet_pton(AF_INET, address, &ipv4) == 1) return 4;
    struct in6_addr ipv6 = {};
    return inet_pton(AF_INET6, address, &ipv6) == 1 ? 6 : 0;
}

bool js_node_network_lookup_sync(const char* hostname, char* address, size_t address_size) {
    if (!hostname || !hostname[0] || !address || address_size == 0) return false;
    address[0] = '\0';
    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result_info = NULL;
    if (getaddrinfo(hostname, NULL, &hints, &result_info) != 0 || !result_info) {
        if (result_info) freeaddrinfo(result_info);
        return false;
    }
    const void* source = NULL;
    int family = result_info->ai_family;
    if (family == AF_INET) {
        source = &((const struct sockaddr_in*)result_info->ai_addr)->sin_addr;
    } else if (family == AF_INET6) {
        source = &((const struct sockaddr_in6*)result_info->ai_addr)->sin6_addr;
    }
    bool success = source && inet_ntop(family, source, address, address_size) != NULL;
    freeaddrinfo(result_info);
    return success;
}
