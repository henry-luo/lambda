/**
 * @file lambda-stack.h
 * @brief Stack overflow protection for Lambda runtime (Phase 2: Signal-Based)
 *
 * Phase 2 uses OS-level signal/exception handling for zero per-call overhead:
 *   - macOS/Linux: sigaltstack + sigaction(SIGSEGV)
 *   - Windows: SEH (EXCEPTION_STACK_OVERFLOW)
 *
 * When stack overflow occurs, the OS delivers a signal/exception, which is
 * caught by our handler running on an alternate signal stack. The handler
 * performs a non-local jump (siglongjmp) back to a recovery point set before
 * script execution begins.
 */

#ifndef LAMBDA_STACK_H
#define LAMBDA_STACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <signal.h>   // sig_atomic_t

#if defined(__APPLE__) || defined(__linux__)
#include <setjmp.h>
#elif defined(_WIN32)
#include <setjmp.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Thread-local stack bounds (used for diagnostics and fault-address check)
// ============================================================================
extern __thread uintptr_t _lambda_stack_limit;
extern __thread uintptr_t _lambda_stack_base;

// Stack safety margin (64KB reserved for cleanup/error handling)
#define LAMBDA_STACK_SAFETY_MARGIN (64 * 1024)

// Alternate signal stack size (64KB)
#define LAMBDA_ALT_STACK_SIZE (64 * 1024)

// JC23: room kept above the fault limit so a language-level stack-overflow
// error (RangeError construction, stack capture, unwinding) never itself
// reaches the guard page.
#define LAMBDA_STACK_THROW_HEADROOM (256 * 1024)

// JC23: default native budget for language-level recursion, measured from the
// thread's stack base. A thread smaller than the budget is bounded by its own
// fault limit plus the throw headroom instead.
#define LAMBDA_STACK_DEFAULT_BUDGET (32 * 1024 * 1024)

// ============================================================================
// Signal-based recovery (Phase 2)
// ============================================================================

// Flag set by the signal handler when stack overflow is detected
extern __thread volatile bool _lambda_stack_overflow_flag;

/**
 * Initialize stack overflow protection for the current thread.
 *
 * Phase 2: Installs signal handler (sigaltstack + SIGSEGV on Unix, SEH on Windows).
 * Also caches stack bounds for diagnostics and fault-address disambiguation.
 *
 * Must be called once per thread before executing user scripts.
 */
void lambda_stack_init(void);

/**
 * Release process-wide stack overflow protection resources.
 * Intended for orderly process shutdown after all script execution is done.
 */
void lambda_stack_cleanup(void);

/**
 * Report stack overflow error and set runtime error state.
 * Called from the recovery path after siglongjmp (not from the signal handler).
 *
 * @param func_name Name of the function context (may be NULL)
 */
void lambda_stack_overflow_error(const char* func_name);

/**
 * Set the process-wide native recursion budget in bytes (0 = default).
 * CLI policy: call before the first execution context binds its limit.
 */
void lambda_stack_set_budget(size_t bytes);

/**
 * The current thread's recoverable stack limit (JC23): the lowest native stack
 * address at which generated code and call kernels must still raise a
 * catchable stack-overflow error. Store it in `Context::stack_limit` on the
 * thread that executes the context.
 */
uintptr_t lambda_stack_recoverable_limit(void);

// Native entries ask this leaf before their first user-code instruction. The
// limit is already bound by the receiving Context; the helper only samples its
// own frame address and therefore cannot allocate or re-enter Lambda.
uint64_t lambda_stack_is_exhausted(uintptr_t stack_limit);

// Native RootFrame constructors cannot return an error to their caller. A
// reservation failure must leave through the armed execution recovery point
// rather than continue with null, non-rooting slots.
void lambda_root_frame_overflow_error(void);

// Current native stack position; the stack-overflow guards compare it against
// `Context::stack_limit` (JC23). The frame address is within one frame of SP,
// which the limit's throw headroom absorbs. A hand-written register-template
// asm read of SP is not used: it named no clobbered register, and inlined into
// the always_inline call kernel it corrupted a live register in debug builds
// (lib_mustache/lib_tabulator under test_js_gtest --full-mir).
static inline uintptr_t lambda_stack_pointer(void) {
#if defined(_MSC_VER)
    return (uintptr_t)_AddressOfReturnAddress();
#else
    return (uintptr_t)__builtin_frame_address(0);
#endif
}

/**
 * Get current stack usage in bytes.
 * Useful for debugging and profiling.
 *
 * @return Number of bytes of stack currently in use
 */
static inline size_t lambda_stack_usage(void) {
    uintptr_t sp = lambda_stack_pointer();
    if (_lambda_stack_base == 0) return 0;
    return (size_t)(_lambda_stack_base - sp);
}

/**
 * Get total stack size in bytes.
 *
 * @return Total stack size available
 */
static inline size_t lambda_stack_size(void) {
    if (_lambda_stack_base == 0 || _lambda_stack_limit == 0) return 0;
    return (size_t)(_lambda_stack_base - _lambda_stack_limit + LAMBDA_STACK_SAFETY_MARGIN);
}

/**
 * Check if stack protection is enabled (initialized).
 * @return true if stack protection is active
 */
static inline bool lambda_stack_enabled(void) {
    return _lambda_stack_limit != 0;
}

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_STACK_H
