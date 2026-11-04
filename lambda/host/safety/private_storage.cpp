#include "private_storage.h"
#include "state_store.h"
#include "storage_tree.h"
#include "../../../lib/arraylist.h"
#include "../../../lib/digest.h"
#include "../../../lib/endian.h"
#include "../../../lib/hex.h"
#include "../../../lib/log.h"
#include "../../../lib/mem.h"
#include <errno.h>
#include <string.h>
#ifndef _WIN32
#include <sys/file.h>
#include <unistd.h>
#endif

struct PrivateMount {
    FileAnchor* directory;
    FileAnchor* usage;
    SecurityFileBroker* broker;
    SecurityIOClient raw;
    SecurityPrivateStorageLimit limit;
};
struct SecurityPrivateStorage {
    FileAnchor* owner;
    FILE* lock;
    PrivateMount mounts[SECURITY_PRIVATE_COUNT];
    FileAnchor* temporary;
    FILE* temporary_lease;
    char temporary_name[35];
    bool ready;
    bool finished;
    bool cleaned;
    uint64_t quota_generation;
    bool custom_quota;
};
static const uint32_t private_flags = FILE_ANCHOR_STRICT_STORAGE;
// one owner-locked atomic write can leave an additional staging entry after
// interruption; count it against quota but keep inspection/reset recoverable
static const uint32_t private_cleanup_limit = SECURITY_PRIVATE_SCAN_ENTRIES;
static const uint32_t private_session_limit = 16;
static const uint32_t private_tree_depth_limit = SECURITY_IO_PATH_DEPTH_LIMIT + 2;
static const char* private_paths[] = {SECURITY_APP_DATA_PATH, SECURITY_APP_CACHE_PATH, SECURITY_APP_TEMP_PATH};
static_assert(sizeof(private_paths) / sizeof(private_paths[0]) == SECURITY_PRIVATE_COUNT, "private mount paths");
const char* security_private_storage_kind_name(SecurityPrivateStorageKind kind) {
    static const char* names[] = {"data", "cache", "temp"};
    static_assert(sizeof(names) / sizeof(names[0]) == SECURITY_PRIVATE_COUNT, "private kind names");
    return (unsigned)kind < SECURITY_PRIVATE_COUNT ? names[kind] : nullptr;
}
static bool private_limits_valid(const SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT]) {
    for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++)
        if (!limits[i].max_bytes || !limits[i].max_entries || limits[i].max_entries > SECURITY_PRIVATE_MAX_ENTRIES ||
                (i == SECURITY_PRIVATE_TEMP && limits[i].max_entries < 3)) { errno = EINVAL; return false; }
    return true;
}
void security_private_storage_default_limits(SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT]) {
    static const SecurityPrivateStorageLimit defaults[] = {
        {64 * 1024 * 1024, 1024}, {32 * 1024 * 1024, 1024}, {16 * 1024 * 1024, 512}};
    static_assert(sizeof(defaults) / sizeof(defaults[0]) == SECURITY_PRIVATE_COUNT, "private quota classes");
    memcpy(limits, defaults, sizeof(defaults));
}

static int private_path_kind(const char* path) {
    if (path) for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++) {
        size_t length = strlen(private_paths[i]);
        if (!strncmp(path, private_paths[i], length) && (!path[length] || path[length] == '/')) return (int)i;
    }
    return -1;
}
bool security_private_storage_path(const char* path) { return private_path_kind(path) >= 0; }
static PrivateMount* private_mount(SecurityPrivateStorage* storage, const char* path, const char** relative) {
    int kind = private_path_kind(path);
    if (!storage || !storage->ready || storage->finished || kind < 0) return nullptr;
    const char* suffix = path + strlen(private_paths[kind]);
    if (*suffix) suffix++;
    if (*suffix == '/') return nullptr;
    *relative = *suffix ? suffix : ".";
    return &storage->mounts[kind];
}
static bool private_lock_file(FileAnchor* directory, const char* name, FILE* file) {
#ifndef _WIN32
    if (!file || flock(fileno(file), LOCK_EX | LOCK_NB)) return false;
    FileRevision held = file_revision_stream(file), named;
    if (file_anchor_stat(directory, name, &named) && held.device == named.device && held.inode == named.inode) return true;
    flock(fileno(file), LOCK_UN); errno = ESTALE;
#else
    (void)directory; (void)name; (void)file; errno = ENOTSUP;
#endif
    return false;
}
static bool private_lock(SecurityPrivateStorage* storage) {
    return storage && private_lock_file(storage->owner, ".storage.lock", storage->lock);
}
static void private_unlock(SecurityPrivateStorage* storage) {
#ifndef _WIN32
    flock(fileno(storage->lock), LOCK_UN);
#else
    (void)storage;
#endif
}

static bool private_clear(FileAnchor* directory, uint32_t* remaining, uint32_t depth = 0,
        uint32_t* budget = nullptr, bool* more = nullptr) {
    return security_storage_clear(directory, remaining, depth, private_tree_depth_limit, budget, more);
}
static bool private_remove_directory(FileAnchor* parent, const char* name, FileAnchor* directory) {
    uint32_t remaining = private_cleanup_limit;
    FileRevision revision = file_anchor_identity(directory);
    return private_clear(directory, &remaining) && file_anchor_remove(parent, name, &revision);
}

struct PrivateUsage {
    FileAnchor* directory;
    uint64_t bytes;
    uint64_t max_bytes;
    uint32_t entries;
    uint32_t max_entries;
    uint32_t depth;
};
static bool private_usage_visit(void* context, const char* name, const FileRevision* revision) {
    PrivateUsage* usage = (PrivateUsage*)context;
    if (usage->entries == usage->max_entries) { errno = EFBIG; return false; }
    usage->entries++;
    if (revision->stat.is_file) {
        if (revision->stat.size < 0 || (uint64_t)revision->stat.size > usage->max_bytes - usage->bytes) {
            errno = EFBIG; return false;
        }
        usage->bytes += (uint64_t)revision->stat.size; return true;
    }
    if (!revision->stat.is_dir || usage->depth == private_tree_depth_limit) { errno = EFBIG; return false; }
    FileAnchor* child = file_anchor_directory(usage->directory, name, false, private_flags);
    PrivateUsage nested = *usage; nested.directory = child; nested.depth++;
    bool valid = child && file_anchor_visit_strict(child, usage->max_entries, private_usage_visit, &nested);
    file_anchor_close(child); usage->bytes = nested.bytes; usage->entries = nested.entries; return valid;
}
static bool private_usage(PrivateMount* mount, PrivateUsage* usage) {
    *usage = {mount->usage, 0, mount->limit.max_bytes, 0, mount->limit.max_entries, 0};
    return file_anchor_visit_strict(mount->usage, mount->limit.max_entries, private_usage_visit, usage);
}
static bool private_measure(PrivateMount* mount, PrivateUsage* usage) {
    // Saved quota reductions must remain inspectable and permit replacements
    // that bring total bytes back within quota, without unbounded traversal.
    PrivateMount measured = *mount; measured.limit = {UINT64_MAX, private_cleanup_limit};
    return private_usage(&measured, usage);
}
static SecurityIOResult private_write(void* context, const char* path, const void* data,
        size_t size, bool append, bool atomic) {
    SecurityPrivateStorage* storage = (SecurityPrivateStorage*)context; const char* relative = nullptr;
    PrivateMount* mount = private_mount(storage, path, &relative);
    if (!mount) return {SECURITY_DENIED, EACCES, false, {}};
    if (!private_lock(storage)) return {SECURITY_DENIED, errno ? errno : EACCES, false, {}};
    PrivateUsage usage; bool valid = private_measure(mount, &usage);
    SecurityIOResult result = {valid || errno == EFBIG ? SECURITY_LIMIT_EXCEEDED : SECURITY_DENIED,
        valid ? EFBIG : errno, false, {}};
    FileStat target = {};
    if (valid) {
        result = mount->raw.stat(mount->raw.context, relative, &target);
        valid = result.decision == SECURITY_ALLOWED && !result.system_error && (!target.exists || target.is_file);
        if (result.decision == SECURITY_ALLOWED && !result.system_error && target.exists && !target.is_file)
            result = {SECURITY_INVALID_REQUEST, EISDIR, false, {}};
    }
    if (valid) {
        uint64_t previous = target.exists ? (uint64_t)target.size : 0;
        valid = previous <= usage.bytes;
        uint64_t retained = append ? usage.bytes : usage.bytes - previous;
        // Missing parent directories and all live temp sessions consume quota.
        uint32_t missing = target.exists ? 0 : 1;
        char prefix[4096]; size_t length = strlen(relative);
        if (length >= sizeof(prefix)) valid = false;
        else {
            memcpy(prefix, relative, length + 1);
            for (char* p = prefix; valid && *p; p++) if (*p == '/') {
                *p = 0; FileStat parent = {};
                SecurityIOResult status = mount->raw.stat(mount->raw.context, prefix, &parent);
                valid = status.decision == SECURITY_ALLOWED && (!status.system_error || status.system_error == ENOENT);
                if (valid && !parent.exists) missing++;
                *p = '/';
            }
        }
        if (valid && retained <= mount->limit.max_bytes && size <= mount->limit.max_bytes - retained &&
                usage.entries <= mount->limit.max_entries && missing <= mount->limit.max_entries - usage.entries)
            result = mount->raw.write(mount->raw.context, relative, data, size, append, atomic);
        else result = {SECURITY_LIMIT_EXCEEDED, EFBIG, false, {}};
    }
    private_unlock(storage); return result;
}

SecurityIOClient security_private_storage_client(SecurityPrivateStorage* storage) {
    if (!storage || !storage->ready || storage->finished) return {};
    SecurityIOClient client = {1, storage};
    client.read = [](void* context, const char* path, bool source, uint64_t limit, MemCategory category,
            char** bytes, size_t* size) {
        const char* relative = nullptr; PrivateMount* mount = private_mount((SecurityPrivateStorage*)context, path, &relative);
        if (bytes) *bytes = nullptr;
        if (size) *size = 0;
        if (!mount || source) return SecurityIOResult{SECURITY_DENIED, EACCES, false, {}};
        return mount->raw.read(mount->raw.context, relative, false, limit, category, bytes, size); };
    client.write = private_write;
    client.canonical_source = [](void*, const char*, char** canonical) {
        if (canonical) *canonical = nullptr;
        return SecurityIOResult{SECURITY_DENIED, EACCES, false, {}}; };
    client.input_kind = [](void* context, const char* path, int32_t* kind) {
        const char* relative = nullptr; PrivateMount* mount = private_mount((SecurityPrivateStorage*)context, path, &relative);
        return mount ? mount->raw.input_kind(mount->raw.context, relative, kind) : SecurityIOResult{SECURITY_DENIED, EACCES, false, {}}; };
    client.enumerate = [](void* context, const char* path, uint32_t limit, SecurityDirectory* directory) {
        const char* relative = nullptr; PrivateMount* mount = private_mount((SecurityPrivateStorage*)context, path, &relative);
        return mount ? mount->raw.enumerate(mount->raw.context, relative, limit, directory) : SecurityIOResult{SECURITY_DENIED, EACCES, false, {}}; };
    client.stat = [](void* context, const char* path, FileStat* metadata) {
        const char* relative = nullptr; PrivateMount* mount = private_mount((SecurityPrivateStorage*)context, path, &relative);
        return mount ? mount->raw.stat(mount->raw.context, relative, metadata) : SecurityIOResult{SECURITY_DENIED, EACCES, false, {}}; };
    client.authority_stamp = [](void* context, SecurityAuthorityStamp* stamp) {
        SecurityPrivateStorage* storage = (SecurityPrivateStorage*)context;
        if (storage->finished) { if (stamp) *stamp = {}; return SecurityIOResult{SECURITY_REVOKED, EACCES, false, {}}; }
        PrivateMount* mount = &storage->mounts[SECURITY_PRIVATE_DATA];
        return mount->raw.authority_stamp(mount->raw.context, stamp); };
    return client;
}

static bool private_record(FileAnchor* root, FileAnchor* owner, FileAnchor* directory,
        const SecurityGrantBinding* binding, unsigned kind, uint8_t marker[128]) {
    memset(marker, 0, 128); memcpy(marker, "LSPRIV1", 7);
    write_le32(marker + 8, binding->principal_kind); write_le32(marker + 12, kind);
    memcpy(marker + 16, binding->principal, 32);
    FileRevision identities[] = {file_anchor_identity(root), file_anchor_identity(owner), file_anchor_identity(directory)};
    for (unsigned i = 0; i < 3; i++) {
        write_le64(marker + 48 + i * 16, identities[i].device);
        write_le64(marker + 56 + i * 16, identities[i].inode);
    }
    return digest_sha256(marker, 96, marker + 96);
}
static bool private_marker(FileAnchor* root, FileAnchor* owner, FileAnchor* directory,
        const SecurityGrantBinding* binding, unsigned kind, const char* name, bool create) {
    uint8_t marker[128];
    if (!private_record(root, owner, directory, binding, kind, marker)) return false;
    if (create) return file_anchor_write_atomic(owner, name, marker, sizeof(marker), nullptr) == 0;
    char* bytes = nullptr; size_t size = 0;
    bool valid = file_anchor_read_all(owner, name, sizeof(marker), MEM_CAT_SYSTEM, &bytes, &size, nullptr);
    if (valid && (size != sizeof(marker) || memcmp(marker, bytes, sizeof(marker)))) { errno = EBADMSG; valid = false; }
    int error = errno; mem_free(bytes); errno = error; return valid;
}
static FileAnchor* private_optional_directory(FileAnchor* root, FileAnchor* owner,
        const SecurityGrantBinding* binding, unsigned kind, const char* name, const char* marker) {
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        FileAnchor* directory = file_anchor_create_directory(owner, name, private_flags);
        bool created = directory;
        if (!directory && errno == EEXIST) directory = file_anchor_directory(owner, name, false, private_flags);
        if (!directory) return nullptr;
        if (private_marker(root, owner, directory, binding, kind, marker, created)) return directory;
        // A crash before the marker was published cannot have exposed this
        // discardable mount. Remove it; never adopt its unverified contents.
        int error = errno;
        bool retry = false;
        if (!created && !attempt && error == ENOENT) {
            retry = private_remove_directory(owner, name, directory); error = errno;
        }
        file_anchor_close(directory);
        if (!retry) { errno = error; return nullptr; }
    }
    errno = EIO; return nullptr;
}
static FILE* private_session_lease(FileAnchor* session, bool create = true) {
    FILE* lease = file_anchor_open_read(session, ".lease");
    if (!lease && errno == ENOENT && create) lease = file_anchor_create_regular(session, ".lease");
    if (lease && private_lock_file(session, ".lease", lease)) return lease;
    int error = errno; if (lease) fclose(lease); errno = error; return nullptr;
}
static bool private_session_name(const char* name) {
    if (strlen(name) != 34 || strncmp(name, "r-", 2)) return false;
    for (const char* p = name + 2; *p; p++) if (!(*p >= '0' && *p <= '9') && !(*p >= 'a' && *p <= 'f')) return false;
    return true;
}
static bool private_sessions(SecurityPrivateStorage* storage, bool recover, uint32_t* live, uint32_t* abandoned) {
    FileAnchor* root = storage->mounts[SECURITY_PRIVATE_TEMP].usage;
    ArrayList* entries = security_storage_entries(root, private_session_limit);
    if (!entries) return false;
    bool valid = true; *live = 0; *abandoned = 0;
    for (int i = 0; valid && i < entries->length; i++) {
        SecurityStorageEntry* entry = (SecurityStorageEntry*)entries->data[i];
        if (!entry->revision.stat.is_dir || !private_session_name(entry->name)) { errno = EBADMSG; valid = false; break; }
        FileAnchor* session = file_anchor_directory(root, entry->name, false, private_flags);
        valid = security_storage_same_object(session, &entry->revision);
        FILE* lease = valid ? private_session_lease(session, recover) : nullptr;
        if (valid && !lease && (errno == EWOULDBLOCK || errno == EAGAIN)) (*live)++;
        else if (valid) {
            valid = lease || (!recover && errno == ENOENT);
            if (valid) (*abandoned)++;
            if (valid && recover) valid = private_remove_directory(root, entry->name, session);
        }
        int error = errno; if (lease) fclose(lease); file_anchor_close(session); errno = error;
    }
    int error = errno; security_storage_entries_free(entries); errno = error; return valid;
}
static bool private_temporary_open(SecurityPrivateStorage* storage) {
    PrivateMount* mount = &storage->mounts[SECURITY_PRIVATE_TEMP]; uint32_t live = 0, abandoned = 0;
    if (!private_sessions(storage, true, &live, &abandoned)) return false;
    PrivateUsage usage;
    if (!private_usage(mount, &usage)) return false;
    if (live == private_session_limit || usage.max_entries - usage.entries < 3) { errno = EFBIG; return false; }
    uint8_t random[16];
    if (!crypto_random_bytes(random, sizeof(random))) { errno = EIO; return false; }
    char name[35] = "r-"; hex_encode(random, sizeof(random), name + 2);
    storage->temporary = file_anchor_create_directory(mount->usage, name, private_flags);
    if (!storage->temporary) return false;
    memcpy(storage->temporary_name, name, sizeof(name));
    storage->temporary_lease = private_session_lease(storage->temporary);
    mount->directory = storage->temporary_lease ? file_anchor_create_directory(storage->temporary, "files", private_flags) : nullptr;
    return mount->directory;
}

struct PrivateControl {
    FileAnchor* root;
    FILE* initializer;
    SecurityPrivateStorage* storage;
    bool owner_locked;
};
struct PrivateQuota {
    uint64_t generation;
    bool custom;
    SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT];
};
static int private_state_error(SecurityStoreResult result) {
    switch (result) {
        case SECURITY_STORE_CORRUPT: return EBADMSG;
        case SECURITY_STORE_CONFLICT: return ESTALE;
        case SECURITY_STORE_INVALID: return EINVAL;
        case SECURITY_STORE_UNSUPPORTED: return ENOTSUP;
        case SECURITY_STORE_UNSAFE_PATH: case SECURITY_STORE_IDENTITY_CHANGED: return EACCES;
        default: return EIO;
    }
}
static bool private_quota_record(SecurityStateTransaction* transaction, const SecurityGrantBinding* binding,
        const PrivateQuota* quota, uint8_t record[160]) {
    static_assert(88 + SECURITY_PRIVATE_COUNT * 12 <= 128, "quota fields precede checksum");
    memset(record, 0, 160); memcpy(record, "LMSQUO1", 7);
    write_le32(record + 8, binding->principal_kind); write_le32(record + 12, quota->custom ? 1 : 0);
    memcpy(record + 16, binding->principal, 32);
    errno = 0;
    if (!security_state_profile(transaction, record + 48)) { if (!errno) errno = EIO; return false; }
    write_le64(record + 80, quota->generation);
    if (quota->custom) for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++) {
        write_le64(record + 88 + i * 12, quota->limits[i].max_bytes);
        write_le32(record + 96 + i * 12, quota->limits[i].max_entries);
    }
    if (!digest_sha256(record, 128, record + 128)) { errno = EIO; return false; }
    return true;
}
static bool private_quota_read(const SecurityPrivateStorageConfig* config, PrivateQuota* quota,
        SecurityStateTransaction** held = nullptr) {
    if (held) *held = nullptr;
    *quota = {}; memcpy(quota->limits, config->limits, sizeof(quota->limits));
    SecurityStoreResult result;
    SecurityStateTransaction* transaction = security_state_begin_principal(config->store, SECURITY_STATE_PRIVATE_QUOTA,
        config->binding.principal_kind, config->binding.principal, &result);
    if (!transaction) { errno = private_state_error(result); return false; }
    char* bytes = nullptr; size_t size = 0;
    result = security_state_read(transaction, &bytes, &size);
    bool valid = result == SECURITY_STORE_MISSING;
    int error = valid ? 0 : private_state_error(result);
    if (result == SECURITY_STORE_OK) {
        uint8_t expected[160]; error = EBADMSG;
        if (size == sizeof(expected)) {
            const uint8_t* wire = (const uint8_t*)bytes;
            quota->generation = read_le64(wire + 80); quota->custom = read_le32(wire + 12) == 1;
            if (quota->custom) for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++) {
                quota->limits[i] = {read_le64(wire + 88 + i * 12), read_le32(wire + 96 + i * 12)};
            }
            valid = quota->generation && private_limits_valid(quota->limits) &&
                private_quota_record(transaction, &config->binding, quota, expected) && !memcmp(bytes, expected, sizeof(expected));
        }
    }
    mem_free(bytes);
    if (valid && held) *held = transaction; else security_state_end(transaction);
    errno = valid ? 0 : error; return valid;
}
static void private_control_close(PrivateControl* control) {
    if (control->owner_locked) private_unlock(control->storage);
    if (control->initializer) fclose(control->initializer);
    file_anchor_close(control->root);
    security_private_storage_close(control->storage);
    *control = {};
}
static bool private_control_open(const SecurityPrivateStorageConfig* config, bool create, PrivateControl* control) {
    if (!config || !config->store || (config->binding.principal_kind != SECURITY_PRINCIPAL_APP &&
            config->binding.principal_kind != SECURITY_PRINCIPAL_SCRIPT)) { errno = EINVAL; return false; }
    if (!private_limits_valid(config->limits) || !security_state_fingerprint_present(config->binding.principal)) {
        errno = EINVAL; return false;
    }
    FileAnchor* root = control->root = security_grant_store_private_root(config->store);
    if (!root) return false;
    // Namespace initialization and principal IO have separate, nonblocking locks.
    FILE* initializer = file_anchor_create_regular(root, ".storage.lock");
    if (!initializer && errno == EEXIST) initializer = file_anchor_open_read(root, ".storage.lock");
    control->initializer = initializer;
    bool locked = private_lock_file(root, ".storage.lock", initializer);
    SecurityPrivateStorage* storage = locked ? (SecurityPrivateStorage*)mem_calloc(1, sizeof(*storage), MEM_CAT_SYSTEM) : nullptr;
    control->storage = storage;
    if (locked && !storage) errno = ENOMEM;
    bool valid = storage;
    char name[80]; name[0] = config->binding.principal_kind == SECURITY_PRINCIPAL_APP ? 'a' : 's'; name[1] = '-';
    hex_encode(config->binding.principal, 32, name + 2);
    bool created = false;
    if (valid) {
        for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++) storage->mounts[i].limit = config->limits[i];
        if (create) { storage->owner = file_anchor_create_directory(root, name, private_flags); created = storage->owner; }
        if (!create || (!storage->owner && errno == EEXIST))
            storage->owner = file_anchor_directory(root, name, false, private_flags);
        PrivateMount* data = &storage->mounts[SECURITY_PRIVATE_DATA];
        data->directory = storage->owner ? (created ? file_anchor_create_directory(storage->owner, "data", private_flags)
            : file_anchor_directory(storage->owner, "data", false, private_flags)) : nullptr;
        data->usage = data->directory;
        valid = data->directory && private_marker(root, storage->owner, data->directory, &config->binding,
            SECURITY_PRIVATE_DATA, "owner", created);
        storage->lock = valid ? (created ? file_anchor_create_regular(storage->owner, ".storage.lock")
            : file_anchor_open_read(storage->owner, ".storage.lock")) : nullptr;
        valid = valid && storage->lock;
    }
    control->owner_locked = valid && private_lock(storage); valid = valid && control->owner_locked;
    if (valid) {
        // keep the owner lock through quota acquisition and session creation;
        // the short protected-state transaction ends before filesystem cleanup.
        PrivateQuota quota;
        valid = private_quota_read(config, &quota);
        if (valid) {
            storage->quota_generation = quota.generation; storage->custom_quota = quota.custom;
            for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++) storage->mounts[i].limit = quota.limits[i];
        }
    }
    return valid;
}
static bool private_mounts_open(PrivateControl* control, const SecurityGrantBinding* binding, bool create) {
    SecurityPrivateStorage* storage = control->storage;
    static const char* markers[] = {"owner", "cache.owner", "temp.owner"};
    for (unsigned i = SECURITY_PRIVATE_CACHE; i < SECURITY_PRIVATE_COUNT; i++) {
        const char* name = security_private_storage_kind_name((SecurityPrivateStorageKind)i);
        FileAnchor* directory = create ? private_optional_directory(control->root, storage->owner, binding, i, name, markers[i])
            : file_anchor_directory(storage->owner, name, false, private_flags);
        storage->mounts[i].usage = directory;
        if (i == SECURITY_PRIVATE_CACHE) storage->mounts[i].directory = directory;
        if (!directory || (!create && !private_marker(control->root, storage->owner, directory, binding, i, markers[i], false))) return false;
    }
    return true;
}
static bool private_reset_record(PrivateControl* control, const SecurityGrantBinding* binding,
        const uint8_t id[16], uint8_t record[160]) {
    uint8_t identity[128];
    if (!private_record(control->root, control->storage->owner,
            control->storage->mounts[SECURITY_PRIVATE_DATA].directory, binding, SECURITY_PRIVATE_COUNT, identity)) return false;
    memset(record, 0, 160); memcpy(record, identity, 96); memcpy(record, "LSPRST1", 7);
    memcpy(record + 96, id, 16);
    return digest_sha256(record, 128, record + 128);
}
static bool private_reset_read(PrivateControl* control, const SecurityGrantBinding* binding,
        bool* pending, uint8_t id[16], FileRevision* revision = nullptr) {
    char* bytes = nullptr; size_t size = 0; *pending = false; memset(id, 0, 16);
    if (!file_anchor_read_all(control->storage->owner, "reset.pending", 160, MEM_CAT_SYSTEM, &bytes, &size, revision)) {
        if (errno == ENOENT) { errno = 0; return true; }
        return false;
    }
    uint8_t expected[160];
    bool valid = size == sizeof(expected) && private_reset_record(control, binding, (uint8_t*)bytes + 96, expected) &&
        !memcmp(bytes, expected, sizeof(expected));
    if (valid) { *pending = true; memcpy(id, bytes + 96, 16); }
    int error = valid ? 0 : EBADMSG; mem_free(bytes); errno = error; return valid;
}

SecurityPrivateStorage* security_private_storage_open(const SecurityPrivateStorageConfig* config) {
    PrivateControl control = {};
    bool valid = private_control_open(config, true, &control);
    SecurityPrivateStorage* storage = control.storage;
    if (valid) {
        bool pending = false; uint8_t id[16];
        valid = private_reset_read(&control, &config->binding, &pending, id);
        if (valid && pending) { errno = EBUSY; valid = false; }
        if (valid) valid = private_mounts_open(&control, &config->binding, true) && private_temporary_open(storage);
    }
    for (unsigned i = 0; valid && i < SECURITY_PRIVATE_COUNT; i++) {
        PrivateMount* mount = &storage->mounts[i]; mount->broker = security_file_broker_create(&config->broker);
        char* path = file_anchor_path(mount->directory);
        SecurityFileRootSpec spec = {path, SECURITY_SCOPE_PRIVATE,
            SECURITY_READ | SECURITY_METADATA | SECURITY_ENUMERATE | SECURITY_CREATE | SECURITY_REPLACE | SECURITY_APPEND,
            0, 0, false, true, nullptr};
        valid = mount->broker && path && security_file_broker_admit_root(mount->broker, &spec);
        mem_free(path); mount->raw = security_file_broker_client(mount->broker);
    }
    int error = errno;
    if (control.owner_locked) { private_unlock(storage); control.owner_locked = false; }
    if (valid) control.storage = nullptr;
    private_control_close(&control);
    if (!valid) { errno = error ? error : EACCES; return nullptr; }
    storage->ready = true; return storage;
}
static bool private_management_open(const SecurityPrivateStorageConfig* config, PrivateControl* control) {
    return private_control_open(config, false, control) && private_mounts_open(control, &config->binding, false);
}
static bool private_control_result(PrivateControl* control, bool valid) {
    int error = valid ? 0 : errno;
    private_control_close(control); errno = error; return valid;
}
static bool private_control_status(PrivateControl* control, const SecurityPrivateStorageConfig* config,
        SecurityPrivateStorageStatus* status, FileRevision* reset_revision = nullptr) {
    *status = {};
    for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++) {
        PrivateUsage usage;
        if (!private_measure(&control->storage->mounts[i], &usage)) return false;
        status->limits[i] = control->storage->mounts[i].limit;
        status->usage[i] = {usage.bytes, usage.entries,
            usage.bytes > status->limits[i].max_bytes || usage.entries > status->limits[i].max_entries};
    }
    status->quota_generation = control->storage->quota_generation; status->custom_quota = control->storage->custom_quota;
    return private_sessions(control->storage, false, &status->live_sessions, &status->abandoned_sessions) &&
        private_reset_read(control, &config->binding, &status->reset_pending, status->reset_id, reset_revision);
}
bool security_private_storage_inspect(const SecurityPrivateStorageConfig* config, SecurityPrivateStorageStatus* status) {
    if (!status) { errno = EINVAL; return false; }
    *status = {};
    PrivateControl control = {}; SecurityPrivateStorageStatus captured = {};
    bool valid = private_management_open(config, &control) && private_control_status(&control, config, &captured);
    if (valid) *status = captured;
    return private_control_result(&control, valid);
}
bool security_private_storage_set_quota(const SecurityPrivateStorageConfig* config,
        const SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT], uint64_t expected_generation, uint64_t* generation) {
    if (generation) *generation = 0;
    if (!generation || (limits && !private_limits_valid(limits))) { errno = EINVAL; return false; }
    PrivateControl control = {}; SecurityPrivateStorageStatus status = {};
    bool valid = private_management_open(config, &control) && private_control_status(&control, config, &status);
    if (valid && (status.live_sessions || status.reset_pending)) { errno = EBUSY; valid = false; }
    if (valid && status.quota_generation != expected_generation) { errno = ESTALE; valid = false; }
    PrivateQuota quota; SecurityStateTransaction* transaction = nullptr;
    if (valid) valid = private_quota_read(config, &quota, &transaction);
    if (valid && quota.generation != expected_generation) { errno = ESTALE; valid = false; }
    if (valid && quota.generation == UINT64_MAX) { errno = EOVERFLOW; valid = false; }
    if (valid) {
        quota.generation++; quota.custom = limits;
        if (limits) memcpy(quota.limits, limits, sizeof(quota.limits));
        uint8_t record[160];
        valid = private_quota_record(transaction, &config->binding, &quota, record);
        if (valid) {
            SecurityStoreResult result = security_state_publish(transaction, record, sizeof(record));
            valid = result == SECURITY_STORE_OK;
            if (!valid) errno = private_state_error(result);
        }
        if (valid) *generation = quota.generation;
    }
    int error = errno; security_state_end(transaction); errno = error;
    return private_control_result(&control, valid);
}
bool security_private_storage_reset_begin(const SecurityPrivateStorageConfig* config, uint8_t reset_id[16]) {
    if (!reset_id) { errno = EINVAL; return false; }
    memset(reset_id, 0, 16);
    PrivateControl control = {}; SecurityPrivateStorageStatus status = {};
    bool valid = private_management_open(config, &control) && private_control_status(&control, config, &status);
    if (valid && (status.live_sessions || status.reset_pending)) {
        errno = status.live_sessions ? EBUSY : EALREADY; valid = false;
    }
    uint8_t id[16], record[160];
    if (valid && !crypto_random_bytes(id, sizeof(id))) { errno = EIO; valid = false; }
    if (valid) valid = private_reset_record(&control, &config->binding, id, record) &&
        file_anchor_write_atomic(control.storage->owner, "reset.pending", record, sizeof(record), nullptr) == 0;
    if (valid) memcpy(reset_id, id, sizeof(id));
    return private_control_result(&control, valid);
}
bool security_private_storage_reset_step(const SecurityPrivateStorageConfig* config,
        const uint8_t reset_id[16], uint32_t max_removals, bool* complete) {
    if (complete) *complete = false;
    if (!complete || !reset_id || !max_removals || max_removals > SECURITY_PRIVATE_MAX_ENTRIES) { errno = EINVAL; return false; }
    PrivateControl control = {}; SecurityPrivateStorageStatus status = {}; FileRevision revision = {};
    bool valid = private_management_open(config, &control) && private_control_status(&control, config, &status, &revision);
    if (valid && (!status.reset_pending || memcmp(reset_id, status.reset_id, 16) || status.live_sessions)) {
        errno = !status.reset_pending ? ENOENT : status.live_sessions ? EBUSY : EACCES; valid = false;
    }
    bool more = false; uint32_t remaining = private_cleanup_limit;
    if (valid) valid = private_clear(control.storage->mounts[SECURITY_PRIVATE_DATA].directory,
        &remaining, 0, &max_removals, &more);
    if (valid && !more) {
        valid = file_anchor_remove(control.storage->owner, "reset.pending", &revision);
        if (valid) *complete = true;
    }
    return private_control_result(&control, valid);
}
bool security_private_storage_clear_cache(SecurityPrivateStorage* storage) {
    if (!storage || !storage->ready || storage->finished) { errno = EACCES; return false; }
    if (!private_lock(storage)) return false;
    uint32_t remaining = private_cleanup_limit;
    bool valid = private_clear(storage->mounts[SECURITY_PRIVATE_CACHE].directory, &remaining);
    int error = errno; private_unlock(storage); errno = error; return valid;
}
bool security_private_storage_finish(SecurityPrivateStorage* storage) {
    if (!storage) return true;
    storage->finished = true;
    if (storage->cleaned || !storage->temporary) return true;
    if (!private_lock(storage)) return false;
    if (!storage->temporary_lease) storage->temporary_lease = private_session_lease(storage->temporary);
    bool valid = storage->temporary_lease && private_remove_directory(storage->mounts[SECURITY_PRIVATE_TEMP].usage,
        storage->temporary_name, storage->temporary);
    int error = errno; private_unlock(storage); errno = error;
    if (valid) storage->cleaned = true;
    return valid;
}
void security_private_storage_close(SecurityPrivateStorage* storage) {
    if (!storage) return;
    if (!security_private_storage_finish(storage)) log_error("safety-private-cleanup: deferred temporary removal: %d", errno);
    for (PrivateMount& mount : storage->mounts) {
        security_file_broker_destroy(mount.broker);
        if (mount.usage != mount.directory) file_anchor_close(mount.usage);
        file_anchor_close(mount.directory);
    }
    if (storage->temporary_lease) fclose(storage->temporary_lease);
    file_anchor_close(storage->temporary);
    if (storage->lock) fclose(storage->lock);
    file_anchor_close(storage->owner); mem_free(storage);
}
