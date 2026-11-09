#include "update_wire.h"
#include "state_wire.h"
#include "lib/mem.h"
#include <errno.h>

static const uint8_t update_wire_magic[8] = {'L','M','T','U','F','I','P','1'};
void security_update_wire_free(UpdateWireMessage* message) {
    if (!message) return;
    security_update_payload_free(message->entries, message->count); mem_free(message->bytes);
    memset(message, 0, sizeof(*message));
}
static bool message_shape(const UpdateWireMessage* m) {
    if (!m || strnlen(m->utc, sizeof(m->utc)) == sizeof(m->utc) || strnlen(m->path, sizeof(m->path)) == sizeof(m->path) ||
        m->kind < UPDATE_WIRE_INIT || m->kind > UPDATE_WIRE_STAGE_ACK || m->count > 64 || m->artifact > 1 ||
        m->size > UPDATE_WIRE_BODY_LIMIT || m->limit > UPDATE_WIRE_BODY_LIMIT || (m->count && !m->entries) || (m->size && !m->bytes)) return false;
    bool empty_digest = true;
    for (uint8_t b : m->digest) if (b) empty_digest = false;
    if (m->kind != UPDATE_WIRE_DONE && m->kind != UPDATE_WIRE_STAGE_END && !empty_digest) return false;
    if (m->kind != UPDATE_WIRE_INIT && m->utc[0]) return false;
    if (m->kind != UPDATE_WIRE_INIT && m->kind != UPDATE_WIRE_FETCH && m->kind != UPDATE_WIRE_STAGE_ENTRY && m->path[0]) return false;
    if (m->kind != UPDATE_WIRE_FETCH && m->kind != UPDATE_WIRE_STAGE_ENTRY && m->artifact) return false;
    switch (m->kind) {
    case UPDATE_WIRE_INIT: return m->count && !m->size && !m->status && m->path[0] && strlen(m->utc) == 20 && m->limit && m->limit <= 600000;
    case UPDATE_WIRE_CHECKPOINT: return m->count && !m->size && !m->status && !m->limit;
    case UPDATE_WIRE_FETCH: return !m->count && !m->size && !m->status && m->path[0];
    case UPDATE_WIRE_RESPONSE: return !m->count && !m->limit && (m->status == UPDATE_WIRE_OK || (m->status == UPDATE_WIRE_NOT_FOUND && !m->size));
    case UPDATE_WIRE_DONE: return !m->count && !m->limit && ((m->status == UPDATE_WIRE_UNCHANGED && !m->size && empty_digest) ||
        (m->status == UPDATE_WIRE_VERIFIED && m->size && m->size <= 262144 && !empty_digest));
    case UPDATE_WIRE_READY:
    case UPDATE_WIRE_ERROR: return !m->count && !m->size && !m->limit && !m->status;
    case UPDATE_WIRE_STAGE_INIT: return !m->count && !m->status && m->size && m->limit && m->limit <= 600000;
    case UPDATE_WIRE_STAGE_ENTRY: return !m->count && !m->limit && m->path[0] && m->size <= UPDATE_WIRE_STAGE_MEMBER_LIMIT &&
        m->status <= 1 && (!m->artifact || (!m->size && !m->status));
    case UPDATE_WIRE_STAGE_END: return !m->count && !m->size && m->status <= 4096 && !empty_digest;
    case UPDATE_WIRE_STAGE_ACK: return !m->count && !m->size && !m->limit && m->status == UPDATE_WIRE_OK;
    }
    return false;
}
static bool message_body(SecurityStateWire* wire, UpdateWireMessage* m) {
    security_state_wire_u32(wire, &m->status); security_state_wire_u32(wire, &m->artifact);
    security_state_wire_u64(wire, &m->limit);
    security_state_wire_text(wire, m->path, sizeof(m->path)); security_state_wire_text(wire, m->utc, sizeof(m->utc));
    security_state_wire_u32(wire, &m->count); security_state_wire_u32(wire, &m->size);
    security_state_wire_bytes(wire, m->digest, sizeof(m->digest));
    if (!wire->valid || m->count > 64 || m->size > wire->capacity - wire->offset) return false;
    if (!wire->writing && m->count) {
        m->entries = (SecurityUpdateMetadata*)mem_calloc(m->count, sizeof(*m->entries), MEM_CAT_SYSTEM);
        if (!m->entries) return false;
    }
    if (!security_update_payload_wire(wire, m->entries, m->count) || m->size > wire->capacity - wire->offset) return false;
    if (!wire->writing && m->size) {
        m->bytes = (uint8_t*)mem_alloc(m->size, MEM_CAT_SYSTEM); if (!m->bytes) return false;
    }
    security_state_wire_bytes(wire, m->bytes, m->size);
    return wire->valid && wire->offset == wire->capacity && message_shape(m);
}
static bool wire_transfer(UpdateWire* wire, void* bytes, size_t size, bool sending) {
    if (wire->failed) return false;
    bool eof = false;
    bool success = ipc_stream_transfer_checked(wire->stream, bytes, size, sending, wire->deadline, &eof, wire->check, wire->context);
    if (!success) wire->failed = true;
    return success;
}
bool security_update_wire_send(UpdateWire* wire, UpdateWireMessage* m) {
    if (!wire || wire->failed || !wire->stream || !wire->session || wire->sent == UINT64_MAX || !message_shape(m)) { errno = EINVAL; return false; }
    size_t rows = 0;
    if (!security_update_payload_size(m->entries, m->count, &rows) || rows > UPDATE_WIRE_RECORD_LIMIT ||
        rows + m->size > UPDATE_WIRE_BODY_LIMIT) return false;
    size_t size = UPDATE_WIRE_FIXED_BYTES + rows + m->size;
    uint8_t* bytes = (uint8_t*)mem_calloc(1, size, MEM_CAT_SYSTEM); if (!bytes) return false;
    SecurityStateWire body = {bytes, size, 0, true, true};
    bool valid = message_body(&body, m);
    uint8_t header[36] = {}; SecurityStateWire h = {header, sizeof(header), 0, true, true};
    uint8_t magic[8]; memcpy(magic, update_wire_magic, 8); uint32_t version = 1, kind = m->kind, length = (uint32_t)size;
    uint64_t sequence = wire->sent + 1;
    security_state_wire_bytes(&h, magic, 8); security_state_wire_u32(&h, &version); security_state_wire_u32(&h, &kind);
    security_state_wire_u64(&h, &wire->session); security_state_wire_u64(&h, &sequence); security_state_wire_u32(&h, &length);
    valid = valid && h.valid && wire_transfer(wire, header, sizeof(header), true) && wire_transfer(wire, bytes, size, true);
    mem_free(bytes); if (valid) wire->sent = sequence; else wire->failed = true; return valid;
}
bool security_update_wire_receive(UpdateWire* wire, uint32_t allowed, UpdateWireMessage* m) {
    if (!wire || wire->failed || !wire->stream || !wire->session || !m || wire->received == UINT64_MAX) return false;
    memset(m, 0, sizeof(*m)); uint8_t header[36], magic[8];
    if (!wire_transfer(wire, header, sizeof(header), false)) return false;
    SecurityStateWire h = {header, sizeof(header), 0, false, true};
    uint32_t version = 0, kind = 0, length = 0; uint64_t session = 0, sequence = 0;
    security_state_wire_bytes(&h, magic, 8); security_state_wire_u32(&h, &version); security_state_wire_u32(&h, &kind);
    security_state_wire_u64(&h, &session); security_state_wire_u64(&h, &sequence); security_state_wire_u32(&h, &length);
    // Validate tags and lengths before allocation, including per-operation limits.
    uint32_t cap = kind == UPDATE_WIRE_RESPONSE || kind == UPDATE_WIRE_STAGE_INIT ? UPDATE_WIRE_FRAME_LIMIT :
        kind == UPDATE_WIRE_STAGE_ENTRY ? UPDATE_WIRE_FIXED_BYTES + UPDATE_WIRE_STAGE_MEMBER_LIMIT :
        kind == UPDATE_WIRE_READY || kind == UPDATE_WIRE_ERROR || kind == UPDATE_WIRE_STAGE_END || kind == UPDATE_WIRE_STAGE_ACK ? UPDATE_WIRE_FIXED_BYTES :
        UPDATE_WIRE_FIXED_BYTES + UPDATE_WIRE_RECORD_LIMIT;
    if (!h.valid || memcmp(magic, update_wire_magic, 8) || version != 1 || session != wire->session ||
        sequence != wire->received + 1 || kind < UPDATE_WIRE_INIT || kind > UPDATE_WIRE_STAGE_ACK || !(allowed & (1u << kind)) ||
        length < UPDATE_WIRE_FIXED_BYTES || length > cap) { wire->failed = true; errno = EPROTO; return false; }
    uint8_t* bytes = (uint8_t*)mem_alloc(length, MEM_CAT_SYSTEM); if (!bytes) return false;
    m->kind = (UpdateWireKind)kind;
    bool valid = wire_transfer(wire, bytes, length, false);
    SecurityStateWire body = {bytes, length, 0, false, true};
    valid = valid && message_body(&body, m); mem_free(bytes);
    if (valid) wire->received = sequence; else { wire->failed = true; security_update_wire_free(m); errno = EPROTO; }
    return valid;
}
