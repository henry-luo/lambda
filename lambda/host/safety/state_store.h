#pragma once
#include "grant_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SecurityStateTransaction SecurityStateTransaction;
typedef enum SecurityStateSlot {
    SECURITY_STATE_PACKAGE_UPDATES = 1,
    SECURITY_STATE_PRIVATE_QUOTA = 2,
    SECURITY_STATE_UPDATE_METADATA = 3
} SecurityStateSlot;

// Trusted-host records only, outside worker ABI. Fixed slots cannot name grant
// files or arbitrary paths. Reuses the grant store's held directory and lock.
// One bounded local read/publication; no UI/network/extraction wait under lock.
SecurityStateTransaction* security_state_begin(SecurityGrantStore* store,
    SecurityStateSlot slot, SecurityStoreResult* result);
// Only principal-keyed quota records are accepted here. Digest/kind construct
// the fixed filename; callers cannot supply a pathname or select a grant file.
SecurityStateTransaction* security_state_begin_principal(SecurityGrantStore* store,
    SecurityStateSlot slot, uint32_t kind, const uint8_t principal[32], SecurityStoreResult* result);
// Select one package checkpoint while retaining an acquired update-catalog
// transaction's lock. Fixed derived names cannot select grant/quota records.
// Selection consumes the earlier read revision; read the selected record anew.
bool security_state_select_update_metadata(SecurityStateTransaction* transaction, const uint8_t package[32]);
SecurityStoreResult security_state_read(SecurityStateTransaction* transaction,
    char** bytes, size_t* size);
// Requires a preceding successful/missing read. Uses its held revision; a
// committed but non-durable result quarantines this store instance.
SecurityStoreResult security_state_publish(SecurityStateTransaction* transaction,
    const void* bytes, size_t size);
void security_state_end(SecurityStateTransaction* transaction);
// Current canonical profile directory/object identity, while its lock is held.
bool security_state_profile(SecurityStateTransaction* transaction, uint8_t digest[32]);
bool security_state_fingerprint_present(const uint8_t digest[32]);
// Fixed host-only namespace; caller already holds this transaction's store lock.
// Read-only acquisition never creates it. No app/user path participates.
FileAnchor* security_state_package_staging(SecurityStateTransaction* transaction, bool create);

#ifdef __cplusplus
}
#endif
