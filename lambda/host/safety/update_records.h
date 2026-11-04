#pragma once
#include "update_controls.h"
#include "state_store.h"

// Internal trusted-host checkpoint seam. Exactly one of control or job is
// supplied; installer initialization may operate while offline/paused. A job
// checkpoint rechecks its live policy. The returned lease holds the existing
// settings lock for one bounded record operation, never network/crypto/UI.
SecurityUpdateLease* security_update_record_enter(SecurityUpdateControl* control,
    const char* package, SecurityUpdateJob* job, SecurityUpdatePackageSpec* out_spec,
    uint64_t* out_time, SecurityStateTransaction** out_state, SecurityUpdateResult* result);
// Public authority only; scheduling ceilings/preferences are deliberately absent.
bool security_update_authority_digest(const SecurityUpdatePackageSpec* spec, uint8_t out[32]);
// Captures one live reservation, not merely its package or a reusable pointer.
bool security_update_job_binding(SecurityUpdateJob* job, uint8_t out[32]);
// Short local host storage operation. Exactly one of control/job is supplied;
// a job checks live policy, management remains available while paused/offline.
SecurityUpdateLease* security_update_storage_enter(SecurityUpdateControl* control, SecurityUpdateJob* job,
    SecurityStateTransaction** out_state, SecurityUpdateResult* result);
// A failed checkpoint cannot be repaired underneath an already captured ticket.
void security_update_record_reject(SecurityUpdateLease* lease);
// Volatile controller watermark: observed rollback or same-generation byte
// replacement invalidates this controller. Protected disk remains the restart
// authority; this is not a hardware monotonic counter or backup-recovery API.
bool security_update_record_observe(SecurityUpdateLease* lease, uint64_t generation, const uint8_t digest[32]);
