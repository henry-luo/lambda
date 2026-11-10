#pragma once
#include "lambda/core/data.hpp"
#include "../lambda.hpp"
#include "context_capsule.h"

typedef struct Heap Heap;
class SchemaValidator;
class MarkEditor;
struct LambdaError;
struct LambdaScheduler;
struct Runtime;
typedef struct TemplateRegistry TemplateRegistry;

// One sealed MIR module is instantiated separately in every EvalContext.  The
// generated code may retain only the module id/slot constants; the mutable
// binding storage is reached through this context-owned slab. JS direct eval
// may grow `vars`; its MIR lowering reloads that pointer from the active
// context for every module-slot access.
typedef struct LambdaModuleState {
    Item* vars;
    uint64_t* var_payloads;
    NameId* property_keys;
    void* consts;
    void* type_list;
    uint32_t var_count;      // live module slots visible to generated code
    uint32_t var_capacity;   // root-range/storage capacity; may exceed var_count in REPL
    uint32_t property_key_count;
    uint32_t module_id;
    bool vars_registered;
    // The active-slab fast path checks this context-local key before using
    // the slab; cached code still resolves any mismatch through its unit id.
    uint32_t module_layout_id;
} LambdaModuleState;

// Runtime-facing scalar materializers are needed by native input adapters as
// well as generated code. Keep DateTime construction here because static input
// headers intentionally keep the public lambda.h ABI declarations minimal.
extern "C" Item push_k(DateTime dtval);



typedef struct EvalContext : Context {
    Heap* heap;
    Pool* ast_pool;
    NamePool* name_pool;        // name_pool for runtime-generated names
    void* type_info;  // meta info for the base types
    Item result; // final exec result
    mpd_context_t* decimal_ctx; // libmpdec context for decimal operations
    SchemaValidator* validator; // Schema validator for document validation

    // Error handling and stack trace support
    ArrayList* debug_info;      // function address → source mapping for stack traces
    const char* current_file;   // current source file (for error reporting)
    LambdaError* last_error;    // most recent runtime error (owned)
    LambdaScheduler* scheduler; // per-runtime cooperative task scheduler
    // Variadic calls may nest. The active list is execution state of this
    // context, never a thread-wide register shared by unrelated runtimes.
    List* current_vargs;
    MarkEditor* edit_editor;    // active editor for this context's edit session
    Input* edit_editor_input;   // input allocated with the editor's context
    // Runtime owns the long-lived execution context. TLS may borrow this
    // pointer while code runs, but it must never become the owner. Keep this
    // after heap so MIR's hot allocation offsets remain stable.
    Runtime* runtime;
    TemplateRegistry* template_registry; // view/edit registry for this isolate
    // Every language selects its context-owned module slab here. Generated
    // Lambda and JS MIR receive this Context directly and load the same selector.
    LambdaModuleState* active_module_state;
    // Indexed by sealed module id.  The table and every state are created at
    // module-instantiation boundaries; generated hot paths only load this
    // pointer and use ordinary owner-thread loads/stores in the selected slab.
    LambdaModuleState** module_states;
    uint32_t module_state_capacity;
    // JIT execution may hint its current slab without changing the semantic
    // active-module selector used by hosted and interpreter entry points.
    LambdaModuleState* jit_current_module_state;
    // Keep new shell bookkeeping at the tail: generated and native callers
    // depend on the established module-state offsets (D8.4.1).
    uint32_t execution_depth;
    // JSCU27: one directory for context-owned subsystem state. Reading a
    // capsule is an indexed load; the lifecycle contract travels with it.
    ContextCapsuleDirectory capsule_directory;
    // borrowing realm controllers share slabs; their temporary selection cannot change the allocator.
    Runtime* module_state_allocator;
} EvalContext;

// D3.4.3/v5: the evaluation context's runtime shape tree (a capsule of the
// Runtime's canonical EvalContext); NULL outside an evaluation.
Input* runtime_shape_tree(void);
// Drops that tree with its heap generation: after heap teardown, before the
// runtime name pool its NameIds index is released.
void runtime_shape_tree_release(EvalContext* owner);
// Impl_Map_Transition_Coverage P3: that tree's root for elements of one tag.
TypeElmt* runtime_shape_tree_element_root(const char* tag, size_t length, Target* ns);
// runtime-built element values share the context's tag/attribute shape tree.
TypeElmt* runtime_element_type(const char* tag, size_t length);
void runtime_element_put(Element* element, String* key, Item value);
