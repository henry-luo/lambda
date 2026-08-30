#pragma once

#include "../core/name_identity.h"

#include <stdint.h>

// Active runtime process state. This declaration deliberately lives outside
// frozen lambda.h so consumers of the native/MIR-direct runtime have a
// provider-owned surface.
#ifdef __cplusplus
struct EvalContext;
struct Context;
struct Runtime;

// The current active evaluator belongs to the runtime process state, not to
// runner.cpp; narrow test fixtures may provide it without linking the runner.
extern __thread EvalContext* context;

// An evaluator thread acquires one context identity before execution and keeps
// it until teardown. Nested callbacks and guest dispatch may validate the
// owner, but must never replace and later restore the TLS pointer.
bool eval_context_init(EvalContext* owner);
bool eval_context_matches(const EvalContext* owner);
bool eval_context_shutdown(EvalContext* owner);

// Retained and fresh clients share owner and result-root publication (D8.1.3v10, D5.2.1v3).
bool runtime_context_bind_retained(Runtime* runtime, EvalContext* owner);
void runtime_context_publish_owners(Runtime* runtime, EvalContext* owner);
Item runtime_publish_result(EvalContext* owner, Item result);

// Restore execution metadata at nested runtime boundaries (D8.1.3v10).
class RuntimeCurrentFileScope {
public:
    RuntimeCurrentFileScope(EvalContext* owner = context, const char* filename = NULL);
    RuntimeCurrentFileScope(const RuntimeCurrentFileScope&) = delete;
    RuntimeCurrentFileScope& operator=(const RuntimeCurrentFileScope&) = delete;
    ~RuntimeCurrentFileScope();

private:
    EvalContext* owner_;
    const char* previous_;
};

class RuntimeExecutionScope {
public:
    explicit RuntimeExecutionScope(EvalContext* owner = context);
    RuntimeExecutionScope(const RuntimeExecutionScope&) = delete;
    RuntimeExecutionScope& operator=(const RuntimeExecutionScope&) = delete;
    ~RuntimeExecutionScope();

    bool is_outermost() const { return outermost_; }

private:
    EvalContext* owner_;
    bool outermost_;
};

// Restore a caller's slab across imports and guest callbacks (D7.2.1).
class RuntimeModuleStateScope {
public:
    explicit RuntimeModuleStateScope(EvalContext* owner = context);
    RuntimeModuleStateScope(const RuntimeModuleStateScope&) = delete;
    RuntimeModuleStateScope& operator=(const RuntimeModuleStateScope&) = delete;
    ~RuntimeModuleStateScope();

    bool activate(uint32_t module_id);
    // A document-level unit may retain its newly active slab (D7.2.1).
    void retain_active() { restore_previous_ = false; }

private:
    EvalContext* owner_;
    struct LambdaModuleState* previous_;
    bool restore_previous_;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_dry_run;

// Context-module state is established before a sealed MIR module is entered.
// These lifecycle calls may allocate/register roots; generated code never
// calls them from a repeated variable path.
struct LambdaModuleState;
struct PropertyKeySpec;
typedef struct LambdaModuleStateSnapshot {
    Item* vars;
    uint64_t* payloads;
    uint32_t count;
    bool roots_registered;
} LambdaModuleStateSnapshot;
bool lambda_module_state_prepare(uint32_t module_id, uint32_t var_count);
uint32_t lambda_active_module_state_id(void);
bool lambda_module_state_activate(uint32_t module_id);
bool lambda_module_state_reserve_and_activate(uint32_t var_count);
bool lambda_module_state_copy_var_prefix(uint32_t source_module_id,
                                         uint32_t destination_module_id,
                                         uint32_t count);
// Interpreter REPL modules append bindings after their first entry. This
// grows the precise root range while preserving the existing slot values.
bool lambda_module_state_grow_vars(uint32_t module_id, uint32_t var_count);
bool lambda_module_state_snapshot(uint32_t module_id,
                                  LambdaModuleStateSnapshot* snapshot);
bool lambda_module_state_restore(uint32_t module_id,
                                 const LambdaModuleStateSnapshot* snapshot);
void lambda_module_state_snapshot_dispose(LambdaModuleStateSnapshot* snapshot);
// Retire one dynamically-owned module without disturbing unrelated modules.
// REPL `clear` uses this before discarding its append-only Script.
void lambda_module_state_release(uint32_t module_id);
// Retire every dynamically-owned module state at or after the checkpoint.
// Batch hosts pair this with Runtime script-generation teardown before IDs
// are reused by a fresh heap generation.
void lambda_module_state_release_from(uint32_t first_module_id);
bool lambda_module_state_prepare_layout(const struct LambdaModuleLayout* layout);
bool lambda_module_state_link_property_keys(uint32_t module_id,
    const struct PropertyKeySpec* specs, uint32_t count, uint32_t bytes_size);
// Validate and intern a sealed property-key image without publishing module
// state. The initial JS closure uses this while its static root is still open.
bool lambda_property_key_specs_prelink(const struct PropertyKeySpec* specs,
    uint32_t count, uint32_t bytes_size);
bool lambda_module_state_append_property_keys(uint32_t module_id,
    const struct PropertyKeySpec* specs, uint32_t count, uint32_t bytes_size);
bool lambda_module_state_reserve(uint32_t var_count, uint32_t* out_module_id);
// The active slab belongs to the EvalContext, not to a guest language. Lambda
// and JS use this common growth path and therefore retain identical root-range
// ownership when a dynamic module adds bindings.
bool lambda_active_module_state_ensure_vars(uint32_t required_var_count);
bool lambda_module_state_bind_static(uint32_t module_id, void* consts,
                                     void* type_list);
uint32_t lambda_module_state_property_key_count(uint32_t module_id);
Item lambda_name_id_to_item(NameId name_id);
uint64_t lambda_module_name_id_at(void* module_state, uint32_t index);
uint64_t lambda_active_module_name_id(uint32_t index);
Item lambda_active_module_name_item(uint32_t module_name_index,
                                    NameId direct_name_id);
// MIR-imported helpers obtain their owner from TLS.  `Context*` remains an
// ABI parameter only between generated MIR functions.
Context* eval_context_tls_runtime(void);
void* lambda_module_const_at(const struct LambdaModuleLayout* layout,
                             uint32_t index);
void* lambda_module_const_at_state(void* module_state, uint32_t index);
Item lambda_module_var_at(void* module_state, uint32_t slot);
void lambda_module_var_store(void* module_state, uint32_t slot, Item item);
Item lambda_active_module_var_at(uint32_t slot);
void lambda_active_module_var_store(uint32_t slot, Item item);
void lambda_module_state_reset(void);
void lambda_module_state_destroy(void);

#ifdef __cplusplus
}
#endif
