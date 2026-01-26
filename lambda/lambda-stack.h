/**
 * @file lambda-stack.h
 * @brief Stack overflow protection for Lambda runtime
 * 
 * Provides fast inline stack pointer checking to detect and handle
 * stack overflow from infinite recursion gracefully.
 */

#ifndef LAMBDA_STACK_H
#define LAMBDA_STACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Thread-local stack bounds (initialized once per thread)
extern __thread uintptr_t _lambda_stack_limit;
extern __thread uintptr_t _lambda_stack_base;

// Stack safety margin (64KB reserved for cleanup/error handling)
#define LAMBDA_STACK_SAFETY_MARGIN (64 * 1024)

/**
 * Initialize stack bounds for current thread.
 * Must be called once at program/thread startup.
 */
void lambda_stack_init(void);

/**
 * Check if stack pointer is dangerously low.
 * This is designed to be as fast as possible (~3 instructions when inlined).
 * 
 * @return true if stack overflow is imminent, false if OK
 */
static inline bool lambda_stack_check(void) {
    uintptr_t sp;
    
#if defined(__x86_64__) || defined(_M_X64)
    // x86-64: read RSP register
    #if defined(_MSC_VER)
        // MSVC: use intrinsic
        sp = (uintptr_t)_AddressOfReturnAddress();
    #else
        // GCC/Clang: inline assembly
        __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    #endif
    
#elif defined(__aarch64__) || defined(_M_ARM64)
    // ARM64: read SP register
    #if defined(_MSC_VER)
        sp = (uintptr_t)_AddressOfReturnAddress();
    #else
        __asm__ volatile("mov %0, sp" : "=r"(sp));
    #endif
    
#elif defined(__i386__) || defined(_M_IX86)
    // x86-32: read ESP register
    #if defined(_MSC_VER)
        sp = (uintptr_t)_AddressOfReturnAddress();
    #else
        __asm__ volatile("mov %%esp, %0" : "=r"(sp));
    #endif

#else
    // Fallback: use local variable address (slightly less accurate but portable)
    char stack_var;
    sp = (uintptr_t)&stack_var;
#endif

    // Compare against cached limit
    return sp < _lambda_stack_limit;
}

/**
 * Report stack overflow error and return error item.
 * Called when lambda_stack_check() returns true.
 * 
 * @param func_name Name of the function where overflow was detected
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

// Macro for convenient stack check in transpiled code
// Returns ItemError if stack overflow detected
#define LAMBDA_STACK_CHECK(func_name) \
    if (lambda_stack_check()) { \
        lambda_stack_overflow_error(func_name); \
        return ItemError; \
    }

// Macro for void functions
#define LAMBDA_STACK_CHECK_VOID(func_name) \
    if (lambda_stack_check()) { \
        lambda_stack_overflow_error(func_name); \
        return; \
    }

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_STACK_H
