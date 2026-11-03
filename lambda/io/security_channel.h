#pragma once

#include "security_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SecurityChannel SecurityChannel;
typedef struct SecurityChannelConfig {
    // Host-assigned connection identity, never selected by a request body.
    uint64_t session;
    uint64_t max_total_bytes;
    uint32_t max_payload_bytes;
    uint32_t max_requests;
    uint32_t timeout_ms;
} SecurityChannelConfig;

typedef struct SecurityChannelReport {
    uint64_t requests;
    uint64_t bytes;
    int system_error;
} SecurityChannelReport;

// Trusted host only, never serialized. Capture precedes the operation; acquire
// compares that same authority at delivery and pins it through publication.
// No publication lease crosses request receipt, callback work or human consent.
typedef struct SecurityChannelDeliveryGuard {
    void* context;
    SecurityIOResult (*capture)(void* context, SecurityAuthorityStamp* stamp);
    void* (*acquire)(void* context, const SecurityAuthorityStamp* stamp, SecurityIOResult* failure);
    bool (*check)(void* context, void* lease); // fresh expiry check; no peer wait
    void (*release)(void* context, void* lease); // same dispatch thread
    uint32_t timeout_ms; // 1..1000, separate from the request/consent deadline
} SecurityChannelDeliveryGuard;

// A connected, privately inherited AF_UNIX stream is the admission capability.
// The launcher must authenticate/contain its child and close all other handles;
// neither a claimed principal nor same-UID peer credentials establish admission.
// Takes ownership of fd only on success. Unsupported platforms fail closed.
SecurityChannel* security_channel_create(int fd, const SecurityChannelConfig* config);
SecurityIOClient security_channel_client(SecurityChannel* channel);
// Interrupt wakes blocked I/O. Join all users before destroy; no fd escapes.
void security_channel_interrupt(SecurityChannel* channel);
void security_channel_destroy(SecurityChannel* channel);

// Host-only dispatch over an already admitted private connection. No settings,
// admission, approval or other management operation exists in the wire schema.
// Takes ownership only after successful channel construction. EOF between
// frames is success; truncation, replay, ancillary data or malformed input fail.
bool security_channel_serve(int fd, const SecurityChannelConfig* config,
    const SecurityIOClient* admitted_client, SecurityChannelReport* report);
// Host dispatcher for a channel constructed before child resume. Borrows the
// channel; its owner interrupts/joins and then destroys it on every exit path.
bool security_channel_dispatch(SecurityChannel* channel,
    const SecurityIOClient* admitted_client, SecurityChannelReport* report);
// Admitted production dispatch must use the guard. Existing unguarded entry
// points remain for neutral/local fixtures without persistent host authority.
bool security_channel_dispatch_guarded(SecurityChannel* channel,
    const SecurityIOClient* admitted_client, const SecurityChannelDeliveryGuard* guard,
    SecurityChannelReport* report);
bool security_channel_serve_guarded(int fd, const SecurityChannelConfig* config,
    const SecurityIOClient* admitted_client, const SecurityChannelDeliveryGuard* guard,
    SecurityChannelReport* report);

#ifdef __cplusplus
}
#endif
