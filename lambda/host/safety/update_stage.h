#pragma once
#include "update_refresh.h"
#include "update_process.h"

typedef struct SecurityStagedUpdate SecurityStagedUpdate;
struct SecurityUpdateStageInfo {
    uint8_t id[16], artifact[32], manifest[32];
    uint32_t entries;
    uint64_t bytes;
    bool ready;
};
struct SecurityUpdateStageConfig { ConfinedCodeSpec decoder; ConfinedProcessLimits limits; };
struct SecurityUpdateStageReport {
    SecurityUpdateResult result;
    SecurityUpdateStageInfo info;
    bool created;
    SecurityUpdateProcessReport process;
    SecurityStagedUpdate* staged;
};
// D11.2.4: only a live, opaque verified artifact is accepted. No arbitrary byte,
// destination, install hook, active image or app-worker entrypoint is exposed.
bool security_update_stage(const SecurityUpdateStageConfig* config, SecurityUpdateJob* job,
    SecurityVerifiedUpdate* artifact, SecurityUpdateStageReport* out);
bool security_update_staged_info(SecurityStagedUpdate* stage, SecurityUpdateJob* job, SecurityUpdateStageInfo* out);
// Closes the claim/handles after the decoder joins; inactive data remains for
// identity-bound recovery. This never deletes retained app-private data.
void security_update_stage_free(SecurityUpdateStageReport* report);
// Trusted management, available offline/paused. One pending stage per profile.
// Inspection refuses live claims. Cleanup requires the inspected random ID,
// removes at most 64 objects per step, and never selects a newer stage.
SecurityUpdateResult security_update_stage_inspect(SecurityUpdateControl* control, SecurityUpdateStageInfo* out);
SecurityUpdateResult security_update_stage_discard(SecurityUpdateControl* control, const uint8_t id[16],
    uint32_t max_removals, bool* complete);
bool security_update_stage_path_allowed(const char* path);
#ifdef LAMBDA_SECURITY_UPDATE_TESTING
// Mechanism/composition control only; absent from production app/host surfaces.
bool security_update_stage_fixture(int descriptor, uint64_t session, SecurityUpdateJob* job,
    SecurityVerifiedUpdate* artifact, SecurityUpdateStageReport* out);
SecurityVerifiedUpdate* security_update_verified_fixture(SecurityUpdateJob* job, const uint8_t* bytes, size_t size);
void security_update_verified_fixture_free(SecurityVerifiedUpdate* artifact);
#endif
