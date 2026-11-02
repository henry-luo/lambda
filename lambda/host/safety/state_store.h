#pragma once
#include "grant_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SecurityStateTransaction SecurityStateTransaction;
typedef enum SecurityStateSlot {
    SECURITY_STATE_PACKAGE_UPDATES = 1
} SecurityStateSlot;

// Trusted-host records only, outside worker ABI. Fixed slots cannot name grant
// files or arbitrary paths. Reuses the grant store's held directory and lock.
// One bounded local read/publication; no UI/network/extraction wait under lock.
SecurityStateTransaction* security_state_begin(SecurityGrantStore* store,
    SecurityStateSlot slot, SecurityStoreResult* result);
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

#ifdef __cplusplus
}
#endif
