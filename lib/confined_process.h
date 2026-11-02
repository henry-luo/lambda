#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ConfinedCode ConfinedCode;
typedef struct ConfinedProcess ConfinedProcess;
typedef struct ConfinedCodeSpec {
    const char* bundle;
    const char* identifier;
    const char* team; // publisher identity, supplied by the trusted host
    const uint8_t* image_digest; // optional additional SHA256 pin, never worker input
} ConfinedCodeSpec;
typedef struct ConfinedProcessLimits {
    uint32_t wall_ms;
    uint32_t cpu_ms;
    uint64_t memory_bytes;
    uint32_t output_bytes;
} ConfinedProcessLimits;
typedef enum ConfinedProcessStop {
    CONFINED_PROCESS_EXITED,
    CONFINED_PROCESS_WALL_LIMIT,
    CONFINED_PROCESS_CPU_LIMIT,
    CONFINED_PROCESS_MEMORY_LIMIT,
    CONFINED_PROCESS_OUTPUT_LIMIT,
    CONFINED_PROCESS_MONITOR_ERROR,
    CONFINED_PROCESS_CANCELLED
} ConfinedProcessStop;
typedef struct ConfinedProcessResult {
    ConfinedProcessStop reason;
    int exit_code;
    int signal;
    int system_error;
    uint64_t elapsed_ms;
    uint64_t peak_memory;
    uint64_t cpu_ms;
    char* output;
    size_t output_size;
    char* error_output;
    size_t error_size;
} ConfinedProcessResult;

// macOS mechanism. Other platforms return ENOTSUP. Strict signature/resource
// validation, exact app-sandbox-only entitlements and hardened runtime required.
// Either a publisher team or an exact image pin is mandatory. No user code runs.
ConfinedCode* confined_code_open(const ConfinedCodeSpec* spec);
void confined_code_close(ConfinedCode* code);
// Verified running host's canonical executable and certificate team. No env/PATH.
bool confined_code_self(char* executable, size_t capacity, char team[64]);

// Starts suspended, in a new session, with clean env and exactly fd 0..3. Static
// and kernel-selected child identities must agree before this returns. The host
// endpoint is taken exactly once; caller owns it. No ambient terminal is inherited.
ConfinedProcess* confined_process_spawn(ConfinedCode* code, const char* entry,
    const char* cwd, uint64_t session);
int confined_process_take_channel(ConfinedProcess* process);
void confined_process_close_channel(int descriptor);
bool confined_process_resume(ConfinedProcess* process);
// Thread-safe request; the monitor owns signalling/reaping to avoid PID races.
void confined_process_cancel(ConfinedProcess* process);
// Dedicated monitor thread; captures output, samples CPU/physical footprint and
// kills the process group on limits/errors. No untrusted output reaches a UI here.
bool confined_process_wait(ConfinedProcess* process, const ConfinedProcessLimits* limits,
    ConfinedProcessResult* result);
void confined_process_destroy(ConfinedProcess* process);
void confined_process_result_free(ConfinedProcessResult* result);
// Trusted worker bootstrap: irreversible kernel CPU/file/descriptor/process caps.
bool confined_process_worker_limits(void);
// Trusted terminal mechanism: bounded confirmation, using /dev/tty exclusively.
// Caller escapes any untrusted display text. No terminal or response enters IPC.
bool confined_process_confirm(const char* prompt, uint32_t timeout_ms);
bool confined_process_emit(int descriptor, const char* bytes, size_t size);

#ifdef __cplusplus
}
#endif
