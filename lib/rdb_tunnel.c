/**
 * @file rdb_tunnel.c
 * @brief Host TLS bridge for RDB drivers (RDB11). See rdb_tunnel.h.
 *
 * One acceptor thread serves the private Unix socket; each accepted client
 * connection gets a session thread that connects upstream, performs the wire
 * protocol's TLS upgrade, and relays bytes. libpq's cancel request opens a
 * second local connection, so sessions are independent of each other.
 */

#include "rdb_tunnel.h"
#include "log.h"
#include "memtrack.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32

RdbTunnel* rdb_tunnel_open(const RdbTarget* target, const RdbTunnelSpec* spec,
                           char* out_endpoint, size_t cap) {
    (void)target; (void)spec; (void)out_endpoint; (void)cap;
    // AF_UNIX listeners and the relay threads are POSIX-only so far
    log_error("rdb-tunnel: the host TLS bridge is not available on Windows yet");
    return NULL;
}

void rdb_tunnel_close(RdbTunnel* tunnel) { (void)tunnel; }

#else

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0      /* macOS: SO_NOSIGPIPE is set on each socket instead */
#endif

#define RDB_TUNNEL_MAX_SESSIONS 16
#define RDB_TUNNEL_BUF 16384
#define RDB_TUNNEL_CONNECT_MS 15000
#define RDB_MYSQL_CLIENT_SSL 0x0800u
#define RDB_MYSQL_MAX_HANDSHAKE_PACKET 65536

typedef struct RdbTunnelSession RdbTunnelSession;

struct RdbTunnel {
    RdbTarget       target;
    RdbTunnelSpec   spec;
    char            ca_file[1024];
    char            server_name[256];
    char            dir[512];
    char            sock_path[600];
    int             listen_fd;
    int             wake[2];        /* written once at close; never drained */
    pthread_t       acceptor;
    bool            acceptor_started;
    pthread_mutex_t lock;
    RdbTunnelSession* sessions[RDB_TUNNEL_MAX_SESSIONS];
    int             session_count;
};

struct RdbTunnelSession {
    RdbTunnel*               tunnel;
    int                      client_fd;
    int                      server_fd;
    pthread_t                thread;
    bool                     tls;
    bool                     tls_ready;     /* contexts initialised (must be freed) */
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt         ca;
};

/* ══════════════════════════════════════════════════════════════════════
 * Socket helpers
 * ══════════════════════════════════════════════════════════════════════ */

static void tunnel_no_sigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)fd;
#endif
}

static bool fd_write_all(int fd, const uint8_t* buf, size_t n) {
    while (n > 0) {
        ssize_t w = send(fd, buf, n, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        buf += w;
        n -= (size_t)w;
    }
    return true;
}

static bool fd_read_exact(int fd, uint8_t* buf, size_t n) {
    while (n > 0) {
        ssize_t r = recv(fd, buf, n, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        buf += r;
        n -= (size_t)r;
    }
    return true;
}

/** connect with a deadline: non-blocking connect, then back to blocking */
static int tunnel_connect_fd(int family, const struct sockaddr* addr, socklen_t len,
                             int64_t timeout_ms) {
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, addr, len);
    if (rc < 0 && errno == EINPROGRESS) {
        struct pollfd p = { fd, POLLOUT, 0 };
        rc = poll(&p, 1, (int)timeout_ms) == 1 ? 0 : -1;
        int err = 0;
        socklen_t err_len = sizeof(err);
        if (rc == 0 && (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0 || err)) rc = -1;
    }
    if (rc < 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags);
    tunnel_no_sigpipe(fd);
    return fd;
}

static int tunnel_connect_upstream(const RdbTunnel* t) {
    int64_t timeout = t->spec.connect_timeout_ms > 0 ? t->spec.connect_timeout_ms
                                                      : RDB_TUNNEL_CONNECT_MS;
    if (t->target.kind == RDB_PEER_UNIX_SOCKET) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (strlen(t->target.path) >= sizeof(addr.sun_path)) return -1;
        strcpy(addr.sun_path, t->target.path);
        return tunnel_connect_fd(AF_UNIX, (struct sockaddr*)&addr, sizeof(addr), timeout);
    }
    if (t->target.kind != RDB_PEER_TCP) return -1;
    char port[16];
    snprintf(port, sizeof(port), "%d", t->target.port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* list = NULL;
    if (getaddrinfo(t->target.host, port, &hints, &list) != 0) return -1;
    int fd = -1;
    for (struct addrinfo* ai = list; ai && fd < 0; ai = ai->ai_next) {
        fd = tunnel_connect_fd(ai->ai_family, ai->ai_addr, ai->ai_addrlen, timeout);
    }
    freeaddrinfo(list);
    if (fd >= 0) {
        int on = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    }
    return fd;
}

/* ══════════════════════════════════════════════════════════════════════
 * TLS (mbedTLS) over the upstream socket
 * ══════════════════════════════════════════════════════════════════════ */

static int tls_bio_send(void* ctx, const unsigned char* buf, size_t len) {
    ssize_t w = send(*(int*)ctx, buf, len, MSG_NOSIGNAL);
    if (w < 0) return errno == EINTR ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
    return (int)w;
}

static int tls_bio_recv(void* ctx, unsigned char* buf, size_t len) {
    ssize_t r = recv(*(int*)ctx, buf, len, 0);
    if (r < 0) return errno == EINTR ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
    return (int)r;
}

/** verify-ca checks the chain only: a name mismatch is not a failure */
static int tls_verify_chain_only(void* ctx, mbedtls_x509_crt* crt, int depth, uint32_t* flags) {
    (void)ctx; (void)crt; (void)depth;
    *flags &= ~(uint32_t)MBEDTLS_X509_BADCERT_CN_MISMATCH;
    return 0;
}

static void tls_log_error(const RdbTunnel* t, const char* what, int ret) {
    char msg[160];
    mbedtls_strerror(ret, msg, sizeof(msg));
    log_error("rdb-tunnel: %s with %s:%d failed: %s", what, t->target.host, t->target.port, msg);
}

static bool tls_start(RdbTunnelSession* s) {
    RdbTunnel* t = s->tunnel;
    // mbedTLS 3 runs the TLS 1.3 key schedule through PSA; init is idempotent
    if (psa_crypto_init() != PSA_SUCCESS) return false;
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_entropy_init(&s->entropy);
    mbedtls_ctr_drbg_init(&s->drbg);
    mbedtls_x509_crt_init(&s->ca);
    s->tls_ready = true;

    int ret = mbedtls_ctr_drbg_seed(&s->drbg, mbedtls_entropy_func, &s->entropy,
                                    (const unsigned char*)"lambda-rdb", 10);
    if (ret == 0) {
        ret = mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    }
    if (ret != 0) {
        tls_log_error(t, "TLS setup", ret);
        return false;
    }
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->drbg);
    bool verify = t->spec.tls >= RDB_TLS_VERIFY_CA;
    if (verify) {
        if (!t->ca_file[0]) {
            log_error("rdb-tunnel: certificate verification for %s needs a CA bundle", t->target.host);
            return false;
        }
        ret = mbedtls_x509_crt_parse_file(&s->ca, t->ca_file);
        if (ret < 0) {
            tls_log_error(t, "loading the CA bundle", ret);
            return false;
        }
        mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca, NULL);
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        if (t->spec.tls == RDB_TLS_VERIFY_CA) {
            mbedtls_ssl_conf_verify(&s->conf, tls_verify_chain_only, NULL);
        }
    } else {
        // require/prefer encrypt without verifying, as libpq and MySQL clients do
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    ret = mbedtls_ssl_setup(&s->ssl, &s->conf);
    if (ret == 0) {
        ret = mbedtls_ssl_set_hostname(&s->ssl, t->server_name[0] ? t->server_name : t->target.host);
    }
    if (ret != 0) {
        tls_log_error(t, "TLS setup", ret);
        return false;
    }
    mbedtls_ssl_set_bio(&s->ssl, &s->server_fd, tls_bio_send, tls_bio_recv, NULL);
    while ((ret = mbedtls_ssl_handshake(&s->ssl)) != 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        tls_log_error(t, "TLS handshake", ret);
        return false;
    }
    s->tls = true;
    log_debug("rdb-tunnel: TLS %s with %s:%d (%s)", mbedtls_ssl_get_version(&s->ssl),
              t->target.host, t->target.port, mbedtls_ssl_get_ciphersuite(&s->ssl));
    return true;
}

static bool up_write_all(RdbTunnelSession* s, const uint8_t* buf, size_t n) {
    if (!s->tls) return fd_write_all(s->server_fd, buf, n);
    while (n > 0) {
        int w = mbedtls_ssl_write(&s->ssl, buf, n);
        if (w == MBEDTLS_ERR_SSL_WANT_READ || w == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (w <= 0) return false;
        buf += w;
        n -= (size_t)w;
    }
    return true;
}

/** one read from upstream: >0 bytes, 0 when the peer closed, <0 on error */
static int up_read_some(RdbTunnelSession* s, uint8_t* buf, size_t cap) {
    if (!s->tls) {
        ssize_t r;
        do { r = recv(s->server_fd, buf, cap, 0); } while (r < 0 && errno == EINTR);
        return (int)r;
    }
    for (;;) {
        int r = mbedtls_ssl_read(&s->ssl, buf, cap);
        if (r > 0) return r;
        // TLS 1.3 tickets and partial records are not application data
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ||
                r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
        return (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) ? 0 : -1;
    }
}

static bool up_read_exact(RdbTunnelSession* s, uint8_t* buf, size_t n) {
    while (n > 0) {
        int r = up_read_some(s, buf, n);
        if (r <= 0) return false;
        buf += r;
        n -= (size_t)r;
    }
    return true;
}

/* ══════════════════════════════════════════════════════════════════════
 * Wire-protocol upgrades
 * ══════════════════════════════════════════════════════════════════════ */

static bool upgrade_postgres(RdbTunnelSession* s) {
    RdbTunnel* t = s->tunnel;
    if (t->spec.tls == RDB_TLS_DISABLE) return true;
    // SSLRequest: length 8, code 80877103; the server answers 'S' or 'N'
    static const uint8_t ssl_request[8] = { 0, 0, 0, 8, 0x04, 0xd2, 0x16, 0x2f };
    uint8_t answer = 0;
    if (!fd_write_all(s->server_fd, ssl_request, sizeof(ssl_request)) ||
            !fd_read_exact(s->server_fd, &answer, 1)) {
        log_error("rdb-tunnel: %s:%d closed during SSLRequest", t->target.host, t->target.port);
        return false;
    }
    if (answer == 'S') return tls_start(s);
    if (answer == 'N' && t->spec.tls == RDB_TLS_PREFER) return true;
    log_error("rdb-tunnel: %s:%d refused TLS", t->target.host, t->target.port);
    return false;
}

typedef struct {
    uint8_t  header[4];     /* 3-byte length + sequence id */
    uint8_t* payload;
    uint32_t length;
} MysqlPacket;

static uint32_t mysql_packet_length(const uint8_t* h) {
    return (uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16);
}

static bool mysql_read_packet(RdbTunnelSession* s, bool from_server, MysqlPacket* p) {
    bool ok = from_server ? up_read_exact(s, p->header, 4) : fd_read_exact(s->client_fd, p->header, 4);
    if (!ok) return false;
    p->length = mysql_packet_length(p->header);
    if (p->length > RDB_MYSQL_MAX_HANDSHAKE_PACKET) return false;
    p->payload = (uint8_t*)mem_alloc(p->length ? p->length : 1, MEM_CAT_INPUT_OTHER);
    return from_server ? up_read_exact(s, p->payload, p->length)
                       : fd_read_exact(s->client_fd, p->payload, p->length);
}

static bool mysql_write_packet(RdbTunnelSession* s, bool to_server, uint8_t seq,
                               const uint8_t* payload, uint32_t length) {
    uint8_t header[4] = { (uint8_t)length, (uint8_t)(length >> 8), (uint8_t)(length >> 16), seq };
    if (to_server) return up_write_all(s, header, 4) && up_write_all(s, payload, length);
    return fd_write_all(s->client_fd, header, 4) && fd_write_all(s->client_fd, payload, length);
}

/** offset of the low capability word in a protocol-10 server greeting */
static int mysql_greeting_caps_offset(const MysqlPacket* g) {
    if (g->length < 1 || g->payload[0] != 10) return -1;
    const uint8_t* end = (const uint8_t*)memchr(g->payload + 1, 0, g->length - 1);
    if (!end) return -1;
    // version NUL, connection id (4), auth data part 1 (8), filler (1)
    size_t offset = (size_t)(end - g->payload) + 1 + 4 + 8 + 1;
    return offset + 2 <= g->length ? (int)offset : -1;
}

/**
 * MySQL negotiates TLS inside its handshake: the client answers the greeting
 * with a short SSLRequest, upgrades, and only then sends its real response.
 * The local client never sees TLS, so the bridge hides CLIENT_SSL from it,
 * sends the SSLRequest itself, and shifts sequence ids by one in each
 * direction until the server ends authentication with OK or ERR.
 */
static bool upgrade_mysql(RdbTunnelSession* s) {
    RdbTunnel* t = s->tunnel;
    if (t->spec.tls == RDB_TLS_DISABLE) return true;
    MysqlPacket greeting = { {0}, NULL, 0 };
    MysqlPacket response = { {0}, NULL, 0 };
    bool ok = false;
    if (!mysql_read_packet(s, true, &greeting)) goto done;
    int caps_at = mysql_greeting_caps_offset(&greeting);
    if (caps_at < 0) {
        log_error("rdb-tunnel: unexpected MySQL greeting from %s:%d", t->target.host, t->target.port);
        goto done;
    }
    uint16_t server_caps = (uint16_t)(greeting.payload[caps_at] | (greeting.payload[caps_at + 1] << 8));
    if (!(server_caps & RDB_MYSQL_CLIENT_SSL)) {
        if (t->spec.tls >= RDB_TLS_REQUIRE) {
            log_error("rdb-tunnel: %s:%d does not offer TLS", t->target.host, t->target.port);
            goto done;
        }
        ok = mysql_write_packet(s, false, greeting.header[3], greeting.payload, greeting.length);
        goto done;
    }
    greeting.payload[caps_at] &= (uint8_t)~(RDB_MYSQL_CLIENT_SSL & 0xff);
    greeting.payload[caps_at + 1] &= (uint8_t)~(RDB_MYSQL_CLIENT_SSL >> 8);
    if (!mysql_write_packet(s, false, greeting.header[3], greeting.payload, greeting.length) ||
            !mysql_read_packet(s, false, &response) || response.length < 32) {
        goto done;
    }
    // capability flags are the first little-endian word of the response
    response.payload[1] |= (uint8_t)(RDB_MYSQL_CLIENT_SSL >> 8);
    uint8_t seq = response.header[3];
    if (!mysql_write_packet(s, true, seq, response.payload, 32) || !tls_start(s) ||
            !mysql_write_packet(s, true, (uint8_t)(seq + 1), response.payload, response.length)) {
        goto done;
    }
    for (;;) {
        bool buffered = mbedtls_ssl_get_bytes_avail(&s->ssl) > 0;
        struct pollfd fds[3] = {
            { s->client_fd, POLLIN, 0 }, { s->server_fd, POLLIN, 0 }, { t->wake[0], POLLIN, 0 },
        };
        if (!buffered && poll(fds, 3, -1) < 0) {
            if (errno == EINTR) continue;
            goto done;
        }
        if (fds[2].revents) goto done;
        if (buffered || (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            MysqlPacket p = { {0}, NULL, 0 };
            if (!mysql_read_packet(s, true, &p)) { mem_free(p.payload); goto done; }
            bool last = p.length > 0 && (p.payload[0] == 0x00 || p.payload[0] == 0xff);
            bool sent = mysql_write_packet(s, false, (uint8_t)(p.header[3] - 1), p.payload, p.length);
            mem_free(p.payload);
            if (!sent) goto done;
            if (last) break;        // command phase: sequence ids restart, plain relay from here
        } else if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            MysqlPacket p = { {0}, NULL, 0 };
            if (!mysql_read_packet(s, false, &p)) { mem_free(p.payload); goto done; }
            bool sent = mysql_write_packet(s, true, (uint8_t)(p.header[3] + 1), p.payload, p.length);
            mem_free(p.payload);
            if (!sent) goto done;
        }
    }
    ok = true;
done:
    mem_free(greeting.payload);
    mem_free(response.payload);
    return ok;
}

/* ══════════════════════════════════════════════════════════════════════
 * Session: connect, upgrade, relay
 * ══════════════════════════════════════════════════════════════════════ */

/* ══════════════════════════════════════════════════════════════════════
 * PostgreSQL authentication filter
 *
 * Over a TLS upstream the server offers SCRAM-SHA-256-PLUS (channel binding).
 * libpq sees a plaintext socket and rejects that offer as a downgrade, and
 * the binding could not reach it anyway: the host ends the TLS session. Until
 * authentication finishes, the bridge therefore frames server messages and
 * drops the -PLUS mechanism from AuthenticationSASL; afterwards it relays raw.
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    bool     active;
    uint8_t* data;
    size_t   len;
    size_t   cap;
} PgAuthFilter;

static uint32_t pg_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void pg_put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/** AuthenticationSASL without SCRAM-SHA-256-PLUS; returns the new message size */
static size_t pg_strip_channel_binding(uint8_t* msg, size_t size) {
    static const char plus[] = "SCRAM-SHA-256-PLUS";
    size_t in = 9, out = 9;         // type, length, auth code 10
    while (in < size && msg[in]) {
        size_t n = strnlen((const char*)msg + in, size - in) + 1;
        if (!(n == sizeof(plus) && memcmp(msg + in, plus, n) == 0)) {
            memmove(msg + out, msg + in, n);
            out += n;
        }
        in += n;
    }
    msg[out++] = 0;                 // list terminator
    pg_put_be32(msg + 1, (uint32_t)(out - 1));
    return out;
}

/** forward complete server messages; false when the client write fails */
static bool pg_filter_forward(RdbTunnelSession* s, PgAuthFilter* f, const uint8_t* buf, size_t n) {
    if (f->len + n > f->cap) {
        f->cap = (f->len + n) * 2;
        f->data = (uint8_t*)mem_realloc(f->data, f->cap, MEM_CAT_INPUT_OTHER);
    }
    memcpy(f->data + f->len, buf, n);
    f->len += n;
    size_t at = 0;
    while (f->active && f->len - at >= 5) {
        uint8_t* msg = f->data + at;
        size_t size = 1 + (size_t)pg_be32(msg + 1);
        if (f->len - at < size) break;
        size_t out = size;
        if (msg[0] == 'R' && size >= 9) {
            uint32_t code = pg_be32(msg + 5);
            if (code == 10) out = pg_strip_channel_binding(msg, size);
            if (code == 0) f->active = false;       // AuthenticationOk
        } else if (msg[0] == 'E') {
            f->active = false;
        }
        if (!fd_write_all(s->client_fd, msg, out)) return false;
        at += size;
    }
    // whatever follows authentication is relayed unchanged
    bool ok = f->active || fd_write_all(s->client_fd, f->data + at, f->len - at);
    memmove(f->data, f->data + at, f->active ? f->len - at : 0);
    f->len = f->active ? f->len - at : 0;
    return ok;
}

static void session_relay(RdbTunnelSession* s) {
    uint8_t* buf = (uint8_t*)mem_alloc(RDB_TUNNEL_BUF, MEM_CAT_INPUT_OTHER);
    PgAuthFilter filter = { s->tls && s->tunnel->spec.protocol == RDB_WIRE_POSTGRES, NULL, 0, 0 };
    for (;;) {
        bool server_buffered = s->tls && mbedtls_ssl_get_bytes_avail(&s->ssl) > 0;
        struct pollfd fds[3] = {
            { s->client_fd, POLLIN, 0 }, { s->server_fd, POLLIN, 0 }, { s->tunnel->wake[0], POLLIN, 0 },
        };
        if (!server_buffered && poll(fds, 3, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[2].revents) break;
        if (server_buffered || (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            int r = up_read_some(s, buf, RDB_TUNNEL_BUF);
            if (r <= 0) break;
            bool sent = filter.active ? pg_filter_forward(s, &filter, buf, (size_t)r)
                                      : fd_write_all(s->client_fd, buf, (size_t)r);
            if (!sent) break;
            continue;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t r;
            do { r = recv(s->client_fd, buf, RDB_TUNNEL_BUF, 0); } while (r < 0 && errno == EINTR);
            if (r <= 0 || !up_write_all(s, buf, (size_t)r)) break;
        }
    }
    mem_free(filter.data);
    mem_free(buf);
}

static void session_close_fds(RdbTunnelSession* s) {
    pthread_mutex_lock(&s->tunnel->lock);
    if (s->server_fd >= 0) close(s->server_fd);
    if (s->client_fd >= 0) close(s->client_fd);
    s->server_fd = -1;
    s->client_fd = -1;
    pthread_mutex_unlock(&s->tunnel->lock);
}

static void* session_main(void* arg) {
    RdbTunnelSession* s = (RdbTunnelSession*)arg;
    RdbTunnel* t = s->tunnel;
    int fd = tunnel_connect_upstream(t);
    pthread_mutex_lock(&t->lock);
    s->server_fd = fd;
    pthread_mutex_unlock(&t->lock);
    if (fd < 0) {
        log_error("rdb-tunnel: cannot connect to %s:%d", t->target.host, t->target.port);
    } else {
        bool upgraded = t->spec.protocol == RDB_WIRE_POSTGRES ? upgrade_postgres(s) : upgrade_mysql(s);
        if (upgraded) session_relay(s);
        if (s->tls) mbedtls_ssl_close_notify(&s->ssl);
    }
    if (s->tls_ready) {
        mbedtls_ssl_free(&s->ssl);
        mbedtls_ssl_config_free(&s->conf);
        mbedtls_x509_crt_free(&s->ca);
        mbedtls_ctr_drbg_free(&s->drbg);
        mbedtls_entropy_free(&s->entropy);
    }
    // the local client sees EOF; the reason is in the log above
    session_close_fds(s);
    return NULL;
}

static void* acceptor_main(void* arg) {
    RdbTunnel* t = (RdbTunnel*)arg;
    for (;;) {
        struct pollfd fds[2] = { { t->listen_fd, POLLIN, 0 }, { t->wake[0], POLLIN, 0 } };
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[1].revents) break;
        int client = accept(t->listen_fd, NULL, NULL);
        if (client < 0) continue;
        fcntl(client, F_SETFD, FD_CLOEXEC);
        tunnel_no_sigpipe(client);
        pthread_mutex_lock(&t->lock);
        RdbTunnelSession* s = NULL;
        if (t->session_count < RDB_TUNNEL_MAX_SESSIONS) {
            s = (RdbTunnelSession*)mem_calloc(1, sizeof(RdbTunnelSession), MEM_CAT_INPUT_OTHER);
            s->tunnel = t;
            s->client_fd = client;
            s->server_fd = -1;
            if (pthread_create(&s->thread, NULL, session_main, s) == 0) {
                t->sessions[t->session_count++] = s;
            } else {
                mem_free(s);
                s = NULL;
            }
        }
        pthread_mutex_unlock(&t->lock);
        if (!s) {
            log_error("rdb-tunnel: session limit reached for %s", t->target.host);
            close(client);
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ══════════════════════════════════════════════════════════════════════ */

RdbTunnel* rdb_tunnel_open(const RdbTarget* target, const RdbTunnelSpec* spec,
                           char* out_endpoint, size_t cap) {
    if (!target || !spec || !out_endpoint ||
            (target->kind != RDB_PEER_TCP && target->kind != RDB_PEER_UNIX_SOCKET)) {
        return NULL;
    }
    RdbTunnel* t = (RdbTunnel*)mem_calloc(1, sizeof(RdbTunnel), MEM_CAT_INPUT_OTHER);
    t->target = *target;
    t->spec = *spec;
    snprintf(t->ca_file, sizeof(t->ca_file), "%s", spec->ca_file ? spec->ca_file : "");
    snprintf(t->server_name, sizeof(t->server_name), "%s", spec->server_name ? spec->server_name : "");
    t->spec.ca_file = NULL;
    t->spec.server_name = NULL;
    t->listen_fd = -1;
    t->wake[0] = t->wake[1] = -1;

    // a private 0700 directory: only this user can reach the plaintext side
    const char* tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    size_t tmp_len = strlen(tmp);
    while (tmp_len > 1 && tmp[tmp_len - 1] == '/') tmp_len--;
    snprintf(t->dir, sizeof(t->dir), "%.*s/lambda-rdb-XXXXXX", (int)tmp_len, tmp);
    if (!mkdtemp(t->dir)) {
        log_error("rdb-tunnel: cannot create a private endpoint directory");
        mem_free(t);
        return NULL;
    }
    int port = target->port > 0 ? target->port : 5432;
    if (spec->protocol == RDB_WIRE_POSTGRES) {
        snprintf(t->sock_path, sizeof(t->sock_path), "%s/.s.PGSQL.%d", t->dir, port);
    } else {
        snprintf(t->sock_path, sizeof(t->sock_path), "%s/mysql.sock", t->dir);
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* endpoint = spec->protocol == RDB_WIRE_POSTGRES ? t->dir : t->sock_path;
    if (strlen(t->sock_path) >= sizeof(addr.sun_path) || strlen(endpoint) >= cap) {
        log_error("rdb-tunnel: endpoint path too long");
        rmdir(t->dir);
        mem_free(t);
        return NULL;
    }
    strcpy(addr.sun_path, t->sock_path);
    t->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (t->listen_fd < 0 || bind(t->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
            chmod(t->sock_path, 0600) < 0 || listen(t->listen_fd, 8) < 0 || pipe(t->wake) < 0) {
        log_error("rdb-tunnel: cannot listen on the local endpoint: %s", strerror(errno));
        rdb_tunnel_close(t);
        return NULL;
    }
    fcntl(t->listen_fd, F_SETFD, FD_CLOEXEC);
    fcntl(t->wake[0], F_SETFD, FD_CLOEXEC);
    fcntl(t->wake[1], F_SETFD, FD_CLOEXEC);
    pthread_mutex_init(&t->lock, NULL);
    if (pthread_create(&t->acceptor, NULL, acceptor_main, t) != 0) {
        rdb_tunnel_close(t);
        return NULL;
    }
    t->acceptor_started = true;
    strcpy(out_endpoint, endpoint);
    log_debug("rdb-tunnel: %s:%d via %s (tls mode %d)", target->host, target->port,
              t->sock_path, (int)spec->tls);
    return t;
}

void rdb_tunnel_close(RdbTunnel* t) {
    if (!t) return;
    if (t->wake[1] >= 0) {
        uint8_t one = 1;
        ssize_t ignored = write(t->wake[1], &one, 1);
        (void)ignored;
    }
    if (t->acceptor_started) {
        pthread_join(t->acceptor, NULL);
        // unblock sessions stuck in a blocking upgrade read
        pthread_mutex_lock(&t->lock);
        for (int i = 0; i < t->session_count; i++) {
            if (t->sessions[i]->server_fd >= 0) shutdown(t->sessions[i]->server_fd, SHUT_RDWR);
            if (t->sessions[i]->client_fd >= 0) shutdown(t->sessions[i]->client_fd, SHUT_RDWR);
        }
        pthread_mutex_unlock(&t->lock);
        for (int i = 0; i < t->session_count; i++) {
            pthread_join(t->sessions[i]->thread, NULL);
            mem_free(t->sessions[i]);
        }
        pthread_mutex_destroy(&t->lock);
    }
    if (t->listen_fd >= 0) close(t->listen_fd);
    if (t->wake[0] >= 0) close(t->wake[0]);
    if (t->wake[1] >= 0) close(t->wake[1]);
    if (t->sock_path[0]) unlink(t->sock_path);
    if (t->dir[0]) rmdir(t->dir);
    mem_free(t);
}

#endif /* _WIN32 */
