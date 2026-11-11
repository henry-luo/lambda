#pragma once

#include "durable_activation.hpp"
#include "activation.h"
#include "async.h"

struct JsInterpEnv;
struct JsInterpHostOps;
struct MvpLmdProgram;

// a closed MIR generator retains its resume state and traced iterator links.
struct JsCompactGeneratorFrame {
    MvpLmdProgram* program = NULL;
    MvpLmdProgram* resume_program = NULL;
    Item saved = {};
    uint64_t scalar_home = 0;
    Item pending = {};
    uint64_t pending_scalar_home = 0;
    Item parent = {}; // transient trampoline link, traced while a child is running
    int pending_operation = 0;
    uint8_t position = 0; // 0: start, 1: yielded, 2: delegate, 3: complete, 4: stackful delegate
    bool delegates = false;
};

// The common durable part of generator and async execution. It owns exactly
// the outliving activation edges; each state machine keeps its own semantic
// tail (D6.1.1, D7.2.2; JSCU32).
struct JsSuspendedActivation : DurableActivation {
    Context* runtime_context = NULL;
    void* state_fn = NULL;
    Item ast_function = {};
    // a promoted definition keeps its observable callee separate from the
    // immutable MIR implementation retained by this instance (D8.4.5).
    Item implementation = {};
    Item ast_arguments = {};
    // context-owned interpreter policy follows this body across every park.
    const JsInterpHostOps* ast_host = NULL;
    JsInterpEnv* ast_function_env = NULL;
    JsInterpEnv* ast_body_env = NULL;
    // stackful bodies run once on their activation and park in place; compact
    // MIR generators use the derived record's heap frame instead (D6.1.1v3).
    Activation* activation = NULL;
    ActivationEntry body = NULL;
    bool ast_initialized = false;
    // JSCU44: the `with` scopes open where the generator or async function was
    // created; its body enters this captured chain once, on its own stack.
    Item* with_env = NULL;
    int with_depth = 0;
};

struct JsGeneratorStateRecord : JsSuspendedActivation {
    JsCompactGeneratorFrame* compact = NULL;
    bool done = false;
    bool started = false;
    bool executing = false;
    bool is_async = false;
    Item private_home_class = {};
    Item delegate = {};
    Item ast_this = {};
    // the receiver can outlive its caller before a lazy first next (D6.1.3).
    uint64_t ast_this_scalar = 0;
};

static inline Item js_generator_implementation(const JsGeneratorStateRecord* state) {
    return state->implementation.item ? state->implementation : state->ast_function;
}


// One carrier layout and lifecycle for full JS and MVP MIR activations.
struct JsGeneratorMapCarrier { Map base; JsGeneratorStateRecord state; };
// only async generators allocate this tail; synchronous carriers keep their layout.
struct JsAsyncGeneratorMapCarrier {
    JsGeneratorMapCarrier generator;
    RuntimeJobQueue requests;
    Item request_storage;
    Item request;
    Item receiver;
    uint64_t receiver_scalar;
    uint32_t module_state_id;
    int await_kind;
    bool awaiting;
    bool driving;
};
struct JsIterData {
    Item source;
    int64_t index;
    int64_t length;
    uint8_t ordered_projection;
    uint8_t sequence_projection; // 0: values, 1: keys, 2: entries; independent of ordered cursor leases
};
struct JsIteratorMapCarrier { Map base; JsIterData payload; };
extern "C" void js_iterator_map_gc_trace(Map* map, gc_heap_t* gc);
extern "C" void js_iterator_map_heap_destroy(Map* map);
void js_suspended_activation_gc_trace(const JsSuspendedActivation* activation, gc_heap_t* gc);
extern "C" void js_generator_map_gc_trace(Map* map, gc_heap_t* gc);
extern "C" void js_generator_map_heap_destroy(Map* map);
