#include "update_metadata.h"
#include "update_records.h"
#include "state_wire.h"
#include "../../../lib/digest.h"
#include "../../../lib/https_url.h"
#include "../../../lib/mem.h"
#include <errno.h>

static const uint8_t metadata_magic[8] = {'L','M','T','U','F','C','0','1'};
enum { METADATA_HEADER_BYTES = 104, METADATA_CHECKSUM_BYTES = 32 };
static SecurityUpdateResult metadata_store_result(SecurityStoreResult result) {
    return result <= SECURITY_STORE_UNAVAILABLE ? (SecurityUpdateResult)result : SECURITY_UPDATE_IO_ERROR;
}
static bool metadata_role_valid(const char* role) {
    return https_url_opaque_name_valid(role, SECURITY_UPDATE_METADATA_ROLE_BYTES);
}
static bool metadata_value_valid(const char* role, size_t size) {
    return metadata_role_valid(role) && size &&
        size <= (!strcmp(role, "root") ? SECURITY_UPDATE_METADATA_ROOT_BYTES : SECURITY_UPDATE_METADATA_ROLE_LIMIT);
}
void security_update_metadata_free(SecurityUpdateMetadataSnapshot* snapshot) {
    if (!snapshot) return;
    security_update_payload_free(snapshot->entries, snapshot->count); memset(snapshot, 0, sizeof(*snapshot));
}
const SecurityUpdateMetadata* security_update_metadata_find(const SecurityUpdateMetadataSnapshot* snapshot, const char* role) {
    if (!snapshot || !role) return nullptr;
    for (uint32_t i = 0; i < snapshot->count; i++) if (!strcmp(snapshot->entries[i].role, role)) return &snapshot->entries[i];
    return nullptr;
}
static bool metadata_time_valid(uint64_t time) {
    // The verifier's RFC3339 UTC domain is 1970..9999; reject clock errors, not
    // an accidental unsigned conversion of time() == -1 or unbounded wire input.
    return time && time <= 253402300799ULL;
}
static bool metadata_size(const SecurityUpdateMetadataSnapshot* snapshot, size_t* out) {
    if (!snapshot || !snapshot->count || snapshot->count > SECURITY_UPDATE_METADATA_ROLES || !snapshot->entries) return false;
    size_t size = 0;
    if (!security_update_payload_size(snapshot->entries, snapshot->count, &size) ||
        size > SECURITY_STORE_MAX_BYTES - METADATA_HEADER_BYTES - METADATA_CHECKSUM_BYTES) return false;
    *out = size + METADATA_HEADER_BYTES + METADATA_CHECKSUM_BYTES; return true;
}
static SecurityUpdateResult metadata_decode(char* bytes, size_t size, const uint8_t profile[32],
        const uint8_t authority[32], SecurityUpdateMetadataSnapshot* out) {
    if (size < METADATA_HEADER_BYTES + METADATA_CHECKSUM_BYTES || size > SECURITY_STORE_MAX_BYTES) return SECURITY_UPDATE_CORRUPT;
    uint8_t digest[32];
    if (!digest_sha256(bytes, size - METADATA_CHECKSUM_BYTES, digest) ||
            memcmp(bytes + size - METADATA_CHECKSUM_BYTES, digest, 32)) return SECURITY_UPDATE_CORRUPT;
    SecurityStateWire wire = {(uint8_t*)bytes, size - METADATA_CHECKSUM_BYTES, 0, false, true};
    uint8_t magic[8], saved_profile[32], saved_authority[32]; uint32_t version = 0, reserved = 0, count = 0;
    security_state_wire_bytes(&wire, magic, 8); security_state_wire_u32(&wire, &version); security_state_wire_u32(&wire, &reserved);
    security_state_wire_bytes(&wire, saved_profile, 32); security_state_wire_bytes(&wire, saved_authority, 32);
    security_state_wire_u64(&wire, &out->generation); security_state_wire_u64(&wire, &out->observed_time);
    security_state_wire_u32(&wire, &count);
    if (!wire.valid || memcmp(magic, metadata_magic, 8) || version != 1 || reserved || !out->generation ||
            !metadata_time_valid(out->observed_time) || !count || count > SECURITY_UPDATE_METADATA_ROLES) return SECURITY_UPDATE_CORRUPT;
    security_state_wire_u32(&wire, &reserved);
    if (reserved || !wire.valid) return SECURITY_UPDATE_CORRUPT;
    if (memcmp(saved_profile, profile, 32) || memcmp(saved_authority, authority, 32)) return SECURITY_UPDATE_IDENTITY_CHANGED;
    out->entries = (SecurityUpdateMetadata*)mem_calloc(SECURITY_UPDATE_METADATA_ROLES, sizeof(SecurityUpdateMetadata), MEM_CAT_SYSTEM);
    if (!out->entries) return SECURITY_UPDATE_IO_ERROR;
    out->count = count;
    if (!security_update_payload_wire(&wire, out->entries, count)) return errno == ENOMEM ? SECURITY_UPDATE_IO_ERROR : SECURITY_UPDATE_CORRUPT;
    return wire.valid && wire.offset == wire.capacity ? SECURITY_UPDATE_OK : SECURITY_UPDATE_CORRUPT;
}
static SecurityUpdateResult metadata_encode(SecurityUpdateLease* lease, SecurityStateTransaction* state, SecurityUpdateMetadataSnapshot* snapshot,
        const uint8_t profile[32], const uint8_t authority[32]) {
    size_t size = 0;
    if (!metadata_size(snapshot, &size)) return SECURITY_UPDATE_INVALID;
    uint8_t* bytes = (uint8_t*)mem_calloc(1, size, MEM_CAT_SYSTEM);
    if (!bytes) return SECURITY_UPDATE_IO_ERROR;
    SecurityStateWire wire = {bytes, size - METADATA_CHECKSUM_BYTES, 0, true, true};
    uint8_t magic[8]; memcpy(magic, metadata_magic, 8); uint32_t version = 1, reserved = 0;
    security_state_wire_bytes(&wire, magic, 8); security_state_wire_u32(&wire, &version); security_state_wire_u32(&wire, &reserved);
    uint8_t binding[64]; memcpy(binding, profile, 32); memcpy(binding + 32, authority, 32);
    security_state_wire_bytes(&wire, binding, sizeof(binding));
    security_state_wire_u64(&wire, &snapshot->generation); security_state_wire_u64(&wire, &snapshot->observed_time);
    security_state_wire_u32(&wire, &snapshot->count); security_state_wire_u32(&wire, &reserved);
    if (!security_update_payload_wire(&wire, snapshot->entries, snapshot->count)) wire.valid = false;
    SecurityUpdateResult result = SECURITY_UPDATE_IO_ERROR;
    if (wire.valid && wire.offset == wire.capacity && digest_sha256(bytes, wire.capacity, bytes + wire.capacity)) {
        // Reject observed-history resets before touching disk. Failed publication
        // may conservatively retain this watermark; it must never permit replay.
        result = security_update_record_observe(lease, snapshot->generation, bytes + wire.capacity) ?
            metadata_store_result(security_state_publish(state, bytes, size)) : SECURITY_UPDATE_IDENTITY_CHANGED;
    }
    mem_free(bytes); return result;
}

struct MetadataOperation {
    SecurityUpdateLease* lease = nullptr;
    SecurityStateTransaction* state = nullptr;
    SecurityUpdatePackageSpec spec = {};
    SecurityUpdateMetadataSnapshot snapshot = {};
    SecurityUpdateResult result = SECURITY_UPDATE_INVALID;
    uint64_t now = 0;
    uint8_t profile[32] = {}, authority[32] = {};
    MetadataOperation(SecurityUpdateControl* control, const char* package, SecurityUpdateJob* job) {
        lease = security_update_record_enter(control, package, job, &spec, &now, &state, &result);
        if (!lease) return;
        if (!metadata_time_valid(now) || !security_state_profile(state, profile) || !security_update_authority_digest(&spec, authority)) {
            result = SECURITY_UPDATE_IO_ERROR; security_update_record_reject(lease); return;
        }
        char* bytes = nullptr; size_t size = 0;
        result = metadata_store_result(security_state_read(state, &bytes, &size));
        if (result == SECURITY_UPDATE_OK) {
            result = metadata_decode(bytes, size, profile, authority, &snapshot);
            if (result == SECURITY_UPDATE_OK && !security_update_record_observe(lease, snapshot.generation,
                    (const uint8_t*)bytes + size - METADATA_CHECKSUM_BYTES)) result = SECURITY_UPDATE_IDENTITY_CHANGED;
        }
        mem_free(bytes);
        if (result == SECURITY_UPDATE_OK && now < snapshot.observed_time) result = SECURITY_UPDATE_REVOKED;
        if (job && result != SECURITY_UPDATE_OK) security_update_record_reject(lease);
    }
    ~MetadataOperation() { security_update_metadata_free(&snapshot); security_update_job_leave(lease); }
};
SecurityUpdateResult security_update_metadata_initialize(SecurityUpdateControl* control,
        const char* package, const void* root, size_t size) {
    if (!root || !metadata_value_valid("root", size)) return SECURITY_UPDATE_INVALID;
    // Fingerprint and publication must use the same owned bytes.
    uint8_t* copy = (uint8_t*)mem_dup_n((const char*)root, size, MEM_CAT_SYSTEM);
    if (!copy) return SECURITY_UPDATE_IO_ERROR;
    struct RootCopy { uint8_t* bytes; ~RootCopy() { mem_free(bytes); } } owned = {copy};
    uint8_t digest[32]; if (!digest_sha256(copy, size, digest)) return SECURITY_UPDATE_IO_ERROR;
    MetadataOperation operation(control, package, nullptr);
    if (operation.result != SECURITY_UPDATE_MISSING) return operation.result == SECURITY_UPDATE_OK ? SECURITY_UPDATE_CONFLICT : operation.result;
    if (memcmp(digest, operation.spec.root, 32)) return SECURITY_UPDATE_IDENTITY_CHANGED;
    SecurityUpdateMetadata entry = {}; strcpy(entry.role, "root"); entry.size = (uint32_t)size;
    entry.bytes = copy;
    SecurityUpdateMetadataSnapshot snapshot = {1, operation.now, 1, &entry};
    return metadata_encode(operation.lease, operation.state, &snapshot, operation.profile, operation.authority);
}
SecurityUpdateResult security_update_metadata_read(SecurityUpdateJob* job, SecurityUpdateMetadataSnapshot* out) {
    if (!out) return SECURITY_UPDATE_INVALID;
    memset(out, 0, sizeof(*out)); MetadataOperation operation(nullptr, nullptr, job);
    if (operation.result == SECURITY_UPDATE_OK) { *out = operation.snapshot; memset(&operation.snapshot, 0, sizeof(operation.snapshot)); }
    return operation.result;
}
static SecurityUpdateResult metadata_checkpoint(SecurityUpdateJob* job, uint64_t expected,
        const SecurityUpdateMetadata* entries, uint32_t count, uint64_t* out_generation) {
    if (out_generation) *out_generation = 0;
    if (!out_generation || (entries && (!count || count > SECURITY_UPDATE_METADATA_ROLES)) || (!entries && count))
        return SECURITY_UPDATE_INVALID;
    MetadataOperation operation(nullptr, nullptr, job);
    if (operation.result != SECURITY_UPDATE_OK) return operation.result;
    SecurityUpdateMetadataSnapshot* snapshot = &operation.snapshot;
    if (snapshot->generation != expected) return SECURITY_UPDATE_CONFLICT;
    if (snapshot->generation == UINT64_MAX) return SECURITY_UPDATE_INVALID;
    if (entries) {
        SecurityUpdateMetadataSnapshot replacement = {0, 0, count, (SecurityUpdateMetadata*)entries}; size_t size = 0;
        if (!metadata_size(&replacement, &size)) return SECURITY_UPDATE_INVALID;
        SecurityUpdateMetadata* copies = (SecurityUpdateMetadata*)mem_calloc(count, sizeof(*copies), MEM_CAT_SYSTEM);
        if (!copies) return SECURITY_UPDATE_IO_ERROR;
        replacement.entries = copies;
        for (uint32_t i = 0; i < count; i++) {
            strcpy(copies[i].role, entries[i].role); copies[i].size = entries[i].size;
            copies[i].bytes = (uint8_t*)mem_dup_n((const char*)entries[i].bytes, entries[i].size, MEM_CAT_SYSTEM);
            if (!copies[i].bytes) { security_update_metadata_free(&replacement); return SECURITY_UPDATE_IO_ERROR; }
        }
        uint64_t generation = snapshot->generation;
        security_update_metadata_free(snapshot); *snapshot = replacement; snapshot->generation = generation;
    }
    ++snapshot->generation; snapshot->observed_time = operation.now;
    SecurityUpdateResult result = metadata_encode(operation.lease, operation.state, snapshot, operation.profile, operation.authority);
    if (result == SECURITY_UPDATE_OK) *out_generation = snapshot->generation;
    else if (result != SECURITY_UPDATE_INVALID) security_update_record_reject(operation.lease);
    return result;
}
SecurityUpdateResult security_update_metadata_replace(SecurityUpdateJob* job, uint64_t expected_generation,
        const SecurityUpdateMetadata* entries, uint32_t count, uint64_t* out_generation) {
    if (!entries) return SECURITY_UPDATE_INVALID;
    return metadata_checkpoint(job, expected_generation, entries, count, out_generation);
}
SecurityUpdateResult security_update_metadata_observe(SecurityUpdateJob* job, uint64_t expected_generation, uint64_t* out_generation) {
    return metadata_checkpoint(job, expected_generation, nullptr, 0, out_generation);
}
