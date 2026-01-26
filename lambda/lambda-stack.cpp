/**
 * @file lambda-stack.cpp
 * @brief Stack overflow protection implementation
 */

#include "lambda-stack.h"
#include "lambda-error.h"
#include "../lib/log.h"
#include <cstdio>

#if defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

// External function from lambda-eval.cpp to set runtime error without stack trace
extern "C" void set_runtime_error_no_trace(LambdaErrorCode code, const char* message);

// Thread-local storage for stack bounds
__thread uintptr_t _lambda_stack_limit = 0;
__thread uintptr_t _lambda_stack_base = 0;

void lambda_stack_init(void) {
    // Only initialize once per thread
    if (_lambda_stack_limit != 0) return;
    
#if defined(__APPLE__)
    // macOS: use pthread APIs
    pthread_t self = pthread_self();
    void* stack_addr = pthread_get_stackaddr_np(self);
    size_t stack_size = pthread_get_stacksize_np(self);
    
    // On macOS, stack_addr is the TOP of the stack (highest address)
    // Stack grows downward, so limit is at the bottom
    _lambda_stack_base = (uintptr_t)stack_addr;
    _lambda_stack_limit = (uintptr_t)stack_addr - stack_size + LAMBDA_STACK_SAFETY_MARGIN;
    
#elif defined(__linux__)
    // Linux: use pthread_attr_getstack
    pthread_t self = pthread_self();
    pthread_attr_t attr;
    
    if (pthread_getattr_np(self, &attr) != 0) {
        // Fallback if getattr fails
        char stack_var;
        _lambda_stack_base = (uintptr_t)&stack_var;
        _lambda_stack_limit = _lambda_stack_base - (8 * 1024 * 1024) + LAMBDA_STACK_SAFETY_MARGIN;
        log_warn("Could not get stack attributes, using fallback");
        return;
    }
    
    void* stack_addr;
    size_t stack_size;
    pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    pthread_attr_destroy(&attr);
    
    // On Linux, stack_addr is the BOTTOM of the stack (lowest address)
    _lambda_stack_base = (uintptr_t)stack_addr + stack_size;
    _lambda_stack_limit = (uintptr_t)stack_addr + LAMBDA_STACK_SAFETY_MARGIN;
    
#elif defined(_WIN32)
    // Windows: use GetCurrentThreadStackLimits
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high);
    
    _lambda_stack_base = (uintptr_t)high;
    _lambda_stack_limit = (uintptr_t)low + LAMBDA_STACK_SAFETY_MARGIN;
    
#else
    // Conservative fallback for unknown platforms
    // Assume 8MB stack (common default), use current SP as reference
    char stack_var;
    _lambda_stack_base = (uintptr_t)&stack_var;
    _lambda_stack_limit = _lambda_stack_base - (8 * 1024 * 1024) + LAMBDA_STACK_SAFETY_MARGIN;
    log_warn("Unknown platform, using fallback stack detection");
#endif

    log_debug("Stack initialized: base=%p, limit=%p, available=%zu KB",
              (void*)_lambda_stack_base, 
              (void*)_lambda_stack_limit,
              (_lambda_stack_base - _lambda_stack_limit) / 1024);
}

void lambda_stack_overflow_error(const char* func_name) {
    // Log error with function name for debugging
    log_error("Stack overflow detected in function '%s' - possible infinite recursion",
              func_name ? func_name : "<unknown>");
    
    // Log stack statistics
    size_t usage = lambda_stack_usage();
    size_t total = lambda_stack_size();
    if (total > 0) {
        log_error("Stack usage: %zu KB / %zu KB (%.1f%%)",
                  usage / 1024,
                  total / 1024,
                  100.0 * usage / total);
    }
    
    // Create error message
    char message[256];
    snprintf(message, sizeof(message), 
             "Stack overflow in '%s' - likely infinite recursion (stack: %zuKB/%zuKB)",
             func_name ? func_name : "<unknown>",
             usage / 1024, total / 1024);
    
    // Set runtime error using the no-trace version (safe in low-stack conditions)
    set_runtime_error_no_trace(ERR_STACK_OVERFLOW, message);
}

// Wrapper function to get stack limit (for MIR compatibility)
extern "C" uintptr_t get_stack_limit(void) {
    return _lambda_stack_limit;
}
