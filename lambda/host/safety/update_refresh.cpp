#include "update_refresh.h"
#include "update_records.h"
#include "update_metadata.h"
#include "update_wire.h"
#include "update_process.h"
#include "lib/https_url.h"
#include "lib/digest.h"
#include "lib/mem.h"
#include "lib/time_util.h"
#include <errno.h>

struct SecurityVerifiedUpdate {
    uint8_t binding[32];
    uint8_t* bytes;
    size_t size;
    uint8_t* description;
    size_t description_size;
};
static void verified_free(SecurityVerifiedUpdate* update) {
    if (update) { mem_free(update->bytes); mem_free(update->description); mem_free(update); }
}
void security_update_refresh_free(SecurityUpdateRefreshReport* report) {
    if (!report) return;
    verified_free(report->artifact); confined_process_result_free(&report->process); memset(report, 0, sizeof(*report));
}
bool security_update_verified_view(SecurityVerifiedUpdate* update, SecurityUpdateJob* job, SecurityVerifiedUpdateView* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out)); uint8_t binding[32];
    if (!update || !security_update_job_binding(job, binding) || memcmp(binding, update->binding, sizeof(binding))) return false;
    *out = {update->bytes, update->size, update->description, update->description_size}; return true;
}
typedef HttpsPullStatus (*RefreshDownload)(SecurityUpdateJob*, const char*, uint64_t, uint64_t, const void*, SecurityUpdateDownload*);
static HttpsPullStatus refresh_download(SecurityUpdateJob* job, const char* path, uint64_t transfer, uint64_t success,
        const void*, SecurityUpdateDownload* out) {
    return security_update_download_limited(job, path, transfer, success, out);
}
struct RefreshOperation {
    SecurityUpdateJob* job;
    SecurityUpdateRefreshReport* report;
    uint8_t binding[32];
};
static bool refresh_check(void* context) {
    RefreshOperation* operation = (RefreshOperation*)context;
    operation->report->policy = security_update_job_check(operation->job);
    return operation->report->policy == SECURITY_UPDATE_OK;
}
static bool refresh_utc(uint64_t seconds, char out[21]) {
    if (!seconds || seconds > 253402300799ULL) return false;
    time_t time = (time_t)seconds; struct tm utc = {};
#ifdef _WIN32
    if (gmtime_s(&utc, &time)) return false;
#else
    if (!gmtime_r(&time, &utc)) return false;
#endif
    return strftime(out, 21, "%Y-%m-%dT%H:%M:%SZ", &utc) == 20;
}
static bool refresh_dispatch(UpdateWire* wire, SecurityUpdateJob* job, const char* target,
        RefreshDownload download, const void* transport_context, SecurityUpdateRefreshReport* report) {
    SecurityUpdatePackageSpec spec = {}; report->policy = security_update_job_describe(job, &spec);
    if (report->policy != SECURITY_UPDATE_OK || !https_url_target_name_valid(target, 4096)) return false;
    RefreshOperation operation = {job, report, {}};
    if (!security_update_job_binding(job, operation.binding)) return false;
    wire->check = refresh_check; wire->context = &operation;
    wire->deadline = time_now_ms() + (uint64_t)spec.job_seconds * 1000;
    SecurityUpdateMetadataSnapshot snapshot = {};
    report->policy = security_update_metadata_read(job, &snapshot);
    if (report->policy != SECURITY_UPDATE_OK) return false;
    // Persist trusted time before the worker parses anything or requests IO.
    report->policy = security_update_metadata_observe(job, snapshot.generation, &report->generation);
    security_update_metadata_free(&snapshot);
    if (report->policy != SECURITY_UPDATE_OK) return false;
    report->policy = security_update_metadata_read(job, &snapshot);
    if (report->policy != SECURITY_UPDATE_OK) return false;
    UpdateWireMessage initial = {}; initial.kind = UPDATE_WIRE_INIT;
    strcpy(initial.path, target); initial.limit = (uint64_t)spec.job_seconds * 1000;
    bool time_valid = refresh_utc(snapshot.observed_time, initial.utc);
    initial.count = snapshot.count; initial.entries = snapshot.entries;
    snapshot.count = 0; snapshot.entries = nullptr;
    bool started = time_valid && security_update_wire_send(wire, &initial);
    security_update_wire_free(&initial);
    if (!started) return false;
    uint64_t remaining = spec.max_download_bytes;
    uint8_t* candidate = nullptr; size_t candidate_size = 0; bool artifact_fetched = false;
    struct CandidateOwner { uint8_t** bytes; ~CandidateOwner() { mem_free(*bytes); } } owned = {&candidate};
    const uint32_t allowed = update_wire_kind_mask(UPDATE_WIRE_FETCH) | update_wire_kind_mask(UPDATE_WIRE_CHECKPOINT) |
        update_wire_kind_mask(UPDATE_WIRE_DONE) | update_wire_kind_mask(UPDATE_WIRE_ERROR);
    for (unsigned step = 0; step < 256; step++) {
        UpdateWireMessage action = {};
        if (!security_update_wire_receive(wire, allowed, &action)) return false;
        struct ActionOwner { UpdateWireMessage* message; ~ActionOwner() { security_update_wire_free(message); } } action_owner = {&action};
        if (!refresh_check(&operation)) return false;
        UpdateWireMessage response = {}; response.kind = UPDATE_WIRE_RESPONSE; response.status = UPDATE_WIRE_OK;
        if (action.kind == UPDATE_WIRE_CHECKPOINT) {
            report->policy = security_update_metadata_replace(job, report->generation, action.entries, action.count, &report->generation);
            if (report->policy != SECURITY_UPDATE_OK) return false;
            report->checkpoints++;
        } else if (action.kind == UPDATE_WIRE_FETCH) {
            if (!remaining) { report->transport = HTTPS_PULL_LIMIT; return false; }
            mem_free(candidate); candidate = nullptr; candidate_size = 0; artifact_fetched = false;
            SecurityUpdateDownload pulled = {}; report->requests++;
            report->transport = download(job, action.path, remaining, action.limit, transport_context, &pulled);
            report->policy = pulled.policy;
            if (pulled.response.received_bytes > remaining) { security_update_download_free(&pulled); return false; }
            remaining -= pulled.response.received_bytes; report->received_bytes += pulled.response.received_bytes;
            bool missing = report->transport == HTTPS_PULL_HTTP_ERROR &&
                (pulled.response.http_status == 404 || pulled.response.http_status == 410);
            if (missing) response.status = UPDATE_WIRE_NOT_FOUND;
            else if (report->transport != HTTPS_PULL_OK || report->policy != SECURITY_UPDATE_OK) { security_update_download_free(&pulled); return false; }
            response.bytes = pulled.response.bytes; response.size = (uint32_t)pulled.response.size;
            pulled.response.bytes = nullptr;
            security_update_download_free(&pulled);
            if (action.artifact && response.status == UPDATE_WIRE_OK) {
                candidate = (uint8_t*)mem_dup_n((const char*)response.bytes, response.size, MEM_CAT_SYSTEM);
                if (!candidate) { security_update_wire_free(&response); return false; }
                candidate_size = response.size; artifact_fetched = true;
            }
            bool sent = security_update_wire_send(wire, &response);
            security_update_wire_free(&response);
            if (!sent) return false;
            continue;
        } else if (action.kind == UPDATE_WIRE_DONE) {
            if (action.status == UPDATE_WIRE_UNCHANGED) { report->unchanged = true; return true; }
            uint8_t digest[32];
            if (!artifact_fetched || !digest_sha256(candidate, candidate_size, digest) || memcmp(digest, action.digest, 32)) return false;
            SecurityUpdateResult policy; SecurityUpdateLease* lease = security_update_job_enter(job, &policy);
            report->policy = policy;
            if (!lease) return false;
            SecurityVerifiedUpdate* verified = (SecurityVerifiedUpdate*)mem_calloc(1, sizeof(*verified), MEM_CAT_SYSTEM);
            if (verified) {
                memcpy(verified->binding, operation.binding, 32);
                verified->bytes = candidate; verified->size = candidate_size; candidate = nullptr;
                verified->description = action.bytes; verified->description_size = action.size; action.bytes = nullptr;
                report->artifact = verified;
            }
            security_update_job_leave(lease); return verified != nullptr;
        } else return false;
        if (!security_update_wire_send(wire, &response)) return false;
    }
    return false;
}
bool security_update_refresh(const SecurityUpdateRefreshConfig* config, SecurityUpdateJob* job, SecurityUpdateRefreshReport* report) {
    if (!report) return false;
    memset(report, 0, sizeof(*report)); report->policy = SECURITY_UPDATE_INVALID; report->process.exit_code = -1;
    if (!config) return false;
    struct RefreshCall { SecurityUpdateJob* job; const char* target; SecurityUpdateRefreshReport* report; } call = {job, config->target, report};
    auto dispatch = [](UpdateWire* wire, void* pointer) -> bool {
        RefreshCall* call = (RefreshCall*)pointer;
        return refresh_dispatch(wire, call->job, call->target, refresh_download, nullptr, call->report);
    };
    SecurityUpdateProcessReport process = {};
    bool success = security_update_process_run(&config->verifier, &config->limits, job, "lambda-update-verifier-v1", dispatch, &call, &process);
    report->process = process.process; report->launched = process.launched; report->system_error = process.system_error;
    if (!success) { verified_free(report->artifact); report->artifact = nullptr; report->unchanged = false; }
    return success;
}
#ifdef LAMBDA_SECURITY_UPDATE_TESTING
SecurityVerifiedUpdate* security_update_verified_fixture(SecurityUpdateJob* job, const uint8_t* bytes, size_t size) {
    if ((!bytes && size) || size > UPDATE_WIRE_BODY_LIMIT) return nullptr;
    SecurityVerifiedUpdate* update = (SecurityVerifiedUpdate*)mem_calloc(1, sizeof(*update), MEM_CAT_SYSTEM);
    if (!update) return nullptr;
    if (security_update_job_binding(job, update->binding)) {
        update->bytes = (uint8_t*)mem_dup_n((const char*)bytes, size, MEM_CAT_SYSTEM); update->size = size;
        update->description = (uint8_t*)mem_dup_n("{}", 2, MEM_CAT_SYSTEM); update->description_size = 2;
        if (update->bytes && update->description) return update;
    }
    verified_free(update); return nullptr;
}
void security_update_verified_fixture_free(SecurityVerifiedUpdate* update) { verified_free(update); }
static HttpsPullStatus refresh_fixture_download(SecurityUpdateJob* job, const char* path, uint64_t transfer, uint64_t success,
        const void* fixture, SecurityUpdateDownload* out) {
    return security_update_download_limited_fixture(job, path, transfer, success, (const HttpsPullFixture*)fixture, out);
}
bool security_update_refresh_fixture(int fd, uint64_t session, SecurityUpdateJob* job, const char* target,
        const HttpsPullFixture* fixture, SecurityUpdateRefreshReport* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out)); out->policy = SECURITY_UPDATE_INVALID;
    IpcStream* stream = ipc_stream_adopt(fd); if (!stream) return false;
    UpdateWire wire = {stream, session, 0, 0, time_now_ms() + 5000, nullptr, nullptr};
    UpdateWireMessage ready = {};
    bool success = security_update_wire_receive(&wire, update_wire_kind_mask(UPDATE_WIRE_READY), &ready) &&
        refresh_dispatch(&wire, job, target, refresh_fixture_download, fixture, out);
    security_update_wire_free(&ready);
    ipc_stream_close(stream); return success;
}
#endif
