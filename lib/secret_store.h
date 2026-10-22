/**
 * @file secret_store.h
 * @brief Generic passwords in the OS secret store, named by a service and an
 * account: the Keychain on macOS; other platforms report SECRET_STORE_UNSUPPORTED.
 *
 * Calls may block on the OS (an unlock or access prompt), so callers run them
 * off the event loop. Never log a value.
 */
#ifndef LIB_SECRET_STORE_H
#define LIB_SECRET_STORE_H

#include <stddef.h>
#include "strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SecretStoreStatus {
    SECRET_STORE_OK = 0,
    SECRET_STORE_NOT_FOUND,
    SECRET_STORE_UNSUPPORTED,   // no backend on this platform
    SECRET_STORE_DENIED,        // the user or the OS refused access
    SECRET_STORE_FAILED,
} SecretStoreStatus;

/** Appends the stored value to out; on failure, message holds the OS reason. */
SecretStoreStatus secret_store_get(const char* service, const char* account, StrBuf* out,
                                   char* message, size_t message_size);
/** Stores value, replacing any earlier one for the same service and account. */
SecretStoreStatus secret_store_set(const char* service, const char* account,
                                   const char* value, size_t length,
                                   char* message, size_t message_size);
/** SECRET_STORE_NOT_FOUND when there was nothing to delete. */
SecretStoreStatus secret_store_delete(const char* service, const char* account,
                                      char* message, size_t message_size);
/** Zeroes memory that held a secret; the compiler may not elide it. */
void secret_store_wipe(void* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
