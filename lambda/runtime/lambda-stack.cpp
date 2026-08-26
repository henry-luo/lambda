/**
 * @file lambda-stack.cpp
 * @brief Stack overflow protection implementation (Phase 2: Signal-Based)
 *
 * Uses OS-level mechanisms for zero per-call overhead stack overflow detection:
 *   - macOS/Linux: sigaltstack + sigaction(SIGSEGV) with fault-address disambiguation
 *   - Windows: SEH (EXCEPTION_STACK_OVERFLOW)
 *
 * When the stack hits the guard page, the OS delivers a signal/exception.
 * Our handler verifies it's a true stack overflow (not a null-pointer deref),
 * then siglongjmp's back to a recovery point set before script execution.
 */

#include "lambda-stack.h"
#include "lambda-error.h"
#include "recovery_frame.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#include <pthread.h>
#include <signal.h>
#include <execinfo.h>
#elif defined(__linux__)
#include <pthread.h>
#include <signal.h>
#include <execinfo.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

extern "C" void* eval_context_tls_runtime(void);

// ============================================================================
// Thread-local state
// ============================================================================

// Stack bounds (used for diagnostics and fault-address disambiguation)
__thread uintptr_t _lambda_stack_limit = 0;
__thread uintptr_t _lambda_stack_base = 0;

__thread volatile bool _lambda_stack_overflow_flag = false;

// Track whether signal handler has been installed (process-wide, only once)
static volatile bool _signal_handler_installed = false;

#if defined(__APPLE__) || defined(__linux__)
// The signal-stack selection itself is per-thread. Preserve the host's prior
// selection so a worker can return to ASan or an embedding host intact.
static __thread void* _lambda_alt_stack_mem = NULL;
static __thread stack_t _lambda_alt_stack_previous = {};
static __thread bool _lambda_alt_stack_previous_valid = false;
#endif

// ============================================================================
// Stack bounds initialization (platform-specific)
// ============================================================================

static void init_stack_bounds(void) {
#if defined(__APPLE__)
    pthread_t self = pthread_self();
    void* stack_addr = pthread_get_stackaddr_np(self);
    size_t stack_size = pthread_get_stacksize_np(self);
    // macOS: stack_addr is the TOP (highest address), stack grows down
    _lambda_stack_base = (uintptr_t)stack_addr;
    _lambda_stack_limit = (uintptr_t)stack_addr - stack_size + LAMBDA_STACK_SAFETY_MARGIN;

#elif defined(__linux__)
    pthread_t self = pthread_self();
    pthread_attr_t attr;

    if (pthread_getattr_np(self, &attr) != 0) {
        char stack_var;
        _lambda_stack_base = (uintptr_t)&stack_var;
        _lambda_stack_limit = _lambda_stack_base - (8 * 1024 * 1024) + LAMBDA_STACK_SAFETY_MARGIN;
        log_warn("stack bounds: could not get stack attributes, using fallback");
        return;
    }

    void* stack_addr;
    size_t stack_size;
    pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    pthread_attr_destroy(&attr);
    // Linux: stack_addr is the BOTTOM (lowest address)
    _lambda_stack_base = (uintptr_t)stack_addr + stack_size;
    _lambda_stack_limit = (uintptr_t)stack_addr + LAMBDA_STACK_SAFETY_MARGIN;

#elif defined(_WIN32)
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high);
    _lambda_stack_base = (uintptr_t)high;
    uintptr_t jit_budget = 8 * 1024 * 1024;
    uintptr_t jit_limit = high > jit_budget ? (uintptr_t)high - jit_budget : (uintptr_t)low;
    uintptr_t os_limit = (uintptr_t)low + LAMBDA_STACK_SAFETY_MARGIN;
    // JIT frames have no unwind metadata, so reserve a recoverable runtime
    // budget instead of waiting for the PE guard page at the full stack limit.
    _lambda_stack_limit = jit_limit > os_limit ? jit_limit : os_limit;

#else
    char stack_var;
    _lambda_stack_base = (uintptr_t)&stack_var;
    _lambda_stack_limit = _lambda_stack_base - (8 * 1024 * 1024) + LAMBDA_STACK_SAFETY_MARGIN;
    log_warn("stack bounds: unknown platform, using fallback");
#endif

    log_debug("stack bounds: base=%p, limit=%p, available=%zu KB",
              (void*)_lambda_stack_base,
              (void*)_lambda_stack_limit,
              (_lambda_stack_base - _lambda_stack_limit) / 1024);
}

// ============================================================================
// Signal handler (macOS / Linux)
// ============================================================================

#if defined(__APPLE__) || defined(__linux__)

/**
 * Check if a fault address is near the stack guard region.
 * Returns true if it looks like a stack overflow, false if it's likely
 * a null-pointer dereference or other memory fault.
 */
static bool is_stack_overflow_fault(uintptr_t fault_addr) {
    // Reject addresses in the first 64KB — these are NULL pointer dereferences
    // (accessing a field at some offset from a NULL struct pointer), not stack overflows.
    if (fault_addr < 0x10000) {
        return false;
    }

    if (_lambda_stack_base == 0) {
        // stack bounds not initialized — can't tell, assume stack overflow
        return true;
    }

    // Compute the approximate stack bottom (lowest valid stack address)
    uintptr_t stack_bottom = _lambda_stack_base -
        (_lambda_stack_base - _lambda_stack_limit + LAMBDA_STACK_SAFETY_MARGIN);

    // Check if fault address is within a generous range around the stack bottom
    // The guard page is typically at or just below stack_bottom.
    // Use a 64KB window to account for large stack frames that skip over the guard.
    uintptr_t guard_window = 64 * 1024;
    if (fault_addr >= (stack_bottom > guard_window ? stack_bottom - guard_window : 0)
        && fault_addr <= stack_bottom + guard_window) {
        return true;
    }

    // Also check if fault_addr is just below our safety limit (deep in guard territory)
    // but still in the stack region. Don't catch addresses far from the stack.
    uintptr_t stack_size = _lambda_stack_base - _lambda_stack_limit + LAMBDA_STACK_SAFETY_MARGIN;
    uintptr_t stack_region_start = _lambda_stack_base > stack_size + guard_window
        ? _lambda_stack_base - stack_size - guard_window : 0;
    if (fault_addr < _lambda_stack_limit && fault_addr >= stack_region_start) {
        return true;
    }

    return false;
}

static void stack_overflow_signal_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;

    uintptr_t fault_addr = (uintptr_t)info->si_addr;

    // Check if this is actually a stack overflow (vs null-pointer deref, etc.)
    if (!is_stack_overflow_fault(fault_addr)) {
        // Not a stack overflow — restore default handler and re-raise. A
        // signal handler cannot safely log or allocate before fail-stop.
        signal(sig, SIG_DFL);
        raise(sig);
        return;  // unreachable
    }

    // It is a stack overflow. Select the nearest local-fault or execution
    // boundary. The
    // handler reads the TLS LIFO directly because logging or helper calls are
    // not safe on the alternate signal stack.
    LambdaRecoveryFrame* frame = lambda_recovery_frame_tls_top;
    LambdaRecoveryFrame* candidate = NULL;
    while (frame) {
        if (frame->signal_armed) {
            // An active transaction owns all of its inner local frames: a
            // stack landing cannot resume a half-initialized module or guest.
            if (frame->capabilities & LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER) {
                candidate = frame;
                break;
            }
            if (!candidate && (frame->capabilities &
                    (LAMBDA_RECOVERY_CAP_LOCAL_FAULT |
                     LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY))) {
                candidate = frame;
            }
        }
        frame = frame->previous;
    }
    if (!candidate || (candidate != lambda_recovery_frame_tls_top &&
            candidate->discarded_inner)) {
        // An unarmed target has no valid native activation to receive a jump.
        signal(sig, SIG_DFL);
        raise(sig);
        return;  // unreachable
    }

    if (candidate != lambda_recovery_frame_tls_top) {
        candidate->discarded_inner = lambda_recovery_frame_tls_top;
        lambda_recovery_frame_tls_top = candidate;
    }

    _lambda_stack_overflow_flag = true;
    candidate->signal_fault_reason = LAMBDA_FAULT_STACK_OVERFLOW;
    candidate->signal_armed = 0;
    siglongjmp(candidate->jump_buffer, 1);
}

static void install_signal_handler(void) {
    if (!_lambda_alt_stack_mem) {
        // The handler is process-wide, but every execution thread needs its
        // own alternate stack before a SA_ONSTACK delivery can be safe.
        void* alt_stack_mem = mem_alloc(LAMBDA_ALT_STACK_SIZE, MEM_CAT_SYSTEM);
        if (!alt_stack_mem) {
            log_error("stack init: failed to allocate alternate signal stack");
            return;
        }
        stack_t ss;
        ss.ss_sp = alt_stack_mem;
        ss.ss_size = LAMBDA_ALT_STACK_SIZE;
        ss.ss_flags = 0;
        if (sigaltstack(&ss, &_lambda_alt_stack_previous) != 0) {
            log_error("stack init: sigaltstack failed");
            mem_free(alt_stack_mem);
            return;
        }
        _lambda_alt_stack_previous_valid = true;
        _lambda_alt_stack_mem = alt_stack_mem;
    }
    if (_signal_handler_installed) return;

    // 2. Install SIGSEGV handler on the alternate stack
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = stack_overflow_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        log_error("stack init: sigaction(SIGSEGV) failed");
        if (_lambda_alt_stack_previous_valid) {
            sigaltstack(&_lambda_alt_stack_previous, NULL);
        }
        mem_free(_lambda_alt_stack_mem);
        _lambda_alt_stack_mem = NULL;
        _lambda_alt_stack_previous_valid = false;
        return;
    }

#if defined(__linux__)
    // Linux may also deliver SIGBUS for some stack faults
    if (sigaction(SIGBUS, &sa, NULL) != 0) {
        log_error("stack init: sigaction(SIGBUS) failed");
    }
#endif

    _signal_handler_installed = true;
    log_debug("stack init: signal-based overflow handler installed (alt stack=%zu KB)",
              LAMBDA_ALT_STACK_SIZE / 1024);
}

#endif // __APPLE__ || __linux__

// ============================================================================
// SEH handler (Windows)
// ============================================================================

#if defined(_WIN32)

static LONG WINAPI stack_overflow_seh_handler(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        LambdaRecoveryFrame* frame = lambda_recovery_frame_tls_top;
        LambdaRecoveryFrame* candidate = NULL;
        while (frame) {
            if (frame->signal_armed) {
                if (frame->capabilities & LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER) {
                    candidate = frame;
                    break;
                }
                if (!candidate && (frame->capabilities &
                        (LAMBDA_RECOVERY_CAP_LOCAL_FAULT |
                         LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY))) {
                    candidate = frame;
                }
            }
            frame = frame->previous;
        }
        if (!candidate || (candidate != lambda_recovery_frame_tls_top &&
                candidate->discarded_inner)) return EXCEPTION_CONTINUE_SEARCH;
        if (candidate != lambda_recovery_frame_tls_top) {
            candidate->discarded_inner = lambda_recovery_frame_tls_top;
            lambda_recovery_frame_tls_top = candidate;
        }
        _lambda_stack_overflow_flag = true;
        candidate->signal_fault_reason = LAMBDA_FAULT_STACK_OVERFLOW;
        candidate->signal_armed = 0;
        // Reset the stack overflow state so the thread can continue
        _resetstkoflw();
        longjmp(candidate->jump_buffer, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void install_signal_handler(void) {
    if (_signal_handler_installed) return;
    SetUnhandledExceptionFilter(stack_overflow_seh_handler);
    _signal_handler_installed = true;
    log_debug("stack init: SEH overflow handler installed");
}

#endif // _WIN32

// ============================================================================
// Public API
// ============================================================================

void lambda_stack_init(void) {
    // Initialize stack bounds (per-thread)
    if (_lambda_stack_limit == 0) {
        init_stack_bounds();
    }

    // Install signal/exception handler (process-wide, once)
    install_signal_handler();
}

void lambda_stack_cleanup(void) {
#if defined(__APPLE__) || defined(__linux__)
    if (!_signal_handler_installed && !_lambda_alt_stack_mem) return;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
#if defined(__linux__)
    sigaction(SIGBUS, &sa, NULL);
#endif

    if (_lambda_alt_stack_mem) {
        if (_lambda_alt_stack_previous_valid) {
            if (sigaltstack(&_lambda_alt_stack_previous, NULL) != 0) {
                log_error("stack cleanup: sigaltstack restore failed");
            }
        } else {
            stack_t disabled;
            memset(&disabled, 0, sizeof(disabled));
            disabled.ss_flags = SS_DISABLE;
            if (sigaltstack(&disabled, NULL) != 0) {
                log_error("stack cleanup: sigaltstack disable failed");
            }
        }
        mem_free(_lambda_alt_stack_mem);
        _lambda_alt_stack_mem = NULL;
        _lambda_alt_stack_previous_valid = false;
    }
    _signal_handler_installed = false;
    log_debug("stack cleanup: signal-based overflow handler released");
#elif defined(_WIN32)
    if (_signal_handler_installed) {
        SetUnhandledExceptionFilter(NULL);
        _signal_handler_installed = false;
    }
#endif
}

extern "C" void lambda_stack_overflow_error(const char* func_name) {
    (void)func_name;
    _lambda_stack_overflow_flag = true;
    // Generated side-stack and TCO guards run below local handler frames. They
    // must transfer the pre-reserved reason, never format an ordinary error
    // after the resource invariant has already failed.
    if (lambda_recovery_frame_raise_fault(LAMBDA_FAULT_STACK_OVERFLOW, ERR_OK)) return;
    lambda_recovery_publish_fault((Context*)eval_context_tls_runtime(),
        LAMBDA_FAULT_STACK_OVERFLOW, ERR_OK);
}

extern "C" void lambda_root_frame_overflow_error(void) {
    _lambda_stack_overflow_flag = true;
    // Continuing after a failed reservation would turn every Rooted home in
    // the frame into a null slot and make precise collection unsound.
    if (lambda_recovery_frame_raise_fault(
            LAMBDA_FAULT_SIDE_STACK_EXHAUSTION, ERR_OK)) return;
    log_error("native-root-frame: reservation failed without an armed recovery point");
    abort();
}
