#pragma once
#include "update_controls.h"

// Trusted host console only; no initializer, endpoint admission or fetching is
// reachable from a worker. The caller owns verified host/account admission.
int security_update_command(SecurityGrantStore* store, int argc, char** argv,
    uint64_t (*clock)(void*), void* context);
