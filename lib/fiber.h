#ifndef LAMBDA_LIB_FIBER_H
#define LAMBDA_LIB_FIBER_H

// Stackful-execution primitive: a reserved native stack with a low guard page,
// and a register-level switch between stacks. It knows nothing about the
// Lambda runtime; `lambda/runtime/activation.cpp` builds suspension on it
// (vibe/Lambda_Design_Runtime_Async.md RA1).
//
// The switch saves only the callee-saved register set of the platform ABI and
// the stack pointer, on the stack being left. It deliberately is not
// ucontext: no signal-mask syscall per switch.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FiberStack {
    void* reservation;   // start of the mapping (the guard page is first)
    size_t reserved;     // whole mapping, guard included
    uintptr_t low;       // lowest usable address (just above the guard)
    uintptr_t high;      // one past the highest usable address
    // Extra bytes reserved directly above `high` in the same mapping, for the
    // owner's own per-stack storage (none when zero).
    size_t tail_bytes;
} FiberStack;

typedef void (*FiberEntry)(void* arg);

// Reserve `usable_bytes` of lazily committed stack plus a guard page, and
// `tail_bytes` of further storage above the stack in the same mapping.
bool fiber_stack_reserve(FiberStack* stack, size_t usable_bytes, size_t tail_bytes);
void fiber_stack_release(FiberStack* stack);
// Return the committed pages of a stack that is not running to the OS, except
// `keep_bytes` at the high end (the part every activation touches again).
// if reclaim is false, the owner must reclaim the discarded range before reuse.
void fiber_stack_trim(FiberStack* stack, size_t keep_bytes, bool reclaim);

// Return the pages covering [addr, addr + len) to the OS while keeping the
// range reserved; their contents become undefined. Before writing the range
// again, call fiber_memory_reclaim.
void fiber_memory_discard(void* addr, size_t len);
void fiber_memory_reclaim(void* addr, size_t len);
size_t fiber_page_size(void);

// Prepare a stack so that the first fiber_switch to the returned stack pointer
// calls entry(arg). The entry must never return; it leaves by switching away.
void* fiber_stack_prime(FiberStack* stack, FiberEntry entry, void* arg);

// Save the running context's callee-saved state on its own stack, store that
// stack pointer in *save_sp, then continue the context saved at load_sp.
void fiber_switch(void** save_sp, void* load_sp);

// Run fn(arg) on another stack, below `stack_top`, and return its result on
// the caller's stack. The other stack must be idle (parked in fiber_switch)
// and fn must not switch fibers or jump out. Windows x64 runs fn in place:
// its stack probes read TIB bounds this call does not swap.
typedef void* (*FiberCall)(void* arg);
void* fiber_call_on(void* stack_top, FiberCall fn, void* arg);

#ifdef __cplusplus
}
#endif

#endif
