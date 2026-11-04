#pragma once

#include "../../io/security_policy.h"
#include "../../../lib/file.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SecurityGrantStore SecurityGrantStore;
typedef struct SecurityGrantLease SecurityGrantLease;
enum {
    SECURITY_PRINCIPAL_APP = 1,
    SECURITY_PRINCIPAL_SCRIPT = 2,
    SECURITY_SAVED_FILE = 1,
    SECURITY_SAVED_DIRECTORY = 2,
    SECURITY_SAVED_ENDPOINT = 3,
    SECURITY_SAVED_SERVICE = 4,
    SECURITY_SAVED_PERSISTENT = 1,
    SECURITY_SAVED_RECURSIVE = 2,
    SECURITY_STORE_MAX_GRANTS = 256,
    SECURITY_STORE_MAX_LOCATOR = 4096,
    SECURITY_STORE_MAX_BYTES = 1024 * 1024
};

typedef enum SecurityStoreResult {
    SECURITY_STORE_OK,
    SECURITY_STORE_MISSING,
    SECURITY_STORE_CONFLICT,
    SECURITY_STORE_IDENTITY_CHANGED,
    SECURITY_STORE_CORRUPT,
    SECURITY_STORE_UNSAFE_PATH,
    SECURITY_STORE_UNSUPPORTED,
    SECURITY_STORE_IO_ERROR,
    SECURITY_STORE_INVALID,
    SECURITY_STORE_UNAVAILABLE
} SecurityStoreResult;

typedef struct SecurityGrantBinding {
    uint32_t principal_kind;
    uint8_t principal[32];
    // trusted admission digests include object identity, not just display names/content
    uint8_t code[32];
    uint8_t root[32];
} SecurityGrantBinding;

typedef struct SecurityPersistentGrant {
    uint8_t resource[32];
    SecurityOperations operations;
    uint64_t expires_at;
    uint32_t kind;
    uint32_t flags;
    const char* locator;
} SecurityPersistentGrant;

typedef struct SecurityGrantSnapshot {
    SecurityGrantBinding binding;
    uint64_t generation;
    uint32_t count;
    SecurityPersistentGrant* grants;
} SecurityGrantSnapshot;

// Trusted host only: resolves the OS account home, never HOME/LAMBDA_HOME.
// No store management entrypoint is part of the worker's I/O client API.
SecurityGrantStore* security_grant_store_open(SecurityStoreResult* result);
// OS-derived home only; read-only resolution, mem_free() the returned string.
char* security_grant_store_account_home(void);
// Host catalog uses the actual held settings directory, not an environment path.
char* security_grant_store_directory(SecurityGrantStore* store);
bool security_grant_store_identity(SecurityGrantStore* store, FileRevision* out);
// Host-owned private storage namespace beside settings. Never a worker resource.
// Caller owns this anchor; opening it does not grant any principal data access.
FileAnchor* security_grant_store_private_root(SecurityGrantStore* store);
void security_grant_store_close(SecurityGrantStore* store);
SecurityStoreResult security_grant_store_load(SecurityGrantStore* store,
    const SecurityGrantBinding* binding, SecurityGrantSnapshot* out);
// Trusted inspection/revalidation only. This returns records, never authority;
// the launcher must verify the complete binding before restoring any grant.
SecurityStoreResult security_grant_store_inspect(SecurityGrantStore* store,
    uint32_t principal_kind, const uint8_t principal[32], SecurityGrantSnapshot* out);
// Pin an acknowledged generation through one bounded host operation. Host
// revocation/save waits for prior operations to leave; release on same thread.
// No UI wait, store management or unbounded worker wait may hold this lease.
SecurityGrantLease* security_grant_store_lease(SecurityGrantStore* store,
    const SecurityGrantBinding* binding, uint64_t generation, SecurityStoreResult* result);
void security_grant_store_release(SecurityGrantLease* lease);
void security_grant_snapshot_destroy(SecurityGrantSnapshot* snapshot);
SecurityStoreResult security_grant_store_save(SecurityGrantStore* store,
    const SecurityGrantBinding* binding, uint64_t expected_generation,
    const SecurityPersistentGrant* grants, uint32_t count, uint64_t* out_generation);
// A trusted re-admission changes the code/root binding and clears all old grants.
SecurityStoreResult security_grant_store_rebind(SecurityGrantStore* store,
    const SecurityGrantBinding* previous, const SecurityGrantBinding* replacement,
    uint64_t expected_generation, uint64_t* out_generation);

#ifdef LAMBDA_SECURITY_STORE_TESTING
// Compiled only into the isolated fixture runner; never a production path override.
SecurityGrantStore* security_grant_store_test_open(const char* fixture_home, SecurityStoreResult* result);
#endif

#ifdef __cplusplus
}
#endif
