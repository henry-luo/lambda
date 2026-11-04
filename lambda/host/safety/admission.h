#pragma once
#include "file_broker.h"
#include "grant_store.h"
#include "../../io/security_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SecurityAdmission SecurityAdmission;
typedef struct SecurityProtectionSpec {
    const char* directory;
    SecurityOperations denied;
} SecurityProtectionSpec;
typedef struct SecurityAdmissionConfig {
    SecurityGrantStore* store; // trusted host owns this until admission closes
    const char* entry; // absolute, selected by trusted host; never a worker ID
    const char* project; // NULL admits only the entry leaf
    const char* runtime_directory;
    const char* const* shipped_directories;
    uint32_t shipped_count;
    const SecurityProtectionSpec* protections;
    uint32_t protection_count;
    // Only an explicit trusted revalidation may clear an obsolete binding.
    bool revalidate_changed_identity;
    uint64_t (*clock)(void* context);
    void* clock_context;
} SecurityAdmissionConfig;
typedef struct SecurityAdmissionReport {
    SecurityStoreResult store_result;
    uint64_t generation;
    uint32_t restored;
    uint32_t rejected;
} SecurityAdmissionReport;
typedef struct SecurityFileSelection {
    const char* path;
    SecurityOperations operations;
    uint64_t expires_at;
    bool is_directory;
    bool recursive;
    bool persistent;
} SecurityFileSelection;

// D11.2.1-D11.2.3: host-only ad-hoc admission. The host supplies its verified
// installation/catalog, not a manifest from the app. No parsing runs here.
// Source manifests are bounded to .ls imports plus the exact selected entry.
SecurityAdmission* security_admission_open(const SecurityAdmissionConfig* config,
    SecurityAdmissionReport* report);
void security_admission_close(SecurityAdmission* admission);
// Join all borrowed clients/dispatchers first. Revokes this session and reports
// temporary-file cleanup; persistent data/cache and saved grants are retained.
bool security_admission_finish(SecurityAdmission* admission);
uint64_t security_admission_session(const SecurityAdmission* admission);
const char* security_admission_entry(const SecurityAdmission* admission);
bool security_admission_binding(const SecurityAdmission* admission, SecurityGrantBinding* out);
SecurityIOClient security_admission_client(SecurityAdmission* admission);
// Host dispatcher captures authority before the action, then pins that exact
// view through bounded publication. Owner/store outlive dispatch; release on
// the acquiring thread. Default transfer window is 250 ms, never a UI wait.
SecurityChannelDeliveryGuard security_admission_delivery_guard(SecurityAdmission* admission);

// Trusted picker/permission UI only. Initial profile supports existing selected
// files and recursive directories, without source/network/service authority.
uint64_t security_admission_select(SecurityAdmission* admission,
    const SecurityFileSelection* selection, SecurityStoreResult* result);
SecurityStoreResult security_admission_revoke(SecurityAdmission* admission, uint64_t resource);
SecurityStoreResult security_admission_inspect(SecurityAdmission* admission, SecurityGrantSnapshot* out);
bool security_admission_describe(SecurityAdmission* admission, SecurityApprovalRef approval,
    SecurityOverwriteChallenge* out);
SecurityIOResult security_admission_approve(SecurityAdmission* admission, SecurityApprovalRef approval);
bool security_admission_cancel(SecurityAdmission* admission, SecurityApprovalRef approval);

#ifdef __cplusplus
}
#endif
