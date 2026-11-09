#pragma once
#include "update_download.h"
#include "lib/confined_process.h"

typedef struct SecurityVerifiedUpdate SecurityVerifiedUpdate;
struct SecurityVerifiedUpdateView {
    const uint8_t* bytes;
    size_t size;
    const uint8_t* description; // strict JSON; integers preserved exactly
    size_t description_size;
};
struct SecurityUpdateRefreshConfig {
    ConfinedCodeSpec verifier; // dedicated immutable verifier bundle, not an app
    const char* target; // admitted installed-catalog target; never an app request
    ConfinedProcessLimits limits;
};
struct SecurityUpdateRefreshReport {
    SecurityUpdateResult policy;
    HttpsPullStatus transport;
    uint32_t requests, checkpoints;
    uint64_t received_bytes, generation;
    bool launched, unchanged;
    int system_error;
    ConfinedProcessResult process;
    SecurityVerifiedUpdate* artifact;
};
// D11.2.4: trusted host service only. Does not extract, install, finish the ticket
// or expose an updater command to apps. Caller owns the live job through consume.
bool security_update_refresh(const SecurityUpdateRefreshConfig* config, SecurityUpdateJob* job, SecurityUpdateRefreshReport* out);
// Checks the captured random reservation and live policy on every handoff.
// A view is data, not a publication lease; installers must recheck at commit.
bool security_update_verified_view(SecurityVerifiedUpdate* update, SecurityUpdateJob* job, SecurityVerifiedUpdateView* out);
void security_update_refresh_free(SecurityUpdateRefreshReport* report);
#ifdef LAMBDA_SECURITY_UPDATE_TESTING
// Test-only composition seam: owned real verifier IPC + owned TLS fixture. This
// does not qualify publisher launch and is absent from the production binary.
bool security_update_refresh_fixture(int descriptor, uint64_t session, SecurityUpdateJob* job,
    const char* target, const HttpsPullFixture* fixture, SecurityUpdateRefreshReport* out);
#endif
