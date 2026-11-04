#include "https_pull.h"
#include "https_url.h"
#include "byte_builder.h"
#include "curl_runtime.h"
#include "curl_trust.h"
#include "str.h"
#include "time_util.h"
#include "url.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <mbedtls/x509_crt.h>
#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

bool https_pull_public_address(unsigned version, const uint8_t* b, size_t size) {
    if (!b) return false;
    if (version == 4 && size == 4) {
        // Conservative public unicast: special-use ranges never become peers,
        // including cloud metadata, documentation and embedded-routing ranges.
        return b[0] != 0 && b[0] != 10 && b[0] != 127 && b[0] < 224 &&
            !(b[0] == 100 && (b[1] & 0xc0) == 64) &&
            !(b[0] == 169 && b[1] == 254) &&
            !(b[0] == 172 && (b[1] & 0xf0) == 16) &&
            !(b[0] == 192 && ((b[1] == 0 && (b[2] == 0 || b[2] == 2)) ||
                (b[1] == 88 && b[2] == 99) || b[1] == 168)) &&
            !(b[0] == 198 && ((b[1] & 0xfe) == 18 || (b[1] == 51 && b[2] == 100))) &&
            !(b[0] == 203 && b[1] == 0 && b[2] == 113);
    }
    if (version == 6 && size == 16) {
        return (b[0] & 0xe0) == 0x20 &&
            !(b[0] == 0x20 && b[1] == 1 && b[2] < 2) &&
            !(b[0] == 0x20 && b[1] == 1 && b[2] == 0x0d && b[3] == 0xb8) &&
            !(b[0] == 0x20 && b[1] == 2) &&
            !(b[0] == 0x3f && b[1] == 0xff && (b[2] & 0xf0) == 0);
    }
    return false;
}

#ifndef _WIN32
struct PullState {
    const HttpsPullRequest* request;
    HttpsPullResponse* response;
    ByteBuilder body;
    uint64_t deadline_ms, header_bytes;
    uint16_t port;
    bool failed, length_seen, encoding_seen, location_seen, denied_peer, fixture_loopback;
};
static bool pull_fail(PullState* state, HttpsPullStatus status) {
    if (!state->failed) state->response->status = status;
    state->failed = true;
    return false;
}
static bool pull_check(PullState* state) {
    if (state->failed) return false;
    if (time_now_ms() >= state->deadline_ms ||
            (state->request->check && !state->request->check(state->request->context)))
        return pull_fail(state, HTTPS_PULL_CANCELLED);
    return true;
}
static uint64_t pull_response_limit(const PullState* state) {
    return state->request->success_limit_set && state->response->http_status == 200 &&
        state->request->success_limit < state->request->max_bytes ? state->request->success_limit : state->request->max_bytes;
}
static bool pull_url(const char* text, uint16_t* port) {
    if (!https_url_reference_valid_ex(text, HTTPS_PULL_URL_LIMIT, true) || strncmp(text, "https://", 8)) return false;
    Url* url = url_parse(text);
    bool valid = url && url_is_valid(url) && url_get_scheme(url) == URL_SCHEME_HTTPS &&
        url_get_href(url) && !strcmp(text, url_get_href(url)) && url_get_hostname(url) && *url_get_hostname(url);
    if (valid) {
        const char* optional[] = {url_get_username(url), url_get_password(url), url_get_search(url), url_get_hash(url)};
        for (const char* value : optional) if (value && *value) valid = false;
        *port = url->port_number ? url->port_number : 443;
    }
    url_destroy(url); return valid;
}
static curl_socket_t pull_socket(void* context, curlsocktype purpose, struct curl_sockaddr* address) {
    PullState* state = (PullState*)context;
    if (!pull_check(state)) return CURL_SOCKET_BAD;
    const uint8_t* bytes = nullptr; size_t size = 0; unsigned version = 0; uint16_t port = 0;
    if (address && address->family == AF_INET && address->addrlen == sizeof(sockaddr_in)) {
        const sockaddr_in* peer = (const sockaddr_in*)&address->addr;
        bytes = (const uint8_t*)&peer->sin_addr; size = 4; version = 4; port = ntohs(peer->sin_port);
    } else if (address && address->family == AF_INET6 && address->addrlen == sizeof(sockaddr_in6)) {
        const sockaddr_in6* peer = (const sockaddr_in6*)&address->addr;
        if (peer->sin6_scope_id) return CURL_SOCKET_BAD;
        bytes = (const uint8_t*)&peer->sin6_addr; size = 16; version = 6; port = ntohs(peer->sin6_port);
    }
    bool public_peer = https_pull_public_address(version, bytes, size);
    // Only the separately compiled test entry can reach an owned loopback TLS fixture.
    if (state->fixture_loopback) {
        static const uint8_t loop6[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        public_peer = bytes && ((version == 4 && bytes[0] == 127) || (version == 6 && !memcmp(bytes, loop6, 16)));
    }
    if (!public_peer || port != state->port || purpose != CURLSOCKTYPE_IPCXN ||
            address->socktype != SOCK_STREAM || address->protocol != IPPROTO_TCP) {
        state->denied_peer = true; return CURL_SOCKET_BAD;
    }
    int fd = socket(address->family, address->socktype, address->protocol);
    if (fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) { close(fd); fd = -1; }
    return fd < 0 ? CURL_SOCKET_BAD : fd;
}
static bool pull_field(const char* text, size_t length, const char* name, const char** value, size_t* value_size) {
    size_t key_size = strlen(name);
    if (length <= key_size || text[key_size] != ':' || str_icmp(text, key_size, name, key_size)) return false;
    *value = text + key_size + 1; *value_size = length - key_size - 1;
    while (*value_size && (**value == ' ' || **value == '\t')) { ++*value; --*value_size; }
    while (*value_size && ((*value)[*value_size - 1] == ' ' || (*value)[*value_size - 1] == '\t')) --*value_size;
    return true;
}
static size_t pull_header(char* text, size_t size, size_t count, void* context) {
    PullState* state = (PullState*)context;
    if (!pull_check(state) || (size && count > SIZE_MAX / size)) return 0;
    size_t bytes = size * count;
    if (bytes > HTTPS_PULL_HEADER_LIMIT - state->header_bytes) { pull_fail(state, HTTPS_PULL_LIMIT); return 0; }
    state->header_bytes += bytes;
    if (bytes < 2 || text[bytes - 2] != '\r' || text[bytes - 1] != '\n' || memchr(text, 0, bytes) ||
            text[0] == ' ' || text[0] == '\t') { pull_fail(state, HTTPS_PULL_HTTP_ERROR); return 0; }
    size_t length = bytes - 2;
    for (size_t i = 0; i < length; i++) if ((unsigned char)text[i] < 32 && text[i] != '\t') {
        pull_fail(state, HTTPS_PULL_HTTP_ERROR); return 0;
    }
    if (str_starts_with_const(text, length, "HTTP/")) {
        // libcurl validates the status line; keep body handling tied to its status.
        long status = 0;
        const char* space = (const char*)memchr(text, ' ', length);
        if (!space || text + length - space < 4 || space[1] < '1' || space[1] > '5' ||
                space[2] < '0' || space[2] > '9' || space[3] < '0' || space[3] > '9') {
            pull_fail(state, HTTPS_PULL_HTTP_ERROR); return 0;
        }
        status = (space[1] - '0') * 100 + (space[2] - '0') * 10 + space[3] - '0';
        state->response->http_status = (uint32_t)status;
        state->length_seen = state->encoding_seen = state->location_seen = false;
        state->response->location[0] = 0;
    }
    const char* value = nullptr; size_t value_size = 0;
    if (pull_field(text, length, "Location", &value, &value_size)) {
        if (state->location_seen || !value_size || value_size >= sizeof(state->response->location)) {
            pull_fail(state, HTTPS_PULL_HTTP_ERROR); return 0;
        }
        state->location_seen = true; memcpy(state->response->location, value, value_size);
        state->response->location[value_size] = 0;
    } else if (pull_field(text, length, "Content-Length", &value, &value_size)) {
        if (state->length_seen || !value_size) { pull_fail(state, HTTPS_PULL_HTTP_ERROR); return 0; }
        state->length_seen = true; uint64_t length_value = 0;
        for (size_t i = 0; i < value_size; i++) {
            unsigned digit = (unsigned char)value[i] - '0';
            if (digit > 9 || length_value > (UINT64_MAX - digit) / 10) { pull_fail(state, HTTPS_PULL_HTTP_ERROR); return 0; }
            length_value = length_value * 10 + digit;
        }
        if (length_value > pull_response_limit(state)) { pull_fail(state, HTTPS_PULL_LIMIT); return 0; }
    } else if (pull_field(text, length, "Content-Encoding", &value, &value_size)) {
        if (state->encoding_seen || str_icmp(value, value_size, "identity", 8)) {
            pull_fail(state, HTTPS_PULL_HTTP_ERROR); return 0;
        }
        state->encoding_seen = true;
    }
    return bytes;
}
static size_t pull_body(char* text, size_t size, size_t count, void* context) {
    PullState* state = (PullState*)context;
    if (!pull_check(state) || (size && count > SIZE_MAX / size)) return 0;
    size_t bytes = size * count;
    uint64_t limit = pull_response_limit(state);
    if (state->response->received_bytes > limit || bytes > limit - state->response->received_bytes) {
        pull_fail(state, HTTPS_PULL_LIMIT); return 0;
    }
    state->response->received_bytes += bytes;
    if (state->response->http_status == 200 && !byte_builder_append(&state->body, text, bytes)) {
        pull_fail(state, HTTPS_PULL_IO_ERROR); return 0;
    }
    return bytes;
}
static int pull_progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t uploaded) {
    PullState* state = (PullState*)context;
    if (uploaded > 0) pull_fail(state, HTTPS_PULL_HTTP_ERROR);
    return pull_check(state) ? 0 : 1;
}
static CURLcode pull_disable_ambient_auth(CURL* curl) {
    // Since the required libcurl version, UNKNOWN_OPTION here means this entire
    // feature was compiled out. The bundled build omits proxies and netrc; it
    // cannot accept their disable options or consult their environment/files.
    CURLcode result = curl_easy_setopt(curl, CURLOPT_PROXY, "");
    if (result == CURLE_OK) {
        result = curl_easy_setopt(curl, CURLOPT_PRE_PROXY, "");
        if (result == CURLE_OK) result = curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    } else if (result == CURLE_UNKNOWN_OPTION) result = CURLE_OK;
    if (result != CURLE_OK) return result;
    result = curl_easy_setopt(curl, CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
    return result == CURLE_UNKNOWN_OPTION ? CURLE_OK : result;
}

static HttpsPullStatus pull_perform(const HttpsPullRequest* request, HttpsPullResponse* response,
        const char* resolve, const char* fixture_ca, bool fixture_loopback) {
    if (!response) return HTTPS_PULL_INVALID;
    memset(response, 0, sizeof(*response)); response->status = HTTPS_PULL_INVALID;
    PullState state = {}; state.request = request; state.response = response; state.fixture_loopback = fixture_loopback;
    if (!request || !request->max_bytes || request->max_bytes > 256 * 1024 * 1024 ||
            !request->timeout_ms || request->timeout_ms > 600000 || !pull_url(request->url, &state.port)) return response->status;
    state.deadline_ms = time_now_ms() + request->timeout_ms;
    if (!pull_check(&state)) return response->status;
    // curl opens this ambient path at global TLS initialization. Refuse it before
    // any initialization rather than permitting a trusted service to write there.
    const char* keylog = getenv("SSLKEYLOGFILE");
    if (keylog && *keylog) return response->status = HTTPS_PULL_UNSUPPORTED;
    if (!curl_runtime_initialize()) return response->status = HTTPS_PULL_IO_ERROR;
    if (!curl_runtime_private_tls_ready()) return response->status = HTTPS_PULL_UNSUPPORTED;
    const curl_version_info_data* version = curl_version_info(CURLVERSION_NOW);
    if (!version || version->version_num < 0x080a01 || !version->ssl_version ||
            !str_starts_with_const(version->ssl_version, strlen(version->ssl_version), "mbedTLS/") ||
            !(version->features & CURL_VERSION_ASYNCHDNS)) return response->status = HTTPS_PULL_UNSUPPORTED;
    mbedtls_x509_crt fixture_roots; mbedtls_x509_crt_init(&fixture_roots);
    mbedtls_x509_crt* roots = nullptr;
    if (fixture_ca) {
        if (mbedtls_x509_crt_parse_file(&fixture_roots, fixture_ca) == 0 && fixture_roots.raw.len) roots = &fixture_roots;
    } else roots = trust_store_system_roots();
    CURL* curl = roots ? curl_easy_init() : nullptr;
    struct curl_slist* mapping = nullptr;
    CURLcode setup = roots ? CURLE_OK : CURLE_SSL_CACERT_BADFILE;
    if (roots && !curl) setup = CURLE_OUT_OF_MEMORY;
    if (resolve) { mapping = curl_slist_append(nullptr, resolve); if (!mapping) setup = CURLE_OUT_OF_MEMORY; }
    if (setup == CURLE_OK && !byte_builder_init(&state.body, 0, MEM_CAT_NETWORK, true)) setup = CURLE_OUT_OF_MEMORY;
#define PULL_OPTION(option, value) do { if (setup == CURLE_OK) setup = curl_easy_setopt(curl, option, value); } while (0)
    PULL_OPTION(CURLOPT_URL, request->url);
    PULL_OPTION(CURLOPT_PROTOCOLS_STR, "https");
    PULL_OPTION(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    PULL_OPTION(CURLOPT_FOLLOWLOCATION, 0L);
    if (setup == CURLE_OK) setup = pull_disable_ambient_auth(curl);
    PULL_OPTION(CURLOPT_HTTPAUTH, (long)CURLAUTH_NONE);
    PULL_OPTION(CURLOPT_UNRESTRICTED_AUTH, 0L);
    PULL_OPTION(CURLOPT_HTTPGET, 1L);
    PULL_OPTION(CURLOPT_USERAGENT, "Lambda-Update/1");
    PULL_OPTION(CURLOPT_ACCEPT_ENCODING, "identity");
    PULL_OPTION(CURLOPT_HTTP_CONTENT_DECODING, 0L);
    PULL_OPTION(CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    PULL_OPTION(CURLOPT_SSL_ENABLE_ALPN, 0L);
    PULL_OPTION(CURLOPT_SSL_SESSIONID_CACHE, 0L);
    PULL_OPTION(CURLOPT_SSL_VERIFYPEER, 1L); PULL_OPTION(CURLOPT_SSL_VERIFYHOST, 2L);
    PULL_OPTION(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    if (setup == CURLE_OK) setup = curl_use_explicit_trust_store(curl, roots);
    PULL_OPTION(CURLOPT_FRESH_CONNECT, 1L); PULL_OPTION(CURLOPT_FORBID_REUSE, 1L);
    PULL_OPTION(CURLOPT_DNS_CACHE_TIMEOUT, 0L);
    PULL_OPTION(CURLOPT_NOSIGNAL, 1L);
    PULL_OPTION(CURLOPT_TIMEOUT_MS, (long)request->timeout_ms);
    PULL_OPTION(CURLOPT_CONNECTTIMEOUT_MS, (long)request->timeout_ms);
    PULL_OPTION(CURLOPT_NOPROGRESS, 0L);
    PULL_OPTION(CURLOPT_XFERINFOFUNCTION, pull_progress); PULL_OPTION(CURLOPT_XFERINFODATA, &state);
    PULL_OPTION(CURLOPT_OPENSOCKETFUNCTION, pull_socket); PULL_OPTION(CURLOPT_OPENSOCKETDATA, &state);
    PULL_OPTION(CURLOPT_HEADERFUNCTION, pull_header); PULL_OPTION(CURLOPT_HEADERDATA, &state);
    PULL_OPTION(CURLOPT_WRITEFUNCTION, pull_body); PULL_OPTION(CURLOPT_WRITEDATA, &state);
    if (mapping) PULL_OPTION(CURLOPT_RESOLVE, mapping);
#undef PULL_OPTION
    CURLcode result = setup;
    if (setup == CURLE_OK && pull_check(&state)) result = curl_easy_perform(curl);
    response->transport_error = (int)result;
    if (!state.failed) {
        if (result == CURLE_OK && pull_check(&state)) {
            if (response->http_status == 200) {
                response->bytes = byte_builder_take(&state.body, &response->size); response->status = HTTPS_PULL_OK;
            } else if ((response->http_status == 301 || response->http_status == 302 || response->http_status == 303 ||
                    response->http_status == 307 || response->http_status == 308) && state.location_seen)
                response->status = HTTPS_PULL_REDIRECT;
            else response->status = HTTPS_PULL_HTTP_ERROR;
        } else if (!state.failed) {
            if (result == CURLE_OPERATION_TIMEDOUT) response->status = HTTPS_PULL_CANCELLED;
            else if (state.denied_peer) response->status = HTTPS_PULL_FORBIDDEN_PEER;
            else if (result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CONNECT_ERROR ||
                    result == CURLE_SSL_CACERT_BADFILE) response->status = HTTPS_PULL_TLS_ERROR;
            else response->status = setup != CURLE_OK ? HTTPS_PULL_UNSUPPORTED : HTTPS_PULL_IO_ERROR;
        }
    }
    byte_builder_destroy(&state.body); curl_easy_cleanup(curl); curl_slist_free_all(mapping);
    mbedtls_x509_crt_free(&fixture_roots);
    return response->status;
}
#endif

HttpsPullStatus https_pull_once(const HttpsPullRequest* request, HttpsPullResponse* response) {
#ifndef _WIN32
    return pull_perform(request, response, nullptr, nullptr, false);
#else
    (void)request;
    if (response) { memset(response, 0, sizeof(*response)); response->status = HTTPS_PULL_UNSUPPORTED; }
    return HTTPS_PULL_UNSUPPORTED;
#endif
}
#ifdef LAMBDA_HTTPS_PULL_TESTING
HttpsPullStatus https_pull_fixture(const HttpsPullRequest* request, const HttpsPullFixture* fixture, HttpsPullResponse* response) {
#ifndef _WIN32
    if (fixture) return pull_perform(request, response, fixture->resolve, fixture->ca_file, fixture->loopback);
#endif
    return https_pull_once(request, response);
}
#endif
