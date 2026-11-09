#include "update_payload.h"
#include "state_wire.h"
#include "lib/https_url.h"
#include "lib/mem.h"
#include <errno.h>

static bool payload_entry_valid(const SecurityUpdateMetadata* entry, uint32_t index) {
    return https_url_opaque_name_valid(entry->role, sizeof(entry->role)) &&
        (index || !strcmp(entry->role, "root")) && entry->size &&
        entry->size <= (!strcmp(entry->role, "root") ? SECURITY_UPDATE_METADATA_ROOT_BYTES : SECURITY_UPDATE_METADATA_ROLE_LIMIT);
}
bool security_update_payload_size(const SecurityUpdateMetadata* entries, uint32_t count, size_t* size) {
    if (!size || count > SECURITY_UPDATE_METADATA_ROLES || (count && !entries)) return false;
    *size = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!entries[i].bytes || !payload_entry_valid(&entries[i], i)) return false;
        for (uint32_t j = 0; j < i; j++) if (!strcmp(entries[j].role, entries[i].role)) return false;
        *size += SECURITY_UPDATE_METADATA_ROLE_BYTES + 4 + entries[i].size;
    }
    return true;
}
bool security_update_payload_wire(SecurityStateWire* wire, SecurityUpdateMetadata* entries, uint32_t count) {
    if (!wire || count > SECURITY_UPDATE_METADATA_ROLES || (count && !entries)) { errno = EINVAL; return false; }
    for (uint32_t i = 0; i < count; i++) {
        SecurityUpdateMetadata* entry = &entries[i];
        char role[SECURITY_UPDATE_METADATA_ROLE_BYTES] = {};
        if (wire->writing) {
            if (strnlen(entry->role, sizeof(entry->role)) == sizeof(entry->role)) { errno = EINVAL; return false; }
            strcpy(role, entry->role);
        }
        security_state_wire_text(wire, role, sizeof(role));
        if (!wire->writing && wire->valid) memcpy(entry->role, role, sizeof(role));
        security_state_wire_u32(wire, &entry->size);
        if (!wire->valid || !payload_entry_valid(entry, i) || entry->size > wire->capacity - wire->offset) { errno = EINVAL; return false; }
        for (uint32_t j = 0; j < i; j++) if (!strcmp(entries[j].role, entry->role)) { errno = EINVAL; return false; }
        if (!wire->writing) {
            entry->bytes = (uint8_t*)mem_alloc(entry->size, MEM_CAT_SYSTEM);
            if (!entry->bytes) { errno = ENOMEM; return false; }
        } else if (!entry->bytes) { errno = EINVAL; return false; }
        security_state_wire_bytes(wire, entry->bytes, entry->size);
    }
    return wire->valid;
}
void security_update_payload_free(SecurityUpdateMetadata* entries, uint32_t count) {
    if (!entries) return;
    for (uint32_t i = 0; i < count; i++) mem_free(entries[i].bytes);
    mem_free(entries);
}
