#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct SecurityStateWire SecurityStateWire;

// D11.2.4: bounded opaque role bytes, shared by disk and dedicated verifier IPC.
// This header has no grant, settings or update-management entry point.
enum {
    SECURITY_UPDATE_METADATA_ROLES = 64,
    SECURITY_UPDATE_METADATA_ROLE_BYTES = 129,
    SECURITY_UPDATE_METADATA_ROOT_BYTES = 64 * 1024,
    SECURITY_UPDATE_METADATA_ROLE_LIMIT = 256 * 1024
};
typedef struct SecurityUpdateMetadata {
    char role[SECURITY_UPDATE_METADATA_ROLE_BYTES];
    uint32_t size;
    uint8_t* bytes;
} SecurityUpdateMetadata;
bool security_update_payload_size(const SecurityUpdateMetadata* entries, uint32_t count, size_t* size);
bool security_update_payload_wire(SecurityStateWire* wire, SecurityUpdateMetadata* entries, uint32_t count);
void security_update_payload_free(SecurityUpdateMetadata* entries, uint32_t count);
