#pragma once

#include "../core/name_identity.h"

#include <stdint.h>

// Active runtime process state. This declaration deliberately lives outside
// frozen lambda.h so consumers of the native/MIR-direct runtime have a
// provider-owned surface.
#ifdef __cplusplus
struct EvalContext;
struct Context;

// The current active evaluator belongs to the runtime process state, not to
// runner.cpp; narrow test fixtures may provide it without linking the runner.
extern __thread EvalContext* context;

// An evaluator thread acquires one context identity before execution and keeps
// it until teardown. Nested callbacks and guest dispatch may validate the
// owner, but must never replace and later restore the TLS pointer.
bool eval_context_thread_initialize(EvalContext* owner);
bool eval_context_thread_matches(const EvalContext* owner);
bool eval_context_thread_shutdown(EvalContext* owner);
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
bool lambda_active_js_module_state_ensure_vars(uint32_t required_var_count);
bool lambda_module_state_bind_static(uint32_t module_id, void* consts,
                                     void* type_list);
Item lambda_name_id_to_item(NameId name_id);
uint64_t lambda_module_name_id_at(void* module_state, uint32_t index);
// MIR-imported helpers obtain their owner from TLS.  `Context*` remains an
// ABI parameter only between generated MIR functions.
Context* eval_context_tls_runtime(void);
void* lambda_module_const_at(const struct LambdaModuleLayout* layout,
                             uint32_t index);
void lambda_module_var_store(void* module_state, uint32_t slot, Item item);
void lambda_module_state_reset(void);
void lambda_module_state_destroy(void);

#ifdef __cplusplus
}
#endif
