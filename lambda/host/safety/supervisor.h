#pragma once
#include "admission.h"
#include "../../io/security_channel.h"
#include "lib/confined_process.h"

typedef struct SecuritySupervisorConfig {
    SecurityAdmissionConfig admission;
    ConfinedCodeSpec worker; // trusted installation; never accepted over worker IPC
    const char* working_directory;
    bool ask_overwrite;
    ConfinedProcessLimits limits;
} SecuritySupervisorConfig;
typedef struct SecuritySupervisorReport {
    SecurityAdmissionReport admission;
    SecurityChannelReport channel;
    ConfinedProcessResult process;
    uint32_t challenges;
    uint32_t approvals;
    uint32_t consents;
    int approval_error;
    bool verified;
    bool launched;
    bool served;
    int system_error;
    int cleanup_error;
} SecuritySupervisorReport;

// D11.1.1-2, D11.2.1-7. Trusted host assembles publisher, installation, catalog,
// store and user-selected roots before invoking this. No parsing in the host.
// Captured worker output is released only after all consent and teardown finish.
bool security_supervisor_run(const SecuritySupervisorConfig* config, SecuritySupervisorReport* report);
