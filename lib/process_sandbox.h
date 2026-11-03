#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
// macOS offline worker bootstrap. Runs before untrusted parsing or callbacks.
// No acquisition of user filesystem, preferences, network, process or named
// service authority. Existing file descriptors/mappings remain capabilities;
// the trusted worker bootstrap must validate its inherited handles separately.
// Existing incompatible sandboxes and unsupported platforms fail closed.
// Unix ptrace is rejected at the syscall filter; its TRACE_ME authorization
// otherwise consults the parent label. Kernel task-access port auditing remains.
bool process_sandbox_enter_offline(void);
// Experimental sealed-runtime loader: apply the fixed policy before dlopen and
// constructors. Caller admits/pins the canonical image and symbol; OS hardened
// library validation stays enabled. No retry or fallback after any failure.
void* process_sandbox_load_image(const char* image, const char* symbol);
// Kernel label readback is necessary, not policy authentication by itself.
// The launcher also verifies the exact trusted bootstrap image before resume.
bool process_sandbox_is_applied(int process_id);
#ifdef __cplusplus
}
#endif
