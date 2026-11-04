#include "admission.h"
#include "private_storage.h"
#include "../../../lib/arraylist.h"
#include "../../../lib/digest.h"
#include "../../../lib/mem.h"
#include "../../../lib/path_str.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum { ADMISSION_MAX_SOURCES = 1024, ADMISSION_MAX_ENTRIES = 4096,
    ADMISSION_MAX_SOURCE_BYTES = 32 * 1024 * 1024, ADMISSION_MAX_SELECTIONS = 48 };
static const SecurityOperations admission_file_ops = SECURITY_READ | SECURITY_METADATA |
    SECURITY_CREATE | SECURITY_REPLACE | SECURITY_APPEND;
static const SecurityOperations admission_mutations = SECURITY_CREATE | SECURITY_REPLACE |
    SECURITY_APPEND | SECURITY_DELETE | SECURITY_RENAME;

struct AdmissionSource {
    char* path;
    FileRevision revision;
    uint8_t digest[32];
};
struct AdmissionGrant {
    SecurityPersistentGrant saved;
    uint64_t root;
    bool persistent;
};
struct SecurityAdmission {
    SecurityGrantStore* store;
    SecurityFileBroker* broker;
    SecurityGrantBinding binding;
    SecurityIOClient raw;
    SecurityPrivateStorage* storage;
    SecurityIOClient private_io;
    ArrayList* sources;
    ArrayList* grants;
    char* entry;
    char* store_directory;
    FileRevision store_identity;
    uint64_t session;
    uint64_t generation;
    uint64_t source_bytes;
    uint32_t visited;
    bool active;
    pthread_mutex_t mutex;
};

static bool admission_ok(SecurityIOResult result) {
    return result.decision == SECURITY_ALLOWED && !result.system_error;
}
static SecurityIOClient* admission_resource_client(SecurityAdmission* admission, const char* path) {
    return security_private_storage_path(path) ? &admission->private_io : &admission->raw;
}

struct AdmissionLease {
    SecurityAdmission* admission;
    SecurityGrantLease* lease;
};
static void admission_lease_begin(AdmissionLease* state, SecurityAdmission* admission) {
    *state = {admission, nullptr};
    if (!admission) return;
    pthread_mutex_lock(&admission->mutex);
    if (admission->active) state->lease = security_grant_store_lease(admission->store,
        &admission->binding, admission->generation, nullptr);
    if (!state->lease && admission->active) {
        admission->active = false;
        security_file_broker_restrict(admission->broker, 0);
    }
}
static void admission_lease_end(AdmissionLease* state) {
    security_grant_store_release(state->lease);
    if (state->admission) pthread_mutex_unlock(&state->admission->mutex);
}
// One operation owns the admission and persisted generation. Publication uses
// this same lease shape so revocation acknowledges after prior transfers leave.
struct AdmissionOperation {
    AdmissionLease state;
    explicit AdmissionOperation(SecurityAdmission* owner) { admission_lease_begin(&state, owner); }
    ~AdmissionOperation() { admission_lease_end(&state); }
    AdmissionOperation(const AdmissionOperation&) = delete;
    AdmissionOperation& operator=(const AdmissionOperation&) = delete;
    bool ready() const { return state.lease != nullptr; }
};

static bool admission_hash_text(DigestCtx* digest, const char* text) {
    size_t length = strlen(text);
    return digest_update_u64_le(digest, length) && digest_update(digest, text, length);
}

static bool admission_hash_object(DigestCtx* digest, const FileRevision* revision) {
    // Directory mtimes/size change on ordinary output; identity must not.
    return digest_update_u64_le(digest, revision->device) &&
        digest_update_u64_le(digest, revision->inode);
}

static bool admission_resource_digest(const SecurityFileRootIdentity* identity, uint8_t out[32]) {
    DigestCtx* digest = digest_ctx_new(256);
    bool valid = digest && admission_hash_text(digest, "lambda-selected-file-v1") &&
        digest_update_u64_le(digest, identity->is_file) && admission_hash_text(digest, identity->path) &&
        admission_hash_object(digest, identity->is_file ? &identity->file : &identity->directory) &&
        digest_finalize(digest, out, 32);
    digest_ctx_free(digest); return valid;
}

static bool admission_protect(SecurityAdmission* admission, const char* path,
        SecurityOperations denied, bool required) {
    if (!path || path[0] != '/') return false;
    // OS path aliases are resolved only by the trusted catalog provider. The
    // broker retains the acquired identity and revalidates its ancestor chain.
    char* actual = file_realpath(path);
    if (!actual && (required || errno != ENOENT)) return false;
    const char* selected = actual ? actual : path;
    char* parent = file_path_dirname(selected);
    char* canonical_parent = parent ? file_realpath(parent) : nullptr;
    const char* leaf = strrchr(selected, '/');
    bool valid = canonical_parent && leaf && (!leaf[1] ||
        security_file_broker_protect_name(admission->broker, canonical_parent, leaf + 1, denied));
    if (actual) valid = valid && security_file_broker_protect(admission->broker, actual, denied);
    mem_free(canonical_parent); mem_free(parent); mem_free(actual); return valid;
}

static bool admission_catalog(SecurityAdmission* admission, const SecurityAdmissionConfig* config) {
    char* setting = security_grant_store_directory(admission->store);
    char* lambda_dir = setting ? file_path_dirname(setting) : nullptr;
    char* home = lambda_dir ? file_path_dirname(lambda_dir) : nullptr;
    bool valid = setting && home && admission_protect(admission, setting, SECURITY_ALL_OPERATIONS, true) &&
        admission_protect(admission, lambda_dir, admission_mutations, true) &&
        admission_protect(admission, config->runtime_directory, admission_mutations, true);
    valid = valid && security_file_broker_protect_name(admission->broker, lambda_dir,
        "app-data", SECURITY_ALL_OPERATIONS);
    const char* private_names[] = {".ssh", ".aws", ".gnupg", ".config"};
    for (const char* name : private_names) {
        char* path = home ? file_path_join(home, name) : nullptr;
        valid = valid && path && admission_protect(admission, path, SECURITY_ALL_OPERATIONS, false);
        mem_free(path);
    }
#if defined(__APPLE__)
    const char* os_readonly[] = {"/System", "/usr", "/Library", "/Applications"};
    const char* os_private[] = {"/private/etc", "/private/var/db", "/dev"};
    const char* user_private[] = {"Library"};
    for (const char* name : user_private) {
        char* path = home ? file_path_join(home, name) : nullptr;
        valid = valid && path && admission_protect(admission, path, SECURITY_ALL_OPERATIONS, false);
        mem_free(path);
    }
#elif defined(__linux__)
    const char* os_readonly[] = {"/usr", "/opt", "/boot"};
    const char* os_private[] = {"/etc", "/dev", "/proc", "/sys"};
#else
    mem_free(home); mem_free(lambda_dir); mem_free(setting); return false;
#endif
#if defined(__APPLE__) || defined(__linux__)
    for (const char* path : os_readonly)
        valid = valid && admission_protect(admission, path, admission_mutations, false);
    for (const char* path : os_private)
        valid = valid && admission_protect(admission, path, SECURITY_ALL_OPERATIONS, false);
#endif
    for (uint32_t i = 0; valid && i < config->protection_count; i++)
        valid = admission_protect(admission, config->protections[i].directory,
            config->protections[i].denied, true);
    for (uint32_t i = 0; valid && i < config->shipped_count; i++)
        valid = admission_protect(admission, config->shipped_directories[i], admission_mutations, true);
    valid = valid && security_grant_store_identity(admission->store, &admission->store_identity);
    if (valid) { admission->store_directory = setting; setting = nullptr; }
    mem_free(home); mem_free(lambda_dir); mem_free(setting); return valid;
}

static bool admission_add_source(SecurityAdmission* admission, const char* path) {
    SecuritySourceCapture capture = {};
    SecurityIOResult result = security_file_broker_capture_source(admission->broker, path,
        1024 * 1024, MEM_CAT_SYSTEM, &capture);
    if (!admission_ok(result)) { security_source_capture_free(&capture); return false; }
    uint8_t hash[32]; bool valid = digest_sha256(capture.data, capture.size, hash);
    for (int i = 0; valid && i < admission->sources->length; i++) {
        AdmissionSource* prior = (AdmissionSource*)admission->sources->data[i];
        if (!strcmp(prior->path, capture.path)) {
            valid = file_revision_equal(&prior->revision, &capture.revision) && !memcmp(prior->digest, hash, 32);
            security_source_capture_free(&capture); return valid;
        }
    }
    valid = valid && admission->sources->length < ADMISSION_MAX_SOURCES &&
        capture.size <= ADMISSION_MAX_SOURCE_BYTES - admission->source_bytes;
    AdmissionSource* source = valid ? (AdmissionSource*)mem_alloc(sizeof(*source), MEM_CAT_SYSTEM) : nullptr;
    if (source) {
        source->path = capture.path; source->revision = capture.revision; memcpy(source->digest, hash, 32);
        valid = arraylist_append(admission->sources, source);
        if (valid) { capture.path = nullptr; admission->source_bytes += capture.size; }
        else mem_free(source);
    } else valid = false;
    security_source_capture_free(&capture); return valid;
}

static bool admission_scan(SecurityAdmission* admission, const char* path, unsigned depth) {
    if (depth > SECURITY_IO_PATH_DEPTH_LIMIT) return false;
    SecurityDirectory directory = {};
    if (!security_io_enumerate(&admission->raw, path, ADMISSION_MAX_ENTRIES, &directory, nullptr)) return false;
    bool valid = directory.count <= ADMISSION_MAX_ENTRIES - admission->visited;
    if (valid) admission->visited += directory.count;
    for (uint32_t i = 0; valid && i < directory.count; i++) {
        SecurityDirectoryEntry* entry = &directory.entries[i];
        char* child = file_path_join(path, entry->name);
        if (!child) { valid = false; break; }
        size_t length = strlen(entry->name);
        bool script = length >= 3 && entry->name[length - 3] == '.' &&
            (entry->name[length - 2] == 'l' || entry->name[length - 2] == 'L') &&
            (entry->name[length - 1] == 's' || entry->name[length - 1] == 'S');
        valid = entry->is_directory ? admission_scan(admission, child, depth + 1)
            : !script || admission_add_source(admission, child);
        mem_free(child);
    }
    security_io_directory_free(&directory); return valid;
}

static int admission_source_order(const void* left, const void* right) {
    return strcmp((*(AdmissionSource* const*)left)->path, (*(AdmissionSource* const*)right)->path);
}

static bool admission_code_digest(SecurityAdmission* admission) {
    qsort(admission->sources->data, admission->sources->length, sizeof(void*), admission_source_order);
    DigestCtx* digest = digest_ctx_new(256);
    bool valid = digest && admission_hash_text(digest, "lambda-source-manifest-v1") &&
        digest_update_u64_le(digest, admission->sources->length);
    for (int i = 0; valid && i < admission->sources->length; i++) {
        AdmissionSource* source = (AdmissionSource*)admission->sources->data[i];
        valid = admission_hash_text(digest, source->path) && admission_hash_object(digest, &source->revision) &&
            digest_update_u64_le(digest, source->revision.stat.size) &&
            digest_update_u64_le(digest, source->revision.stat.modified) &&
            digest_update_u64_le(digest, source->revision.modified_nsec) &&
            digest_update_u64_le(digest, source->revision.stat.created) &&
            digest_update_u64_le(digest, source->revision.stat.mode) &&
            digest_update(digest, source->digest, 32);
    }
    valid = valid && digest_finalize(digest, admission->binding.code, 32);
    digest_ctx_free(digest); return valid;
}

static SecurityIOResult admission_source(SecurityAdmission* admission, const char* path,
        uint64_t limit, MemCategory category, SecuritySourceCapture* capture) {
    // Virtual data mounts never fall through to a physical source namespace.
    if (security_private_storage_path(path)) return {SECURITY_DENIED, EACCES, false, {}};
    SecurityIOResult result = security_file_broker_capture_source(admission->broker, path, limit, category, capture);
    if (!admission_ok(result)) return result;
    uint8_t hash[32]; bool valid = digest_sha256(capture->data, capture->size, hash);
    AdmissionSource key = {}; key.path = capture->path;
    AdmissionSource* key_pointer = &key;
    AdmissionSource** found = (AdmissionSource**)bsearch(&key_pointer, admission->sources->data,
        admission->sources->length, sizeof(void*), admission_source_order);
    valid = valid && found && file_revision_equal(&(*found)->revision, &capture->revision) &&
        !memcmp((*found)->digest, hash, 32);
    if (!valid) {
        security_source_capture_free(capture);
        // A changed/unlisted module cannot run with this admission's saved grants.
        admission->active = false; security_file_broker_restrict(admission->broker, 0);
        return {SECURITY_REVOKED, ESTALE, false, {}};
    }
    return result;
}

static SecurityIOResult admission_read(void* owner, const char* path, bool source,
        uint64_t limit, MemCategory category, char** data, size_t* size) {
    if (data) *data = nullptr;
    if (size) *size = 0;
    SecurityAdmission* admission = (SecurityAdmission*)owner;
    AdmissionOperation operation(admission);
    if (!operation.ready()) return {SECURITY_REVOKED, EACCES, false, {}};
    if (!data || !size) return {SECURITY_INVALID_REQUEST, EINVAL, false, {}};
    if (!source) {
        SecurityIOClient* client = admission_resource_client(admission, path);
        return client->read(client->context, path, false, limit, category, data, size);
    }
    SecuritySourceCapture capture = {};
    SecurityIOResult result = admission_source(admission, path, limit, category, &capture);
    if (admission_ok(result)) { *data = capture.data; *size = capture.size; capture.data = nullptr; }
    security_source_capture_free(&capture); return result;
}

static SecurityIOResult admission_source_path(void* owner, const char* path, char** canonical) {
    if (canonical) *canonical = nullptr;
    SecurityAdmission* admission = (SecurityAdmission*)owner;
    AdmissionOperation operation(admission);
    if (!operation.ready()) return {SECURITY_REVOKED, EACCES, false, {}};
    if (!canonical) return {SECURITY_INVALID_REQUEST, EINVAL, false, {}};
    SecuritySourceCapture capture = {};
    SecurityIOResult result = admission_source(admission, path, 1024 * 1024, MEM_CAT_SYSTEM, &capture);
    if (admission_ok(result)) { *canonical = capture.path; capture.path = nullptr; }
    security_source_capture_free(&capture); return result;
}

static SecurityIOResult admission_write(void* owner, const char* path, const void* data,
        size_t size, bool append, bool atomic) {
    SecurityAdmission* admission = (SecurityAdmission*)owner; AdmissionOperation operation(admission);
    if (!operation.ready()) return {SECURITY_REVOKED, EACCES, false, {}};
    SecurityIOClient* client = admission_resource_client(admission, path);
    return client->write(client->context, path, data, size, append, atomic);
}
static SecurityIOResult admission_kind(void* owner, const char* path, int32_t* kind) {
    if (kind) *kind = -1;
    SecurityAdmission* admission = (SecurityAdmission*)owner; AdmissionOperation operation(admission);
    if (!operation.ready()) return {SECURITY_REVOKED, EACCES, false, {}};
    SecurityIOClient* client = admission_resource_client(admission, path);
    return client->input_kind(client->context, path, kind);
}
static SecurityIOResult admission_enumerate(void* owner, const char* path, uint32_t limit, SecurityDirectory* out) {
    if (out) *out = {};
    SecurityAdmission* admission = (SecurityAdmission*)owner; AdmissionOperation operation(admission);
    if (!operation.ready()) return {SECURITY_REVOKED, EACCES, false, {}};
    SecurityIOClient* client = admission_resource_client(admission, path);
    return client->enumerate(client->context, path, limit, out);
}
static SecurityIOResult admission_stat(void* owner, const char* path, FileStat* out) {
    if (out) *out = {};
    SecurityAdmission* admission = (SecurityAdmission*)owner; AdmissionOperation operation(admission);
    if (!operation.ready()) return {SECURITY_REVOKED, EACCES, false, {}};
    SecurityIOClient* client = admission_resource_client(admission, path);
    return client->stat(client->context, path, out);
}
static SecurityIOResult admission_stamp(void* owner, SecurityAuthorityStamp* out) {
    if (out) *out = {};
    SecurityAdmission* admission = (SecurityAdmission*)owner; AdmissionOperation operation(admission);
    if (!operation.ready()) return {SECURITY_REVOKED, EACCES, false, {}};
    return admission->raw.authority_stamp(admission->raw.context, out);
}

SecurityIOClient security_admission_client(SecurityAdmission* admission) {
    return {1, admission, admission_read, admission_write, admission_source_path,
        admission_kind, admission_enumerate, admission_stat, admission_stamp};
}

struct AdmissionDelivery {
    AdmissionLease state;
    SecurityAuthorityStamp expected;
};
static bool admission_delivery_check(void* context, void* token) {
    AdmissionDelivery* delivery = (AdmissionDelivery*)token;
    SecurityAdmission* admission = (SecurityAdmission*)context;
    if (!delivery || delivery->state.admission != admission || !delivery->state.lease || !admission->active) return false;
    SecurityAuthorityStamp current = {};
    SecurityIOResult result = admission->raw.authority_stamp(admission->raw.context, &current);
    return admission_ok(result) && current.session == delivery->expected.session && current.epoch == delivery->expected.epoch;
}
static void* admission_delivery_acquire(void* context, const SecurityAuthorityStamp* expected, SecurityIOResult* failure) {
    AdmissionDelivery* delivery = (AdmissionDelivery*)mem_calloc(1, sizeof(*delivery), MEM_CAT_SYSTEM);
    if (!delivery) { *failure = {SECURITY_LIMIT_EXCEEDED, ENOMEM, false, {}}; return nullptr; }
    admission_lease_begin(&delivery->state, (SecurityAdmission*)context);
    delivery->expected = *expected;
    if (admission_delivery_check(context, delivery)) return delivery;
    *failure = {SECURITY_REVOKED, EACCES, false, {}};
    admission_lease_end(&delivery->state); mem_free(delivery); return nullptr;
}
static void admission_delivery_release(void*, void* token) {
    AdmissionDelivery* delivery = (AdmissionDelivery*)token;
    if (!delivery) return;
    admission_lease_end(&delivery->state); mem_free(delivery);
}
SecurityChannelDeliveryGuard security_admission_delivery_guard(SecurityAdmission* admission) {
    return {admission, admission_stamp, admission_delivery_acquire, admission_delivery_check, admission_delivery_release, 250};
}

static void admission_grant_free(AdmissionGrant* grant) {
    mem_free((void*)grant->saved.locator); mem_free(grant);
}

static SecurityStoreResult admission_save(SecurityAdmission* admission) {
    SecurityPersistentGrant records[ADMISSION_MAX_SELECTIONS]; uint32_t count = 0;
    for (int i = 0; i < admission->grants->length; i++) {
        AdmissionGrant* grant = (AdmissionGrant*)admission->grants->data[i];
        if (grant->persistent) records[count++] = grant->saved;
    }
    uint64_t next = 0;
    SecurityStoreResult result = security_grant_store_save(admission->store, &admission->binding,
        admission->generation, records, count, &next);
    if (result == SECURITY_STORE_OK) admission->generation = next;
    return result;
}

static bool admission_selection_valid(const SecurityFileSelection* selection) {
    SecurityOperations allowed = admission_file_ops | (selection && selection->is_directory ? SECURITY_ENUMERATE : 0);
    return selection && selection->path && selection->path[0] == '/' && selection->operations &&
        !(selection->operations & ~allowed) && selection->recursive == selection->is_directory;
}

static AdmissionGrant* admission_acquire_selection(SecurityAdmission* admission,
        const SecurityFileSelection* selection) {
    if (!admission_selection_valid(selection) || admission->grants->length >= ADMISSION_MAX_SELECTIONS) return nullptr;
    char* parent = selection->is_directory ? mem_strdup(selection->path, MEM_CAT_SYSTEM) : file_path_dirname(selection->path);
    const char* leaf = selection->is_directory ? nullptr : strrchr(selection->path, '/');
    SecurityFileRootSpec spec = {parent, SECURITY_SCOPE_SELECTED, selection->operations,
        selection->operations, selection->expires_at, false, false, leaf ? leaf + 1 : nullptr};
    uint64_t root = parent ? security_file_broker_admit_root(admission->broker, &spec) : 0;
    mem_free(parent);
    SecurityFileRootIdentity identity;
    AdmissionGrant* grant = root && security_file_broker_root_identity(admission->broker, root, &identity) &&
        identity.is_file != selection->is_directory
        ? (AdmissionGrant*)mem_calloc(1, sizeof(*grant), MEM_CAT_SYSTEM) : nullptr;
    bool valid = grant && admission_resource_digest(&identity, grant->saved.resource);
    if (valid) {
        grant->saved.locator = mem_strdup(identity.path, MEM_CAT_SYSTEM);
        grant->saved.operations = selection->operations; grant->saved.expires_at = selection->expires_at;
        grant->saved.kind = selection->is_directory ? SECURITY_SAVED_DIRECTORY : SECURITY_SAVED_FILE;
        grant->saved.flags = SECURITY_SAVED_PERSISTENT | (selection->is_directory ? SECURITY_SAVED_RECURSIVE : 0);
        grant->root = root; grant->persistent = selection->persistent;
        valid = grant->saved.locator != nullptr;
    }
    if (!valid) {
        if (root) security_file_broker_revoke_root(admission->broker, root);
        if (grant) admission_grant_free(grant);
        return nullptr;
    }
    return grant;
}

static SecurityStoreResult admission_load(SecurityAdmission* admission, bool revalidate,
        SecurityGrantSnapshot* snapshot) {
    SecurityStoreResult result = security_grant_store_load(admission->store, &admission->binding, snapshot);
    if (result == SECURITY_STORE_IDENTITY_CHANGED && revalidate) {
        SecurityGrantSnapshot old = {};
        result = security_grant_store_inspect(admission->store, admission->binding.principal_kind,
            admission->binding.principal, &old);
        if (result == SECURITY_STORE_OK) {
            bool changed = memcmp(old.binding.code, admission->binding.code, 32) ||
                memcmp(old.binding.root, admission->binding.root, 32);
            uint64_t next = 0;
            if (changed) result = security_grant_store_rebind(admission->store, &old.binding,
                &admission->binding, old.generation, &next);
            if (result == SECURITY_STORE_OK)
                result = security_grant_store_load(admission->store, &admission->binding, snapshot);
        }
        security_grant_snapshot_destroy(&old);
    }
    if (result == SECURITY_STORE_MISSING) {
        uint64_t next = 0;
        result = security_grant_store_save(admission->store, &admission->binding, 0, nullptr, 0, &next);
        if (result == SECURITY_STORE_OK) { snapshot->binding = admission->binding; snapshot->generation = next; }
    }
    return result;
}

SecurityAdmission* security_admission_open(const SecurityAdmissionConfig* config, SecurityAdmissionReport* report) {
    if (report) *report = {SECURITY_STORE_INVALID, 0, 0, 0};
    if (!config || !config->store || !config->clock || !config->entry || config->entry[0] != '/' ||
            !config->runtime_directory || config->shipped_count > 4 || config->protection_count > 32 ||
            (config->shipped_count && !config->shipped_directories) ||
            (config->protection_count && !config->protections)) return nullptr;
    SecurityAdmission* admission = (SecurityAdmission*)mem_calloc(1, sizeof(*admission), MEM_CAT_SYSTEM);
    if (!admission) return nullptr;
    if (pthread_mutex_init(&admission->mutex, nullptr) != 0) { mem_free(admission); return nullptr; }
    admission->store = config->store;
    admission->sources = arraylist_new(0); admission->grants = arraylist_new(0);
    uint64_t principal_id = 0;
    bool valid = admission->sources && admission->grants &&
        crypto_random_bytes(&admission->session, sizeof(admission->session)) && admission->session &&
        crypto_random_bytes(&principal_id, sizeof(principal_id)) && principal_id;
    SecurityFileBrokerConfig limits = {{principal_id, admission->session, SECURITY_ALL_OPERATIONS, 256, 256},
        1024 * 1024, 16 * 1024 * 1024, 4 * 1024 * 1024, 16, 128, 60, config->clock, config->clock_context};
    admission->broker = valid ? security_file_broker_create(&limits) : nullptr;
    valid = valid && admission->broker && admission_catalog(admission, config);
    admission->raw = security_file_broker_client(admission->broker);
    char* parent = file_path_dirname(config->entry);
    const char* leaf = strrchr(config->entry, '/');
    SecurityFileRootSpec root = {config->project ? config->project : parent,
        config->project ? SECURITY_SCOPE_PROJECT : SECURITY_SCOPE_ENTRY,
        config->project ? admission_file_ops | SECURITY_ENUMERATE | SECURITY_LOAD_SOURCE
            : SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE,
        0, 0, true, true, config->project ? nullptr : leaf ? leaf + 1 : nullptr};
    uint64_t root_id = valid ? security_file_broker_admit_root(admission->broker, &root) : 0;
    mem_free(parent);
    SecurityFileRootIdentity identity;
    valid = root_id && security_file_broker_root_identity(admission->broker, root_id, &identity);
    DigestCtx* root_digest = valid ? digest_ctx_new(256) : nullptr;
    valid = valid && root_digest && admission_hash_text(root_digest, "lambda-ad-hoc-root-v1") &&
        digest_update_u64_le(root_digest, root.scope) && admission_hash_text(root_digest, identity.path) &&
        admission_hash_object(root_digest, &identity.directory);
    if (valid && config->project) valid = admission_scan(admission, identity.path, 0);
    for (uint32_t i = 0; valid && i < config->shipped_count; i++) {
        const char* path = config->shipped_directories[i];
        SecurityFileRootSpec shipped = {path, SECURITY_SCOPE_SHIPPED,
            SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE, 0, 0, true, false, nullptr};
        uint64_t shipped_id = security_file_broker_admit_root(admission->broker, &shipped);
        SecurityFileRootIdentity shipped_identity;
        valid = shipped_id && security_file_broker_root_identity(admission->broker, shipped_id, &shipped_identity);
        if (!valid) break;
        // Enumeration is a temporary host admission right, never a worker grant.
        SecurityFileRootSpec scan = {shipped_identity.path, SECURITY_SCOPE_PROJECT,
            SECURITY_READ | SECURITY_METADATA | SECURITY_ENUMERATE | SECURITY_LOAD_SOURCE, 0, 0, true, false, nullptr};
        uint64_t scan_id = security_file_broker_admit_root(admission->broker, &scan);
        valid = scan_id && admission_scan(admission, shipped_identity.path, 0) &&
            admission_hash_text(root_digest, shipped_identity.path) &&
            admission_hash_object(root_digest, &shipped_identity.directory);
        if (scan_id) valid = security_file_broker_revoke_root(admission->broker, scan_id) && valid;
    }
    SecuritySourceCapture entry = {};
    if (valid) valid = admission_ok(security_file_broker_capture_source(admission->broker,
        config->entry, 1024 * 1024, MEM_CAT_SYSTEM, &entry));
    if (valid) { admission->entry = entry.path; entry.path = nullptr; }
    security_source_capture_free(&entry);
    valid = valid && admission_add_source(admission, config->entry) && admission_code_digest(admission) &&
        digest_finalize(root_digest, admission->binding.root, 32);
    digest_ctx_free(root_digest);
    admission->binding.principal_kind = SECURITY_PRINCIPAL_SCRIPT;
    DigestCtx* principal = valid ? digest_ctx_new(256) : nullptr;
    valid = valid && principal && admission_hash_text(principal, "lambda-ad-hoc-principal-v1") &&
        admission_hash_text(principal, admission->store_directory) &&
        admission_hash_object(principal, &admission->store_identity) &&
        admission_hash_text(principal, admission->entry) && digest_finalize(principal, admission->binding.principal, 32);
    digest_ctx_free(principal);
    SecurityGrantSnapshot snapshot = {};
    SecurityStoreResult store_result = valid ? admission_load(admission,
        config->revalidate_changed_identity, &snapshot) : SECURITY_STORE_UNSAFE_PATH;
    uint32_t restored = 0, rejected = 0;
    if (store_result == SECURITY_STORE_OK) {
        admission->generation = snapshot.generation;
        for (uint32_t i = 0; i < snapshot.count; i++) {
            SecurityPersistentGrant* saved = &snapshot.grants[i];
            SecurityFileSelection selected = {saved->locator, saved->operations, saved->expires_at,
                saved->kind == SECURITY_SAVED_DIRECTORY, (saved->flags & SECURITY_SAVED_RECURSIVE) != 0, true};
            AdmissionGrant* grant = (saved->kind == SECURITY_SAVED_DIRECTORY || saved->kind == SECURITY_SAVED_FILE) &&
                (!saved->expires_at || config->clock(config->clock_context) < saved->expires_at)
                ? admission_acquire_selection(admission, &selected) : nullptr;
            if (grant && !memcmp(grant->saved.resource, saved->resource, 32) &&
                    arraylist_append(admission->grants, grant)) { restored++; continue; }
            if (grant) { security_file_broker_revoke_root(admission->broker, grant->root); admission_grant_free(grant); }
            rejected++;
        }
    }
    security_grant_snapshot_destroy(&snapshot);
    if (report) *report = {store_result, admission->generation, restored, rejected};
    if (store_result != SECURITY_STORE_OK) { security_admission_close(admission); return nullptr; }
    // The virtual mount is bound by this admitted channel, never by a worker's
    // claimed ID/path. Its fixed private roots share this admission's revocation
    // lease; no private operation can outlive a changed source or grant binding.
    SecurityPrivateStorageConfig storage = {config->store, admission->binding, limits, {}};
    security_private_storage_default_limits(storage.limits);
    admission->storage = security_private_storage_open(&storage);
    if (!admission->storage) { security_admission_close(admission); return nullptr; }
    admission->private_io = security_private_storage_client(admission->storage);
    admission->active = true; return admission;
}

bool security_admission_finish(SecurityAdmission* admission) {
    if (!admission) return true;
    pthread_mutex_lock(&admission->mutex);
    if (admission->active) {
        admission->active = false; security_file_broker_restrict(admission->broker, 0);
    }
    // No store lease crosses host cleanup. Joined borrowers cannot publish IO
    // after the session closes, even if temporary removal must be retried.
    bool valid = security_private_storage_finish(admission->storage);
    int error = errno; pthread_mutex_unlock(&admission->mutex); errno = error; return valid;
}

void security_admission_close(SecurityAdmission* admission) {
    if (!admission) return;
    (void)security_admission_finish(admission);
    if (admission->sources) {
        for (int i = 0; i < admission->sources->length; i++) {
            AdmissionSource* source = (AdmissionSource*)admission->sources->data[i];
            mem_free(source->path); mem_free(source);
        }
        arraylist_free(admission->sources);
    }
    if (admission->grants) {
        for (int i = 0; i < admission->grants->length; i++) admission_grant_free((AdmissionGrant*)admission->grants->data[i]);
        arraylist_free(admission->grants);
    }
    security_private_storage_close(admission->storage);
    security_file_broker_destroy(admission->broker); mem_free(admission->entry); mem_free(admission->store_directory);
    pthread_mutex_destroy(&admission->mutex); mem_free(admission);
}

uint64_t security_admission_session(const SecurityAdmission* admission) { return admission ? admission->session : 0; }
const char* security_admission_entry(const SecurityAdmission* admission) { return admission ? admission->entry : nullptr; }
bool security_admission_binding(const SecurityAdmission* admission, SecurityGrantBinding* out) {
    if (out) *out = {};
    if (!admission || !out) return false;
    *out = admission->binding; return true;
}

uint64_t security_admission_select(SecurityAdmission* admission, const SecurityFileSelection* selection,
        SecurityStoreResult* result) {
    if (result) *result = SECURITY_STORE_INVALID;
    if (!admission || !admission_selection_valid(selection)) return 0;
    pthread_mutex_lock(&admission->mutex);
    SecurityStoreResult status;
    SecurityGrantLease* lease = admission->active ? security_grant_store_lease(admission->store,
        &admission->binding, admission->generation, &status) : nullptr;
    if (!admission->active) status = SECURITY_STORE_UNAVAILABLE;
    bool validated = lease != nullptr;
    security_grant_store_release(lease);
    AdmissionGrant* grant = validated ? admission_acquire_selection(admission, selection) : nullptr;
    if (grant) {
        for (int i = 0; i < admission->grants->length; i++)
            if (!memcmp(((AdmissionGrant*)admission->grants->data[i])->saved.resource, grant->saved.resource, 32)) {
                security_file_broker_revoke_root(admission->broker, grant->root);
                admission_grant_free(grant); grant = nullptr; break;
            }
    }
    bool appended = grant && arraylist_append(admission->grants, grant);
    if (appended) status = selection->persistent ? admission_save(admission) : SECURITY_STORE_OK;
    else if (validated) status = SECURITY_STORE_INVALID;
    if (grant && (!appended || status != SECURITY_STORE_OK)) {
        if (appended) arraylist_remove(admission->grants, admission->grants->length - 1);
        security_file_broker_revoke_root(admission->broker, grant->root); admission_grant_free(grant); grant = nullptr;
    }
    uint64_t id = grant ? grant->root : 0;
    pthread_mutex_unlock(&admission->mutex);
    if (result) *result = status;
    return id;
}

SecurityStoreResult security_admission_revoke(SecurityAdmission* admission, uint64_t resource) {
    if (!admission || !resource) return SECURITY_STORE_INVALID;
    pthread_mutex_lock(&admission->mutex);
    SecurityStoreResult result = SECURITY_STORE_INVALID;
    for (int i = 0; i < admission->grants->length; i++) {
        AdmissionGrant* grant = (AdmissionGrant*)admission->grants->data[i];
        if (grant->root != resource) continue;
        arraylist_remove(admission->grants, i);
        result = grant->persistent ? admission_save(admission) : SECURITY_STORE_OK;
        security_file_broker_revoke_root(admission->broker, resource);
        admission_grant_free(grant);
        if (result != SECURITY_STORE_OK) { admission->active = false; security_file_broker_restrict(admission->broker, 0); }
        break;
    }
    pthread_mutex_unlock(&admission->mutex); return result;
}

SecurityStoreResult security_admission_inspect(SecurityAdmission* admission, SecurityGrantSnapshot* out) {
    if (!admission) return SECURITY_STORE_INVALID;
    return security_grant_store_load(admission->store, &admission->binding, out);
}
bool security_admission_describe(SecurityAdmission* admission, SecurityApprovalRef approval, SecurityOverwriteChallenge* out) {
    AdmissionOperation operation(admission);
    return operation.ready() && security_file_broker_describe(admission->broker, approval, out);
}
SecurityIOResult security_admission_approve(SecurityAdmission* admission, SecurityApprovalRef approval) {
    AdmissionOperation operation(admission);
    if (!operation.ready()) return {SECURITY_REVOKED, EACCES, false, {}};
    return security_file_broker_approve(admission->broker, approval);
}
bool security_admission_cancel(SecurityAdmission* admission, SecurityApprovalRef approval) {
    AdmissionOperation operation(admission);
    return operation.ready() && security_file_broker_cancel(admission->broker, approval);
}
