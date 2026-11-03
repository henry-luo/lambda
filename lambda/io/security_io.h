#pragma once

#include "security_policy.h"
#include "../../lib/memtrack.h"
#include "../../lib/file.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SecurityApprovalRef {
    uint64_t session;
    uint64_t serial;
} SecurityApprovalRef;

typedef struct SecurityIOResult {
    SecurityDecision decision;
    int system_error;
    // Publication or an in-place mutation occurred. An error may mean partial
    // or non-durable bytes; never retry a committed error blindly.
    bool committed;
    SecurityApprovalRef approval;
    // Transport loss after write dispatch can leave an unknown outcome. This
    // is distinct from a confirmed commit; neither permits an automatic retry.
    bool outcome_unknown;
} SecurityIOResult;

enum { SECURITY_IO_DIRECTORY_LIMIT = 4096, SECURITY_IO_PATH_DEPTH_LIMIT = 64 };
typedef struct SecurityDirectoryEntry {
    char* name;
    bool is_directory;
    bool has_metadata;
    FileStat metadata;
} SecurityDirectoryEntry;
typedef struct SecurityDirectory {
    SecurityDirectoryEntry* entries;
    uint32_t count;
} SecurityDirectory;
// Invalidates context-owned snapshots on any authority change/expiry. A stamp
// is not an object capability and cannot authorize acquisition or delivery.
typedef struct SecurityAuthorityStamp {
    uint64_t session;
    uint64_t epoch;
} SecurityAuthorityStamp;
void security_io_directory_free(SecurityDirectory* directory);

// Worker-facing byte transport, shared by local adapters and future broker IPC.
// No grant, approval, raw descriptor, anchor or store-management operation.
typedef struct SecurityIOClient {
    uint32_t version;
    void* context;
    SecurityIOResult (*read)(void* context, const char* path, bool source,
        uint64_t limit, MemCategory category, char** data, size_t* size);
    SecurityIOResult (*write)(void* context, const char* path, const void* data,
        size_t size, bool append, bool atomic);
    // Source-authorized identity acquisition, never worker-side realpath().
    // Caller owns the returned path and frees it with mem_free().
    SecurityIOResult (*canonical_source)(void* context, const char* path, char** canonical);
    // Returns FILE with READ authority or DIRECTORY with ENUMERATE authority.
    // This classification never grants general metadata access.
    SecurityIOResult (*input_kind)(void* context, const char* path, int32_t* kind);
    SecurityIOResult (*enumerate)(void* context, const char* path,
        uint32_t max_entries, SecurityDirectory* directory);
    // METADATA is independent of READ/ENUMERATE. Authorized absence is a
    // successful query with exists=false, never an ambient fallback.
    SecurityIOResult (*stat)(void* context, const char* path, FileStat* metadata);
    SecurityIOResult (*authority_stamp)(void* context, SecurityAuthorityStamp* stamp);
} SecurityIOClient;

// Broker-only virtual mount. This is not a host filesystem directory; the
// admitted channel selects its owner, never a principal encoded in the path.
#define SECURITY_APP_DATA_PATH "/.lambda/app-data"
#define SECURITY_APP_CACHE_PATH "/.lambda/app-cache"
#define SECURITY_APP_TEMP_PATH "/.lambda/app-temp"

// Context-free acquisition seam. NULL client is an explicitly ambient host
// mechanism; a present but invalid/denying client never falls back to it.
bool security_io_read_bytes(const SecurityIOClient* client, const char* path,
    bool source, uint64_t limit, MemCategory category, char** data, size_t* size,
    SecurityIOResult* result);
bool security_io_source_path(const SecurityIOClient* client, const char* path,
    char** canonical, SecurityIOResult* result);
bool security_io_input_kind(const SecurityIOClient* client, const char* path,
    int32_t* kind, SecurityIOResult* result);
bool security_io_enumerate(const SecurityIOClient* client, const char* path,
    uint32_t max_entries, SecurityDirectory* directory, SecurityIOResult* result);
bool security_io_stat(const SecurityIOClient* client, const char* path,
    FileStat* metadata, SecurityIOResult* result);
bool security_io_authority_stamp(const SecurityIOClient* client,
    SecurityAuthorityStamp* stamp, SecurityIOResult* result);

#ifdef __cplusplus
}
#endif
