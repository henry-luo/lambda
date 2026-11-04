#pragma once
#include "update_controls.h"
#include "update_payload.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct SecurityUpdateMetadataSnapshot {
    uint64_t generation;
    uint64_t observed_time;
    uint32_t count;
    SecurityUpdateMetadata* entries;
} SecurityUpdateMetadataSnapshot;

// D7.5.7-D7.5.8: trusted installer/verifier only, never worker ABI. This store
// does NOT verify signatures or grant a target execution/installation right.
// Initial root bytes must match the admitted out-of-band bootstrap fingerprint.
// Missing state requires this explicit create-only installer action; no fetch,
// restore, or corruption path silently initializes or replaces it.
SecurityUpdateResult security_update_metadata_initialize(SecurityUpdateControl* control,
    const char* package, const void* root, size_t size);
SecurityUpdateResult security_update_metadata_read(SecurityUpdateJob* job, SecurityUpdateMetadataSnapshot* out);
const SecurityUpdateMetadata* security_update_metadata_find(const SecurityUpdateMetadataSnapshot* snapshot, const char* role);
void security_update_metadata_free(SecurityUpdateMetadataSnapshot* snapshot);
// Signature/freshness/version decisions belong to Lambda Script. It
// calls this only after verifying a metadata transition. The expected generation
// and live-ticket lease serialize local checkpoints with user policy changes.
// Replace the entire current role set atomically: root-key rotation can discard
// timestamp and snapshot in the same checkpoint as the new root. Version fields
// remain opaque signed bytes, including versions larger than a machine integer.
SecurityUpdateResult security_update_metadata_replace(SecurityUpdateJob* job, uint64_t expected_generation,
    const SecurityUpdateMetadata* entries, uint32_t count, uint64_t* out_generation);
// Persist the trusted host clock before starting a refresh, even if no metadata
// changes. Clock rollback, bad/missing state or stale generation refuse the job.
SecurityUpdateResult security_update_metadata_observe(SecurityUpdateJob* job,
    uint64_t expected_generation, uint64_t* out_generation);
#ifdef __cplusplus
}
#endif
