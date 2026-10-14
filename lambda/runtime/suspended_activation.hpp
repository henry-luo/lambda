#pragma once

#include "durable_activation.hpp"
#include "activation.h"
#include "async.h"

struct JsInterpEnv;
struct JsInterpHostOps;

// The common durable part of generator and async execution. It owns exactly
// the outliving activation edges; each state machine keeps its own semantic
// tail (D5.1.1v2, D6.2.2v2; JSCU32).
struct JsSuspendedActivation : DurableActivation {
    Context* runtime_context = NULL;
    void* state_fn = NULL;
    Item ast_function = {};
    Item ast_arguments = {};
    // context-owned interpreter policy follows this body across every park.
    const JsInterpHostOps* ast_host = NULL;
    JsInterpEnv* ast_function_env = NULL;
    JsInterpEnv* ast_body_env = NULL;
    // Every body, of either tier, runs once on its own stackful activation,
    // parked in place at each yield/await; the carrier owns it weakly (RA1,
    // RA8). `body` is its entry.
    Activation* activation = NULL;
    ActivationEntry body = NULL;
    bool ast_initialized = false;
    // JSCU44: the `with` scopes open where the generator or async function was
    // created; its body enters this captured chain once, on its own stack.
    Item* with_env = NULL;
    int with_depth = 0;
};

struct JsGeneratorStateRecord : JsSuspendedActivation {
    bool done = false;
    bool started = false;
    bool executing = false;
    bool is_async = false;
    Item private_home_class = {};
    Item delegate = {};
    Item ast_this = {};
};


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
