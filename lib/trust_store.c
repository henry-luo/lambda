/**
 * @file trust_store.c
 * @brief OS trust anchors as a shared mbedTLS chain. See trust_store.h.
 */

#include "trust_store.h"
#include "log.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/x509_crt.h>

typedef struct TrustChain { mbedtls_x509_crt roots; int count; } TrustChain;
static TrustChain platform_chain, override_chain;
static TrustChain* default_chain;
static pthread_once_t trust_once = PTHREAD_ONCE_INIT;
static pthread_once_t platform_once = PTHREAD_ONCE_INIT;

/** add one DER certificate; unparseable entries are skipped, not fatal */
static void trust_add_der(TrustChain* chain, const unsigned char* der, size_t len) {
    if (mbedtls_x509_crt_parse_der(&chain->roots, der, len) == 0) chain->count++;
}

/** add every certificate of a PEM bundle; returns how many were added */
static int trust_add_pem_file(TrustChain* chain, const char* path) {
    int before = chain->count;
    // a positive return counts certificates that failed to parse; keep the rest
    int rc = mbedtls_x509_crt_parse_file(&chain->roots, path);
    if (rc < 0) return 0;
    int parsed = 0;
    for (const mbedtls_x509_crt* c = &chain->roots; c && c->raw.len; c = c->next) parsed++;
    chain->count = parsed;
    return chain->count - before;
}

#if defined(__APPLE__)

#include <Security/Security.h>

typedef enum { TRUST_UNSPECIFIED = 0, TRUST_ROOT, TRUST_DENY } TrustVerdict;

/** whether a trust-settings entry applies to TLS server authentication */
static bool trust_entry_is_ssl(CFDictionaryRef entry) {
    SecPolicyRef policy = (SecPolicyRef)CFDictionaryGetValue(entry, kSecTrustSettingsPolicy);
    if (!policy) return true;       // no policy constraint: applies to everything
    CFDictionaryRef props = SecPolicyCopyProperties(policy);
    if (!props) return false;
    CFTypeRef oid = CFDictionaryGetValue(props, kSecPolicyOid);
    bool ssl = oid && CFEqual(oid, kSecPolicyAppleSSL);
    CFRelease(props);
    return ssl;
}

/** the verdict a domain's trust settings give a certificate for TLS */
static TrustVerdict trust_verdict(SecCertificateRef cert, SecTrustSettingsDomain domain) {
    CFArrayRef settings = NULL;
    if (SecTrustSettingsCopyTrustSettings(cert, domain, &settings) != errSecSuccess || !settings) {
        return TRUST_UNSPECIFIED;
    }
    // an empty settings array means "always trust as a root"
    TrustVerdict verdict = CFArrayGetCount(settings) == 0 ? TRUST_ROOT : TRUST_UNSPECIFIED;
    for (CFIndex i = 0; i < CFArrayGetCount(settings); i++) {
        CFDictionaryRef entry = (CFDictionaryRef)CFArrayGetValueAtIndex(settings, i);
        if (!trust_entry_is_ssl(entry)) continue;
        SInt32 result = kSecTrustSettingsResultTrustRoot;   // the default when absent
        CFNumberRef number = (CFNumberRef)CFDictionaryGetValue(entry, kSecTrustSettingsResult);
        if (number) CFNumberGetValue(number, kCFNumberSInt32Type, &result);
        if (result == kSecTrustSettingsResultDeny) {
            verdict = TRUST_DENY;
            break;
        }
        if (result == kSecTrustSettingsResultTrustRoot || result == kSecTrustSettingsResultTrustAsRoot) {
            verdict = TRUST_ROOT;
        }
    }
    CFRelease(settings);
    return verdict;
}

/**
 * System, then admin, then user domain: a later domain's explicit verdict
 * replaces an earlier one, so an admin- or user-installed root is trusted and
 * a root they mark Deny is dropped (the same order the OS evaluates).
 */
static void trust_load_platform(TrustChain* chain) {
    static const SecTrustSettingsDomain domains[] = {
        kSecTrustSettingsDomainSystem, kSecTrustSettingsDomainAdmin, kSecTrustSettingsDomainUser,
    };
    CFMutableDictionaryRef verdicts = CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    for (size_t d = 0; d < sizeof(domains) / sizeof(domains[0]); d++) {
        CFArrayRef certs = NULL;
        if (SecTrustSettingsCopyCertificates(domains[d], &certs) != errSecSuccess || !certs) continue;
        for (CFIndex i = 0; i < CFArrayGetCount(certs); i++) {
            SecCertificateRef cert = (SecCertificateRef)CFArrayGetValueAtIndex(certs, i);
            TrustVerdict verdict = trust_verdict(cert, domains[d]);
            // every certificate in the system domain is an Apple-shipped root
            if (verdict == TRUST_UNSPECIFIED && domains[d] == kSecTrustSettingsDomainSystem) {
                verdict = TRUST_ROOT;
            }
            if (verdict == TRUST_UNSPECIFIED) continue;
            CFDataRef der = SecCertificateCopyData(cert);
            if (!der) continue;
            int value = (int)verdict;
            CFNumberRef number = CFNumberCreate(NULL, kCFNumberIntType, &value);
            CFDictionarySetValue(verdicts, der, number);
            CFRelease(number);
            CFRelease(der);
        }
        CFRelease(certs);
    }
    CFIndex count = CFDictionaryGetCount(verdicts);
    const void** keys = (const void**)calloc((size_t)(count ? count : 1), sizeof(void*));
    const void** values = (const void**)calloc((size_t)(count ? count : 1), sizeof(void*));
    CFDictionaryGetKeysAndValues(verdicts, keys, values);
    for (CFIndex i = 0; i < count; i++) {
        int verdict = 0;
        CFNumberGetValue((CFNumberRef)values[i], kCFNumberIntType, &verdict);
        if (verdict != TRUST_ROOT) continue;
        CFDataRef der = (CFDataRef)keys[i];
        trust_add_der(chain, CFDataGetBytePtr(der), (size_t)CFDataGetLength(der));
    }
    free(keys);
    free(values);
    CFRelease(verdicts);
    log_info("trust-store: %d roots from the macOS keychain", chain->count);
}

#elif defined(_WIN32)

#include <windows.h>
#include <wincrypt.h>

static void trust_load_platform(TrustChain* chain) {
    HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
    if (!store) {
        log_error("trust-store: cannot open the Windows ROOT store");
        return;
    }
    PCCERT_CONTEXT cert = NULL;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != NULL) {
        trust_add_der(chain, cert->pbCertEncoded, cert->cbCertEncoded);
    }
    CertCloseStore(store, 0);
    log_info("trust-store: %d roots from the Windows ROOT store", chain->count);
}

#else

static void trust_load_platform(TrustChain* chain) {
    // the bundle each distribution family maintains from its CA store
    static const char* bundles[] = {
        "/etc/ssl/certs/ca-certificates.crt",               // Debian, Ubuntu, Arch, Alpine
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", // Fedora, RHEL
        "/etc/pki/tls/certs/ca-bundle.crt",                 // older RHEL, CentOS
        "/etc/ssl/ca-bundle.pem",                           // openSUSE
        "/etc/ssl/cert.pem",                                // Alpine, BSDs
    };
    for (size_t i = 0; i < sizeof(bundles) / sizeof(bundles[0]); i++) {
        if (trust_add_pem_file(chain, bundles[i]) > 0) {
            log_info("trust-store: %d roots from %s", chain->count, bundles[i]);
            return;
        }
    }
    log_error("trust-store: no CA bundle found; set SSL_CERT_FILE");
}

#endif

static void trust_load_system_once(void) {
    mbedtls_x509_crt_init(&platform_chain.roots);
    trust_load_platform(&platform_chain);
}
static void trust_load_once(void) {
    const char* override = getenv("SSL_CERT_FILE");
    if (override && *override) {
        mbedtls_x509_crt_init(&override_chain.roots);
        trust_add_pem_file(&override_chain, override);
        default_chain = &override_chain;
        log_info("trust-store: %d roots from SSL_CERT_FILE %s", override_chain.count, override);
    } else {
        pthread_once(&platform_once, trust_load_system_once);
        default_chain = &platform_chain;
    }
}

struct mbedtls_x509_crt* trust_store_roots(void) {
    pthread_once(&trust_once, trust_load_once);
    return default_chain->count > 0 ? &default_chain->roots : NULL;
}
struct mbedtls_x509_crt* trust_store_system_roots(void) {
    pthread_once(&platform_once, trust_load_system_once);
    return platform_chain.count > 0 ? &platform_chain.roots : NULL;
}
int trust_store_root_count(void) {
    pthread_once(&trust_once, trust_load_once);
    return default_chain->count;
}
