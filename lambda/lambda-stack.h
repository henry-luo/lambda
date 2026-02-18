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

// ============================================================================
// Signal-based recovery (Phase 2)
// ============================================================================

// Thread-local recovery jump buffer — set once before executing user code
// Usage: if (sigsetjmp(_lambda_recovery_point, 1)) { /* overflow occurred */ }
#if defined(__APPLE__) || defined(__linux__)
extern __thread sigjmp_buf _lambda_recovery_point;
#elif defined(_WIN32)
extern __thread jmp_buf _lambda_recovery_point;
#endif

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
 * Report stack overflow error and set runtime error state.
 * Called from the recovery path after siglongjmp (not from the signal handler).
 *
 * @param func_name Name of the function context (may be NULL)
 */
void lambda_stack_overflow_error(const char* func_name);

/**
 * Get current stack usage in bytes.
 * Useful for debugging and profiling.
 *
 * @return Number of bytes of stack currently in use
 */
static inline size_t lambda_stack_usage(void) {
    uintptr_t sp;

#if defined(__x86_64__) || defined(_M_X64)
    #if defined(_MSC_VER)
        sp = (uintptr_t)_AddressOfReturnAddress();
    #else
        __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #if defined(_MSC_VER)
        sp = (uintptr_t)_AddressOfReturnAddress();
    #else
        __asm__ volatile("mov %0, sp" : "=r"(sp));
    #endif
#else
    char stack_var;
    sp = (uintptr_t)&stack_var;
#endif

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
