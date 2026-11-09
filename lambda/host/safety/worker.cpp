#include "worker.h"
#include "../../runtime/transpiler.hpp"
#include "../../runtime/interp.hpp"
#include "../../runtime/security_context.h"
#include "lambda/io/security_channel.h"
#include "lib/log.h"
#include "lib/confined_process.h"
#include "lib/process_sandbox.h"
#include <unistd.h>

static int worker_run(int broker_fd, uint64_t session, const char* entry, const char* working_directory, bool preconfined) {
    if (!entry || !*entry || !session) return 2;
    // Apply the irreversible native-IO boundary before runtime initialization,
    // source acquisition or user callbacks (D11.1.1-2).
    if (!confined_process_worker_handles(broker_fd) || !confined_process_worker_limits() ||
            (preconfined ? !process_sandbox_is_applied(getpid()) : !process_sandbox_enter_offline())) return 2;
    // A host consent wait may last 30 seconds; the independent supervisor owns
    // the shorter whole-process deadline, including time spent waiting for UI.
    SecurityChannelConfig limits = {session, 64 * 1024 * 1024, 1024 * 1024, 8192, 60000};
    SecurityChannel* channel = security_channel_create(broker_fd, &limits);
    if (!channel) return 2;
    SecurityIOClient client = security_channel_client(channel);
    Runtime runtime = {};
    runtime_init(&runtime);
    EvalContext* owner = runtime_get_eval_context(&runtime);
    // Install acquisition authority before the entry is canonicalized, read,
    // parsed or initialized. This worker never has an ambient execution mode.
    if (!owner || !eval_context_bind_security_io(owner, &client) ||
            !eval_context_bind_security_directory(owner, working_directory)) {
        runtime_cleanup(&runtime); security_channel_destroy(channel); return 2;
    }
    // This first executable profile is explicitly T0; it has no JIT entitlement.
    lambda_tier_set(LAMBDA_TIER_INTERP);
    Input* result = run_script_mir(&runtime, nullptr, (char*)entry, true);
    int status = result && get_type_id(result->root) != LMD_TYPE_ERROR ? 0 : 3;
    if (status) log_error("safety-worker: entry execution failed");
    // Scheduler/prebuild teardown joins all borrowers before closing transport.
    runtime_cleanup(&runtime);
    security_channel_destroy(channel);
    return status;
}

int lambda_safety_worker_run(int broker_fd, uint64_t session, const char* entry, const char* working_directory) {
    return worker_run(broker_fd, session, entry, working_directory, false);
}
int lambda_safety_worker_run_preconfined(int broker_fd, uint64_t session, const char* entry, const char* working_directory) {
    // the sealed minimal bootstrap establishes the policy before loading this
    // runtime. Readback alone never authenticates an arbitrary caller/policy.
    return worker_run(broker_fd, session, entry, working_directory, true);
}
