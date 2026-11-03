#include "supervisor.h"
#include "update_command.h"
#include "../../../lib/hex.h"
#include "../../../lib/mem.h"
#include "../../../lib/path_str.h"
#include <errno.h>
#include <stdio.h>
#include <time.h>

static uint64_t host_clock(void*) { return (uint64_t)time(nullptr); }

static int fail(const char* message) {
    confined_process_emit(2, message, strlen(message)); return 2;
}
static int update_command_and_close(SecurityGrantStore* store, int argc, char** argv) {
    if (!store) return fail("safety-host: protected update settings unavailable\n");
    int result = security_update_command(store, argc, argv, host_clock, nullptr);
    security_grant_store_close(store); return result;
}

int main(int argc, char** argv) {
    const char* entry = nullptr; const char* project = nullptr;
    bool ask = false, revalidate = false;
    char installation[4096], bundle[4096];
    uint8_t digest[32]; const uint8_t* pin = nullptr; const char* publisher = nullptr;
    SecurityGrantStore* store = nullptr; SecurityStoreResult status;
    ConfinedProcessLimits limits = {30000, 10000, 512 * 1024 * 1024, 1024 * 1024};
#ifdef LAMBDA_SECURITY_SUPERVISOR_TESTING
    // Dedicated validation executable only. The production target contains no
    // fixture argument, alternate account home, executable pin or quota override.
    if (argc >= 3 && !strcmp(argv[1], "--fixture-updates"))
        return update_command_and_close(security_grant_store_test_open(argv[2], &status), argc - 3, argv + 3);
    if (argc != 12 || strcmp(argv[1], "--fixture") || strlen(argv[4]) != 64 ||
            !hex_decode(argv[4], 64, digest, nullptr) ||
            path_str_copy(installation, sizeof(installation), argv[3]) < 0) return 2;
    pin = digest; entry = argv[5]; project = strcmp(argv[6], "-") ? argv[6] : nullptr;
    uint64_t parsed[4];
    for (unsigned i = 0; i < 4; i++) {
        char* end = nullptr; errno = 0; parsed[i] = strtoull(argv[7 + i], &end, 10);
        if (errno || !end || *end || !parsed[i] || argv[7 + i][0] == '-') return 2;
    }
    if (parsed[0] > 30000 || parsed[1] > 10000 || parsed[2] > 512 * 1024 * 1024 || parsed[3] > 1024 * 1024) return 2;
    limits = {(uint32_t)parsed[0], (uint32_t)parsed[1], parsed[2], (uint32_t)parsed[3]};
    if (strcmp(argv[11], "ask") && strcmp(argv[11], "deny")) return 2;
    ask = !strcmp(argv[11], "ask");
    store = security_grant_store_test_open(argv[2], &status);
#else
    (void)digest;
    char team[64];
    bool updates = argc > 1 && !strcmp(argv[1], "updates");
    for (int i = 1; !updates && i < argc; i++) {
        if (!strcmp(argv[i], "--ask-overwrite")) ask = true;
        else if (!strcmp(argv[i], "--revalidate")) revalidate = true;
        else if (!strcmp(argv[i], "--project") && i + 1 < argc) project = argv[++i];
        else if (argv[i][0] != '-' && !entry) entry = argv[i];
        else return fail("Usage: lambda-safety-host.exe [--project ABS_DIR] [--ask-overwrite] [--revalidate] ABS_ENTRY\n");
    }
    if (!updates && (!entry || entry[0] != '/' || (project && project[0] != '/')))
        return fail("safety-host: select absolute entry/project paths\n");
    if (!confined_code_self(installation, sizeof(installation), team))
        return fail("safety-host: verified Apple-issued publisher signature required; no worker launched\n");
    char* slash = strrchr(installation, '/');
    if (!slash || slash == installation) return 2;
    *slash = 0; publisher = team;
    store = security_grant_store_open(&status);
    if (updates) return update_command_and_close(store, argc - 2, argv + 2);
#endif
    if (!store) return fail("safety-host: protected grant store unavailable; no worker launched\n");
    if (path_str_join_posix_into(bundle, sizeof(bundle), installation, "LambdaSafetyWorker.app") < 0) {
        security_grant_store_close(store); return 2;
    }
    SecuritySupervisorConfig config = {};
    config.admission = {store, entry, project, installation, nullptr, 0, nullptr, 0, revalidate, host_clock, nullptr};
    config.worker = {bundle, "org.lambda-script.safety-worker", publisher, pin};
    config.working_directory = project ? project : "/";
    config.ask_overwrite = ask; config.limits = limits;
    SecuritySupervisorReport report;
    bool success = security_supervisor_run(&config, &report);
    security_grant_store_close(store);
    // Buffered output cannot impersonate or race the host's consent prompt.
    confined_process_emit(1, report.process.output, report.process.output_size);
    confined_process_emit(2, report.process.error_output, report.process.error_size);
    char summary[640];
    int length = snprintf(summary, sizeof(summary),
        "{\"verified\":%s,\"launched\":%s,\"success\":%s,\"served\":%s,\"store_result\":%u,"
        "\"generation\":%llu,\"restored\":%u,\"requests\":%llu,\"channel_error\":%d,"
        "\"stop\":%u,\"exit\":%d,\"signal\":%d,\"error\":%d,\"elapsed_ms\":%llu,"
        "\"peak_memory\":%llu,\"cpu_ms\":%llu,\"challenges\":%u,\"approvals\":%u,\"consents\":%u,\"approval_error\":%d}\n",
        report.verified ? "true" : "false", report.launched ? "true" : "false", success ? "true" : "false",
        report.served ? "true" : "false", report.admission.store_result,
        (unsigned long long)report.admission.generation, report.admission.restored,
        (unsigned long long)report.channel.requests, report.channel.system_error,
        report.process.reason, report.process.exit_code, report.process.signal, report.system_error,
        (unsigned long long)report.process.elapsed_ms, (unsigned long long)report.process.peak_memory,
        (unsigned long long)report.process.cpu_ms,
        report.challenges, report.approvals, report.consents, report.approval_error);
    if (length > 0 && (size_t)length < sizeof(summary)) confined_process_emit(2, summary, length);
    int result = success ? 0 : !report.launched ? 2 : report.process.reason ? 4 : 3;
    confined_process_result_free(&report.process); return result;
}
