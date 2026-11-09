#include "update_download.h"
#include "lib/mem.h"
#include "lib/time_util.h"
#include "lib/https_url.h"
#include "lib/url.h"

struct UpdatePullContext { SecurityUpdateJob* job; SecurityUpdateResult policy; };
static bool update_pull_check(void* context) {
    UpdatePullContext* pull = (UpdatePullContext*)context;
    pull->policy = security_update_job_check(pull->job);
    return pull->policy == SECURITY_UPDATE_OK;
}
void security_update_download_free(SecurityUpdateDownload* result) {
    if (result) { mem_free(result->response.bytes); memset(result, 0, sizeof(*result)); }
}
typedef HttpsPullStatus (*UpdatePullHop)(const HttpsPullRequest*, const void*, HttpsPullResponse*);
static HttpsPullStatus update_pull_production(const HttpsPullRequest* request, const void*, HttpsPullResponse* response) {
    return https_pull_once(request, response);
}
static HttpsPullStatus update_download(SecurityUpdateJob* job, const char* public_path,
        uint64_t transfer_limit, uint64_t success_limit, UpdatePullHop hop, const void* fixture, SecurityUpdateDownload* out) {
    if (!out) return HTTPS_PULL_INVALID;
    memset(out, 0, sizeof(*out)); out->response.status = HTTPS_PULL_INVALID;
    out->policy = SECURITY_UPDATE_INVALID;
    SecurityUpdatePackageSpec spec = {};
    out->policy = security_update_job_describe(job, &spec);
    if (out->policy != SECURITY_UPDATE_OK) return out->response.status = HTTPS_PULL_CANCELLED;
    uint64_t budget = transfer_limit < spec.max_download_bytes ? transfer_limit : spec.max_download_bytes;
    if (!budget) return out->response.status = HTTPS_PULL_LIMIT;
    if (!https_url_public_path_valid(public_path, HTTPS_PULL_URL_LIMIT)) return out->response.status;
    char current[HTTPS_PULL_URL_LIMIT];
    size_t base_size = strlen(spec.metadata_base), path_size = strlen(public_path);
    if (base_size + path_size >= sizeof(current)) return out->response.status;
    memcpy(current, spec.metadata_base, base_size); memcpy(current + base_size, public_path, path_size + 1);
    uint64_t deadline = time_now_ms() + (uint64_t)spec.job_seconds * 1000, received = 0;
    UpdatePullContext context = {job, SECURITY_UPDATE_OK};
    for (uint32_t i = 0; i < 4; i++) {
        if (!security_update_url_allowed(&spec, current)) return out->response.status = HTTPS_PULL_INVALID;
        uint64_t now = time_now_ms();
        if (now >= deadline || received >= budget) return out->response.status = HTTPS_PULL_LIMIT;
        HttpsPullRequest request = {current, budget - received, (uint32_t)(deadline - now), update_pull_check, &context,
            success_limit != UINT64_MAX, success_limit};
        HttpsPullResponse response = {};
        HttpsPullStatus status = hop(&request, fixture, &response); ++out->hops;
        received += response.received_bytes; response.received_bytes = received;
        out->policy = context.policy;
        if (status != HTTPS_PULL_REDIRECT) {
            if (status == HTTPS_PULL_OK) {
                // Only the bounded local handoff is serialized with policy changes;
                // network waits never hold a protected-state transaction/lease.
                SecurityUpdateLease* lease = security_update_job_enter(job, &out->policy);
                if (!lease) { mem_free(response.bytes); response.bytes = nullptr; response.size = 0; response.status = HTTPS_PULL_CANCELLED; }
                out->response = response;
                security_update_job_leave(lease);
            } else out->response = response;
            return out->response.status;
        }
        out->response = response;
        // Resolve relative Location through the common URL parser, then apply the
        // identical canonical admission profile to every hop (including origins).
        bool plain = https_url_reference_valid_ex(response.location, sizeof(response.location), true);
        Url* base = plain ? url_parse(current) : nullptr;
        Url* next = base ? url_parse_with_base(response.location, base) : nullptr;
        const char* href = next ? url_get_href(next) : nullptr;
        bool allowed = href && security_update_url_allowed(&spec, href) && strlen(href) < sizeof(current);
        if (allowed) strcpy(current, href);
        url_destroy(next); url_destroy(base);
        if (!allowed) return out->response.status = HTTPS_PULL_INVALID;
    }
    return out->response.status = HTTPS_PULL_LIMIT;
}
HttpsPullStatus security_update_download(SecurityUpdateJob* job, const char* public_path, SecurityUpdateDownload* out) {
    return update_download(job, public_path, UINT64_MAX, UINT64_MAX, update_pull_production, nullptr, out);
}
HttpsPullStatus security_update_download_limited(SecurityUpdateJob* job, const char* public_path,
        uint64_t transfer_limit, uint64_t success_limit, SecurityUpdateDownload* out) {
    return update_download(job, public_path, transfer_limit, success_limit, update_pull_production, nullptr, out);
}
#ifdef LAMBDA_HTTPS_PULL_TESTING
static HttpsPullStatus update_pull_fixture(const HttpsPullRequest* request, const void* fixture, HttpsPullResponse* response) {
    return https_pull_fixture(request, (const HttpsPullFixture*)fixture, response);
}
HttpsPullStatus security_update_download_fixture(SecurityUpdateJob* job, const char* public_path,
        const HttpsPullFixture* fixture, SecurityUpdateDownload* out) {
    return update_download(job, public_path, UINT64_MAX, UINT64_MAX, update_pull_fixture, fixture, out);
}
HttpsPullStatus security_update_download_limited_fixture(SecurityUpdateJob* job, const char* public_path,
        uint64_t transfer_limit, uint64_t success_limit, const HttpsPullFixture* fixture, SecurityUpdateDownload* out) {
    return update_download(job, public_path, transfer_limit, success_limit, update_pull_fixture, fixture, out);
}
#endif
