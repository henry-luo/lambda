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

static mbedtls_x509_crt trust_roots;
static int trust_root_count = 0;
static pthread_once_t trust_once = PTHREAD_ONCE_INIT;

/** add one DER certificate; unparseable entries are skipped, not fatal */
static void trust_add_der(const unsigned char* der, size_t len) {
    if (mbedtls_x509_crt_parse_der(&trust_roots, der, len) == 0) trust_root_count++;
}

/** add every certificate of a PEM bundle; returns how many were added */
static int trust_add_pem_file(const char* path) {
    int before = trust_root_count;
    // a positive return counts certificates that failed to parse; keep the rest
    int rc = mbedtls_x509_crt_parse_file(&trust_roots, path);
    if (rc < 0) return 0;
    int parsed = 0;
    for (const mbedtls_x509_crt* c = &trust_roots; c && c->raw.len; c = c->next) parsed++;
    trust_root_count = parsed;
    return trust_root_count - before;
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
static void trust_load_platform(void) {
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
        trust_add_der(CFDataGetBytePtr(der), (size_t)CFDataGetLength(der));
    }
    free(keys);
    free(values);
    CFRelease(verdicts);
    log_info("trust-store: %d roots from the macOS keychain", trust_root_count);
}

#elif defined(_WIN32)

#include <windows.h>
#include <wincrypt.h>

static void trust_load_platform(void) {
    HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
    if (!store) {
        log_error("trust-store: cannot open the Windows ROOT store");
        return;
    }
    PCCERT_CONTEXT cert = NULL;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != NULL) {
        trust_add_der(cert->pbCertEncoded, cert->cbCertEncoded);
    }
    CertCloseStore(store, 0);
    log_info("trust-store: %d roots from the Windows ROOT store", trust_root_count);
}

#else

static void trust_load_platform(void) {
    // the bundle each distribution family maintains from its CA store
    static const char* bundles[] = {
        "/etc/ssl/certs/ca-certificates.crt",               // Debian, Ubuntu, Arch, Alpine
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", // Fedora, RHEL
        "/etc/pki/tls/certs/ca-bundle.crt",                 // older RHEL, CentOS
        "/etc/ssl/ca-bundle.pem",                           // openSUSE
        "/etc/ssl/cert.pem",                                // Alpine, BSDs
    };
    for (size_t i = 0; i < sizeof(bundles) / sizeof(bundles[0]); i++) {
        if (trust_add_pem_file(bundles[i]) > 0) {
            log_info("trust-store: %d roots from %s", trust_root_count, bundles[i]);
            return;
        }
    }
    log_error("trust-store: no CA bundle found; set SSL_CERT_FILE");
}

#endif

static void trust_load_once(void) {
    mbedtls_x509_crt_init(&trust_roots);
    const char* override = getenv("SSL_CERT_FILE");
    if (override && *override) {
        trust_add_pem_file(override);
        log_info("trust-store: %d roots from SSL_CERT_FILE %s", trust_root_count, override);
        return;
    }
    trust_load_platform();
}

struct mbedtls_x509_crt* trust_store_roots(void) {
    pthread_once(&trust_once, trust_load_once);
    return trust_root_count > 0 ? &trust_roots : NULL;
}

int trust_store_root_count(void) {
    pthread_once(&trust_once, trust_load_once);
    return trust_root_count;
}
