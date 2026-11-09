#pragma once
#include "lib/endian.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Bounded canonical host-record encoding; no native struct layout or padding.
struct SecurityStateWire { uint8_t* data; size_t capacity, offset; bool writing, valid; };
static inline void security_state_wire_bytes(SecurityStateWire* wire, void* bytes, size_t count) {
    if (!wire->valid || wire->offset > wire->capacity || count > wire->capacity - wire->offset) { wire->valid = false; return; }
    if (!count) return;
    if (wire->writing) memcpy(wire->data + wire->offset, bytes, count);
    else memcpy(bytes, wire->data + wire->offset, count);
    wire->offset += count;
}
static inline void security_state_wire_u32(SecurityStateWire* wire, uint32_t* value) {
    uint8_t bytes[4]; if (wire->writing) write_le32(bytes, *value);
    security_state_wire_bytes(wire, bytes, sizeof(bytes));
    if (!wire->writing && wire->valid) *value = read_le32(bytes);
}
static inline void security_state_wire_u64(SecurityStateWire* wire, uint64_t* value) {
    uint8_t bytes[8]; if (wire->writing) write_le64(bytes, *value);
    security_state_wire_bytes(wire, bytes, sizeof(bytes));
    if (!wire->writing && wire->valid) *value = read_le64(bytes);
}
static inline void security_state_wire_text(SecurityStateWire* wire, char* text, size_t capacity) {
    security_state_wire_bytes(wire, text, capacity);
    if (!wire->valid) return;
    size_t length = strnlen(text, capacity);
    if (length == capacity) { wire->valid = false; return; }
    for (size_t i = length; i < capacity; i++) if (text[i]) wire->valid = false;
}
