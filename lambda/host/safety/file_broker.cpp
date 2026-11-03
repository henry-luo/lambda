#include "file_broker.h"
#include "../../../lib/arraylist.h"
#include "../../../lib/digest.h"
#include "../../../lib/mem.h"
#include "../../../lib/path_str.h"
#include "../../../lib/utf.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>

struct SecurityFileRoot;
struct SecurityFileLease {
    SecurityFileRoot* root;
    SecurityRequest request;
    uint64_t epoch;
};

struct SecurityFileProtection {
    FileAnchor* anchor;
    FileRevision identity;
    SecurityOperations denied;
    char* name; // utf8_normalize storage; NULL protects the directory object
    char* leaf;
};

struct SecurityPendingWrite {
    SecurityFileLease lease;
    SecurityOverwriteChallenge description;
    char* relative;
    char* payload;
    bool append;
    bool atomic;
};

struct SecurityFileRoot {
    SecurityFileBroker* broker;
    FileAnchor* anchor;
    char* path;
    char* canonical_path;
    char* leaf;
    char* canonical_leaf;
    FileRevision entry;
    SecurityFileRootSpec spec;
    uint64_t id;
    uint32_t anchor_flags;
    bool active;
};

struct SecurityFileBroker {
    SecurityFileBrokerConfig config;
    SecurityPolicy* policy;
    ArrayList* roots;
    ArrayList* protections;
    ArrayList* pending;
    SecurityFileRoot* default_root;
    // Serialized acquisition uses this request only while a raw mechanism runs.
    SecurityFileLease* active;
    uint64_t epoch;
    uint64_t next_approval;
    uint64_t pending_bytes;
    uint64_t written_bytes;
    uint32_t write_operations;
    uint64_t last_now;
    char resolved_relative[4096]; // request-local storage under mutex
    pthread_mutex_t mutex;
};

static SecurityIOResult security_file_result(SecurityDecision decision, int error = 0) {
    return {decision, error, false, {}};
}

static uint64_t security_file_now(SecurityFileBroker* broker) {
    uint64_t now = broker->config.clock(broker->config.clock_context);
    if (now > broker->last_now) broker->last_now = now;
    return broker->last_now; // clock rollback never restores an expired grant
}

static bool security_file_expire_roots(SecurityFileBroker* broker) {
    uint64_t now = security_file_now(broker);
    bool changed = false;
    for (int i = 0; i < broker->roots->length; i++) {
        SecurityFileRoot* root = (SecurityFileRoot*)broker->roots->data[i];
        if (root->active && root->spec.expires_at && now >= root->spec.expires_at) {
            root->active = false; changed = true;
        }
    }
    if (changed && broker->epoch != UINT64_MAX) broker->epoch++;
    return broker->epoch != UINT64_MAX;
}

static bool security_file_same_object(const FileRevision* left, const FileRevision* right) {
    return left && right && left->stat.exists && right->stat.exists &&
        left->device == right->device && left->inode == right->inode;
}

static bool security_file_protections_valid(SecurityFileBroker* broker) {
    for (int i = 0; i < broker->protections->length; i++) {
        SecurityFileProtection* entry = (SecurityFileProtection*)broker->protections->data[i];
        if (!file_anchor_validate(entry->anchor)) return false;
    }
    return true;
}

static bool security_file_object_allowed(SecurityFileRoot* root, const FileRevision* object) {
    SecurityFileBroker* broker = root->broker;
    SecurityOperations operation = broker->active ? broker->active->request.operation : root->spec.ceiling;
    if (!root->active || !security_file_protections_valid(broker)) return false;
    for (int i = 0; i < broker->protections->length; i++) {
        SecurityFileProtection* entry = (SecurityFileProtection*)broker->protections->data[i];
        if (!(entry->denied & operation)) continue;
        if (!entry->name) {
            if (security_file_same_object(&entry->identity, object)) return false;
        } else {
            FileRevision child = {};
            if (file_anchor_stat(entry->anchor, entry->leaf, &child)) {
                if (security_file_same_object(&child, object)) return false;
            } else if (errno != ENOENT) return false;
        }
    }
    return true;
}

static bool security_file_ancestor(void* context, const FileRevision* object) {
    return security_file_object_allowed((SecurityFileRoot*)context, object);
}

static bool security_file_namespace(void* context, const FileRevision* parent, const char* name) {
    SecurityFileRoot* root = (SecurityFileRoot*)context;
    SecurityFileBroker* broker = root->broker;
    SecurityOperations operation = broker->active ? broker->active->request.operation : root->spec.ceiling;
    if (!root->active || !security_file_protections_valid(broker)) return false;
    char* folded = nullptr;
    bool valid = true;
    for (int i = 0; valid && i < broker->protections->length; i++) {
        SecurityFileProtection* entry = (SecurityFileProtection*)broker->protections->data[i];
        if (!entry->name || !(entry->denied & operation) || !security_file_same_object(&entry->identity, parent)) continue;
        if (!folded) folded = utf8_normalize(name, strlen(name), UTF_NORMALIZE_NFKC_CASEFOLD, nullptr);
        valid = folded && strcmp(entry->name, folded);
    }
    raw_free(folded); // RAWALLOC_OK: utf8_normalize returns utf8proc storage
    return valid;
}

static bool security_file_guard(void* context, const char* relative,
        FileAnchorAction action, const FileRevision* object) {
    SecurityFileRoot* root = (SecurityFileRoot*)context;
    SecurityFileBroker* broker = root->broker;
    if (file_path_has_atomic_stage(relative) || !security_file_object_allowed(root, object)) return false;
    if (root->leaf && object && (!strcmp(relative, root->leaf) ||
            (root->canonical_leaf && !strcmp(relative, root->canonical_leaf))) &&
        action != FILE_ANCHOR_TRAVERSE && !file_revision_equal(&root->entry, object)) return false;
    SecurityFileLease* lease = broker->active;
    if (!lease) return true; // host admission/preflight, after namespace authorization
    if (lease->root != root || lease->epoch != broker->epoch ||
        (root->spec.expires_at && security_file_now(broker) >= root->spec.expires_at)) return false;
    lease->request.now = security_file_now(broker);
    return security_policy_check(broker->policy, &lease->request) == SECURITY_ALLOWED;
}

static bool security_file_path_valid(const char* path) {
    if (!path || !*path || strnlen(path, 4096) >= 4096 || file_path_has_atomic_stage(path)) return false;
    unsigned depth = 0;
    for (const char* part = path; *part;) {
        const char* slash = strchr(part, '/');
        size_t length = slash ? (size_t)(slash - part) : strlen(part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.') || ++depth > SECURITY_IO_PATH_DEPTH_LIMIT) return false;
        for (size_t i = 0; i < length; i++) if (part[i] == '\\') return false;
        if (!slash) break;
        part = slash + 1;
        if (!*part) return false;
    }
    return true;
}

static bool security_file_scope_permits(SecurityFileRoot* root, SecurityOperations wanted) {
    const int32_t kinds[] = {SECURITY_OBJECT_FILE, SECURITY_OBJECT_DIRECTORY, SECURITY_OBJECT_NEW_FILE};
    for (int32_t kind : kinds) {
        SecurityResourceSpec spec = {root->spec.scope, kind, root->spec.ceiling,
            root->spec.admitted_source && kind == SECURITY_OBJECT_FILE};
        for (SecurityOperations operation = 1; operation <= SECURITY_INVOKE; operation <<= 1) {
            if (!(wanted & operation)) continue;
            SecurityDecision decision = security_policy_scope_check(root->broker->policy, &spec,
                root->spec.selected_operations, operation);
            if (decision == SECURITY_ALLOWED || decision == SECURITY_APPROVAL_REQUIRED) return true;
        }
    }
    return false;
}

static SecurityFileRoot* security_file_resolve(SecurityFileBroker* broker,
        const char* path, const char** relative, SecurityOperations wanted, bool directory = false) {
    *relative = nullptr;
    if (!path || strnlen(path, 4096) >= 4096 || !security_file_expire_roots(broker)) return nullptr;
    char absolute[4096];
    if (*path != '/') {
        if (!broker->default_root) return nullptr;
        const char* suffix = directory && (!strcmp(path, ".") || !strcmp(path, "./")) ? "" : path;
        if ((*suffix || !directory) && !security_file_path_valid(suffix)) return nullptr;
        if (path_str_join_posix_into(absolute, sizeof(absolute), broker->default_root->canonical_path, suffix) < 0)
            return nullptr;
        path = absolute;
    }
    SecurityFileRoot* root = nullptr;
    const char* chosen = nullptr;
    size_t specificity = 0;
    for (int i = 0; i < broker->roots->length; i++) {
        SecurityFileRoot* candidate = (SecurityFileRoot*)broker->roots->data[i];
        if (!candidate->active ||
                (candidate->spec.expires_at && security_file_now(broker) >= candidate->spec.expires_at)) continue;
        const char* prefixes[] = {candidate->path, candidate->canonical_path};
        for (const char* prefix : prefixes) {
            size_t length = strlen(prefix);
            if (strncmp(path, prefix, length) ||
                    (path[length] != '/' && (!directory || path[length]))) continue;
            const char* suffix = path + length + (path[length] == '/' ? 1 : 0);
            if (((*suffix || !directory) && !security_file_path_valid(suffix)) ||
                    (candidate->leaf && strcmp(candidate->leaf, suffix) &&
                        (!candidate->canonical_leaf || strcmp(candidate->canonical_leaf, suffix)))) continue;
            size_t score = length + (candidate->leaf ? strlen(suffix) + 1 : 0);
            // D7.5.6: a remembered overwrite/append selection at this same
            // root must be considered before the default project's challenge.
            SecurityOperations mutations = wanted & (SECURITY_REPLACE | SECURITY_APPEND);
            bool explicit_mutation = (candidate->spec.selected_operations & mutations) &&
                (!root || !(root->spec.selected_operations & mutations));
            if (score < specificity || (score == specificity && !explicit_mutation) ||
                    !security_file_scope_permits(candidate, wanted)) continue;
            root = candidate; chosen = suffix; specificity = score;
        }
    }
    if (!root) return nullptr;
    // The relative spelling can include a selected root's prefix not present
    // in the caller's relative request; retain it until serialized acquisition ends.
    if (path_str_copy(broker->resolved_relative, sizeof(broker->resolved_relative), chosen) < 0) return nullptr;
    *relative = broker->resolved_relative;
    return root;
}

static SecurityDecision security_file_lease(SecurityFileRoot* root, int32_t kind,
        SecurityOperations operation, SecurityFileLease* out) {
    SecurityFileBroker* broker = root->broker;
    *out = {};
    SecurityResourceSpec spec = {root->spec.scope, kind, root->spec.ceiling,
        root->spec.admitted_source && kind == SECURITY_OBJECT_FILE};
    SecurityResourceRef ref = {};
    SecurityDecision result = security_policy_admit_resource(broker->policy, &spec, &ref);
    if (result != SECURITY_ALLOWED) return result;
    if (root->spec.selected_operations & operation) {
        uint64_t grant;
        result = security_policy_grant(broker->policy, ref, operation, root->spec.expires_at, &grant);
        if (result != SECURITY_ALLOWED) { security_policy_release_resource(broker->policy, ref); return result; }
    }
    *out = {root, {ref, security_policy_generation(broker->policy), operation, security_file_now(broker)}, broker->epoch};
    return security_policy_check(broker->policy, &out->request);
}

static void security_file_release(SecurityFileBroker* broker, SecurityFileLease* lease) {
    if (lease->request.resource.serial) security_policy_release_resource(broker->policy, lease->request.resource);
    *lease = {};
}

static void security_file_pending_free(SecurityFileBroker* broker, SecurityPendingWrite* pending) {
    broker->pending_bytes -= pending->description.size;
    security_file_release(broker, &pending->lease);
    mem_free(pending->relative); mem_free(pending->payload); mem_free(pending);
}

static int security_file_pending_index(SecurityFileBroker* broker, SecurityApprovalRef ref) {
    if (ref.session != broker->config.policy.session || !ref.serial) return -1;
    for (int i = 0; i < broker->pending->length; i++) {
        SecurityPendingWrite* pending = (SecurityPendingWrite*)broker->pending->data[i];
        if (pending->description.approval.serial == ref.serial) return i;
    }
    return -1;
}

static bool security_file_pending_current(SecurityFileBroker* broker, SecurityPendingWrite* pending) {
    return pending->lease.epoch == broker->epoch && pending->lease.root->active &&
        (!pending->lease.root->spec.expires_at || security_file_now(broker) < pending->lease.root->spec.expires_at) &&
        pending->lease.request.generation == security_policy_generation(broker->policy) &&
        security_file_now(broker) < pending->description.expires_at;
}

static void security_file_sweep(SecurityFileBroker* broker) {
    for (int i = broker->pending->length - 1; i >= 0; i--) {
        SecurityPendingWrite* pending = (SecurityPendingWrite*)broker->pending->data[i];
        if (security_file_pending_current(broker, pending)) continue;
        arraylist_remove(broker->pending, (unsigned)i);
        security_file_pending_free(broker, pending);
    }
}

SecurityFileBroker* security_file_broker_create(const SecurityFileBrokerConfig* config) {
    if (!config || !config->clock || !config->max_io_bytes ||
        config->max_io_bytes >= SIZE_MAX || !config->max_total_write_bytes ||
        !config->max_pending_bytes || !config->max_pending || config->max_pending > 256 || !config->max_write_operations ||
        !config->approval_lifetime_seconds) return nullptr;
    SecurityFileBroker* broker = (SecurityFileBroker*)mem_calloc(1, sizeof(SecurityFileBroker), MEM_CAT_SYSTEM);
    if (!broker) return nullptr;
    broker->config = *config;
    broker->policy = security_policy_create(&config->policy);
    broker->roots = arraylist_new(0); broker->protections = arraylist_new(0); broker->pending = arraylist_new(0);
    if (!broker->policy || !broker->roots || !broker->protections || !broker->pending ||
        pthread_mutex_init(&broker->mutex, nullptr) != 0) {
        security_policy_destroy(broker->policy);
        if (broker->roots) arraylist_free(broker->roots);
        if (broker->protections) arraylist_free(broker->protections);
        if (broker->pending) arraylist_free(broker->pending);
        mem_free(broker); return nullptr;
    }
    broker->epoch = 1; broker->next_approval = 1;
    return broker;
}

void security_file_broker_destroy(SecurityFileBroker* broker) {
    if (!broker) return;
    for (int i = 0; i < broker->pending->length; i++)
        security_file_pending_free(broker, (SecurityPendingWrite*)broker->pending->data[i]);
    for (int i = 0; i < broker->roots->length; i++) {
        SecurityFileRoot* root = (SecurityFileRoot*)broker->roots->data[i];
        file_anchor_close(root->anchor); mem_free(root->path); mem_free(root->canonical_path);
        mem_free(root->leaf); mem_free(root->canonical_leaf); mem_free(root);
    }
    for (int i = 0; i < broker->protections->length; i++) {
        SecurityFileProtection* entry = (SecurityFileProtection*)broker->protections->data[i];
        file_anchor_close(entry->anchor);
        raw_free(entry->name); // RAWALLOC_OK: utf8_normalize storage
        mem_free(entry->leaf);
        mem_free(entry);
    }
    arraylist_free(broker->pending); arraylist_free(broker->roots); arraylist_free(broker->protections);
    security_policy_destroy(broker->policy); pthread_mutex_destroy(&broker->mutex); mem_free(broker);
}

bool security_file_broker_protect(SecurityFileBroker* broker, const char* directory, SecurityOperations denied) {
    if (!broker || !denied || (denied & ~((SecurityOperations)SECURITY_ALL_OPERATIONS))) return false;
    pthread_mutex_lock(&broker->mutex);
    bool valid = false;
    if (!broker->roots->length && broker->protections->length < 128) {
        FileAnchorOptions options = {FILE_ANCHOR_SAME_DEVICE, nullptr, nullptr, nullptr};
        FileAnchor* anchor = file_anchor_open(directory, &options);
        SecurityFileProtection* entry = anchor ? (SecurityFileProtection*)mem_alloc(sizeof(*entry), MEM_CAT_SYSTEM) : nullptr;
        if (entry) {
            *entry = {anchor, file_anchor_identity(anchor), denied, nullptr, nullptr};
            valid = arraylist_append(broker->protections, entry);
            if (!valid) mem_free(entry);
        }
        if (!valid) file_anchor_close(anchor);
    }
    pthread_mutex_unlock(&broker->mutex);
    return valid;
}

bool security_file_broker_protect_name(SecurityFileBroker* broker, const char* parent,
        const char* name, SecurityOperations denied) {
    if (!broker || !name || !*name || strchr(name, '/') || strchr(name, '\\') ||
            !strcmp(name, ".") || !strcmp(name, "..") || !denied ||
            (denied & ~((SecurityOperations)SECURITY_ALL_OPERATIONS))) return false;
    char* folded = utf8_normalize(name, strlen(name), UTF_NORMALIZE_NFKC_CASEFOLD, nullptr);
    if (!folded || !*folded) {
        raw_free(folded); // RAWALLOC_OK: utf8_normalize storage
        return false;
    }
    pthread_mutex_lock(&broker->mutex);
    // Parent/child identity probes may cross an OS mount (for example /dev).
    // Actual resource traversal retains its independent same-device ceiling.
    FileAnchorOptions options = {0, nullptr, nullptr, nullptr, nullptr};
    FileAnchor* anchor = !broker->roots->length && broker->protections->length < 128
        ? file_anchor_open(parent, &options) : nullptr;
    SecurityFileProtection* entry = anchor ? (SecurityFileProtection*)mem_alloc(sizeof(*entry), MEM_CAT_SYSTEM) : nullptr;
    char* leaf = entry ? mem_strdup(name, MEM_CAT_SYSTEM) : nullptr;
    bool valid = entry && leaf;
    if (entry) {
        *entry = {anchor, file_anchor_identity(anchor), denied, folded, leaf};
        valid = valid && arraylist_append(broker->protections, entry);
        if (!valid) mem_free(entry);
    }
    if (!valid) {
        file_anchor_close(anchor);
        mem_free(leaf);
        raw_free(folded); // RAWALLOC_OK: utf8_normalize storage
    }
    pthread_mutex_unlock(&broker->mutex); return valid;
}

uint64_t security_file_broker_admit_root(SecurityFileBroker* broker, const SecurityFileRootSpec* spec) {
    if (!broker || !spec || !spec->path || spec->path[0] != '/' ||
        !security_file_path_valid(spec->path + 1) || spec->scope < SECURITY_SCOPE_ENTRY ||
        spec->scope > SECURITY_SCOPE_SELECTED || (spec->scope == SECURITY_SCOPE_ENTRY && !spec->leaf) ||
        (spec->leaf && (!security_file_path_valid(spec->leaf) || strchr(spec->leaf, '/') ||
            (spec->scope != SECURITY_SCOPE_ENTRY && spec->scope != SECURITY_SCOPE_SELECTED))) ||
        !spec->ceiling || (spec->ceiling & ~((SecurityOperations)SECURITY_ALL_OPERATIONS)) ||
        (spec->selected_operations & ~spec->ceiling) ||
        (spec->selected_operations && spec->scope != SECURITY_SCOPE_SELECTED)) return 0;
    pthread_mutex_lock(&broker->mutex);
    uint64_t result = 0;
    if (broker->epoch != UINT64_MAX && broker->roots->length < 64 && (!spec->default_root || !broker->default_root)) {
        SecurityFileRoot* root = (SecurityFileRoot*)mem_calloc(1, sizeof(*root), MEM_CAT_SYSTEM);
        if (root) {
            root->broker = broker; root->spec = *spec; root->active = true;
            root->path = mem_strdup(spec->path, MEM_CAT_SYSTEM);
            root->leaf = spec->leaf ? mem_strdup(spec->leaf, MEM_CAT_SYSTEM) : nullptr;
            root->spec.path = root->path; root->spec.leaf = root->leaf;
            root->anchor_flags = spec->scope == SECURITY_SCOPE_PRIVATE ? FILE_ANCHOR_STRICT_STORAGE : FILE_ANCHOR_SAME_DEVICE;
            FileAnchorOptions options = {root->anchor_flags, security_file_guard, root,
                security_file_ancestor, security_file_namespace};
            root->anchor = root->path ? file_anchor_open(root->path, &options) : nullptr;
            root->canonical_path = root->anchor ? file_anchor_path(root->anchor) : nullptr;
            // Capture the exact admitted file before enabling its revision guard.
            bool valid = root->anchor && root->canonical_path && (!spec->leaf || (root->leaf &&
                file_anchor_stat(root->anchor, root->leaf, &root->entry) && root->entry.stat.is_file));
            if (valid && spec->leaf) {
                FILE* entry = file_anchor_open_read(root->anchor, root->leaf);
                char* canonical = entry ? file_stream_path(entry) : nullptr;
                if (entry) fclose(entry);
                size_t prefix = strlen(root->canonical_path);
                valid = canonical && !strncmp(canonical, root->canonical_path, prefix) &&
                    canonical[prefix] == '/' && !strchr(canonical + prefix + 1, '/');
                if (valid) root->canonical_leaf = mem_strdup(canonical + prefix + 1, MEM_CAT_SYSTEM);
                valid = valid && root->canonical_leaf;
                mem_free(canonical);
            }
            if (valid && arraylist_append(broker->roots, root)) {
                root->id = (uint64_t)broker->roots->length;
                if (spec->default_root) broker->default_root = root;
                broker->epoch++;
                result = root->id;
            } else {
                file_anchor_close(root->anchor); mem_free(root->leaf); mem_free(root->path);
                mem_free(root->canonical_path); mem_free(root->canonical_leaf); mem_free(root);
            }
        }
    }
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

bool security_file_broker_revoke_root(SecurityFileBroker* broker, uint64_t id) {
    if (!broker) return false;
    pthread_mutex_lock(&broker->mutex);
    bool valid = id && id <= (uint64_t)broker->roots->length && broker->epoch != UINT64_MAX;
    SecurityFileRoot* root = valid ? (SecurityFileRoot*)broker->roots->data[id - 1] : nullptr;
    valid = root && root->active;
    if (valid) { root->active = false; broker->epoch++; security_file_sweep(broker); }
    pthread_mutex_unlock(&broker->mutex);
    return valid;
}

bool security_file_broker_restrict(SecurityFileBroker* broker, SecurityOperations ceiling) {
    if (!broker) return false;
    pthread_mutex_lock(&broker->mutex);
    bool result = broker->epoch != UINT64_MAX && security_policy_restrict(broker->policy, ceiling);
    if (result) { broker->epoch++; security_file_sweep(broker); }
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

static SecurityIOResult security_file_read_internal(SecurityFileBroker* broker, const char* path, bool source,
        uint64_t limit, MemCategory category, char** data, size_t* size,
        FileRevision* revision, char** canonical) {
    if (data) *data = nullptr;
    if (size) *size = 0;
    if (!broker || !data || !size) return security_file_result(SECURITY_INVALID_REQUEST);
    pthread_mutex_lock(&broker->mutex);
    const char* relative;
    SecurityFileRoot* root = security_file_resolve(broker, path, &relative, source ? SECURITY_LOAD_SOURCE : SECURITY_READ);
    SecurityIOResult result = security_file_result(SECURITY_DENIED);
    SecurityFileLease lease = {};
    if (root) {
        result.decision = security_file_lease(root, SECURITY_OBJECT_FILE,
            source ? SECURITY_LOAD_SOURCE : SECURITY_READ, &lease);
        if (result.decision == SECURITY_ALLOWED) {
            broker->active = &lease;
            if (limit > broker->config.max_io_bytes) limit = broker->config.max_io_bytes;
            if (!file_anchor_read_all_named(root->anchor, relative, limit, category, data, size, revision, canonical))
                result.system_error = errno;
            broker->active = nullptr;
        }
    }
    security_file_release(broker, &lease);
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

static SecurityIOResult security_file_read(void* context, const char* path, bool source,
        uint64_t limit, MemCategory category, char** data, size_t* size) {
    return security_file_read_internal((SecurityFileBroker*)context, path, source,
        limit, category, data, size, nullptr, nullptr);
}

void security_source_capture_free(SecuritySourceCapture* capture) {
    if (!capture) return;
    mem_free(capture->data); mem_free(capture->path); *capture = {};
}

SecurityIOResult security_file_broker_capture_source(SecurityFileBroker* broker,
        const char* path, uint64_t limit, MemCategory category, SecuritySourceCapture* out) {
    if (!out) return security_file_result(SECURITY_INVALID_REQUEST, EINVAL);
    *out = {};
    return security_file_read_internal(broker, path, true, limit, category,
        &out->data, &out->size, &out->revision, &out->path);
}

bool security_file_broker_root_identity(SecurityFileBroker* broker, uint64_t id,
        SecurityFileRootIdentity* out) {
    if (out) *out = {};
    if (!broker || !out) return false;
    pthread_mutex_lock(&broker->mutex);
    bool valid = security_file_expire_roots(broker) && security_file_protections_valid(broker) &&
        id && id <= (uint64_t)broker->roots->length;
    SecurityFileRoot* root = valid ? (SecurityFileRoot*)broker->roots->data[id - 1] : nullptr;
    valid = root && root->active && file_anchor_validate(root->anchor);
    if (valid) {
        out->directory = file_anchor_identity(root->anchor);
        out->is_file = root->leaf != nullptr;
        valid = out->is_file
            ? file_anchor_stat(root->anchor, root->leaf, &out->file) &&
                file_revision_equal(&root->entry, &out->file) &&
                path_str_join_posix_into(out->path, sizeof(out->path), root->canonical_path, root->canonical_leaf) >= 0
            : path_str_copy(out->path, sizeof(out->path), root->canonical_path) >= 0;
    }
    if (!valid) *out = {};
    pthread_mutex_unlock(&broker->mutex);
    return valid;
}

static bool security_file_directory_path(const char* path, char* output, size_t capacity) {
    size_t length = path ? strnlen(path, capacity) : 0;
    if (!length || length >= capacity) return false;
    memcpy(output, path, length + 1);
    while (length > 1 && output[length - 1] == '/') output[--length] = '\0';
    return true;
}

static SecurityIOResult security_file_stat(void* context, const char* path, FileStat* metadata) {
    if (metadata) *metadata = {};
    SecurityFileBroker* broker = (SecurityFileBroker*)context;
    char normalized[4096];
    if (!broker || !metadata || !security_file_directory_path(path, normalized, sizeof(normalized)))
        return security_file_result(SECURITY_INVALID_REQUEST, EINVAL);
    bool directory_only = path[strlen(path) - 1] == '/';
    pthread_mutex_lock(&broker->mutex);
    const char* relative;
    SecurityFileRoot* root = security_file_resolve(broker, normalized, &relative, SECURITY_METADATA, true);
    SecurityIOResult result = security_file_result(SECURITY_DENIED);
    SecurityFileLease lease = {};
    if (root) {
        result.decision = security_file_lease(root, SECURITY_OBJECT_FILE, SECURITY_METADATA, &lease);
        if (result.decision == SECURITY_ALLOWED) {
            broker->active = &lease;
            FileRevision revision;
            if (file_anchor_stat(root->anchor, relative, &revision)) {
                if (directory_only && !revision.stat.is_dir) result.system_error = ENOTDIR;
                else *metadata = revision.stat;
            } else if (errno != ENOENT) result.system_error = errno ? errno : EIO;
            broker->active = nullptr;
        }
    }
    security_file_release(broker, &lease);
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

static SecurityIOResult security_file_input_kind(void* context, const char* path, int32_t* kind) {
    if (kind) *kind = -1;
    SecurityFileBroker* broker = (SecurityFileBroker*)context;
    char normalized[4096];
    if (!broker || !kind || !security_file_directory_path(path, normalized, sizeof(normalized)))
        return security_file_result(SECURITY_INVALID_REQUEST, EINVAL);
    bool directory_only = path[strlen(path) - 1] == '/';
    pthread_mutex_lock(&broker->mutex);
    const char* relative;
    SecurityFileRoot* root = security_file_resolve(broker, normalized, &relative, SECURITY_READ | SECURITY_ENUMERATE, true);
    SecurityIOResult result = security_file_result(SECURITY_DENIED);
    if (root) {
        const int32_t kinds[] = {SECURITY_OBJECT_FILE, SECURITY_OBJECT_DIRECTORY};
        for (int32_t candidate : kinds) {
            if (directory_only && candidate == SECURITY_OBJECT_FILE) continue;
            SecurityFileLease lease = {};
            SecurityDecision decision = security_file_lease(root, candidate,
                candidate == SECURITY_OBJECT_FILE ? SECURITY_READ : SECURITY_ENUMERATE, &lease);
            if (decision == SECURITY_ALLOWED) {
                broker->active = &lease;
                FileRevision revision;
                if (!file_anchor_stat(root->anchor, relative, &revision)) result.system_error = errno;
                else if ((candidate == SECURITY_OBJECT_FILE && revision.stat.is_file) ||
                        (candidate == SECURITY_OBJECT_DIRECTORY && revision.stat.is_dir)) {
                    *kind = candidate; result = security_file_result(SECURITY_ALLOWED);
                }
                broker->active = nullptr;
            }
            security_file_release(broker, &lease);
            if (*kind != -1) break;
        }
    }
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

struct SecurityFileListing {
    SecurityFileBroker* broker;
    FileAnchor* directory;
    SecurityFileLease* metadata;
    SecurityDirectory* output;
    uint64_t bytes;
};

static bool security_file_list_entry(void* context, const char* name, const FileRevision* revision) {
    SecurityFileListing* listing = (SecurityFileListing*)context;
    size_t bytes = strlen(name) + 1 + sizeof(SecurityDirectoryEntry);
    if (bytes > listing->broker->config.max_io_bytes - listing->bytes) { errno = EFBIG; return false; }
    SecurityDirectoryEntry* entry = &listing->output->entries[listing->output->count];
    entry->name = mem_strdup(name, MEM_CAT_INPUT_OTHER);
    if (!entry->name) { errno = ENOMEM; return false; }
    listing->output->count++;
    listing->bytes += bytes;
    entry->is_directory = revision->stat.is_dir;
    // Enumeration conveys a name and kind. Metadata has its own authorization
    // and object guard, including protected ancestors with a narrower denial.
    if (listing->metadata) {
        SecurityFileLease* previous = listing->broker->active;
        listing->broker->active = listing->metadata;
        FileRevision current;
        entry->has_metadata = file_anchor_stat(listing->directory, name, &current) &&
            file_revision_equal(revision, &current);
        if (entry->has_metadata) entry->metadata = current.stat;
        listing->broker->active = previous;
    }
    return true;
}

static SecurityIOResult security_file_enumerate(void* context, const char* path,
        uint32_t max_entries, SecurityDirectory* output) {
    if (output) *output = {};
    SecurityFileBroker* broker = (SecurityFileBroker*)context;
    char normalized[4096];
    if (!broker || !output || !max_entries || max_entries > SECURITY_IO_DIRECTORY_LIMIT ||
            !security_file_directory_path(path, normalized, sizeof(normalized)))
        return security_file_result(SECURITY_INVALID_REQUEST, EINVAL);
    pthread_mutex_lock(&broker->mutex);
    const char* relative;
    SecurityFileRoot* root = security_file_resolve(broker, normalized, &relative, SECURITY_ENUMERATE, true);
    SecurityIOResult result = security_file_result(SECURITY_DENIED);
    SecurityFileLease lease = {}, metadata = {};
    if (root) {
        bool has_metadata = security_file_lease(root, SECURITY_OBJECT_DIRECTORY,
            SECURITY_METADATA, &metadata) == SECURITY_ALLOWED;
        result.decision = security_file_lease(root, SECURITY_OBJECT_DIRECTORY, SECURITY_ENUMERATE, &lease);
        // Creating the optional selected grant can advance the policy epoch.
        metadata.request.generation = security_policy_generation(broker->policy);
        if (result.decision == SECURITY_ALLOWED) {
            broker->active = &lease;
            FileAnchor* directory = *relative ? file_anchor_directory(root->anchor, relative,
                false, root->anchor_flags) : root->anchor;
            if (directory) {
                output->entries = (SecurityDirectoryEntry*)mem_calloc(max_entries,
                    sizeof(SecurityDirectoryEntry), MEM_CAT_INPUT_OTHER);
                SecurityFileListing listing = {broker, directory, has_metadata ? &metadata : nullptr, output, 0};
                if (!output->entries) result.system_error = ENOMEM;
                else if (!file_anchor_visit(directory, max_entries, security_file_list_entry, &listing))
                    result.system_error = errno ? errno : EIO;
                if (directory != root->anchor) file_anchor_close(directory);
            } else result.system_error = errno;
            broker->active = nullptr;
        }
    }
    security_file_release(broker, &lease); security_file_release(broker, &metadata);
    if (result.decision != SECURITY_ALLOWED || result.system_error) security_io_directory_free(output);
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

static SecurityIOResult security_file_commit(SecurityFileBroker* broker, SecurityFileLease* lease,
        const char* relative, const void* data, size_t size, bool append, bool atomic,
        const FileRevision* expected) {
    SecurityIOResult result = security_file_result(SECURITY_ALLOWED);
    if (size > broker->config.max_total_write_bytes - broker->written_bytes ||
        broker->write_operations == broker->config.max_write_operations)
        return security_file_result(SECURITY_LIMIT_EXCEEDED);
    broker->write_operations++;
    broker->written_bytes += size; // charge attempted writes too, including partial failures
    broker->active = lease;
    int status = -1;
    if (!expected) {
        char* parent = file_path_dirname(relative);
        bool ready = parent && !strcmp(parent, ".");
        if (parent && !ready) {
            FileAnchor* directory = file_anchor_directory(lease->root->anchor, parent, true, lease->root->anchor_flags);
            ready = directory != nullptr;
            file_anchor_close(directory);
        }
        mem_free(parent);
        if (ready) status = file_anchor_write_atomic(lease->root->anchor, relative, data, size, nullptr);
    } else status = atomic && !append
        ? file_anchor_write_atomic(lease->root->anchor, relative, data, size, expected)
        : file_anchor_write_existing(lease->root->anchor, relative, data, size, expected, append);
    result.system_error = status ? (errno ? errno : EIO) : 0;
    result.committed = status >= 0;
    broker->active = nullptr;
    return result;
}

static SecurityIOResult security_file_write(void* context, const char* path, const void* data,
        size_t size, bool append, bool atomic) {
    SecurityFileBroker* broker = (SecurityFileBroker*)context;
    if (!broker || !data) return security_file_result(SECURITY_INVALID_REQUEST);
    if (size > broker->config.max_io_bytes) return security_file_result(SECURITY_LIMIT_EXCEEDED);
    pthread_mutex_lock(&broker->mutex);
    security_file_sweep(broker);
    const char* relative;
    SecurityFileRoot* root = security_file_resolve(broker, path, &relative, append ? SECURITY_CREATE | SECURITY_APPEND : SECURITY_CREATE | SECURITY_REPLACE);
    SecurityIOResult result = security_file_result(SECURITY_DENIED);
    SecurityFileLease lease = {}, creation = {};
    if (root) {
        SecurityOperations operation = append ? SECURITY_APPEND : SECURITY_REPLACE;
        SecurityDecision existing = security_file_lease(root, SECURITY_OBJECT_FILE, operation, &lease);
        SecurityDecision fresh = security_file_lease(root, SECURITY_OBJECT_NEW_FILE, SECURITY_CREATE, &creation);
        // Namespace authority is checked before filesystem discovery or mkdir.
        if (existing == SECURITY_ALLOWED || existing == SECURITY_APPROVAL_REQUIRED || fresh == SECURITY_ALLOWED) {
            FileRevision target = {};
            errno = 0;
            bool exists = file_anchor_stat(root->anchor, relative, &target);
            int error = errno;
            if (exists && target.stat.is_file) {
                security_file_release(broker, &creation);
                // Creating another lease may have advanced the policy generation.
                lease.request.generation = security_policy_generation(broker->policy);
                result.decision = security_policy_check(broker->policy, &lease.request);
                if (result.decision == SECURITY_ALLOWED)
                    result = security_file_commit(broker, &lease, relative, data, size, append, atomic, &target);
                else if (result.decision == SECURITY_APPROVAL_REQUIRED) {
                    result.decision = SECURITY_LIMIT_EXCEEDED;
                    uint64_t now = security_file_now(broker);
                    if ((uint32_t)broker->pending->length < broker->config.max_pending &&
                        size <= broker->config.max_pending_bytes - broker->pending_bytes &&
                        broker->next_approval != UINT64_MAX &&
                        now <= UINT64_MAX - broker->config.approval_lifetime_seconds) {
                        SecurityPendingWrite* pending = (SecurityPendingWrite*)mem_calloc(1, sizeof(*pending), MEM_CAT_SYSTEM);
                        if (pending) {
                            pending->relative = mem_strdup(relative, MEM_CAT_SYSTEM);
                            pending->payload = (char*)mem_alloc(size ? size : 1, MEM_CAT_SYSTEM);
                            pending->lease = lease; pending->append = append; pending->atomic = atomic;
                            pending->description.approval = {broker->config.policy.session, broker->next_approval++};
                            pending->description.principal = broker->config.policy.principal;
                            pending->description.root = root->id; pending->description.operation = operation;
                            pending->description.size = size; pending->description.target = target;
                            pending->description.expires_at = now + broker->config.approval_lifetime_seconds;
                            bool valid = pending->relative && pending->payload &&
                                path_str_join_posix_into(pending->description.path, sizeof(pending->description.path),
                                    root->path, relative) >= 0;
                            if (valid) {
                                // Hash the retained snapshot, never the caller's mutable buffer.
                                memcpy(pending->payload, data, size);
                                valid = digest_sha256(pending->payload, size, pending->description.digest);
                            }
                            if (valid && arraylist_append(broker->pending, pending)) {
                                broker->pending_bytes += size; lease = {};
                                result.decision = SECURITY_APPROVAL_REQUIRED;
                                result.approval = pending->description.approval;
                            } else { mem_free(pending->relative); mem_free(pending->payload); mem_free(pending); }
                        }
                    }
                }
            } else if (!exists && error == ENOENT && fresh == SECURITY_ALLOWED) {
                security_file_release(broker, &lease);
                creation.request.generation = security_policy_generation(broker->policy);
                result = security_file_commit(broker, &creation, relative, data, size, append, atomic, nullptr);
            } else { result.system_error = exists ? EISDIR : error; }
        } else result.decision = existing == SECURITY_LIMIT_EXCEEDED || fresh == SECURITY_LIMIT_EXCEEDED
            ? SECURITY_LIMIT_EXCEEDED : SECURITY_DENIED;
    }
    security_file_release(broker, &lease); security_file_release(broker, &creation);
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

static SecurityIOResult security_file_canonical_source(void* context, const char* path, char** canonical) {
    if (canonical) *canonical = nullptr;
    SecurityFileBroker* broker = (SecurityFileBroker*)context;
    if (!broker || !canonical) return security_file_result(SECURITY_INVALID_REQUEST, EINVAL);
    pthread_mutex_lock(&broker->mutex);
    const char* relative;
    SecurityFileRoot* root = security_file_resolve(broker, path, &relative, SECURITY_LOAD_SOURCE);
    SecurityIOResult result = security_file_result(SECURITY_DENIED);
    SecurityFileLease lease = {};
    if (root) {
        result.decision = security_file_lease(root, SECURITY_OBJECT_FILE, SECURITY_LOAD_SOURCE, &lease);
        if (result.decision == SECURITY_ALLOWED) {
            broker->active = &lease;
            FILE* stream = file_anchor_open_read(root->anchor, relative);
            if (stream) {
                FileRevision acquired = file_revision_stream(stream), named = {};
                char* actual = file_stream_path(stream);
                bool closed = fclose(stream) == 0;
                size_t prefix = strlen(root->canonical_path);
                bool valid = actual && closed && !strncmp(actual, root->canonical_path, prefix) &&
                    actual[prefix] == '/' && file_anchor_stat(root->anchor, relative, &named) &&
                    file_revision_equal(&acquired, &named) && file_anchor_validate(root->anchor);
                if (valid) *canonical = actual;
                else { mem_free(actual); result.system_error = errno ? errno : ESTALE; }
            } else result.system_error = errno;
            broker->active = nullptr;
        }
    }
    security_file_release(broker, &lease);
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

static SecurityIOResult security_file_authority_stamp(void* context, SecurityAuthorityStamp* stamp) {
    if (stamp) *stamp = {};
    SecurityFileBroker* broker = (SecurityFileBroker*)context;
    if (!broker || !stamp) return security_file_result(SECURITY_INVALID_REQUEST, EINVAL);
    pthread_mutex_lock(&broker->mutex);
    bool valid = security_file_expire_roots(broker);
    if (valid) *stamp = {broker->config.policy.session, broker->epoch};
    pthread_mutex_unlock(&broker->mutex);
    return security_file_result(valid ? SECURITY_ALLOWED : SECURITY_DENIED);
}

SecurityIOClient security_file_broker_client(SecurityFileBroker* broker) {
    return {1, broker, security_file_read, security_file_write, security_file_canonical_source,
        security_file_input_kind, security_file_enumerate, security_file_stat, security_file_authority_stamp};
}

bool security_file_broker_describe(SecurityFileBroker* broker, SecurityApprovalRef ref,
        SecurityOverwriteChallenge* out) {
    if (out) *out = {};
    if (!broker || !out) return false;
    pthread_mutex_lock(&broker->mutex);
    int index = security_file_pending_index(broker, ref);
    SecurityPendingWrite* pending = index >= 0 ? (SecurityPendingWrite*)broker->pending->data[index] : nullptr;
    bool valid = pending && security_file_pending_current(broker, pending);
    if (valid) *out = pending->description;
    pthread_mutex_unlock(&broker->mutex);
    return valid;
}

SecurityIOResult security_file_broker_approve(SecurityFileBroker* broker, SecurityApprovalRef ref) {
    if (!broker) return security_file_result(SECURITY_INVALID_REQUEST);
    pthread_mutex_lock(&broker->mutex);
    int index = security_file_pending_index(broker, ref);
    SecurityPendingWrite* pending = index >= 0 ? (SecurityPendingWrite*)broker->pending->data[index] : nullptr;
    SecurityIOResult result = security_file_result(SECURITY_REVOKED);
    if (pending) {
        arraylist_remove(broker->pending, (unsigned)index);
        if (security_file_pending_current(broker, pending) && security_file_protections_valid(broker)) {
            uint64_t grant;
            result.decision = security_policy_grant(broker->policy, pending->lease.request.resource,
                pending->lease.request.operation, pending->description.expires_at, &grant);
            if (result.decision == SECURITY_ALLOWED) {
                pending->lease.request.generation = security_policy_generation(broker->policy);
                result = security_file_commit(broker, &pending->lease, pending->relative, pending->payload,
                    pending->description.size, pending->append, pending->atomic, &pending->description.target);
            }
        }
        security_file_pending_free(broker, pending);
    }
    pthread_mutex_unlock(&broker->mutex);
    return result;
}

bool security_file_broker_cancel(SecurityFileBroker* broker, SecurityApprovalRef ref) {
    if (!broker) return false;
    pthread_mutex_lock(&broker->mutex);
    int index = security_file_pending_index(broker, ref);
    if (index >= 0) {
        SecurityPendingWrite* pending = (SecurityPendingWrite*)broker->pending->data[index];
        arraylist_remove(broker->pending, (unsigned)index);
        security_file_pending_free(broker, pending);
    }
    pthread_mutex_unlock(&broker->mutex);
    return index >= 0;
}
