#include "js_interp.hpp"

#include "js_interp_env.h"
#include "js_runtime_state.hpp"
#include "js_builtin_catalog.hpp"
#include "js_dom.h"
#include "js_event_loop.h"
#include "js_property_attrs.h"
#include "js_props.h"
#include "../runtime/gc/gc_heap.h"
#include "../runtime/heap_api.h"
#include "../runtime/module_registry.h"
#include "../runtime/runtime-state.h"
#include "../runtime/side_stack.h"
#include "../../lib/lambda_alloca.h"
#include "../../lib/log.h"
#include "../../lib/file.h"
#include "../../lib/mempool.h"
#include "../../lib/memtrack.h"

extern __thread EvalContext* context;
extern int js_dynamic_import_suppress_module_drain;
extern Item js_make_number(double value);
extern "C" Item bigint_from_string(const char* value, int length);
void jm_resolve_module_path(const char* base_file, const char* specifier,
    int spec_len, char* output, int output_size);
bool js_activate_runtime_name_pool(void);

enum JsInterpCompletionKind : uint8_t {
    JS_INTERP_NORMAL,
    JS_INTERP_RETURN,
    JS_INTERP_THROW,
    JS_INTERP_BREAK,
    JS_INTERP_CONTINUE,
    JS_INTERP_TAIL_CALL,
    JS_INTERP_YIELD,
    JS_INTERP_AWAIT,
};

struct JsInterpCompletion {
    JsInterpCompletionKind kind;
    Item value;
    const char* label;
    int label_len;
    // A self-tail call is carried back to its AST activation without adding a
    // native call frame. The argument array is immediately rooted by that
    // activation before another allocation can occur.
    Item tail_arguments = ItemNull;
    Item tail_this = ItemNull;
    // A delegated yield parks the outer AST activation while the shared
    // generator runtime advances the delegated iterator.
    bool yield_delegate = false;
};

struct JsInterpTailScratch {
    uint64_t* homes[2];
    int next_index;
};

struct JsInterpFrame {
    JsScript* script;
    JsInterpEnv* env;
    // `this` survives arbitrary nested evaluation; keep its canonical Item in
    // the side-root window rather than a native-stack copy.
    uint64_t* this_home;
    uint64_t* new_target_home;
    // The shared call kernel publishes the lexical [[HomeObject]] while the
    // body runs. Keep it alongside `this` so arrows and post-super fields use
    // the same runtime class capability rather than a walker-local stack.
    uint64_t* home_class_home;
    bool strict;
    const char* active_label;
    int active_label_len;
    JsFunction* active_function;
    // Parameter initializers have a distinct variable environment when the
    // formal list is non-simple; direct eval must not redeclare a parameter.
    bool in_parameter_initializer;
    // Reused only by a self-tail call in this activation. Alternating roots
    // preserve current wide scalar arguments while the next list is built.
    JsInterpTailScratch* tail_scratch;
    // A generator replays its AST from the durable activation and turns the
    // next not-yet-observed yield into a completion for the shared iterator.
    int64_t* generator_yield_seen;
    int64_t generator_yield_skip;
    Item generator_resume_input;
    Item generator_yield_values;
    // An injected throw/return can cross one or more yields in `finally`.
    // Replay it at its original suspension point until that abrupt completion
    // exits, rather than replacing it with the later next() input.
    int64_t generator_abrupt_resume_yield;
    Item generator_abrupt_resume_input;
    int64_t* async_await_seen;
    int64_t async_await_skip;
    Item async_resume_input;
    Item async_await_values;
    JsAstNode* async_root_statement_list;
    JsAstNode* async_resume_statement;
    JsAstNode** async_suspended_statement;
    bool* async_skip_completed_statements;
    JsGeneratorStateRecord* generator_state;
};

struct JsInterpReference {
    NameEntry* entry;
    JsInterpEnv* arguments_env;
    // Assignment/update callers provide these exact root slots. A property
    // reference must keep both operands live while evaluating its RHS.
    uint64_t* object_home;
    uint64_t* key_home;
    // A computed super reference snapshots [[GetSuperBase]] before converting
    // its property expression, which may mutate the receiver's prototype.
    uint64_t* super_base_home;
    // The key has already completed ToPropertyKey. Preserve the shared lane
    // classification so computed numeric AST members reach the same dense
    // array kernels as MIR without repeating coercion.
    JsPropertyLane property_lane;
    bool property;
    bool super_property;
    bool with_binding;
    bool with_lookup_completed;
    // PutValue uses the Reference resolved before its RHS ran. In strict code,
    // a later RHS-created global cannot make an unresolvable reference valid.
    bool unresolvable_binding;
    // A reference can resolve before direct eval introduces an inner `var`.
    // Its later PutValue must retain that original binding resolution.
    bool binding_uses_eval;
    // Destructuring evaluates computed target expressions before IteratorStep,
    // but defers their ToPropertyKey conversion until it performs PutValue.
    bool property_key_deferred;
};

struct JsInterpEnvRoot {
    JsInterpEnv* env;
    bool registered;

    JsInterpEnvRoot() : env(NULL), registered(false) {}
    explicit JsInterpEnvRoot(JsInterpEnv* value)
        : env(value), registered(value && heap_try_register_gc_object_root(value)) {}
    ~JsInterpEnvRoot() {
        if (registered) heap_unregister_gc_object_root(env);
    }
    void replace_with(JsInterpEnvRoot* replacement) {
        if (!replacement) return;
        if (registered) heap_unregister_gc_object_root(env);
        env = replacement->env;
        registered = replacement->registered;
        replacement->registered = false;
    }
    void adopt_from(JsInterpEnvRoot* replacement) {
        if (!replacement) return;
        if (registered) heap_unregister_gc_object_root(env);
        env = replacement->env;
        registered = replacement->registered;
        replacement->env = NULL;
        replacement->registered = false;
    }
    JsInterpEnvRoot(const JsInterpEnvRoot&) = delete;
    JsInterpEnvRoot& operator=(const JsInterpEnvRoot&) = delete;
};

struct JsInterpGeneratorLoopContinuation {
    JsAstNode* loop;
    JsInterpEnv* env;
    Item iterator;
    Item for_in_object;
    bool is_for_of;
    struct JsInterpGeneratorLoopContinuation* next;
};

struct JsInterpGeneratorListContinuation {
    JsAstNode* statements;
    JsAstNode* next_statement;
    JsInterpEnv* env;
    // Yield-ledger position at the point where `next_statement` begins.
    int64_t yield_count_before_next_statement;
    // A nested yield must re-enter its containing statement with the replay
    // ledger, while a terminal yield can continue at the following statement.
    bool replay_current_statement;
    // Only the innermost list observes a resumed throw or return. Outer
    // cursors merely restore the structural path to that list.
    bool receives_resume_input;
    struct JsInterpGeneratorListContinuation* next;
};

struct JsInterpGeneratorArrayBindingContinuation {
    JsAstNode* pattern;
    JsAstNode* element;
    Item iterator;
    Item value;
    Item reference_object;
    Item reference_key;
    JsPropertyLane reference_lane;
    bool iterator_done;
    bool value_ready;
    bool has_reference;
    bool reference_super_property;
    bool reference_key_deferred;
    struct JsInterpGeneratorArrayBindingContinuation* next;
};

void js_interp_generator_clear_continuations(JsGeneratorStateRecord* state) {
    if (!state) return;
    JsInterpGeneratorLoopContinuation* loop = state->ast_loop_continuations;
    while (loop) {
        JsInterpGeneratorLoopContinuation* next = loop->next;
        mem_free(loop);
        loop = next;
    }
    state->ast_loop_continuations = NULL;
    JsInterpGeneratorListContinuation* list = state->ast_list_continuation;
    while (list) {
        JsInterpGeneratorListContinuation* next = list->next;
        mem_free(list);
        list = next;
    }
    state->ast_list_continuation = NULL;
    JsInterpGeneratorArrayBindingContinuation* array =
        state->ast_array_binding_continuations;
    while (array) {
        JsInterpGeneratorArrayBindingContinuation* next = array->next;
        mem_free(array);
        array = next;
    }
    state->ast_array_binding_continuations = NULL;
    state->ast_resumable_loop_active = false;
}

void js_interp_generator_trace_continuations(JsGeneratorStateRecord* state,
        gc_heap_t* gc) {
    if (!state || !gc) return;
    for (JsInterpGeneratorLoopContinuation* loop = state->ast_loop_continuations;
            loop; loop = loop->next) {
        gc_mark_item(gc, loop->iterator.item);
        gc_mark_item(gc, loop->for_in_object.item);
        if (loop->env) gc_mark_object_ptr(gc, loop->env);
    }
    for (JsInterpGeneratorListContinuation* list = state->ast_list_continuation;
            list; list = list->next) {
        if (list->env) gc_mark_object_ptr(gc, list->env);
    }
    for (JsInterpGeneratorArrayBindingContinuation* array =
            state->ast_array_binding_continuations; array; array = array->next) {
        gc_mark_item(gc, array->iterator.item);
        gc_mark_item(gc, array->value.item);
        if (array->has_reference) {
            gc_mark_item(gc, array->reference_object.item);
            gc_mark_item(gc, array->reference_key.item);
        }
    }
}

static JsInterpGeneratorArrayBindingContinuation*
js_interp_generator_find_array_binding(JsInterpFrame* frame, JsAstNode* pattern) {
    JsGeneratorStateRecord* state = frame ? frame->generator_state : NULL;
    if (!state) return NULL;
    for (JsInterpGeneratorArrayBindingContinuation* current =
            state->ast_array_binding_continuations; current; current = current->next) {
        if (current->pattern == pattern) return current;
    }
    return NULL;
}

static bool js_interp_generator_suspend_array_binding(JsInterpFrame* frame,
        JsAstNode* pattern, JsAstNode* element, Item iterator, bool iterator_done,
        Item value, bool value_ready, const JsInterpReference* reference) {
    JsGeneratorStateRecord* state = frame ? frame->generator_state : NULL;
    if (!state) return false;
    JsInterpGeneratorArrayBindingContinuation* continuation =
        js_interp_generator_find_array_binding(frame, pattern);
    if (!continuation) {
        continuation = (JsInterpGeneratorArrayBindingContinuation*)mem_calloc(1,
            sizeof(*continuation), MEM_CAT_JS_RUNTIME);
        if (!continuation) return false;
        continuation->pattern = pattern;
        continuation->next = state->ast_array_binding_continuations;
        state->ast_array_binding_continuations = continuation;
    }
    continuation->element = element;
    continuation->iterator = iterator;
    continuation->iterator_done = iterator_done;
    continuation->value = value;
    continuation->value_ready = value_ready;
    continuation->has_reference = reference && reference->property;
    if (continuation->has_reference) {
        continuation->reference_object = reference->object_home
            ? (Item){.item = *reference->object_home} : ItemNull;
        continuation->reference_key = reference->key_home
            ? (Item){.item = *reference->key_home} : ItemNull;
        continuation->reference_lane = reference->property_lane;
        continuation->reference_super_property = reference->super_property;
        continuation->reference_key_deferred = reference->property_key_deferred;
    } else {
        continuation->reference_object = ItemNull;
        continuation->reference_key = ItemNull;
        continuation->reference_super_property = false;
        continuation->reference_key_deferred = false;
    }
    return true;
}

static void js_interp_generator_clear_array_binding(JsInterpFrame* frame,
        JsAstNode* pattern) {
    JsGeneratorStateRecord* state = frame ? frame->generator_state : NULL;
    if (!state) return;
    JsInterpGeneratorArrayBindingContinuation** link =
        &state->ast_array_binding_continuations;
    while (*link) {
        if ((*link)->pattern == pattern) {
            JsInterpGeneratorArrayBindingContinuation* removed = *link;
            *link = removed->next;
            mem_free(removed);
            return;
        }
        link = &(*link)->next;
    }
}

static JsInterpGeneratorLoopContinuation* js_interp_generator_find_loop(
        JsInterpFrame* frame, JsAstNode* loop) {
    JsGeneratorStateRecord* state = frame ? frame->generator_state : NULL;
    if (!state || !state->ast_resumable_loop_active) return NULL;
    for (JsInterpGeneratorLoopContinuation* current = state->ast_loop_continuations;
            current; current = current->next) {
        if (current->loop == loop) return current;
    }
    return NULL;
}

static JsInterpGeneratorListContinuation* js_interp_generator_find_list(
        JsGeneratorStateRecord* state, JsAstNode* statements) {
    if (!state) return NULL;
    for (JsInterpGeneratorListContinuation* current = state->ast_list_continuation;
            current; current = current->next) {
        if (current->statements == statements) return current;
    }
    return NULL;
}

static bool js_interp_generator_has_list_resume(JsInterpFrame* frame,
        JsAstNode* statements) {
    JsGeneratorStateRecord* state = frame ? frame->generator_state : NULL;
    // A terminal yield has a list cursor even outside a loop. Requiring a
    // loop continuation here replays prior statements on every next/return.
    return state && state->ast_list_continuation &&
        state->ast_list_continuation->statements == statements;
}

static JsInterpEnv* js_interp_generator_list_resume_env(JsInterpFrame* frame,
        JsAstNode* statements) {
    return js_interp_generator_has_list_resume(frame, statements)
        ? frame->generator_state->ast_list_continuation->env : NULL;
}

static bool js_interp_generator_list_throw_requires_replay(
        JsGeneratorStateRecord* state) {
    if (!state) return false;
    for (JsInterpGeneratorListContinuation* current = state->ast_list_continuation;
            current; current = current->next) {
        // A throw caught by a nested try can suspend again in its handler.
        // List cursors do not retain try/catch selection, so replaying the
        // owning try is required to keep later next() calls in that handler.
        if (current->replay_current_statement && current->next_statement &&
                current->next_statement->node_type == AST_NODE_TRY_STAM) {
            return true;
        }
    }
    return false;
}

static void js_interp_generator_remove_loop(JsInterpFrame* frame,
        JsAstNode* loop) {
    JsGeneratorStateRecord* state = frame ? frame->generator_state : NULL;
    if (!state) return;
    JsInterpGeneratorLoopContinuation** link = &state->ast_loop_continuations;
    while (*link) {
        if ((*link)->loop == loop) {
            JsInterpGeneratorLoopContinuation* removed = *link;
            *link = removed->next;
            mem_free(removed);
            return;
        }
        link = &(*link)->next;
    }
}

static bool js_interp_generator_suspend_loop(JsInterpFrame* frame,
        JsAstNode* loop, JsInterpEnv* env, Item iterator, Item for_in_object,
        bool is_for_of) {
    JsGeneratorStateRecord* state = frame ? frame->generator_state : NULL;
    if (!state || !state->ast_list_continuation) return false;
    JsInterpGeneratorLoopContinuation* existing = js_interp_generator_find_loop(frame,
        loop);
    if (existing) return true;
    JsInterpGeneratorLoopContinuation* continuation =
        (JsInterpGeneratorLoopContinuation*)mem_calloc(1, sizeof(*continuation),
            MEM_CAT_JS_RUNTIME);
    if (!continuation) return false;
    continuation->loop = loop;
    continuation->env = env;
    continuation->iterator = iterator;
    continuation->for_in_object = for_in_object;
    continuation->is_for_of = is_for_of;
    continuation->next = state->ast_loop_continuations;
    state->ast_loop_continuations = continuation;
    state->ast_resumable_loop_active = true;
    return true;
}

static JsInterpCompletion js_interp_normal(Item value) {
    return {JS_INTERP_NORMAL, value, NULL, 0};
}

static JsInterpCompletion js_interp_throw(Item value) {
    return {JS_INTERP_THROW, value, NULL, 0};
}

static bool js_interp_completion_targets_active_label(
        const JsInterpCompletion* completion, const JsInterpFrame* frame) {
    if (!completion || completion->label_len == 0) return true;
    return frame && frame->active_label &&
        completion->label_len == frame->active_label_len &&
        memcmp(completion->label, frame->active_label,
            (size_t)completion->label_len) == 0;
}

// Capturing an arrow or entering direct eval must retain a derived
// constructor's uninitialized `this`; reading `this` must resolve it instead.
static Item js_interp_frame_this_binding(const JsInterpFrame* frame) {
    return frame && frame->this_home ? (Item){.item = *frame->this_home}
        : js_get_lexical_this_binding();
}

static Item js_interp_frame_this(const JsInterpFrame* frame) {
    return js_resolve_lexical_this(js_interp_frame_this_binding(frame));
}

static Item js_interp_frame_new_target(const JsInterpFrame* frame) {
    if (!frame || !frame->new_target_home || *frame->new_target_home == 0) {
        return make_js_undefined();
    }
    return (Item){.item = *frame->new_target_home};
}

static Item js_interp_frame_home_class(const JsInterpFrame* frame) {
    return frame && frame->home_class_home
        ? (Item){.item = *frame->home_class_home} : ItemNull;
}

struct JsInterpCurrentFileScope {
    const char* previous;

    explicit JsInterpCurrentFileScope(const char* filename)
        : previous(context ? context->current_file : NULL) {
        if (context) context->current_file = filename;
    }
    ~JsInterpCurrentFileScope() {
        if (context) context->current_file = previous;
    }
    JsInterpCurrentFileScope(const JsInterpCurrentFileScope&) = delete;
    JsInterpCurrentFileScope& operator=(const JsInterpCurrentFileScope&) = delete;
};

struct JsInterpModuleStateScope {
    uint32_t previous;

    JsInterpModuleStateScope() : previous(js_get_active_module_state_id()) {}
    ~JsInterpModuleStateScope() {
        if (previous != UINT32_MAX) js_set_active_module_state_id(previous);
    }
    JsInterpModuleStateScope(const JsInterpModuleStateScope&) = delete;
    JsInterpModuleStateScope& operator=(const JsInterpModuleStateScope&) = delete;
};

struct JsInterpModuleNamespaceScope {
    Item previous;
    bool active;

    explicit JsInterpModuleNamespaceScope(JsScript* script)
        : previous(ItemNull), active(false) {
        if (!script || !script->is_es_module || !script->reference) return;
        Item namespace_obj = js_module_get(js_make_string(script->reference));
        if (get_type_id(namespace_obj) == LMD_TYPE_NULL) return;
        previous = js_set_active_module_namespace(namespace_obj);
        active = true;
    }
    ~JsInterpModuleNamespaceScope() {
        if (active) js_set_active_module_namespace(previous);
    }
    JsInterpModuleNamespaceScope(const JsInterpModuleNamespaceScope&) = delete;
    JsInterpModuleNamespaceScope& operator=(const JsInterpModuleNamespaceScope&) = delete;
};

struct JsInterpExecutionScope {
    bool outermost;

    JsInterpExecutionScope() : outermost(false) {
        outermost = js_runtime_state.ast_interpreter.execution_depth == 0;
        js_runtime_state.ast_interpreter.execution_depth++;
    }
    ~JsInterpExecutionScope() {
        if (js_runtime_state.ast_interpreter.execution_depth > 0) {
            js_runtime_state.ast_interpreter.execution_depth--;
        }
    }
    bool should_initialize_event_loop() const {
        return outermost && !js_runtime_state.event_loop.callback_running;
    }
    JsInterpExecutionScope(const JsInterpExecutionScope&) = delete;
    JsInterpExecutionScope& operator=(const JsInterpExecutionScope&) = delete;
};

static Item js_interp_super_this(JsInterpFrame* frame) {
    Item value = js_interp_frame_this(frame);
    if (value.item != ITEM_JS_TDZ) return value;
    return js_throw_reference_error(js_make_string(
        "Must call super constructor before accessing 'this'"));
}

static Item js_interp_reference_object(const JsInterpReference* reference) {
    return reference && reference->object_home
        ? (Item){.item = *reference->object_home} : ItemNull;
}

static Item js_interp_reference_key(const JsInterpReference* reference) {
    return reference && reference->key_home
        ? (Item){.item = *reference->key_home} : ItemNull;
}

static bool js_interp_name_equals(const String* name, const char* chars) {
    if (!name || !chars) return false;
    size_t length = strlen(chars);
    return name->len == length && memcmp(name->chars, chars, length) == 0;
}

static bool js_interp_member_uses_super(const JsMemberNode* member) {
    return member && member->object && member->object->node_type == AST_NODE_IDENT &&
        js_interp_name_equals(((JsIdentifierNode*)member->object)->name, "super");
}

static Item js_interp_name_key(const String* name) {
    return name ? (Item){.item = s2it((String*)name)} : ItemNull;
}

static Item js_interp_property_key_value(Item value) {
    // Proxy traps observe a Symbol itself, not its internal NameRecord key.
    return js_key_is_symbol_c(value) ? value : js_to_property_key(value);
}

static bool js_interp_name_matches(const String* left, const String* right) {
    return left && right && left->len == right->len &&
        memcmp(left->chars, right->chars, left->len) == 0;
}

static JsInterpImportBinding* js_interp_import_binding(JsScript* script,
        String* local_name) {
    for (JsInterpImportBinding* binding = script ? script->interp_imports : NULL;
            binding; binding = binding->next) {
        if (js_interp_name_matches(binding->local_name, local_name)) return binding;
    }
    return NULL;
}

static JsInterpExportBinding* js_interp_add_reexport_binding(JsScript* script,
        String* local_name, String* export_name, String* source) {
    if (!script || !script->pool || !script->name_pool || !local_name ||
            !export_name || !source) return NULL;
    for (JsInterpExportBinding* binding = script->interp_exports; binding;
            binding = binding->next) {
        if (binding->source && js_interp_name_matches(binding->source, source) &&
                js_interp_name_matches(binding->local_name, local_name) &&
                js_interp_name_matches(binding->export_name, export_name)) {
            return binding;
        }
    }
    JsInterpExportBinding* binding = (JsInterpExportBinding*)pool_calloc(script->pool,
        sizeof(JsInterpExportBinding));
    if (!binding) return NULL;
    binding->local_name = name_pool_create_len(script->name_pool, local_name->chars,
        local_name->len);
    binding->export_name = name_pool_create_len(script->name_pool, export_name->chars,
        export_name->len);
    binding->source = name_pool_create_len(script->name_pool, source->chars, source->len);
    if (!binding->local_name || !binding->export_name || !binding->source) return NULL;
    binding->star_export = true;
    binding->next = script->interp_exports;
    script->interp_exports = binding;
    return binding;
}

static bool js_interp_has_explicit_export(JsScript* script, String* export_name) {
    for (JsInterpExportBinding* binding = script ? script->interp_exports : NULL;
            binding; binding = binding->next) {
        if (!binding->star_export && js_interp_name_matches(binding->export_name,
                export_name)) return true;
    }
    return false;
}

static JsScript* js_interp_script_for_reference(Runtime* runtime,
        const char* reference) {
    if (!runtime || !runtime->scripts || !reference) return NULL;
    for (int index = 0; index < runtime->scripts->length; index++) {
        Script* candidate = (Script*)runtime->scripts->data[index];
        if (candidate && candidate->profile == &js_profile && candidate->reference &&
                strcmp(candidate->reference, reference) == 0) {
            return (JsScript*)candidate;
        }
    }
    return NULL;
}

static void js_interp_propagate_reexports(Runtime* runtime, const char* source_ref,
        String* source_name, Item value, int depth) {
    if (!runtime || !source_ref || !source_name || depth > 64) return;
    for (int index = 0; runtime->scripts && index < runtime->scripts->length; index++) {
        JsScript* candidate = (JsScript*)runtime->scripts->data[index];
        if (!candidate || candidate->profile != &js_profile || !candidate->is_es_module ||
                !candidate->reference) continue;
        ModuleDescriptor* module = module_get_for_runtime(runtime, candidate->reference);
        if (!module) continue;
        for (JsInterpExportBinding* binding = candidate->interp_exports;
                binding; binding = binding->next) {
            if (!binding->source || !js_interp_name_matches(binding->local_name, source_name)) {
                continue;
            }
            char resolved[512];
            jm_resolve_module_path(candidate->reference, binding->source->chars,
                (int)binding->source->len, resolved, (int)sizeof(resolved));
            if (strcmp(resolved, source_ref) != 0) continue;
            js_set_key_default(module->namespace_obj,
                js_interp_name_key(binding->export_name), value);
            js_interp_propagate_reexports(runtime, candidate->reference,
                binding->export_name, value, depth + 1);
        }
    }
}

static Item js_interp_read_module_export(Runtime* runtime, const char* reference,
        String* export_name, int depth) {
    if (!runtime || !reference || !export_name || depth > 64) return ItemError;
    Item namespace_obj = js_module_get(js_make_string(reference));
    if (get_type_id(namespace_obj) == LMD_TYPE_NULL) {
        return js_throw_reference_error(js_make_string("imported module is unavailable"));
    }
    JsScript* source_script = js_interp_script_for_reference(runtime, reference);
    for (JsInterpExportBinding* binding = source_script
            ? source_script->interp_exports : NULL; binding; binding = binding->next) {
        if (!binding->source || !js_interp_name_matches(binding->export_name, export_name)) {
            continue;
        }
        char resolved[512];
        jm_resolve_module_path(reference, binding->source->chars,
            (int)binding->source->len, resolved, (int)sizeof(resolved));
        if (binding->namespace_export) return js_module_get(js_make_string(resolved));
        return js_interp_read_module_export(runtime, resolved, binding->local_name,
            depth + 1);
    }
    Item has_export = js_has_own_property(namespace_obj, js_interp_name_key(export_name));
    if (item_is_error(has_export)) return has_export;
    if (!js_is_truthy(has_export)) {
        return js_throw_reference_error(js_make_string("imported binding is uninitialized"));
    }
    return js_get_key_default(namespace_obj, js_interp_name_key(export_name));
}

static Item js_interp_read_import_binding(JsScript* script,
        JsInterpImportBinding* binding) {
    if (!script || !binding) return ItemError;
    char resolved[512];
    jm_resolve_module_path(script->reference, binding->source->chars,
        (int)binding->source->len, resolved, (int)sizeof(resolved));
    Item namespace_obj = js_module_get(js_make_string(resolved));
    if (get_type_id(namespace_obj) == LMD_TYPE_NULL) {
        return js_throw_reference_error(js_make_string("imported module is unavailable"));
    }
    return binding->namespace_import ? namespace_obj : js_interp_read_module_export(
        context ? context->runtime : NULL, resolved, binding->export_name, 0);
}

static void js_interp_publish_export_bindings(JsInterpFrame* frame,
        String* local_name, Item value) {
    if (!frame || !frame->script || !frame->script->is_es_module || !local_name) return;
    ModuleDescriptor* module = module_get_for_runtime(context ? context->runtime : NULL,
        frame->script->reference);
    if (!module) return;
    Item namespace_obj = module->namespace_obj;
    for (JsInterpExportBinding* binding = frame->script->interp_exports;
            binding; binding = binding->next) {
        if (!binding->source && js_interp_name_matches(binding->local_name, local_name)) {
            js_set_key_default(namespace_obj, js_interp_name_key(binding->export_name), value);
            js_interp_propagate_reexports(context ? context->runtime : NULL,
                frame->script->reference, binding->export_name, value, 0);
        }
    }
}

static Item js_interp_publish_export_value(JsInterpFrame* frame,
        Item namespace_obj, String* export_name, Item value) {
    Item stored = js_set_key_default(namespace_obj, js_interp_name_key(export_name), value);
    if (item_is_error(stored)) return stored;
    js_interp_propagate_reexports(context ? context->runtime : NULL,
        frame && frame->script ? frame->script->reference : NULL, export_name, value, 0);
    return stored;
}

static bool js_interp_private_source_name(Item key) {
    String* name = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
    return name && name->len > 1 && name->chars[0] == '#';
}

static bool js_interp_private_key(Item key) {
    String* name = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
    return name && property_key_requires_identity(name) &&
        property_key_kind(name) == NAME_KEY_PRIVATE;
}

static int js_interp_scope_slot_count(NameScope* scope) {
    if (!scope) return 0;
    int count = 0;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        entry->slot = count++;
        entry->storage_assigned = true;
    }
    return count;
}

static bool js_interp_scope_needs_environment(NameScope* scope) {
    return scope && scope->first;
}

static JsInterpEnv* js_interp_env_create(NameScope* scope, JsInterpEnv* outer) {
    if (!context || !context->heap || !context->heap->gc) return NULL;
    int count = js_interp_scope_slot_count(scope);
    // Each durable slot owns a companion scalar payload, matching the common
    // module/closure storage contract (D5.3).  A raw Item copy would retain a
    // caller's float home across a GC or async suspension.
    size_t words = count > 0 ? (size_t)count * 2 : 1;
    size_t size = offsetof(JsInterpEnv, slots) + words * sizeof(uint64_t);
    JsInterpEnv* env = (JsInterpEnv*)gc_heap_calloc(context->heap->gc, size,
        GC_TYPE_JS_INTERP_ENV);
    if (!env) return NULL;
    env->outer = outer;
    env->scope = scope;
    env->slot_count = (uint32_t)count;
    return env;
}

static JsInterpEnv* js_interp_env_clone(JsInterpEnv* source) {
    if (!source) return NULL;
    JsInterpEnv* copy = js_interp_env_create(source->scope, source->outer);
    if (!copy || copy->slot_count != source->slot_count) return NULL;
    copy->arguments_object = source->arguments_object;
    copy->private_home_class = source->private_home_class;
    copy->private_bindings = source->private_bindings;
    copy->eval_bindings = source->eval_bindings;
    copy->lexical_this = source->lexical_this;
    copy->function_node = source->function_node;
    copy->arguments_are_mapped = source->arguments_are_mapped;
    copy->has_lexical_this = source->has_lexical_this;
    if (copy->slot_count) {
        memcpy(copy->slots, source->slots,
            (size_t)copy->slot_count * 2 * sizeof(uint64_t));
    }
    return copy;
}

static bool js_interp_loop_scope_owns_entry(NameScope* scope, NameEntry* entry) {
    if (!scope || !entry) return false;
    for (NameEntry* candidate = scope->first; candidate; candidate = candidate->next) {
        if (candidate == entry) return true;
    }
    return false;
}

static bool js_interp_function_node(JsAstNode* node) {
    return node && (node->node_type == AST_NODE_FUNC ||
        node->node_type == AST_NODE_FUNC_EXPR ||
        node->node_type == AST_NODE_ARROW_FUNC ||
        node->node_type == JS_AST_NODE_METHOD_DEFINITION);
}

struct JsInterpLoopCaptureProbe {
    NameScope* loop_scope;
    int function_depth;
    bool captures_loop_lexical;
};

static void js_interp_probe_loop_capture(JsAstNode* node,
    JsInterpLoopCaptureProbe* probe);

static void js_interp_probe_loop_capture_child(JsAstNode* child, void* opaque) {
    js_interp_probe_loop_capture(child, (JsInterpLoopCaptureProbe*)opaque);
}

static void js_interp_probe_loop_capture(JsAstNode* node,
        JsInterpLoopCaptureProbe* probe) {
    if (!node || !probe || probe->captures_loop_lexical) return;
    if (node->node_type == AST_NODE_IDENT && probe->function_depth > 0) {
        JsIdentifierNode* identifier = (JsIdentifierNode*)node;
        if (js_interp_loop_scope_owns_entry(probe->loop_scope, identifier->entry)) {
            probe->captures_loop_lexical = true;
        }
        return;
    }
    if (js_interp_function_node(node)) {
        JsFunctionNode* function = (JsFunctionNode*)node;
        // Direct eval can publish a closure over a loop binding that does not
        // appear as an identifier in its enclosing AST.
        if (js_ast_has_direct_eval_call((JsAstNode*)function->body)) {
            probe->captures_loop_lexical = true;
            return;
        }
        probe->function_depth++;
        for (JsAstNode* parameter = (JsAstNode*)function->params; parameter;
                parameter = (JsAstNode*)parameter->next) {
            js_interp_probe_loop_capture(parameter, probe);
        }
        js_interp_probe_loop_capture((JsAstNode*)function->body, probe);
        probe->function_depth--;
        return;
    }
    if (node->node_type == JS_AST_NODE_CLASS_DECLARATION ||
            node->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
        // Class members and instance-field initializer thunks can retain the
        // loop lexical environment after the iteration completes.
        probe->captures_loop_lexical = true;
        return;
    }
    js_ast_visit_children(node, js_interp_probe_loop_capture_child, probe);
}

static bool js_interp_loop_needs_per_iteration_env(NameScope* scope,
        JsAstNode* initialization, JsAstNode* test, JsAstNode* update,
        JsAstNode* body) {
    bool has_lexical = false;
    for (NameEntry* entry = scope ? scope->first : NULL; entry; entry = entry->next) {
        if (entry->is_lexical) {
            has_lexical = true;
            break;
        }
    }
    if (!has_lexical) return false;
    // Direct eval in the loop frame can create a closure over its lexical
    // binding without an AST-visible nested function.
    if (js_ast_has_direct_eval_call(initialization) ||
            js_ast_has_direct_eval_call(test) ||
            js_ast_has_direct_eval_call(update) ||
            js_ast_has_direct_eval_call(body)) return true;
    JsInterpLoopCaptureProbe probe = {scope, 0, false};
    js_interp_probe_loop_capture(initialization, &probe);
    js_interp_probe_loop_capture(test, &probe);
    js_interp_probe_loop_capture(update, &probe);
    js_interp_probe_loop_capture(body, &probe);
    return probe.captures_loop_lexical;
}

static JsInterpEnv* js_interp_find_env(JsInterpEnv* env, NameScope* scope) {
    for (JsInterpEnv* scan = env; scan; scan = scan->outer) {
        if (scan->scope == scope) return scan;
    }
    return NULL;
}

static uint64_t* js_interp_function_lexical_this_home(JsFunction* function) {
    for (JsInterpEnv* env = function ? function->interp_env : NULL; env;
            env = env->outer) {
        if (env->has_lexical_this) return &env->lexical_this;
    }
    return NULL;
}

static Item js_interp_function_lexical_this(JsFunction* function) {
    uint64_t* home = js_interp_function_lexical_this_home(function);
    return home ? (Item){.item = *home}
        : (function ? function->ast_lexical_this : ItemError);
}

static JsInterpEnv* js_interp_find_arguments_env(JsInterpEnv* env) {
    for (JsInterpEnv* scan = env; scan; scan = scan->outer) {
        if (scan->arguments_object != 0) return scan;
    }
    return NULL;
}

static bool js_interp_env_get_eval_binding(JsInterpEnv* env, Item key,
        Item* out_value) {
    if (!env) return false;
    Item bindings = (Item){.item = env->eval_bindings};
    if (get_type_id(bindings) != LMD_TYPE_ARRAY) return false;
    for (int64_t index = js_array_length(bindings) - 2; index >= 0; index -= 2) {
        Item bound_key = js_elements_get_int(bindings, index);
        Item equal = js_strict_equal(bound_key, key);
        if (!item_is_error(equal) && js_is_truthy(equal)) {
            if (out_value) *out_value = js_elements_get_int(bindings, index + 1);
            return true;
        }
    }
    return false;
}

static Item js_interp_env_set_eval_binding(JsInterpEnv* env, Item key,
        Item value) {
    if (!env) return ItemError;
    RootFrame roots(3);
    Rooted<Item> key_root(roots, key);
    Rooted<Item> value_root(roots, value);
    Rooted<Item> bindings_root(roots, (Item){.item = env->eval_bindings});
    if (get_type_id(bindings_root.get()) != LMD_TYPE_ARRAY) {
        bindings_root.set(js_array_new(0));
        if (item_is_error(bindings_root.get())) return bindings_root.get();
        env->eval_bindings = bindings_root.get().item;
    }
    for (int64_t index = js_array_length(bindings_root.get()) - 2; index >= 0;
            index -= 2) {
        Item equal = js_strict_equal(js_elements_get_int(bindings_root.get(), index),
            key_root.get());
        if (!item_is_error(equal) && js_is_truthy(equal)) {
            return js_elements_set_int_direct(bindings_root.get(), index + 1,
                value_root.get());
        }
    }
    Item pushed = js_array_push(bindings_root.get(), key_root.get());
    if (item_is_error(pushed)) return pushed;
    return js_array_push(bindings_root.get(), value_root.get());
}

static JsInterpEnv* js_interp_find_eval_binding_env(JsInterpEnv* env,
        NameEntry* entry, Item key, Item* out_value) {
    for (JsInterpEnv* scan = env; scan; scan = scan->outer) {
        if (entry && scan->scope == entry->scope) {
            // Eval var redeclarations update their function's var binding,
            // while an intervening lexical declaration remains authoritative.
            return !entry->is_lexical && js_interp_env_get_eval_binding(scan, key,
                out_value) ? scan : NULL;
        }
        if (js_interp_env_get_eval_binding(scan, key, out_value)) return scan;
    }
    return NULL;
}

static JsInterpEnv* js_interp_find_variable_env(JsInterpFrame* frame) {
    for (JsInterpEnv* env = frame ? frame->env : NULL; env; env = env->outer) {
        if (env->scope && env->scope->kind == SCOPE_KIND_FUNCTION) return env;
    }
    return NULL;
}

static bool js_interp_env_get_private_binding(JsInterpEnv* env, Item source_key,
        Item* out_private_key) {
    if (!env) return false;
    Item bindings = (Item){.item = env->private_bindings};
    if (get_type_id(bindings) != LMD_TYPE_ARRAY) return false;
    for (int64_t index = js_array_length(bindings) - 2; index >= 0; index -= 2) {
        Item bound_source = js_elements_get_int(bindings, index);
        Item equal = js_strict_equal(bound_source, source_key);
        if (!item_is_error(equal) && js_is_truthy(equal)) {
            if (out_private_key) {
                *out_private_key = js_elements_get_int(bindings, index + 1);
            }
            return true;
        }
    }
    return false;
}

static Item js_interp_find_private_home(JsInterpFrame* frame) {
    Item home = js_interp_frame_home_class(frame);
    if (home.item != 0 && home.item != ItemNull.item &&
            get_type_id(home) != LMD_TYPE_UNDEFINED) return home;
    for (JsInterpEnv* env = frame ? frame->env : NULL; env; env = env->outer) {
        if (env->private_home_class != 0 &&
                env->private_home_class != ItemNull.item) {
            return (Item){.item = env->private_home_class};
        }
    }
    return ItemNull;
}

static Item js_interp_private_key_for_frame(JsInterpFrame* frame, Item source_key) {
    if (!js_interp_private_source_name(source_key)) return source_key;
    // Private identifiers resolve through the lexical private environment,
    // so an inner class can retain access to a private member of its outer class.
    for (JsInterpEnv* env = frame ? frame->env : NULL; env; env = env->outer) {
        Item private_key = ItemNull;
        if (js_interp_env_get_private_binding(env, source_key, &private_key)) {
            return private_key;
        }
    }
    RootFrame roots(2);
    Rooted<Item> source_root(roots, source_key);
    Rooted<Item> home_root(roots, js_interp_find_private_home(frame));
    if (!roots.valid() || home_root.get().item == ItemNull.item ||
            home_root.get().item == 0) {
        return js_throw_type_error("Private name used outside its declaring class");
    }
    return js_private_key_for_class(home_root.get(), source_root.get());
}

static Item js_interp_register_private_binding(JsInterpFrame* frame,
        Item source_key, Item private_key) {
    JsInterpEnv* env = frame ? frame->env : NULL;
    while (env && env->private_home_class == 0) env = env->outer;
    if (!env) return ItemError;
    RootFrame roots(3);
    String* source_name = get_type_id(source_key) == LMD_TYPE_STRING
        ? it2s(source_key) : NULL;
    Rooted<Item> source_root(roots, source_name
        ? js_make_string_len(source_name->chars, (int)source_name->len) : ItemError);
    Rooted<Item> key_root(roots, private_key);
    Rooted<Item> bindings_root(roots, (Item){.item = env->private_bindings});
    if (item_is_error(source_root.get())) return source_root.get();
    if (bindings_root.get().item == 0 || bindings_root.get().item == ItemNull.item) {
        bindings_root.set(js_array_new(0));
        if (item_is_error(bindings_root.get())) return bindings_root.get();
        env->private_bindings = bindings_root.get().item;
    }
    if (get_type_id(bindings_root.get()) != LMD_TYPE_ARRAY) return ItemError;
    Item pushed = js_array_push(bindings_root.get(), source_root.get());
    if (item_is_error(pushed)) return pushed;
    pushed = js_array_push(bindings_root.get(), key_root.get());
    return item_is_error(pushed) ? pushed : js_status_ok();
}

static bool js_interp_function_has_simple_params(const JsFunctionNode* function) {
    for (const JsAstNode* param = function ? (const JsAstNode*)function->params : NULL;
            param; param = (const JsAstNode*)param->next) {
        if (param->node_type != AST_NODE_IDENT) return false;
    }
    return true;
}

static int js_interp_arguments_param_index(const JsInterpEnv* env,
        const NameEntry* entry) {
    if (!env || !env->arguments_are_mapped || !env->function_node || !entry ||
            entry->scope != env->scope || !entry->name) return -1;
    JsFunctionNode* function = (JsFunctionNode*)env->function_node;
    int result = -1;
    int index = 0;
    for (JsAstNode* param = (JsAstNode*)function->params; param;
            param = (JsAstNode*)param->next, index++) {
        if (param->node_type != AST_NODE_IDENT) continue;
        JsIdentifierNode* identifier = (JsIdentifierNode*)param;
        if (identifier->entry == entry || (identifier->name &&
                identifier->name->len == entry->name->len &&
                memcmp(identifier->name->chars, entry->name->chars,
                    entry->name->len) == 0)) {
            result = index;
        }
    }
    return result;
}

static bool js_interp_binding_shadows_arguments(JsInterpFrame* frame,
        JsInterpEnv* arguments_env, NameEntry* entry) {
    if (!frame || !arguments_env || !entry) return false;
    // A lexical record between the current frame and the Function Environment
    // Record resolves before its implicit `arguments` binding.  Bindings in an
    // outer function remain behind that record and therefore cannot shadow it.
    for (JsInterpEnv* env = frame->env; env && env != arguments_env;
            env = env->outer) {
        if (env->scope == entry->scope) return true;
    }
    if (entry->scope != arguments_env->scope) return false;
    // FunctionDeclarationInstantiation creates the implicit arguments binding
    // before `var` declarations.  Only a parameter or hoisted function named
    // `arguments` replaces it in the function's own environment.
    return entry->is_parameter || entry->is_function_name_binding ||
        (entry->node && entry->node->node_type == AST_NODE_FUNC);
}

static JsInterpEnv* js_interp_arguments_env_for_binding(JsInterpFrame* frame,
        NameEntry* entry) {
    JsInterpEnv* env = js_interp_find_arguments_env(frame ? frame->env : NULL);
    return env && !js_interp_binding_shadows_arguments(frame, env, entry)
        ? env : NULL;
}

static Item js_interp_read_arguments_param(JsInterpFrame* frame, NameEntry* entry,
        Item fallback) {
    JsInterpEnv* env = js_interp_find_arguments_env(frame ? frame->env : NULL);
    int index = js_interp_arguments_param_index(env, entry);
    return index >= 0 ? js_arguments_mapped_get((Item){.item = env->arguments_object},
        index, fallback) : fallback;
}

static Item js_interp_write_arguments_param(JsInterpFrame* frame, NameEntry* entry,
        Item value) {
    JsInterpEnv* env = js_interp_find_arguments_env(frame ? frame->env : NULL);
    int index = js_interp_arguments_param_index(env, entry);
    return index >= 0 ? js_arguments_mapped_param_writeback(
        (Item){.item = env->arguments_object}, index, value) : value;
}

static Item js_interp_tdz_error(String* name) {
    return js_throw_reference_error(js_make_string_len(name ? name->chars : "",
        name ? (int)name->len : 0));
}

static NameEntry* js_interp_find_binding(JsInterpFrame* frame, String* name);
static bool js_interp_is_undefined(Item value);

static int js_interp_captured_with_depth(const JsInterpFrame* frame) {
    return frame && frame->active_function ? frame->active_function->with_env_depth : 0;
}

static bool js_interp_binding_precedes_captured_with(const JsInterpFrame* frame,
        NameEntry* entry) {
    if (!frame || !entry || js_interp_captured_with_depth(frame) <= 0 ||
            !frame->active_function) return false;
    // The activation's own records precede the outer Object Environment Record
    // captured by a function created inside `with`.
    for (JsInterpEnv* env = frame->env;
            env && env != frame->active_function->interp_env; env = env->outer) {
        if (env->scope == entry->scope) return true;
    }
    return false;
}

static int js_interp_with_minimum_depth(const JsInterpFrame* frame,
        NameEntry* entry) {
    return js_interp_binding_precedes_captured_with(frame, entry)
        ? js_interp_captured_with_depth(frame) : 0;
}

static Item js_interp_read_binding(JsInterpFrame* frame, NameEntry* entry,
        String* unresolved_name) {
    if (!frame) return ItemError;
    if (unresolved_name && js_interp_name_equals(unresolved_name, "this")) {
        return js_interp_frame_this(frame);
    }
    if (unresolved_name && !entry &&
            js_interp_name_equals(unresolved_name, "new.target")) {
        return js_interp_frame_new_target(frame);
    }
    if (unresolved_name && !entry && js_interp_name_equals(unresolved_name, "import.meta")) {
        if (!frame->script || !frame->script->is_es_module) {
            return js_throw_syntax_error(js_make_string("import.meta outside module"));
        }
        RootFrame roots(2);
        Rooted<Item> meta(roots, js_get_import_meta());
        Rooted<Item> url(roots, js_make_string(frame->script->reference));
        Item stored = js_set_key_cstr(meta.get(), "url", url.get());
        return item_is_error(stored) ? stored : meta.get();
    }
    // Object Environment Records sit in front of lexical bindings. Probe
    // before reading the static NameEntry so an outer TDZ does not mask a
    // visible `with` property.
    bool with_lookup_completed = false;
    if (unresolved_name && js_with_depth_active()) {
        Item key = js_interp_name_key(unresolved_name);
        Item captured = js_capture_with_binding_from(key,
            js_interp_with_minimum_depth(frame, entry));
        if (item_is_error(captured)) return captured;
        with_lookup_completed = true;
        if (js_is_truthy(captured)) {
            Item base = js_get_last_with_binding_base_or_undefined(key);
            Item present = js_in(key, base);
            if (item_is_error(present)) return present;
            if (!js_is_truthy(present)) {
                return frame->strict ? js_interp_tdz_error(unresolved_name)
                    : make_js_undefined();
            }
            return js_get_key_default(base, key);
        }
    }
    if (unresolved_name && js_interp_name_equals(unresolved_name, "arguments")) {
        JsInterpEnv* arguments_env = js_interp_arguments_env_for_binding(frame, entry);
        if (arguments_env) return (Item){.item = arguments_env->arguments_object};
    }
    if (unresolved_name) {
        Item eval_value = ItemNull;
        if (js_interp_find_eval_binding_env(frame->env, entry,
                js_interp_name_key(unresolved_name), &eval_value)) {
            return eval_value;
        }
        // A direct eval may introduce a function-scoped var which was absent
        // from this script's static NameScope. The shared eval journal is the
        // authoritative extension of that function environment.
        Item key = js_interp_name_key(unresolved_name);
        if (js_eval_local_has_var_binding(key)) {
            return js_eval_local_get_binding_or_fallback(key, ItemError);
        }
    }
    if (!entry && unresolved_name) {
        // Annex-B companions are discovered while later block declarations
        // are built, after an earlier identifier node captured no static entry.
        entry = js_interp_find_binding(frame, unresolved_name);
    }
    if (!entry) {
        JsInterpImportBinding* imported = js_interp_import_binding(frame->script,
            unresolved_name);
        if (imported) return js_interp_read_import_binding(frame->script, imported);
    }
    if (!entry) {
        if (js_interp_name_equals(unresolved_name, "undefined")) return make_js_undefined();
        Item key = js_interp_name_key(unresolved_name);
        if (with_lookup_completed) {
            // ResolveBinding already completed Object Environment HasBinding;
            // revisiting the with-aware global path repeats Proxy [[Has]].
            return js_get_global_property_after_with_lookup(key);
        }
        if (!js_global_binding_exists(key)) return js_interp_tdz_error(unresolved_name);
        return js_get_global_property(key);
    }
    Item value = ItemNull;
    if (entry->scope == frame->script->global_scope) {
        value = js_get_module_var(entry->slot);
    } else {
        JsInterpEnv* env = js_interp_find_env(frame->env, entry->scope);
        if (!env || entry->slot < 0 || (uint32_t)entry->slot >= env->slot_count) {
            return ItemError;
        }
        value = owned_item_slot_read((Item*)(void*)env->slots, env->slot_count,
            entry->slot, false);
    }
    if (value.item == ITEM_JS_TDZ) return js_interp_tdz_error(entry->name);
    return js_interp_read_arguments_param(frame, entry, value);
}

static Item js_interp_write_binding(JsInterpFrame* frame, NameEntry* entry,
        String* unresolved_name, Item value, bool initialize,
        bool allow_eval_bindings = true, bool with_lookup_completed = false) {
    if (!frame) return ItemError;
    String* name = entry ? entry->name : unresolved_name;
    Item key = js_interp_name_key(name);
    if (!entry && js_interp_import_binding(frame->script, name)) {
        if (initialize) return value;
        return js_throw_type_error("Assignment to constant variable");
    }
    Item eval_value = ItemNull;
    JsInterpEnv* eval_env = !initialize && allow_eval_bindings ? js_interp_find_eval_binding_env(
        frame->env, entry, key, &eval_value) : NULL;
    if (eval_env) {
        Item stored = js_interp_env_set_eval_binding(eval_env, key, value);
        if (item_is_error(stored)) return stored;
        if (js_eval_local_has_var_binding(key)) js_eval_local_export_var(key, value);
        js_interp_publish_export_bindings(frame, name, value);
        return value;
    }
    // `var`/parameter bindings may have been supplied by a previous direct
    // eval. Keep subsequent interpreted writes in the shared function journal
    // instead of accidentally materializing a realm-global property.
    if (!initialize && allow_eval_bindings && (!entry || !entry->is_const) &&
            js_eval_local_has_var_binding(key)) {
        js_eval_local_export_var(key, value);
        js_interp_publish_export_bindings(frame, name, value);
        return value;
    }
    if (!entry) {
        return with_lookup_completed
            ? js_set_global_property_after_with_lookup(key, value, frame->strict ? 1 : 0)
            : js_set_global_property(key, value, frame->strict ? 1 : 0);
    }
    Item current = ItemNull;
    if (entry->scope == frame->script->global_scope) {
        current = js_get_module_var(entry->slot);
        if (!initialize && current.item == ITEM_JS_TDZ) {
            return js_interp_tdz_error(entry->name);
        }
        if (!initialize && entry->is_function_name_binding) {
            return frame->strict ? js_throw_const_assign(entry->name
                ? name_ref_id(entry->name) : NAME_ID_NONE,
                entry->name ? (int)entry->name->len : 0) : current;
        }
        if (!initialize && entry->is_const) {
            return js_throw_const_assign(entry->name ? name_ref_id(entry->name) : NAME_ID_NONE,
                entry->name ? (int)entry->name->len : 0);
        }
        js_set_module_var(entry->slot, value);
        if (!entry->is_lexical) {
            // Module declarations, including the synthetic CJS wrapper,
            // must never publish their cells as realm-global properties.
            if (!frame->script->is_module) {
                if (initialize) {
                    // $262.evalScript evaluates a Script, not an EvalCode
                    // record: CreateGlobalVarBinding creates the same
                    // non-configurable property as a top-level `var`.
                    if (frame->script->is_eval_script &&
                            !js_262_eval_script_is_active()) {
                        js_define_global_eval_var_property(key, value);
                    } else {
                        js_define_global_var_property(key, value);
                    }
                    // An indirect eval has a private execution slab. Let the
                    // pre-existing Script binding remain the global-property
                    // synchronization owner instead of replacing it here.
                    if (!frame->script->is_eval_script) {
                        js_register_global_var_module_binding(key, entry->slot);
                    }
                } else {
                    Item global_written = with_lookup_completed
                        ? js_set_global_property_after_with_lookup(key, value,
                            frame->strict ? 1 : 0)
                        : js_set_global_property(key, value, frame->strict ? 1 : 0);
                    if (item_is_error(global_written)) return global_written;
                }
            }
        } else if (initialize) {
            if (!frame->script->is_module) {
                js_global_lexical_declare(key, value, entry->is_const ? 1 : 0);
            }
        } else {
            if (!frame->script->is_module) {
                Item set_result = js_global_lexical_set_if_exists(key, value);
                if (item_is_error(set_result)) return set_result;
            }
        }
        js_interp_publish_export_bindings(frame, entry->name, value);
        return value;
    }
    JsInterpEnv* env = js_interp_find_env(frame->env, entry->scope);
    if (!env || entry->slot < 0 || (uint32_t)entry->slot >= env->slot_count) {
        return ItemError;
    }
    current = owned_item_slot_read((Item*)(void*)env->slots, env->slot_count,
        entry->slot, false);
    if (!initialize && current.item == ITEM_JS_TDZ) {
        return js_interp_tdz_error(entry->name);
    }
    if (!initialize && entry->is_function_name_binding) {
        return frame->strict ? js_throw_const_assign(entry->name
            ? name_ref_id(entry->name) : NAME_ID_NONE,
            entry->name ? (int)entry->name->len : 0) : current;
    }
    if (!initialize && entry->is_const) {
        return js_throw_const_assign(entry->name ? name_ref_id(entry->name) : NAME_ID_NONE,
            entry->name ? (int)entry->name->len : 0);
    }
    owned_item_slot_store((Item*)(void*)env->slots, env->slot_count, entry->slot,
        value);
    Item written = js_interp_write_arguments_param(frame, entry, value);
    if (item_is_error(written)) return written;
    js_interp_publish_export_bindings(frame, entry->name, value);
    return value;
}

static int js_interp_function_param_count(const JsFunctionNode* function) {
    int count = 0;
    for (const AstNode* param = function ? function->params : NULL; param;
            param = param->next) {
        count++;
    }
    return count;
}

// Function.length counts only parameters preceding the first default or rest
// parameter; binding still needs the full source parameter list.
static int js_interp_function_formal_length(const JsFunctionNode* function) {
    int count = 0;
    for (const AstNode* param = function ? function->params : NULL; param;
            param = param->next) {
        if (param->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN ||
                param->node_type == JS_AST_NODE_REST_ELEMENT) {
            break;
        }
        count++;
    }
    return count;
}

static Item js_interp_configure_function_metadata(Item function_item);

static NameScope* js_interp_function_name_scope(const JsFunctionNode* function) {
    NameScope* scope = function && function->vars ? function->vars->parent : NULL;
    return scope && scope->is_function_name_scope ? scope : NULL;
}

static Item js_interp_new_function(JsInterpFrame* frame,
        JsFunctionNode* function, uint32_t flags) {
    if (!frame || !function) return ItemError;
    NameScope* name_scope = js_interp_function_name_scope(function);
    JsInterpEnv* name_env = name_scope
        ? js_interp_env_create(name_scope, frame->env) : NULL;
    JsInterpEnvRoot name_env_root(name_env);
    if (name_scope && (!name_env || !name_env_root.registered)) return ItemError;
    JS_ROOTS(roots, result_root, js_new_interpreted_function(function, frame->script,
        name_env ? name_env : frame->env,
        js_interp_function_param_count(function), flags));
    if (!item_is_error(result_root.get()) &&
            (js_private_field_initializing || js_eval_initializer_context)) {
        // Nested closures retain the field-initializer early-error context
        // after the initializer itself has returned.
        ((JsFunction*)result_root.get().function)->eval_initializer_context = true;
    }
    if (name_scope) {
        NameEntry* self = name_scope->first;
        if (!self || !self->is_function_name_binding || self->slot < 0 ||
                (uint32_t)self->slot >= name_env->slot_count) return ItemError;
        owned_item_slot_store((Item*)(void*)name_env->slots, name_env->slot_count,
            self->slot, result_root.get());
    }
    result_root.set(js_interp_configure_function_metadata(result_root.get()));
    if (!item_is_error(result_root.get())) {
        js_set_formal_length(result_root.get(), js_interp_function_formal_length(function));
        const char* source_text = NULL;
        uint32_t source_length = 0;
        if (frame->script && js_function_source_span(frame->script->source,
                frame->script->source_length, function, &source_text, &source_length)) {
            Item source_item = js_make_string_len(source_text, (int)source_length);
            if (get_type_id(source_item) == LMD_TYPE_STRING) {
                js_set_function_source(result_root.get(), source_item);
            }
        }
    }
    return result_root.get();
}

static Item js_interp_make_function(JsInterpFrame* frame, JsFunctionNode* function) {
    if (!frame || !function) {
        return js_throw_type_error("unsupported interpreted function form");
    }
    uint32_t flags = 0;
    if (function->is_arrow) flags |= JS_FUNC_FLAG_ARROW;
    if (function->is_generator) {
        flags |= JS_FUNC_FLAG_GENERATOR;
        if (function->is_async) flags |= JS_FUNC_FLAG_ASYNC_GEN;
    }
    // Async AST bodies use the common promise call wrapper.  `await` itself
    // remains in the AST tier so closure and EvalContext ownership do not
    // cross into a separately compiled function body.
    if (function->is_async && !function->is_generator) flags |= JS_FUNC_FLAG_ASYNC;
    if (frame->strict || function->has_use_strict_directive) flags |= JS_FUNC_FLAG_STRICT;
    Item result = js_interp_new_function(frame, function, flags);
    if (function->is_arrow && !item_is_error(result)) {
        // The shared factory normally snapshots the ambient call kernel.
        // AST evaluation carries `this` explicitly, which differs while a
        // static field or field initializer is being evaluated.
        JsFunction* closure = (JsFunction*)result.function;
        closure->ast_lexical_this = js_interp_frame_this_binding(frame);
        closure->ast_lexical_new_target = js_interp_frame_new_target(frame);
    }
    // Arrow functions inherit the surrounding method or class-initializer
    // [[HomeObject]], which is the common runtime's lexical `super` carrier.
    if (function->is_arrow && !item_is_error(result)) {
        Item home_class = js_interp_frame_home_class(frame);
        if (home_class.item != ItemNull.item && home_class.item != 0) {
            js_set_function_home_class(result, home_class);
        }
    }
    return result;
}

static Item js_interp_make_method(JsInterpFrame* frame, JsFunctionNode* function) {
    if (!frame || !function) {
        return js_throw_type_error("unsupported interpreted method form");
    }
    // Class methods are strict and never expose [[Construct]]. The common
    // function factory still supplies their ordinary JS call capability.
    uint32_t flags = JS_FUNC_FLAG_METHOD | JS_FUNC_FLAG_STRICT;
    if (function->is_generator) {
        flags |= JS_FUNC_FLAG_GENERATOR;
        if (function->is_async) flags |= JS_FUNC_FLAG_ASYNC_GEN;
    }
    if (function->is_async && !function->is_generator) flags |= JS_FUNC_FLAG_ASYNC;
    return js_interp_new_function(frame, function, flags);
}

static Item js_interp_make_object_method(JsInterpFrame* frame,
        JsFunctionNode* function) {
    if (!frame || !function) {
        return js_throw_type_error("unsupported interpreted object method form");
    }
    uint32_t flags = JS_FUNC_FLAG_METHOD;
    if (function->is_generator) {
        flags |= JS_FUNC_FLAG_GENERATOR;
        if (function->is_async) flags |= JS_FUNC_FLAG_ASYNC_GEN;
    }
    if (function->is_async && !function->is_generator) flags |= JS_FUNC_FLAG_ASYNC;
    if (frame->strict || function->has_use_strict_directive) flags |= JS_FUNC_FLAG_STRICT;
    return js_interp_new_function(frame, function, flags);
}

static Item js_interp_make_field_initializer(JsInterpFrame* frame,
        JsFieldDefinitionNode* field) {
    if (!frame || !frame->script || !frame->script->pool || !field || !field->value) {
        return ItemError;
    }
    JsFunctionNode* initializer = (JsFunctionNode*)pool_calloc(frame->script->pool,
        sizeof(JsFunctionNode));
    NameScope* scope = (NameScope*)pool_calloc(frame->script->pool, sizeof(NameScope));
    if (!initializer || !scope) return ItemError;
    // Keep an empty function environment between the initializer's `this`
    // and its captured defining environment. Evaluating the expression at
    // class definition would bind `this` to the wrong receiver.
    scope->kind = SCOPE_KIND_FUNCTION;
    scope->strict = true;
    initializer->node_type = JS_AST_NODE_FUNCTION_EXPRESSION;
    initializer->source_span = field->source_span;
    initializer->body = field->value;
    initializer->vars = scope;
    initializer->has_use_strict_directive = true;
    Item result = js_interp_new_function(frame, initializer,
        JS_FUNC_FLAG_METHOD | JS_FUNC_FLAG_STRICT);
    if (!item_is_error(result)) {
        // Instance fields run later, outside class evaluation, but retain the
        // same direct-eval early-error rules as a static field initializer.
        ((JsFunction*)result.function)->eval_initializer_context = true;
    }
    return result;
}

static JsInterpCompletion js_interp_eval(JsInterpFrame* frame, JsAstNode* node);
static JsInterpCompletion js_interp_exec(JsInterpFrame* frame, JsAstNode* node);
static JsInterpCompletion js_interp_exec_list(JsInterpFrame* frame, JsAstNode* node);
static void js_interp_generator_clear_list_continuation(JsGeneratorStateRecord* state);
static bool js_interp_is_anonymous_function_definition(JsAstNode* initializer);
static JsAstNode* js_interp_unwrap_name_initializer(JsAstNode* initializer);
static JsInterpCompletion js_interp_initialize_scope(JsInterpFrame* frame,
        NameScope* scope, bool initialize_functions = true);
static JsInterpCompletion js_interp_initialize_function_declarations(
        JsInterpFrame* frame, NameScope* scope);
static Item js_interp_load_es_module(Runtime* runtime, const char* filename);
static Item js_interp_configure_function_metadata(Item function_item);

struct JsInterpMemberResult {
    JsInterpCompletion completion;
    bool optional_short_circuit;
};

static JsInterpMemberResult js_interp_eval_member_chain(JsInterpFrame* frame,
        JsMemberNode* member);

static NameEntry* js_interp_find_binding(JsInterpFrame* frame, String* name) {
    if (!frame || !name) return NULL;
    for (JsInterpEnv* env = frame->env; env; env = env->outer) {
        for (NameEntry* entry = env->scope ? env->scope->first : NULL;
                entry; entry = entry->next) {
            if (entry->name && entry->name->len == name->len &&
                    memcmp(entry->name->chars, name->chars, name->len) == 0) {
                return entry;
            }
        }
    }
    for (NameEntry* entry = frame->script && frame->script->global_scope
            ? frame->script->global_scope->first : NULL;
            entry; entry = entry->next) {
        if (entry->name && entry->name->len == name->len &&
                memcmp(entry->name->chars, name->chars, name->len) == 0) {
            return entry;
        }
    }
    return NULL;
}

static JsInterpCompletion js_interp_class_key(JsInterpFrame* frame,
        JsAstNode* key, bool computed, Item class_item) {
    if (!key) return js_interp_throw(js_throw_type_error("class member has no name"));
    Item result = ItemNull;
    if (!computed && key->node_type == AST_NODE_IDENT) {
        result = js_interp_name_key(((JsIdentifierNode*)key)->name);
    } else {
        JsInterpCompletion value = js_interp_eval(frame, key);
        if (value.kind != JS_INTERP_NORMAL) return value;
        result = js_interp_property_key_value(value.value);
    }
    if (item_is_error(result)) return js_interp_throw(result);
    if (!computed && js_interp_private_source_name(result)) {
        RootFrame roots(2);
        Rooted<Item> class_root(roots, class_item);
        Rooted<Item> key_root(roots, result);
        Item private_key = js_private_key_for_class(class_root.get(), key_root.get());
        if (item_is_error(private_key)) return js_interp_throw(private_key);
        Item registered = js_interp_register_private_binding(frame,
            key_root.get(), private_key);
        return item_is_error(registered) ? js_interp_throw(registered)
            : js_interp_normal(private_key);
    }
    return js_interp_normal(result);
}

static JsInterpCompletion js_interp_eval_class(JsInterpFrame* frame,
        JsClassNode* cls, bool declaration, String* inferred_name = NULL) {
    if (!frame || !cls) return js_interp_throw(ItemError);
    RootFrame roots(7);
    Rooted<Item> class_root(roots, js_new_class_function());
    Rooted<Item> prototype_root(roots, ItemNull);
    Rooted<Item> super_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> target_root(roots, ItemNull);
    Rooted<Item> method_root(roots, ItemNull);
    if (item_is_error(class_root.get())) return js_interp_throw(class_root.get());

    prototype_root.set(js_get_key_cstr(class_root.get(), "prototype"));
    if (item_is_error(prototype_root.get())) return js_interp_throw(prototype_root.get());
    if (get_type_id(prototype_root.get()) != LMD_TYPE_MAP) {
        prototype_root.set(js_new_object());
        if (item_is_error(prototype_root.get())) return js_interp_throw(prototype_root.get());
        key_root.set(js_make_string("prototype"));
        Item published = js_set_key_default(class_root.get(), key_root.get(), prototype_root.get());
        if (item_is_error(published)) return js_interp_throw(published);
    }
    js_set_class_instance_prototype(class_root.get(), prototype_root.get());
    js_set_default_constructor_property(prototype_root.get(), class_root.get());
    key_root.set(js_make_string("prototype"));
    // Classes expose a non-writable, non-enumerable, non-configurable
    // prototype data property even though their factory starts as a function.
    js_mark_non_writable(class_root.get(), key_root.get());
    js_mark_non_enumerable(class_root.get(), key_root.get());
    js_mark_non_configurable(class_root.get(), key_root.get());
    if (cls->name) {
        value_root.set(js_make_string_len(cls->name->chars, (int)cls->name->len));
        js_set_class_name(class_root.get(), value_root.get());
    }

    // The class's private name binding exists through heritage evaluation and
    // all element definitions; initialize it only after heritage succeeds.
    JsInterpEnv* class_env = js_interp_env_create(cls->expression_scope, frame->env);
    JsInterpEnvRoot class_env_root(class_env);
    if (!class_env || !class_env_root.registered) return js_interp_throw(ItemError);
    class_env->private_home_class = class_root.get().item;
    JsInterpFrame class_frame = *frame;
    class_frame.env = class_env;
    if (cls->expression_scope && cls->name) {
        JsInterpCompletion initialized = js_interp_initialize_scope(&class_frame,
            cls->expression_scope);
        if (initialized.kind != JS_INTERP_NORMAL) return initialized;
    }

    if (cls->superclass) {
        // Class definition evaluation is strict, including a function
        // expression created by its heritage expression.  Preserve this frame
        // fact so the function receives a strict arguments object.
        JsInterpFrame heritage_frame = class_frame;
        heritage_frame.strict = true;
        JsInterpCompletion heritage = js_interp_eval(&heritage_frame,
            (JsAstNode*)cls->superclass);
        if (heritage.kind != JS_INTERP_NORMAL) return heritage;
        super_root.set(heritage.value);
        if (get_type_id(super_root.get()) != LMD_TYPE_NULL) {
            if (!js_has_construct_capability(super_root.get())) {
                return js_interp_throw(js_throw_type_error(
                    "Class extends value is not a constructor or null"));
            }
            value_root.set(js_get_key_cstr(super_root.get(), "prototype"));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            TypeId parent_type = get_type_id(value_root.get());
            if (parent_type != LMD_TYPE_NULL && parent_type != LMD_TYPE_MAP &&
                    parent_type != LMD_TYPE_FUNC &&
                    !js_is_js_array(value_root.get()) && parent_type != LMD_TYPE_ELEMENT) {
                return js_interp_throw(js_throw_type_error(
                    "Class extends value has invalid prototype property"));
            }
            js_set_prototype(prototype_root.get(), value_root.get());
            js_set_prototype(class_root.get(), super_root.get());
        } else {
            js_set_prototype(prototype_root.get(), ItemNull);
        }
        js_set_class_superclass(class_root.get(), super_root.get());
    }

    if (!cls->name && inferred_name) {
        value_root.set(js_make_string_len(inferred_name->chars,
            (int)inferred_name->len));
        if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
        js_set_class_name(class_root.get(), value_root.get());
    }

    if (cls->expression_scope && cls->name) {
        NameEntry* self = cls->expression_scope->first;
        Item stored = js_interp_write_binding(&class_frame, self, cls->name,
            class_root.get(), true);
        if (item_is_error(stored)) return js_interp_throw(stored);
    }

    // A declaration becomes visible after heritage setup, before static
    // element evaluation. That lets static fields/blocks close over the class
    // binding while preserving the pre-heritage TDZ.
    if (declaration && cls->name) {
        NameEntry* entry = js_interp_find_binding(frame, cls->name);
        Item stored = js_interp_write_binding(frame, entry, cls->name,
            class_root.get(), true);
        if (item_is_error(stored)) return js_interp_throw(stored);
    }

    int instance_field_count = 0;
    int deferred_static_count = 0;
    for (JsAstNode* member = cls->body ? (JsAstNode*)((JsBlockNode*)cls->body)->statements
            : NULL; member; member = (JsAstNode*)member->next) {
        if (member->node_type == JS_AST_NODE_FIELD_DEFINITION &&
                !((JsFieldDefinitionNode*)member)->is_static) {
            instance_field_count++;
        } else if ((member->node_type == JS_AST_NODE_FIELD_DEFINITION &&
                ((JsFieldDefinitionNode*)member)->is_static) ||
                member->node_type == JS_AST_NODE_STATIC_BLOCK) {
            deferred_static_count++;
        } else if (member->node_type == JS_AST_NODE_METHOD_DEFINITION) {
            JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)member;
            Item source_key = method->key && method->key->node_type == AST_NODE_IDENT
                ? js_interp_name_key(((JsIdentifierNode*)method->key)->name) : ItemNull;
            if (!method->static_method && js_interp_private_source_name(source_key)) {
                instance_field_count++;
            }
        }
    }
    if (instance_field_count > 0) {
        js_init_class_instance_field_metadata(class_root.get(), instance_field_count);
    }
    // ClassElementEvaluation records all field keys before DefineField runs
    // static initializers. Keep the values rooted while later keys execute.
    RootSpan deferred_static_key_roots((size_t)deferred_static_count);
    Item* deferred_static_keys = deferred_static_count > 0
        ? (Item*)(void*)deferred_static_key_roots.words() : NULL;
    JsAstNode** deferred_static_members = deferred_static_count > 0
        ? (JsAstNode**)(void*)LAMBDA_ALLOCA(deferred_static_count, uint64_t) : NULL;
    for (int index = 0; index < deferred_static_count; index++) {
        deferred_static_keys[index] = ItemNull;
    }
    int deferred_static_index = 0;
    int instance_field_index = 0;
    JsInterpFrame static_frame = *frame;
    static_frame.env = class_env;
    static_frame.this_home = class_root.home();
    static_frame.home_class_home = class_root.home();
    for (JsAstNode* member = cls->body ? (JsAstNode*)((JsBlockNode*)cls->body)->statements
            : NULL; member; member = (JsAstNode*)member->next) {
        if (member->node_type == JS_AST_NODE_METHOD_DEFINITION) {
            JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)member;
            JsInterpCompletion key = js_interp_class_key(&static_frame,
                (JsAstNode*)method->key, method->computed, class_root.get());
            if (key.kind != JS_INTERP_NORMAL) return key;
            key_root.set(key.value);
            method_root.set(js_interp_make_method(&static_frame, (JsFunctionNode*)method));
            if (item_is_error(method_root.get())) return js_interp_throw(method_root.get());
            js_set_function_home_class(method_root.get(), class_root.get());
            if (method->kind == JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR) {
                if (method->static_method) {
                    return js_interp_throw(js_throw_type_error(
                        "class constructor cannot be static"));
                }
                if (cls->superclass) js_mark_derived_constructor_func(method_root.get());
                js_set_class_constructor(class_root.get(), method_root.get());
                js_set_formal_length(class_root.get(), js_interp_function_formal_length(
                    (JsFunctionNode*)method));
                continue;
            }
            int64_t prefix_kind = method->kind == JsMethodDefinitionNode::JS_METHOD_GET
                ? 1 : (method->kind == JsMethodDefinitionNode::JS_METHOD_SET ? 2 : 0);
            // Class methods acquire their names from the evaluated property key.
            js_set_function_name_from_property_key(method_root.get(), key_root.get(),
                prefix_kind);
            target_root.set(method->static_method ? class_root.get() : prototype_root.get());
            bool private_method = js_interp_private_key(key_root.get());
            Item installed = (method->kind == JsMethodDefinitionNode::JS_METHOD_GET ||
                    method->kind == JsMethodDefinitionNode::JS_METHOD_SET)
                ? js_define_accessor_partial(target_root.get(), key_root.get(), method_root.get(),
                    method->kind == JsMethodDefinitionNode::JS_METHOD_SET ? 1 : 0,
                    JSPD_NON_ENUMERABLE)
                : js_create_data_property(target_root.get(), key_root.get(), method_root.get());
            if (item_is_error(installed)) return js_interp_throw(installed);
            if (private_method) {
                js_mark_private_method_non_writable(target_root.get(), key_root.get());
                if (!method->static_method) {
                    js_set_class_instance_field_metadata_key(class_root.get(),
                        instance_field_index, key_root.get());
                    js_set_class_instance_field_metadata_private_method(class_root.get(),
                        instance_field_index++);
                } else {
                    Item branded = js_private_brand_add(class_root.get(), key_root.get(),
                        class_root.get());
                    if (item_is_error(branded)) return js_interp_throw(branded);
                }
            }
            if (method->kind != JsMethodDefinitionNode::JS_METHOD_GET &&
                    method->kind != JsMethodDefinitionNode::JS_METHOD_SET) {
                js_mark_non_enumerable(target_root.get(), key_root.get());
            }
            continue;
        }
        if (member->node_type == JS_AST_NODE_FIELD_DEFINITION) {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)member;
            JsInterpCompletion key = js_interp_class_key(&static_frame,
                (JsAstNode*)field->key, field->computed, class_root.get());
            if (key.kind != JS_INTERP_NORMAL) return key;
            key_root.set(key.value);
            if (!field->is_static) {
                js_set_class_instance_field_metadata_key(class_root.get(),
                    instance_field_index, key_root.get());
                if (field->value) {
                    method_root.set(js_interp_make_field_initializer(&static_frame, field));
                    if (item_is_error(method_root.get())) return js_interp_throw(method_root.get());
                    js_set_function_home_class(method_root.get(), class_root.get());
                    js_set_class_instance_field_metadata_initializer(class_root.get(),
                        instance_field_index, method_root.get());
                } else {
                    js_set_class_instance_field_metadata_value(class_root.get(),
                        instance_field_index, make_js_undefined());
                }
                instance_field_index++;
                continue;
            }
            deferred_static_members[deferred_static_index] = member;
            deferred_static_keys[deferred_static_index++] = key_root.get();
            continue;
        }
        if (member->node_type == JS_AST_NODE_STATIC_BLOCK) {
            deferred_static_members[deferred_static_index] = member;
            deferred_static_keys[deferred_static_index++] = ItemNull;
            continue;
        }
        return js_interp_throw(js_throw_type_error("unsupported interpreted class member"));
    }
    for (int index = 0; index < deferred_static_index; index++) {
        JsAstNode* member = deferred_static_members[index];
        if (member->node_type == JS_AST_NODE_FIELD_DEFINITION) {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)member;
            key_root.set(deferred_static_keys[index]);
            if (field->value) {
                JsInterpCompletion value = js_interp_eval(&static_frame,
                    (JsAstNode*)field->value);
                if (value.kind != JS_INTERP_NORMAL) return value;
                value_root.set(value.value);
            } else {
                value_root.set(make_js_undefined());
            }
            if (field->value && js_interp_is_anonymous_function_definition(
                    (JsAstNode*)field->value)) {
                js_set_function_name_if_anonymous(value_root.get(), key_root.get());
            }
            Item installed = js_create_data_property(class_root.get(), key_root.get(),
                value_root.get());
            if (item_is_error(installed)) return js_interp_throw(installed);
            if (field->is_private) {
                Item branded = js_private_brand_add(class_root.get(), key_root.get(),
                    class_root.get());
                if (item_is_error(branded)) return js_interp_throw(branded);
            }
            continue;
        }
        JsStaticBlockNode* block = (JsStaticBlockNode*)member;
        JsInterpCompletion completion = block->body
            ? js_interp_exec(&static_frame, block->body)
            : js_interp_normal(make_js_undefined());
        if (completion.kind != JS_INTERP_NORMAL) return completion;
    }
    js_mark_all_non_enumerable(prototype_root.get());
    return js_interp_normal(class_root.get());
}

static JsInterpCompletion js_interp_eval_initializer_with_binding_name(
        JsInterpFrame* frame, JsAstNode* initializer, String* binding_name) {
    JsAstNode* unwrapped = js_interp_unwrap_name_initializer(initializer);
    if (binding_name && unwrapped &&
            unwrapped->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
        JsClassNode* cls = (JsClassNode*)unwrapped;
        if (!cls->name) return js_interp_eval_class(frame, cls, false, binding_name);
    }
    return js_interp_eval(frame, initializer);
}

static JsInterpCompletion js_interp_property_key(JsInterpFrame* frame,
        JsMemberNode* member) {
    if (!member) return js_interp_throw(ItemError);
    if (!member->computed && member->property &&
            member->property->node_type == AST_NODE_IDENT) {
        Item result = js_interp_private_key_for_frame(frame,
            js_interp_name_key(((JsIdentifierNode*)member->property)->name));
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    RootFrame roots(1);
    Rooted<Item> key_root(roots, ItemNull);
    JsInterpCompletion key = js_interp_eval(frame, (JsAstNode*)member->property);
    if (key.kind != JS_INTERP_NORMAL) return key;
    key_root.set(key.value);
    Item result = js_interp_property_key_value(key_root.get());
    return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
}

static JsInterpCompletion js_interp_member_key_after_base(JsInterpFrame* frame,
        JsMemberNode* member, Item base, bool require_object,
        bool defer_property_reference, uint64_t* key_home,
        bool* key_deferred, uint64_t* super_base_home = NULL) {
    if (!member || !key_home) return js_interp_throw(ItemError);
    if (key_deferred) *key_deferred = false;
    if (!member->computed) {
        if (super_base_home) {
            Item super_base = js_super_get_base(base);
            if (item_is_error(super_base)) return js_interp_throw(super_base);
            *super_base_home = super_base.item;
        }
        if (require_object && !defer_property_reference) {
            Item checked = js_require_object_coercible(base);
            if (item_is_error(checked)) return js_interp_throw(checked);
        }
        JsInterpCompletion key = js_interp_property_key(frame, member);
        if (key.kind == JS_INTERP_NORMAL) *key_home = key.value.item;
        return key;
    }
    JsInterpCompletion raw_key = js_interp_eval(frame, (JsAstNode*)member->property);
    if (raw_key.kind != JS_INTERP_NORMAL) return raw_key;
    RootFrame roots(1);
    Rooted<Item> raw_key_root(roots, raw_key.value);
    if (super_base_home) {
        Item super_base = js_super_get_base(base);
        if (item_is_error(super_base)) return js_interp_throw(super_base);
        *super_base_home = super_base.item;
    }
    if (require_object && !defer_property_reference) {
        Item checked = js_require_object_coercible(base);
        if (item_is_error(checked)) return js_interp_throw(checked);
    }
    if (defer_property_reference) {
        *key_home = raw_key_root.get().item;
        if (key_deferred) *key_deferred = true;
        return js_interp_normal(raw_key_root.get());
    }
    Item canonical_key = js_interp_property_key_value(raw_key_root.get());
    if (item_is_error(canonical_key)) return js_interp_throw(canonical_key);
    *key_home = canonical_key.item;
    return js_interp_normal(canonical_key);
}

static JsInterpCompletion js_interp_eval_reference(JsInterpFrame* frame,
        JsAstNode* node, JsInterpReference* out_reference,
        uint64_t* object_home, uint64_t* key_home,
        bool defer_property_reference = false, uint64_t* super_base_home = NULL) {
    if (!out_reference) return js_interp_throw(ItemError);
    memset(out_reference, 0, sizeof(*out_reference));
    out_reference->object_home = object_home;
    out_reference->key_home = key_home;
    out_reference->super_base_home = super_base_home;
    if (node && node->node_type == AST_NODE_IDENT) {
        JsIdentifierNode* identifier = (JsIdentifierNode*)node;
        out_reference->entry = identifier->entry ? identifier->entry
            : js_interp_find_binding(frame, identifier->name);
        Item key = js_interp_name_key(identifier->name);
        if (key_home) *key_home = key.item;
        Item eval_value = ItemNull;
        out_reference->binding_uses_eval = js_interp_find_eval_binding_env(frame->env,
            out_reference->entry, key, &eval_value) != NULL;
        if (js_with_depth_active()) {
            Item captured = js_capture_with_binding_from(key,
                js_interp_with_minimum_depth(frame, out_reference->entry));
            if (item_is_error(captured)) return js_interp_throw(captured);
            out_reference->with_lookup_completed = true;
            out_reference->with_binding = js_is_truthy(captured);
            if (out_reference->with_binding && object_home) {
                // A Reference keeps its resolved Object Environment base even
                // when the initializer mutates the with object before PutValue.
                *object_home = js_get_last_with_binding_base_or_undefined(key).item;
            }
        }
        if (!out_reference->with_binding && js_interp_name_equals(identifier->name,
                "arguments")) {
            out_reference->arguments_env = js_interp_arguments_env_for_binding(frame,
                out_reference->entry);
        }
        bool special_binding = js_interp_name_equals(identifier->name, "this") ||
            js_interp_name_equals(identifier->name, "new.target") ||
            js_interp_name_equals(identifier->name, "import.meta");
        out_reference->unresolvable_binding = !out_reference->entry &&
            !out_reference->with_binding && !out_reference->arguments_env &&
            !out_reference->binding_uses_eval &&
            !js_eval_local_has_var_binding(key) && !special_binding &&
            !js_interp_import_binding(frame->script, identifier->name) &&
            !js_global_binding_exists_after_with_lookup(key);
        return js_interp_normal(ItemNull);
    }
    if (node && (node->node_type == AST_NODE_MEMBER_EXPR ||
            node->node_type == AST_NODE_INDEX_EXPR)) {
        JsMemberNode* member = (JsMemberNode*)node;
        RootFrame roots(2);
        Rooted<Item> object_root(roots, ItemNull);
        Rooted<Item> key_root(roots, ItemNull);
        bool super_property = js_interp_member_uses_super(member);
        if (super_property) {
            object_root.set(js_interp_super_this(frame));
            if (item_is_error(object_root.get())) return js_interp_throw(object_root.get());
        } else {
            JsInterpCompletion object = js_interp_eval(frame, (JsAstNode*)member->object);
            if (object.kind != JS_INTERP_NORMAL) return object;
            object_root.set(object.value);
        }
        bool defer_key = false;
        JsInterpCompletion key = js_interp_member_key_after_base(frame, member,
            object_root.get(), !super_property, defer_property_reference,
            key_root.home(), &defer_key, super_property ? super_base_home : NULL);
        if (key.kind != JS_INTERP_NORMAL) return key;
        if (object_home) *object_home = object_root.get().item;
        if (key_home) *key_home = key_root.get().item;
        out_reference->property = true;
        out_reference->super_property = super_property;
        out_reference->property_key_deferred = defer_key;
        out_reference->property_lane = defer_key ? 0 :
            js_property_lane_for_canonical_key(key_root.get());
        return js_interp_normal(ItemNull);
    }
    if (node && node->node_type == AST_NODE_CALL_EXPR) {
        // Annex B accepts a call expression in sloppy assignment positions.
        // Evaluate the call for its side effects, then reject its non-reference
        // result without reading it, coercing it, or evaluating a RHS.
        JsInterpCompletion evaluated = js_interp_eval(frame, node);
        if (evaluated.kind != JS_INTERP_NORMAL) return evaluated;
        return js_interp_throw(js_throw_reference_error(js_make_string(
            "Invalid left-hand side in assignment")));
    }
    return js_interp_throw(js_throw_type_error("invalid assignment target"));
}

static Item js_interp_reference_read(JsInterpFrame* frame,
        const JsInterpReference* reference) {
    if (!reference) return ItemError;
    if (reference->with_binding) {
        Item base = reference->object_home
            ? (Item){.item = *reference->object_home} : make_js_undefined();
        if (!js_interp_is_undefined(base)) {
            Item present = js_in(js_interp_reference_key(reference), base);
            if (item_is_error(present)) return present;
            if (!js_is_truthy(present)) {
                return frame->strict ? js_throw_reference_error(
                    js_interp_reference_key(reference)) : make_js_undefined();
            }
            return js_get_key_default(base, js_interp_reference_key(reference));
        }
        return js_get_with_binding_or_fallback(js_interp_reference_key(reference),
            make_js_undefined());
    }
    if (reference->arguments_env) {
        return (Item){.item = reference->arguments_env->arguments_object};
    }
    if (!reference->property) {
        return js_interp_read_binding(frame, reference->entry,
            reference->entry ? reference->entry->name
                : it2s(js_interp_reference_key(reference)));
    }
    if (reference->super_property) {
        Item base = reference->super_base_home
            ? (Item){.item = *reference->super_base_home} : js_super_get_base(
                js_interp_reference_object(reference));
        return js_super_property_get_from_base(js_interp_reference_object(reference), base,
            js_interp_reference_key(reference));
    }
    if (js_property_lane_is_valid(reference->property_lane)) {
        return js_get(js_interp_reference_object(reference), reference->property_lane,
            js_interp_reference_key(reference), js_interp_reference_object(reference));
    }
    return js_get_key_default(js_interp_reference_object(reference),
        js_interp_reference_key(reference));
}

static Item js_interp_reference_write(JsInterpFrame* frame,
        const JsInterpReference* reference, Item value, bool initialize) {
    if (!reference) return ItemError;
    RootFrame roots(3);
    Rooted<Item> object_root(roots, js_interp_reference_object(reference));
    Rooted<Item> key_root(roots, js_interp_reference_key(reference));
    Rooted<Item> value_root(roots, value);
    JsPropertyLane property_lane = reference->property_lane;
    if (reference->property && reference->property_key_deferred) {
        key_root.set(js_interp_property_key_value(key_root.get()));
        if (item_is_error(key_root.get())) return key_root.get();
        property_lane = js_property_lane_for_canonical_key(key_root.get());
    }
    if (reference->with_binding) {
        Item base = reference->object_home
            ? (Item){.item = *reference->object_home} : make_js_undefined();
        if (!js_interp_is_undefined(base)) {
            Item written = js_set_with_binding_base(base,
                key_root.get(), value_root.get(), frame->strict ? 1 : 0);
            if (item_is_error(written)) return written;
            if (js_is_truthy(written)) return value_root.get();
        }
        Item written = js_set_last_with_binding_if_valid(
            key_root.get(), value_root.get(), frame->strict ? 1 : 0);
        if (item_is_error(written)) return written;
        if (js_is_truthy(written)) return value_root.get();
    }
    if (reference->arguments_env) {
        reference->arguments_env->arguments_object = value_root.get().item;
        reference->arguments_env->arguments_are_mapped = false;
        return value_root.get();
    }
    if (!reference->property) {
        if (reference->unresolvable_binding && frame->strict && !initialize) {
            return js_throw_reference_error(key_root.get());
        }
        return js_interp_write_binding(frame, reference->entry,
            reference->entry ? NULL : it2s(key_root.get()), value_root.get(), initialize,
            reference->binding_uses_eval, reference->with_lookup_completed);
    }
    if (js_interp_private_key(key_root.get())) {
        return js_private_property_set(object_root.get(),
            key_root.get(), value_root.get(), frame->strict ? 1 : 0);
    }
    if (reference->super_property) {
        Item base = reference->super_base_home
            ? (Item){.item = *reference->super_base_home}
            : js_super_get_base(object_root.get());
        return js_super_property_set_from_base(object_root.get(), base,
            key_root.get(), value_root.get(), frame->strict ? 1 : 0);
    }
    if (js_property_lane_is_valid(property_lane)) {
        Item set_result = js_set(object_root.get(), property_lane, key_root.get(),
            value_root.get(), object_root.get());
        return js_assignment_set_result(value_root.get(), key_root.get(),
            set_result, frame->strict ? 1 : 0, object_root.get());
    }
    return js_set_key_policy(object_root.get(), key_root.get(), value_root.get(),
        frame->strict ? 1 : 0);
}

static Item js_interp_binary(Operator op, Item left, Item right) {
    switch (op) {
    case OPERATOR_ADD: return js_add(left, right);
    case OPERATOR_SUB: return js_subtract(left, right);
    case OPERATOR_MUL: return js_multiply(left, right);
    case OPERATOR_DIV: return js_divide(left, right);
    case OPERATOR_MOD: return js_modulo(left, right);
    case OPERATOR_POW:
    case OPERATOR_JS_EXP: return js_power(left, right);
    case OPERATOR_EQ: return js_equal(left, right);
    case OPERATOR_NE: {
        Item equal = js_equal(left, right);
        return item_is_error(equal) ? equal : js_logical_not(equal);
    }
    case OPERATOR_JS_STRICT_EQ: return js_strict_equal(left, right);
    case OPERATOR_JS_STRICT_NE: return js_logical_not(js_strict_equal(left, right));
    case OPERATOR_LT: return js_less_than(left, right);
    // js_compare uses its own compact ABI (LT=0, GT=1, LE=2, GE=3), not
    // AstNode's Operator enum values used by the interpreter dispatch.
    case OPERATOR_LE: return js_compare(2, left, right);
    case OPERATOR_GT: return js_greater_than(left, right);
    case OPERATOR_GE: return js_compare(3, left, right);
    case OPERATOR_JS_BIT_AND: return js_bitwise_and(left, right);
    case OPERATOR_JS_BIT_OR: return js_bitwise_or(left, right);
    case OPERATOR_JS_BIT_XOR: return js_bitwise_xor(left, right);
    case OPERATOR_JS_LSHIFT: return js_left_shift(left, right);
    case OPERATOR_JS_RSHIFT: return js_right_shift(left, right);
    case OPERATOR_JS_URSHIFT: return js_unsigned_right_shift(left, right);
    case OPERATOR_IN: return js_in(left, right);
    case OPERATOR_JS_INSTANCEOF: return js_instanceof(left, right);
    default: return js_throw_type_error("unsupported interpreted binary operator");
    }
}

static Item js_interp_assignment_value(Operator op, Item old_value, Item value) {
    switch (op) {
    case OPERATOR_ASSIGN: return value;
    case OPERATOR_JS_ADD_ASSIGN: return js_add(old_value, value);
    case OPERATOR_JS_SUB_ASSIGN: return js_subtract(old_value, value);
    case OPERATOR_JS_MUL_ASSIGN: return js_multiply(old_value, value);
    case OPERATOR_JS_DIV_ASSIGN: return js_divide(old_value, value);
    case OPERATOR_JS_MOD_ASSIGN: return js_modulo(old_value, value);
    case OPERATOR_JS_EXP_ASSIGN: return js_power(old_value, value);
    case OPERATOR_JS_BIT_AND_ASSIGN: return js_bitwise_and(old_value, value);
    case OPERATOR_JS_BIT_OR_ASSIGN: return js_bitwise_or(old_value, value);
    case OPERATOR_JS_BIT_XOR_ASSIGN: return js_bitwise_xor(old_value, value);
    case OPERATOR_JS_LSHIFT_ASSIGN: return js_left_shift(old_value, value);
    case OPERATOR_JS_RSHIFT_ASSIGN: return js_right_shift(old_value, value);
    case OPERATOR_JS_URSHIFT_ASSIGN: return js_unsigned_right_shift(old_value, value);
    default: return js_throw_type_error("unsupported interpreted assignment operator");
    }
}

static bool js_interp_is_nullish(Item value) {
    return get_type_id(value) == LMD_TYPE_NULL ||
        get_type_id(value) == LMD_TYPE_UNDEFINED;
}

static bool js_interp_is_logical_assignment(Operator op) {
    return op == OPERATOR_JS_AND_ASSIGN || op == OPERATOR_JS_OR_ASSIGN ||
        op == OPERATOR_JS_NULLISH_ASSIGN;
}

static bool js_interp_logical_assignment_keeps_old(Operator op, Item old_value) {
    if (op == OPERATOR_JS_AND_ASSIGN) return !js_is_truthy(old_value);
    if (op == OPERATOR_JS_OR_ASSIGN) return js_is_truthy(old_value);
    return op == OPERATOR_JS_NULLISH_ASSIGN && !js_interp_is_nullish(old_value);
}

static bool js_interp_scope_has_name(NameScope* scope, String* name) {
    if (!scope || !name) return false;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (entry->name && entry->name->len == name->len &&
                memcmp(entry->name->chars, name->chars, name->len) == 0) {
            return true;
        }
    }
    return false;
}

static Item js_interp_binding_raw_value(JsInterpFrame* frame, NameEntry* entry) {
    if (!frame || !entry) return ItemError;
    if (entry->scope == frame->script->global_scope) {
        return js_get_module_var(entry->slot);
    }
    JsInterpEnv* env = js_interp_find_env(frame->env, entry->scope);
    if (!env || entry->slot < 0 || (uint32_t)entry->slot >= env->slot_count) {
        return ItemError;
    }
    return owned_item_slot_read((Item*)(void*)env->slots, env->slot_count,
        entry->slot, false);
}

static void js_interp_eval_bind_scope(JsInterpFrame* frame, NameScope* scope,
        bool global_lexical) {
    if (!frame || !scope) return;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (!entry->name) continue;
        // Top-level vars already use their global-object binding. Mirroring
        // them through the lexical bridge would create then tombstone a fresh
        // shape entry for every direct eval; only lexical module cells need it.
        if (global_lexical && !entry->is_lexical) continue;
        Item key = js_interp_name_key(entry->name);
        Item value = js_interp_binding_raw_value(frame, entry);
        if (global_lexical) {
            js_eval_global_lexical_bind(key, value, entry->is_const ? 1 : 0);
        } else {
            js_eval_env_bind(key, value);
        }
    }
}

static void js_interp_eval_bind_envs(JsInterpFrame* frame, JsInterpEnv* env,
        bool global_lexical) {
    if (!env) return;
    js_interp_eval_bind_envs(frame, env->outer, global_lexical);
    js_interp_eval_bind_scope(frame, env->scope, global_lexical);
}

static bool js_interp_env_name_shadowed_before(JsInterpEnv* inner,
        JsInterpEnv* target, String* name) {
    for (JsInterpEnv* env = inner; env && env != target; env = env->outer) {
        if (js_interp_scope_has_name(env->scope, name)) return true;
    }
    return false;
}

static Item js_interp_eval_writeback_scope(JsInterpFrame* frame,
        NameScope* scope, JsInterpEnv* scope_env, bool lexical_only) {
    if (!frame) return ItemError;
    // Class-private environments carry lexical identity only; direct eval
    // must bridge their names without treating them as writable JS bindings.
    if (!scope) return js_status_ok();
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (!entry->name || entry->is_const || entry->is_function_name_binding ||
                (lexical_only && !entry->is_lexical) ||
                (scope_env && js_interp_env_name_shadowed_before(frame->env,
                    scope_env, entry->name))) {
            continue;
        }
        // A direct eval can observe an outer TDZ binding through the bridge,
        // but it cannot initialize that declaration. Do not turn the bridge's
        // TDZ sentinel into a normal assignment during writeback.
        if (js_interp_binding_raw_value(frame, entry).item == ITEM_JS_TDZ) continue;
        Item key = js_interp_name_key(entry->name);
        RootFrame roots(2);
        Rooted<Item> global_root(roots, js_get_global_this());
        // Global direct eval updates the realm lexical record. Function-scope
        // eval instead updates its temporary own-property bridge.
        // the bridge property is the eval result; resolving through the realm
        // lexical record would mask it with the caller's pre-eval value
        Rooted<Item> value_root(roots, js_get_key_default(global_root.get(), key));
        if (item_is_error(value_root.get())) return value_root.get();
        // The bridge replays writes to pre-eval static bindings. An eval-created
        // `var` in an inner environment must not redirect this writeback and
        // overwrite its own journal value.
        Item written = js_interp_write_binding(frame, entry, entry->name,
            value_root.get(), false, false);
        if (item_is_error(written)) return written;
    }
    return js_status_ok();
}

static Item js_interp_eval_writeback_envs(JsInterpFrame* frame) {
    if (!frame) return ItemError;
    for (JsInterpEnv* env = frame->env; env; env = env->outer) {
        Item status = js_interp_eval_writeback_scope(frame, env->scope, env, false);
        if (item_is_error(status)) return status;
    }
    return js_status_ok();
}

static bool js_interp_frame_has_function_scope(const JsInterpFrame* frame) {
    for (JsInterpEnv* env = frame ? frame->env : NULL; env; env = env->outer) {
        if (env->scope && env->scope->kind == SCOPE_KIND_FUNCTION) return true;
    }
    return false;
}

static void js_interp_eval_note_lexicals(JsInterpEnv* env) {
    if (!env) return;
    js_interp_eval_note_lexicals(env->outer);
    for (NameEntry* entry = env->scope ? env->scope->first : NULL;
            entry; entry = entry->next) {
        if (!entry->name || (!entry->is_lexical &&
                !entry->is_function_name_binding)) continue;
        Item key = js_interp_name_key(entry->name);
        if (entry->is_lexical) js_eval_local_note_lexical_binding(key);
        if (entry->is_const || entry->is_function_name_binding) {
            js_eval_local_note_immutable_binding(key);
        }
    }
}

static bool js_interp_env_has_private_bindings(JsInterpEnv* env) {
    for (; env; env = env->outer) {
        if (get_type_id((Item){.item = env->private_bindings}) == LMD_TYPE_ARRAY) {
            return true;
        }
    }
    return false;
}

static void js_interp_eval_bind_private_envs(JsInterpEnv* env) {
    if (!env) return;
    js_interp_eval_bind_private_envs(env->outer);
    Item bindings = (Item){.item = env->private_bindings};
    if (get_type_id(bindings) != LMD_TYPE_ARRAY) return;
    RootFrame roots(3);
    Rooted<Item> bindings_root(roots, bindings);
    Rooted<Item> source_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    int64_t length = js_array_length(bindings_root.get());
    for (int64_t index = 0; index + 1 < length; index += 2) {
        source_root.set(js_elements_get_int(bindings_root.get(), index));
        key_root.set(js_elements_get_int(bindings_root.get(), index + 1));
        if (item_is_error(source_root.get()) || item_is_error(key_root.get())) return;
        js_eval_private_bind(source_root.get(), key_root.get());
    }
}

struct JsInterpPrivateEvalBridge {
    bool active;

    explicit JsInterpPrivateEvalBridge(JsInterpFrame* frame)
        : active(js_interp_env_has_private_bindings(frame ? frame->env : NULL)) {
        if (!active) return;
        js_eval_private_push_frame();
        // Bind outer classes first so a nested class's declaration shadows
        // an equal spelling exactly as the lexical private environment does.
        js_interp_eval_bind_private_envs(frame->env);
    }

    void close() {
        if (!active) return;
        js_eval_private_pop_frame();
        active = false;
    }

    ~JsInterpPrivateEvalBridge() { close(); }

    JsInterpPrivateEvalBridge(const JsInterpPrivateEvalBridge&) = delete;
    JsInterpPrivateEvalBridge& operator=(const JsInterpPrivateEvalBridge&) = delete;
};

struct JsInterpEvalBridge {
    JsInterpFrame* frame;
    bool global_lexical;
    bool active;

    explicit JsInterpEvalBridge(JsInterpFrame* value)
        : frame(value), global_lexical(!js_interp_frame_has_function_scope(value)),
          active(false) {
        if (!frame) return;
        if (global_lexical) {
            js_eval_global_lexical_push_frame();
            js_interp_eval_bind_scope(frame, frame->script->global_scope, true);
            js_interp_eval_bind_envs(frame, frame->env, true);
        } else {
            js_eval_env_push_frame();
            js_interp_eval_bind_envs(frame, frame->env, false);
            // Preserve vars created by a prior direct eval in this activation.
            js_eval_env_bridge_journal_vars();
        }
        active = true;
    }

    Item writeback() {
        if (!active) return ItemError;
        if (global_lexical) {
            // The direct-eval script sees caller bindings through temporary
            // globals; synchronize both lexical and var cells before removal.
            return js_interp_eval_writeback_scope(frame,
                frame->script ? frame->script->global_scope : NULL, NULL, true);
        }
        return js_interp_eval_writeback_envs(frame);
    }

    void close() {
        if (!active) return;
        if (global_lexical) js_eval_global_lexical_pop_frame();
        else js_eval_env_pop_frame();
        active = false;
    }

    ~JsInterpEvalBridge() { close(); }

    JsInterpEvalBridge(const JsInterpEvalBridge&) = delete;
    JsInterpEvalBridge& operator=(const JsInterpEvalBridge&) = delete;
};

static Item js_interp_capture_eval_bindings(JsInterpFrame* frame) {
    JsInterpEnv* variable_env = js_interp_find_variable_env(frame);
    if (!variable_env) return js_status_ok();
    int64_t count = js_eval_local_current_var_count();
    for (int64_t index = 0; index < count; index++) {
        RootFrame roots(2);
        Rooted<Item> key_root(roots, js_eval_local_current_var_key(index));
        Rooted<Item> value_root(roots, js_eval_local_current_var_value(index));
        if (key_root.get().item == ItemNull.item) continue;
        Item stored = js_interp_env_set_eval_binding(variable_env, key_root.get(),
            value_root.get());
        if (item_is_error(stored)) return stored;
    }
    return js_status_ok();
}

static bool js_interp_eval_redeclares_parameter(JsInterpFrame* frame, Item code) {
    if (!frame || !frame->in_parameter_initializer ||
            get_type_id(code) != LMD_TYPE_STRING || !frame->active_function ||
            !frame->active_function->ast_function) {
        return false;
    }
    String* source = it2s(code);
    JsTranspiler* transpiler = js_transpiler_create(context ? context->runtime : NULL);
    if (!source || !transpiler || !js_transpiler_parse(transpiler, source->chars,
            source->len)) {
        js_transpiler_destroy(transpiler);
        return false;
    }
    JsAstNode* eval_ast = build_js_ast_indexed(transpiler,
        ts_tree_root_node(transpiler->tree));
    bool redeclares_parameter = false;
    if (eval_ast && transpiler->global_scope) {
        for (NameEntry* declared = transpiler->global_scope->first; declared &&
                !redeclares_parameter; declared = declared->next) {
            if (declared->is_lexical || !declared->name) continue;
            for (NameEntry* parameter = frame->active_function->ast_function->vars
                    ? frame->active_function->ast_function->vars->first : NULL;
                    parameter; parameter = parameter->next) {
                if (!parameter->is_parameter || !parameter->name ||
                        parameter->name->len != declared->name->len) {
                    continue;
                }
                if (memcmp(parameter->name->chars, declared->name->chars,
                        parameter->name->len) == 0) {
                    redeclares_parameter = true;
                    break;
                }
            }
        }
    }
    js_transpiler_destroy(transpiler);
    return redeclares_parameter;
}

static Item js_interp_direct_eval(JsInterpFrame* frame, Item code) {
    // EvalDeclarationInstantiation rejects a var/function redeclaration of a
    // parameter in the separate environment created for default parameters.
    if (js_interp_eval_redeclares_parameter(frame, code)) {
        return js_throw_syntax_error(js_make_string(
            "eval declaration conflicts with a parameter binding"));
    }
    // The module slab is not a realm property table. Bridge script bindings
    // too, otherwise a direct eval at top level cannot observe `var`/`let`
    // values held only in the shared EvalContext module state.
    JsInterpEvalBridge bridge(frame);
    if (!bridge.active) return ItemError;
    JsInterpPrivateEvalBridge private_bridge(frame);
    RootFrame roots(3);
    Rooted<Item> result_root(roots, ItemNull);
    Rooted<Item> prior_this_root(roots, js_get_lexical_this_binding());
    Rooted<Item> caller_this_root(roots, js_interp_frame_this_binding(frame));
    js_set_this(caller_this_root.get());
    result_root.set(js_builtin_eval(code, 3 | (frame && frame->strict ? 4 : 0)));
    js_set_this(prior_this_root.get());
    Item captured = js_interp_capture_eval_bindings(frame);
    Item writeback = bridge.writeback();
    private_bridge.close();
    bridge.close();
    if (item_is_error(result_root.get())) return result_root.get();
    if (item_is_error(captured)) return captured;
    return item_is_error(writeback) ? writeback : result_root.get();
}

struct JsInterpEvalLocalFrame {
    bool pushed;

    JsInterpEvalLocalFrame(JsInterpEnv* env, bool needed)
        : pushed(needed && js_eval_local_push_frame() != 0) {
        if (pushed) js_interp_eval_note_lexicals(env);
    }
    ~JsInterpEvalLocalFrame() {
        if (pushed) js_eval_local_pop_frame();
    }
    JsInterpEvalLocalFrame(const JsInterpEvalLocalFrame&) = delete;
    JsInterpEvalLocalFrame& operator=(const JsInterpEvalLocalFrame&) = delete;
};

static bool js_interp_function_has_direct_eval(JsFunctionNode* function) {
    if (!function) return false;
    for (JsAstNode* param = (JsAstNode*)function->params; param;
            param = (JsAstNode*)param->next) {
        if (js_ast_has_direct_eval_call(param)) return true;
    }
    return js_ast_has_direct_eval_call((JsAstNode*)function->body);
}

static bool js_interp_identifier_is(JsAstNode* node, const char* name);

static JsInterpMemberResult js_interp_eval_call_chain(JsInterpFrame* frame,
        JsCallNode* call, bool construct) {
#define JS_INTERP_CALL_RETURN(value) return {(value), false}
    if (!call) JS_INTERP_CALL_RETURN(js_interp_throw(ItemError));
    RootFrame roots(7);
    Rooted<Item> callee_root(roots, ItemNull);
    Rooted<Item> this_root(roots, make_js_undefined());
    Rooted<Item> key_root(roots, ItemNull);
    Rooted<Item> arguments_root(roots, js_array_new(0));
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> spread_item_root(roots, ItemNull);
    Rooted<Item> super_base_root(roots, ItemNull);
    if (item_is_error(arguments_root.get())) {
        JS_INTERP_CALL_RETURN(js_interp_throw(arguments_root.get()));
    }
    bool super_call = !construct && js_interp_identifier_is(
        (JsAstNode*)call->function, "super");
    bool intrinsic_require = !construct && call->function &&
        call->function->node_type == AST_NODE_IDENT &&
        js_interp_identifier_is((JsAstNode*)call->function, "require") &&
        ((JsIdentifierNode*)call->function)->entry == NULL &&
        !js_with_depth_active();
    bool intrinsic_dynamic_import = !construct && call->function &&
        call->function->node_type == AST_NODE_IDENT &&
        js_interp_identifier_is((JsAstNode*)call->function, "import") &&
        ((JsIdentifierNode*)call->function)->entry == NULL;
    if (super_call) {
        // The generic dispatcher installed this constructor's home class,
        // deferred derived `this` binding, and active new.target. Resolve the
        // live parent through that shared state before evaluating arguments.
        this_root.set(js_get_super_this_value());
        callee_root.set(js_get_super_constructor_from_receiver(this_root.get(),
            make_js_undefined()));
    } else if (call->function && (call->function->node_type == AST_NODE_MEMBER_EXPR ||
            call->function->node_type == AST_NODE_INDEX_EXPR)) {
        JsMemberNode* member = (JsMemberNode*)call->function;
        if (js_interp_member_uses_super(member)) {
            this_root.set(js_interp_super_this(frame));
            if (item_is_error(this_root.get())) {
                JS_INTERP_CALL_RETURN(js_interp_throw(this_root.get()));
            }
        } else if (member->object &&
                (member->object->node_type == AST_NODE_MEMBER_EXPR ||
                    member->object->node_type == AST_NODE_INDEX_EXPR)) {
            JsInterpMemberResult receiver = js_interp_eval_member_chain(frame,
                (JsMemberNode*)member->object);
            if (receiver.completion.kind != JS_INTERP_NORMAL) return receiver;
            if (receiver.optional_short_circuit) {
                return {js_interp_normal(make_js_undefined()), true};
            }
            this_root.set(receiver.completion.value);
        } else {
            JsInterpCompletion receiver = js_interp_eval(frame, (JsAstNode*)member->object);
            if (receiver.kind != JS_INTERP_NORMAL) JS_INTERP_CALL_RETURN(receiver);
            this_root.set(receiver.value);
        }
        if (member->optional && js_interp_is_nullish(this_root.get())) {
            return {js_interp_normal(make_js_undefined()), true};
        }
        bool key_deferred = false;
        JsInterpCompletion key = js_interp_member_key_after_base(frame, member,
            this_root.get(), !js_interp_member_uses_super(member), false,
            key_root.home(), &key_deferred, js_interp_member_uses_super(member)
                ? super_base_root.home() : NULL);
        if (key.kind != JS_INTERP_NORMAL) JS_INTERP_CALL_RETURN(key);
        callee_root.set(js_interp_member_uses_super(member)
            ? js_super_property_get_from_base(this_root.get(), super_base_root.get(),
                key_root.get())
            : js_get_key_default(this_root.get(), key_root.get()));
    } else if (!intrinsic_require && !intrinsic_dynamic_import) {
        JsInterpCompletion callee = js_interp_eval(frame, (JsAstNode*)call->function);
        if (callee.kind != JS_INTERP_NORMAL) JS_INTERP_CALL_RETURN(callee);
        callee_root.set(callee.value);
        if (call->function && call->function->node_type == AST_NODE_IDENT &&
                js_with_depth_active()) {
            this_root.set(js_get_last_with_binding_base_or_undefined(
                js_interp_name_key(((JsIdentifierNode*)call->function)->name)));
        }
    }
    if (call->optional && js_interp_is_nullish(callee_root.get())) {
        return {js_interp_normal(make_js_undefined()), true};
    }
    if (item_is_error(callee_root.get())) {
        JS_INTERP_CALL_RETURN(js_interp_throw(callee_root.get()));
    }
    for (JsAstNode* arg = (JsAstNode*)call->arguments; arg; arg = (JsAstNode*)arg->next) {
        if (arg->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            JsSpreadElementNode* spread = (JsSpreadElementNode*)arg;
            JsInterpCompletion source = js_interp_eval(frame,
                (JsAstNode*)spread->argument);
            if (source.kind != JS_INTERP_NORMAL) JS_INTERP_CALL_RETURN(source);
            value_root.set(source.value);
            value_root.set(js_iterable_to_array(value_root.get()));
            if (item_is_error(value_root.get())) {
                JS_INTERP_CALL_RETURN(js_interp_throw(value_root.get()));
            }
            int64_t length = js_array_length(value_root.get());
            for (int64_t index = 0; index < length; index++) {
                spread_item_root.set(js_elements_get_int(value_root.get(), index));
                if (item_is_error(spread_item_root.get())) {
                    JS_INTERP_CALL_RETURN(js_interp_throw(spread_item_root.get()));
                }
                Item pushed = js_array_push(arguments_root.get(), spread_item_root.get());
                if (item_is_error(pushed)) JS_INTERP_CALL_RETURN(js_interp_throw(pushed));
            }
            continue;
        }
        JsInterpCompletion value = js_interp_eval(frame, arg);
        if (value.kind != JS_INTERP_NORMAL) JS_INTERP_CALL_RETURN(value);
        value_root.set(value.value);
        Item pushed = js_array_push(arguments_root.get(), value_root.get());
        if (item_is_error(pushed)) JS_INTERP_CALL_RETURN(js_interp_throw(pushed));
    }
    if (!construct && call->function && call->function->node_type == AST_NODE_IDENT &&
            js_interp_identifier_is((JsAstNode*)call->function, "eval")) {
        Item intrinsic = js_get_global_builtin_fn_by_id(
            (Item){.item = i2it(JS_BUILTIN_GLOBAL_FN_EVAL)});
        if (js_strict_equal(callee_root.get(), intrinsic).item == b2it(true)) {
            value_root.set(js_elements_get_int(arguments_root.get(), 0));
            if (item_is_error(value_root.get())) {
                JS_INTERP_CALL_RETURN(js_interp_throw(value_root.get()));
            }
            Item result = js_interp_direct_eval(frame, value_root.get());
            JS_INTERP_CALL_RETURN(item_is_error(result) ? js_interp_throw(result)
                : js_interp_normal(result));
        }
    }
    if (intrinsic_require &&
            js_array_length(arguments_root.get()) == 1) {
        // MIR resolves literal require targets against its compilation unit.
        // Preserve that exact rule for AST bodies before entering the shared
        // CJS resolver, whose cache and registry are the single authority.
        value_root.set(js_elements_get_int(arguments_root.get(), 0));
        if (item_is_error(value_root.get())) {
            JS_INTERP_CALL_RETURN(js_interp_throw(value_root.get()));
        }
        if (get_type_id(value_root.get()) == LMD_TYPE_STRING && frame->script &&
                frame->script->reference) {
            String* requested = it2s(value_root.get());
            char resolved[512];
            jm_resolve_module_path(frame->script->reference, requested->chars,
                (int)requested->len, resolved, (int)sizeof(resolved));
            value_root.set(js_make_string(resolved));
        }
        Item result = js_require(value_root.get());
        JS_INTERP_CALL_RETURN(item_is_error(result) ? js_interp_throw(result)
            : js_interp_normal(result));
    }
    if (intrinsic_dynamic_import && js_array_length(arguments_root.get()) >= 1) {
        value_root.set(js_elements_get_int(arguments_root.get(), 0));
        if (item_is_error(value_root.get())) {
            JS_INTERP_CALL_RETURN(js_interp_throw(value_root.get()));
        }
        Item string_value = js_to_string(value_root.get());
        if (item_is_error(string_value)) JS_INTERP_CALL_RETURN(js_interp_throw(string_value));
        String* requested = it2s(string_value);
        char resolved[512];
        jm_resolve_module_path(frame->script && frame->script->reference
            ? frame->script->reference : ".", requested->chars,
            (int)requested->len, resolved, (int)sizeof(resolved));
        value_root.set(js_interp_load_es_module(context ? context->runtime : NULL,
            resolved));
        Item result = item_is_error(value_root.get()) ? js_promise_reject(value_root.get())
            : js_promise_resolve(value_root.get());
        JS_INTERP_CALL_RETURN(item_is_error(result) ? js_interp_throw(result)
            : js_interp_normal(result));
    }
    if (super_call) {
        // SuperCall invokes the parent's [[Construct]] with the active
        // new.target. The runtime helper selects the class or native
        // capability; the bind helper is the single derived-`this` state
        // transition and rejects a duplicate `super()`.
        value_root.set(js_is_class_constructor_value(callee_root.get())
            ? js_super_apply_class_into(callee_root.get(), this_root.get(),
                arguments_root.get(), value_root.home())
            : js_super_apply_native(callee_root.get(), this_root.get(), arguments_root.get()));
        if (item_is_error(value_root.get())) {
            JS_INTERP_CALL_RETURN(js_interp_throw(value_root.get()));
        }
        value_root.set(js_super_bind_this(this_root.get(), value_root.get()));
        if (item_is_error(value_root.get())) {
            JS_INTERP_CALL_RETURN(js_interp_throw(value_root.get()));
        }
        if (frame->this_home) *frame->this_home = value_root.get().item;
        Item home_class = js_interp_frame_home_class(frame);
        if (home_class.item != ItemNull.item && home_class.item != 0) {
            Item initialized = js_init_class_instance_fields_after_super(home_class,
                value_root.get());
            if (item_is_error(initialized)) {
                JS_INTERP_CALL_RETURN(js_interp_throw(initialized));
            }
        }
        JS_INTERP_CALL_RETURN(js_interp_normal(value_root.get()));
    }
    Item result = construct
        ? js_construct_array_like(callee_root.get(), arguments_root.get(), callee_root.get())
        : js_apply_function(callee_root.get(), this_root.get(), arguments_root.get());
    JS_INTERP_CALL_RETURN(item_is_error(result) ? js_interp_throw(result)
        : js_interp_normal(result));
#undef JS_INTERP_CALL_RETURN
}

static JsInterpCompletion js_interp_eval_call(JsInterpFrame* frame, JsCallNode* call,
        bool construct) {
    return js_interp_eval_call_chain(frame, call, construct).completion;
}

static JsInterpCompletion js_interp_eval_template(JsInterpFrame* frame,
        JsTemplateLiteralNode* literal) {
    if (!literal) return js_interp_throw(ItemError);
    RootFrame roots(3);
    Rooted<Item> result_root(roots, js_make_string(""));
    Rooted<Item> chunk_root(roots, ItemNull);
    Rooted<Item> value_root(roots, ItemNull);
    if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
    JsAstNode* expression = literal->expressions;
    for (JsAstNode* quasi = literal->quasis; quasi;
            quasi = (JsAstNode*)quasi->next) {
        if (quasi->node_type != JS_AST_NODE_TEMPLATE_ELEMENT) {
            return js_interp_throw(js_throw_type_error("invalid template element"));
        }
        JsTemplateElementNode* element = (JsTemplateElementNode*)quasi;
        if (!element->cooked) {
            return js_interp_throw(js_throw_syntax_error(
                js_make_string("invalid escape in template literal")));
        }
        chunk_root.set(js_make_string_len(element->cooked->chars,
            (int)element->cooked->len));
        result_root.set(js_add(result_root.get(), chunk_root.get()));
        if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
        if (!element->tail) {
            if (!expression) {
                return js_interp_throw(js_throw_syntax_error(
                    js_make_string("missing template substitution")));
            }
            JsInterpCompletion substituted = js_interp_eval(frame, expression);
            if (substituted.kind != JS_INTERP_NORMAL) return substituted;
            // Template substitutions apply ToString directly. Routing this
            // through `+` would use the default coercion hint instead.
            value_root.set(js_to_string(substituted.value));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            result_root.set(js_add(result_root.get(), value_root.get()));
            if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
            expression = (JsAstNode*)expression->next;
        }
    }
    return expression ? js_interp_throw(js_throw_syntax_error(
        js_make_string("extra template substitution"))) : js_interp_normal(result_root.get());
}

static JsInterpCompletion js_interp_eval_tagged_template(JsInterpFrame* frame,
        JsTaggedTemplateNode* tagged) {
    if (!frame || !tagged || !tagged->quasi) return js_interp_throw(ItemError);
    int count = 0;
    for (JsAstNode* quasi = tagged->quasi->quasis; quasi;
            quasi = (JsAstNode*)quasi->next) count++;
    RootFrame roots(5);
    Rooted<Item> tag_root(roots, ItemNull);
    Rooted<Item> this_root(roots, make_js_undefined());
    Rooted<Item> key_root(roots, ItemNull);
    Rooted<Item> template_root(roots, ItemNull);
    Rooted<Item> arguments_root(roots, js_array_new(0));
    RootSpan cooked_roots((size_t)count);
    RootSpan raw_roots((size_t)count);
    Item* cooked = (Item*)(void*)cooked_roots.words();
    Item* raw = (Item*)(void*)raw_roots.words();
    if (item_is_error(arguments_root.get())) return js_interp_throw(arguments_root.get());

    if (tagged->tag->node_type == AST_NODE_MEMBER_EXPR ||
            tagged->tag->node_type == AST_NODE_INDEX_EXPR) {
        JsMemberNode* member = (JsMemberNode*)tagged->tag;
        bool super_property = js_interp_member_uses_super(member);
        if (super_property) {
            this_root.set(js_interp_super_this(frame));
            if (item_is_error(this_root.get())) return js_interp_throw(this_root.get());
        } else {
            JsInterpCompletion receiver = js_interp_eval(frame, (JsAstNode*)member->object);
            if (receiver.kind != JS_INTERP_NORMAL) return receiver;
            this_root.set(receiver.value);
        }
        JsInterpCompletion key = js_interp_property_key(frame, member);
        if (key.kind != JS_INTERP_NORMAL) return key;
        key_root.set(key.value);
        tag_root.set(super_property
            ? js_super_property_get(this_root.get(), key_root.get())
            : js_get_key_default(this_root.get(), key_root.get()));
    } else {
        JsInterpCompletion tag = js_interp_eval(frame, tagged->tag);
        if (tag.kind != JS_INTERP_NORMAL) return tag;
        tag_root.set(tag.value);
        if (tagged->tag->node_type == AST_NODE_IDENT && js_with_depth_active()) {
            this_root.set(js_get_last_with_binding_base_or_undefined(
                js_interp_name_key(((JsIdentifierNode*)tagged->tag)->name)));
        }
    }
    if (item_is_error(tag_root.get())) return js_interp_throw(tag_root.get());

    int index = 0;
    for (JsAstNode* quasi = tagged->quasi->quasis; quasi;
            quasi = (JsAstNode*)quasi->next, index++) {
        if (quasi->node_type != JS_AST_NODE_TEMPLATE_ELEMENT) {
            return js_interp_throw(js_throw_syntax_error(
                js_make_string("invalid tagged template element")));
        }
        JsTemplateElementNode* element = (JsTemplateElementNode*)quasi;
        raw[index] = js_make_string_len(element->raw ? element->raw->chars : "",
            element->raw ? (int)element->raw->len : 0);
        cooked[index] = element->cooked
            ? js_make_string_len(element->cooked->chars, (int)element->cooked->len)
            : make_js_undefined();
        if (item_is_error(raw[index]) || item_is_error(cooked[index])) {
            return js_interp_throw(item_is_error(raw[index]) ? raw[index] : cooked[index]);
        }
    }
    // Nested tagged applications share their enclosing expression span; the
    // template literal itself is the unique GetTemplateObject call site.
    int64_t site_id = ((int64_t)frame->script->index << 32) |
        (int64_t)tagged->quasi->source_span.start_byte;
    template_root.set(js_build_template_object_cached(cooked, raw, count, site_id));
    if (item_is_error(template_root.get())) return js_interp_throw(template_root.get());
    Item pushed = js_array_push(arguments_root.get(), template_root.get());
    if (item_is_error(pushed)) return js_interp_throw(pushed);
    for (JsAstNode* expression = tagged->quasi->expressions; expression;
            expression = (JsAstNode*)expression->next) {
        JsInterpCompletion value = js_interp_eval(frame, expression);
        if (value.kind != JS_INTERP_NORMAL) return value;
        pushed = js_array_push(arguments_root.get(), value.value);
        if (item_is_error(pushed)) return js_interp_throw(pushed);
    }
    Item result = js_apply_function(tag_root.get(), this_root.get(), arguments_root.get());
    return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
}

static bool js_interp_is_undefined(Item value) {
    return value.item == ITEM_JS_UNDEFINED || get_type_id(value) == LMD_TYPE_UNDEFINED;
}

static void js_interp_set_parameter_pattern_tdz(JsInterpFrame* frame,
        JsAstNode* pattern) {
    if (!frame || !pattern) return;
    switch (pattern->node_type) {
    case AST_NODE_IDENT: {
        JsIdentifierNode* identifier = (JsIdentifierNode*)pattern;
        NameEntry* entry = identifier->entry ? identifier->entry
            : js_interp_find_binding(frame, identifier->name);
        JsInterpEnv* env = entry ? js_interp_find_env(frame->env, entry->scope) : NULL;
        if (env && entry->slot >= 0 && (uint32_t)entry->slot < env->slot_count) {
            owned_item_slot_store((Item*)(void*)env->slots, env->slot_count,
                entry->slot, (Item){.item = ITEM_JS_TDZ});
        }
        return;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        js_interp_set_parameter_pattern_tdz(frame,
            (JsAstNode*)((JsAssignmentPatternNode*)pattern)->left);
        return;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        js_interp_set_parameter_pattern_tdz(frame,
            (JsAstNode*)((JsSpreadElementNode*)pattern)->argument);
        return;
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* element = (JsAstNode*)((JsArrayPatternNode*)pattern)->elements;
                element; element = (JsAstNode*)element->next) {
            js_interp_set_parameter_pattern_tdz(frame, element);
        }
        return;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* property = (JsAstNode*)((JsObjectPatternNode*)pattern)->properties;
                property; property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_REST_PROPERTY) {
                js_interp_set_parameter_pattern_tdz(frame, property);
            } else if (property->node_type == AST_NODE_PROPERTY) {
                js_interp_set_parameter_pattern_tdz(frame,
                    (JsAstNode*)((JsPropertyNode*)property)->value);
            }
        }
        return;
    default:
        return;
    }
}

static void js_interp_prepare_parameter_tdz(JsInterpFrame* frame,
        JsFunctionNode* function) {
    // FunctionDeclarationInstantiation creates every formal binding before
    // evaluating defaults, but only initializes them left-to-right. Keep later
    // parameter names in TDZ rather than the scope's ordinary `undefined`.
    for (JsAstNode* param = function ? (JsAstNode*)function->params : NULL;
            param; param = (JsAstNode*)param->next) {
        js_interp_set_parameter_pattern_tdz(frame, param);
    }
}

static JsInterpCompletion js_interp_finish_array_binding(Item iterator,
        bool iterator_done, JsInterpCompletion completion) {
    // A suspended initializer resumes the surrounding generator before its
    // destructuring can finish, so its iterator must remain live.
    if (iterator_done || completion.kind == JS_INTERP_YIELD ||
            completion.kind == JS_INTERP_AWAIT) {
        return completion;
    }
    RootFrame roots(1);
    Rooted<Item> completion_root(roots, completion.value);
    Item closed = js_iterator_close(iterator);
    // IteratorClose retains a pre-existing throw, even when `.return()` fails.
    if (completion.kind == JS_INTERP_THROW) {
        completion.value = completion_root.get();
        return completion;
    }
    if (item_is_error(closed)) return js_interp_throw(closed);
    completion.value = completion_root.get();
    return completion;
}

static JsAstNode* js_interp_unwrap_name_initializer(JsAstNode* initializer) {
    while (initializer && initializer->node_type == AST_NODE_PRIMARY) {
        initializer = (JsAstNode*)((AstPrimaryNode*)initializer)->expr;
    }
    return initializer;
}

static bool js_interp_is_anonymous_function_definition(JsAstNode* initializer) {
    initializer = js_interp_unwrap_name_initializer(initializer);
    return initializer && (initializer->node_type == AST_NODE_FUNC_EXPR ||
        initializer->node_type == AST_NODE_ARROW_FUNC ||
        initializer->node_type == JS_AST_NODE_CLASS_EXPRESSION);
}

static void js_interp_infer_binding_name(JsAstNode* target,
        JsAstNode* initializer, Item value) {
    if (!target || target->node_type != AST_NODE_IDENT) return;
    if (!js_interp_is_anonymous_function_definition(initializer)) return;
    JsIdentifierNode* identifier = (JsIdentifierNode*)target;
    // NamedEvaluation retains an explicit source-level name when present.
    js_set_function_name_if_anonymous(value, js_interp_name_key(identifier->name));
}

static JsInterpCompletion js_interp_write_pattern_assignment_target(
        JsInterpFrame* frame, JsAstNode* target, Item value) {
    RootFrame roots(2);
    Rooted<Item> object_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    JsInterpReference reference;
    // Assignment patterns accept property references where declarations only
    // permit bindings; use the ordinary reference path to retain setter and
    // computed-key semantics.
    JsInterpCompletion resolved = js_interp_eval_reference(frame, target, &reference,
        object_root.home(), key_root.home());
    if (resolved.kind != JS_INTERP_NORMAL) return resolved;
    Item stored = js_interp_reference_write(frame, &reference, value, false);
    return item_is_error(stored) ? js_interp_throw(stored) : js_interp_normal(stored);
}

static JsAstNode* js_interp_pattern_assignment_reference_target(JsAstNode* pattern) {
    if (pattern && pattern->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        pattern = (JsAstNode*)((JsAssignmentPatternNode*)pattern)->left;
    }
    return pattern && (pattern->node_type == AST_NODE_IDENT ||
        pattern->node_type == AST_NODE_MEMBER_EXPR ||
        pattern->node_type == AST_NODE_INDEX_EXPR) ? pattern : NULL;
}

static JsInterpCompletion js_interp_bind_pattern(JsInterpFrame* frame,
        JsAstNode* pattern, Item input, bool initialize,
        const JsInterpReference* pre_reference = NULL) {
    if (!pattern) return js_interp_throw(js_throw_type_error("missing binding pattern"));
    switch (pattern->node_type) {
    case AST_NODE_IDENT: {
        JsIdentifierNode* identifier = (JsIdentifierNode*)pattern;
        if (pre_reference) {
            Item stored = js_interp_reference_write(frame, pre_reference, input, initialize);
            return item_is_error(stored) ? js_interp_throw(stored)
                : js_interp_normal(stored);
        }
        // Parameter patterns are built before their bindings are registered,
        // so their identifier node can retain a null pre-binding lookup.
        NameEntry* entry = identifier->entry ? identifier->entry
            : js_interp_find_binding(frame, identifier->name);
        Item stored = js_interp_write_binding(frame, entry,
            identifier->name, input, initialize);
        return item_is_error(stored) ? js_interp_throw(stored) : js_interp_normal(stored);
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        JsAssignmentPatternNode* assignment = (JsAssignmentPatternNode*)pattern;
        RootFrame roots(1);
        Rooted<Item> value_root(roots, input);
        if (js_interp_is_undefined(value_root.get())) {
            JsInterpCompletion fallback = js_interp_eval(frame,
                (JsAstNode*)assignment->right);
            if (fallback.kind != JS_INTERP_NORMAL) return fallback;
            value_root.set(fallback.value);
            js_interp_infer_binding_name((JsAstNode*)assignment->left,
                (JsAstNode*)assignment->right, value_root.get());
        }
        return js_interp_bind_pattern(frame, (JsAstNode*)assignment->left,
            value_root.get(), initialize, pre_reference);
    }
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY: {
        // A direct rest parameter receives its already-materialized array;
        // array/object patterns consume their rest member in their own case.
        JsSpreadElementNode* rest = (JsSpreadElementNode*)pattern;
        return js_interp_bind_pattern(frame, (JsAstNode*)rest->argument,
            input, initialize);
    }
    case JS_AST_NODE_ARRAY_PATTERN: {
        JsArrayPatternNode* array = (JsArrayPatternNode*)pattern;
        RootFrame roots(5);
        Rooted<Item> source_root(roots, input);
        Rooted<Item> iterator_root(roots, ItemNull);
        Rooted<Item> value_root(roots, ItemNull);
        Rooted<Item> reference_object_root(roots, ItemNull);
        Rooted<Item> reference_key_root(roots, ItemNull);
        JsInterpGeneratorArrayBindingContinuation* resume =
            js_interp_generator_find_array_binding(frame, pattern);
        // Binding patterns consume iterators one entry at a time. Materializing
        // first both starts generators too eagerly and misses IteratorClose.
        if (resume) {
            iterator_root.set(resume->iterator);
        } else {
            iterator_root.set(js_get_iterator_lazy(source_root.get()));
            if (item_is_error(iterator_root.get())) return js_interp_throw(iterator_root.get());
        }
        bool iterator_done = resume ? resume->iterator_done : false;
        bool skipping_to_resume = resume != NULL;
        for (JsAstNode* element = (JsAstNode*)array->elements; element;
                element = (JsAstNode*)element->next) {
            if (skipping_to_resume && element != resume->element) continue;
            bool resumed_element = skipping_to_resume;
            skipping_to_resume = false;
            if (element->node_type == AST_NODE_NULL) {
                if (iterator_done) continue;
                value_root.set(js_iterator_step(iterator_root.get()));
                if (item_is_error(value_root.get())) {
                    // IteratorStep's own abrupt completion is not closed.
                    return js_interp_throw(value_root.get());
                }
                iterator_done = value_root.get().item == JS_ITER_DONE_SENTINEL;
                continue;
            }
            if (element->node_type == JS_AST_NODE_REST_ELEMENT ||
                    element->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* rest = (JsSpreadElementNode*)element;
                JsInterpReference reference = {};
                bool has_reference = false;
                if (resumed_element && resume->value_ready) {
                    value_root.set(resume->value);
                    if (resume->has_reference) {
                        reference_object_root.set(resume->reference_object);
                        reference_key_root.set(resume->reference_key);
                        reference.object_home = reference_object_root.home();
                        reference.key_home = reference_key_root.home();
                        reference.property = true;
                        reference.super_property = resume->reference_super_property;
                        reference.property_lane = resume->reference_lane;
                        reference.property_key_deferred = resume->reference_key_deferred;
                        has_reference = true;
                    }
                } else {
                    JsAstNode* reference_target = !initialize
                        ? js_interp_pattern_assignment_reference_target(
                            (JsAstNode*)rest->argument) : NULL;
                    if (reference_target) {
                        JsInterpCompletion resolved = js_interp_eval_reference(frame,
                            reference_target, &reference, reference_object_root.home(),
                            reference_key_root.home(), true);
                        if (resolved.kind != JS_INTERP_NORMAL) {
                            if (resolved.kind == JS_INTERP_YIELD &&
                                    !js_interp_generator_suspend_array_binding(frame, pattern,
                                        element, iterator_root.get(), iterator_done,
                                        ItemNull, false, NULL)) {
                                return js_interp_throw(ItemError);
                            }
                            if (resolved.kind != JS_INTERP_YIELD &&
                                    resolved.kind != JS_INTERP_AWAIT) {
                                js_interp_generator_clear_array_binding(frame, pattern);
                            }
                            return js_interp_finish_array_binding(iterator_root.get(),
                                iterator_done, resolved);
                        }
                        has_reference = true;
                    }
                    value_root.set(iterator_done ? js_array_new(0)
                        : js_iterator_collect_rest(iterator_root.get()));
                    if (item_is_error(value_root.get())) {
                        // CollectRest only fails through IteratorStep, which
                        // has not completed an ArrayBindingInitialization step.
                        return js_interp_throw(value_root.get());
                    }
                    iterator_done = true;
                }
                JsInterpCompletion bound = js_interp_bind_pattern(frame,
                    (JsAstNode*)rest->argument, value_root.get(), initialize,
                    has_reference ? &reference : NULL);
                if (bound.kind == JS_INTERP_YIELD && !js_interp_generator_suspend_array_binding(
                    frame, pattern, element, iterator_root.get(), iterator_done,
                        value_root.get(), true, has_reference ? &reference : NULL)) {
                    return js_interp_throw(ItemError);
                }
                if (bound.kind != JS_INTERP_YIELD && bound.kind != JS_INTERP_AWAIT) {
                    js_interp_generator_clear_array_binding(frame, pattern);
                }
                return js_interp_finish_array_binding(iterator_root.get(), iterator_done, bound);
            }
            JsInterpReference reference = {};
            bool has_reference = false;
            if (resumed_element && resume->value_ready) {
                value_root.set(resume->value);
                if (resume->has_reference) {
                    reference_object_root.set(resume->reference_object);
                    reference_key_root.set(resume->reference_key);
                    reference.object_home = reference_object_root.home();
                    reference.key_home = reference_key_root.home();
                    reference.property = true;
                    reference.super_property = resume->reference_super_property;
                    reference.property_lane = resume->reference_lane;
                    reference.property_key_deferred = resume->reference_key_deferred;
                    has_reference = true;
                }
            } else {
                JsAstNode* reference_target = !initialize
                    ? js_interp_pattern_assignment_reference_target(element) : NULL;
                if (reference_target) {
                    JsInterpCompletion resolved = js_interp_eval_reference(frame,
                        reference_target, &reference, reference_object_root.home(),
                        reference_key_root.home(), true);
                    if (resolved.kind != JS_INTERP_NORMAL) {
                        if (resolved.kind == JS_INTERP_YIELD &&
                                !js_interp_generator_suspend_array_binding(frame, pattern,
                                    element, iterator_root.get(), iterator_done,
                                    ItemNull, false, NULL)) {
                            return js_interp_throw(ItemError);
                        }
                        if (resolved.kind != JS_INTERP_YIELD &&
                                resolved.kind != JS_INTERP_AWAIT) {
                            js_interp_generator_clear_array_binding(frame, pattern);
                        }
                        return js_interp_finish_array_binding(iterator_root.get(),
                            iterator_done, resolved);
                    }
                    has_reference = true;
                }
                if (iterator_done) {
                    value_root.set(make_js_undefined());
                } else {
                    value_root.set(js_iterator_step(iterator_root.get()));
                    if (item_is_error(value_root.get())) {
                        // IteratorStep failures bypass IteratorClose.
                        return js_interp_throw(value_root.get());
                    }
                    if (value_root.get().item == JS_ITER_DONE_SENTINEL) {
                        iterator_done = true;
                        value_root.set(make_js_undefined());
                    }
                }
            }
            if (resumed_element && !resume->value_ready) {
                // A reference target yielded before IteratorStep. Its resumed
                // evaluation above now owns the first iterator advancement.
                has_reference = reference.property;
            }
            if (iterator_done && !resumed_element) {
                value_root.set(make_js_undefined());
            }
            JsInterpCompletion bound = js_interp_bind_pattern(frame, element,
                value_root.get(), initialize, has_reference ? &reference : NULL);
            if (bound.kind == JS_INTERP_YIELD && !js_interp_generator_suspend_array_binding(
                    frame, pattern, element, iterator_root.get(), iterator_done,
                    value_root.get(), true, has_reference ? &reference : NULL)) {
                return js_interp_throw(ItemError);
            }
            if (bound.kind != JS_INTERP_YIELD && bound.kind != JS_INTERP_AWAIT &&
                    resumed_element) {
                js_interp_generator_clear_array_binding(frame, pattern);
            }
            if (bound.kind != JS_INTERP_NORMAL) {
                return js_interp_finish_array_binding(iterator_root.get(), iterator_done, bound);
            }
        }
        js_interp_generator_clear_array_binding(frame, pattern);
        return js_interp_finish_array_binding(iterator_root.get(), iterator_done,
            js_interp_normal(make_js_undefined()));
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        JsObjectPatternNode* object = (JsObjectPatternNode*)pattern;
        int property_count = 0;
        JsSpreadElementNode* rest = NULL;
        for (JsAstNode* property = (JsAstNode*)object->properties; property;
                property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_REST_PROPERTY) {
                rest = (JsSpreadElementNode*)property;
            } else if (property->node_type == AST_NODE_PROPERTY) {
                property_count++;
            } else {
                return js_interp_throw(js_throw_type_error("invalid object binding pattern"));
            }
        }
        RootFrame roots(5);
        Rooted<Item> source_root(roots, input);
        Rooted<Item> key_root(roots, ItemNull);
        Rooted<Item> value_root(roots, ItemNull);
        Rooted<Item> reference_object_root(roots, ItemNull);
        Rooted<Item> reference_key_root(roots, ItemNull);
        // Empty object patterns still perform RequireObjectCoercible before
        // binding any properties, matching the shared MIR lowering path.
        source_root.set(js_require_object_coercible(source_root.get()));
        if (item_is_error(source_root.get())) return js_interp_throw(source_root.get());
        RootSpan excluded_roots(property_count > 0 ? (size_t)property_count : 0);
        Item* excluded = property_count > 0 ? (Item*)(void*)excluded_roots.words() : NULL;
        int excluded_count = 0;
        for (JsAstNode* property = (JsAstNode*)object->properties; property;
                property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_REST_PROPERTY) continue;
            JsPropertyNode* pair = (JsPropertyNode*)property;
            if (!pair->computed && pair->key && pair->key->node_type == AST_NODE_IDENT) {
                key_root.set(js_interp_name_key(((JsIdentifierNode*)pair->key)->name));
            } else {
                JsInterpCompletion key = js_interp_eval(frame, (JsAstNode*)pair->key);
                if (key.kind != JS_INTERP_NORMAL) return key;
                key_root.set(js_interp_property_key_value(key.value));
            }
            if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
            excluded[excluded_count++] = key_root.get();
            JsInterpReference reference = {};
            const JsInterpReference* pre_reference = NULL;
            JsAstNode* reference_target = js_interp_pattern_assignment_reference_target(
                (JsAstNode*)pair->value);
            if (reference_target) {
                // Object binding resolves its target before GetV, preserving
                // `with` and Proxy environment lookup order.
                JsInterpCompletion resolved = js_interp_eval_reference(frame,
                    reference_target, &reference, reference_object_root.home(),
                    reference_key_root.home(), true);
                if (resolved.kind != JS_INTERP_NORMAL) return resolved;
                pre_reference = &reference;
            }
            value_root.set(js_get_key_default(source_root.get(), key_root.get()));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            JsInterpCompletion bound = js_interp_bind_pattern(frame,
                (JsAstNode*)pair->value, value_root.get(), initialize, pre_reference);
            if (bound.kind != JS_INTERP_NORMAL) return bound;
        }
        if (!rest) return js_interp_normal(make_js_undefined());
        value_root.set(js_object_rest(source_root.get(), excluded, excluded_count));
        if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
        return js_interp_bind_pattern(frame, (JsAstNode*)rest->argument,
            value_root.get(), initialize);
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR: {
        if (pre_reference) {
            Item stored = js_interp_reference_write(frame, pre_reference, input, false);
            return item_is_error(stored) ? js_interp_throw(stored)
                : js_interp_normal(stored);
        }
        return js_interp_write_pattern_assignment_target(frame, pattern, input);
    }
    default:
        return js_interp_throw(js_throw_type_error("unsupported binding pattern"));
    }
}

static bool js_interp_yield_argument_can_suspend(JsAstNode* node);

static JsInterpMemberResult js_interp_eval_member_chain(JsInterpFrame* frame,
        JsMemberNode* member) {
    if (!member) return {js_interp_throw(ItemError), false};
    RootFrame roots(3);
    Rooted<Item> object_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    Rooted<Item> super_base_root(roots, ItemNull);
    bool super_property = js_interp_member_uses_super(member);
    if (super_property) {
        object_root.set(js_interp_super_this(frame));
        if (item_is_error(object_root.get())) {
            return {js_interp_throw(object_root.get()), false};
        }
    } else if (member->object &&
            (member->object->node_type == AST_NODE_MEMBER_EXPR ||
                member->object->node_type == AST_NODE_INDEX_EXPR)) {
        JsInterpMemberResult object = js_interp_eval_member_chain(frame,
            (JsMemberNode*)member->object);
        if (object.completion.kind != JS_INTERP_NORMAL) return object;
        if (object.optional_short_circuit) {
            return {js_interp_normal(make_js_undefined()), true};
        }
        object_root.set(object.completion.value);
    } else if (member->object && member->object->node_type == AST_NODE_CALL_EXPR) {
        JsInterpMemberResult object = js_interp_eval_call_chain(frame,
            (JsCallNode*)member->object, false);
        if (object.completion.kind != JS_INTERP_NORMAL) return object;
        if (object.optional_short_circuit) {
            return {js_interp_normal(make_js_undefined()), true};
        }
        object_root.set(object.completion.value);
    } else {
        JsInterpCompletion object = js_interp_eval(frame, (JsAstNode*)member->object);
        if (object.kind != JS_INTERP_NORMAL) return {object, false};
        object_root.set(object.value);
    }
    if (member->optional && js_interp_is_nullish(object_root.get())) {
        return {js_interp_normal(make_js_undefined()), true};
    }
    bool key_deferred = false;
    JsInterpCompletion key = js_interp_member_key_after_base(frame, member,
        object_root.get(), !super_property, false, key_root.home(), &key_deferred,
        super_property ? super_base_root.home() : NULL);
    if (key.kind != JS_INTERP_NORMAL) return {key, false};
    Item result = super_property
        ? js_super_property_get_from_base(object_root.get(), super_base_root.get(),
            key_root.get())
        // Member-expression reads must preserve nullish-reference errors;
        // js_get_key_default is a generic object operation and may box it.
        : js_get(object_root.get(), js_property_lane_for_canonical_key(
            key_root.get()), key_root.get(), object_root.get());
    return {item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result), false};
}

static JsInterpCompletion js_interp_eval(JsInterpFrame* frame, JsAstNode* node) {
    if (!node) return js_interp_normal(make_js_undefined());
    switch (node->node_type) {
    case AST_NODE_LITERAL: {
        JsLiteralNode* literal = (JsLiteralNode*)node;
        switch (literal->literal_type) {
        case JS_LITERAL_NUMBER:
            return js_interp_normal(literal->is_bigint
                ? bigint_from_string(literal->bigint_str->chars, literal->bigint_str->len)
                : js_make_number(literal->value.number_value));
        case JS_LITERAL_STRING:
            return js_interp_normal(js_make_string_len(literal->value.string_value->chars,
                literal->value.string_value->len));
        case JS_LITERAL_BOOLEAN:
            return js_interp_normal((Item){.item = b2it(literal->value.boolean_value)});
        case JS_LITERAL_NULL: return js_interp_normal(ItemNull);
        case JS_LITERAL_UNDEFINED: return js_interp_normal(make_js_undefined());
        default: return js_interp_throw(ItemError);
        }
    }
    case AST_NODE_IDENT: {
        JsIdentifierNode* identifier = (JsIdentifierNode*)node;
        Item value = js_interp_read_binding(frame, identifier->entry, identifier->name);
        return item_is_error(value) ? js_interp_throw(value) : js_interp_normal(value);
    }
    case AST_NODE_NULL:
        // Array elisions are preserved as null AST nodes. Array construction
        // keeps them as holes, while expression-list consumers observe their
        // specified undefined value.
        return js_interp_normal(make_js_undefined());
    case JS_AST_NODE_CLASS_EXPRESSION:
        return js_interp_eval_class(frame, (JsClassNode*)node, false);
    case AST_NODE_PRIMARY:
        return js_interp_eval(frame, (JsAstNode*)((AstPrimaryNode*)node)->expr);
    case AST_NODE_EXPR_STMT:
        // Loop headers can retain an expression-statement wrapper from the
        // shared statement builder; its completion is the wrapped expression.
        return js_interp_eval(frame,
            (JsAstNode*)((JsExpressionStatementNode*)node)->expression);
    case AST_NODE_UNARY: {
        JsUnaryNode* unary = (JsUnaryNode*)node;
        if (unary->op == OPERATOR_JS_TYPEOF && unary->operand &&
                unary->operand->node_type == AST_NODE_IDENT) {
            JsIdentifierNode* identifier = (JsIdentifierNode*)unary->operand;
            NameEntry* entry = identifier->entry ? identifier->entry
                : js_interp_find_binding(frame, identifier->name);
            bool has_arguments_binding = js_interp_name_equals(identifier->name,
                "arguments") && js_interp_arguments_env_for_binding(frame, entry) != NULL;
            // The AST retains `this`, `new.target`, and `import.meta` as
            // identifier-shaped nodes, but they are activation/module bindings
            // rather than unresolvable references. Let the normal evaluator
            // resolve them instead of folding `typeof this` to "undefined".
            bool has_special_binding = js_interp_name_equals(identifier->name, "this") ||
                js_interp_name_equals(identifier->name, "new.target") ||
                js_interp_name_equals(identifier->name, "import.meta");
            // ECMAScript's `typeof` is the one identifier consumer that does
            // not throw for an unresolvable reference. A lexical TDZ still
            // has an entry and therefore follows the regular error path.
            if (!has_special_binding && !has_arguments_binding && !entry && !js_global_binding_exists(
                    js_interp_name_key(identifier->name)) &&
                    !js_interp_import_binding(frame->script, identifier->name) &&
                    !js_eval_local_has_var_binding(
                        js_interp_name_key(identifier->name))) {
                return js_interp_normal(js_make_string("undefined"));
            }
        }
        if (unary->op == OPERATOR_JS_INCREMENT || unary->op == OPERATOR_JS_DECREMENT) {
            RootFrame roots(6);
            Rooted<Item> reference_object_root(roots, ItemNull);
            Rooted<Item> reference_key_root(roots, ItemNull);
            Rooted<Item> super_base_root(roots, ItemNull);
            JsInterpReference reference;
            JsInterpCompletion ref = js_interp_eval_reference(frame,
                (JsAstNode*)unary->operand, &reference,
                reference_object_root.home(), reference_key_root.home(), false,
                super_base_root.home());
            if (ref.kind != JS_INTERP_NORMAL) return ref;
            Rooted<Item> old_root(roots, js_interp_reference_read(frame, &reference));
            Rooted<Item> numeric_root(roots, js_to_numeric(old_root.get()));
            Rooted<Item> next_root(roots, unary->op == OPERATOR_JS_INCREMENT
                ? js_increment(numeric_root.get()) : js_decrement(numeric_root.get()));
            if (item_is_error(old_root.get()) || item_is_error(numeric_root.get()) ||
                    item_is_error(next_root.get())) {
                return js_interp_throw(item_is_error(old_root.get()) ? old_root.get()
                    : (item_is_error(numeric_root.get()) ? numeric_root.get() : next_root.get()));
            }
            Item set = js_interp_reference_write(frame, &reference, next_root.get(), false);
            if (item_is_error(set)) return js_interp_throw(set);
            // Postfix operators return the already-coerced numeric value, not
            // the original object or primitive read from the reference.
            return js_interp_normal(unary->prefix ? next_root.get() : numeric_root.get());
        }
        if (unary->op == OPERATOR_JS_DELETE) {
            if (unary->operand && (unary->operand->node_type == AST_NODE_MEMBER_EXPR ||
                    unary->operand->node_type == AST_NODE_INDEX_EXPR)) {
                RootFrame roots(3);
                Rooted<Item> object_root(roots, ItemNull);
                Rooted<Item> key_root(roots, ItemNull);
                Rooted<Item> super_base_root(roots, ItemNull);
                JsInterpReference reference;
                JsMemberNode* member = (JsMemberNode*)unary->operand;
                bool super_property = js_interp_member_uses_super(member);
                JsInterpCompletion resolved = js_interp_eval_reference(frame,
                    (JsAstNode*)unary->operand, &reference, object_root.home(), key_root.home(),
                    super_property, super_base_root.home());
                if (resolved.kind != JS_INTERP_NORMAL) return resolved;
                // Evaluate the super reference before rejecting it: a null
                // super base must not preempt delete's mandated ReferenceError.
                if (reference.super_property) {
                    return js_interp_throw(js_throw_reference_error(js_make_string(
                        "Cannot delete a super property")));
                }
                Item deleted = js_delete_property(js_interp_reference_object(&reference),
                    js_interp_reference_key(&reference));
                Item result = js_delete_reference_result(js_interp_reference_key(&reference),
                    deleted, frame->strict ? 1 : 0);
                return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
            }
            if (unary->operand && unary->operand->node_type == AST_NODE_IDENT) {
                JsIdentifierNode* identifier = (JsIdentifierNode*)unary->operand;
                if (js_interp_name_equals(identifier->name, "arguments") &&
                        js_interp_arguments_env_for_binding(frame, identifier->entry)) {
                    return js_interp_normal((Item){.item = b2it(false)});
                }
                Item result = js_delete_identifier_with_binding(
                    js_interp_name_key(identifier->name), identifier->entry ? 1 : 0);
                return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
            }
            JsInterpCompletion value = js_interp_eval(frame, (JsAstNode*)unary->operand);
            return value.kind == JS_INTERP_NORMAL ? js_interp_normal((Item){.item = b2it(true)})
                : value;
        }
        RootFrame roots(1);
        Rooted<Item> operand_root(roots, ItemNull);
        JsInterpCompletion operand = js_interp_eval(frame, (JsAstNode*)unary->operand);
        if (operand.kind != JS_INTERP_NORMAL) return operand;
        operand_root.set(operand.value);
        Item result = ItemError;
        switch (unary->op) {
        case OPERATOR_NOT: result = js_logical_not(operand_root.get()); break;
        case OPERATOR_POS: result = js_unary_plus(operand_root.get()); break;
        case OPERATOR_NEG: result = js_unary_minus(operand_root.get()); break;
        case OPERATOR_JS_BIT_NOT: result = js_bitwise_not(operand_root.get()); break;
        case OPERATOR_JS_TYPEOF: result = js_typeof(operand_root.get()); break;
        case OPERATOR_JS_VOID: result = make_js_undefined(); break;
        default: result = js_throw_type_error("unsupported interpreted unary operator"); break;
        }
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    case AST_NODE_BINARY: {
        JsBinaryNode* binary = (JsBinaryNode*)node;
        if (binary->op == OPERATOR_IN && binary->left &&
                binary->left->node_type == AST_NODE_IDENT) {
            Item source_key = js_interp_name_key(
                ((JsIdentifierNode*)binary->left)->name);
            if (js_interp_private_source_name(source_key)) {
                RootFrame roots(2);
                Rooted<Item> key_root(roots,
                    js_interp_private_key_for_frame(frame, source_key));
                if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
                JsInterpCompletion right = js_interp_eval(frame,
                    (JsAstNode*)binary->right);
                if (right.kind != JS_INTERP_NORMAL) return right;
                Rooted<Item> object_root(roots, right.value);
                Item result = js_private_in(object_root.get(), key_root.get());
                return item_is_error(result) ? js_interp_throw(result)
                    : js_interp_normal(result);
            }
        }
        JsInterpCompletion left = js_interp_eval(frame, (JsAstNode*)binary->left);
        if (left.kind != JS_INTERP_NORMAL) return left;
        if (binary->op == OPERATOR_AND && !js_is_truthy(left.value)) return left;
        if (binary->op == OPERATOR_OR && js_is_truthy(left.value)) return left;
        if (binary->op == OPERATOR_JS_NULLISH_COALESCE &&
                get_type_id(left.value) != LMD_TYPE_NULL &&
                get_type_id(left.value) != LMD_TYPE_UNDEFINED) return left;
        RootFrame roots(2);
        Rooted<Item> left_root(roots, left.value);
        JsInterpCompletion right = js_interp_eval(frame, (JsAstNode*)binary->right);
        if (right.kind != JS_INTERP_NORMAL) return right;
        Rooted<Item> right_root(roots, right.value);
        Item result = (binary->op == OPERATOR_AND || binary->op == OPERATOR_OR ||
                binary->op == OPERATOR_JS_NULLISH_COALESCE) ? right_root.get()
            : js_interp_binary(binary->op, left_root.get(), right_root.get());
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    case AST_NODE_ASSIGN: {
        JsAssignmentNode* assignment = (JsAssignmentNode*)node;
        if (assignment->left && (assignment->left->node_type == JS_AST_NODE_ARRAY_PATTERN ||
                assignment->left->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                assignment->left->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)) {
            RootFrame roots(1);
            Rooted<Item> value_root(roots, ItemNull);
            JsInterpCompletion right = js_interp_eval(frame,
                (JsAstNode*)assignment->right);
            if (right.kind != JS_INTERP_NORMAL) return right;
            value_root.set(right.value);
            JsInterpCompletion bound = js_interp_bind_pattern(frame,
                (JsAstNode*)assignment->left, value_root.get(), false);
            // Destructuring assignment evaluates to its RHS, rather than the
            // last target written while recursively binding the pattern.
            return bound.kind == JS_INTERP_NORMAL ? js_interp_normal(value_root.get()) : bound;
        }
        RootFrame roots(6);
        Rooted<Item> reference_object_root(roots, ItemNull);
        Rooted<Item> reference_key_root(roots, ItemNull);
        Rooted<Item> super_base_root(roots, ItemNull);
        JsInterpReference reference;
        JsInterpCompletion ref = js_interp_eval_reference(frame, (JsAstNode*)assignment->left,
            &reference, reference_object_root.home(), reference_key_root.home(),
            assignment->op == OPERATOR_ASSIGN,
            super_base_root.home());
        if (ref.kind != JS_INTERP_NORMAL) return ref;
        Rooted<Item> old_root(roots, ItemNull);
        if (assignment->op != OPERATOR_ASSIGN) {
            old_root.set(js_interp_reference_read(frame, &reference));
            if (item_is_error(old_root.get())) return js_interp_throw(old_root.get());
            if (js_interp_is_logical_assignment(assignment->op) &&
                    js_interp_logical_assignment_keeps_old(assignment->op, old_root.get())) {
                return js_interp_normal(old_root.get());
            }
        }
        Rooted<Item> right_root(roots, ItemNull);
        String* inferred_name = !assignment->lhs_is_parenthesized && assignment->left &&
                assignment->left->node_type == AST_NODE_IDENT
            ? ((JsIdentifierNode*)assignment->left)->name : NULL;
        JsInterpCompletion right = js_interp_eval_initializer_with_binding_name(frame,
            (JsAstNode*)assignment->right, inferred_name);
        if (right.kind != JS_INTERP_NORMAL) return right;
        right_root.set(right.value);
        Rooted<Item> result_root(roots, js_interp_is_logical_assignment(assignment->op)
            ? right_root.get() : js_interp_assignment_value(assignment->op,
                old_root.get(), right_root.get()));
        if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
        if (!assignment->lhs_is_parenthesized && (assignment->op == OPERATOR_ASSIGN ||
                js_interp_is_logical_assignment(assignment->op))) {
            js_interp_infer_binding_name((JsAstNode*)assignment->left,
                (JsAstNode*)assignment->right, result_root.get());
        }
        Item set = js_interp_reference_write(frame, &reference, result_root.get(), false);
        return item_is_error(set) ? js_interp_throw(set) : js_interp_normal(result_root.get());
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR:
        // The public evaluator consumes the chain marker; only a containing
        // member access needs to observe that optional chaining short-circuited.
        return js_interp_eval_member_chain(frame, (JsMemberNode*)node).completion;
    case AST_NODE_CALL_EXPR:
        return js_interp_eval_call(frame, (JsCallNode*)node, false);
    case AST_NODE_NEW_EXPR:
        return js_interp_eval_call(frame, (JsCallNode*)node, true);
    case JS_AST_NODE_TEMPLATE_LITERAL:
        return js_interp_eval_template(frame, (JsTemplateLiteralNode*)node);
    case JS_AST_NODE_TAGGED_TEMPLATE:
        return js_interp_eval_tagged_template(frame, (JsTaggedTemplateNode*)node);
    case JS_AST_NODE_REGEX: {
        JsRegexNode* regex = (JsRegexNode*)node;
        RootFrame roots(2);
        Rooted<Item> pattern_root(roots, js_make_string_len(
            regex->pattern ? regex->pattern : "", regex->pattern ? regex->pattern_len : 0));
        Rooted<Item> flags_root(roots, js_make_string_len(
            regex->flags ? regex->flags : "", regex->flags ? regex->flags_len : 0));
        Item result = js_create_regex_literal_items(pattern_root.get(), flags_root.get());
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    case JS_AST_NODE_AWAIT_EXPRESSION: {
        JsAwaitNode* awaited = (JsAwaitNode*)node;
        if (frame && frame->async_await_seen) {
            if (*frame->async_await_seen < frame->async_await_skip) {
                int64_t resume_index = *frame->async_await_seen;
                (*frame->async_await_seen)++;
                if (*frame->async_await_seen == frame->async_await_skip) {
                    Item resumed = frame->async_resume_input;
                    return item_is_error(resumed) ? js_interp_throw(resumed)
                        : js_interp_normal(resumed);
                }
                Item prior = get_type_id(frame->async_await_values) == LMD_TYPE_ARRAY
                    ? js_elements_get_int(frame->async_await_values, resume_index)
                    : make_js_undefined();
                return item_is_error(prior) ? js_interp_throw(prior)
                    : js_interp_normal(prior);
            }
            RootFrame roots(1);
            Rooted<Item> target_root(roots, make_js_undefined());
            if (awaited->argument) {
                JsInterpCompletion target = js_interp_eval(frame,
                    (JsAstNode*)awaited->argument);
                if (target.kind != JS_INTERP_NORMAL) return target;
                target_root.set(target.value);
            }
            target_root.set(js_promise_resolve(target_root.get()));
            if (item_is_error(target_root.get())) return js_interp_throw(target_root.get());
            (*frame->async_await_seen)++;
            return {JS_INTERP_AWAIT, target_root.get(), NULL, 0};
        }
        RootFrame roots(1);
        Rooted<Item> target_root(roots, make_js_undefined());
        if (awaited->argument) {
            JsInterpCompletion target = js_interp_eval(frame,
                (JsAstNode*)awaited->argument);
            if (target.kind != JS_INTERP_NORMAL) return target;
            target_root.set(target.value);
        }
        // The shared await helper owns Promise assimilation and drains the
        // current runtime turn for an already-settled result.  This keeps an
        // AST async function inside the same event loop, promise queue, and
        // EvalContext as Lambda and regular JS execution.
        Item result = js_await_sync_incremental(target_root.get());
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    case JS_AST_NODE_YIELD_EXPRESSION: {
        JsYieldNode* yielded = (JsYieldNode*)node;
        if (!frame || !frame->generator_yield_seen) {
            return js_interp_throw(js_throw_syntax_error(
                js_make_string("yield outside interpreted generator")));
        }
        bool replaying = !frame->generator_state ||
            !frame->generator_state->ast_resumable_loop_active;
        RootFrame roots(2);
        Rooted<Item> argument_root(roots, make_js_undefined());
        Rooted<Item> iterator_root(roots, make_js_undefined());
        bool argument_ready = false;
        if (replaying && *frame->generator_yield_seen <
                frame->generator_yield_skip && yielded->argument &&
                js_interp_yield_argument_can_suspend(
                    (JsAstNode*)yielded->argument)) {
            // A child yield can own the prior suspension. Evaluate through it
            // before deciding whether this parent yield is the replay target.
            JsInterpCompletion argument = js_interp_eval(frame,
                (JsAstNode*)yielded->argument);
            if (argument.kind != JS_INTERP_NORMAL) return argument;
            argument_root.set(argument.value);
            argument_ready = true;
        }
        if (replaying && *frame->generator_yield_seen < frame->generator_yield_skip) {
            // Replaying the durable activation deliberately skips the old
            // yield expression itself: its side effects already occurred at
            // the earlier suspension point.
            (*frame->generator_yield_seen)++;
            if (*frame->generator_yield_seen ==
                    frame->generator_abrupt_resume_yield) {
                Item abrupt = frame->generator_abrupt_resume_input;
                if (js_gen_is_throw_signal(abrupt)) {
                    return js_interp_throw(js_throw_value(js_gen_throw_signal_value(
                        abrupt)));
                }
                if (js_gen_is_return_signal(abrupt)) {
                    return {JS_INTERP_RETURN, js_gen_return_signal_value(abrupt),
                        NULL, 0};
                }
            }
            if (*frame->generator_yield_seen == frame->generator_yield_skip) {
                if (js_gen_is_throw_signal(frame->generator_resume_input)) {
                    return js_interp_throw(js_throw_value(js_gen_throw_signal_value(
                        frame->generator_resume_input)));
                }
                if (js_gen_is_return_signal(frame->generator_resume_input)) {
                    return {JS_INTERP_RETURN, js_gen_return_signal_value(
                        frame->generator_resume_input), NULL, 0};
                }
                return js_interp_normal(frame->generator_resume_input);
            }
            Item prior = get_type_id(frame->generator_yield_values) == LMD_TYPE_ARRAY
                ? js_elements_get_int(frame->generator_yield_values,
                    *frame->generator_yield_seen - 1)
                : make_js_undefined();
            return item_is_error(prior) ? js_interp_throw(prior)
                : js_interp_normal(prior);
        }
        if (yielded->delegate) {
            if (yielded->argument) {
                if (!argument_ready) {
                    JsInterpCompletion argument = js_interp_eval(frame,
                        (JsAstNode*)yielded->argument);
                    if (argument.kind != JS_INTERP_NORMAL) return argument;
                    argument_root.set(argument.value);
                }
                iterator_root.set(js_get_iterator(argument_root.get()));
                if (item_is_error(iterator_root.get())) {
                    return js_interp_throw(iterator_root.get());
                }
            } else {
                iterator_root.set(js_get_iterator(make_js_undefined()));
                if (item_is_error(iterator_root.get())) {
                    return js_interp_throw(iterator_root.get());
                }
            }
            // The shared generator runtime owns delegated next/throw/return
            // protocol. This completion only transfers its iterator there.
            return {JS_INTERP_YIELD, iterator_root.get(), NULL, 0,
                ItemNull, ItemNull, true};
        }
        if (yielded->argument) {
            if (!argument_ready) {
                JsInterpCompletion argument = js_interp_eval(frame,
                    (JsAstNode*)yielded->argument);
                if (argument.kind != JS_INTERP_NORMAL) return argument;
                argument_root.set(argument.value);
            }
        }
        (*frame->generator_yield_seen)++;
        return {JS_INTERP_YIELD, argument_root.get(), NULL, 0};
    }
    case AST_NODE_ARRAY: {
        JsArrayNode* array = (JsArrayNode*)node;
        RootFrame roots(3);
        Rooted<Item> result_root(roots, js_array_new(0));
        Rooted<Item> value_root(roots, ItemNull);
        Rooted<Item> spread_item_root(roots, ItemNull);
        if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
        for (JsAstNode* element = (JsAstNode*)array->elements; element;
                element = (JsAstNode*)element->next) {
            if (element->node_type == JS_AST_NODE_NULL) {
                // An elision is an absent property, not an own undefined value.
                Item pushed = js_array_push(result_root.get(), js_array_hole());
                if (item_is_error(pushed)) return js_interp_throw(pushed);
                continue;
            }
            if (element->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* spread = (JsSpreadElementNode*)element;
                JsInterpCompletion source = js_interp_eval(frame,
                    (JsAstNode*)spread->argument);
                if (source.kind != JS_INTERP_NORMAL) return source;
                value_root.set(source.value);
                value_root.set(js_iterable_to_array(value_root.get()));
                if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
                int64_t length = js_array_length(value_root.get());
                for (int64_t index = 0; index < length; index++) {
                    spread_item_root.set(js_elements_get_int(value_root.get(), index));
                    if (item_is_error(spread_item_root.get())) {
                        return js_interp_throw(spread_item_root.get());
                    }
                    Item pushed = js_array_push(result_root.get(), spread_item_root.get());
                    if (item_is_error(pushed)) return js_interp_throw(pushed);
                }
                continue;
            }
            JsInterpCompletion value = js_interp_eval(frame, element);
            if (value.kind != JS_INTERP_NORMAL) return value;
            value_root.set(value.value);
            Item pushed = js_array_push(result_root.get(), value_root.get());
            if (item_is_error(pushed)) return js_interp_throw(pushed);
        }
        return js_interp_normal(result_root.get());
    }
    case AST_NODE_MAP: {
        JsObjectNode* object = (JsObjectNode*)node;
        RootFrame roots(4);
        Rooted<Item> result_root(roots, js_new_object());
        Rooted<Item> key_root(roots, ItemNull);
        Rooted<Item> value_root(roots, ItemNull);
        Rooted<Item> raw_key_root(roots, ItemNull);
        if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
        for (JsAstNode* property = (JsAstNode*)object->properties; property;
                property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsInterpCompletion source = js_interp_eval(frame,
                    (JsAstNode*)((JsSpreadElementNode*)property)->argument);
                if (source.kind != JS_INTERP_NORMAL) return source;
                value_root.set(source.value);
                Item spread = js_object_spread_into(result_root.get(), value_root.get());
                if (item_is_error(spread)) return js_interp_throw(spread);
                continue;
            }
            if (property->node_type != AST_NODE_PROPERTY) {
                return js_interp_throw(js_throw_type_error("unsupported object property"));
            }
            JsPropertyNode* pair = (JsPropertyNode*)property;
            if (!pair->computed && pair->key && pair->key->node_type == AST_NODE_IDENT) {
                key_root.set(js_interp_name_key(((JsIdentifierNode*)pair->key)->name));
            } else {
                JsInterpCompletion key = js_interp_eval(frame, (JsAstNode*)pair->key);
                if (key.kind != JS_INTERP_NORMAL) return key;
                raw_key_root.set(key.value);
                key_root.set(js_interp_property_key_value(raw_key_root.get()));
            }
            if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
            if (pair->method || pair->is_getter || pair->is_setter) {
                value_root.set(js_interp_make_object_method(frame,
                    (JsFunctionNode*)pair->value));
                if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            } else {
                JsInterpCompletion value = js_interp_eval(frame, (JsAstNode*)pair->value);
                if (value.kind != JS_INTERP_NORMAL) return value;
                value_root.set(value.value);
            }
            bool is_proto_literal = !pair->computed && !pair->method &&
                !pair->is_getter && !pair->is_setter && !pair->shorthand &&
                pair->key && pair->value && pair->key != pair->value &&
                js_ast_is_proto_literal_key((JsAstNode*)pair->key);
            if (is_proto_literal) {
                js_object_proto_setter(result_root.get(), value_root.get());
                continue;
            }
            if (pair->method || pair->is_getter || pair->is_setter ||
                    js_interp_is_anonymous_function_definition(
                        (JsAstNode*)pair->value)) {
                int64_t prefix_kind = pair->is_getter ? 1 : (pair->is_setter ? 2 : 0);
                js_set_function_name_from_property_key_if_anonymous(value_root.get(),
                    key_root.get(), prefix_kind);
            }
            Item set = (pair->is_getter || pair->is_setter)
                ? js_define_accessor_partial(result_root.get(), key_root.get(), value_root.get(),
                    pair->is_setter ? 1 : 0, 0)
                : js_create_data_property(result_root.get(), key_root.get(), value_root.get());
            if (item_is_error(set)) return js_interp_throw(set);
            if (pair->method || pair->is_getter || pair->is_setter) {
                js_set_function_home_class(value_root.get(), result_root.get());
            }
        }
        return js_interp_normal(result_root.get());
    }
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_ARROW_FUNC: {
        Item function = js_interp_make_function(frame, (JsFunctionNode*)node);
        return item_is_error(function) ? js_interp_throw(function) : js_interp_normal(function);
    }
    case AST_NODE_CONDITIONAL_EXPR: {
        JsConditionalNode* conditional = (JsConditionalNode*)node;
        JsInterpCompletion test = js_interp_eval(frame, (JsAstNode*)conditional->test);
        if (test.kind != JS_INTERP_NORMAL) return test;
        return js_interp_eval(frame, (JsAstNode*)(js_is_truthy(test.value)
            ? conditional->consequent : conditional->alternate));
    }
    case AST_NODE_SEQ: {
        JsInterpCompletion result = js_interp_normal(make_js_undefined());
        for (JsAstNode* expression = (JsAstNode*)((JsSequenceNode*)node)->elements;
                expression; expression = (JsAstNode*)expression->next) {
            result = js_interp_eval(frame, expression);
            if (result.kind != JS_INTERP_NORMAL) return result;
        }
        return result;
    }
    default:
        return js_interp_throw(js_throw_type_error("unsupported interpreted expression"));
    }
}

static JsInterpCompletion js_interp_initialize_scope(JsInterpFrame* frame,
        NameScope* scope, bool initialize_functions) {
    if (!scope) return js_interp_normal(make_js_undefined());
    js_interp_scope_slot_count(scope);
    if (frame && !frame->script->is_module &&
            scope == frame->script->global_scope) {
        // GlobalDeclarationInstantiation validates every lexical name before
        // it creates any binding, so a later collision cannot leak an earlier
        // declaration into the realm.
        for (NameEntry* entry = scope->first; entry; entry = entry->next) {
            if (!entry->is_lexical || !entry->name) continue;
            Item status = js_evalscript_check_global_lex_decl(
                js_interp_name_key(entry->name));
            if (item_is_error(status)) return js_interp_throw(status);
        }
        // $262.evalScript additionally uses Script's global var/function
        // checks.  Validate the complete declaration set before slot or
        // property initialization so a rejected script remains atomic.
        if (frame->script->is_eval_script && js_262_eval_script_is_active()) {
            for (NameEntry* entry = scope->first; entry; entry = entry->next) {
                if (entry->is_lexical || !entry->name) continue;
                Item status = entry->node && entry->node->node_type == AST_NODE_FUNC
                    ? js_evalscript_check_global_function_decl(
                        js_interp_name_key(entry->name))
                    : js_evalscript_check_global_var_decl(
                        js_interp_name_key(entry->name));
                if (item_is_error(status)) return js_interp_throw(status);
            }
        }
    }
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        Item initial = entry->is_lexical ? (Item){.item = ITEM_JS_TDZ}
            : make_js_undefined();
        if (frame->script->is_eval_script && !entry->is_lexical &&
                entry->scope == frame->script->global_scope && !frame->script->is_module) {
            Item key = js_interp_name_key(entry->name);
            Item global = js_get_global_this();
            Item exists = js_has_own_property(global, key);
            if (item_is_error(exists)) return js_interp_throw(exists);
            if (js_is_truthy(exists)) {
                initial = js_get_key_default(global, key);
                if (item_is_error(initial)) return js_interp_throw(initial);
            }
        }
        Item stored = js_interp_write_binding(frame, entry, NULL, initial, true);
        if (item_is_error(stored)) return js_interp_throw(stored);
    }
    if (!initialize_functions) return js_interp_normal(make_js_undefined());
    return js_interp_initialize_function_declarations(frame, scope);
}

static JsInterpCompletion js_interp_initialize_function_declarations(
        JsInterpFrame* frame, NameScope* scope) {
    if (!scope) return js_interp_normal(make_js_undefined());
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        AstNode* node = entry->node;
        if (!node || (node->node_type != AST_NODE_FUNC &&
                node->node_type != AST_NODE_FUNC_EXPR &&
                node->node_type != AST_NODE_ARROW_FUNC)) continue;
        JsFunctionNode* function = (JsFunctionNode*)node;
        if (node->node_type == AST_NODE_FUNC_EXPR && function->vars == scope &&
                js_interp_name_matches(entry->name, function->name)) {
            // The private name of a function expression is its one closure,
            // not a fresh function created during each invocation.
            continue;
        }
        RootFrame roots(1);
        Rooted<Item> function_root(roots, js_interp_make_function(frame, function));
        if (item_is_error(function_root.get())) return js_interp_throw(function_root.get());
        Item stored = js_interp_write_binding(frame, entry, NULL, function_root.get(), true);
        if (item_is_error(stored)) return js_interp_throw(stored);
        if (entry->scope == frame->script->global_scope && !frame->script->is_module) {
            Item key = js_interp_name_key(entry->name);
            if (frame->script->is_eval_script) {
                if (js_262_eval_script_is_active()) {
                    // A Script function declaration creates a global function
                    // binding, which may tighten a configurable property's
                    // attributes to writable/enumerable/non-configurable.
                    js_define_global_function_property(key, function_root.get());
                } else {
                    Item global_written = js_set_global_property(key,
                        function_root.get(), frame->strict ? 1 : 0);
                    if (item_is_error(global_written)) return js_interp_throw(global_written);
                }
            } else {
                js_define_global_function_property(key, function_root.get());
            }
        }
    }
    return js_interp_normal(make_js_undefined());
}

static JsInterpCompletion js_interp_bind_named_function_expression_self(
        JsInterpFrame* frame, JsFunction* function, Item callable) {
    JsFunctionNode* ast = function ? function->ast_function : NULL;
    if (!frame || !ast || ast->node_type != AST_NODE_FUNC_EXPR || !ast->name ||
            !ast->vars) {
        return js_interp_normal(make_js_undefined());
    }
    for (NameEntry* entry = ast->vars->first; entry; entry = entry->next) {
        if (entry->node != (AstNode*)ast ||
                !js_interp_name_matches(entry->name, ast->name)) continue;
        Item stored = js_interp_write_binding(frame, entry, NULL, callable, true);
        return item_is_error(stored) ? js_interp_throw(stored)
            : js_interp_normal(make_js_undefined());
    }
    return js_interp_normal(make_js_undefined());
}

struct JsInterpAwaitCount {
    int64_t count;
};

static void js_interp_count_await_child(JsAstNode* child, void* opaque) {
    JsInterpAwaitCount* state = (JsInterpAwaitCount*)opaque;
    if (!child || !state) return;
    if (child->node_type == JS_AST_NODE_AWAIT_EXPRESSION) state->count++;
    js_ast_visit_children(child, js_interp_count_await_child, state);
}

static int64_t js_interp_count_awaits(JsAstNode* node) {
    JsInterpAwaitCount state = {0};
    js_interp_count_await_child(node, &state);
    return state.count;
}

static bool js_interp_yield_argument_can_suspend(JsAstNode* node) {
    if (!node) return false;
    if (node->node_type == JS_AST_NODE_YIELD_EXPRESSION) return true;
    if (node->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
            node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
            node->node_type == JS_AST_NODE_ARROW_FUNCTION) {
        // Creating a nested function does not execute its body.
        return false;
    }
    struct NestedYieldState {
        bool found;
    } state = {false};
    js_ast_visit_children(node, [](JsAstNode* child, void* opaque) {
        NestedYieldState* state = (NestedYieldState*)opaque;
        if (!state || state->found) return;
        state->found = js_interp_yield_argument_can_suspend(child);
    }, &state);
    return state.found;
}

static bool js_interp_terminal_yield_expr(JsAstNode* node) {
    if (!node) return false;
    if (node->node_type == JS_AST_NODE_YIELD_EXPRESSION) {
        JsYieldNode* yielded = (JsYieldNode*)node;
        // A nested yield suspends before this yield owns the completion, so
        // the generator must replay the expression to deliver next()'s input.
        return !js_interp_yield_argument_can_suspend(
            (JsAstNode*)yielded->argument);
    }
    if (node->node_type != AST_NODE_BINARY) return false;
    JsBinaryNode* binary = (JsBinaryNode*)node;
    return (binary->op == JS_OP_AND || binary->op == JS_OP_OR ||
            binary->op == JS_OP_NULLISH_COALESCE) &&
        js_interp_terminal_yield_expr((JsAstNode*)binary->right);
}

static bool js_interp_terminal_yield_statement(JsAstNode* node) {
    return node && node->node_type == AST_NODE_EXPR_STMT &&
        js_interp_terminal_yield_expr((JsAstNode*)((JsExpressionStatementNode*)node)->expression);
}

static void js_interp_generator_clear_list_continuation(
        JsGeneratorStateRecord* state) {
    if (!state || !state->ast_list_continuation) return;
    JsInterpGeneratorListContinuation* removed = state->ast_list_continuation;
    state->ast_list_continuation = removed->next;
    mem_free(removed);
}

static void js_interp_generator_clear_nested_list_continuations(
        JsGeneratorStateRecord* state) {
    if (!state || !state->ast_list_continuation) return;
    JsInterpGeneratorListContinuation* list =
        state->ast_list_continuation->next;
    state->ast_list_continuation->next = NULL;
    while (list) {
        JsInterpGeneratorListContinuation* next = list->next;
        mem_free(list);
        list = next;
    }
}

static JsInterpCompletion js_interp_exec_list(JsInterpFrame* frame, JsAstNode* node) {
    JsInterpCompletion result = js_interp_normal(make_js_undefined());
    JsAstNode* start = node;
    bool direct_resume = js_interp_generator_has_list_resume(frame, node);
    if (direct_resume) {
        JsInterpGeneratorListContinuation* continuation =
            frame->generator_state->ast_list_continuation;
        Item input = frame->generator_resume_input;
        start = continuation->next_statement;
        bool receives_resume_input = continuation->receives_resume_input;
        bool replay_abrupt = receives_resume_input &&
            continuation->replay_current_statement &&
            (js_gen_is_throw_signal(input) || js_gen_is_return_signal(input));
        if (frame->generator_yield_seen &&
                *frame->generator_yield_seen <
                    continuation->yield_count_before_next_statement) {
            *frame->generator_yield_seen =
                continuation->yield_count_before_next_statement;
        }
        js_interp_generator_clear_list_continuation(frame->generator_state);
        if (receives_resume_input && !replay_abrupt) {
            if (js_gen_is_throw_signal(input)) {
                return js_interp_throw(js_throw_value(js_gen_throw_signal_value(input)));
            }
            if (js_gen_is_return_signal(input)) {
                return {JS_INTERP_RETURN, js_gen_return_signal_value(input), NULL, 0};
            }
        }
    }
    for (JsAstNode* current = start; current; current = (JsAstNode*)current->next) {
        if (frame && node == frame->async_root_statement_list &&
                frame->async_skip_completed_statements &&
                *frame->async_skip_completed_statements) {
            if (current != frame->async_resume_statement) {
                if (frame->async_await_seen) {
                    *frame->async_await_seen += js_interp_count_awaits(current);
                }
                continue;
            }
            *frame->async_skip_completed_statements = false;
        }
        int64_t yield_count_before_current = frame && frame->generator_yield_seen
            ? *frame->generator_yield_seen : 0;
        result = js_interp_exec(frame, current);
        if (result.kind == JS_INTERP_AWAIT && frame &&
                node == frame->async_root_statement_list &&
                frame->async_suspended_statement) {
            *frame->async_suspended_statement = current;
        }
        if (result.kind == JS_INTERP_YIELD && frame && frame->generator_state &&
                !js_interp_generator_find_list(frame->generator_state, node)) {
            JsInterpGeneratorListContinuation* continuation =
                (JsInterpGeneratorListContinuation*)mem_calloc(1,
                    sizeof(*continuation), MEM_CAT_JS_RUNTIME);
            if (!continuation) return js_interp_throw(ItemError);
            continuation->statements = node;
            continuation->replay_current_statement =
                !js_interp_terminal_yield_statement(current);
            continuation->next_statement = continuation->replay_current_statement
                ? current : (JsAstNode*)current->next;
            continuation->env = frame->env;
            continuation->yield_count_before_next_statement =
                continuation->replay_current_statement ? yield_count_before_current
                : (frame->generator_yield_seen ? *frame->generator_yield_seen : 0);
            continuation->receives_resume_input =
                frame->generator_state->ast_list_continuation == NULL;
            continuation->next = frame->generator_state->ast_list_continuation;
            frame->generator_state->ast_list_continuation = continuation;
        }
        if (result.kind != JS_INTERP_NORMAL) return result;
    }
    return result;
}

static JsInterpCompletion js_interp_publish_annex_b_functions(JsInterpFrame* frame,
        NameScope* scope) {
    if (!frame || !scope || frame->strict) return js_interp_normal(make_js_undefined());
    for (NameEntry* lexical = scope->first; lexical; lexical = lexical->next) {
        AstNode* declaration = lexical->node;
        if (!lexical->is_lexical || !lexical->name || !declaration ||
                declaration->node_type != AST_NODE_FUNC) continue;
        NameEntry* outer = lexical->annex_b_outer_binding;
        if (!outer) continue;
        Item value = js_interp_binding_raw_value(frame, lexical);
        if (item_is_error(value)) return js_interp_throw(value);
        Item stored = js_interp_write_binding(frame, outer, NULL, value, false);
        if (item_is_error(stored)) return js_interp_throw(stored);
    }
    return js_interp_normal(make_js_undefined());
}

static JsInterpCompletion js_interp_exec_block(JsInterpFrame* frame, JsBlockNode* block) {
    if (!block) return js_interp_normal(make_js_undefined());
    if (!js_interp_scope_needs_environment(block->vars)) {
        return js_interp_exec_list(frame, (JsAstNode*)block->statements);
    }
    JsInterpEnv* env = js_interp_generator_list_resume_env(frame,
        (JsAstNode*)block->statements);
    bool resuming = env != NULL;
    if (!env) env = js_interp_env_create(block->vars, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame child = *frame;
    child.env = env;
    if (!resuming) {
        JsInterpCompletion initialized = js_interp_initialize_scope(&child, block->vars);
        if (initialized.kind != JS_INTERP_NORMAL) return initialized;
    }
    JsInterpCompletion completion = js_interp_exec_list(&child,
        (JsAstNode*)block->statements);
    if (completion.kind != JS_INTERP_NORMAL) return completion;
    return js_interp_publish_annex_b_functions(&child, block->vars);
}

static JsInterpCompletion js_interp_exec_scoped(JsInterpFrame* frame,
        NameScope* scope, JsAstNode* node) {
    if (!js_interp_scope_needs_environment(scope)) return js_interp_exec(frame, node);
    JsInterpEnv* env = js_interp_env_create(scope, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame child = *frame;
    child.env = env;
    JsInterpCompletion initialized = js_interp_initialize_scope(&child, scope);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized;
    JsInterpCompletion completion = js_interp_exec(&child, node);
    if (completion.kind != JS_INTERP_NORMAL) return completion;
    return js_interp_publish_annex_b_functions(&child, scope);
}

static JsInterpCompletion js_interp_exec_switch(JsInterpFrame* frame,
        JsSwitchNode* switched) {
    if (!switched) return js_interp_throw(ItemError);
    JsInterpEnv* env = js_interp_env_create(switched->vars, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame switch_frame = *frame;
    switch_frame.env = env;
    JsInterpCompletion initialized = js_interp_initialize_scope(&switch_frame,
        switched->vars);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized;

    RootFrame roots(1);
    Rooted<Item> discriminant_root(roots, ItemNull);
    JsInterpCompletion discriminant = js_interp_eval(&switch_frame,
        (JsAstNode*)switched->discriminant);
    if (discriminant.kind != JS_INTERP_NORMAL) return discriminant;
    discriminant_root.set(discriminant.value);

    JsSwitchCaseNode* selected = NULL;
    JsSwitchCaseNode* default_case = NULL;
    for (JsSwitchCaseNode* candidate = (JsSwitchCaseNode*)switched->cases;
            candidate; candidate = (JsSwitchCaseNode*)candidate->next) {
        if (!candidate->test) {
            default_case = candidate;
            continue;
        }
        JsInterpCompletion test = js_interp_eval(&switch_frame,
            (JsAstNode*)candidate->test);
        if (test.kind != JS_INTERP_NORMAL) return test;
        if (js_strict_equal(discriminant_root.get(), test.value).item == b2it(true)) {
            selected = candidate;
            break;
        }
    }
    if (!selected) selected = default_case;
    if (!selected) return js_interp_normal(make_js_undefined());

    for (JsSwitchCaseNode* current = selected; current;
            current = (JsSwitchCaseNode*)current->next) {
        JsInterpCompletion completion = js_interp_exec_list(&switch_frame,
            (JsAstNode*)current->consequent);
        if (completion.kind == JS_INTERP_BREAK &&
                js_interp_completion_targets_active_label(&completion, &switch_frame)) {
            return js_interp_publish_annex_b_functions(&switch_frame, switched->vars);
        }
        if (completion.kind != JS_INTERP_NORMAL) return completion;
    }
    return js_interp_publish_annex_b_functions(&switch_frame, switched->vars);
}

static JsInterpCompletion js_interp_assign_iteration_head(JsInterpFrame* frame,
        JsAstNode* left, Item value, bool initialize) {
    if (!left) return js_interp_throw(ItemError);
    if (left->node_type == AST_NODE_VAR_STAM) {
        JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)left;
        JsVariableDeclaratorNode* declarator = declaration->declarations &&
                declaration->declarations->node_type == AST_NODE_VARIABLE_DECLARATOR
            ? (JsVariableDeclaratorNode*)declaration->declarations : NULL;
        if (!declarator || declarator->next || !declarator->id) {
            return js_interp_throw(js_throw_type_error("unsupported iteration declaration"));
        }
        return js_interp_bind_pattern(frame, (JsAstNode*)declarator->id,
            value, initialize);
    }
    if (left->node_type == JS_AST_NODE_ARRAY_PATTERN ||
            left->node_type == JS_AST_NODE_OBJECT_PATTERN ||
            left->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        return js_interp_bind_pattern(frame, left, value, initialize);
    }
    RootFrame roots(2);
    Rooted<Item> object_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    JsInterpReference reference;
    JsInterpCompletion resolved = js_interp_eval_reference(frame, left, &reference,
        object_root.home(), key_root.home());
    if (resolved.kind != JS_INTERP_NORMAL) return resolved;
    Item stored = js_interp_reference_write(frame, &reference, value, initialize);
    return item_is_error(stored) ? js_interp_throw(stored) : js_interp_normal(stored);
}

static JsInterpCompletion js_interp_close_iterator_after_completion(Item iterator,
        const JsInterpFrame* frame, JsInterpCompletion completion) {
    bool matching_continue = completion.kind == JS_INTERP_CONTINUE &&
        js_interp_completion_targets_active_label(&completion, frame);
    if (completion.kind == JS_INTERP_NORMAL || matching_continue) {
        return completion;
    }
    RootFrame roots(1);
    Rooted<Item> completion_root(roots, completion.value);
    Item closed = js_iterator_close(iterator);
    // IteratorClose preserves a throw completion even when its return lookup,
    // call, or result validation fails.
    if (completion.kind == JS_INTERP_THROW) {
        completion.value = completion_root.get();
        return completion;
    }
    if (item_is_error(closed)) return js_interp_throw(closed);
    if (completion.kind == JS_INTERP_BREAK &&
            js_interp_completion_targets_active_label(&completion, frame)) {
        return js_interp_normal(make_js_undefined());
    }
    completion.value = completion_root.get();
    return completion;
}

static JsInterpCompletion js_interp_exec_for_of(JsInterpFrame* frame,
        JsForOfNode* loop, bool is_for_in) {
    if (!loop) return js_interp_throw(ItemError);
    if (loop->is_await) return js_interp_throw(js_throw_syntax_error(
        js_make_string("unsupported asynchronous iteration")));

    RootFrame roots(4);
    Rooted<Item> source_root(roots, ItemNull);
    Rooted<Item> iterator_root(roots, ItemNull);
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> for_in_object_root(roots, ItemNull);
    JsInterpGeneratorLoopContinuation* resume = js_interp_generator_find_loop(frame,
        (JsAstNode*)loop);
    JsInterpEnv* env = resume ? resume->env : NULL;
    if (!resume) {
        if (loop->init) {
            JsInterpCompletion initial = js_interp_eval(frame, (JsAstNode*)loop->init);
            if (initial.kind != JS_INTERP_NORMAL) return initial;
            value_root.set(initial.value);
            // Sloppy for-in permits `var name = value`. Its initializer writes
            // before the RHS is evaluated, including when the object is empty.
            if (is_for_in && loop->declares_binding) {
                JsInterpCompletion bound = js_interp_assign_iteration_head(frame,
                    (JsAstNode*)loop->left, value_root.get(), false);
                if (bound.kind != JS_INTERP_NORMAL) return bound;
            }
        }
        // The loop declaration creates a TDZ before its RHS is evaluated.
        // This also supplies the outer record cloned for each lexical iteration.
        env = js_interp_env_create(loop->vars, frame ? frame->env : NULL);
    }
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame header_frame = *frame;
    header_frame.env = env;
    if (!resume) {
        JsInterpCompletion initialized = js_interp_initialize_scope(&header_frame, loop->vars);
        if (initialized.kind != JS_INTERP_NORMAL) return initialized;
        JsInterpCompletion source = js_interp_eval(&header_frame, (JsAstNode*)loop->right);
        if (source.kind != JS_INTERP_NORMAL) return source;
        source_root.set(source.value);
        if (is_for_in) {
            for_in_object_root.set(source_root.get());
            source_root.set(js_for_in_keys(source_root.get()));
            if (item_is_error(source_root.get())) return js_interp_throw(source_root.get());
        }
        iterator_root.set(js_get_iterator(source_root.get()));
        if (item_is_error(iterator_root.get())) return js_interp_throw(iterator_root.get());
    } else {
        iterator_root.set(resume->iterator);
        for_in_object_root.set(resume->for_in_object);
    }
    bool has_per_iteration_lexical = js_interp_loop_needs_per_iteration_env(
        loop->vars, (JsAstNode*)loop->init, (JsAstNode*)loop->right,
        (JsAstNode*)loop->left,
        (JsAstNode*)loop->body);

    bool resume_body = resume != NULL;
    for (;;) {
        JsInterpEnv* iteration_env = env_root.env;
        bool create_iteration_env = false;
        if (!resume_body) {
            value_root.set(js_iterator_step(iterator_root.get()));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            if (value_root.get().item == JS_ITER_DONE_SENTINEL) {
                js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
                return js_interp_normal(make_js_undefined());
            }
            if (is_for_in && !js_for_in_key_is_live(for_in_object_root.get(),
                    value_root.get())) {
                // Enumerate a snapshot, but re-check each candidate before it
                // is visited so a prior iteration's delete suppresses it.
                continue;
            }
            iteration_env = has_per_iteration_lexical
                ? js_interp_env_clone(env_root.env) : env_root.env;
            create_iteration_env = has_per_iteration_lexical;
        }
        JsInterpEnvRoot iteration_root(create_iteration_env ? iteration_env : NULL);
        if (create_iteration_env && (!iteration_env || !iteration_root.registered)) {
            return js_interp_throw(ItemError);
        }
        JsInterpFrame iteration_frame = *frame;
        iteration_frame.env = iteration_env;
        if (!resume_body) {
            JsInterpCompletion assigned = js_interp_assign_iteration_head(&iteration_frame,
                (JsAstNode*)loop->left, value_root.get(), loop->declares_binding);
            if (assigned.kind != JS_INTERP_NORMAL) {
                js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
                return js_interp_close_iterator_after_completion(iterator_root.get(),
                    &iteration_frame, assigned);
            }
        }
        JsInterpFrame body_frame = iteration_frame;
        body_frame.active_label = NULL;
        body_frame.active_label_len = 0;
        JsInterpCompletion completion = js_interp_exec(&body_frame,
            (JsAstNode*)loop->body);
        if (has_per_iteration_lexical && !resume_body) env_root.replace_with(&iteration_root);
        if (completion.kind == JS_INTERP_YIELD || completion.kind == JS_INTERP_AWAIT) {
            if (completion.kind == JS_INTERP_YIELD && frame && frame->generator_state &&
                    frame->generator_state->ast_list_continuation &&
                        !js_interp_generator_suspend_loop(frame, (JsAstNode*)loop,
                        env_root.env, iterator_root.get(),
                        for_in_object_root.get(), true)) {
                return js_interp_throw(ItemError);
            }
            return completion;
        }
        if (completion.kind == JS_INTERP_NORMAL ||
                (completion.kind == JS_INTERP_CONTINUE &&
                 js_interp_completion_targets_active_label(&completion, &iteration_frame))) {
            js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
            resume_body = false;
            continue;
        }
        js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
        return js_interp_close_iterator_after_completion(iterator_root.get(),
            &iteration_frame, completion);
    }
}

static JsInterpCompletion js_interp_exec_declaration(JsInterpFrame* frame,
        JsVariableDeclarationNode* declaration) {
    for (JsAstNode* item = (JsAstNode*)declaration->declarations; item;
            item = (JsAstNode*)item->next) {
        if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) {
            return js_interp_throw(js_throw_type_error("unsupported declaration pattern"));
        }
        JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)item;
        if (!declarator->id) {
            return js_interp_throw(js_throw_type_error("unsupported declaration pattern"));
        }
        RootFrame roots(3);
        Rooted<Item> value_root(roots, make_js_undefined());
        Rooted<Item> reference_object_root(roots, ItemNull);
        Rooted<Item> reference_key_root(roots, ItemNull);
        JsInterpReference pre_reference = {};
        const JsInterpReference* resolved_reference = NULL;
        // `var x;` is declaration instantiation only: it must not overwrite a
        // prior global-property value. Var initializers are ordinary writes,
        // whereas lexical declarations initialize their TDZ cell here.
        if (declaration->kind == JS_VAR_VAR && !declarator->init) continue;
        if (declaration->kind == JS_VAR_VAR && declarator->init &&
                declarator->id->node_type == AST_NODE_IDENT) {
            JsInterpCompletion resolved = js_interp_eval_reference(frame,
                (JsAstNode*)declarator->id, &pre_reference,
                reference_object_root.home(), reference_key_root.home());
            if (resolved.kind != JS_INTERP_NORMAL) return resolved;
            resolved_reference = &pre_reference;
        }
        if (declarator->init) {
            String* inferred_name = declarator->id->node_type == AST_NODE_IDENT
                ? ((JsIdentifierNode*)declarator->id)->name : NULL;
            JsInterpCompletion initialized = js_interp_eval_initializer_with_binding_name(frame,
                (JsAstNode*)declarator->init, inferred_name);
            if (initialized.kind != JS_INTERP_NORMAL) return initialized;
            value_root.set(initialized.value);
            js_interp_infer_binding_name((JsAstNode*)declarator->id,
                (JsAstNode*)declarator->init, value_root.get());
        }
        JsInterpCompletion bound = js_interp_bind_pattern(frame,
            (JsAstNode*)declarator->id, value_root.get(),
            declaration->kind != JS_VAR_VAR, resolved_reference);
        if (bound.kind != JS_INTERP_NORMAL) return bound;
    }
    return js_interp_normal(make_js_undefined());
}

static JsInterpCompletion js_interp_exec_export(JsInterpFrame* frame,
        JsExportNode* exported) {
    if (!frame || !exported || !frame->script || !frame->script->is_es_module) {
        return js_interp_throw(js_throw_syntax_error(
            js_make_string("export outside module")));
    }
    RootFrame roots(5);
    Rooted<Item> namespace_root(roots, js_get_active_module_namespace());
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> source_namespace_root(roots, ItemNull);
    Rooted<Item> keys_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    if (exported->source) {
        char resolved[512];
        jm_resolve_module_path(frame->script->reference, exported->source->chars,
            (int)exported->source->len, resolved, (int)sizeof(resolved));
        source_namespace_root.set(js_module_get(js_make_string(resolved)));
        if (get_type_id(source_namespace_root.get()) == LMD_TYPE_NULL) {
            return js_interp_throw(js_throw_reference_error(
                js_make_string("re-exported module is unavailable")));
        }
    }
    JsInterpCompletion completion = js_interp_normal(make_js_undefined());
    if (exported->declaration) {
        completion = js_interp_exec(frame, exported->declaration);
        if (completion.kind != JS_INTERP_NORMAL) return completion;
        if (!exported->is_default &&
                (exported->declaration->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
                 exported->declaration->node_type == JS_AST_NODE_CLASS_DECLARATION ||
                 exported->declaration->node_type == JS_AST_NODE_CLASS_EXPRESSION)) {
            String* name = exported->declaration->node_type ==
                    JS_AST_NODE_FUNCTION_DECLARATION
                ? ((JsFunctionNode*)exported->declaration)->name
                : ((JsClassNode*)exported->declaration)->name;
            NameEntry* entry = js_interp_find_binding(frame, name);
            value_root.set(js_interp_read_binding(frame, entry, name));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            Item stored = js_interp_publish_export_value(frame, namespace_root.get(), name,
                value_root.get());
            if (item_is_error(stored)) return js_interp_throw(stored);
        } else if (!exported->is_default &&
                exported->declaration->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* declaration =
                (JsVariableDeclarationNode*)exported->declaration;
            for (JsAstNode* declarator = (JsAstNode*)declaration->declarations;
                    declarator; declarator = (JsAstNode*)declarator->next) {
                JsVariableDeclaratorNode* variable =
                    (JsVariableDeclaratorNode*)declarator;
                if (!variable->id || variable->id->node_type != AST_NODE_IDENT) continue;
                String* name = ((JsIdentifierNode*)variable->id)->name;
                NameEntry* entry = js_interp_find_binding(frame, name);
                value_root.set(js_interp_read_binding(frame, entry, name));
                if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
                Item stored = js_interp_publish_export_value(frame, namespace_root.get(), name,
                    value_root.get());
                if (item_is_error(stored)) return js_interp_throw(stored);
            }
        }
    }
    if (exported->is_star && !exported->is_namespace) {
        keys_root.set(js_object_keys(source_namespace_root.get()));
        if (item_is_error(keys_root.get())) return js_interp_throw(keys_root.get());
        int64_t count = js_array_length(keys_root.get());
        for (int64_t index = 0; index < count; index++) {
            key_root.set(js_elements_get_int(keys_root.get(), index));
            if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
            if (get_type_id(key_root.get()) != LMD_TYPE_STRING) continue;
            String* name = it2s(key_root.get());
            // Export-star intentionally omits the source module's default.
            if (js_interp_name_equals(name, "default") ||
                    js_interp_has_explicit_export(frame->script, name)) continue;
            if (!js_interp_add_reexport_binding(frame->script, name, name,
                    exported->source)) {
                return js_interp_throw(ItemError);
            }
            value_root.set(js_get_key_default(source_namespace_root.get(), key_root.get()));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            Item stored = js_interp_publish_export_value(frame, namespace_root.get(), name,
                value_root.get());
            if (item_is_error(stored)) return js_interp_throw(stored);
        }
    }
    for (JsAstNode* spec = exported->specifiers; spec;
            spec = (JsAstNode*)spec->next) {
        JsExportSpecifierNode* item = (JsExportSpecifierNode*)spec;
        if (exported->is_namespace) {
            value_root.set(source_namespace_root.get());
        } else if (exported->source) {
            value_root.set(js_get_key_default(source_namespace_root.get(),
                js_interp_name_key(item->local_name)));
        } else {
            NameEntry* entry = js_interp_find_binding(frame, item->local_name);
            value_root.set(js_interp_read_binding(frame, entry, item->local_name));
        }
        if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
        Item stored = js_interp_publish_export_value(frame, namespace_root.get(),
            item->export_name, value_root.get());
        if (item_is_error(stored)) return js_interp_throw(stored);
    }
    if (!exported->is_default) return completion;
    if (exported->declaration &&
            (exported->declaration->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
             exported->declaration->node_type == JS_AST_NODE_CLASS_DECLARATION ||
             exported->declaration->node_type == JS_AST_NODE_CLASS_EXPRESSION)) {
        String* name = exported->declaration->node_type ==
                JS_AST_NODE_FUNCTION_DECLARATION
            ? ((JsFunctionNode*)exported->declaration)->name
            : ((JsClassNode*)exported->declaration)->name;
        if (name) {
            NameEntry* entry = js_interp_find_binding(frame, name);
            value_root.set(js_interp_read_binding(frame, entry, name));
        } else {
            // Anonymous default function declarations are not local module
            // bindings. Materialize their one exported function identity here.
            value_root.set(js_interp_make_function(frame,
                (JsFunctionNode*)exported->declaration));
            if (!item_is_error(value_root.get())) {
                js_set_function_name(value_root.get(), js_make_string("default"));
            }
        }
    } else {
        value_root.set(completion.value);
    }
    if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
    Item stored = js_interp_publish_export_value(frame, namespace_root.get(),
        name_pool_create_len(frame->script->name_pool, "default", 7), value_root.get());
    return item_is_error(stored) ? js_interp_throw(stored) : completion;
}

static JsInterpCompletion js_interp_exec_catch(JsInterpFrame* frame,
        JsCatchNode* handler, Item thrown) {
    if (!handler) return js_interp_throw(thrown);
    RootFrame roots(2);
    Rooted<Item> thrown_root(roots, thrown);
    JsInterpEnv* env = js_interp_env_create(handler->vars, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame catch_frame = *frame;
    catch_frame.env = env;
    JsInterpCompletion initialized = js_interp_initialize_scope(&catch_frame, handler->vars);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized;
    if (handler->param) {
        Rooted<Item> payload_root(roots, js_error_lane_payload(thrown_root.get()));
        JsInterpCompletion bound = js_interp_bind_pattern(&catch_frame,
            (JsAstNode*)handler->param, payload_root.get(), true);
        if (bound.kind != JS_INTERP_NORMAL) return bound;
    }
    return handler->body ? js_interp_exec(&catch_frame, (JsAstNode*)handler->body)
        : js_interp_normal(make_js_undefined());
}

static bool js_interp_prepare_self_tail_call(JsInterpFrame* frame,
        JsCallNode* call, JsInterpCompletion* completion) {
    if (!frame || !frame->active_function || !call || !completion ||
            !call->function || call->function->node_type != AST_NODE_IDENT) {
        return false;
    }
    if (js_interp_identifier_is((JsAstNode*)call->function, "super")) {
        // SuperCall is a syntactically identifier-shaped call, but it is an
        // activation capability rather than a lexical function binding.
        // Evaluating it for self-tail-call detection would wrongly throw.
        return false;
    }
    JsInterpCompletion callee = js_interp_eval(frame, (JsAstNode*)call->function);
    if (callee.kind != JS_INTERP_NORMAL) {
        *completion = callee;
        return true;
    }
    if (get_type_id(callee.value) != LMD_TYPE_FUNC ||
            callee.value.function != (Function*)frame->active_function) {
        return false;
    }
    RootFrame roots(3);
    uint64_t* scratch_home = NULL;
    if (frame->tail_scratch) {
        int index = frame->tail_scratch->next_index;
        if (index >= 0 && index < 2) scratch_home = frame->tail_scratch->homes[index];
    }
    Item scratch = scratch_home ? (Item){.item = *scratch_home} : ItemNull;
    Rooted<Item> arguments_root(roots, get_type_id(scratch) == LMD_TYPE_ARRAY
        ? scratch : js_array_new(0));
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> spread_item_root(roots, ItemNull);
    if (item_is_error(arguments_root.get())) {
        *completion = js_interp_throw(arguments_root.get());
        return true;
    }
    // This array is private to the activation. Wide scalar elements own tail
    // storage in List::extra, so clear both logical portions before reuse;
    // retaining only length made every full-width numeric tail argument grow
    // the backing allocation indefinitely.
    arguments_root.get().array->length = 0;
    arguments_root.get().array->extra = 0;
    if (scratch_home) {
        *scratch_home = arguments_root.get().item;
        frame->tail_scratch->next_index = (frame->tail_scratch->next_index + 1) % 2;
    }
    for (JsAstNode* arg = (JsAstNode*)call->arguments; arg;
            arg = (JsAstNode*)arg->next) {
        if (arg->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            JsInterpCompletion source = js_interp_eval(frame,
                (JsAstNode*)((JsSpreadElementNode*)arg)->argument);
            if (source.kind != JS_INTERP_NORMAL) {
                *completion = source;
                return true;
            }
            value_root.set(js_iterable_to_array(source.value));
            if (item_is_error(value_root.get())) {
                *completion = js_interp_throw(value_root.get());
                return true;
            }
            int64_t length = js_array_length(value_root.get());
            for (int64_t index = 0; index < length; index++) {
                spread_item_root.set(js_elements_get_int(value_root.get(), index));
                if (item_is_error(spread_item_root.get())) {
                    *completion = js_interp_throw(spread_item_root.get());
                    return true;
                }
                Item pushed = js_array_push(arguments_root.get(), spread_item_root.get());
                if (item_is_error(pushed)) {
                    *completion = js_interp_throw(pushed);
                    return true;
                }
            }
            continue;
        }
        JsInterpCompletion value = js_interp_eval(frame, arg);
        if (value.kind != JS_INTERP_NORMAL) {
            *completion = value;
            return true;
        }
        value_root.set(value.value);
        Item pushed = js_array_push(arguments_root.get(), value_root.get());
        if (item_is_error(pushed)) {
            *completion = js_interp_throw(pushed);
            return true;
        }
    }
    *completion = {JS_INTERP_TAIL_CALL, make_js_undefined(), NULL, 0,
        arguments_root.get(), (frame->active_function->flags & JS_FUNC_FLAG_ARROW)
            ? js_interp_frame_this(frame)
            : (frame->strict ? make_js_undefined() : js_get_global_this())};
    return true;
}

static JsInterpCompletion js_interp_exec(JsInterpFrame* frame, JsAstNode* node) {
    if (!node) return js_interp_normal(make_js_undefined());
    switch (node->node_type) {
    case AST_SCRIPT:
        return js_interp_exec_list(frame, (JsAstNode*)((JsProgramNode*)node)->body);
    case AST_NODE_BLOCK:
        return js_interp_exec_block(frame, (JsBlockNode*)node);
    case AST_NODE_EXPR_STMT:
        return js_interp_eval(frame, (JsAstNode*)((JsExpressionStatementNode*)node)->expression);
    case AST_NODE_VAR_STAM:
        return js_interp_exec_declaration(frame, (JsVariableDeclarationNode*)node);
    case AST_NODE_FUNC:
        // Declaration instantiation created the function before any statement
        // executed. Re-creating it here would change its observable identity.
        return js_interp_normal(make_js_undefined());
    case JS_AST_NODE_IMPORT_DECLARATION:
        // Module linking eagerly loads dependencies before body execution.
        // Reads stay live through the retained namespace binding plan.
        return js_interp_normal(make_js_undefined());
    case JS_AST_NODE_EXPORT_DECLARATION:
        return js_interp_exec_export(frame, (JsExportNode*)node);
    case JS_AST_NODE_CLASS_DECLARATION:
        return js_interp_eval_class(frame, (JsClassNode*)node, true);
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* with = (JsWithStatementNode*)node;
        RootFrame roots(1);
        Rooted<Item> object_root(roots, ItemNull);
        JsInterpCompletion object = js_interp_eval(frame, with->object);
        if (object.kind != JS_INTERP_NORMAL) return object;
        object_root.set(object.value);
        Item pushed = js_with_push(object_root.get());
        if (item_is_error(pushed)) return js_interp_throw(pushed);
        JsInterpCompletion completion = with->body
            ? js_interp_exec(frame, with->body) : js_interp_normal(make_js_undefined());
        js_with_pop();
        return completion;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* labeled = (JsLabeledStatementNode*)node;
        JsInterpFrame labeled_frame = *frame;
        labeled_frame.active_label = labeled->label;
        labeled_frame.active_label_len = labeled->label_len;
        bool directly_labels_iteration = labeled->body &&
            (labeled->body->node_type == AST_NODE_WHILE_STAM ||
             labeled->body->node_type == AST_NODE_DO_WHILE_STAM ||
             labeled->body->node_type == AST_NODE_FOR_STAM ||
             labeled->body->node_type == JS_AST_NODE_FOR_OF_STATEMENT ||
             labeled->body->node_type == JS_AST_NODE_FOR_IN_STATEMENT);
        JsInterpFrame body_frame = labeled_frame;
        if (!directly_labels_iteration) {
            // A label on a block must receive its break completion itself;
            // nested loops may only consume labels that directly annotate them.
            body_frame.active_label = NULL;
            body_frame.active_label_len = 0;
        }
        JsInterpCompletion completion = js_interp_exec(&body_frame, labeled->body);
        if (completion.kind == JS_INTERP_BREAK &&
                js_interp_completion_targets_active_label(&completion, &labeled_frame)) {
            return js_interp_normal(make_js_undefined());
        }
        return completion;
    }
    case AST_NODE_IF_EXPR: {
        JsIfNode* conditional = (JsIfNode*)node;
        JsInterpCompletion test = js_interp_eval(frame, (JsAstNode*)conditional->test);
        if (test.kind != JS_INTERP_NORMAL) return test;
        bool take_consequent = js_is_truthy(test.value);
        return js_interp_exec_scoped(frame, take_consequent
            ? conditional->consequent_vars : conditional->alternate_vars,
            (JsAstNode*)(take_consequent ? conditional->consequent
                : conditional->alternate));
    }
    case AST_NODE_WHILE_STAM: {
        JsWhileNode* loop = (JsWhileNode*)node;
        for (;;) {
            JsInterpCompletion test = js_interp_eval(frame, (JsAstNode*)loop->test);
            if (test.kind != JS_INTERP_NORMAL) return test;
            if (!js_is_truthy(test.value)) return js_interp_normal(make_js_undefined());
            JsInterpFrame body_frame = *frame;
            body_frame.active_label = NULL;
            body_frame.active_label_len = 0;
            JsInterpCompletion body = js_interp_exec(&body_frame, (JsAstNode*)loop->body);
            if (body.kind == JS_INTERP_BREAK) {
                if (js_interp_completion_targets_active_label(&body, frame)) {
                    return js_interp_normal(make_js_undefined());
                }
                return body;
            }
            if (body.kind == JS_INTERP_RETURN || body.kind == JS_INTERP_THROW) return body;
            if (body.kind == JS_INTERP_CONTINUE &&
                    !js_interp_completion_targets_active_label(&body, frame)) return body;
            if (body.kind == JS_INTERP_YIELD || body.kind == JS_INTERP_AWAIT) return body;
        }
    }
    case AST_NODE_DO_WHILE_STAM: {
        JsDoWhileNode* loop = (JsDoWhileNode*)node;
        for (;;) {
            JsInterpFrame body_frame = *frame;
            body_frame.active_label = NULL;
            body_frame.active_label_len = 0;
            JsInterpCompletion body = js_interp_exec(&body_frame, (JsAstNode*)loop->body);
            if (body.kind == JS_INTERP_BREAK) {
                if (js_interp_completion_targets_active_label(&body, frame)) {
                    return js_interp_normal(make_js_undefined());
                }
                return body;
            }
            if (body.kind == JS_INTERP_RETURN || body.kind == JS_INTERP_THROW) return body;
            if (body.kind == JS_INTERP_CONTINUE &&
                    !js_interp_completion_targets_active_label(&body, frame)) return body;
            if (body.kind == JS_INTERP_YIELD || body.kind == JS_INTERP_AWAIT) return body;
            JsInterpCompletion test = js_interp_eval(frame, (JsAstNode*)loop->test);
            if (test.kind != JS_INTERP_NORMAL) return test;
            if (!js_is_truthy(test.value)) return js_interp_normal(make_js_undefined());
        }
    }
    case AST_NODE_FOR_STAM: {
        JsForNode* loop = (JsForNode*)node;
        JsInterpGeneratorLoopContinuation* resume = js_interp_generator_find_loop(frame,
            (JsAstNode*)loop);
        JsInterpEnv* env = resume ? resume->env
            : js_interp_env_create(loop->vars, frame ? frame->env : NULL);
        JsInterpEnvRoot env_root(env);
        if (!env || !env_root.registered) return js_interp_throw(ItemError);
        if (!resume) {
            JsInterpFrame header_frame = *frame;
            header_frame.env = env;
            JsInterpCompletion initialized = js_interp_initialize_scope(&header_frame, loop->vars);
            if (initialized.kind != JS_INTERP_NORMAL) return initialized;
            if (loop->init) {
                JsInterpCompletion init = loop->init->node_type == AST_NODE_VAR_STAM
                    ? js_interp_exec(&header_frame, (JsAstNode*)loop->init)
                    : js_interp_eval(&header_frame, (JsAstNode*)loop->init);
                if (init.kind != JS_INTERP_NORMAL) return init;
            }
        }
        bool has_per_iteration_lexical = js_interp_loop_needs_per_iteration_env(
            loop->vars, (JsAstNode*)loop->init, (JsAstNode*)loop->test,
            (JsAstNode*)loop->update,
            (JsAstNode*)loop->body);
        if (!resume && has_per_iteration_lexical) {
            // The initializer retains the header environment. Create the first
            // iteration record before its test can mutate a captured `let`.
            JsInterpEnv* first_iteration_env = js_interp_env_clone(env_root.env);
            JsInterpEnvRoot first_iteration_root(first_iteration_env);
            if (!first_iteration_env || !first_iteration_root.registered) {
                return js_interp_throw(ItemError);
            }
            env_root.replace_with(&first_iteration_root);
        }
        bool resume_body = resume != NULL;
        for (;;) {
            JsInterpFrame loop_frame = *frame;
            loop_frame.env = env_root.env;
            if (!resume_body && loop->test) {
                JsInterpCompletion test = js_interp_eval(&loop_frame, (JsAstNode*)loop->test);
                if (test.kind != JS_INTERP_NORMAL) return test;
                if (!js_is_truthy(test.value)) {
                    js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
                    return js_interp_normal(make_js_undefined());
                }
            }
            JsInterpFrame body_frame = loop_frame;
            body_frame.active_label = NULL;
            body_frame.active_label_len = 0;
            JsInterpCompletion body = js_interp_exec(&body_frame, (JsAstNode*)loop->body);
            if (body.kind == JS_INTERP_BREAK) {
                if (js_interp_completion_targets_active_label(&body, &loop_frame)) {
                    js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
                    return js_interp_normal(make_js_undefined());
                }
                js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
                return body;
            }
            if (body.kind == JS_INTERP_RETURN || body.kind == JS_INTERP_THROW) {
                js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
                return body;
            }
            if (body.kind == JS_INTERP_CONTINUE &&
                    !js_interp_completion_targets_active_label(&body, &loop_frame)) {
                js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
                return body;
            }
            if (body.kind == JS_INTERP_YIELD || body.kind == JS_INTERP_AWAIT) {
                if (body.kind == JS_INTERP_YIELD && frame && frame->generator_state &&
                        frame->generator_state->ast_list_continuation &&
                        !js_interp_generator_suspend_loop(frame, (JsAstNode*)loop,
                            env_root.env, ItemNull, ItemNull, false)) {
                    return js_interp_throw(ItemError);
                }
                return body;
            }
            js_interp_generator_remove_loop(frame, (JsAstNode*)loop);
            // ECMAScript gives each `for (let/const ...)` iteration a fresh
            // binding environment. Clone before the update so body closures
            // retain the value they observed, while the update feeds the next
            // iteration through the new record.
            if (has_per_iteration_lexical) {
                JsInterpEnv* next_env = js_interp_env_clone(env_root.env);
                JsInterpEnvRoot next_env_root(next_env);
                if (!next_env || !next_env_root.registered) return js_interp_throw(ItemError);
                JsInterpFrame update_frame = loop_frame;
                update_frame.env = next_env;
                if (loop->update) {
                JsInterpCompletion update = js_interp_eval(&update_frame,
                        (JsAstNode*)loop->update);
                if (update.kind != JS_INTERP_NORMAL) return update;
                }
                env_root.replace_with(&next_env_root);
            } else if (loop->update) {
                JsInterpCompletion update = js_interp_eval(&loop_frame,
                    (JsAstNode*)loop->update);
                if (update.kind != JS_INTERP_NORMAL) return update;
            }
            resume_body = false;
        }
    }
    case JS_AST_NODE_SWITCH_STATEMENT:
        return js_interp_exec_switch(frame, (JsSwitchNode*)node);
    case JS_AST_NODE_FOR_OF_STATEMENT:
        return js_interp_exec_for_of(frame, (JsForOfNode*)node, false);
    case JS_AST_NODE_FOR_IN_STATEMENT:
        return js_interp_exec_for_of(frame, (JsForOfNode*)node, true);
    case AST_NODE_TRY_STAM: {
        JsTryNode* tried = (JsTryNode*)node;
        JsInterpCompletion completion = js_interp_exec(frame, (JsAstNode*)tried->block);
        RootFrame roots(1);
        Rooted<Item> completion_root(roots, completion.value);
        if (completion.kind == JS_INTERP_THROW && tried->handler) {
            completion = js_interp_exec_catch(frame, (JsCatchNode*)tried->handler,
                completion_root.get());
            completion_root.set(completion.value);
        }
        if (completion.kind == JS_INTERP_YIELD || completion.kind == JS_INTERP_AWAIT) {
            return completion;
        }
        if (tried->finalizer) {
            JsInterpCompletion finalizer = js_interp_exec(frame,
                (JsAstNode*)tried->finalizer);
            if (finalizer.kind != JS_INTERP_NORMAL) {
                completion = finalizer;
                completion_root.set(completion.value);
            }
        }
        completion.value = completion_root.get();
        return completion;
    }
    case AST_NODE_RETURN_STAM: {
        JsReturnNode* returned = (JsReturnNode*)node;
        if (returned->value && returned->value->node_type == AST_NODE_CALL_EXPR) {
            JsInterpCompletion tail;
            if (js_interp_prepare_self_tail_call(frame,
                    (JsCallNode*)returned->value, &tail)) return tail;
        }
        JsInterpCompletion value = js_interp_eval(frame, (JsAstNode*)returned->value);
        return value.kind == JS_INTERP_NORMAL
            ? JsInterpCompletion{JS_INTERP_RETURN, value.value, NULL, 0} : value;
    }
    case AST_NODE_RAISE_STAM: {
        JsThrowNode* thrown = (JsThrowNode*)node;
        RootFrame roots(1);
        Rooted<Item> value_root(roots, ItemNull);
        JsInterpCompletion value = js_interp_eval(frame, (JsAstNode*)thrown->value);
        if (value.kind != JS_INTERP_NORMAL) return value;
        value_root.set(value.value);
        return js_interp_throw(js_throw_value(value_root.get()));
    }
    case AST_NODE_BREAK_STAM: {
        AstBreakContinueNode* control = (AstBreakContinueNode*)node;
        return {JS_INTERP_BREAK, make_js_undefined(), control->label, control->label_len};
    }
    case AST_NODE_CONTINUE_STAM: {
        AstBreakContinueNode* control = (AstBreakContinueNode*)node;
        return {JS_INTERP_CONTINUE, make_js_undefined(), control->label, control->label_len};
    }
    default:
        return js_interp_eval(frame, node);
    }
}

static bool js_interp_operator_supported(Operator op) {
    switch (op) {
    case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL: case OPERATOR_DIV:
    case OPERATOR_MOD: case OPERATOR_POW: case OPERATOR_JS_EXP:
    case OPERATOR_EQ: case OPERATOR_NE: case OPERATOR_JS_STRICT_EQ:
    case OPERATOR_JS_STRICT_NE: case OPERATOR_LT: case OPERATOR_LE:
    case OPERATOR_GT: case OPERATOR_GE: case OPERATOR_AND: case OPERATOR_OR:
    case OPERATOR_IN: case OPERATOR_JS_INSTANCEOF: case OPERATOR_JS_NULLISH_COALESCE:
    case OPERATOR_JS_BIT_AND: case OPERATOR_JS_BIT_OR: case OPERATOR_JS_BIT_XOR:
    case OPERATOR_JS_LSHIFT: case OPERATOR_JS_RSHIFT: case OPERATOR_JS_URSHIFT:
    case OPERATOR_NOT: case OPERATOR_POS: case OPERATOR_NEG: case OPERATOR_JS_BIT_NOT:
    case OPERATOR_JS_TYPEOF: case OPERATOR_JS_VOID: case OPERATOR_JS_INCREMENT:
    case OPERATOR_JS_DECREMENT: case OPERATOR_JS_DELETE:
    case OPERATOR_ASSIGN: case OPERATOR_JS_ADD_ASSIGN:
    case OPERATOR_JS_SUB_ASSIGN: case OPERATOR_JS_MUL_ASSIGN: case OPERATOR_JS_DIV_ASSIGN:
    case OPERATOR_JS_MOD_ASSIGN: case OPERATOR_JS_EXP_ASSIGN: case OPERATOR_JS_BIT_AND_ASSIGN:
    case OPERATOR_JS_BIT_OR_ASSIGN: case OPERATOR_JS_BIT_XOR_ASSIGN:
    case OPERATOR_JS_LSHIFT_ASSIGN: case OPERATOR_JS_RSHIFT_ASSIGN:
    case OPERATOR_JS_URSHIFT_ASSIGN:
    case OPERATOR_JS_AND_ASSIGN: case OPERATOR_JS_OR_ASSIGN:
    case OPERATOR_JS_NULLISH_ASSIGN:
        return true;
    default:
        return false;
    }
}

struct JsInterpSupportState {
    bool supported;
};

static void js_interp_check_child(JsAstNode* child, void* opaque);

static bool js_interp_identifier_is(JsAstNode* node, const char* name) {
    if (!node || node->node_type != AST_NODE_IDENT) return false;
    return js_interp_name_equals(((JsIdentifierNode*)node)->name, name);
}

static bool js_interp_pattern_supported(JsAstNode* pattern) {
    if (!pattern) return false;
    switch (pattern->node_type) {
    case AST_NODE_IDENT:
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR:
        return true;
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        return ((JsAssignmentPatternNode*)pattern)->left &&
            js_interp_pattern_supported((JsAstNode*)((JsAssignmentPatternNode*)pattern)->left);
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        return ((JsSpreadElementNode*)pattern)->argument &&
            js_interp_pattern_supported((JsAstNode*)((JsSpreadElementNode*)pattern)->argument);
    case JS_AST_NODE_ARRAY_PATTERN: {
        for (JsAstNode* element = (JsAstNode*)((JsArrayPatternNode*)pattern)->elements;
                element; element = (JsAstNode*)element->next) {
            if (element->node_type != AST_NODE_NULL &&
                    !js_interp_pattern_supported(element)) return false;
        }
        return true;
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        for (JsAstNode* property = (JsAstNode*)((JsObjectPatternNode*)pattern)->properties;
                property; property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_REST_PROPERTY ||
                    property->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                if (!js_interp_pattern_supported((JsAstNode*)((JsSpreadElementNode*)property)->argument)) {
                    return false;
                }
            } else if (property->node_type != AST_NODE_PROPERTY ||
                    !js_interp_pattern_supported((JsAstNode*)((JsPropertyNode*)property)->value)) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

static bool js_interp_function_params_supported(JsFunctionNode* function) {
    for (JsAstNode* param = function ? (JsAstNode*)function->params : NULL;
            param; param = (JsAstNode*)param->next) {
        if (!js_interp_pattern_supported(param)) return false;
    }
    return true;
}

static bool js_interp_iteration_head_supported(JsAstNode* left) {
    if (!left) return false;
    if (js_interp_pattern_supported(left) || left->node_type == AST_NODE_IDENT ||
            left->node_type == AST_NODE_MEMBER_EXPR ||
            left->node_type == AST_NODE_INDEX_EXPR ||
            left->node_type == AST_NODE_CALL_EXPR) return true;
    if (left->node_type != AST_NODE_VAR_STAM) return false;
    JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)left;
    JsVariableDeclaratorNode* declarator = declaration->declarations &&
            declaration->declarations->node_type == AST_NODE_VARIABLE_DECLARATOR
        ? (JsVariableDeclaratorNode*)declaration->declarations : NULL;
    return declarator && !declarator->next && declarator->id &&
        js_interp_pattern_supported((JsAstNode*)declarator->id);
}

static void js_interp_check_node(JsAstNode* node, JsInterpSupportState* state) {
    if (!node || !state || !state->supported) return;
    switch (node->node_type) {
    case AST_SCRIPT: case AST_NODE_BLOCK: case AST_NODE_EXPR_STMT:
    case AST_NODE_VAR_STAM:
    case AST_NODE_LITERAL: case AST_NODE_PRIMARY: case AST_NODE_NULL:
    case AST_NODE_NEW_EXPR:
    case AST_NODE_ARRAY: case AST_NODE_MAP:
    case AST_NODE_CONDITIONAL_EXPR: case AST_NODE_SEQ:
    case AST_NODE_IF_EXPR: case AST_NODE_WHILE_STAM: case AST_NODE_DO_WHILE_STAM:
    case AST_NODE_FOR_STAM: case AST_NODE_RETURN_STAM: case AST_NODE_RAISE_STAM:
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: case AST_NODE_TRY_STAM:
    case JS_AST_NODE_IMPORT_DECLARATION: case JS_AST_NODE_EXPORT_DECLARATION:
    case JS_AST_NODE_IMPORT_SPECIFIER: case JS_AST_NODE_EXPORT_SPECIFIER:
    case JS_AST_NODE_SWITCH_STATEMENT: case JS_AST_NODE_SWITCH_CASE:
    case JS_AST_NODE_TEMPLATE_LITERAL: case JS_AST_NODE_TEMPLATE_ELEMENT:
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REGEX:
    case JS_AST_NODE_ARRAY_PATTERN: case JS_AST_NODE_OBJECT_PATTERN:
    case JS_AST_NODE_ASSIGNMENT_PATTERN: case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_AWAIT_EXPRESSION:
    case JS_AST_NODE_YIELD_EXPRESSION:
        break;
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION:
        break;
    case JS_AST_NODE_METHOD_DEFINITION: {
        JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)node;
        if ((method->kind == JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR &&
                 method->static_method)) state->supported = false;
        break;
    }
    case JS_AST_NODE_FIELD_DEFINITION: {
        break;
    }
    case JS_AST_NODE_STATIC_BLOCK:
    case JS_AST_NODE_LABELED_STATEMENT:
    case JS_AST_NODE_WITH_STATEMENT:
    case JS_AST_NODE_TAGGED_TEMPLATE:
        break;
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* loop = (JsForOfNode*)node;
        // This preflight runs while creating nested functions.  `for await`
        // may be syntactically valid in an uninvoked async body even though
        // iteration itself is deferred until that function resumes.
        if (!js_interp_iteration_head_supported((JsAstNode*)loop->left)) {
            state->supported = false;
        }
        break;
    }
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: {
        JsFunctionNode* function = (JsFunctionNode*)node;
        if (!js_interp_function_params_supported(function)) state->supported = false;
        break;
    }
    case AST_NODE_IDENT:
        // `arguments`, `super`, and `import.meta` are activation/module
        // bindings carried by the shared runtime kernels.
        break;
    case AST_NODE_VARIABLE_DECLARATOR:
        if (!((JsVariableDeclaratorNode*)node)->id ||
                !js_interp_pattern_supported((JsAstNode*)((JsVariableDeclaratorNode*)node)->id)) {
            state->supported = false;
        }
        break;
    case AST_NODE_CALL_EXPR:
        break;
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR:
        break;
    case AST_NODE_PROPERTY: {
        break;
    }
    case AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* handler = (JsCatchNode*)node;
        if (handler->param && !js_interp_pattern_supported((JsAstNode*)handler->param)) {
            state->supported = false;
        }
        break;
    }
    case AST_NODE_UNARY:
        if (!js_interp_operator_supported(((JsUnaryNode*)node)->op)) state->supported = false;
        break;
    case AST_NODE_BINARY:
        if (!js_interp_operator_supported(((JsBinaryNode*)node)->op)) state->supported = false;
        break;
    case AST_NODE_ASSIGN:
        if (!js_interp_operator_supported(((JsAssignmentNode*)node)->op)) state->supported = false;
        break;
    default:
        state->supported = false;
        break;
    }
    if (state->supported) js_ast_visit_children(node, js_interp_check_child, state);
}

static void js_interp_check_child(JsAstNode* child, void* opaque) {
    js_interp_check_node(child, (JsInterpSupportState*)opaque);
}

bool js_interp_script_is_supported(JsScript* script) {
    if (!script || !script->ast_root) return false;
    JsInterpSupportState state = {true};
    js_interp_check_node((JsAstNode*)script->ast_root, &state);
    return state.supported;
}

static bool js_interp_tail_reuse_node_safe(JsAstNode* node, JsAstNode* root);

static bool js_interp_tail_reuse_child_unsafe(JsAstNode* child, void* opaque) {
    return !js_interp_tail_reuse_node_safe(child, (JsAstNode*)opaque);
}

static bool js_interp_tail_reuse_node_safe(JsAstNode* node, JsAstNode* root) {
    if (!node) return true;
    if (node != root && (node->node_type == AST_NODE_FUNC ||
            node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_ARROW_FUNC ||
            node->node_type == JS_AST_NODE_CLASS_DECLARATION ||
            node->node_type == JS_AST_NODE_CLASS_EXPRESSION)) {
        return false;
    }
    if (node->node_type == JS_AST_NODE_WITH_STATEMENT ||
            node->node_type == AST_NODE_TRY_STAM) return false;
    if (node->node_type == AST_NODE_IDENT) {
        String* name = ((JsIdentifierNode*)node)->name;
        if (js_interp_name_equals(name, "arguments") ||
                js_interp_name_equals(name, "eval")) return false;
    }
    return !js_ast_any_child(node, js_interp_tail_reuse_child_unsafe, root);
}

static bool js_interp_arguments_usage_node(JsAstNode* node, JsAstNode* root);

static bool js_interp_arguments_usage_child(JsAstNode* child, void* opaque) {
    return js_interp_arguments_usage_node(child, (JsAstNode*)opaque);
}

static bool js_interp_arguments_usage_node(JsAstNode* node, JsAstNode* root) {
    if (!node) return false;
    // Ordinary nested functions own their own `arguments`, while arrows retain
    // the enclosing function's binding and must remain part of this scan.
    if (node != root && (node->node_type == AST_NODE_FUNC ||
            node->node_type == AST_NODE_FUNC_EXPR)) return false;
    if (node->node_type == AST_NODE_IDENT && js_interp_name_equals(
            ((JsIdentifierNode*)node)->name, "arguments")) return true;
    return js_ast_any_child(node, js_interp_arguments_usage_child, root);
}

static bool js_interp_function_uses_arguments(JsFunctionNode* function) {
    if (!function) return false;
    for (JsAstNode* param = (JsAstNode*)function->params; param;
            param = (JsAstNode*)param->next) {
        if (js_interp_arguments_usage_node(param, (JsAstNode*)function)) return true;
    }
    return js_interp_arguments_usage_node((JsAstNode*)function->body,
        (JsAstNode*)function);
}

static Item js_interp_configure_function_metadata(Item function_item) {
    if (get_type_id(function_item) != LMD_TYPE_FUNC) return function_item;
    JsFunction* function = (JsFunction*)function_item.function;
    if (!function || !function->ast_function) return ItemError;
    function->ast_has_direct_eval = js_interp_function_has_direct_eval(
        function->ast_function);
    function->ast_uses_arguments = js_interp_function_uses_arguments(
        function->ast_function);
    function->ast_tail_reuse_safe = js_interp_tail_reuse_node_safe(
        (JsAstNode*)function->ast_function, (JsAstNode*)function->ast_function);
    return function_item;
}

static bool js_interp_function_tail_reuse_safe(JsFunction* function) {
    return function && function->ast_tail_reuse_safe;
}

Item js_interp_call_function(JsFunction* function, Item* args, int arg_count,
        uint64_t* result_home) {
    (void)result_home;
    if (!function || function->body_kind != JS_FUNCTION_BODY_AST ||
            !function->ast_function || !function->ast_script) return ItemError;
    RootFrame roots(7);
    Rooted<Item> function_root(roots, (Item){.function = (Function*)function});
    uint64_t* lexical_this_home = (function->flags & JS_FUNC_FLAG_ARROW)
        ? js_interp_function_lexical_this_home(function) : NULL;
    Rooted<Item> this_root(roots, (function->flags & JS_FUNC_FLAG_ARROW)
        ? js_interp_function_lexical_this(function) : js_get_lexical_this_binding());
    Rooted<Item> new_target_root(roots, (function->flags & JS_FUNC_FLAG_ARROW)
        ? function->ast_lexical_new_target : js_get_new_target());
    Rooted<Item> home_class_root(roots, function->home_class);
    Rooted<Item> arguments_root(roots, ItemNull);
    Rooted<Item> tail_arguments_root(roots, ItemNull);
    Rooted<Item> tail_scratch_root(roots, ItemNull);
    JsBlockNode* body_block = function->ast_function->body &&
            function->ast_function->body->node_type == AST_NODE_BLOCK
        ? (JsBlockNode*)function->ast_function->body : NULL;
    // A block record exists only when its predeclared lexical graph has
    // bindings. Empty function-body records otherwise add one GC allocation
    // to every call without changing identifier resolution.
    bool body_needs_environment = body_block &&
        js_interp_scope_needs_environment(body_block->vars);
    // Function declarations are hoisted into the function environment, yet
    // their closures must retain the function body's lexical environment so
    // a later same-body class/let binding remains visible when they run.
    Item* call_args = args;
    int call_arg_count = arg_count;
    JsInterpTailScratch tail_scratch = {
        {tail_arguments_root.home(), tail_scratch_root.home()}, 0};
    // Reusing an activation removes the allocation/GC cost from a direct
    // self-tail call. Keep this deliberately narrow: nested closures, eval,
    // `arguments`, classes, and dynamic scopes can retain a prior activation.
    bool reuse_tail_activation = js_interp_function_tail_reuse_safe(function);
    bool reusing_tail_activation = false;
    JsInterpEnvRoot retained_env_root;
    JsInterpEnvRoot retained_body_env_root;
    for (;;) {
        JsInterpEnv* env = reusing_tail_activation ? retained_env_root.env
            : js_interp_env_create(function->ast_function->vars, function->interp_env);
        JsInterpEnvRoot env_root(reusing_tail_activation ? NULL : env);
        if (!env || (!reusing_tail_activation && !env_root.registered)) return ItemError;
        if (!(function->flags & JS_FUNC_FLAG_ARROW)) {
            env->has_lexical_this = 1;
            env->lexical_this = this_root.get().item;
        }
        JsInterpEnv* body_env = body_needs_environment
            ? (reusing_tail_activation ? retained_body_env_root.env
                : js_interp_env_create(body_block->vars, env)) : NULL;
        JsInterpEnvRoot body_env_root(reusing_tail_activation ? NULL : body_env);
        if (body_needs_environment && (!body_env ||
                (!reusing_tail_activation && !body_env_root.registered))) return ItemError;
        if (!(function->flags & JS_FUNC_FLAG_ARROW) && function->ast_uses_arguments &&
                !reusing_tail_activation) {
            bool strict = (function->flags & JS_FUNC_FLAG_STRICT) != 0;
            bool mapped = !strict && js_interp_function_has_simple_params(
                function->ast_function);
            // Materialize before parameter defaults and keep it in the traced
            // activation record. Later nested calls therefore cannot replace an
            // AST function's lexical arguments binding through ambient state.
            arguments_root.set(js_build_arguments_object_for_call(call_args,
                call_arg_count, mapped ? 0 : 1,
                (Item){.function = (Function*)function}));
            if (item_is_error(arguments_root.get())) return arguments_root.get();
            env->arguments_object = arguments_root.get().item;
            env->function_node = (AstNode*)function->ast_function;
            env->arguments_are_mapped = mapped ? 1 : 0;
        }
        // Direct eval's function-scoped `var` declarations live in the shared
        // EvalContext journal for this activation, including names absent from
        // the static AST scope.
        // A non-strict function body has its own lexical record. Direct eval
        // must see it when rejecting conflicting var declarations.
        JsInterpEvalLocalFrame eval_local(body_env ? body_env : env,
            function->ast_has_direct_eval);
        uint64_t* frame_this_home = lexical_this_home ? lexical_this_home
            : ((function->flags & JS_FUNC_FLAG_ARROW) ? this_root.home()
                : &env->lexical_this);
        JsInterpFrame frame = {function->ast_script, env, frame_this_home,
            new_target_root.home(), home_class_root.home(),
            (function->flags & JS_FUNC_FLAG_STRICT) != 0, NULL, 0, function,
            false, &tail_scratch};
        if (body_env) frame.env = body_env;
        JsInterpCompletion initialized = js_interp_initialize_scope(&frame,
            function->ast_function->vars, false);
        if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
        initialized = js_interp_bind_named_function_expression_self(&frame,
            function, function_root.get());
        if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
        js_interp_prepare_parameter_tdz(&frame, function->ast_function);
        int index = 0;
        for (JsAstNode* param = (JsAstNode*)function->ast_function->params; param;
                param = (JsAstNode*)param->next) {
            RootFrame param_roots(1);
            Rooted<Item> value_root(param_roots, make_js_undefined());
            if (param->node_type == JS_AST_NODE_REST_ELEMENT) {
                value_root.set(js_array_new(0));
                if (item_is_error(value_root.get())) return value_root.get();
                for (; index < call_arg_count; index++) {
                    Item argument = call_args ? call_args[index] : make_js_undefined();
                    Item pushed = js_array_push(value_root.get(), argument);
                    if (item_is_error(pushed)) return pushed;
                }
            } else {
                value_root.set(index < call_arg_count && call_args ? call_args[index]
                    : make_js_undefined());
                index++;
            }
            frame.in_parameter_initializer = true;
            JsInterpCompletion bound = js_interp_bind_pattern(&frame, param,
                value_root.get(), true);
            frame.in_parameter_initializer = false;
            if (bound.kind != JS_INTERP_NORMAL) return bound.value;
        }
        initialized = js_interp_initialize_function_declarations(&frame,
            function->ast_function->vars);
        if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
        if (body_block) {
            initialized = js_interp_initialize_scope(&frame, body_block->vars);
            if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
        }
        JsInterpCompletion result = body_block
            ? js_interp_exec_list(&frame, (JsAstNode*)body_block->statements)
            : js_interp_eval(&frame, (JsAstNode*)function->ast_function->body);
        if (result.kind == JS_INTERP_TAIL_CALL) {
            tail_arguments_root.set(result.tail_arguments);
            if (get_type_id(tail_arguments_root.get()) != LMD_TYPE_ARRAY) return ItemError;
            this_root.set(result.tail_this);
            call_arg_count = (int)js_array_length(tail_arguments_root.get());
            call_args = call_arg_count > 0 ? tail_arguments_root.get().array->items : NULL;
            if (reuse_tail_activation && !reusing_tail_activation) {
                retained_env_root.adopt_from(&env_root);
                if (body_needs_environment) retained_body_env_root.adopt_from(&body_env_root);
                reusing_tail_activation = true;
            }
            continue;
        }
        if (result.kind == JS_INTERP_RETURN) return result.value;
        // FunctionDeclaration and method bodies complete with undefined without a
        // return. Preserve expression-bodied arrows and field-initializer thunks,
        // whose normal completion is their callable result.
        if (result.kind == JS_INTERP_NORMAL) {
            return function->ast_function->body &&
                    function->ast_function->body->node_type == AST_NODE_BLOCK
                ? make_js_undefined() : result.value;
        }
        if (result.kind == JS_INTERP_THROW) return result.value;
        return js_throw_syntax_error(js_make_string("illegal control flow"));
    }
}

Item js_interp_start_async_function(JsFunction* function, Item* args,
        int arg_count) {
    if (!function || function->body_kind != JS_FUNCTION_BODY_AST) return ItemError;
    RootFrame roots(3);
    Rooted<Item> function_root(roots, (Item){.function = (Function*)function});
    Rooted<Item> this_root(roots, (function->flags & JS_FUNC_FLAG_ARROW)
        ? js_interp_function_lexical_this(function) : js_get_lexical_this_binding());
    Rooted<Item> args_root(roots, js_array_new(0));
    if (item_is_error(args_root.get())) return args_root.get();
    for (int index = 0; index < arg_count; index++) {
        Item pushed = js_array_push(args_root.get(), args ? args[index] : ItemNull);
        if (item_is_error(pushed)) return pushed;
    }
    Item context_index = js_async_context_create_ast(function_root.get(),
        args_root.get(), this_root.get());
    if (item_is_error(context_index)) return context_index;
    Item promise = js_async_get_promise(context_index);
    js_async_start(context_index);
    return promise;
}

static Item js_interp_prepare_suspended_activation(JsFunction* function,
        Item function_item, Item arguments, Item this_value,
        JsInterpEnv** out_function_env, JsInterpEnv** out_body_env);

Item js_interp_create_generator(JsFunction* function, Item* args, int arg_count) {
    if (!function || function->body_kind != JS_FUNCTION_BODY_AST ||
            !function->ast_function) {
        return js_throw_type_error("unsupported interpreted generator form");
    }
    RootFrame roots(4);
    Rooted<Item> function_root(roots, (Item){.function = (Function*)function});
    Rooted<Item> arguments_root(roots, js_array_new(0));
    Rooted<Item> this_root(roots, (function->flags & JS_FUNC_FLAG_ARROW)
        ? js_interp_function_lexical_this(function) : js_get_lexical_this_binding());
    Rooted<Item> generator_root(roots, ItemNull);
    if (item_is_error(arguments_root.get())) return arguments_root.get();
    for (int index = 0; index < arg_count; index++) {
        Item pushed = js_array_push(arguments_root.get(), args ? args[index] : ItemNull);
        if (item_is_error(pushed)) return pushed;
    }
    function = (JsFunction*)function_root.get().function;
    // FunctionDeclarationInstantiation, including default parameters, precedes
    // OrdinaryCreateFromConstructor. Keep the new environments rooted until
    // the generator carrier adopts them below.
    JsInterpEnv* function_env = NULL;
    JsInterpEnv* body_env = NULL;
    Item prepared = js_interp_prepare_suspended_activation(function,
        function_root.get(), arguments_root.get(), this_root.get(),
        &function_env, &body_env);
    if (item_is_error(prepared)) return prepared;
    JsInterpEnvRoot function_env_root(function_env);
    JsInterpEnvRoot body_env_root(body_env == function_env ? NULL : body_env);
    if (!function_env_root.registered ||
            (body_env != function_env && !body_env_root.registered)) {
        return ItemError;
    }
    generator_root.set(js_generator_create_ast(function_root.get(), arguments_root.get(),
        this_root.get(), (function->flags & JS_FUNC_FLAG_ASYNC_GEN) != 0));
    if (item_is_error(generator_root.get())) return generator_root.get();
    JsGeneratorStateRecord* state = js_generator_get_ast_state(generator_root.get());
    if (!state) return ItemError;
    state->ast_function_env = function_env;
    state->ast_body_env = body_env;
    state->ast_initialized = true;
    return generator_root.get();
}

static Item js_interp_prepare_suspended_activation(JsFunction* function,
        Item function_item, Item arguments, Item this_value,
        JsInterpEnv** out_function_env, JsInterpEnv** out_body_env) {
    if (!function || !function->ast_function || !out_function_env || !out_body_env ||
            get_type_id(arguments) != LMD_TYPE_ARRAY) return ItemError;
    JsBlockNode* body = function->ast_function->body &&
            function->ast_function->body->node_type == AST_NODE_BLOCK
        ? (JsBlockNode*)function->ast_function->body : NULL;
    RootFrame roots(4);
    Rooted<Item> function_root(roots, function_item);
    Rooted<Item> arguments_root(roots, arguments);
    Rooted<Item> this_root(roots, this_value);
    Rooted<Item> arguments_object_root(roots, ItemNull);
    JsInterpEnv* function_env = js_interp_env_create(function->ast_function->vars,
        function->interp_env);
    JsInterpEnvRoot function_env_root(function_env);
    if (!function_env || !function_env_root.registered) return ItemError;
    JsInterpEnv* body_env = body ? js_interp_env_create(body->vars, function_env)
        : function_env;
    JsInterpEnvRoot body_env_root(body ? body_env : NULL);
    if (!body_env || (body && !body_env_root.registered)) return ItemError;
    bool strict = (function->flags & JS_FUNC_FLAG_STRICT) != 0;
    bool mapped = !strict && js_interp_function_has_simple_params(function->ast_function);
    int arg_count = (int)js_array_length(arguments_root.get());
    Item* args = arg_count > 0 ? arguments_root.get().array->items : NULL;
    arguments_object_root.set(js_build_arguments_object_for_call(args, arg_count,
        mapped ? 0 : 1, function_root.get()));
    if (item_is_error(arguments_object_root.get())) return arguments_object_root.get();
    function_env->arguments_object = arguments_object_root.get().item;
    function_env->function_node = (AstNode*)function->ast_function;
    function_env->arguments_are_mapped = mapped ? 1 : 0;
    Item home_class = function->home_class;
    JsInterpFrame init_frame = {};
    init_frame.script = function->ast_script;
    init_frame.env = function_env;
    init_frame.this_home = this_root.home();
    init_frame.home_class_home = &home_class.item;
    init_frame.strict = strict;
    init_frame.active_function = function;
    // Generator parameters execute during activation setup, before the first
    // resume, but direct eval still needs the activation's shared var journal.
    JsInterpEvalLocalFrame eval_local(body_env ? body_env : function_env,
        function->ast_has_direct_eval);
    JsInterpCompletion initialized = js_interp_initialize_scope(&init_frame,
        function->ast_function->vars, false);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
    initialized = js_interp_bind_named_function_expression_self(&init_frame,
        function, function_root.get());
    if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
    js_interp_prepare_parameter_tdz(&init_frame, function->ast_function);
    int index = 0;
    for (JsAstNode* parameter = (JsAstNode*)function->ast_function->params;
            parameter; parameter = (JsAstNode*)parameter->next) {
        RootFrame parameter_roots(1);
        Rooted<Item> value_root(parameter_roots, make_js_undefined());
        if (parameter->node_type == JS_AST_NODE_REST_ELEMENT) {
            value_root.set(js_array_new(0));
            if (item_is_error(value_root.get())) return value_root.get();
            for (; index < arg_count; index++) {
                Item pushed = js_array_push(value_root.get(), args[index]);
                if (item_is_error(pushed)) return pushed;
            }
        } else {
            value_root.set(index < arg_count ? args[index] : make_js_undefined());
            index++;
        }
        init_frame.in_parameter_initializer = true;
        JsInterpCompletion bound = js_interp_bind_pattern(&init_frame,
            parameter, value_root.get(), true);
        init_frame.in_parameter_initializer = false;
        if (bound.kind != JS_INTERP_NORMAL) return bound.value;
    }
    initialized = js_interp_initialize_function_declarations(&init_frame,
        function->ast_function->vars);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
    if (body) {
        JsInterpFrame body_init_frame = init_frame;
        body_init_frame.env = body_env;
        initialized = js_interp_initialize_scope(&body_init_frame, body->vars);
        if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
    }
    *out_function_env = function_env;
    *out_body_env = body_env;
    return make_js_undefined();
}

extern "C" Item js_interp_resume_generator(Item generator,
        JsGeneratorStateRecord* state, Item input) {
    if (!state || get_type_id(state->ast_function) != LMD_TYPE_FUNC) return ItemError;
    RootFrame roots(8);
    Rooted<Item> generator_root(roots, generator);
    Rooted<Item> function_root(roots, state->ast_function);
    Rooted<Item> arguments_root(roots, state->ast_arguments);
    Rooted<Item> this_root(roots, state->ast_this);
    Rooted<Item> input_root(roots, input);
    Rooted<Item> pending_root(roots, state->ast_pending_resume_input);
    Rooted<Item> home_class_root(roots, ItemNull);
    Rooted<Item> yield_values_root(roots, state->ast_yield_values);
    if (get_type_id(function_root.get()) != LMD_TYPE_FUNC) return ItemError;
    JsFunction* function = (JsFunction*)function_root.get().function;
    if (!function || !function->ast_function ||
            get_type_id(arguments_root.get()) != LMD_TYPE_ARRAY) return ItemError;
    home_class_root.set(function->home_class);
    JsBlockNode* body = function->ast_function->body &&
            function->ast_function->body->node_type == AST_NODE_BLOCK
        ? (JsBlockNode*)function->ast_function->body : NULL;
    if (!body) return js_throw_type_error("interpreted generator requires a block body");

    if (state->ast_yield_skip > 0) {
        if (get_type_id(yield_values_root.get()) != LMD_TYPE_ARRAY) {
            yield_values_root.set(js_array_new(0));
            if (item_is_error(yield_values_root.get())) return yield_values_root.get();
            state->ast_yield_values = yield_values_root.get();
        }
        int64_t resume_index = state->ast_yield_skip - 1;
        while (js_array_length(yield_values_root.get()) < resume_index) {
            Item padded = js_array_push(yield_values_root.get(), make_js_undefined());
            if (item_is_error(padded)) return padded;
        }
        Item stored = js_array_length(yield_values_root.get()) == resume_index
            ? js_array_push(yield_values_root.get(), input_root.get())
            : js_elements_set_int_direct(yield_values_root.get(), resume_index,
                input_root.get());
        if (item_is_error(stored)) return stored;
    }

    if (!state->ast_initialized) {
        // The generator carrier traces these environments before the local
        // exact roots leave scope, preserving its activation across next().
        Item prepared = js_interp_prepare_suspended_activation(function,
            function_root.get(), arguments_root.get(), this_root.get(),
            &state->ast_function_env, &state->ast_body_env);
        if (item_is_error(prepared)) return prepared;
        state->ast_initialized = true;
    }

    // List continuations form an outer-to-inner stack. Resume at the function
    // body first so its structural path reaches the suspended inner statement.
    bool resuming_list = state->ast_list_continuation &&
        state->ast_list_continuation->statements == (JsAstNode*)body->statements;
    bool replaying_suspended_statement = resuming_list &&
        state->ast_list_continuation->replay_current_statement;
    int64_t resumed_yield_count = resuming_list && state->ast_pending_resume_yield > 0
        ? state->ast_list_continuation->yield_count_before_next_statement : 0;
    if (resuming_list && js_gen_is_throw_signal(input_root.get()) &&
            js_interp_generator_list_throw_requires_replay(state)) {
        // Replay only injected throws through their owning try. Return signals
        // resume structurally so IteratorClose does not repeat prior effects.
        while (state->ast_list_continuation) {
            js_interp_generator_clear_list_continuation(state);
        }
        resuming_list = false;
        replaying_suspended_statement = false;
    }
    if (!resuming_list && state->ast_list_continuation) {
        // The root dispatcher cannot enter a nested block cursor directly;
        // discard stale cursors rather than running an unreachable block.
        while (state->ast_list_continuation) {
            js_interp_generator_clear_list_continuation(state);
        }
    }
    if (resuming_list && state->ast_pending_resume_yield > 0) {
        // Replay the handler yield through its owning try; its old list cursor
        // would otherwise skip the ledger position that consumes next()'s input.
        js_interp_generator_clear_nested_list_continuations(state);
    }
    if (!resuming_list && state->ast_yield_skip > 0 &&
            (js_gen_is_throw_signal(input_root.get()) ||
             js_gen_is_return_signal(input_root.get()))) {
        // A yield in finally suspends before the injected completion escapes.
        // Retain the original signal and re-inject it on every replay until a
        // later resume completes the finally chain.
        state->ast_pending_resume_yield = state->ast_yield_skip;
        state->ast_pending_resume_input = input_root.get();
        pending_root.set(input_root.get());
    }

    int64_t yielded = resumed_yield_count;
    JsInterpFrame frame = {};
    frame.script = function->ast_script;
    frame.env = state->ast_body_env;
    frame.this_home = this_root.home();
    frame.home_class_home = home_class_root.home();
    frame.strict = (function->flags & JS_FUNC_FLAG_STRICT) != 0;
    frame.active_function = function;
    frame.generator_yield_seen = &yielded;
    // A terminal-yield cursor starts after its observed yield. A nested yield
    // instead re-enters the suspended statement and consumes its replay ledger.
    frame.generator_yield_skip = resuming_list && !replaying_suspended_statement
        ? 0 : state->ast_yield_skip;
    frame.generator_resume_input = input_root.get();
    frame.generator_yield_values = yield_values_root.get();
    frame.generator_abrupt_resume_yield = state->ast_pending_resume_yield;
    frame.generator_abrupt_resume_input = pending_root.get();
    frame.generator_state = state;
    JsInterpEvalLocalFrame eval_local(state->ast_body_env ? state->ast_body_env
        : state->ast_function_env,
        function->ast_has_direct_eval);
    JsInterpCompletion result = js_interp_exec_list(&frame,
        (JsAstNode*)body->statements);
    if (result.kind == JS_INTERP_YIELD) {
        if (result.yield_delegate) {
            // The generator record is a traced owner, so the iterator remains
            // live while the common generator runtime drives its protocol.
            state->delegate = result.value;
            return ItemNull;
        }
        if (!state->ast_resumable_loop_active) state->ast_yield_skip++;
        return js_make_iter_result(result.value, false);
    }
    state->done = true;
    state->state = -1;
    state->ast_pending_resume_yield = 0;
    state->ast_pending_resume_input = ItemNull;
    js_interp_generator_clear_continuations(state);
    if (result.kind == JS_INTERP_RETURN) return js_make_iter_result(result.value, true);
    if (result.kind == JS_INTERP_NORMAL) {
        return js_make_iter_result(make_js_undefined(), true);
    }
    return result.value;
}

static Item js_interp_async_state_result(Item value, int64_t next_state) {
    RootFrame roots(1);
    Rooted<Item> result_root(roots, js_array_new(0));
    if (item_is_error(result_root.get())) return result_root.get();
    Item pushed = js_array_push(result_root.get(), value);
    if (item_is_error(pushed)) return pushed;
    pushed = js_array_push(result_root.get(), (Item){.item = i2it(next_state)});
    return item_is_error(pushed) ? pushed : result_root.get();
}

extern "C" Item js_interp_resume_async(JsAsyncContextStateRecord* state,
        Item input) {
    if (!state || get_type_id(state->ast_function) != LMD_TYPE_FUNC) return ItemError;
    RootFrame roots(6);
    Rooted<Item> function_root(roots, state->ast_function);
    Rooted<Item> arguments_root(roots, state->ast_arguments);
    Rooted<Item> this_root(roots, state->this_val);
    Rooted<Item> input_root(roots, input);
    Rooted<Item> home_class_root(roots,
        ((JsFunction*)function_root.get().function)->home_class);
    Rooted<Item> await_values_root(roots, state->ast_await_values);
    JsFunction* function = (JsFunction*)function_root.get().function;
    if (!function || !function->ast_function ||
            get_type_id(arguments_root.get()) != LMD_TYPE_ARRAY) return ItemError;
    JsBlockNode* body = function->ast_function->body &&
            function->ast_function->body->node_type == AST_NODE_BLOCK
        ? (JsBlockNode*)function->ast_function->body : NULL;
    if (state->ast_await_skip > 0) {
        if (get_type_id(await_values_root.get()) != LMD_TYPE_ARRAY) {
            await_values_root.set(js_array_new(0));
            if (item_is_error(await_values_root.get())) {
                return js_interp_async_state_result(await_values_root.get(), -2);
            }
            state->ast_await_values = await_values_root.get();
        }
        int64_t resume_index = state->ast_await_skip - 1;
        while (js_array_length(await_values_root.get()) < resume_index) {
            Item padded = js_array_push(await_values_root.get(), make_js_undefined());
            if (item_is_error(padded)) return js_interp_async_state_result(padded, -2);
        }
        Item stored = js_array_length(await_values_root.get()) == resume_index
            ? js_array_push(await_values_root.get(), input_root.get())
            : js_elements_set_int_direct(await_values_root.get(), resume_index,
                input_root.get());
        if (item_is_error(stored)) return js_interp_async_state_result(stored, -2);
    }
    if (!state->ast_initialized) {
        Item prepared = js_interp_prepare_suspended_activation(function,
            function_root.get(), arguments_root.get(), this_root.get(),
            &state->ast_function_env, &state->ast_body_env);
        if (item_is_error(prepared)) return js_interp_async_state_result(prepared, -2);
        state->ast_initialized = true;
    }
    int64_t awaited = 0;
    JsAstNode* suspended_statement = NULL;
    bool skip_completed_statements = state->ast_resume_statement != NULL;
    JsInterpFrame frame = {};
    frame.script = function->ast_script;
    frame.env = state->ast_body_env;
    frame.this_home = this_root.home();
    frame.home_class_home = home_class_root.home();
    frame.strict = (function->flags & JS_FUNC_FLAG_STRICT) != 0;
    frame.active_function = function;
    frame.async_await_seen = &awaited;
    frame.async_await_skip = state->ast_await_skip;
    frame.async_resume_input = input_root.get();
    frame.async_await_values = await_values_root.get();
    frame.async_root_statement_list = body ? (JsAstNode*)body->statements : NULL;
    frame.async_resume_statement = (JsAstNode*)state->ast_resume_statement;
    frame.async_suspended_statement = &suspended_statement;
    frame.async_skip_completed_statements = &skip_completed_statements;
    JsInterpEvalLocalFrame eval_local(state->ast_body_env ? state->ast_body_env
        : state->ast_function_env,
        function->ast_has_direct_eval);
    JsInterpCompletion result = body
        ? js_interp_exec_list(&frame, (JsAstNode*)body->statements)
        : js_interp_eval(&frame, (JsAstNode*)function->ast_function->body);
    if (result.kind == JS_INTERP_AWAIT) {
        state->ast_resume_statement = (AstNode*)suspended_statement;
        state->ast_await_skip++;
        return js_interp_async_state_result(result.value, state->ast_await_skip);
    }
    if (result.kind == JS_INTERP_RETURN) return js_interp_async_state_result(result.value, -1);
    if (result.kind == JS_INTERP_NORMAL) {
        return js_interp_async_state_result(make_js_undefined(), -1);
    }
    Item reason = item_is_error(result.value)
        ? js_error_lane_payload(result.value) : result.value;
    return js_interp_async_state_result(reason, -2);
}

Item js_interp_execute_script(Runtime* runtime, JsScript* script,
        uint64_t* result_home) {
    (void)result_home;
    if (!runtime || !script || !script->ast_root) return ItemError;
    EvalContext* eval = NULL;
    bool reusing_context = false;
    if (!js_prepare_eval_context(runtime, true, &eval, &reusing_context)) return ItemError;
    (void)eval;
    if (runtime->dom_ui_context) js_dom_set_ui_context(runtime->dom_ui_context);
    // DOM wrapper construction interns runtime property names, so it must run
    // after the common JS name pool becomes dynamic, as it does on the MIR path.
    if (!js_activate_runtime_name_pool()) return ItemError;
    JsInterpExecutionScope execution_scope;
    if (execution_scope.should_initialize_event_loop() &&
            js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_init();
    }
    JsInterpCurrentFileScope current_file(script->reference);
    Input* input = Input::create(context->pool);
    js_runtime_set_input(input);
    // Rejection happens before declaration instantiation or a user-visible
    // runtime action, but after the realm exists so its SyntaxError uses the
    // same JavaScript error lane as an admitted script.
    if (!js_interp_script_is_supported(script)) {
        return js_throw_syntax_error(js_make_string("unsupported AST interpreter script"));
    }
    int global_slots = js_interp_scope_slot_count(script->global_scope);
    // CJS/ES module evaluation can re-enter this executor through require or
    // import. The caller's slab is the dynamic state seen after that call.
    JsInterpModuleStateScope module_state;
    if (!lambda_module_state_prepare(script->module_state_id,
            (uint32_t)(global_slots > 0 ? global_slots : 1)) ||
            !js_set_active_module_state_id(script->module_state_id)) {
        return ItemError;
    }
    // DOM globals publish through the active module slab. MIR binds the
    // document only after that slab and its property-name image are ready.
    if (runtime->dom_doc) js_dom_set_document(runtime->dom_doc);
    JsInterpModuleNamespaceScope module_namespace(script);
    if (script->is_es_module && !module_namespace.active) return ItemError;
    RootFrame roots(3);
    // ES module code has an undefined top-level this; classic scripts keep
    // the shared realm receiver.
    Rooted<Item> this_root(roots, script->is_es_module ? make_js_undefined()
        : js_get_global_this());
    Rooted<Item> new_target_root(roots, js_get_new_target());
    Rooted<Item> home_class_root(roots, ItemNull);
    JsInterpFrame frame = {script, NULL, this_root.home(), new_target_root.home(),
        home_class_root.home(),
        script->strict_mode, NULL, 0, NULL, false, NULL};
    if (!script->is_es_module || !script->es_module_scope_initialized) {
        JsInterpCompletion initialized = js_interp_initialize_scope(&frame,
            script->global_scope);
        if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
    }
    JsInterpCompletion result = js_interp_exec(&frame, (JsAstNode*)script->ast_root);
    return result.value;
}

JsScript* js_interp_prepare_script(Runtime* runtime, const char* source,
        size_t source_length, const char* filename, bool strict) {
    if (!runtime || !source) return NULL;
    JsTranspiler* transpiler = js_transpiler_create(runtime);
    if (transpiler && strict) {
        transpiler->strict_mode = true;
        transpiler->global_scope->strict = true;
    }
    if (!transpiler || !js_transpiler_parse(transpiler, source, source_length)) {
        js_transpiler_destroy(transpiler);
        return NULL;
    }
    JsAstNode* ast = build_js_ast_indexed(transpiler, ts_tree_root_node(transpiler->tree));
    if (!ast || js_check_early_errors(transpiler, ast) > 0) {
        js_transpiler_destroy(transpiler);
        return NULL;
    }
    JsScript* script = js_script_adopt_transpiler(transpiler, runtime,
        filename ? filename : "<inline-js>");
    return script;
}

static Item js_interp_execute_source_mode(Runtime* runtime, const char* source,
        size_t source_length, const char* filename, bool is_module, bool strict,
        bool is_eval_script, uint64_t* result_home) {
    JsScript* script = js_interp_prepare_script(runtime, source, source_length,
        filename, strict);
    if (!script) {
        // Parse and early-error rejection must enter JavaScript as SyntaxError.
        // Dynamic Function invokes this entry beneath a user catch handler.
        return js_throw_syntax_error(js_make_string("invalid JavaScript source"));
    }
    script->is_module = is_module;
    if (strict) script->strict_mode = true;
    script->is_eval_script = is_eval_script;
    return js_interp_execute_script(runtime, script, result_home);
}

Item js_interp_execute_source(Runtime* runtime, const char* source,
        size_t source_length, const char* filename, uint64_t* result_home) {
    return js_interp_execute_source_mode(runtime, source, source_length, filename,
        false, false, false, result_home);
}

Item js_interp_execute_indirect_eval_source(Runtime* runtime, const char* source,
        size_t source_length, const char* filename, uint64_t* result_home) {
    return js_interp_execute_source_mode(runtime, source, source_length, filename,
        false, false, true, result_home);
}

Item js_interp_execute_module_source(Runtime* runtime, const char* source,
        size_t source_length, const char* filename, bool strict,
        uint64_t* result_home) {
    return js_interp_execute_source_mode(runtime, source, source_length, filename,
        true, strict, false, result_home);
}

static bool js_interp_is_lambda_module_path(const char* filename) {
    size_t length = filename ? strlen(filename) : 0;
    return length >= 3 && strcmp(filename + length - 3, ".ls") == 0;
}

static Item js_interp_load_es_module(Runtime* runtime, const char* filename) {
    if (!runtime || !filename) return ItemError;
    Item specifier = js_make_string(filename);
    Item existing = js_module_get(specifier);
    if (get_type_id(existing) != LMD_TYPE_NULL) return existing;
    if (js_interp_is_lambda_module_path(filename)) {
        // Publish the loading record directly into the shared registry. This
        // keeps Lambda's compiler and JS linker on one descriptor rather than
        // registering a temporary JS namespace and replacing it afterward.
        runtime->js_runtime_used = true;
        // An outer JS turn can initialize before its static imports are
        // linked. Attach only the Lambda scheduler here; reinitializing the
        // event loop would discard callbacks already queued by that turn.
        js_event_loop_attach_lambda_scheduler();
        ModuleDescriptor* loading = module_register_loading_with_namespace_ops_for_runtime(
            runtime, filename, "lambda", NULL);
        if (!loading) return ItemError;
        Script* lambda_script = load_script_mir_direct(runtime, filename, NULL, true);
        ModuleDescriptor* lambda_module = lambda_script
            ? module_get_for_runtime(runtime, lambda_script->reference) : NULL;
        if (!lambda_module) {
            return js_throw_reference_error(js_make_string("Cannot load Lambda module"));
        }
        return lambda_module->namespace_obj;
    }
    char* source = read_text_file(filename);
    if (!source) return js_throw_reference_error(js_make_string("Cannot find module"));
    Item result = js_interp_execute_es_module_source(runtime, source,
        strlen(source), filename, NULL);
    mem_free(source);
    return result;
}

static Item js_interp_load_static_imports(Runtime* runtime, JsScript* script) {
    JsProgramNode* program = script && script->ast_root &&
            script->ast_root->node_type == AST_SCRIPT
        ? (JsProgramNode*)script->ast_root : NULL;
    for (JsAstNode* statement = program ? (JsAstNode*)program->body : NULL;
            statement; statement = (JsAstNode*)statement->next) {
        String* source = NULL;
        if (statement->node_type == JS_AST_NODE_IMPORT_DECLARATION) {
            source = ((JsImportNode*)statement)->source;
        } else if (statement->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
            source = ((JsExportNode*)statement)->source;
        }
        if (!source) continue;
        char resolved[512];
        jm_resolve_module_path(script->reference, source->chars,
            (int)source->len, resolved, (int)sizeof(resolved));
        Item namespace_obj = js_interp_load_es_module(runtime, resolved);
        if (item_is_error(namespace_obj) || get_type_id(namespace_obj) == LMD_TYPE_NULL) {
            return item_is_error(namespace_obj) ? namespace_obj :
                js_throw_reference_error(js_make_string("imported module is unavailable"));
        }
    }
    return make_js_undefined();
}

typedef struct JsInterpStarExportName {
    String* name;
    String* source_ref;
    struct JsInterpStarExportName* next;
} JsInterpStarExportName;

static Item js_interp_validate_star_exports(Runtime* runtime, JsScript* script) {
    if (!runtime || !script || !script->pool || !script->name_pool) return ItemError;
    JsProgramNode* program = script->ast_root && script->ast_root->node_type == AST_SCRIPT
        ? (JsProgramNode*)script->ast_root : NULL;
    JsInterpStarExportName* seen = NULL;
    RootFrame roots(2);
    Rooted<Item> keys_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    for (JsAstNode* statement = program ? (JsAstNode*)program->body : NULL;
            statement; statement = (JsAstNode*)statement->next) {
        if (statement->node_type != JS_AST_NODE_EXPORT_DECLARATION) continue;
        JsExportNode* exported = (JsExportNode*)statement;
        if (!exported->is_star || exported->is_namespace || !exported->source) continue;
        char resolved[512];
        jm_resolve_module_path(script->reference, exported->source->chars,
            (int)exported->source->len, resolved, (int)sizeof(resolved));
        Item namespace_obj = js_module_get(js_make_string(resolved));
        if (get_type_id(namespace_obj) == LMD_TYPE_NULL) {
            return js_throw_reference_error(js_make_string("re-exported module is unavailable"));
        }
        String* source_ref = name_pool_create_len(script->name_pool, resolved,
            strlen(resolved));
        if (!source_ref) return ItemError;
        keys_root.set(js_object_keys(namespace_obj));
        if (item_is_error(keys_root.get())) return keys_root.get();
        int64_t count = js_array_length(keys_root.get());
        for (int64_t index = 0; index < count; index++) {
            key_root.set(js_elements_get_int(keys_root.get(), index));
            if (item_is_error(key_root.get())) return key_root.get();
            if (get_type_id(key_root.get()) != LMD_TYPE_STRING) continue;
            String* source_name = it2s(key_root.get());
            if (js_interp_name_equals(source_name, "default") ||
                    js_interp_has_explicit_export(script, source_name)) continue;
            String* name = name_pool_create_len(script->name_pool, source_name->chars,
                source_name->len);
            if (!name) return ItemError;
            for (JsInterpStarExportName* prior = seen; prior; prior = prior->next) {
                if (!js_interp_name_matches(prior->name, name)) continue;
                if (!js_interp_name_matches(prior->source_ref, source_ref)) {
                    return js_throw_syntax_error(js_make_string(
                        "ambiguous export-star binding"));
                }
                name = NULL;
                break;
            }
            if (!name) continue;
            JsInterpStarExportName* added = (JsInterpStarExportName*)pool_calloc(
                script->pool, sizeof(JsInterpStarExportName));
            if (!added) return ItemError;
            added->name = name;
            added->source_ref = source_ref;
            added->next = seen;
            seen = added;
        }
    }
    return make_js_undefined();
}

static Item js_interp_instantiate_es_module(Runtime* runtime, JsScript* script) {
    if (!runtime || !script || !script->is_es_module) return ItemError;
    if (script->es_module_scope_initialized) return make_js_undefined();
    JsInterpCurrentFileScope current_file(script->reference);
    int global_slots = js_interp_scope_slot_count(script->global_scope);
    JsInterpModuleStateScope module_state;
    if (!lambda_module_state_prepare(script->module_state_id,
            (uint32_t)(global_slots > 0 ? global_slots : 1)) ||
            !js_set_active_module_state_id(script->module_state_id)) {
        return ItemError;
    }
    JsInterpModuleNamespaceScope module_namespace(script);
    if (!module_namespace.active) return ItemError;
    RootFrame roots(3);
    Rooted<Item> this_root(roots, make_js_undefined());
    Rooted<Item> new_target_root(roots, js_get_new_target());
    Rooted<Item> home_class_root(roots, ItemNull);
    JsInterpFrame frame = {script, NULL, this_root.home(), new_target_root.home(),
        home_class_root.home(), true, NULL, 0, NULL, false, NULL};
    JsInterpCompletion initialized = js_interp_initialize_scope(&frame,
        script->global_scope);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
    // ES declaration instantiation is a one-time state transition. The body
    // reuses these cells so hoisted function identity survives a cycle.
    script->es_module_scope_initialized = true;
    return make_js_undefined();
}

Item js_interp_execute_es_module_script(Runtime* runtime, JsScript* script,
        uint64_t* result_home) {
    (void)result_home;
    if (!runtime || !script) return ItemError;
    script->is_module = true;
    script->is_es_module = true;
    script->strict_mode = true;

    EvalContext* eval = NULL;
    bool reusing_context = false;
    if (!js_prepare_eval_context(runtime, true, &eval, &reusing_context)) return ItemError;
    (void)eval;
    if (runtime->dom_ui_context) js_dom_set_ui_context(runtime->dom_ui_context);
    // Static import linkage may compile a Lambda dependency. Seal the JS
    // parser root first so both languages append runtime names through the
    // canonical dynamic child rather than mutating the frozen static table.
    if (!js_activate_runtime_name_pool()) return ItemError;
    JsInterpExecutionScope execution_scope;
    if (execution_scope.should_initialize_event_loop() &&
            js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_init();
    }
    // Cross-language namespace construction uses the JS property runtime
    // during linking, before the AST body reaches js_interp_execute_script().
    // Give that shared runtime its document/input owner up front.
    Input* input = Input::create(context->pool);
    if (!input) return ItemError;
    js_runtime_set_input(input);
    // Reject before registration or dependency evaluation. A forced AST run
    // may not observe an unsupported unit through a dependency side effect.
    if (!js_interp_script_is_supported(script)) {
        return js_throw_syntax_error(js_make_string("unsupported AST interpreter module"));
    }
    ModuleDescriptor* module = module_get_for_runtime(runtime, script->reference);
    if (module && (module->initialized || module->loading)) return module->namespace_obj;
    module = module_register_loading_with_namespace_ops_for_runtime(runtime,
        script->reference, "js", NULL);
    if (!module) return ItemError;
    module->specifier_item = js_make_string(script->reference);

    RootFrame roots(1);
    Rooted<Item> namespace_root(roots, module->namespace_obj);
    Item instantiated = js_interp_instantiate_es_module(runtime, script);
    if (item_is_error(instantiated)) {
        module->evaluation_error = instantiated;
        module->loading = false;
        return instantiated;
    }
    if (runtime->dom_doc) js_dom_set_document(runtime->dom_doc);
    Item imports = js_interp_load_static_imports(runtime, script);
    if (item_is_error(imports)) {
        module->evaluation_error = imports;
        module->loading = false;
        return imports;
    }
    Item star_validation = js_interp_validate_star_exports(runtime, script);
    if (item_is_error(star_validation)) {
        module->evaluation_error = star_validation;
        module->loading = false;
        return star_validation;
    }
    Item evaluated = js_interp_execute_script(runtime, script, NULL);
    if (item_is_error(evaluated)) {
        module->evaluation_error = evaluated;
        module->loading = false;
        return evaluated;
    }
    module->namespace_obj = namespace_root.get();
    module->initialized = true;
    module->loading = false;
    return namespace_root.get();
}

Item js_interp_execute_es_module_source(Runtime* runtime, const char* source,
        size_t source_length, const char* filename, uint64_t* result_home) {
    JsScript* script = js_interp_prepare_script(runtime, source, source_length,
        filename, true);
    if (!script) return ItemError;
    return js_interp_execute_es_module_script(runtime, script, result_home);
}
