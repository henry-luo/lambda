#pragma once
#include "grant_store.h"

// Trusted host only (D7.5.5-7). Explicit management never restores app grants
// or admits source. The caller owns verified host/account admission.
int security_storage_command(SecurityGrantStore* store, int argc, char** argv);
