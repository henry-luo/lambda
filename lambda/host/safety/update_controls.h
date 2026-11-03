#pragma once
#include "grant_store.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { SECURITY_UPDATE_MAX_PACKAGES = 128, SECURITY_UPDATE_MAX_ORIGINS = 8 };
typedef enum SecurityUpdateMode {
    SECURITY_UPDATE_ENABLED = 1,
    SECURITY_UPDATE_PAUSED = 2,
    SECURITY_UPDATE_DISABLED = 3
} SecurityUpdateMode;
typedef enum SecurityUpdateResult {
    SECURITY_UPDATE_OK, SECURITY_UPDATE_MISSING, SECURITY_UPDATE_CONFLICT,
    SECURITY_UPDATE_IDENTITY_CHANGED, SECURITY_UPDATE_CORRUPT,
    SECURITY_UPDATE_UNSAFE_PATH, SECURITY_UPDATE_UNSUPPORTED,
    SECURITY_UPDATE_IO_ERROR, SECURITY_UPDATE_INVALID, SECURITY_UPDATE_UNAVAILABLE,
    SECURITY_UPDATE_OFFLINE, SECURITY_UPDATE_PAUSE_ACTIVE, SECURITY_UPDATE_DISABLE_ACTIVE,
    SECURITY_UPDATE_NOT_DUE, SECURITY_UPDATE_BUSY, SECURITY_UPDATE_REVOKED,
    SECURITY_UPDATE_CLOSED
} SecurityUpdateResult;
typedef struct SecurityUpdatePackageSpec {
    char package[129]; // public package identity; never a user pathname
    char metadata_base[1025]; // canonical public HTTPS directory, without query/credentials
    char channel[33];
    uint32_t origin_count;
    char origins[SECURITY_UPDATE_MAX_ORIGINS][257]; // exact canonical HTTPS origins
    uint8_t publisher[32]; // trusted installer/verifier identity binding
    uint8_t root[32]; // fingerprint of its out-of-band trusted root metadata
    uint32_t poll_seconds; // host-owned: 300..604800
    uint32_t job_seconds; // host-owned: 1..600
    uint64_t max_download_bytes; // host ceiling, at most 256 MiB
} SecurityUpdatePackageSpec;
typedef struct SecurityUpdatePackageInfo {
    SecurityUpdatePackageSpec spec;
    SecurityUpdateMode mode;
    uint64_t pause_until; // UTC seconds; zero means indefinite
    uint64_t next_poll;
    uint64_t lease_until;
    uint32_t failures;
} SecurityUpdatePackageInfo;
typedef struct SecurityUpdateSnapshot {
    uint64_t generation;
    uint64_t policy_epoch;
    bool offline;
    SecurityUpdateMode mode;
    uint64_t pause_until;
    uint32_t count;
    SecurityUpdatePackageInfo* packages;
} SecurityUpdateSnapshot;
typedef struct SecurityUpdateControl SecurityUpdateControl;
typedef struct SecurityUpdateJob SecurityUpdateJob;
typedef struct SecurityUpdateLease SecurityUpdateLease;

// D7.5.7-D7.5.8: trusted host only. No management or request-now operation is
// part of the worker channel. These controls do not fetch or authenticate data.
// Missing state requires explicit create-only initialization, never recovery
// from a backup or an environment-provided catalog. Initial catalog is empty.
SecurityUpdateResult security_update_initialize(SecurityGrantStore* store);
SecurityUpdateControl* security_update_open(SecurityGrantStore* store,
    uint64_t (*clock)(void* context), void* context, SecurityUpdateResult* result);
// Cancels this controller's tickets; tickets retain a closed owner until freed.
// Store must outlive open controllers and acquired publication leases.
void security_update_close(SecurityUpdateControl* control);
SecurityUpdateResult security_update_inspect(SecurityUpdateControl* control, SecurityUpdateSnapshot* out);
void security_update_snapshot_free(SecurityUpdateSnapshot* snapshot);
// Installer provides authenticated public configuration/trust identity. Any
// change requires explicit trusted revalidation and retains user preferences.
SecurityUpdateResult security_update_admit(SecurityUpdateControl* control,
    const SecurityUpdatePackageSpec* spec, bool revalidate);
// Canonical public HTTPS URL within the authenticated package's exact origin
// set. This admits a destination, not a network capability or artifact trust.
bool security_update_url_allowed(const SecurityUpdatePackageSpec* spec, const char* url);
SecurityUpdateResult security_update_set_mode(SecurityUpdateControl* control,
    const char* package, SecurityUpdateMode mode, uint64_t pause_until); // NULL = global
SecurityUpdateResult security_update_set_offline(SecurityUpdateControl* control, bool offline);

// Host scheduler only. One persisted, expiring job reservation per package;
// cadence/backoff survive restart. An explicit trusted manual check may bypass
// paused/disabled scheduling, but never offline, and never re-enables schedules.
SecurityUpdateJob* security_update_begin(SecurityUpdateControl* control,
    const char* package, bool manual, SecurityUpdateResult* result);
SecurityUpdateResult security_update_job_check(SecurityUpdateJob* job);
SecurityUpdateResult security_update_job_describe(SecurityUpdateJob* job, SecurityUpdatePackageSpec* out);
SecurityUpdateResult security_update_job_finish(SecurityUpdateJob* job, bool success);
void security_update_job_free(SecurityUpdateJob* job);
// Recheck immediately before one bounded local publication. Serializes with
// acknowledged pause/disable/offline changes across host instances. Do not wait
// for network, extraction, UI, or call another controller method under a lease.
// Release on the same thread that acquired it.
SecurityUpdateLease* security_update_job_enter(SecurityUpdateJob* job, SecurityUpdateResult* result);
void security_update_job_leave(SecurityUpdateLease* lease);
const char* security_update_result_name(SecurityUpdateResult result);

#ifdef __cplusplus
}
#endif
