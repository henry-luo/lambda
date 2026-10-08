// no_js_stubs.cpp
// Runtime-side boundary of the JS, DOM and Jube modules for the runtime-only
// lambda-cli.exe build (LAMBDA_NO_JS / LAMBDA_NO_JUBE).
//
// Contract: each definition behaves exactly as the real module does when no JS
// realm or Jube module is attached, which is the only state a build without
// those modules can reach. Lifecycle hooks are no-ops, ownership queries report
// "not mine", and entry points that would need a realm log and fail.

#include "../lambda-data.hpp"
#include "../../lib/log.h"

#ifdef LAMBDA_NO_JS

#include "transpiler.hpp"
#include "gc/gc_heap.h"
#include "concurrency_js.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_typed_array.h"
#include "../js/js_event_loop.h"
#include "../js/js_class.h"
#include "../js/js_property_attrs.h"
#include "../dom/dom.h"

static Item no_js_unavailable(const char* entry) {
    log_error("no-js: %s requires the JavaScript runtime, which this build excludes", entry);
    return ItemNull;
}

// --- realm state: never created, so every capsule lookup sees NULL ---

__thread JsRuntimeState* js_active_runtime_state = NULL;
// identity only: no Script ever carries this profile without the JS front end
LangProfile js_profile = { "js", NULL, NULL };
int g_js_force_document_interp = 0;

bool js_runtime_state_init(EvalContext* context) { (void)context; return true; }
bool js_runtime_state_shutdown(EvalContext* context) { (void)context; return true; }
bool js_runtime_state_thread_matches(const EvalContext* context) { (void)context; return false; }
bool js_runtime_context_enter_turn(Runtime* runtime, EvalContext* owner) {
    (void)runtime; (void)owner;
    // document turns require the JS realm excluded by this profile.
    no_js_unavailable("js_runtime_context_enter_turn");
    return false;
}
void js_runtime_state_destroy_context(void) {}
void js_runtime_state_release_heap_resources(void) {}
uint64_t js_get_heap_epoch(void) { return 0; }

// --- lifecycle hooks called by runtime reset/teardown ---

void js_batch_reset(void) {}
void js_intrinsic_state_teardown(void) {}
void js_eval_preamble_cache_reset(void) {}
void js_release_global_var_module_bindings_from(uint32_t first_module_state_id) {
    (void)first_module_state_id;
}
void js_event_loop_shutdown(void) {}
void jm_cleanup_deferred_mir() {}
void js_canvas_cleanup(void) {}
void js_fetch_reset(void) {}
void dom_batch_reset(void) {}
void dom_shutdown(void) {}

// --- GC hooks: only JS-layout objects carry payloads these would visit ---

extern "C" void js_generator_map_gc_trace(Map* map, gc_heap_t* gc) { (void)map; (void)gc; }
extern "C" void js_async_frame_map_gc_trace(Map* map, gc_heap_t* gc) { (void)map; (void)gc; }
extern "C" void js_collection_map_gc_trace(Map* map, gc_heap_t* gc) { (void)map; (void)gc; }
extern "C" void js_iterator_map_gc_trace(Map* map, gc_heap_t* gc) { (void)map; (void)gc; }
extern "C" void js_regex_map_heap_destroy(Map* map, gc_native_seen_t* seen_native) {
    (void)map; (void)seen_native;
}
extern "C" void js_collection_map_heap_destroy(Map* map, gc_native_seen_t* seen_native) {
    (void)map; (void)seen_native;
}
extern "C" void js_generator_map_heap_destroy(Map* map) { (void)map; }
extern "C" void js_async_frame_map_heap_destroy(Map* map) { (void)map; }
// 0 = not a JS function layout; the collector falls through to Lambda tracing
extern "C" int js_function_gc_trace(void* data, gc_heap_t* gc) { (void)data; (void)gc; return 0; }
extern "C" int js_function_gc_compact(void* data, gc_heap_t* gc) { (void)data; (void)gc; return 0; }
extern "C" void js_function_gc_destroy(void* data) { (void)data; }

// --- ownership queries: no Array/Map is ever given a JS-owned payload ---

bool js_array_runtime_items_release(Array* owner) { (void)owner; return false; }
void js_array_runtime_items_cleanup_all(void) {}
bool js_array_immortal_props_store(Array* owner, Map* props) { (void)owner; (void)props; return false; }
void js_array_immortal_props_cleanup_all(void) {}
bool js_is_proxy(Item obj) { (void)obj; return false; }
JsProxyData* js_get_proxy_data(Item obj) { (void)obj; return NULL; }
bool js_is_typed_array(Item val) { (void)val; return false; }
bool js_is_dataview(Item val) { (void)val; return false; }
JsTypedArray* js_get_typed_array_ptr(Map* m) { (void)m; return NULL; }
JsDataView* js_get_dataview_ptr(Item val) { (void)val; return NULL; }
JsArrayBuffer* js_get_arraybuffer_ptr_item(Item val) { (void)val; return NULL; }
void js_arraybuffer_destroy(JsArrayBuffer* ab) { (void)ab; }
Item binary_from_typed_array(JsTypedArray* ta) { (void)ta; return no_js_unavailable("binary_from_typed_array"); }
Item binary_from_dataview(JsDataView* dv) { (void)dv; return no_js_unavailable("binary_from_dataview"); }

// Shape detach needs the realm's Input pool; with no realm the real functions
// also return NULL and the caller keeps its existing rebuild path.
bool js_realm_runtime_has_input(void) { return false; }
extern "C" TypeMap* js_typemap_clone_for_mutation_pub(Item obj) { (void)obj; return NULL; }
TypeMap* js_typemap_transition_for_type(Item obj, ShapeEntry* entry,
        NameId operation_name_id, TypeId value_type) {
    (void)obj; (void)entry; (void)operation_name_id; (void)value_type;
    return NULL;
}

// --- cross-language entry points: unreachable without a JS module ---

Item load_js_module(Runtime* runtime, const char* js_path) {
    (void)runtime;
    log_error("no-js: cannot import '%s'", js_path ? js_path : "");
    return ItemNull;
}
Item js_new_object(void) { return no_js_unavailable("js_new_object"); }
Item js_get_key_default(Item object, Item key) {
    (void)object; (void)key;
    return no_js_unavailable("js_get_key_default");
}
Item js_set_key_default(Item object, Item key, Item value) {
    (void)object; (void)key; (void)value;
    return no_js_unavailable("js_set_key_default");
}
int js_function_get_arity(Item fn_item) { (void)fn_item; return 0; }
void* js_function_get_ptr(Item fn_item) { (void)fn_item; return NULL; }
Item lambda_js_wrap_procedure(Function* function, int arity, const char* name) {
    (void)function; (void)arity; (void)name;
    return no_js_unavailable("lambda_js_wrap_procedure");
}

// JS exports reach Lambda only through an imported JS module, which load_js_module refuses
#define NO_JS_EXPORT_CALL(n, params) \
    Item js_call_export_##n##_into params { \
        (void)function; (void)result_home; \
        return no_js_unavailable("js_call_export_" #n "_into"); \
    }
NO_JS_EXPORT_CALL(0, (Function* function, uint64_t* result_home))
NO_JS_EXPORT_CALL(1, (Function* function, Item, uint64_t* result_home))
NO_JS_EXPORT_CALL(2, (Function* function, Item, Item, uint64_t* result_home))
NO_JS_EXPORT_CALL(3, (Function* function, Item, Item, Item, uint64_t* result_home))
NO_JS_EXPORT_CALL(4, (Function* function, Item, Item, Item, Item, uint64_t* result_home))
NO_JS_EXPORT_CALL(5, (Function* function, Item, Item, Item, Item, Item, uint64_t* result_home))
NO_JS_EXPORT_CALL(6, (Function* function, Item, Item, Item, Item, Item, Item, uint64_t* result_home))
NO_JS_EXPORT_CALL(7, (Function* function, Item, Item, Item, Item, Item, Item, Item, uint64_t* result_home))
NO_JS_EXPORT_CALL(8, (Function* function, Item, Item, Item, Item, Item, Item, Item, Item,
    uint64_t* result_home))
#undef NO_JS_EXPORT_CALL

#endif // LAMBDA_NO_JS

#ifdef LAMBDA_NO_JUBE

#include "../jube/jube_registry.h"
#include "../jube/jube_interface.h"

// --- an empty Jube registry: no static or dynamic host modules exist ---

void jube_register_builtin_modules(void) {}
int jube_static_module_count(void) { return 0; }
const JubeModuleDef* jube_static_module_at(int index) { (void)index; return NULL; }
const JubeModuleDef* jube_find_static_module(const char* name) { (void)name; return NULL; }
const JubeTypeDef* jube_find_type_by_host_type(const void* host_type) { (void)host_type; return NULL; }
void jube_modules_runtime_reset(void) {}
void jube_interface_runtime_reset(void) {}
void jube_notify_heap_cleanup(void* heap) { (void)heap; }
// 0 = receiver is not a declared host object
int jube_member_get(Item receiver, Item key, Item* out) { (void)receiver; (void)key; (void)out; return 0; }
int jube_member_get_js(Item target, Item key, Item receiver, Item* out) { (void)target; (void)receiver; (void)key; (void)out; return 0; }
int jube_member_projected_get(Item receiver, Item key, Item* out) {
    (void)receiver; (void)key; (void)out;
    return 0;
}
int jube_member_native_own_get(Item receiver, Item key, Item* out) {
    (void)receiver; (void)key; (void)out;
    return 0;
}
int jube_member_set(Item receiver, Item key, Item value, Item* out) {
    (void)receiver; (void)key; (void)value; (void)out;
    return 0;
}
int jube_member_set_js(Item target, Item key, Item value, Item receiver, Item* out) { (void)target; (void)receiver; (void)key; (void)value; (void)out; return 0; }
int jube_member_projection_keys(Item receiver, Item* out) { (void)receiver; (void)out; return 0; }

#endif // LAMBDA_NO_JUBE
