#include "grant_store.h"
#include "state_store.h"
#include "../../../lib/file.h"
#include "../../../lib/digest.h"
#include "../../../lib/hex.h"
#include "../../../lib/mem.h"
#include <errno.h>
#include <pthread.h>
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#include <sys/file.h>
#endif

struct SecurityGrantStore {
    FileAnchor* directory;
    FileAnchor* profile;
    FILE* lock;
    pthread_mutex_t mutex;
    bool unavailable;
};
struct SecurityGrantLease { SecurityGrantStore* store; };
struct SecurityStateTransaction {
    SecurityGrantStore* store;
    char name[96];
    FileRevision revision;
    bool ready;
    bool missing;
};

static const uint8_t grant_store_magic[8] = {'L','M','D','G','R','N','0','1'};
enum { GRANT_HEADER_BYTES = 128, GRANT_ROW_BYTES = 64, GRANT_CHECKSUM_BYTES = 32 };

bool security_state_fingerprint_present(const uint8_t digest[32]) {
    if (!digest) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < 32; i++) bits |= digest[i];
    return bits != 0;
}

static bool grant_binding_valid(const SecurityGrantBinding* binding) {
    return binding && (binding->principal_kind == SECURITY_PRINCIPAL_APP ||
        binding->principal_kind == SECURITY_PRINCIPAL_SCRIPT) &&
        security_state_fingerprint_present(binding->principal) && security_state_fingerprint_present(binding->code) &&
        security_state_fingerprint_present(binding->root);
}

static bool grant_binding_equal(const SecurityGrantBinding* left, const SecurityGrantBinding* right) {
    return left->principal_kind == right->principal_kind &&
        !memcmp(left->principal, right->principal, 32) && !memcmp(left->code, right->code, 32) &&
        !memcmp(left->root, right->root, 32);
}

static bool grant_record_valid(const SecurityPersistentGrant* grant) {
    if (!grant || !security_state_fingerprint_present(grant->resource) || !grant->operations ||
        (grant->operations & ~((SecurityOperations)SECURITY_ALL_OPERATIONS)) ||
        !(grant->flags & SECURITY_SAVED_PERSISTENT) ||
        (grant->flags & ~(SECURITY_SAVED_PERSISTENT | SECURITY_SAVED_RECURSIVE)) ||
        ((grant->flags & SECURITY_SAVED_RECURSIVE) && grant->kind != SECURITY_SAVED_DIRECTORY) ||
        !grant->locator || !*grant->locator ||
        strnlen(grant->locator, SECURITY_STORE_MAX_LOCATOR + 1) > SECURITY_STORE_MAX_LOCATOR) return false;
    SecurityOperations allowed;
    switch (grant->kind) {
        case SECURITY_SAVED_FILE:
        case SECURITY_SAVED_DIRECTORY:
            allowed = SECURITY_READ | SECURITY_METADATA | SECURITY_ENUMERATE | SECURITY_CREATE |
                SECURITY_REPLACE | SECURITY_APPEND | SECURITY_DELETE | SECURITY_RENAME |
                SECURITY_WATCH | SECURITY_LOAD_SOURCE;
            if (grant->kind == SECURITY_SAVED_FILE) allowed &= ~((SecurityOperations)SECURITY_ENUMERATE);
            break;
        case SECURITY_SAVED_ENDPOINT: allowed = SECURITY_CONNECT | SECURITY_LISTEN; break;
        case SECURITY_SAVED_SERVICE: allowed = SECURITY_INVOKE; break;
        default: return false;
    }
    return !(grant->operations & ~allowed);
}

static bool grant_records_valid(const SecurityPersistentGrant* grants, uint32_t count) {
    if (count > SECURITY_STORE_MAX_GRANTS || (count && !grants)) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (!grant_record_valid(&grants[i])) return false;
        for (uint32_t j = 0; j < i; j++)
            if (!memcmp(grants[i].resource, grants[j].resource, 32)) return false;
    }
    return true;
}

static void grant_store_filename(const SecurityGrantBinding* binding, char name[71]) {
    hex_encode(binding->principal, 32, name);
    memcpy(name + 64, ".grant", 7);
}

static void grant_wire_put(uint8_t** cursor, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) *(*cursor)++ = (uint8_t)(value >> (i * 8));
}

static uint64_t grant_wire_get(const uint8_t** cursor, size_t bytes) {
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; i++) value |= (uint64_t)*(*cursor)++ << (i * 8);
    return value;
}

static uint8_t* grant_store_encode(const SecurityGrantBinding* binding, uint64_t generation,
        const SecurityPersistentGrant* grants, uint32_t count, size_t* out_size) {
    if (!grant_binding_valid(binding) || !generation || !grant_records_valid(grants, count)) return nullptr;
    size_t size = GRANT_HEADER_BYTES + GRANT_CHECKSUM_BYTES;
    for (uint32_t i = 0; i < count; i++) {
        size_t row = GRANT_ROW_BYTES + strlen(grants[i].locator);
        if (row > SECURITY_STORE_MAX_BYTES - size) return nullptr;
        size += row;
    }
    uint8_t* data = (uint8_t*)mem_alloc(size, MEM_CAT_SYSTEM);
    if (!data) return nullptr;
    uint8_t* cursor = data;
    memcpy(cursor, grant_store_magic, sizeof(grant_store_magic)); cursor += sizeof(grant_store_magic);
    grant_wire_put(&cursor, 1, 4);
    grant_wire_put(&cursor, binding->principal_kind, 4);
    grant_wire_put(&cursor, generation, 8);
    memcpy(cursor, binding->principal, 32); cursor += 32;
    memcpy(cursor, binding->code, 32); cursor += 32;
    memcpy(cursor, binding->root, 32); cursor += 32;
    grant_wire_put(&cursor, count, 4);
    grant_wire_put(&cursor, 0, 4);
    for (uint32_t i = 0; i < count; i++) {
        const SecurityPersistentGrant* grant = &grants[i];
        memcpy(cursor, grant->resource, 32); cursor += 32;
        grant_wire_put(&cursor, grant->operations, 8);
        grant_wire_put(&cursor, grant->expires_at, 8);
        grant_wire_put(&cursor, grant->kind, 4);
        grant_wire_put(&cursor, grant->flags, 4);
        size_t length = strlen(grant->locator);
        grant_wire_put(&cursor, length, 4);
        grant_wire_put(&cursor, 0, 4);
        memcpy(cursor, grant->locator, length); cursor += length;
    }
    if (!digest_sha256(data, size - GRANT_CHECKSUM_BYTES, cursor)) { mem_free(data); return nullptr; }
    *out_size = size;
    return data;
}

void security_grant_snapshot_destroy(SecurityGrantSnapshot* snapshot) {
    if (!snapshot) return;
    for (uint32_t i = 0; i < snapshot->count; i++) mem_free((void*)snapshot->grants[i].locator);
    mem_free(snapshot->grants);
    *snapshot = {};
}

static SecurityStoreResult grant_store_decode(const uint8_t* data, size_t size, SecurityGrantSnapshot* out) {
    if (size < GRANT_HEADER_BYTES + GRANT_CHECKSUM_BYTES || size > SECURITY_STORE_MAX_BYTES ||
        memcmp(data, grant_store_magic, sizeof(grant_store_magic))) return SECURITY_STORE_CORRUPT;
    uint8_t checksum[32];
    size_t payload = size - GRANT_CHECKSUM_BYTES;
    if (!digest_sha256(data, payload, checksum)) return SECURITY_STORE_IO_ERROR;
    if (memcmp(checksum, data + payload, 32)) return SECURITY_STORE_CORRUPT;
    const uint8_t* cursor = data + sizeof(grant_store_magic);
    if (grant_wire_get(&cursor, 4) != 1) return SECURITY_STORE_CORRUPT;
    out->binding.principal_kind = (uint32_t)grant_wire_get(&cursor, 4);
    out->generation = grant_wire_get(&cursor, 8);
    memcpy(out->binding.principal, cursor, 32); cursor += 32;
    memcpy(out->binding.code, cursor, 32); cursor += 32;
    memcpy(out->binding.root, cursor, 32); cursor += 32;
    uint32_t count = (uint32_t)grant_wire_get(&cursor, 4);
    if (grant_wire_get(&cursor, 4) || !out->generation || !grant_binding_valid(&out->binding) ||
        count > SECURITY_STORE_MAX_GRANTS) return SECURITY_STORE_CORRUPT;
    out->grants = count ? (SecurityPersistentGrant*)mem_calloc(count, sizeof(SecurityPersistentGrant), MEM_CAT_SYSTEM) : nullptr;
    if (count && !out->grants) return SECURITY_STORE_IO_ERROR;
    const uint8_t* end = data + payload;
    for (uint32_t i = 0; i < count; i++) {
        if ((size_t)(end - cursor) < GRANT_ROW_BYTES) return SECURITY_STORE_CORRUPT;
        SecurityPersistentGrant* grant = &out->grants[out->count++];
        memcpy(grant->resource, cursor, 32); cursor += 32;
        grant->operations = grant_wire_get(&cursor, 8);
        grant->expires_at = grant_wire_get(&cursor, 8);
        grant->kind = (uint32_t)grant_wire_get(&cursor, 4);
        grant->flags = (uint32_t)grant_wire_get(&cursor, 4);
        uint32_t length = (uint32_t)grant_wire_get(&cursor, 4);
        if (grant_wire_get(&cursor, 4) || !length || length > SECURITY_STORE_MAX_LOCATOR ||
            length > (size_t)(end - cursor) || memchr(cursor, '\0', length)) return SECURITY_STORE_CORRUPT;
        grant->locator = mem_dup_n((const char*)cursor, length, MEM_CAT_SYSTEM);
        if (!grant->locator) return SECURITY_STORE_IO_ERROR;
        cursor += length;
    }
    return cursor == end && grant_records_valid(out->grants, count) ? SECURITY_STORE_OK : SECURITY_STORE_CORRUPT;
}

static SecurityStoreResult grant_store_path_error(void) {
    if (errno == ENOTSUP) return SECURITY_STORE_UNSUPPORTED;
    if (errno == EACCES || errno == EPERM || errno == ELOOP || errno == ENOTDIR || errno == ESTALE)
        return SECURITY_STORE_UNSAFE_PATH;
    return SECURITY_STORE_IO_ERROR;
}

static SecurityGrantStore* grant_store_open_home(const char* home, SecurityStoreResult* result) {
    if (result) *result = SECURITY_STORE_INVALID;
    if (!home || home[0] != '/') return nullptr;
    const uint32_t directory_flags = FILE_ANCHOR_STRICT_STORAGE;
    FileAnchorOptions options = {FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_SAME_DEVICE, nullptr, nullptr};
    FileAnchor* home_dir = file_anchor_open(home, &options);
    FileAnchor* lambda_dir = home_dir ? file_anchor_directory(home_dir, ".lambda", true, directory_flags) : nullptr;
    FileAnchor* directory = lambda_dir ? file_anchor_directory(lambda_dir, "setting", true, directory_flags) : nullptr;
    int error = errno;
    file_anchor_close(home_dir);
    if (!directory) {
        file_anchor_close(lambda_dir); errno = error;
        if (result) *result = grant_store_path_error(); return nullptr;
    }
    FILE* lock = file_anchor_open_read(directory, ".grants.lock");
    if (!lock && errno == ENOENT) {
        lock = file_anchor_create_regular(directory, ".grants.lock");
        if (!lock && errno == EEXIST) lock = file_anchor_open_read(directory, ".grants.lock");
    }
    if (!lock) {
        int lock_error = errno;
        file_anchor_close(directory);
        file_anchor_close(lambda_dir);
        errno = lock_error;
        if (result) *result = grant_store_path_error();
        return nullptr;
    }
    SecurityGrantStore* store = (SecurityGrantStore*)mem_calloc(1, sizeof(SecurityGrantStore), MEM_CAT_SYSTEM);
    if (!store || pthread_mutex_init(&store->mutex, nullptr) != 0) {
        if (lock) fclose(lock);
        file_anchor_close(directory);
        file_anchor_close(lambda_dir);
        mem_free(store);
        if (result) *result = SECURITY_STORE_IO_ERROR;
        return nullptr;
    }
    store->directory = directory;
    store->profile = lambda_dir;
    store->lock = lock;
    if (result) *result = SECURITY_STORE_OK;
    return store;
}

char* security_grant_store_account_home(void) {
#ifdef _WIN32
    errno = ENOTSUP;
    return nullptr;
#else
    // getpwuid_r ignores script-supplied HOME and Lambda path overrides (D7.5.7).
    size_t capacity = 16384;
    char* home = nullptr;
    for (; capacity <= 1024 * 1024; capacity *= 2) {
        char* buffer = (char*)mem_alloc(capacity, MEM_CAT_SYSTEM);
        if (!buffer) { errno = ENOMEM; break; }
        struct passwd entry; struct passwd* found = nullptr;
        int status = getpwuid_r(geteuid(), &entry, buffer, capacity, &found);
        if (status) errno = status;
        else if (!found) errno = ENOENT;
        else if (!entry.pw_dir || entry.pw_dir[0] != '/') errno = EINVAL;
        else {
            home = mem_strdup(entry.pw_dir, MEM_CAT_SYSTEM);
            if (!home) errno = ENOMEM;
        }
        mem_free(buffer);
        if (status != ERANGE) break;
    }
    return home;
#endif
}

SecurityGrantStore* security_grant_store_open(SecurityStoreResult* result) {
    char* home = security_grant_store_account_home();
    if (!home) { if (result) *result = grant_store_path_error(); return nullptr; }
    SecurityGrantStore* store = grant_store_open_home(home, result);
    mem_free(home);
    return store;
}

#ifdef LAMBDA_SECURITY_STORE_TESTING
SecurityGrantStore* security_grant_store_test_open(const char* home, SecurityStoreResult* result) {
    return grant_store_open_home(home, result);
}
#endif

void security_grant_store_close(SecurityGrantStore* store) {
    if (!store) return;
    // callers join outstanding requests before destroying the host service
    fclose(store->lock);
    file_anchor_close(store->directory);
    file_anchor_close(store->profile);
    pthread_mutex_destroy(&store->mutex);
    mem_free(store);
}

static SecurityStoreResult grant_store_begin(SecurityGrantStore* store) {
    if (!store) return SECURITY_STORE_INVALID;
    pthread_mutex_lock(&store->mutex);
    SecurityStoreResult result = store->unavailable ? SECURITY_STORE_UNAVAILABLE : SECURITY_STORE_OK;
    if (result == SECURITY_STORE_OK && !file_anchor_validate(store->directory)) result = grant_store_path_error();
#ifndef _WIN32
    if (result == SECURITY_STORE_OK) {
        int status;
        do { status = flock(fileno(store->lock), LOCK_EX); } while (status != 0 && errno == EINTR);
        if (status != 0) result = SECURITY_STORE_IO_ERROR;
        else {
            FileRevision name;
            FileRevision held = file_revision_stream(store->lock);
            if (!file_anchor_stat(store->directory, ".grants.lock", &name) ||
                name.device != held.device || name.inode != held.inode) {
                flock(fileno(store->lock), LOCK_UN);
                result = SECURITY_STORE_UNSAFE_PATH;
            }
        }
    }
#else
    result = SECURITY_STORE_UNSUPPORTED;
#endif
    if (result != SECURITY_STORE_OK) pthread_mutex_unlock(&store->mutex);
    return result;
}

static void grant_store_end(SecurityGrantStore* store) {
#ifndef _WIN32
    flock(fileno(store->lock), LOCK_UN);
#endif
    pthread_mutex_unlock(&store->mutex);
}

FileAnchor* security_grant_store_private_root(SecurityGrantStore* store) {
    if (grant_store_begin(store) != SECURITY_STORE_OK) return nullptr;
    FileAnchor* result = file_anchor_directory(store->profile, "app-data", true,
        FILE_ANCHOR_STRICT_STORAGE);
    int error = errno; grant_store_end(store); errno = error; return result;
}
FileAnchor* security_state_package_staging(SecurityStateTransaction* transaction, bool create) {
    if (!transaction || !transaction->store) { errno = EINVAL; return nullptr; }
    return file_anchor_directory(transaction->store->profile, "package-staging", create, FILE_ANCHOR_STRICT_STORAGE);
}

char* security_grant_store_directory(SecurityGrantStore* store) {
    if (grant_store_begin(store) != SECURITY_STORE_OK) return nullptr;
    char* path = file_anchor_path(store->directory);
    grant_store_end(store);
    return path;
}

bool security_grant_store_identity(SecurityGrantStore* store, FileRevision* out) {
    if (out) *out = {};
    if (!out || grant_store_begin(store) != SECURITY_STORE_OK) return false;
    *out = file_anchor_identity(store->directory);
    grant_store_end(store); return true;
}

static SecurityStoreResult grant_store_read(SecurityGrantStore* store, const SecurityGrantBinding* binding,
        SecurityGrantSnapshot* out, FileRevision* revision, bool check_binding = true) {
    char name[71];
    grant_store_filename(binding, name);
    char* bytes = nullptr; size_t size = 0;
    if (!file_anchor_read_all(store->directory, name, SECURITY_STORE_MAX_BYTES, MEM_CAT_SYSTEM,
            &bytes, &size, revision)) {
        if (errno == ENOENT) return SECURITY_STORE_MISSING;
        if (errno == EFBIG) return SECURITY_STORE_CORRUPT;
        return grant_store_path_error();
    }
    SecurityStoreResult result = grant_store_decode((const uint8_t*)bytes, size, out);
    mem_free(bytes);
    if (result == SECURITY_STORE_OK && memcmp(out->binding.principal, binding->principal, 32))
        result = SECURITY_STORE_CORRUPT;
    if (result == SECURITY_STORE_OK && (out->binding.principal_kind != binding->principal_kind ||
            (check_binding && !grant_binding_equal(binding, &out->binding))))
        result = SECURITY_STORE_IDENTITY_CHANGED;
    if (result != SECURITY_STORE_OK) security_grant_snapshot_destroy(out);
    return result;
}

SecurityStoreResult security_grant_store_inspect(SecurityGrantStore* store,
        uint32_t kind, const uint8_t principal[32], SecurityGrantSnapshot* out) {
    if (out) *out = {};
    if (!out || !principal || !security_state_fingerprint_present(principal) ||
            (kind != SECURITY_PRINCIPAL_SCRIPT && kind != SECURITY_PRINCIPAL_APP)) return SECURITY_STORE_INVALID;
    SecurityGrantBinding binding = {}; binding.principal_kind = kind;
    memcpy(binding.principal, principal, sizeof(binding.principal));
    SecurityStoreResult result = grant_store_begin(store);
    if (result != SECURITY_STORE_OK) return result;
    FileRevision revision;
    result = grant_store_read(store, &binding, out, &revision, false);
    grant_store_end(store);
    return result;
}

SecurityStoreResult security_grant_store_load(SecurityGrantStore* store,
        const SecurityGrantBinding* binding, SecurityGrantSnapshot* out) {
    if (out) *out = {};
    if (!out || !grant_binding_valid(binding)) return SECURITY_STORE_INVALID;
    SecurityStoreResult result = grant_store_begin(store);
    if (result != SECURITY_STORE_OK) return result;
    FileRevision revision;
    result = grant_store_read(store, binding, out, &revision);
    grant_store_end(store);
    return result;
}

SecurityGrantLease* security_grant_store_lease(SecurityGrantStore* store,
        const SecurityGrantBinding* binding, uint64_t generation, SecurityStoreResult* result) {
    if (result) *result = SECURITY_STORE_INVALID;
    if (!grant_binding_valid(binding) || !generation) return nullptr;
    SecurityGrantLease* lease = (SecurityGrantLease*)mem_alloc(sizeof(*lease), MEM_CAT_SYSTEM);
    if (!lease) { if (result) *result = SECURITY_STORE_IO_ERROR; return nullptr; }
    SecurityStoreResult status = grant_store_begin(store);
    if (status == SECURITY_STORE_OK) {
        SecurityGrantSnapshot snapshot = {}; FileRevision revision;
        status = grant_store_read(store, binding, &snapshot, &revision);
        if (status == SECURITY_STORE_OK && snapshot.generation != generation) status = SECURITY_STORE_CONFLICT;
        security_grant_snapshot_destroy(&snapshot);
        if (status != SECURITY_STORE_OK) grant_store_end(store);
    }
    if (result) *result = status;
    if (status != SECURITY_STORE_OK) { mem_free(lease); return nullptr; }
    lease->store = store; return lease;
}

void security_grant_store_release(SecurityGrantLease* lease) {
    if (!lease) return;
    grant_store_end(lease->store); mem_free(lease);
}

static SecurityStoreResult grant_store_publish(SecurityGrantStore* store, const char* name,
        const void* bytes, size_t size, const FileRevision* revision) {
    int status = file_anchor_write_atomic(store->directory, name, bytes, size, revision);
    if (status == 0) return SECURITY_STORE_OK;
    if (status == 1) { store->unavailable = true; return SECURITY_STORE_UNAVAILABLE; }
    return errno == ESTALE || errno == EEXIST ? SECURITY_STORE_CONFLICT : grant_store_path_error();
}

static SecurityStateTransaction* security_state_begin_named(SecurityGrantStore* store,
        const char* name, SecurityStoreResult* result) {
    if (result) *result = SECURITY_STORE_INVALID;
    if (strnlen(name, sizeof(SecurityStateTransaction::name)) >= sizeof(SecurityStateTransaction::name)) return nullptr;
    SecurityStateTransaction* transaction = (SecurityStateTransaction*)mem_calloc(1, sizeof(*transaction), MEM_CAT_SYSTEM);
    if (!transaction) { if (result) *result = SECURITY_STORE_IO_ERROR; return nullptr; }
    SecurityStoreResult status = grant_store_begin(store);
    if (result) *result = status;
    if (status != SECURITY_STORE_OK) { mem_free(transaction); return nullptr; }
    transaction->store = store; memcpy(transaction->name, name, strlen(name) + 1);
    return transaction;
}
SecurityStateTransaction* security_state_begin(SecurityGrantStore* store,
        SecurityStateSlot slot, SecurityStoreResult* result) {
    if (result) *result = SECURITY_STORE_INVALID;
    return slot == SECURITY_STATE_PACKAGE_UPDATES ? security_state_begin_named(store, "package-updates.state", result) : nullptr;
}
SecurityStateTransaction* security_state_begin_principal(SecurityGrantStore* store,
        SecurityStateSlot slot, uint32_t kind, const uint8_t principal[32], SecurityStoreResult* result) {
    if (result) *result = SECURITY_STORE_INVALID;
    if (slot != SECURITY_STATE_PRIVATE_QUOTA || !security_state_fingerprint_present(principal) ||
            (kind != SECURITY_PRINCIPAL_APP && kind != SECURITY_PRINCIPAL_SCRIPT)) return nullptr;
    char name[81] = "storage-"; name[8] = kind == SECURITY_PRINCIPAL_APP ? 'a' : 's'; name[9] = '-';
    hex_encode(principal, 32, name + 10); memcpy(name + 74, ".quota", 7);
    return security_state_begin_named(store, name, result);
}
bool security_state_select_update_metadata(SecurityStateTransaction* transaction, const uint8_t package[32]) {
    if (!transaction || !transaction->ready || strcmp(transaction->name, "package-updates.state") ||
            !security_state_fingerprint_present(package) || transaction->store->unavailable) return false;
    char name[81] = "update-"; hex_encode(package, 32, name + 7); memcpy(name + 71, ".metadata", 10);
    memcpy(transaction->name, name, sizeof(name));
    transaction->ready = transaction->missing = false; memset(&transaction->revision, 0, sizeof(transaction->revision));
    return true;
}
SecurityStoreResult security_state_read(SecurityStateTransaction* transaction, char** bytes, size_t* size) {
    if (bytes) *bytes = nullptr;
    if (size) *size = 0;
    if (!transaction || !bytes || !size) return SECURITY_STORE_INVALID;
    if (transaction->store->unavailable) return SECURITY_STORE_UNAVAILABLE;
    transaction->ready = false; transaction->missing = false;
    if (file_anchor_read_all(transaction->store->directory, transaction->name,
            SECURITY_STORE_MAX_BYTES, MEM_CAT_SYSTEM, bytes, size, &transaction->revision)) {
        transaction->ready = true; return SECURITY_STORE_OK;
    }
    if (errno == ENOENT) { transaction->ready = transaction->missing = true; return SECURITY_STORE_MISSING; }
    return errno == EFBIG ? SECURITY_STORE_CORRUPT : grant_store_path_error();
}
SecurityStoreResult security_state_publish(SecurityStateTransaction* transaction, const void* bytes, size_t size) {
    if (!transaction || !transaction->ready || !bytes || !size || size > SECURITY_STORE_MAX_BYTES)
        return SECURITY_STORE_INVALID;
    if (transaction->store->unavailable) return SECURITY_STORE_UNAVAILABLE;
    transaction->ready = false;
    return grant_store_publish(transaction->store, transaction->name, bytes, size,
        transaction->missing ? nullptr : &transaction->revision);
}
void security_state_end(SecurityStateTransaction* transaction) {
    if (!transaction) return;
    grant_store_end(transaction->store); mem_free(transaction);
}
bool security_state_profile(SecurityStateTransaction* transaction, uint8_t digest[32]) {
    if (!transaction || !digest) return false;
    char* path = file_anchor_path(transaction->store->directory);
    FileRevision identity = file_anchor_identity(transaction->store->directory);
    DigestCtx* hash = path ? digest_ctx_new(DIGEST_SHA256) : nullptr;
    static const char domain[] = "Lambda protected state profile v1";
    bool valid = hash && digest_update(hash, domain, sizeof(domain) - 1) &&
        digest_update_u64_le(hash, strlen(path)) && digest_update(hash, path, strlen(path)) &&
        digest_update_u64_le(hash, identity.device) && digest_update_u64_le(hash, identity.inode) &&
        digest_finalize(hash, digest, 32);
    digest_ctx_free(hash); mem_free(path); return valid;
}

static SecurityStoreResult grant_store_commit(SecurityGrantStore* store,
        const SecurityGrantBinding* previous, const SecurityGrantBinding* replacement,
        uint64_t expected_generation, const SecurityPersistentGrant* grants, uint32_t count,
        uint64_t* out_generation) {
    if (out_generation) *out_generation = 0;
    if (!out_generation || !grant_binding_valid(previous) || !grant_binding_valid(replacement) ||
        expected_generation == UINT64_MAX || memcmp(previous->principal, replacement->principal, 32) ||
        previous->principal_kind != replacement->principal_kind) return SECURITY_STORE_INVALID;
    size_t size = 0;
    uint8_t* encoded = grant_store_encode(replacement, expected_generation + 1, grants, count, &size);
    if (!encoded) return SECURITY_STORE_INVALID;
    SecurityStoreResult result = grant_store_begin(store);
    if (result != SECURITY_STORE_OK) { mem_free(encoded); return result; }
    SecurityGrantSnapshot current = {};
    FileRevision revision;
    result = grant_store_read(store, previous, &current, &revision);
    bool missing = result == SECURITY_STORE_MISSING;
    if ((missing && expected_generation != 0) ||
        (result == SECURITY_STORE_OK && current.generation != expected_generation)) result = SECURITY_STORE_CONFLICT;
    if (result == SECURITY_STORE_OK || (missing && expected_generation == 0)) {
        char name[71]; grant_store_filename(previous, name);
        result = grant_store_publish(store, name, encoded, size, missing ? nullptr : &revision);
        if (result == SECURITY_STORE_OK) *out_generation = expected_generation + 1;
    }
    security_grant_snapshot_destroy(&current);
    mem_free(encoded);
    grant_store_end(store);
    return result;
}

SecurityStoreResult security_grant_store_save(SecurityGrantStore* store,
        const SecurityGrantBinding* binding, uint64_t expected_generation,
        const SecurityPersistentGrant* grants, uint32_t count, uint64_t* out_generation) {
    return grant_store_commit(store, binding, binding, expected_generation, grants, count, out_generation);
}

SecurityStoreResult security_grant_store_rebind(SecurityGrantStore* store,
        const SecurityGrantBinding* previous, const SecurityGrantBinding* replacement,
        uint64_t expected_generation, uint64_t* out_generation) {
    return grant_store_commit(store, previous, replacement, expected_generation, nullptr, 0, out_generation);
}
