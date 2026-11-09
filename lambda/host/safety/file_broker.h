#pragma once

#include "../../io/security_io.h"
#include "lib/file.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SecurityFileBroker SecurityFileBroker;
typedef struct SecurityFileBrokerConfig {
    SecurityPolicyConfig policy;
    uint64_t max_io_bytes;
    uint64_t max_total_write_bytes;
    uint64_t max_pending_bytes;
    uint32_t max_pending;
    uint32_t max_write_operations;
    uint32_t approval_lifetime_seconds;
    // Host clock and context; timestamps never come from the worker.
    uint64_t (*clock)(void* context);
    void* clock_context;
} SecurityFileBrokerConfig;

typedef struct SecurityFileRootSpec {
    const char* path;
    int32_t scope;
    SecurityOperations ceiling;
    SecurityOperations selected_operations;
    uint64_t expires_at;
    bool admitted_source;
    bool default_root;
    // ENTRY requires one exact leaf, pinned to its admitted revision.
    // SELECTED may likewise select one file without exposing its siblings.
    const char* leaf;
} SecurityFileRootSpec;

typedef struct SecurityOverwriteChallenge {
    SecurityApprovalRef approval;
    uint64_t principal;
    uint64_t root;
    SecurityOperations operation;
    uint64_t expires_at;
    size_t size;
    uint8_t digest[32];
    FileRevision target;
    // Copied into host-owned storage by describe; never use a worker label for UI.
    char path[4096];
} SecurityOverwriteChallenge;

// These management functions are trusted-host-only, outside the worker ABI.
SecurityFileBroker* security_file_broker_create(const SecurityFileBrokerConfig* config);
void security_file_broker_destroy(SecurityFileBroker* broker);
// Install the complete protected-directory catalog before admitting any roots.
// Object identities are checked on absolute ancestors and relative traversal.
bool security_file_broker_protect(SecurityFileBroker* broker, const char* directory,
    SecurityOperations denied_operations);
// Reserves a child namespace even while absent, using an acquired parent
// identity and conservative Unicode alias folding; never a path-prefix grant.
bool security_file_broker_protect_name(SecurityFileBroker* broker, const char* parent,
    const char* name, SecurityOperations denied_operations);
uint64_t security_file_broker_admit_root(SecurityFileBroker* broker, const SecurityFileRootSpec* root);
bool security_file_broker_revoke_root(SecurityFileBroker* broker, uint64_t root);
bool security_file_broker_restrict(SecurityFileBroker* broker, SecurityOperations ceiling);
typedef struct SecurityFileRootIdentity {
    FileRevision directory;
    FileRevision file;
    bool is_file;
    char path[4096];
} SecurityFileRootIdentity;
typedef struct SecuritySourceCapture {
    char* path;
    char* data;
    size_t size;
    FileRevision revision;
} SecuritySourceCapture;
// Trusted admission only; never in the worker protocol. A locator is not an
// identity: these describe an acquired root/file after its anchor validates.
bool security_file_broker_root_identity(SecurityFileBroker* broker, uint64_t root,
    SecurityFileRootIdentity* out);
SecurityIOResult security_file_broker_capture_source(SecurityFileBroker* broker,
    const char* path, uint64_t limit, MemCategory category, SecuritySourceCapture* out);
void security_source_capture_free(SecuritySourceCapture* capture);
SecurityIOClient security_file_broker_client(SecurityFileBroker* broker);
bool security_file_broker_describe(SecurityFileBroker* broker, SecurityApprovalRef approval,
    SecurityOverwriteChallenge* out);
SecurityIOResult security_file_broker_approve(SecurityFileBroker* broker, SecurityApprovalRef approval);
bool security_file_broker_cancel(SecurityFileBroker* broker, SecurityApprovalRef approval);

#ifdef __cplusplus
}
#endif
