#include "update_controls.h"
#include "state_store.h"
#include "state_wire.h"
#include "update_records.h"
#include "../../../lib/digest.h"
#include "../../../lib/endian.h"
#include "../../../lib/mem.h"
#include "../../../lib/time_util.h"
#include "../../../lib/url.h"
#include "../../../lib/https_url.h"
#include <pthread.h>

struct UpdateRow { SecurityUpdatePackageInfo info; uint64_t nonce; };
struct UpdateCatalog {
    uint64_t generation, epoch;
    uint8_t instance[16];
    uint8_t profile[32];
    SecurityUpdateMode mode;
    bool offline;
    uint64_t pause_until;
    uint32_t count;
    UpdateRow rows[SECURITY_UPDATE_MAX_PACKAGES];
};
struct SecurityUpdateControl {
    SecurityGrantStore* store;
    uint64_t (*clock)(void*);
    void* clock_context;
    uint8_t instance[16];
    pthread_mutex_t mutex;
    uint32_t references;
    bool closed;
    bool invalidated;
    uint64_t observed_generation, observed_epoch;
    uint32_t metadata_count;
    struct { uint8_t package[32], digest[32]; uint64_t generation; } metadata[SECURITY_UPDATE_MAX_PACKAGES];
};
struct SecurityUpdateJob {
    SecurityUpdateControl* owner;
    char package[129];
    uint64_t epoch, nonce, deadline_ms;
    bool manual, finished;
};
struct UpdateOperation {
    SecurityUpdateControl* control;
    SecurityStateTransaction* transaction;
    UpdateCatalog* catalog;
    SecurityUpdateResult result;
    bool locked;
};
struct SecurityUpdateLease { UpdateOperation operation; SecurityUpdateJob* job; uint8_t package_record[32]; };

static const uint8_t update_magic[8] = {'L','M','D','U','P','D','0','1'};
enum { UPDATE_HEADER_BYTES = 104, UPDATE_ROW_BYTES = 129 + 1025 + 33 + 8 * 257 + 64 + 6 * 4 + 5 * 8,
    UPDATE_CHECKSUM_BYTES = 32 };
static_assert((int)SECURITY_UPDATE_UNAVAILABLE == (int)SECURITY_STORE_UNAVAILABLE,
    "storage result prefix must remain aligned");
static SecurityUpdateResult update_storage_result(SecurityStoreResult result) {
    return result <= SECURITY_STORE_UNAVAILABLE ? (SecurityUpdateResult)result : SECURITY_UPDATE_IO_ERROR;
}

const char* security_update_result_name(SecurityUpdateResult result) {
    static const char* const names[] = {"ok", "missing", "conflict", "identity_changed", "corrupt",
        "unsafe_path", "unsupported", "io_error", "invalid", "unavailable", "offline", "paused",
        "disabled", "not_due", "busy", "revoked", "closed"};
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "invalid";
}

static bool update_identifier(const char* text, size_t capacity) {
    size_t length = strnlen(text, capacity);
    if (!length || length == capacity || strstr(text, "..") || strstr(text, "//")) return false;
    unsigned char first = text[0];
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') ||
            (first >= '0' && first <= '9') || first == '@')) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-' || c == '@' || c == '/')) return false;
    }
    return true;
}

static bool update_public_hostname(const char* name) {
    if (!name || !strchr(name, '.') || strlen(name) > 253) return false;
    size_t label = 0; bool alpha = false;
    for (const char* p = name; *p; p++) {
        if (*p == '.') {
            if (!label || p[-1] == '-') return false;
            label = 0;
        } else {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-') ||
                    (!label && *p == '-') || ++label > 63) return false;
            if (*p >= 'a' && *p <= 'z') alpha = true;
        }
    }
    if (!label || name[strlen(name) - 1] == '-' || !alpha) return false;
    static const char* const local_suffixes[] = {".localhost", ".local", ".internal", ".home.arpa"};
    for (size_t i = 0; i < sizeof(local_suffixes) / sizeof(local_suffixes[0]); i++) {
        size_t suffix = strlen(local_suffixes[i]), size = strlen(name);
        if (!strcmp(name, local_suffixes[i] + 1) ||
                (size >= suffix && !strcmp(name + size - suffix, local_suffixes[i]))) return false;
    }
    return true;
}

static bool update_endpoint(const char* text, size_t capacity, bool origin, char* out_origin = nullptr, bool directory = true) {
    if (!https_url_reference_valid_ex(text, capacity, !origin && !directory) || strncmp(text, "https://", 8)) return false;
    size_t length = strnlen(text, capacity);
    Url* url = url_parse(text);
    bool valid = url && url_is_valid(url) && url_get_scheme(url) == URL_SCHEME_HTTPS &&
        url_get_origin(url) && url_get_href(url) &&
        update_public_hostname(url_get_hostname(url)) &&
        !strcmp(text, origin ? url_get_origin(url) : url_get_href(url));
    // Optional WHATWG components are NULL when absent, not empty allocations.
    if (url) {
        const char* optional[] = {url_get_username(url), url_get_password(url), url_get_search(url), url_get_hash(url)};
        for (const char* value : optional) if (value && *value) valid = false;
    }
    // The shared parser preserves dot segments in href. This authority profile
    // rejects them rather than letting a later HTTP client reinterpret the base.
    if (valid && !origin) {
        const char* path = url_get_pathname(url);
        valid = (!directory || text[length - 1] == '/') && path && strlen(url_get_origin(url)) < 257;
    }
    if (valid && out_origin) strcpy(out_origin, url_get_origin(url));
    url_destroy(url); return valid;
}

static bool update_spec_valid(const SecurityUpdatePackageSpec* spec) {
    if (!spec || !update_identifier(spec->package, sizeof(spec->package)) ||
            !update_identifier(spec->channel, sizeof(spec->channel)) || !spec->origin_count ||
            spec->origin_count > SECURITY_UPDATE_MAX_ORIGINS ||
            !security_state_fingerprint_present(spec->publisher) || !security_state_fingerprint_present(spec->root) ||
            spec->poll_seconds < 300 || spec->poll_seconds > 604800 || !spec->job_seconds || spec->job_seconds > 600 ||
            !spec->max_download_bytes || spec->max_download_bytes > 256 * 1024 * 1024) return false;
    char origin[257];
    if (!update_endpoint(spec->metadata_base, sizeof(spec->metadata_base), false, origin)) return false;
    bool includes_base = false;
    for (uint32_t i = 0; i < spec->origin_count; i++) {
        if (!update_endpoint(spec->origins[i], sizeof(spec->origins[i]), true)) return false;
        if (!strcmp(origin, spec->origins[i])) includes_base = true;
        for (uint32_t j = 0; j < i; j++) if (!strcmp(spec->origins[i], spec->origins[j])) return false;
    }
    return includes_base;
}
bool security_update_url_allowed(const SecurityUpdatePackageSpec* spec, const char* url) {
    char origin[257];
    if (!update_spec_valid(spec) || !update_endpoint(url, 4096, false, origin, false)) return false;
    for (uint32_t i = 0; i < spec->origin_count; i++) if (!strcmp(origin, spec->origins[i])) return true;
    return false;
}
static void update_spec_copy(SecurityUpdatePackageSpec* out, const SecurityUpdatePackageSpec* in) {
    memset(out, 0, sizeof(*out));
    strcpy(out->package, in->package); strcpy(out->metadata_base, in->metadata_base); strcpy(out->channel, in->channel);
    out->origin_count = in->origin_count;
    for (uint32_t i = 0; i < in->origin_count; i++) strcpy(out->origins[i], in->origins[i]);
    memcpy(out->publisher, in->publisher, 32); memcpy(out->root, in->root, 32);
    out->poll_seconds = in->poll_seconds; out->job_seconds = in->job_seconds;
    out->max_download_bytes = in->max_download_bytes;
}
static bool update_spec_equal(const SecurityUpdatePackageSpec* a, const SecurityUpdatePackageSpec* b) {
    if (strcmp(a->package, b->package) || strcmp(a->metadata_base, b->metadata_base) || strcmp(a->channel, b->channel) ||
            a->origin_count != b->origin_count || memcmp(a->publisher, b->publisher, 32) || memcmp(a->root, b->root, 32) ||
            a->poll_seconds != b->poll_seconds || a->job_seconds != b->job_seconds || a->max_download_bytes != b->max_download_bytes)
        return false;
    for (uint32_t i = 0; i < a->origin_count; i++) if (strcmp(a->origins[i], b->origins[i])) return false;
    return true;
}
static bool update_mode_valid(uint32_t mode, uint64_t pause) {
    return mode >= SECURITY_UPDATE_ENABLED && mode <= SECURITY_UPDATE_DISABLED &&
        (mode == SECURITY_UPDATE_PAUSED || !pause);
}

static void update_authority_wire(SecurityStateWire* wire, SecurityUpdatePackageSpec* spec) {
    security_state_wire_text(wire, spec->package, sizeof(spec->package));
    security_state_wire_text(wire, spec->metadata_base, sizeof(spec->metadata_base));
    security_state_wire_text(wire, spec->channel, sizeof(spec->channel));
    for (uint32_t j = 0; j < SECURITY_UPDATE_MAX_ORIGINS; j++) security_state_wire_text(wire, spec->origins[j], sizeof(spec->origins[j]));
    security_state_wire_u32(wire, &spec->origin_count); security_state_wire_bytes(wire, spec->publisher, 32); security_state_wire_bytes(wire, spec->root, 32);
}
bool security_update_authority_digest(const SecurityUpdatePackageSpec* spec, uint8_t out[32]) {
    if (!out || !update_spec_valid(spec)) return false;
    SecurityUpdatePackageSpec copy; update_spec_copy(&copy, spec);
    uint8_t bytes[sizeof(copy.package) + sizeof(copy.metadata_base) + sizeof(copy.channel) +
        sizeof(copy.origins) + sizeof(copy.origin_count) + sizeof(copy.publisher) + sizeof(copy.root)];
    SecurityStateWire wire = {bytes, sizeof(bytes), 0, true, true}; update_authority_wire(&wire, &copy);
    static const char domain[] = "Lambda update public authority v1";
    DigestCtx* digest = digest_ctx_new(DIGEST_SHA256);
    bool valid = wire.valid && wire.offset == sizeof(bytes) && digest &&
        digest_update(digest, domain, sizeof(domain) - 1) && digest_update(digest, bytes, sizeof(bytes)) && digest_finalize(digest, out, 32);
    digest_ctx_free(digest); return valid;
}
static bool update_catalog_wire(UpdateCatalog* catalog, uint8_t* bytes, size_t size, bool writing) {
    SecurityStateWire wire = {bytes, size, 0, writing, true};
    uint8_t magic[8]; memcpy(magic, update_magic, 8);
    uint32_t version = 1, reserved = 0, mode = catalog->mode, offline = catalog->offline;
    security_state_wire_bytes(&wire, magic, 8); security_state_wire_u32(&wire, &version); security_state_wire_u32(&wire, &reserved);
    security_state_wire_u64(&wire, &catalog->generation); security_state_wire_u64(&wire, &catalog->epoch);
    security_state_wire_bytes(&wire, catalog->instance, 16); security_state_wire_bytes(&wire, catalog->profile, 32);
    security_state_wire_u32(&wire, &mode); security_state_wire_u32(&wire, &offline);
    security_state_wire_u64(&wire, &catalog->pause_until); security_state_wire_u32(&wire, &catalog->count);
    if (reserved) return false;
    security_state_wire_u32(&wire, &reserved);
    uint8_t zero[16] = {};
    if (!wire.valid || memcmp(magic, update_magic, 8) || version != 1 || reserved || offline > 1 ||
            !catalog->generation || !catalog->epoch || catalog->epoch > catalog->generation ||
            !memcmp(catalog->instance, zero, 16) || !security_state_fingerprint_present(catalog->profile) ||
            catalog->count > SECURITY_UPDATE_MAX_PACKAGES ||
            size != UPDATE_HEADER_BYTES + (size_t)catalog->count * UPDATE_ROW_BYTES ||
            !update_mode_valid(mode, catalog->pause_until)) return false;
    catalog->mode = (SecurityUpdateMode)mode; catalog->offline = offline != 0;
    for (uint32_t i = 0; i < catalog->count; i++) {
        UpdateRow* row = &catalog->rows[i]; SecurityUpdatePackageSpec* spec = &row->info.spec;
        update_authority_wire(&wire, spec);
        mode = row->info.mode; security_state_wire_u32(&wire, &mode);
        // Validate the wire integer before converting to a C++ enum.
        if (mode < SECURITY_UPDATE_ENABLED || mode > SECURITY_UPDATE_DISABLED) return false;
        row->info.mode = (SecurityUpdateMode)mode;
        security_state_wire_u64(&wire, &row->info.pause_until); security_state_wire_u32(&wire, &spec->poll_seconds);
        security_state_wire_u32(&wire, &spec->job_seconds); security_state_wire_u64(&wire, &spec->max_download_bytes);
        security_state_wire_u64(&wire, &row->info.next_poll); security_state_wire_u64(&wire, &row->info.lease_until);
        security_state_wire_u64(&wire, &row->nonce); security_state_wire_u32(&wire, &row->info.failures);
        security_state_wire_u32(&wire, &reserved);
        if (!wire.valid || reserved || !update_spec_valid(spec) || !update_mode_valid(row->info.mode, row->info.pause_until) ||
                row->info.failures > 16 || (!!row->nonce != !!row->info.lease_until)) return false;
        for (uint32_t j = spec->origin_count; j < SECURITY_UPDATE_MAX_ORIGINS; j++) if (spec->origins[j][0]) return false;
        for (uint32_t j = 0; j < i; j++) if (!strcmp(catalog->rows[j].info.spec.package, spec->package)) return false;
    }
    return wire.valid && wire.offset == size;
}

static SecurityUpdateResult update_catalog_read(SecurityStateTransaction* transaction, UpdateCatalog** out) {
    *out = nullptr; char* bytes = nullptr; size_t size = 0;
    SecurityStoreResult status = security_state_read(transaction, &bytes, &size);
    if (status != SECURITY_STORE_OK) return update_storage_result(status);
    uint8_t digest[32];
    bool valid = size >= UPDATE_HEADER_BYTES + UPDATE_CHECKSUM_BYTES &&
        digest_sha256(bytes, size - UPDATE_CHECKSUM_BYTES, digest) && !memcmp(digest, bytes + size - UPDATE_CHECKSUM_BYTES, 32);
    UpdateCatalog* catalog = valid ? (UpdateCatalog*)mem_calloc(1, sizeof(*catalog), MEM_CAT_SYSTEM) : nullptr;
    SecurityUpdateResult result = valid ? SECURITY_UPDATE_IO_ERROR : SECURITY_UPDATE_CORRUPT;
    if (catalog) {
        valid = update_catalog_wire(catalog, (uint8_t*)bytes, size - UPDATE_CHECKSUM_BYTES, false);
        result = valid ? SECURITY_UPDATE_OK : SECURITY_UPDATE_CORRUPT;
        if (valid) {
            uint8_t profile[32];
            if (!security_state_profile(transaction, profile)) result = SECURITY_UPDATE_IO_ERROR;
            else if (memcmp(profile, catalog->profile, 32)) result = SECURITY_UPDATE_IDENTITY_CHANGED;
        }
    }
    mem_free(bytes);
    if (result == SECURITY_UPDATE_OK) *out = catalog;
    else mem_free(catalog);
    return result;
}
static SecurityUpdateResult update_catalog_publish(SecurityStateTransaction* transaction, UpdateCatalog* catalog) {
    if (catalog->generation == UINT64_MAX) return SECURITY_UPDATE_INVALID;
    catalog->generation++;
    size_t payload = UPDATE_HEADER_BYTES + (size_t)catalog->count * UPDATE_ROW_BYTES;
    uint8_t* bytes = (uint8_t*)mem_alloc(payload + UPDATE_CHECKSUM_BYTES, MEM_CAT_SYSTEM);
    if (!bytes) return SECURITY_UPDATE_IO_ERROR;
    bool valid = update_catalog_wire(catalog, bytes, payload, true) && digest_sha256(bytes, payload, bytes + payload);
    SecurityUpdateResult result = valid ? update_storage_result(security_state_publish(transaction, bytes, payload + UPDATE_CHECKSUM_BYTES))
        : SECURITY_UPDATE_INVALID;
    mem_free(bytes); return result;
}

static void update_operation_begin(UpdateOperation* operation, SecurityUpdateControl* control) {
    *operation = {}; operation->control = control; operation->result = SECURITY_UPDATE_INVALID;
    if (!control) return;
    pthread_mutex_lock(&control->mutex); operation->locked = true;
    if (control->closed) { operation->result = SECURITY_UPDATE_CLOSED; return; }
    if (control->invalidated) { operation->result = SECURITY_UPDATE_IDENTITY_CHANGED; return; }
    SecurityStoreResult status;
    operation->transaction = security_state_begin(control->store, SECURITY_STATE_PACKAGE_UPDATES, &status);
    if (!operation->transaction) { operation->result = update_storage_result(status); return; }
    operation->result = update_catalog_read(operation->transaction, &operation->catalog);
    if (operation->result == SECURITY_UPDATE_OK) {
        if (memcmp(control->instance, operation->catalog->instance, 16) ||
                operation->catalog->generation < control->observed_generation || operation->catalog->epoch < control->observed_epoch)
            operation->result = SECURITY_UPDATE_IDENTITY_CHANGED;
        else { control->observed_generation = operation->catalog->generation; control->observed_epoch = operation->catalog->epoch; }
    }
    if (operation->result == SECURITY_UPDATE_IDENTITY_CHANGED) control->invalidated = true;
}
static void update_operation_end(UpdateOperation* operation) {
    mem_free(operation->catalog); security_state_end(operation->transaction);
    if (operation->locked) pthread_mutex_unlock(&operation->control->mutex);
}
struct UpdateGuard {
    UpdateOperation operation;
    explicit UpdateGuard(SecurityUpdateControl* control) { update_operation_begin(&operation, control); }
    ~UpdateGuard() { update_operation_end(&operation); }
};
static SecurityUpdateResult update_operation_publish(UpdateOperation* operation) {
    SecurityUpdateResult result = update_catalog_publish(operation->transaction, operation->catalog);
    if (result == SECURITY_UPDATE_OK) {
        operation->control->observed_generation = operation->catalog->generation;
        operation->control->observed_epoch = operation->catalog->epoch;
    }
    return result;
}
static UpdateRow* update_find(UpdateCatalog* catalog, const char* package) {
    if (package) for (uint32_t i = 0; i < catalog->count; i++)
        if (!strcmp(catalog->rows[i].info.spec.package, package)) return &catalog->rows[i];
    return nullptr;
}
static bool update_policy_changed(UpdateCatalog* catalog) {
    if (catalog->epoch == UINT64_MAX) return false;
    catalog->epoch++;
    // A control acknowledgment invalidates all old tickets, including tickets
    // from another host instance and tickets issued before disable/restore.
    for (uint32_t i = 0; i < catalog->count; i++) { catalog->rows[i].nonce = 0; catalog->rows[i].info.lease_until = 0; }
    return true;
}

SecurityUpdateResult security_update_initialize(SecurityGrantStore* store) {
    SecurityStoreResult status;
    SecurityStateTransaction* transaction = security_state_begin(store, SECURITY_STATE_PACKAGE_UPDATES, &status);
    if (!transaction) return update_storage_result(status);
    char* bytes = nullptr; size_t size = 0;
    status = security_state_read(transaction, &bytes, &size); mem_free(bytes);
    SecurityUpdateResult result = update_storage_result(status);
    if (status == SECURITY_STORE_OK) result = SECURITY_UPDATE_CONFLICT;
    if (status == SECURITY_STORE_MISSING) {
        UpdateCatalog* catalog = (UpdateCatalog*)mem_calloc(1, sizeof(*catalog), MEM_CAT_SYSTEM);
        if (catalog) {
            catalog->epoch = 1; catalog->mode = SECURITY_UPDATE_ENABLED;
            if (crypto_random_bytes(catalog->instance, sizeof(catalog->instance)) && security_state_profile(transaction, catalog->profile))
                result = update_catalog_publish(transaction, catalog);
            else result = SECURITY_UPDATE_IO_ERROR;
            mem_free(catalog);
        } else result = SECURITY_UPDATE_IO_ERROR;
    }
    security_state_end(transaction); return result;
}
SecurityUpdateControl* security_update_open(SecurityGrantStore* store, uint64_t (*clock)(void*),
        void* context, SecurityUpdateResult* result) {
    if (result) *result = SECURITY_UPDATE_INVALID;
    if (!store || !clock) return nullptr;
    SecurityStoreResult status;
    SecurityStateTransaction* transaction = security_state_begin(store, SECURITY_STATE_PACKAGE_UPDATES, &status);
    if (!transaction) { if (result) *result = update_storage_result(status); return nullptr; }
    UpdateCatalog* catalog = nullptr;
    SecurityUpdateResult loaded = update_catalog_read(transaction, &catalog);
    SecurityUpdateControl* control = loaded == SECURITY_UPDATE_OK ? (SecurityUpdateControl*)mem_calloc(1, sizeof(*control), MEM_CAT_SYSTEM) : nullptr;
    if (control && pthread_mutex_init(&control->mutex, nullptr)) { mem_free(control); control = nullptr; }
    if (control) {
        control->store = store; control->clock = clock; control->clock_context = context;
        control->references = 1; memcpy(control->instance, catalog->instance, 16);
        control->observed_generation = catalog->generation; control->observed_epoch = catalog->epoch;
    } else if (loaded == SECURITY_UPDATE_OK) loaded = SECURITY_UPDATE_IO_ERROR;
    if (result) *result = loaded;
    mem_free(catalog); security_state_end(transaction); return control;
}
static void update_control_release(SecurityUpdateControl* control, bool closing) {
    pthread_mutex_lock(&control->mutex);
    if (closing && control->closed) { pthread_mutex_unlock(&control->mutex); return; }
    if (closing) control->closed = true;
    bool destroy = --control->references == 0;
    pthread_mutex_unlock(&control->mutex);
    if (destroy) { pthread_mutex_destroy(&control->mutex); mem_free(control); }
}
void security_update_close(SecurityUpdateControl* control) { if (control) update_control_release(control, true); }
void security_update_snapshot_free(SecurityUpdateSnapshot* snapshot) {
    if (!snapshot) return;
    mem_free(snapshot->packages); *snapshot = {};
}
SecurityUpdateResult security_update_inspect(SecurityUpdateControl* control, SecurityUpdateSnapshot* out) {
    if (!out) return SECURITY_UPDATE_INVALID;
    *out = {}; UpdateGuard guard(control); UpdateOperation* operation = &guard.operation;
    if (operation->result != SECURITY_UPDATE_OK) return operation->result;
    UpdateCatalog* catalog = operation->catalog;
    out->packages = catalog->count ? (SecurityUpdatePackageInfo*)mem_calloc(catalog->count, sizeof(*out->packages), MEM_CAT_SYSTEM) : nullptr;
    if (catalog->count && !out->packages) return SECURITY_UPDATE_IO_ERROR;
    out->generation = catalog->generation; out->policy_epoch = catalog->epoch;
    out->offline = catalog->offline; out->mode = catalog->mode; out->pause_until = catalog->pause_until; out->count = catalog->count;
    for (uint32_t i = 0; i < catalog->count; i++) out->packages[i] = catalog->rows[i].info;
    return SECURITY_UPDATE_OK;
}
SecurityUpdateResult security_update_admit(SecurityUpdateControl* control, const SecurityUpdatePackageSpec* spec, bool revalidate) {
    if (!update_spec_valid(spec)) return SECURITY_UPDATE_INVALID;
    UpdateGuard guard(control); UpdateOperation* operation = &guard.operation;
    if (operation->result != SECURITY_UPDATE_OK) return operation->result;
    UpdateCatalog* catalog = operation->catalog; UpdateRow* row = update_find(catalog, spec->package);
    if (row && update_spec_equal(&row->info.spec, spec)) return SECURITY_UPDATE_OK;
    if (row && !revalidate) return SECURITY_UPDATE_IDENTITY_CHANGED;
    if (!row) {
        if (catalog->count == SECURITY_UPDATE_MAX_PACKAGES) return SECURITY_UPDATE_INVALID;
        uint64_t now = control->clock(control->clock_context);
        if (now > UINT64_MAX - 604800) return SECURITY_UPDATE_INVALID;
        row = &catalog->rows[catalog->count++]; row->info.mode = SECURITY_UPDATE_ENABLED;
        row->info.next_poll = now;
    }
    update_spec_copy(&row->info.spec, spec);
    if (!update_policy_changed(catalog)) return SECURITY_UPDATE_INVALID;
    return update_operation_publish(operation);
}
SecurityUpdateResult security_update_set_mode(SecurityUpdateControl* control, const char* package,
        SecurityUpdateMode mode, uint64_t pause_until) {
    if (!update_mode_valid(mode, pause_until)) return SECURITY_UPDATE_INVALID;
    UpdateGuard guard(control); UpdateOperation* operation = &guard.operation;
    if (operation->result != SECURITY_UPDATE_OK) return operation->result;
    UpdateRow* row = package ? update_find(operation->catalog, package) : nullptr;
    if (package && !row) return SECURITY_UPDATE_MISSING;
    if (row) { row->info.mode = mode; row->info.pause_until = pause_until; }
    else { operation->catalog->mode = mode; operation->catalog->pause_until = pause_until; }
    if (!update_policy_changed(operation->catalog)) return SECURITY_UPDATE_INVALID;
    return update_operation_publish(operation);
}
SecurityUpdateResult security_update_set_offline(SecurityUpdateControl* control, bool offline) {
    UpdateGuard guard(control); UpdateOperation* operation = &guard.operation;
    if (operation->result != SECURITY_UPDATE_OK) return operation->result;
    operation->catalog->offline = offline;
    if (!update_policy_changed(operation->catalog)) return SECURITY_UPDATE_INVALID;
    return update_operation_publish(operation);
}

static SecurityUpdateResult update_enabled(UpdateCatalog* catalog, UpdateRow* row, uint64_t now, bool manual) {
    if (catalog->offline) return SECURITY_UPDATE_OFFLINE;
    if (manual) return SECURITY_UPDATE_OK;
    const SecurityUpdateMode modes[] = {catalog->mode, row->info.mode};
    const uint64_t pauses[] = {catalog->pause_until, row->info.pause_until};
    for (size_t i = 0; i < 2; i++) {
        if (modes[i] == SECURITY_UPDATE_DISABLED) return SECURITY_UPDATE_DISABLE_ACTIVE;
        if (modes[i] == SECURITY_UPDATE_PAUSED && (!pauses[i] || now < pauses[i])) return SECURITY_UPDATE_PAUSE_ACTIVE;
    }
    return SECURITY_UPDATE_OK;
}
SecurityUpdateJob* security_update_begin(SecurityUpdateControl* control, const char* package, bool manual, SecurityUpdateResult* result) {
    UpdateGuard guard(control); UpdateOperation* operation = &guard.operation;
    SecurityUpdateResult status = operation->result;
    SecurityUpdateJob* job = nullptr;
    UpdateRow* row = status == SECURITY_UPDATE_OK ? update_find(operation->catalog, package) : nullptr;
    if (status == SECURITY_UPDATE_OK && !row) status = SECURITY_UPDATE_MISSING;
    uint64_t started_ms = time_now_ms();
    uint64_t now = status == SECURITY_UPDATE_OK ? control->clock(control->clock_context) : 0;
    if (status == SECURITY_UPDATE_OK) status = update_enabled(operation->catalog, row, now, manual);
    if (status == SECURITY_UPDATE_OK && row->nonce && now < row->info.lease_until) status = SECURITY_UPDATE_BUSY;
    if (status == SECURITY_UPDATE_OK && !manual && now < row->info.next_poll) status = SECURITY_UPDATE_NOT_DUE;
    if (status == SECURITY_UPDATE_OK && now > UINT64_MAX - 604800) status = SECURITY_UPDATE_INVALID;
    if (status == SECURITY_UPDATE_OK) {
        job = (SecurityUpdateJob*)mem_calloc(1, sizeof(*job), MEM_CAT_SYSTEM);
        if (!job || !crypto_random_bytes(&row->nonce, sizeof(row->nonce)) || !row->nonce) status = SECURITY_UPDATE_IO_ERROR;
    }
    if (status == SECURITY_UPDATE_OK) {
        // round up the persisted seconds so another host cannot steal a live
        // monotonic reservation at the next clock tick; publication uses its budget.
        row->info.lease_until = now + row->info.spec.job_seconds + 1;
        uint64_t next = now + (manual ? 300 : row->info.spec.poll_seconds);
        if (next > row->info.next_poll) row->info.next_poll = next;
        status = update_operation_publish(operation);
        if (status == SECURITY_UPDATE_OK) {
            job->owner = control; control->references++;
            strcpy(job->package, row->info.spec.package); job->epoch = operation->catalog->epoch; job->nonce = row->nonce;
            job->manual = manual; job->deadline_ms = started_ms + (uint64_t)row->info.spec.job_seconds * 1000;
        }
    }
    if (status != SECURITY_UPDATE_OK) { mem_free(job); job = nullptr; }
    if (result) *result = status;
    return job;
}
static SecurityUpdateResult update_job_validate(UpdateOperation* operation, SecurityUpdateJob* job, UpdateRow** out) {
    SecurityUpdateResult result = operation->result;
    if (result == SECURITY_UPDATE_OK) {
        UpdateRow* row = update_find(operation->catalog, job->package);
        uint64_t now = job->owner->clock(job->owner->clock_context);
        if (job->finished || !row || job->epoch != operation->catalog->epoch || job->nonce != row->nonce ||
                now >= row->info.lease_until || time_now_ms() >= job->deadline_ms) result = SECURITY_UPDATE_REVOKED;
        else { *out = row; result = update_enabled(operation->catalog, row, now, job->manual); }
    }
    // Once observed cancelled/expired/invalid, a ticket never revives after a
    // clock rollback, recovery, or disable/restore cycle.
    if (result != SECURITY_UPDATE_OK) job->finished = true;
    return result;
}
SecurityUpdateResult security_update_job_check(SecurityUpdateJob* job) {
    if (!job) return SECURITY_UPDATE_INVALID;
    UpdateGuard guard(job->owner); UpdateRow* row;
    return update_job_validate(&guard.operation, job, &row);
}
SecurityUpdateResult security_update_job_describe(SecurityUpdateJob* job, SecurityUpdatePackageSpec* out) {
    if (!job || !out) return SECURITY_UPDATE_INVALID;
    memset(out, 0, sizeof(*out)); UpdateGuard guard(job->owner); UpdateRow* row;
    SecurityUpdateResult result = update_job_validate(&guard.operation, job, &row);
    if (result == SECURITY_UPDATE_OK) *out = row->info.spec;
    return result;
}
SecurityUpdateResult security_update_job_finish(SecurityUpdateJob* job, bool success) {
    if (!job) return SECURITY_UPDATE_INVALID;
    UpdateGuard guard(job->owner); UpdateOperation* operation = &guard.operation; UpdateRow* row;
    SecurityUpdateResult result = update_job_validate(operation, job, &row);
    if (result != SECURITY_UPDATE_OK) return result;
    uint64_t now = job->owner->clock(job->owner->clock_context);
    if (now > UINT64_MAX - 604800) return SECURITY_UPDATE_INVALID;
    row->nonce = 0; row->info.lease_until = 0;
    if (success) row->info.failures = 0;
    else if (row->info.failures < 16) row->info.failures++;
    uint64_t delay = row->info.spec.poll_seconds;
    if (!success) {
        delay = 300;
        for (uint32_t i = 1; i < row->info.failures && delay < 86400; i++) delay *= 2;
        if (delay > 86400) delay = 86400;
    }
    uint64_t next = now + delay;
    if (!job->manual || next > row->info.next_poll) row->info.next_poll = next;
    result = update_operation_publish(operation);
    if (result == SECURITY_UPDATE_OK) job->finished = true;
    return result;
}
void security_update_job_free(SecurityUpdateJob* job) {
    if (!job) return;
    (void)security_update_job_finish(job, false);
    update_control_release(job->owner, false); mem_free(job);
}
SecurityUpdateLease* security_update_job_enter(SecurityUpdateJob* job, SecurityUpdateResult* result) {
    if (result) *result = SECURITY_UPDATE_INVALID;
    if (!job) return nullptr;
    SecurityUpdateLease* lease = (SecurityUpdateLease*)mem_calloc(1, sizeof(*lease), MEM_CAT_SYSTEM);
    if (!lease) { if (result) *result = SECURITY_UPDATE_IO_ERROR; return nullptr; }
    update_operation_begin(&lease->operation, job->owner); UpdateRow* row;
    SecurityUpdateResult status = update_job_validate(&lease->operation, job, &row);
    if (result) *result = status;
    if (status != SECURITY_UPDATE_OK) { update_operation_end(&lease->operation); mem_free(lease); return nullptr; }
    job->owner->references++;
    lease->job = job;
    return lease;
}
void security_update_record_reject(SecurityUpdateLease* lease) {
    if (lease && lease->job) lease->job->finished = true;
}
bool security_update_job_binding(SecurityUpdateJob* job, uint8_t out[32]) {
    if (!out) return false;
    memset(out, 0, 32); SecurityUpdateResult result;
    SecurityUpdateLease* lease = security_update_job_enter(job, &result);
    if (!lease) return false;
    uint8_t bytes[64] = {};
    memcpy(bytes, lease->operation.catalog->instance, 16);
    write_le64(bytes + 16, job->nonce); write_le64(bytes + 24, job->epoch);
    UpdateRow* row = update_find(lease->operation.catalog, job->package);
    bool valid = row && security_update_authority_digest(&row->info.spec, bytes + 32) && digest_sha256(bytes, sizeof(bytes), out);
    security_update_job_leave(lease); return valid;
}
bool security_update_record_observe(SecurityUpdateLease* lease, uint64_t generation, const uint8_t digest[32]) {
    if (!lease || !generation || !digest || !security_state_fingerprint_present(lease->package_record)) return false;
    SecurityUpdateControl* control = lease->operation.control;
    for (uint32_t i = 0; i < control->metadata_count; i++) {
        auto* seen = &control->metadata[i];
        if (memcmp(seen->package, lease->package_record, 32)) continue;
        if (generation < seen->generation || (generation == seen->generation && memcmp(digest, seen->digest, 32))) {
            control->invalidated = true; security_update_record_reject(lease); return false;
        }
        seen->generation = generation; memcpy(seen->digest, digest, 32); return true;
    }
    if (control->metadata_count == SECURITY_UPDATE_MAX_PACKAGES) return false;
    auto* seen = &control->metadata[control->metadata_count++];
    memcpy(seen->package, lease->package_record, 32); seen->generation = generation; memcpy(seen->digest, digest, 32);
    return true;
}
SecurityUpdateLease* security_update_record_enter(SecurityUpdateControl* control, const char* package,
        SecurityUpdateJob* job, SecurityUpdatePackageSpec* out_spec, uint64_t* out_time,
        SecurityStateTransaction** out_state, SecurityUpdateResult* result) {
    if (result) *result = SECURITY_UPDATE_INVALID;
    if (out_state) *out_state = nullptr;
    if (out_spec) memset(out_spec, 0, sizeof(*out_spec));
    if (out_time) *out_time = 0;
    if (!out_spec || !out_time || !out_state || (job ? control || package : !control || !package)) return nullptr;
    SecurityUpdateResult status;
    SecurityStateTransaction* state = nullptr;
    SecurityUpdateLease* lease = security_update_storage_enter(control, job, &state, &status);
    if (job) { package = job->package; control = job->owner; }
    if (lease) {
        UpdateRow* row = update_find(lease->operation.catalog, package);
        uint8_t key[32];
        if (!row) status = SECURITY_UPDATE_MISSING;
        else if (!digest_sha256(package, strlen(package), key) ||
                !security_state_select_update_metadata(lease->operation.transaction, key)) status = SECURITY_UPDATE_IO_ERROR;
        if (status == SECURITY_UPDATE_OK) {
            memcpy(lease->package_record, key, 32);
            *out_spec = row->info.spec; *out_time = control->clock(control->clock_context);
            *out_state = lease->operation.transaction;
        } else { security_update_job_leave(lease); lease = nullptr; }
    }
    if (result) *result = status;
    return lease;
}
SecurityUpdateLease* security_update_storage_enter(SecurityUpdateControl* control, SecurityUpdateJob* job,
        SecurityStateTransaction** out_state, SecurityUpdateResult* result) {
    if (out_state) *out_state = nullptr;
    if (result) *result = SECURITY_UPDATE_INVALID;
    if (!out_state || (job ? control != nullptr : control == nullptr)) return nullptr;
    SecurityUpdateLease* lease = nullptr;
    SecurityUpdateResult status = SECURITY_UPDATE_IO_ERROR;
    if (job) lease = security_update_job_enter(job, &status);
    else {
        lease = (SecurityUpdateLease*)mem_calloc(1, sizeof(*lease), MEM_CAT_SYSTEM);
        if (lease) {
            update_operation_begin(&lease->operation, control); status = lease->operation.result;
            if (status == SECURITY_UPDATE_OK) control->references++;
            else { update_operation_end(&lease->operation); mem_free(lease); lease = nullptr; }
        }
    }
    if (lease) *out_state = lease->operation.transaction;
    if (result) *result = status;
    return lease;
}
void security_update_job_leave(SecurityUpdateLease* lease) {
    if (!lease) return;
    SecurityUpdateControl* control = lease->operation.control;
    update_operation_end(&lease->operation); mem_free(lease); update_control_release(control, false);
}
