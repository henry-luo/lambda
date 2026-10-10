#pragma once

// Activation: the one suspension mechanism shared by Lambda and LambdaJS in
// both execution tiers (vibe/Lambda_Design_Runtime_Async.md RA1-RA8).
//
// An activation runs a function on its own native stack with its own root
// and number side-stack segments. `activation_suspend` may be called at any
// depth — from interpreted code, from MIR code, or from a runtime helper —
// and returns when the activation is resumed. No tier transforms a function
// in order to suspend it.
//
// Ownership: the creator owns the Activation record and destroys it. A strong
// activation's root segment is traced as a root while it is not running (a
// Lambda task); a weak one is traced only through its owner's trace hook
// (`activation_trace`), so an unreachable owner lets it be collected.

#include "../lambda.h"
#include "lambda-error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Activation Activation;
typedef struct gc_heap gc_heap_t;

typedef enum ActivationStatus {
    ACTIVATION_NEW = 0,
    ACTIVATION_RUNNING,
    ACTIVATION_SUSPENDED,
    ACTIVATION_DONE,
} ActivationStatus;

// `arg` stays rooted in the activation's own segment for the entry's lifetime.
typedef Item (*ActivationEntry)(Activation* self, Item arg);

Activation* activation_create(ActivationEntry entry, Item arg, bool strong,
                              void* user);
// Run `activation` until it suspends or returns. `input` becomes the return
// value of the `activation_suspend` call it is parked in (ignored on a first
// resume). Resuming a running or finished activation is a no-op that returns
// its status.
ActivationStatus activation_resume(Activation* activation, Item input);
// The value passed to the pending `activation_suspend`, or the entry's result
// once DONE. A DONE value is no longer rooted by the activation: the caller
// stores it in a rooted home before allocating. Wide scalars borrow the record's
// home until the next resume or destruction; the caller must adopt them first.
Item activation_value(const Activation* activation);
// Non-NULL when the entry ended in a native fault caught by the activation's
// own execution boundary; the value is then the fault's error Item.
const LambdaFaultRecord* activation_fault(const Activation* activation);
ActivationStatus activation_status(const Activation* activation);
void* activation_user(const Activation* activation);
// Free the record. A parked activation is abandoned: its stack is released and
// no code on it runs again (RA7).
void activation_destroy(Activation* activation);

// Called inside an activation: park it, hand `value` to the resumer, and
// return the next resume input. Outside any activation, or beneath a native
// barrier, it parks nothing and returns an error Item (RA10).
Item activation_suspend(Item value);
// A native host frame (a module calling back into script) pins the
// activation whose stack holds it: suspending beneath one is a fault (RA10,
// D7.4.2). Enter and leave bracket the call-in; on the base stack they do
// nothing.
void activation_barrier_enter(void);
void activation_barrier_leave(void);

// RA6: run stack-hungry native work that cannot park (a MIR compile, a source
// parse) on the thread's base stack, beneath its parked frames, so activation
// stacks can stay small. On the base stack it simply calls fn. fn must not
// park, resume or create a resume of any activation; a fault in it lands on
// a boundary of its own on the base stack, sets *faulted and returns NULL.
typedef void* (*ActivationCall)(void* arg);
void* activation_call_on_base(ActivationCall fn, void* arg, bool* faulted);
// The activation running on this thread, or NULL on the thread's own stack.
Activation* activation_current(void);

// Return the pooled idle stacks to the OS (scheduler teardown).
void activation_release_pool(void);

// Owner-side tracing for weak activations.
void activation_trace(const Activation* activation, gc_heap_t* gc);
// Installed as the GC root visitor: marks every strong parked activation and
// every stack on the current resume chain other than the running one.
void activation_gc_visit_roots(gc_heap_t* gc);

// Per-stack state owned by another subsystem (the interpreters' frame chains).
// Hooks run on every switch: `save` captures the departing stack's state into
// its storage, `load` installs the arriving stack's, and `init` prepares the
// storage of a fresh activation. Register once per process, before the first
// activation is created.
typedef struct ActivationAmbientHook {
    size_t size;
    void (*init)(void* storage);
    void (*save)(void* storage);
    void (*load)(const void* storage);
} ActivationAmbientHook;
bool activation_register_ambient_hook(const ActivationAmbientHook* hook);

#ifdef __cplusplus
}
#endif
