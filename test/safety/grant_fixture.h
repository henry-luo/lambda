#pragma once
#include "../../lambda/host/safety/grant_store.h"

struct SafetyGrantRevocation {
    SecurityGrantStore* store;
    SecurityGrantBinding binding;
    uint64_t expected, generation;
    SecurityStoreResult result;
    static void run(void* context) {
        SafetyGrantRevocation* revocation = (SafetyGrantRevocation*)context;
        revocation->result = security_grant_store_save(revocation->store, &revocation->binding,
            revocation->expected, nullptr, 0, &revocation->generation);
    }
};
