#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "confined_process.h"
#include "digest.h"
#include "file.h"
#include "ipc_stream.h"
#include "mem.h"
#include "process_sandbox.h"
#include <errno.h>
#include <string.h>

#ifdef __APPLE__
#include "time_util.h"
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <mach/mach_param.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

struct ConfinedCode {
    SecStaticCodeRef reference;
    CFDataRef identity;
    char* executable;
};
struct ConfinedProcess { pid_t pid; int channel; int output; int error; bool reaped; bool resumed; atomic_bool cancelled; };

static void stop_owned_child(ConfinedProcess* process) {
    // An unreaped child reserves its PID. Never signal a number after reaping,
    // when the kernel could have assigned it to an unrelated process/session.
    if (process && process->pid > 0 && !process->reaped) {
        (void)kill(-process->pid, SIGKILL); (void)kill(process->pid, SIGKILL);
    }
}
static bool reap_exited_child(ConfinedProcess* process, int* status, bool* known, int* error) {
    if (process->reaped) return true;
    siginfo_t exited = {0};
    int observed = waitid(P_PID, process->pid, &exited, WEXITED | WNOHANG | WNOWAIT);
    pid_t waited = 0;
    if (!observed && exited.si_pid == process->pid) {
        stop_owned_child(process); // stop descendants while PID ownership is pinned
        waited = waitpid(process->pid, status, WNOHANG);
        if (waited == process->pid) { process->reaped = true; *known = true; }
    }
    if ((observed < 0 || waited < 0) && errno != EINTR) {
        *error = errno;
        if (errno == ECHILD) process->reaped = true; // ownership was already lost
        return false;
    }
    return true;
}

static bool signing_string(CFDictionaryRef info, CFStringRef key, const char* expected) {
    CFTypeRef value = CFDictionaryGetValue(info, key);
    char text[256];
    return value && CFGetTypeID(value) == CFStringGetTypeID() &&
        CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8) && !strcmp(text, expected);
}
static SecRequirementRef apple_publisher_requirement(void) {
    SecRequirementRef requirement = NULL;
    if (SecRequirementCreateWithString(CFSTR("anchor apple generic"), kSecCSDefaultFlags, &requirement) != errSecSuccess)
        return NULL;
    return requirement;
}

ConfinedCode* confined_code_open(const ConfinedCodeSpec* spec) {
    if (!spec || !spec->bundle || spec->bundle[0] != '/' || !spec->identifier ||
            (!spec->team && !spec->image_digest)) { errno = EINVAL; return NULL; }
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
        (const UInt8*)spec->bundle, strlen(spec->bundle), true);
    SecStaticCodeRef reference = NULL;
    CFDictionaryRef info = NULL;
    SecRequirementRef publisher = spec->team ? apple_publisher_requirement() : NULL;
    bool valid = url && SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &reference) == errSecSuccess &&
        (!spec->team || publisher) &&
        SecStaticCodeCheckValidity(reference, kSecCSStrictValidate | kSecCSCheckAllArchitectures | kSecCSCheckNestedCode, publisher) == errSecSuccess &&
        SecCodeCopySigningInformation(reference, kSecCSSigningInformation, &info) == errSecSuccess;
    if (publisher) CFRelease(publisher);
    if (url) CFRelease(url);
    if (valid) {
        CFTypeRef entitlements = CFDictionaryGetValue(info, kSecCodeInfoEntitlementsDict);
        CFTypeRef flags = CFDictionaryGetValue(info, kSecCodeInfoFlags);
        uint32_t bits = 0;
        // Reject extra entitlements, including JIT, network, file exceptions,
        // debugging and inheritance. App Sandbox would prevent installation of
        // our stricter process policy; this offline T0 profile has no entitlement.
        valid = signing_string(info, kSecCodeInfoIdentifier, spec->identifier) &&
            (!spec->team || signing_string(info, kSecCodeInfoTeamIdentifier, spec->team)) &&
            flags && CFGetTypeID(flags) == CFNumberGetTypeID() &&
            CFNumberGetValue(flags, kCFNumberSInt32Type, &bits) && (bits & kSecCodeSignatureRuntime) &&
            (!spec->team || !(bits & kSecCodeSignatureAdhoc)) &&
            (!entitlements || (CFGetTypeID(entitlements) == CFDictionaryGetTypeID() &&
                CFDictionaryGetCount(entitlements) == 0));
    }
    char path[4096];
    if (valid) {
        CFTypeRef executable = CFDictionaryGetValue(info, kSecCodeInfoMainExecutable);
        valid = executable && CFGetTypeID(executable) == CFURLGetTypeID() &&
            CFURLGetFileSystemRepresentation(executable, true, (UInt8*)path, sizeof(path));
    }
    if (valid && spec->image_digest) {
        char* bytes = NULL; size_t size = 0; uint8_t hash[32];
        valid = file_read_all_limit(path, MEM_CAT_SYSTEM, 128 * 1024 * 1024, &bytes, &size) &&
            digest_sha256(bytes, size, hash) && !memcmp(hash, spec->image_digest, sizeof(hash));
        mem_free(bytes);
    }
    CFTypeRef identity = valid ? CFDictionaryGetValue(info, kSecCodeInfoUnique) : NULL;
    valid = valid && identity && CFGetTypeID(identity) == CFDataGetTypeID();
    ConfinedCode* code = valid ? (ConfinedCode*)mem_calloc(1, sizeof(*code), MEM_CAT_SYSTEM) : NULL;
    if (code) {
        code->executable = mem_strdup(path, MEM_CAT_SYSTEM);
        code->identity = CFRetain(identity); code->reference = reference; reference = NULL;
        if (!code->executable) { confined_code_close(code); code = NULL; }
    }
    if (info) CFRelease(info);
    if (reference) CFRelease(reference);
    if (!code) errno = EACCES;
    return code;
}
void confined_code_close(ConfinedCode* code) {
    if (!code) return;
    CFRelease(code->identity); CFRelease(code->reference); mem_free(code->executable); mem_free(code);
}
bool confined_code_self(char* executable, size_t capacity, char team[64]) {
    SecCodeRef self = NULL; CFDictionaryRef info = NULL;
    SecRequirementRef publisher_requirement = apple_publisher_requirement();
    bool valid = executable && capacity && team &&
        publisher_requirement &&
        SecCodeCopySelf(kSecCSDefaultFlags, &self) == errSecSuccess &&
        SecCodeCheckValidity(self, kSecCSDefaultFlags, publisher_requirement) == errSecSuccess &&
        SecCodeCopySigningInformation(self, kSecCSSigningInformation, &info) == errSecSuccess;
    CFTypeRef publisher = valid ? CFDictionaryGetValue(info, kSecCodeInfoTeamIdentifier) : NULL;
    CFTypeRef path = valid ? CFDictionaryGetValue(info, kSecCodeInfoMainExecutable) : NULL;
    CFTypeRef flags = valid ? CFDictionaryGetValue(info, kSecCodeInfoFlags) : NULL;
    CFTypeRef entitlements = valid ? CFDictionaryGetValue(info, kSecCodeInfoEntitlementsDict) : NULL;
    uint32_t bits = 0;
    valid = valid && publisher && CFGetTypeID(publisher) == CFStringGetTypeID() &&
        flags && CFGetTypeID(flags) == CFNumberGetTypeID() && CFNumberGetValue(flags, kCFNumberSInt32Type, &bits) &&
        (bits & kSecCodeSignatureRuntime) && !(bits & kSecCodeSignatureAdhoc) &&
        (!entitlements || (CFGetTypeID(entitlements) == CFDictionaryGetTypeID() && !CFDictionaryGetCount(entitlements))) &&
        CFStringGetCString(publisher, team, 64, kCFStringEncodingUTF8) && team[0] &&
        path && CFGetTypeID(path) == CFURLGetTypeID() &&
        CFURLGetFileSystemRepresentation(path, true, (UInt8*)executable, capacity);
    if (info) CFRelease(info);
    if (self) CFRelease(self);
    if (publisher_requirement) CFRelease(publisher_requirement);
    if (!valid) errno = EACCES;
    return valid;
}

static void close_fd(int* fd) { if (*fd >= 0) close(*fd); *fd = -1; }
static bool marked_fd(int fd, bool nonblocking) {
    return !fcntl(fd, F_SETFD, FD_CLOEXEC) && (!nonblocking || !fcntl(fd, F_SETFL, O_NONBLOCK));
}
static bool child_identity(ConfinedCode* code, pid_t pid) {
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberIntType, &pid);
    const void* key = kSecGuestAttributePid; const void* value = number;
    CFDictionaryRef attributes = number ? CFDictionaryCreate(NULL, &key, &value, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks) : NULL;
    SecCodeRef child = NULL; CFDictionaryRef info = NULL;
    bool valid = attributes && SecCodeCopyGuestWithAttributes(NULL, attributes, kSecCSDefaultFlags, &child) == errSecSuccess &&
        SecCodeCheckValidity(child, kSecCSDefaultFlags, NULL) == errSecSuccess &&
        SecCodeCopySigningInformation(child, kSecCSSigningInformation, &info) == errSecSuccess;
    CFTypeRef identity = valid ? CFDictionaryGetValue(info, kSecCodeInfoUnique) : NULL;
    valid = valid && identity && CFEqual(identity, code->identity);
    if (info) CFRelease(info);
    if (child) CFRelease(child);
    if (attributes) CFRelease(attributes);
    if (number) CFRelease(number);
    return valid;
}
static int configure_mach_ports(posix_spawnattr_t* attr) {
    // CLOEXEC controls fd inheritance only. Empty Mach actions must run in
    // the kernel before dyld/constructors can acquire the parent's rights.
    typedef int (*SetRegisteredPorts)(posix_spawnattr_t*, mach_port_t[], uint32_t);
    SetRegisteredPorts set_registered = (SetRegisteredPorts)dlsym(RTLD_DEFAULT,
        "posix_spawnattr_set_registered_ports_np");
    if (!set_registered) return ENOTSUP;
    mach_port_t empty[TASK_PORT_REGISTER_MAX] = {0};
    // A zero count appends no action and would preserve the inherited array.
    int error = set_registered(attr, empty, TASK_PORT_REGISTER_MAX);
    if (!error) error = posix_spawnattr_setspecialport_np(attr, MACH_PORT_NULL, TASK_BOOTSTRAP_PORT);
    if (!error) error = posix_spawnattr_setexceptionports_np(attr, EXC_MASK_ALL,
        MACH_PORT_NULL, EXCEPTION_DEFAULT, THREAD_STATE_NONE);
    return error;
}
ConfinedProcess* confined_process_spawn(ConfinedCode* code, const char* entry, const char* cwd, uint64_t session) {
    if (!code || !entry || entry[0] != '/' || !cwd || cwd[0] != '/' || !session) { errno = EINVAL; return NULL; }
    int pair[2] = {-1, -1}, output[2] = {-1, -1}, error[2] = {-1, -1}, input = -1;
    posix_spawnattr_t attr; posix_spawn_file_actions_t actions;
    bool has_attr = false, has_actions = false;
    ConfinedProcess* process = NULL; int failure = 0;
    // Reserve fd 0..3 in the host before creating endpoints, so dup actions
    // cannot clobber a still-needed pipe end when the caller had closed stdio.
    int reserved[4] = {-1, -1, -1, -1};
    for (int i = 0; i < 4; i++) if (fcntl(i, F_GETFD) < 0) {
        reserved[i] = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (reserved[i] != i) { failure = errno ? errno : EIO; goto done; }
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) || pipe(output) || pipe(error) ||
            (input = open("/dev/null", O_RDONLY | O_CLOEXEC)) < 0) { failure = errno; goto done; }
    int descriptors[] = {pair[0], pair[1], output[0], output[1], error[0], error[1]};
    for (size_t i = 0; i < sizeof(descriptors) / sizeof(descriptors[0]); i++)
        if (!marked_fd(descriptors[i], i == 2 || i == 4)) { failure = errno; goto done; }
    failure = posix_spawnattr_init(&attr); if (failure) goto done; has_attr = true;
    failure = posix_spawn_file_actions_init(&actions); if (failure) goto done; has_actions = true;
    failure = configure_mach_ports(&attr); if (failure) goto done;
    sigset_t empty, defaults; sigemptyset(&empty); sigfillset(&defaults);
    if ((failure = posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED | POSIX_SPAWN_CLOEXEC_DEFAULT |
                POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF)) ||
            (failure = posix_spawnattr_setsigmask(&attr, &empty)) ||
            (failure = posix_spawnattr_setsigdefault(&attr, &defaults)) ||
            (failure = posix_spawn_file_actions_addchdir_np(&actions, cwd)) ||
            (failure = posix_spawn_file_actions_adddup2(&actions, input, 0)) ||
            (failure = posix_spawn_file_actions_adddup2(&actions, output[1], 1)) ||
            (failure = posix_spawn_file_actions_adddup2(&actions, error[1], 2)) ||
            (failure = posix_spawn_file_actions_adddup2(&actions, pair[1], 3))) goto done;
    process = (ConfinedProcess*)mem_calloc(1, sizeof(*process), MEM_CAT_SYSTEM);
    if (!process) { failure = ENOMEM; goto done; }
    process->channel = process->output = process->error = -1;
    atomic_init(&process->cancelled, false);
    char session_text[32]; snprintf(session_text, sizeof(session_text), "%llu", (unsigned long long)session);
    char* argv[] = {code->executable, "3", session_text, (char*)entry, NULL};
    char* env[] = {"PATH=/usr/bin:/bin", "LANG=en_US.UTF-8", NULL};
    failure = posix_spawn(&process->pid, code->executable, &actions, &attr, argv, env);
    if (failure) { process->pid = 0; goto done; }
    // The kernel has exec'd, but no bootstrap instruction runs until the host
    // verifies/resumes it. Broker dispatch later checks sandbox installation.
    if (!child_identity(code, process->pid) ||
            SecStaticCodeCheckValidity(code->reference, kSecCSStrictValidate | kSecCSCheckNestedCode, NULL) != errSecSuccess) {
        failure = EACCES; goto done;
    }
    process->channel = pair[0]; pair[0] = -1;
    process->output = output[0]; output[0] = -1;
    process->error = error[0]; error[0] = -1;
done:
    if (has_actions) posix_spawn_file_actions_destroy(&actions);
    if (has_attr) posix_spawnattr_destroy(&attr);
    for (int i = 0; i < 2; i++) { close_fd(&pair[i]); close_fd(&output[i]); close_fd(&error[i]); }
    close_fd(&input);
    for (int i = 0; i < 4; i++) close_fd(&reserved[i]);
    if (failure) { confined_process_destroy(process); errno = failure; return NULL; }
    return process;
}
int confined_process_take_channel(ConfinedProcess* process) {
    if (!process) { errno = EINVAL; return -1; }
    int fd = process->channel; process->channel = -1; return fd;
}
void confined_process_close_channel(int descriptor) { if (descriptor >= 0) close(descriptor); }
bool confined_process_is_sandboxed(ConfinedProcess* process) {
    return process && process_sandbox_is_applied(process->pid);
}
bool confined_process_resume(ConfinedProcess* process) {
    if (!process || process->resumed || process->reaped) { errno = EINVAL; return false; }
    if (kill(process->pid, SIGCONT)) return false;
    process->resumed = true; return true;
}
void confined_process_cancel(ConfinedProcess* process) {
    if (process) atomic_store(&process->cancelled, true);
}
static bool capture_output(int* fd, char* buffer, size_t* size, size_t available,
        uint64_t* total, uint32_t limit, ConfinedProcessStop* reason, int* system_error) {
    char bytes[4096];
    ssize_t count = read(*fd, bytes, sizeof(bytes));
    if (count > 0) {
        if ((uint64_t)count > limit - *total || (size_t)count > available) {
            *reason = CONFINED_PROCESS_OUTPUT_LIMIT; return false;
        }
        memcpy(buffer + *size, bytes, count); *size += count; *total += count; return true;
    }
    if (!count) { close_fd(fd); return true; }
    if (errno == EAGAIN || errno == EINTR) return true;
    *system_error = errno ? errno : EIO;
    *reason = CONFINED_PROCESS_MONITOR_ERROR; return false;
}
bool confined_process_wait(ConfinedProcess* process, const ConfinedProcessLimits* limits, ConfinedProcessResult* result) {
    if (!process || !process->resumed || !limits || !limits->wall_ms || !limits->cpu_ms ||
            !limits->memory_bytes || !limits->output_bytes || limits->output_bytes > 16 * 1024 * 1024 || !result) {
        errno = EINVAL; return false;
    }
    memset(result, 0, sizeof(*result)); result->exit_code = -1;
    result->output = (char*)mem_alloc((size_t)limits->output_bytes + 1, MEM_CAT_SYSTEM);
    result->error_output = (char*)mem_alloc((size_t)limits->output_bytes + 1, MEM_CAT_SYSTEM);
    uint64_t start = time_now_ms(), total = 0; int status = 0; bool status_known = false;
    mach_timebase_info_data_t timebase;
    bool has_timebase = mach_timebase_info(&timebase) == KERN_SUCCESS && timebase.denom && timebase.numer;
    if (!result->output || !result->error_output || !has_timebase) {
        result->reason = CONFINED_PROCESS_MONITOR_ERROR; result->system_error = ENOMEM;
    } else while (true) {
        if (atomic_load(&process->cancelled)) { result->reason = CONFINED_PROCESS_CANCELLED; break; }
        // Reap once. Pipe data/EOF may need later iterations after a clean
        // leader exit; another waitpid would report ECHILD and truncate capture.
        if (!reap_exited_child(process, &status, &status_known, &result->system_error)) {
            result->reason = CONFINED_PROCESS_MONITOR_ERROR; break;
        }
        result->elapsed_ms = time_now_ms() - start;
        if (result->elapsed_ms >= limits->wall_ms) { result->reason = CONFINED_PROCESS_WALL_LIMIT; break; }
        if (!process->reaped) {
            struct rusage_info_v0 usage;
            if (proc_pid_rusage(process->pid, RUSAGE_INFO_V0, (rusage_info_t*)&usage)) {
                // An exit may race sampling; waitpid below will authenticate it.
                int saved = errno;
                if (!reap_exited_child(process, &status, &status_known, &result->system_error) || !process->reaped) {
                    if (!result->system_error) result->system_error = saved ? saved : EIO;
                    result->reason = CONFINED_PROCESS_MONITOR_ERROR; break;
                }
            } else {
                if (usage.ri_phys_footprint > result->peak_memory) result->peak_memory = usage.ri_phys_footprint;
                if (usage.ri_phys_footprint > limits->memory_bytes) { result->reason = CONFINED_PROCESS_MEMORY_LIMIT; break; }
                // proc_pid_rusage reports Mach absolute ticks, whose scale on
                // Apple Silicon differs from nanoseconds. Convert before gates.
                __uint128_t cpu_ticks = (__uint128_t)usage.ri_user_time + usage.ri_system_time;
                result->cpu_ms = (uint64_t)(cpu_ticks * timebase.numer / timebase.denom / 1000000);
                if (result->cpu_ms >= limits->cpu_ms) {
                    result->reason = CONFINED_PROCESS_CPU_LIMIT; break;
                }
            }
        }
        if (process->output < 0 && process->error < 0 && process->reaped) break;
        struct pollfd streams[] = {{process->output, POLLIN, 0}, {process->error, POLLIN, 0}};
        int ready = poll(streams, 2, 20);
        if (ready < 0 && errno != EINTR) { result->system_error = errno; result->reason = CONFINED_PROCESS_MONITOR_ERROR; break; }
        if (streams[0].revents && !capture_output(&process->output, result->output, &result->output_size,
                limits->output_bytes - result->output_size, &total, limits->output_bytes, &result->reason, &result->system_error)) break;
        if (streams[1].revents && !capture_output(&process->error, result->error_output, &result->error_size,
                limits->output_bytes - result->error_size, &total, limits->output_bytes, &result->reason, &result->system_error)) break;
    }
    // Even a clean leader exit must not leave a descendant holding the broker
    // or output pipes. Only the private session's process group is signalled.
    stop_owned_child(process);
    if (!process->reaped) {
        pid_t waited; do { waited = waitpid(process->pid, &status, 0); } while (waited < 0 && errno == EINTR);
        process->reaped = waited == process->pid; status_known = process->reaped;
    }
    if (status_known && WIFEXITED(status)) result->exit_code = WEXITSTATUS(status);
    if (status_known && WIFSIGNALED(status)) result->signal = WTERMSIG(status);
    if (result->output) result->output[result->output_size] = 0;
    if (result->error_output) result->error_output[result->error_size] = 0;
    return process->reaped && result->reason == CONFINED_PROCESS_EXITED;
}
void confined_process_destroy(ConfinedProcess* process) {
    if (!process) return;
    if (process->pid > 0 && !process->reaped) {
        stop_owned_child(process);
        while (waitpid(process->pid, NULL, 0) < 0 && errno == EINTR) {}
    }
    close_fd(&process->channel); close_fd(&process->output); close_fd(&process->error); mem_free(process);
}
bool confined_process_confirm(const char* prompt, uint32_t timeout_ms) {
    if (!prompt || !timeout_ms || timeout_ms > 30000) return false;
    int fd = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return false;
    if (fd >= FD_SETSIZE) { close(fd); return false; }
    if (tcflush(fd, TCIFLUSH)) { close(fd); return false; }
    size_t length = strlen(prompt);
    bool accepted = false;
    if (write(fd, prompt, length) == (ssize_t)length) {
        char answer[16]; size_t size = 0; uint64_t deadline = time_now_ms() + timeout_ms;
        while (size < sizeof(answer) - 1) {
            uint64_t now = time_now_ms(); if (now >= deadline) break;
            // Darwin poll() reports POLLNVAL for the /dev/tty alias even while
            // read() works. pselect waits for terminal input without that race.
            fd_set input; FD_ZERO(&input); FD_SET(fd, &input);
            uint64_t remaining = deadline - now;
            struct timespec wait = {(time_t)(remaining / 1000), (long)(remaining % 1000) * 1000000};
            int ready = pselect(fd + 1, &input, NULL, NULL, &wait, NULL);
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0) break;
            char c; ssize_t count = read(fd, &c, 1);
            if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (count != 1) break;
            if (c == '\n') { answer[size] = 0; accepted = !strcmp(answer, "yes"); break; }
            answer[size++] = c;
        }
    }
    close(fd); return accepted;
}
bool confined_process_emit(int descriptor, const char* bytes, size_t size) {
    bool terminal = isatty(descriptor);
    for (size_t i = 0; i < size;) {
        size_t count = terminal ? 1 : size - i;
        char safe[5]; const char* text = bytes + i;
        if (terminal && (((unsigned char)*text < 32 && *text != '\n' && *text != '\t') || (unsigned char)*text >= 127)) {
            snprintf(safe, sizeof(safe), "\\x%02x", (unsigned char)*text); text = safe; count = 4;
        }
        ssize_t written = write(descriptor, text, count);
        if (written < 0 && errno == EINTR) continue;
        if (written != (ssize_t)count) return false;
        i += terminal ? 1 : count;
    }
    return true;
}
#else
bool confined_process_is_sandboxed(ConfinedProcess* process) { (void)process; errno = ENOTSUP; return false; }
ConfinedCode* confined_code_open(const ConfinedCodeSpec* spec) { (void)spec; errno = ENOTSUP; return NULL; }
void confined_code_close(ConfinedCode* code) { (void)code; }
bool confined_code_self(char* executable, size_t capacity, char team[64]) { (void)executable; (void)capacity; (void)team; errno = ENOTSUP; return false; }
ConfinedProcess* confined_process_spawn(ConfinedCode* code, const char* entry, const char* cwd, uint64_t session) {
    (void)code; (void)entry; (void)cwd; (void)session; errno = ENOTSUP; return NULL;
}
int confined_process_take_channel(ConfinedProcess* process) { (void)process; errno = ENOTSUP; return -1; }
void confined_process_close_channel(int descriptor) { (void)descriptor; }
bool confined_process_resume(ConfinedProcess* process) { (void)process; errno = ENOTSUP; return false; }
void confined_process_cancel(ConfinedProcess* process) { (void)process; }
bool confined_process_wait(ConfinedProcess* process, const ConfinedProcessLimits* limits, ConfinedProcessResult* result) {
    (void)process; (void)limits; (void)result; errno = ENOTSUP; return false;
}
void confined_process_destroy(ConfinedProcess* process) { (void)process; }
bool confined_process_confirm(const char* prompt, uint32_t timeout_ms) { (void)prompt; (void)timeout_ms; return false; }
bool confined_process_emit(int descriptor, const char* bytes, size_t size) { (void)descriptor; (void)bytes; (void)size; return false; }
#endif

void confined_process_result_free(ConfinedProcessResult* result) {
    if (!result) return;
    mem_free(result->output); mem_free(result->error_output); memset(result, 0, sizeof(*result));
}
