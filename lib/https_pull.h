#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { HTTPS_PULL_URL_LIMIT = 4096, HTTPS_PULL_HEADER_LIMIT = 16384 };
typedef enum HttpsPullStatus {
    HTTPS_PULL_OK, HTTPS_PULL_REDIRECT, HTTPS_PULL_INVALID, HTTPS_PULL_CANCELLED,
    HTTPS_PULL_LIMIT, HTTPS_PULL_FORBIDDEN_PEER, HTTPS_PULL_TLS_ERROR,
    HTTPS_PULL_HTTP_ERROR, HTTPS_PULL_IO_ERROR, HTTPS_PULL_UNSUPPORTED
} HttpsPullStatus;
typedef struct HttpsPullRequest {
    const char* url;
    uint64_t max_bytes;
    uint32_t timeout_ms;
    bool (*check)(void*);
    void* context;
    bool success_limit_set; // additional exact ceiling for 200 bodies; redirects use max_bytes
    uint64_t success_limit; // zero permits only an empty successful body
} HttpsPullRequest;
typedef struct HttpsPullResponse {
    HttpsPullStatus status;
    uint32_t http_status;
    int transport_error;
    uint64_t received_bytes;
    uint8_t* bytes; // unverified, mem_free(); delivered only for a complete 200
    size_t size;
    char location[HTTPS_PULL_URL_LIMIT]; // never followed by this mechanism
} HttpsPullResponse;
// Trusted mechanism only (D11.1.2): anonymous HTTPS GET, one hop, no browser
// cookies/cache/proxy/auth, OS-only trust, public peer filtering and bounded IO.
// Caller separately admits the URL/origin, owns policy checks and authenticates
// returned metadata/artifacts before publication or execution.
HttpsPullStatus https_pull_once(const HttpsPullRequest* request, HttpsPullResponse* response);
// Raw network-byte address; conservative public-unicast profile, not DNS or a grant.
bool https_pull_public_address(unsigned version, const uint8_t* bytes, size_t size);
#ifdef LAMBDA_HTTPS_PULL_TESTING
typedef struct HttpsPullFixture {
    const char* resolve; // one curl HOST:PORT:ADDRESS rule for an owned fixture
    const char* ca_file;
    bool loopback; // test profile allows loopback only, never other private peers
} HttpsPullFixture;
HttpsPullStatus https_pull_fixture(const HttpsPullRequest* request, const HttpsPullFixture* fixture, HttpsPullResponse* response);
#endif
#ifdef __cplusplus
}
#endif
