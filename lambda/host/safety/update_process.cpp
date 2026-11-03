#include "update_process.h"
#include "update_records.h"
#include "../../../lib/time_util.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>

struct UpdateProcessMonitor {
    ConfinedProcess* process;
    IpcStream* stream;
    ConfinedProcessLimits limits;
    ConfinedProcessResult* result;
    bool success;
};
static void* update_process_monitor(void* pointer) {
    UpdateProcessMonitor* monitor = (UpdateProcessMonitor*)pointer;
    monitor->success = confined_process_wait(monitor->process, &monitor->limits, monitor->result);
    // A clean exit may leave a complete DONE frame queued; consume it before EOF.
    if (!monitor->success || monitor->result->exit_code || monitor->result->signal) ipc_stream_interrupt(monitor->stream);
    return nullptr;
}
bool security_update_process_run(const ConfinedCodeSpec* spec, const ConfinedProcessLimits* limits,
        SecurityUpdateJob* job, const char* entry, SecurityUpdateProcessAction action, void* context, SecurityUpdateProcessReport* report) {
    if (!report) return false;
    memset(report, 0, sizeof(*report)); report->process.exit_code = -1;
    if (!spec || !limits || !entry || !action || !spec->team || !*spec->team || !limits->wall_ms || limits->wall_ms > 600000 ||
        !limits->cpu_ms || limits->cpu_ms > 10000 || !limits->memory_bytes ||
        limits->memory_bytes > 512 * 1024 * 1024 || !limits->output_bytes || limits->output_bytes > 1024 * 1024) {
        report->system_error = EINVAL; return false;
    }
    ConfinedCode* code = confined_code_open(spec);
    if (!code) { report->system_error = errno; return false; }
    uint8_t binding[32]; uint64_t session = 0;
    if (security_update_job_binding(job, binding)) memcpy(&session, binding, sizeof(session));
    ConfinedProcess* process = session ? confined_process_spawn(code, entry, "/", session) : nullptr;
    if (!process) { report->system_error = errno; confined_code_close(code); return false; }
    int fd = confined_process_take_channel(process);
    IpcStream* stream = fd >= 0 ? ipc_stream_adopt(fd) : nullptr;
    if (!stream) confined_process_close_channel(fd);
    UpdateProcessMonitor monitor = {process, stream, *limits, &report->process, false}; pthread_t thread;
    bool started = false, success = false;
    if (stream && confined_process_resume(process)) {
        int error = pthread_create(&thread, nullptr, update_process_monitor, &monitor);
        if (!error) {
            started = true; report->launched = true;
            UpdateWire wire = {stream, session, 0, 0, time_now_ms() + 5000, nullptr, nullptr};
            UpdateWireMessage ready = {};
            success = security_update_wire_receive(&wire, update_wire_kind_mask(UPDATE_WIRE_READY), &ready) &&
                confined_process_is_sandboxed(process) && action(&wire, context);
            security_update_wire_free(&ready);
            if (!success) confined_process_cancel(process);
        } else report->system_error = error;
    } else report->system_error = errno;
    if (started) pthread_join(thread, nullptr);
    success = success && monitor.success && !report->process.exit_code && !report->process.signal && report->process.reason == CONFINED_PROCESS_EXITED;
    ipc_stream_close(stream); confined_process_destroy(process); confined_code_close(code); return success;
}
