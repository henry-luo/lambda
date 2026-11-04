#pragma once
#include "update_wire.h"
#include "update_controls.h"
#include "../../../lib/confined_process.h"

struct SecurityUpdateProcessReport {
    ConfinedProcessResult process;
    bool launched;
    int system_error;
};
typedef bool (*SecurityUpdateProcessAction)(UpdateWire* wire, void* context);
// D11.1.2, D11.2.4: dedicated immutable offline service, never package code.
// Callback runs only after READY and kernel-label readback. No settings lock
// crosses execution; every caller supplies its own live-policy transfer check.
bool security_update_process_run(const ConfinedCodeSpec* code, const ConfinedProcessLimits* limits,
    SecurityUpdateJob* job, const char* entry, SecurityUpdateProcessAction action, void* context,
    SecurityUpdateProcessReport* out);
