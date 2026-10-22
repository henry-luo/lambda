/**
 * @file secret_store.c
 * @brief OS secret store for generic passwords. See secret_store.h.
 */

#include "secret_store.h"
#include <stdio.h>
#include <string.h>

void secret_store_wipe(void* data, size_t length) {
    volatile unsigned char* p = (volatile unsigned char*)data;
    while (p && length--) *p++ = 0;
}

static void secret_message(char* message, size_t size, const char* text) {
    if (message && size) snprintf(message, size, "%s", text);
}

#ifdef __APPLE__
#include <Security/Security.h>

/** The OS's own wording for status, or its number when it has none. */
static void secret_os_message(OSStatus status, char* message, size_t size) {
    if (!message || !size) return;
    CFStringRef text = SecCopyErrorMessageString(status, NULL);
    if (!text || !CFStringGetCString(text, message, (CFIndex)size, kCFStringEncodingUTF8)) {
        snprintf(message, size, "Keychain error %d", (int)status);
    }
    if (text) CFRelease(text);
}

static SecretStoreStatus secret_status(OSStatus status, char* message, size_t size) {
    switch (status) {
    case errSecSuccess: return SECRET_STORE_OK;
    case errSecItemNotFound: return SECRET_STORE_NOT_FOUND;
    case errSecAuthFailed:
    case errSecUserCanceled:
    case errSecInteractionNotAllowed:
    case errSecNoAccessForItem:
        secret_os_message(status, message, size);
        return SECRET_STORE_DENIED;
    default:
        secret_os_message(status, message, size);
        return SECRET_STORE_FAILED;
    }
}

/** The generic-password query for one service and account; NULL when out of memory. */
static CFMutableDictionaryRef secret_query(const char* service, const char* account) {
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFStringRef service_text = CFStringCreateWithCString(NULL, service, kCFStringEncodingUTF8);
    CFStringRef account_text = CFStringCreateWithCString(NULL, account, kCFStringEncodingUTF8);
    if (query && service_text && account_text) {
        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, service_text);
        CFDictionarySetValue(query, kSecAttrAccount, account_text);
    } else if (query) {
        CFRelease(query);
        query = NULL;
    }
    if (service_text) CFRelease(service_text);
    if (account_text) CFRelease(account_text);
    return query;
}

SecretStoreStatus secret_store_get(const char* service, const char* account, StrBuf* out,
                                   char* message, size_t message_size) {
    CFMutableDictionaryRef query = secret_query(service, account);
    if (!query) {
        secret_message(message, message_size, "out of memory");
        return SECRET_STORE_FAILED;
    }
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef found = NULL;
    OSStatus status = SecItemCopyMatching(query, &found);
    CFRelease(query);
    SecretStoreStatus result = secret_status(status, message, message_size);
    if (result == SECRET_STORE_OK) {
        if (found && CFGetTypeID(found) == CFDataGetTypeID()) {
            CFDataRef data = (CFDataRef)found;
            strbuf_append_str_n(out, (const char*)CFDataGetBytePtr(data), (size_t)CFDataGetLength(data));
        } else {
            secret_message(message, message_size, "the Keychain returned no data");
            result = SECRET_STORE_FAILED;
        }
    }
    if (found) CFRelease(found);
    return result;
}

SecretStoreStatus secret_store_set(const char* service, const char* account,
                                   const char* value, size_t length,
                                   char* message, size_t message_size) {
    CFMutableDictionaryRef query = secret_query(service, account);
    CFDataRef data = CFDataCreate(NULL, (const UInt8*)value, (CFIndex)length);
    CFMutableDictionaryRef change = CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    SecretStoreStatus result = SECRET_STORE_FAILED;
    if (!query || !data || !change) {
        secret_message(message, message_size, "out of memory");
    } else {
        // an existing item keeps its access list; only its data changes
        CFDictionarySetValue(change, kSecValueData, data);
        OSStatus status = SecItemUpdate(query, change);
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query, kSecValueData, data);
            status = SecItemAdd(query, NULL);
        }
        result = secret_status(status, message, message_size);
    }
    if (query) CFRelease(query);
    if (data) CFRelease(data);
    if (change) CFRelease(change);
    return result;
}

SecretStoreStatus secret_store_delete(const char* service, const char* account,
                                      char* message, size_t message_size) {
    CFMutableDictionaryRef query = secret_query(service, account);
    if (!query) {
        secret_message(message, message_size, "out of memory");
        return SECRET_STORE_FAILED;
    }
    OSStatus status = SecItemDelete(query);
    CFRelease(query);
    return secret_status(status, message, message_size);
}

#else
// MQ7: macOS first; libsecret and Credential Manager backends come later

SecretStoreStatus secret_store_get(const char* service, const char* account, StrBuf* out,
                                   char* message, size_t message_size) {
    (void)service; (void)account; (void)out;
    secret_message(message, message_size, "no secret store on this platform");
    return SECRET_STORE_UNSUPPORTED;
}

SecretStoreStatus secret_store_set(const char* service, const char* account,
                                   const char* value, size_t length,
                                   char* message, size_t message_size) {
    (void)service; (void)account; (void)value; (void)length;
    secret_message(message, message_size, "no secret store on this platform");
    return SECRET_STORE_UNSUPPORTED;
}

SecretStoreStatus secret_store_delete(const char* service, const char* account,
                                      char* message, size_t message_size) {
    (void)service; (void)account;
    secret_message(message, message_size, "no secret store on this platform");
    return SECRET_STORE_UNSUPPORTED;
}
#endif
