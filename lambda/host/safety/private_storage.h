#pragma once
#include "file_broker.h"
#include "grant_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SecurityPrivateStorage SecurityPrivateStorage;
enum {
    SECURITY_PRIVATE_MAX_ENTRIES = 4096,
    // management includes a possible extra internal atomic staging entry
    SECURITY_PRIVATE_SCAN_ENTRIES = SECURITY_PRIVATE_MAX_ENTRIES + 1
};
typedef enum SecurityPrivateStorageKind {
    SECURITY_PRIVATE_DATA,
    SECURITY_PRIVATE_CACHE,
    SECURITY_PRIVATE_TEMP,
    SECURITY_PRIVATE_COUNT
} SecurityPrivateStorageKind;
const char* security_private_storage_kind_name(SecurityPrivateStorageKind kind);
typedef struct SecurityPrivateStorageLimit {
    uint64_t max_bytes;
    uint32_t max_entries; // files and directories combined
} SecurityPrivateStorageLimit;
typedef struct SecurityPrivateStorageConfig {
    SecurityGrantStore* store;
    SecurityGrantBinding binding;
    SecurityFileBrokerConfig broker;
    SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT]; // trusted fallback if no custom policy
} SecurityPrivateStorageConfig;
typedef struct SecurityPrivateStorageUsage {
    uint64_t bytes;
    uint32_t entries;
    bool over_limit;
} SecurityPrivateStorageUsage;
typedef struct SecurityPrivateStorageStatus {
    SecurityPrivateStorageUsage usage[SECURITY_PRIVATE_COUNT];
    SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT];
    uint64_t quota_generation;
    bool custom_quota;
    uint32_t live_sessions;
    uint32_t abandoned_sessions;
    bool reset_pending;
    uint8_t reset_id[16];
} SecurityPrivateStorageStatus;
void security_private_storage_default_limits(SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT]);

// Trusted host only (D11.1.1-2, D11.2.1-7). Private data grants confer no source
// loading or host settings authority. Caller serializes callbacks and destruction.
SecurityPrivateStorage* security_private_storage_open(const SecurityPrivateStorageConfig* config);
void security_private_storage_close(SecurityPrivateStorage* storage);
// After borrowers join: refuse further IO and remove this admission's temp
// tree. Failure is observable and leaves abandoned state for a later recovery.
bool security_private_storage_finish(SecurityPrivateStorage* storage);
// Trusted cache eviction only; does not remove persistent data or live temps.
bool security_private_storage_clear_cache(SecurityPrivateStorage* storage);
// Trusted management only, never worker opcodes. Inspection neither recovers
// abandoned sessions nor creates an admission. Output is published on success.
bool security_private_storage_inspect(const SecurityPrivateStorageConfig* config,
    SecurityPrivateStorageStatus* status);
// Trusted quota editing requires joined admissions and the inspected generation
// (zero for an absent record). NULL limits restores this host's fallback limits;
// it persists a new generation, never deletes data or clears other permissions.
bool security_private_storage_set_quota(const SecurityPrivateStorageConfig* config,
    const SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT], uint64_t expected_generation, uint64_t* generation);
// Explicit persistent-data deletion after trusted consent. Refuses live
// admissions; a durable intent blocks new admissions until cleanup completes.
// Each bounded step checks the same principal/object-bound random reset ID.
// Cache, grants, update settings and other principals are preserved.
bool security_private_storage_reset_begin(const SecurityPrivateStorageConfig* config, uint8_t reset_id[16]);
bool security_private_storage_reset_step(const SecurityPrivateStorageConfig* config,
    const uint8_t reset_id[16], uint32_t max_removals, bool* complete);
SecurityIOClient security_private_storage_client(SecurityPrivateStorage* storage);
bool security_private_storage_path(const char* path);

#ifdef __cplusplus
}
#endif
