#include "supervisor.h"
#include "../../io/security_channel.h"
#include "../../../lib/hex.h"
#include "../../../lib/mem.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

struct SupervisorSession {
    SecurityAdmission* admission;
    SecurityIOClient client;
    bool ask;
    SecuritySupervisorReport* report;
    pthread_mutex_t mutex;
    bool active;
    ConfinedProcess* process;
    SecurityChannelDeliveryGuard delivery;
};
struct SupervisorMonitor {
    ConfinedProcess* process;
    SecurityChannel* channel;
    SupervisorSession* session;
    ConfinedProcessLimits limits;
    ConfinedProcessResult* result;
    bool success;
};
static void* supervise_child(void* context) {
    SupervisorMonitor* monitor = (SupervisorMonitor*)context;
    monitor->success = confined_process_wait(monitor->process, &monitor->limits, monitor->result);
    pthread_mutex_lock(&monitor->session->mutex);
    monitor->session->active = false;
    pthread_mutex_unlock(&monitor->session->mutex);
    security_channel_interrupt(monitor->channel);
    return nullptr;
}
static SecurityIOResult supervised_capture(void* context, SecurityAuthorityStamp* stamp) {
    SupervisorSession* session = (SupervisorSession*)context;
    pthread_mutex_lock(&session->mutex);
    bool ready = session->active && confined_process_is_sandboxed(session->process);
    pthread_mutex_unlock(&session->mutex);
    // The exact verified bootstrap owns the policy; label readback prevents an
    // early/missing bootstrap from receiving source bytes or invoking broker IO.
    if (!ready) { *stamp = {}; return {SECURITY_DENIED, EACCES, false, {}}; }
    return session->delivery.capture(session->delivery.context, stamp);
}
static SecurityIOResult supervised_write(void* context, const char* path, const void* data,
        size_t size, bool append, bool atomic) {
    SupervisorSession* session = (SupervisorSession*)context;
    SecurityIOResult result = session->client.write(session->client.context, path, data, size, append, atomic);
    if (result.decision != SECURITY_APPROVAL_REQUIRED) return result;
    session->report->challenges++;
    SecurityOverwriteChallenge challenge;
    bool approved = false;
    if (session->ask && security_admission_describe(session->admission, result.approval, &challenge)) {
        // Escape every non-ASCII/control byte so a filename cannot alter the
        // trusted terminal prompt. Contents and worker output never render here.
        char escaped[sizeof(challenge.path) * 4]; size_t used = 0;
        for (const unsigned char* p = (const unsigned char*)challenge.path; *p; p++) {
            if (*p >= 32 && *p < 127 && *p != '\\') escaped[used++] = *p;
            else { escaped[used++] = '\\'; escaped[used++] = 'x';
                escaped[used++] = hex_encode_nibble(*p >> 4); escaped[used++] = hex_encode_nibble(*p & 15); }
        }
        escaped[used] = 0;
        char digest[65]; hex_encode(challenge.digest, 32, digest);
        char prompt[sizeof(escaped) + 512];
        int count = snprintf(prompt, sizeof(prompt),
            "\nLambda host requests approval ONCE\n%s\nOperation: %s; payload: %zu bytes\n"
            "SHA256: %s\nTarget device/inode: %llu/%llu\nType yes to approve (30 seconds): ",
            escaped, append ? "append" : "replace", challenge.size, digest,
            (unsigned long long)challenge.target.device, (unsigned long long)challenge.target.inode);
        if (count > 0 && (size_t)count < sizeof(prompt)) approved = confined_process_confirm(prompt, 30000);
    }
    if (approved) {
        session->report->consents++;
        pthread_mutex_lock(&session->mutex);
        if (session->active) {
            result = security_admission_approve(session->admission, result.approval);
            session->report->approval_error = result.system_error;
            if (result.committed) session->report->approvals++;
            pthread_mutex_unlock(&session->mutex); return result;
        }
        pthread_mutex_unlock(&session->mutex);
    }
    security_admission_cancel(session->admission, result.approval);
    return {SECURITY_DENIED, EACCES, false, {}};
}

static bool supervisor_close_admission(SecurityAdmission* admission, SecuritySupervisorReport* report) {
    bool cleaned = security_admission_finish(admission);
    if (!cleaned) report->cleanup_error = errno ? errno : EIO;
    security_admission_close(admission); return cleaned;
}

bool security_supervisor_run(const SecuritySupervisorConfig* config, SecuritySupervisorReport* report) {
    if (!config || !report) return false;
    memset(report, 0, sizeof(*report)); report->process.exit_code = -1;
    if (!config->limits.wall_ms || config->limits.wall_ms > 30000 || !config->limits.cpu_ms ||
            config->limits.cpu_ms > 10000 || !config->limits.memory_bytes ||
            config->limits.memory_bytes > 512 * 1024 * 1024 || !config->limits.output_bytes ||
            config->limits.output_bytes > 1024 * 1024) { report->system_error = EINVAL; return false; }
    ConfinedCode* code = confined_code_open(&config->worker);
    if (!code) { report->system_error = errno; return false; }
    report->verified = true;
    SecurityAdmission* admission = security_admission_open(&config->admission, &report->admission);
    ConfinedProcess* process = admission ? confined_process_spawn(code, security_admission_entry(admission),
        config->working_directory, security_admission_session(admission)) : nullptr;
    if (!process) {
        report->system_error = errno;
        supervisor_close_admission(admission, report); confined_code_close(code); return false;
    }
    report->launched = true;
    SupervisorSession session = {admission, security_admission_client(admission), config->ask_overwrite, report};
    // Other callbacks retain admission's context; only write requires host UI.
    // A wrapper client forwards each callback to keep a single context owner.
    if (pthread_mutex_init(&session.mutex, nullptr)) {
        report->system_error = ENOMEM; confined_process_destroy(process);
        supervisor_close_admission(admission, report); confined_code_close(code); return false;
    }
    session.active = true;
    session.process = process;
    session.delivery = security_admission_delivery_guard(admission);
    SecurityIOClient wrapped = session.client;
    wrapped.context = &session;
    wrapped.read = [](void* c, const char* path, bool source, uint64_t limit, MemCategory category, char** bytes, size_t* size) {
        SupervisorSession* s = (SupervisorSession*)c; return s->client.read(s->client.context, path, source, limit, category, bytes, size); };
    wrapped.canonical_source = [](void* c, const char* path, char** canonical) {
        SupervisorSession* s = (SupervisorSession*)c; return s->client.canonical_source(s->client.context, path, canonical); };
    wrapped.input_kind = [](void* c, const char* path, int32_t* kind) {
        SupervisorSession* s = (SupervisorSession*)c; return s->client.input_kind(s->client.context, path, kind); };
    wrapped.enumerate = [](void* c, const char* path, uint32_t limit, SecurityDirectory* directory) {
        SupervisorSession* s = (SupervisorSession*)c; return s->client.enumerate(s->client.context, path, limit, directory); };
    wrapped.stat = [](void* c, const char* path, FileStat* metadata) {
        SupervisorSession* s = (SupervisorSession*)c; return s->client.stat(s->client.context, path, metadata); };
    wrapped.authority_stamp = [](void* c, SecurityAuthorityStamp* stamp) {
        SupervisorSession* s = (SupervisorSession*)c; return s->client.authority_stamp(s->client.context, stamp); };
    wrapped.write = supervised_write;
    SecurityChannelConfig limits = {security_admission_session(admission), 64 * 1024 * 1024, 1024 * 1024, 8192, 60000};
    int fd = confined_process_take_channel(process);
    SecurityChannel* channel = fd >= 0 ? security_channel_create(fd, &limits) : nullptr;
    if (!channel) confined_process_close_channel(fd);
    SupervisorMonitor monitor = {process, channel, &session, config->limits, &report->process, false};
    pthread_t thread; bool started = false;
    if (channel && confined_process_resume(process)) {
        int status = pthread_create(&thread, nullptr, supervise_child, &monitor);
        if (!status) {
            started = true;
            SecurityChannelDeliveryGuard guard = {&session, supervised_capture,
                [](void* c, const SecurityAuthorityStamp* stamp, SecurityIOResult* failure) -> void* {
                    SupervisorSession* s = (SupervisorSession*)c;
                    return s->delivery.acquire(s->delivery.context, stamp, failure); },
                [](void* c, void* lease) {
                    SupervisorSession* s = (SupervisorSession*)c;
                    return s->delivery.check(s->delivery.context, lease); },
                [](void* c, void* lease) {
                    SupervisorSession* s = (SupervisorSession*)c;
                    s->delivery.release(s->delivery.context, lease); }, session.delivery.timeout_ms};
            report->served = security_channel_dispatch_guarded(channel, &wrapped, &guard, &report->channel);
            if (!report->served) confined_process_cancel(process);
        } else report->system_error = status;
    } else report->system_error = errno;
    // A child that leaves the channel early still receives a finite watchdog.
    if (started) pthread_join(thread, nullptr);
    pthread_mutex_destroy(&session.mutex);
    security_channel_destroy(channel);
    confined_process_destroy(process);
    bool cleaned = supervisor_close_admission(admission, report); confined_code_close(code);
    return cleaned && started && monitor.success && report->served && report->process.exit_code == 0 && !report->process.signal;
}
