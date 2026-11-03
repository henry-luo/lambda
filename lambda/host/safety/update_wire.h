#pragma once
#include "update_payload.h"
#include "../../../lib/ipc_stream.h"

enum UpdateWireKind { UPDATE_WIRE_INIT = 1, UPDATE_WIRE_FETCH, UPDATE_WIRE_CHECKPOINT,
    UPDATE_WIRE_DONE, UPDATE_WIRE_RESPONSE, UPDATE_WIRE_ERROR, UPDATE_WIRE_READY,
    UPDATE_WIRE_STAGE_INIT, UPDATE_WIRE_STAGE_ENTRY, UPDATE_WIRE_STAGE_END, UPDATE_WIRE_STAGE_ACK };
enum UpdateWireStatus { UPDATE_WIRE_OK = 1, UPDATE_WIRE_NOT_FOUND, UPDATE_WIRE_UNCHANGED, UPDATE_WIRE_VERIFIED };
enum { UPDATE_WIRE_BODY_LIMIT = 64 * 1024 * 1024, UPDATE_WIRE_RECORD_LIMIT = 1024 * 1024,
    UPDATE_WIRE_STAGE_MEMBER_LIMIT = 16 * 1024 * 1024,
    UPDATE_WIRE_FIXED_BYTES = 4173, UPDATE_WIRE_FRAME_LIMIT = UPDATE_WIRE_BODY_LIMIT + UPDATE_WIRE_FIXED_BYTES };
struct UpdateWireMessage {
    UpdateWireKind kind;
    uint32_t status, artifact;
    uint64_t limit;
    char path[4096], utc[21];
    uint32_t count, size;
    SecurityUpdateMetadata* entries;
    uint8_t* bytes;
    uint8_t digest[32];
};
struct UpdateWire {
    IpcStream* stream;
    uint64_t session, sent, received, deadline;
    IpcStreamTransferCheck check;
    void* context;
    bool failed;
};
// Dedicated trusted-verifier protocol; never dispatch on an app worker channel.
// All message buffers are owned; received ancillary descriptors are refused.
void security_update_wire_free(UpdateWireMessage* message);
bool security_update_wire_send(UpdateWire* wire, UpdateWireMessage* message);
bool security_update_wire_receive(UpdateWire* wire, uint32_t allowed_kinds, UpdateWireMessage* message);
static inline uint32_t update_wire_kind_mask(UpdateWireKind kind) { return 1u << kind; }
