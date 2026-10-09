#include "jube_registry.h"
#include "../dom/dom_core.h"
#include "../dom/dom_engine.h"
#include "jube_interface.h"
#include "../input/input-script-cache.h"
#include "../runtime/ast.hpp"
#include "../runtime/activation.h"
#include "../runtime/module_registry.h"
#include "../runtime/transpiler.hpp"
#include "../runtime/template_host.h"
#include "../runtime/template_registry.h"
#include "../runtime/template_state.h"
#include "../runtime/radiant_event_hook.h"
#include "../runtime/lambda-root-frame.hpp"
#include "../runtime/heap_api.h"
#include "../runtime/mir_emitter_shared.hpp"
#include "../runtime/mir_dump.h"
#include "../runtime/sys_func_registry.h"
#include "../format/format.h"
#include "../input/css/dom_element.hpp"
#include "../js/js_class.h"
#include "jube_node_permission.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_typed_array.h"
#include "../js/js_state_guards.h"
#include "../js/js_fs_service.h"
#include "jube_node_zlib_codec.hpp"
#include "../js/js_runtime.h"
#include "../module/node_core/node_events.hpp"
#include "../module/node_core/node_trace_events.hpp"
#include "../module/node_core/node_runtime_state.hpp"
#include "../module/node_core/node_url.hpp"
#include "../core/lambda-decimal.hpp"
#include "../runtime/lambda-stack.h"
#include "../runtime/recovery_frame.h"
#include "../runtime/side_stack.h"
#include "../../lib/file.h"
#include "../../lib/digest.h"
#include "../../lib/hex.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/str.h"
#include "../../lib/strbuf.h"
#include "../../lib/mem_factory.h"
#include "../../lib/arraylist.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_typed.hpp"
#include "../../lib/atomic.h"
#include "../../lib/uv_loop.h"
#include "../runtime/gc/gc_heap.h"
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <mir.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <dirent.h>
#endif

#define JUBE_STATIC_MODULE_CAPACITY 64
#define JUBE_MANIFEST_DEPENDENCY_CAPACITY 32
#define JUBE_MANIFEST_PATH_CAPACITY 1024
#define JUBE_SPECIFIER_NAME_CAPACITY 256
#define JUBE_SPECIFIER_MODULE_NAME_CAPACITY 128
#define JUBE_SPECIFIER_CATALOG_CAPACITY 256
#define JUBE_GUEST_EXECUTION_MAGIC 0x4A474558u
#define JUBE_MIR_CURSOR_INDEX_BITS 16u
#define JUBE_MIR_CURSOR_INDEX_MASK ((uintptr_t)((1u << JUBE_MIR_CURSOR_INDEX_BITS) - 1u))
#define JUBE_NODE_CAPSULE_OWNER 0x4A554245u

typedef struct JubeStaticModuleEntry {
    const JubeModuleDef* module;
    uint32_t activation_state;
    // valid only while ACTIVATING: the thread running init, for cycle detection
    uv_thread_t activating_thread;
    ArrayList* attached_sessions;
    void* dynamic_handle;
} JubeStaticModuleEntry;

typedef enum JubeModuleActivationState {
    JUBE_MODULE_REGISTERED = 0,
    JUBE_MODULE_ACTIVATING = 1,
    JUBE_MODULE_ACTIVE = 2,
    JUBE_MODULE_FAILED = 3,
} JubeModuleActivationState;

static JubeStaticModuleEntry jube_static_modules[JUBE_STATIC_MODULE_CAPACITY];
static int jube_static_modules_count = 0;
static bool jube_dynamic_modules_from_env_loaded = false;
static char jube_host_module_root[1024];
// -1 means no bundle profile has been selected yet; 0/1 are immutable for the
// process so repeated registration sites never rescan the module directory.
static int jube_node_core_module_set_enabled = -1;
static char jube_manifest_loading_paths[JUBE_MANIFEST_DEPENDENCY_CAPACITY][JUBE_MANIFEST_PATH_CAPACITY];
static int jube_manifest_loading_depth = 0;

typedef enum JubeSpecifierOwnerState {
    JUBE_SPECIFIER_OWNER_STATIC = 0,
    JUBE_SPECIFIER_OWNER_CATALOGED = 1,
    JUBE_SPECIFIER_OWNER_ACTIVATING = 2,
    JUBE_SPECIFIER_OWNER_ACTIVE = 3,
    JUBE_SPECIFIER_OWNER_FAILED = 4,
} JubeSpecifierOwnerState;

typedef struct JubeSpecifierEntry {
    char normalized[JUBE_SPECIFIER_NAME_CAPACITY];
    char module_name[JUBE_SPECIFIER_MODULE_NAME_CAPACITY];
    char manifest_path[JUBE_MANIFEST_PATH_CAPACITY];
    const JubeModuleDef* module;
    int32_t namespace_index;
    uint32_t state;
} JubeSpecifierEntry;

static HashMap* jube_specifier_index = NULL;
// 0 = not built, 1 = built.  The lock protects HashMap construction while
// parallel lowering threads perform read-only catalog queries.
static atomic_int32 jube_specifier_catalog_state = {};
static atomic_int32 jube_specifier_catalog_lock = {};
static bool jube_specifier_catalog_failed = false;

static int jube_specifier_index_module(const JubeModuleDef* module);
static bool jube_specifier_catalog_ensure(void);
static int jube_load_manifest_path_internal(const char* manifest_path,
                                            const char* expected_name);
static bool jube_activate_module_descriptor(const JubeModuleDef* module);
static const JubeModuleDef* jube_module_for_host_type(const void* host_type);

struct NodeRuntimeSession {
    uint64_t generation;
    bool live;
    bool detaching;
    NodeTraceState* trace;
    JsCjsState cjs;
    JsCommonJsCompileCacheState* commonjs_compile_cache;
    JsDiagnosticsChannelState* diagnostics_channels;
    JsPermissionPolicy* permission_policy;
    JsCryptoNativeState crypto_native;
};

static const ContextCapsuleOps jube_node_session_capsule_ops = {
    "jube-node-session", CONTEXT_CAPSULE_LIFETIME_CONTEXT, 0, NULL, NULL, NULL
};

// Keep retired tokens until process cleanup so a stale module-held opaque
// pointer can never become a newly attached runtime by allocator reuse.
static ArrayList* jube_node_runtime_sessions = NULL;
static uint64_t jube_node_runtime_generation = 0;
static uv_once_t jube_runtime_session_lock_once = UV_ONCE_INIT;
static uv_mutex_t jube_runtime_session_lock;
static uv_cond_t jube_runtime_activation_cond;

static void jube_runtime_session_lock_init(void) {
    uv_mutex_init(&jube_runtime_session_lock);
    uv_cond_init(&jube_runtime_activation_cond);
}

static void jube_runtime_session_lock_acquire(void) {
    uv_once(&jube_runtime_session_lock_once, jube_runtime_session_lock_init);
    uv_mutex_lock(&jube_runtime_session_lock);
}

static void jube_runtime_session_lock_release(void) {
    uv_mutex_unlock(&jube_runtime_session_lock);
}

static NodeRuntimeSession* jube_active_node_runtime_session_current(void) {
    return context ? (NodeRuntimeSession*)context_capsule((EvalContext*)context,
        CONTEXT_CAPSULE_NODE_RUNTIME) : NULL;
}

static bool jube_active_node_runtime_session_set(NodeRuntimeSession* session) {
    EvalContext* owner = (EvalContext*)context;
    if (!owner) return session == NULL;
    if (!session) {
        context_capsule_take(owner, CONTEXT_CAPSULE_NODE_RUNTIME);
        return true;
    }
    return context_capsule_install(owner, CONTEXT_CAPSULE_NODE_RUNTIME, session,
        &jube_node_session_capsule_ops);
}

#define jube_active_node_runtime_session (jube_active_node_runtime_session_current())

void* jube_node_session_module_state_get(void* session_handle, uint32_t slot, size_t size) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (!session || session != jube_active_node_runtime_session || !session->live || size == 0) {
        return NULL;
    }
    jube_runtime_session_lock_acquire();
    void* state = context_capsule_extension((EvalContext*)context, JUBE_NODE_CAPSULE_OWNER, slot);
    if (!state) {
        // Module attach is the only allocation point. Runtime dispatch later
        // reads this context-directory entry without locking (JSCU27).
        state = mem_calloc(1, size, MEM_CAT_SYSTEM);
        if (state && !context_capsule_extension_install((EvalContext*)context,
                JUBE_NODE_CAPSULE_OWNER, slot, state, &jube_node_session_capsule_ops)) {
            mem_free(state);
            state = NULL;
        }
    }
    jube_runtime_session_lock_release();
    return state;
}

void* jube_node_current_module_state(uint32_t slot) {
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    return session && session->live ? context_capsule_extension((EvalContext*)context,
        JUBE_NODE_CAPSULE_OWNER, slot) : NULL;
}

static void jube_node_session_module_states_destroy(NodeRuntimeSession* session) {
    if (session && session == jube_active_node_runtime_session) {
        context_capsule_extensions_drop_owner((EvalContext*)context, JUBE_NODE_CAPSULE_OWNER);
    }
}

static void jube_node_session_state_init(NodeRuntimeSession* session) {
    if (!session) return;
    // owner NULL: the session is used only on its attaching thread, so the
    // vector resolves the current context at each use (JSCU14).
    root_vector_init(&session->cjs.module_stack, NULL, "CommonJS module stack");
}

static void jube_node_session_state_clear(NodeRuntimeSession* session) {
    if (!session) return;
    // release the rooted blocks before the record is zeroed
    root_vector_destroy(&session->cjs.module_stack);
    memset(&session->cjs, 0, sizeof(session->cjs));
    if (session->commonjs_compile_cache) {
        mem_free(session->commonjs_compile_cache->directory);
        mem_free(session->commonjs_compile_cache);
        session->commonjs_compile_cache = NULL;
    }
    if (session->diagnostics_channels) {
        root_vector_unbind_external(session->diagnostics_channels);
        mem_free(session->diagnostics_channels);
        session->diagnostics_channels = NULL;
    }
    if (session->permission_policy) {
        js_permission_policy_destroy(session->permission_policy);
        mem_free(session->permission_policy);
        session->permission_policy = NULL;
    }
    if (session->trace) {
        node_trace_events_runtime_detach(session);
        mem_free(session->trace);
        session->trace = NULL;
    }
    memset(&session->crypto_native, 0, sizeof(session->crypto_native));
}

struct JubeNodeAsyncWork {
    uv_work_t request;
    void* session;
    JubeAsyncWorkCallback work;
    JubeAsyncCompletionCallback complete;
    JubeAsyncDestroyCallback destroy;
    void* user;
    uint32_t resource_id;
    bool queued;
};

extern __thread EvalContext* context;
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);
extern "C" void heap_register_gc_weak(uint64_t* slot,
    JubeGcWeakClearFn on_clear, void* context);
extern "C" void heap_unregister_gc_weak(uint64_t* slot);
extern "C" void* import_resolver(const char* name);

static bool jube_host_root_frame_begin(LambdaRootFrame* frame,
        size_t slot_count) {
    return lambda_root_frame_begin(frame, slot_count);
}

static uint64_t* jube_host_root_frame_take_slot(LambdaRootFrame* frame) {
    return lambda_root_frame_take_slot(frame);
}

static void jube_host_root_frame_end(LambdaRootFrame* frame) {
    lambda_root_frame_end(frame);
}

static LambdaRootFrame* jube_host_opaque_root_frame(JubeRootFrame* frame) {
    static_assert(sizeof(JubeRootFrame) >= sizeof(LambdaRootFrame),
        "JubeRootFrame storage must contain the host root frame");
    return frame ? (LambdaRootFrame*)frame->storage : NULL;
}

static bool jube_host_opaque_root_frame_begin(JubeRootFrame* frame,
        size_t slot_count) {
    LambdaRootFrame* host_frame = jube_host_opaque_root_frame(frame);
    if (!host_frame) return false;
    // Reused guest stack storage must not retain a prior activation watermark.
    memset(host_frame, 0, sizeof(*host_frame));
    return lambda_root_frame_begin(host_frame, slot_count);
}

static uint64_t* jube_host_opaque_root_frame_take_slot(JubeRootFrame* frame) {
    LambdaRootFrame* host_frame = jube_host_opaque_root_frame(frame);
    return host_frame ? lambda_root_frame_take_slot(host_frame) : NULL;
}

static void jube_host_opaque_root_frame_end(JubeRootFrame* frame) {
    LambdaRootFrame* host_frame = jube_host_opaque_root_frame(frame);
    if (host_frame) lambda_root_frame_end(host_frame);
}

static int jube_host_opaque_persistent_root_register(void* session, uint64_t* slot);
static int jube_host_opaque_persistent_root_unregister(void* session, uint64_t* slot);
static void* jube_host_node_current_session(void);
static bool jube_host_node_session_is_live(void* session);
static Item jube_host_node_session_global_this(void* session);
static Item jube_host_node_session_process(void* session);
static Item jube_host_node_current_this(void* session);
static Item jube_host_node_session_active_resources_info(void* session);
static Item jube_host_node_session_active_handles(void* session);
static int jube_host_node_resolve_namespace(void* session, const char* specifier,
                                            Item* out_namespace);
static int jube_host_node_resolve_host_namespace(void* session, const char* specifier,
                                                 Item* out_namespace);
extern "C" Item js_get_node_module_namespace(void);
extern "C" int js_permission_has_net(void);
extern "C" int js_permission_enabled(void);
extern "C" Item js_process_permission_has(Item scope, Item resource);
extern "C" Item js_process_permission_drop(Item scope, Item resource);
extern "C" Item js_permission_make_net_error(const char* syscall, const char* resource);
static Item jube_host_node_throw_type_error_code(void* session, const char* code,
                                                  const char* message);
static Item jube_host_node_throw_range_error_code(void* session, const char* code,
                                                   const char* message);
static Item jube_host_node_throw_zlib_error(void* session, const char* method, int status);
static Item jube_host_node_throw_system_error(void* session, const char* syscall,
                                              int error_number);
static Item jube_host_node_throw_error_code(void* session, const char* code,
                                            const char* message);
static Item jube_host_node_throw_network_error(void* session, int status, const char* syscall,
                                               const char* address, int port);
extern "C" Item js_node_throw_system_error(const char* syscall, int error_number);
extern "C" Item js_throw_error_with_code(const char* code, const char* message);
static int jube_host_node_work_submit(void* session, JubeAsyncWorkCallback work,
                                      JubeAsyncCompletionCallback complete,
                                      JubeAsyncDestroyCallback destroy, void* user,
                                      uint32_t* out_request_id);
static int jube_host_node_work_cancel(void* session, uint32_t request_id);
static int jube_host_node_work_submit_root_span(void* session, int resource_kind,
                                                const Item* root_values, int root_count,
                                                JubeAsyncWorkCallback work,
                                                JubeAsyncCompletionCallback complete,
                                                JubeAsyncDestroyCallback destroy, void* user,
                                                uint32_t* out_resource_id);
static Item jube_host_node_work_resource_value(void* session,
                                               uint32_t resource_id, int root_index);
static void jube_host_node_next_tick_callback(void* session, Item callback, Item error, Item result);
static Item jube_host_node_emit_callback(Item env_item);
static bool jube_host_node_is_typed_array(Item value);
static uint8_t* jube_host_node_typed_array_data(Item value);
static int jube_host_node_typed_array_length(Item value);
static Item jube_host_node_buffer_from_bytes(const uint8_t* data, int length);
static Item jube_host_node_buffer_alloc(int length);
static uint32_t jube_host_node_zlib_crc32(const uint8_t* data, int length, uint32_t seed);
static bool jube_host_node_zlib_codec(enum JubeNodeZlibCodecMode mode, const uint8_t* data,
                                      int length, JubeNodeZlibResult* out_result);
static void jube_host_node_zlib_result_release(JubeNodeZlibResult* result);
static bool jube_host_node_zlib_stream_init(enum JubeNodeZlibCodecMode mode, int window_bits,
                                            int level, int mem_level, int strategy,
                                            void** out_state, int* out_status);
static bool jube_host_node_zlib_stream_run(void* state, const uint8_t* data, int length,
                                           int flush, JubeNodeZlibResult* out_result);
static void jube_host_node_zlib_stream_free(void* state);
static uint8_t* jube_host_node_buffer_prepare_write(Item value);
static bool jube_host_node_is_buffer(Item value);
static int jube_host_node_describe_binary_view(Item value, JubeBinaryView* out_view);
static Item jube_host_node_buffer_view(Item array_buffer, int byte_offset, int byte_length);
static Item jube_host_node_events_als_capture_context(void);
static Item jube_host_node_events_als_context_call(Item context, Item callback, Item this_val,
                                                    Item arg1, int64_t has_arg);
static int jube_host_node_events_domain_emit_current_error(Item error);
static Item jube_host_node_events_process_emit_warning(Item warning, Item type_item,
                                                        Item code_item);
static bool jube_host_node_events_is_error_like(Item value);
static Item jube_host_node_events_domain_current(void);
static Item jube_host_node_events_domain_call(Item domain, Item callback, Item this_val,
                                              Item* args, int arg_count);
static bool jube_host_node_permission_has_fs_read(const char* path);
static bool jube_host_node_permission_has_fs_write(const char* path);
static Item jube_host_node_permission_check_fs_read(const char* path);
static Item jube_host_node_permission_check_fs_write(const char* path);
static bool jube_host_node_permission_enabled(void);
extern "C" Item js_domain_get_current(void);
extern "C" Item js_domain_call_function(Item domain, Item fn, Item this_val,
                                         Item* args, int arg_count);
extern "C" Item js_message_channel_new(void);
extern "C" Item js_message_port_new(void);
extern "C" Item js_message_port_move_to_context(Item port, Item context);
extern "C" Item js_message_port_receive_message_on_port(Item port);
extern "C" Item js_worker_mark_as_untransferable(Item value);
extern "C" Item js_worker_is_marked_as_untransferable(Item value);
static Item jube_host_value_array_set(Item array, int64_t index, Item value);
static Item jube_host_script_current_this(void);
static Item jube_host_script_strict_equal(Item left, Item right);
static Item jube_host_script_new_object_with_class(int class_id);
static bool jube_host_script_class_is(Item object, int class_id);
static int jube_host_value_kind(Item value);
static bool jube_host_value_string_copy(Item value, char* out, size_t out_size,
                                        size_t* out_length);
static Item jube_host_value_string_from_utf8_n(const char* text, size_t length);
static size_t jube_host_value_string_length(Item value);
static const uint8_t* jube_host_value_string_bytes(Item value);
static bool jube_host_value_number_to_int64_exact(Item value, int64_t* out_value);
static Item jube_host_value_native_object_new(const JubeTypeDef* type, void* payload);
static void* jube_host_value_native_object_data(Item object, const JubeTypeDef* type);
static Item jube_host_value_property_set_own(Item object, Item key, Item value);
static bool jube_host_value_property_has_own(Item object, Item key);
static bool jube_host_value_property_get_own_data(Item object, Item key, Item* out_value);
static bool jube_host_value_is_array(Item value);
static Item jube_host_script_new_function(JubeNativeFunctionSpec spec);
extern "C" Item vmap_new(void);
extern "C" Item js_new_object(void);
extern "C" Item js_array_new(int capacity);
extern "C" Item js_array_push(Item array, Item value);
extern "C" int64_t js_array_length(Item array);
extern "C" Item js_elements_get_int(Item array, int64_t index);
extern "C" Item js_get_key_default(Item object, Item key);
extern "C" Item js_set_key_default(Item object, Item key, Item value);
extern "C" Item js_has_own_property(Item object, Item key);
extern "C" Item js_map_shape_lookup_ext(Map* map, const char* key, int key_length, bool* out_found);
extern "C" Item js_make_string_len(const char* str, int len);
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" void js_set_function_name(Item fn_item, Item name_item);
extern "C" void js_mark_non_enumerable(Item object, Item name);
extern "C" Item js_get_global_this(void);
extern "C" Item js_get_global_property(Item key);
extern "C" Item js_get_current_this(void);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" Item js_throw_type_error_code(const char* code, const char* message);
extern "C" Item js_throw_range_error_code(const char* code, const char* message);
extern "C" Item js_throw_uri_error_code(const char* code, const char* message);
extern "C" Item js_throw_value(Item error);
extern "C" Item js_reflect_own_keys(Item obj);
extern "C" Item js_object_keys(Item obj);
extern "C" Item js_reflect_delete_property(Item obj, Item key);
extern "C" Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
extern "C" Item js_error_lane_payload(Item lane);
extern "C" void js_mark_non_writable(Item object, Item name);
extern "C" Item js_object_freeze(Item obj);
extern "C" bool js_is_truthy(Item value);
extern "C" Item js_get_intrinsic_prototype_for_class(int class_id);
Item js_make_number(double value);
double js_get_number(Item value);
extern "C" Item js_date_new_from(Item value);
extern "C" Item js_date_method(Item date, int method_id);
extern "C" Item js_to_string(Item value);
extern "C" Item js_typeof(Item value);
extern "C" Item js_object_create(Item prototype);
extern "C" Item* js_alloc_env(int count);
extern "C" Item js_get_prototype(Item object);
extern "C" void js_set_prototype(Item object, Item prototype);
extern "C" Item js_promise_with_resolvers(void);
extern "C" void js_next_tick_enqueue(Item callback);
extern "C" Item js_als_capture_context(void);
extern "C" Item js_als_context_call(Item context, Item callback, Item this_val,
                                     Item arg1, int64_t has_arg);
extern "C" int js_domain_emit_current_error(Item error);
extern "C" Item js_process_emitWarning(Item warning, Item type_item, Item code_item);
extern "C" void js_mark_own_proto_property(Item object);

static Item jube_host_script_bigint_from_decimal(const char* text, size_t length) {
    if (!text || length > (size_t)INT_MAX) return ItemError;
    return bigint_from_string(text, (int)length);
}

static int jube_script_class_id(Item value) {
    return (int)js_class_id(value);
}
extern "C" void* dom_get_document(void);
extern "C" void* dom_get_ui_context(void);
extern "C" bool dom_has_committed_geometry_snapshot(void* doc);
extern "C" Item js_get_document_object_value(void);
extern "C" void* dom_get_or_create_doc_node(void* doc);
extern "C" Item dom_document_proxy_for_doc_bridge(void* doc);
extern "C" void* dom_unwrap_element_impl(Item item);
extern "C" void dom_initialize_node_wrapper(void* dom_elem);
extern "C" bool dom_is_inline_style_item(Item item);
extern "C" bool dom_is_computed_style_item(Item item);
extern "C" bool dom_is_stylesheet(Item item);
extern "C" bool dom_is_css_rule(Item item);
extern "C" bool dom_is_rule_style_decl(Item item);
extern "C" Item dom_get_property_impl(Item elem_item, Item prop_name);
extern "C" Item dom_set_property_impl(Item elem_item, Item prop_name, Item value);
extern "C" Item dom_element_operation_impl(Item elem_item,
                                                JubeDomElementOperation operation,
                                                Item* args, int argc);
extern "C" Item dom_create_tree_walker_bridge(Item root, Item what_to_show);
extern "C" Item dom_document_create_event_bridge(Item interface_name);
extern "C" Item dom_document_exec_command_bridge(Item command, Item value);
extern "C" Item dom_computed_style_get_property(Item style_item, Item prop_name);
extern "C" Item dom_get_prototype_value(Item obj);
extern "C" Item dom_cssom_rule_decl_get_property(Item decl_item, Item prop_name);
extern "C" Item dom_cssom_rule_decl_set_property(Item decl_item, Item prop_name, Item value);
extern "C" void* dom_get_foreign_doc(Item item);
extern "C" void* dom_swap_active_document(void* new_doc);
extern "C" void dom_restore_active_document(void* prev_doc);
extern "C" Item dom_document_proxy_get_property(Item prop_name);
extern "C" Item dom_document_proxy_set_property(Item prop_name, Item value);
extern "C" Item dom_range_get_prototype_value(void);
extern "C" Item dom_selection_get_prototype_value(void);
extern "C" bool dom_expando_has_property(Item obj, Item key);
extern "C" Item dom_expando_get_own_property_descriptor(Item obj, Item key);
extern "C" Item dom_expando_delete_property(Item obj, Item key);
extern "C" Item dom_expando_own_property_names(Item obj);
extern "C" Item dom_owner_document_for_node(void* node);
extern "C" const char* dom_to_attribute_cstr(Item value);
extern "C" void dom_after_set_attribute(void* elem, const char* attr_name, const char* attr_value);
extern "C" void dom_after_remove_attribute(void* elem, const char* attr_name);
extern "C" void dom_after_toggle_attribute_remove(void* elem, const char* attr_name);
extern "C" void dom_after_disabled_attribute_set(void* elem);
extern "C" void dom_after_default_checked_set(void* elem, bool checked);
extern "C" void dom_after_default_selected_set(void* elem, bool selected);
extern "C" void dom_after_select_multiple_removed(void* elem);
extern "C" void dom_set_checked_dirty(void* elem, bool checked);
extern "C" void dom_select_set_value_bridge(void* elem, const char* value);
extern "C" void dom_select_set_selected_index_bridge(void* elem, Item value);
extern "C" void dom_select_set_length_bridge(void* elem, Item value);
extern "C" void dom_set_option_selected_dirty(void* elem, bool selected);
extern "C" void dom_set_option_text_bridge(void* elem, const char* value);
extern "C" void dom_after_srcdoc_set(void* elem);
extern "C" Item dom_throw_contenteditable_syntax_error(void);
extern "C" Item dom_set_text_data_property(void* text, Item value);
extern "C" Item dom_text_control_set_value_bridge(void* elem, Item value);
extern "C" Item dom_text_control_set_selection_start_bridge(void* elem, Item value);
extern "C" Item dom_text_control_set_selection_end_bridge(void* elem, Item value);
extern "C" Item dom_text_control_set_selection_direction_bridge(void* elem, Item value);
extern "C" Item dom_text_control_set_default_value_bridge(void* elem, Item value);
extern "C" Item dom_text_control_set_selection_range_bridge(void* elem, Item start, Item end, Item dir);
extern "C" Item dom_text_control_set_range_text_bridge(void* elem, Item replacement, Item start,
                                                          Item end, Item mode);
extern "C" Item dom_text_control_select_bridge(void* elem);
extern "C" Item dom_form_reset_bridge(Item form_item);
extern "C" Item dom_check_validity_bridge(Item elem_item);
extern "C" Item dom_report_validity_bridge(Item elem_item);
extern "C" Item dom_form_submit_bridge(Item form_item);
extern "C" Item dom_form_request_submit_bridge(Item form_item, Item submitter);
extern "C" Item dom_focus_method_bridge(void* elem, bool focus);
extern "C" Item dom_click_method_bridge(Item elem_item);
extern "C" Item dom_add_event_listener_bridge(Item target_item, Item type, Item callback, Item opts);
extern "C" Item dom_remove_event_listener_bridge(Item target_item, Item type, Item callback, Item opts);
extern "C" Item dom_dispatch_event_bridge(Item target_item, Item event_item);
extern "C" Item dom_get_bounding_client_rect_bridge(void* elem);
extern "C" Item dom_get_client_rects_bridge(void* elem);
extern "C" Item dom_scroll_into_view_bridge(void* elem);
extern "C" Item dom_scroll_operation_bridge(Item elem_item,
                                                 JubeDomElementOperation operation,
                                                 Item* args, int argc);
extern "C" Item dom_text_control_caret_bounds_bridge(void* elem);
extern "C" Item dom_text_control_boundary_from_point_bridge(void* elem, Item x, Item y);
extern "C" Item dom_boundary_from_point_bridge(void* elem, Item x, Item y, Item behavior);
extern "C" Item dom_style_set_property_bridge(void* elem, Item prop, Item value,
                                                 Item priority, bool has_priority);
extern "C" Item dom_style_remove_property_bridge(void* elem, Item prop);
extern "C" Item dom_text_replace_data_bridge(void* text, Item offset, Item count, Item data);
extern "C" Item dom_text_insert_data_bridge(void* text, Item offset, Item data);
extern "C" Item dom_text_append_data_bridge(void* text, Item data);
extern "C" Item dom_text_delete_data_bridge(void* text, Item offset, Item count);
extern "C" Item dom_text_substring_data_bridge(void* text, Item offset, Item count);
extern "C" Item dom_append_child_bridge(void* parent, Item child);
extern "C" Item dom_remove_child_bridge(void* parent, Item child);
extern "C" Item dom_insert_before_bridge(void* parent, Item new_child, Item ref_child);
extern "C" Item dom_remove_bridge(void* node);
extern "C" Item dom_adopt_node_bridge(Item node);
extern "C" Item dom_location_navigate_bridge(void* doc, Item next_url,
                                                  bool replace);
extern "C" Item dom_document_open_bridge(void* doc);
extern "C" Item dom_document_write_bridge(void* doc, Item text);
extern "C" Item dom_document_element_from_point_bridge(void* doc, Item x, Item y);
extern "C" Item dom_create_range(void);
extern "C" Item dom_get_selection(void);
extern "C" Item dom_get_selection_function_for_document(void* doc);
extern "C" bool dom_doc_has_browsing_context(void* doc);
extern "C" Item dom_document_fonts_bridge(void);
extern "C" Item dom_document_stylesheets_bridge(void);
extern "C" Item dom_document_default_view_bridge(void* doc);
extern "C" Item dom_document_implementation_bridge(void);
extern "C" Item dom_document_design_mode_bridge(void);
extern "C" Item dom_document_active_element_bridge(void* doc);
extern "C" Item dom_normalize_bridge(void* elem);
extern "C" Item dom_live_child_collection_bridge(void* elem, bool elements_only);
extern "C" Item dom_attribute_collection_bridge(void* elem);
extern "C" Item dom_token_list_operation(Item receiver, int operation,
                                            Item* args, int argc);
extern "C" Item dom_live_document_forms_bridge(void* doc);
extern "C" Item dom_live_form_elements_bridge(void* elem);
extern "C" Item dom_live_document_get_elements_by_tag_name_bridge(void* doc, Item query);
extern "C" Item dom_live_document_get_elements_by_class_name_bridge(void* doc, Item query);
extern "C" Item dom_live_document_get_elements_by_name_bridge(void* doc, Item query);
extern "C" Item dom_live_element_get_elements_by_tag_name_bridge(void* elem, Item query);
extern "C" Item dom_live_element_get_elements_by_class_name_bridge(void* elem, Item query);
extern "C" Item dom_clone_node_bridge(void* elem, Item deep, bool has_deep);
extern "C" Item dom_import_node_bridge(void* doc, void* elem, bool deep);
extern "C" Item dom_replace_child_bridge(void* parent, Item new_child, Item old_child);
extern "C" Item dom_replace_with_bridge(void* node, Item* args, int argc);
extern "C" Item dom_insert_adjacent_element_bridge(void* elem, Item position, Item new_node);
extern "C" Item dom_insert_adjacent_html_bridge(void* elem, Item position, Item html);
extern "C" Item dom_append_variadic_bridge(void* elem, Item* args, int argc);
extern "C" Item dom_prepend_variadic_bridge(void* elem, Item* args, int argc);
// DOM3 Phase 2: receiver-explicit CSSOM behavior entries
extern "C" Item dom_cssom_stylesheet_get_css_rules(Item sheet);
extern "C" Item dom_cssom_stylesheet_get_length(Item sheet);
extern "C" Item dom_cssom_stylesheet_get_disabled(Item sheet);
extern "C" Item dom_cssom_stylesheet_get_type(Item sheet);
extern "C" Item dom_cssom_stylesheet_get_href(Item sheet);
extern "C" Item dom_cssom_stylesheet_get_title(Item sheet);
extern "C" Item dom_cssom_stylesheet_index(Item sheet, int64_t index);
extern "C" Item dom_cssom_insert_rule(Item sheet, Item text, Item index);
extern "C" Item dom_cssom_delete_rule(Item sheet, Item index);
extern "C" Item dom_cssom_rule_get_selector_text(Item rule);
extern "C" Item dom_cssom_rule_set_selector_text(Item rule, Item value);
extern "C" Item dom_cssom_rule_get_style(Item rule);
extern "C" Item dom_cssom_rule_get_css_rules(Item rule);
extern "C" Item dom_cssom_rule_get_css_text(Item rule);
extern "C" Item dom_cssom_rule_get_type(Item rule);
extern "C" Item dom_cssom_rule_get_parent_rule(Item rule);
extern "C" Item dom_cssom_rule_decl_remove_property(Item decl, Item prop);
extern "C" Item dom_cssom_decl_css_has(Item decl, Item prop);
// DOM3 Phase 3: style-host behavior entries
extern "C" Item dom_get_style_property(Item elem_item, Item prop_name);
extern "C" Item dom_set_style_property(Item elem_item, Item prop_name, Item value);
extern "C" Item dom_style_css_has(Item style_item, Item prop_name);
// DOM3 Phase 1: receiver-explicit Range/Selection behavior entries
extern "C" Item js_range_get_start_container(Item self);
extern "C" Item js_range_get_start_offset(Item self);
extern "C" Item js_range_get_end_container(Item self);
extern "C" Item js_range_get_end_offset(Item self);
extern "C" Item js_range_get_collapsed(Item self);
extern "C" Item js_range_get_common_ancestor(Item self);
extern "C" Item js_range_set_start(Item self, Item node, Item offset);
extern "C" Item js_range_set_end(Item self, Item node, Item offset);
extern "C" Item js_range_set_start_before(Item self, Item node);
extern "C" Item js_range_set_start_after(Item self, Item node);
extern "C" Item js_range_set_end_before(Item self, Item node);
extern "C" Item js_range_set_end_after(Item self, Item node);
extern "C" Item js_range_collapse(Item self, Item to_start);
extern "C" Item js_range_select_node(Item self, Item node);
extern "C" Item js_range_select_node_contents(Item self, Item node);
extern "C" Item js_range_clone_range(Item self);
extern "C" Item js_range_compare_boundary_points(Item self, Item how, Item other);
extern "C" Item js_range_compare_point(Item self, Item node, Item offset);
extern "C" Item js_range_is_point_in_range(Item self, Item node, Item offset);
extern "C" Item js_range_intersects_node(Item self, Item node);
extern "C" Item js_range_detach(Item self);
extern "C" Item js_range_to_string(Item self);
extern "C" Item js_range_get_client_rects(Item self);
extern "C" Item js_range_get_bounding_client_rect(Item self);
extern "C" Item js_range_delete_contents(Item self);
extern "C" Item js_range_extract_contents(Item self);
extern "C" Item js_range_clone_contents(Item self);
extern "C" Item js_range_insert_node(Item self, Item node);
extern "C" Item js_range_surround_contents(Item self, Item node);
extern "C" Item js_selection_get_anchor_node(Item self);
extern "C" Item js_selection_get_anchor_offset(Item self);
extern "C" Item js_selection_get_focus_node(Item self);
extern "C" Item js_selection_get_focus_offset(Item self);
extern "C" Item js_selection_get_is_collapsed(Item self);
extern "C" Item js_selection_get_range_count(Item self);
extern "C" Item js_selection_get_type(Item self);
extern "C" Item js_selection_get_direction(Item self);
extern "C" Item js_selection_get_range_at(Item self, Item index);
extern "C" Item js_selection_add_range(Item self, Item range);
extern "C" Item js_selection_remove_range(Item self, Item range);
extern "C" Item js_selection_remove_all_ranges(Item self);
extern "C" Item js_selection_empty(Item self);
extern "C" Item js_selection_collapse(Item self, Item node, Item offset);
extern "C" Item js_selection_set_position(Item self, Item node, Item offset);
extern "C" Item js_selection_collapse_to_start(Item self);
extern "C" Item js_selection_collapse_to_end(Item self);
extern "C" Item js_selection_extend(Item self, Item node, Item offset);
extern "C" Item js_selection_set_base_and_extent(Item self, Item anchor_node, Item anchor_offset, Item focus_node, Item focus_offset);
extern "C" Item js_selection_select_all_children(Item self, Item node);
extern "C" Item js_selection_contains_node(Item self, Item node, Item allow_partial);
extern "C" Item js_selection_delete_from_document(Item self);
extern "C" Item js_selection_to_string(Item self);
extern "C" Item js_selection_modify(Item self, Item alter, Item direction, Item granularity);
extern "C" Item js_selection_force_direction(Item self, Item direction);
extern "C" void dom_notify_mutation(DomJsMutationKind kind, void* target, void* parent);
extern "C" void dom_notify_mutation_detail(DomJsMutationKind kind, void* target,
                                                void* parent, const char* attribute_name,
                                                const char* old_value);
extern "C" Item js_setTimeout_promise(Item delay, Item value, Item options);
extern "C" Item js_setImmediate_promise(Item value, Item options);
extern "C" Item js_setInterval(Item callback, Item delay);
extern "C" Item js_scheduler_wait(Item delay, Item options);
extern "C" Item js_scheduler_yield(void);
extern "C" Item js_setTimeout(Item callback, Item delay);
extern "C" void js_clearTimeout(Item timer);
extern "C" void js_clearInterval(Item timer);
extern "C" Item js_setImmediate(Item callback);
extern "C" void js_timer_install_promisify_custom(Item function);

static Item jube_host_node_promisify_symbol(const char* name) {
    return js_symbol_for(js_name_item(name, strlen(name)));
}

static void jube_host_node_function_install_promisify_custom(Item function,
        JubeNativeFunctionSpec spec) {
    JubeRootFrame frame = {};
    if (!jube_host_opaque_root_frame_begin(&frame, 3)) return;
    uint64_t* function_root = jube_host_opaque_root_frame_take_slot(&frame);
    uint64_t* symbol_root = jube_host_opaque_root_frame_take_slot(&frame);
    uint64_t* custom_root = jube_host_opaque_root_frame_take_slot(&frame);
    if (!function_root || !symbol_root || !custom_root) {
        jube_host_opaque_root_frame_end(&frame);
        return;
    }
    *function_root = function.item;
    Item symbol = jube_host_node_promisify_symbol("nodejs.util.promisify.custom");
    *symbol_root = symbol.item;
    Item custom = jube_host_script_new_function(spec);
    *custom_root = custom.item;
    // The module supplies behavior while the host owns shared symbol identity.
    js_set_key_default((Item){.item = *function_root}, (Item){.item = *symbol_root},
        (Item){.item = *custom_root});
    jube_host_opaque_root_frame_end(&frame);
}

static void jube_host_node_function_install_promisify_args(Item function, const char* first,
                                                            const char* second) {
    if (!first) return;
    JubeRootFrame frame = {};
    if (!jube_host_opaque_root_frame_begin(&frame, 4)) return;
    uint64_t* function_root = jube_host_opaque_root_frame_take_slot(&frame);
    uint64_t* symbol_root = jube_host_opaque_root_frame_take_slot(&frame);
    uint64_t* names_root = jube_host_opaque_root_frame_take_slot(&frame);
    uint64_t* name_root = jube_host_opaque_root_frame_take_slot(&frame);
    if (!function_root || !symbol_root || !names_root || !name_root) {
        jube_host_opaque_root_frame_end(&frame);
        return;
    }
    *function_root = function.item;
    Item symbol = jube_host_node_promisify_symbol("nodejs.util.promisify.customArgs");
    *symbol_root = symbol.item;
    Item names = js_array_new(0);
    *names_root = names.item;
    Item name = js_make_string_len(first, (int)strlen(first));
    *name_root = name.item;
    js_array_push((Item){.item = *names_root}, (Item){.item = *name_root});
    if (second) {
        name = js_make_string_len(second, (int)strlen(second));
        *name_root = name.item;
        js_array_push((Item){.item = *names_root}, (Item){.item = *name_root});
    }
    js_set_key_default((Item){.item = *function_root}, (Item){.item = *symbol_root},
        (Item){.item = *names_root});
    jube_host_opaque_root_frame_end(&frame);
}

static void jube_host_dom_notify_mutation(int kind, void* target, void* parent) {
    dom_notify_mutation((DomJsMutationKind)kind, target, parent);
}

static const JubeHostGcAPI jube_host_gc_api = {
    heap_register_gc_root,
    heap_unregister_gc_root,
    jube_host_root_frame_begin,
    jube_host_root_frame_take_slot,
    jube_host_root_frame_end,
    heap_register_gc_weak,
    heap_unregister_gc_weak,
};

static const JubeHostRootAPI jube_host_root_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostRootAPI),
    jube_host_opaque_root_frame_begin,
    jube_host_opaque_root_frame_take_slot,
    jube_host_opaque_root_frame_end,
    jube_host_opaque_persistent_root_register,
    jube_host_opaque_persistent_root_unregister,
};

static const JubeHostRuntimeAPI jube_host_node_runtime_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostRuntimeAPI),
    jube_host_node_current_session,
    jube_host_node_session_is_live,
    jube_host_node_session_global_this,
    jube_host_node_session_process,
    jube_host_node_current_this,
    jube_host_node_resolve_namespace,
    jube_host_node_resolve_host_namespace,
    jube_host_node_session_active_resources_info,
    jube_host_node_session_active_handles,
};

static const JubeHostNodeErrorAPI jube_host_node_error_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNodeErrorAPI),
    jube_host_node_throw_type_error_code,
    jube_host_node_throw_range_error_code,
    jube_host_node_throw_zlib_error,
    jube_host_node_throw_system_error,
    jube_host_node_throw_error_code,
    jube_host_node_throw_network_error,
};

static const JubeHostAsyncAPI jube_host_node_async_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostAsyncAPI),
    jube_host_node_work_submit,
    jube_host_node_work_cancel,
    js_setTimeout_promise,
    js_setImmediate_promise,
    js_setInterval,
    js_scheduler_wait,
    js_scheduler_yield,
    js_setTimeout,
    js_clearTimeout,
    js_clearInterval,
    js_setImmediate,
    js_timer_install_promisify_custom,
    jube_host_node_function_install_promisify_custom,
    jube_host_node_function_install_promisify_args,
    jube_host_node_next_tick_callback,
    jube_host_node_work_submit_root_span,
    jube_host_node_work_resource_value,
};

static const JubeHostBinaryAPI jube_host_node_binary_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostBinaryAPI),
    jube_host_node_is_typed_array,
    jube_host_node_typed_array_data,
    jube_host_node_typed_array_length,
    jube_host_node_buffer_from_bytes,
    jube_host_node_buffer_alloc,
    jube_host_node_buffer_prepare_write,
    jube_host_node_is_buffer,
    jube_host_node_describe_binary_view,
    jube_host_node_buffer_view,
};

static const JubeHostNodeEventsAPI jube_host_node_events_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNodeEventsAPI),
    jube_host_node_events_als_capture_context,
    jube_host_node_events_als_context_call,
    jube_host_node_events_domain_emit_current_error,
    jube_host_node_events_process_emit_warning,
    jube_host_node_events_is_error_like,
    jube_host_node_events_domain_current,
    jube_host_node_events_domain_call,
};

static const JubeHostNodePermissionAPI jube_host_node_permission_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNodePermissionAPI),
    jube_host_node_permission_has_fs_read,
    jube_host_node_permission_has_fs_write,
    jube_host_node_permission_check_fs_read,
    jube_host_node_permission_check_fs_write,
    jube_host_node_permission_enabled,
    js_process_permission_has,
    js_process_permission_drop,
};

static const JubeHostWorkerAPI jube_host_node_worker_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostWorkerAPI),
    js_message_channel_new,
    js_message_port_new,
    js_message_port_move_to_context,
    js_message_port_receive_message_on_port,
    js_worker_mark_as_untransferable,
    js_worker_is_marked_as_untransferable,
};

static const JubeHostNodeZlibAPI jube_host_node_zlib_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNodeZlibAPI),
    jube_host_node_zlib_crc32,
    jube_host_node_zlib_codec,
    jube_host_node_zlib_result_release,
    jube_host_node_zlib_stream_init,
    jube_host_node_zlib_stream_run,
    jube_host_node_zlib_stream_free,
};

static const JubeHostFilesystemAPI jube_host_filesystem_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostFilesystemAPI),
    js_node_fs_read_write,
    js_node_fs_read_write_release,
    js_node_fs_copy_file,
    js_node_fs_path_operation,
    js_node_fs_string_operation,
    js_node_fs_string_operation_release,
    js_node_fs_directory_read,
    js_node_fs_directory_read_release,
    js_node_fs_descriptor_operation,
    js_node_fs_metadata_operation,
    js_node_fs_statfs_operation,
};

static const JubeHostStreamAPI jube_host_node_stream_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostStreamAPI),
    js_node_fs_read_stream_new,
    js_node_fs_write_stream_new,
};

static const JubeHostNodeAPI jube_host_node_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNodeAPI),
    JUBE_HOST_CAP_NODE_RUNTIME,
    &jube_host_node_runtime_api,
    &jube_host_root_api,
    &jube_host_node_error_api,
    &jube_host_node_async_api,
    &jube_host_node_binary_api,
    &jube_host_node_events_api,
    &jube_host_node_worker_api,
    &jube_host_node_permission_api,
    &jube_host_node_stream_api,
    NULL,
    &jube_host_node_zlib_api,
    &jube_host_filesystem_api,
};

static const JubeHostValueAPI jube_host_value_api = {
    vmap_new,
    js_new_object,
    js_array_new,
    js_array_push,
    js_array_length,
    js_elements_get_int,
    jube_host_value_array_set,
    js_get_key_default,
    js_set_key_default,
    jube_host_value_property_set_own,
    jube_host_value_property_has_own,
    jube_host_value_property_get_own_data,
    jube_host_value_is_array,
    jube_host_value_kind,
    jube_host_value_string_copy,
    jube_host_value_string_from_utf8_n,
    jube_host_value_string_length,
    jube_host_value_string_bytes,
    jube_host_value_number_to_int64_exact,
    jube_host_value_native_object_new,
    jube_host_value_native_object_data,
};

static Item jube_host_script_new_function(JubeNativeFunctionSpec spec) {
#define JUBE_NATIVE_FUNCTION_CASE(arity, member) \
    case arity: \
        if (spec.constructable) { \
            if (spec.adapter_arity != arity) { \
                log_error("jube-callable: construct target arity %d mismatches adapter %d", \
                    arity, spec.adapter_arity); \
                return ItemError; \
            } \
            return js_new_native_constructor(spec.target.member); \
        } \
        return js_new_native_function(spec.target.member, spec.adapter_arity)
    switch (spec.target_arity) {
    JUBE_NATIVE_FUNCTION_CASE(0, p0);
    JUBE_NATIVE_FUNCTION_CASE(1, p1);
    JUBE_NATIVE_FUNCTION_CASE(2, p2);
    JUBE_NATIVE_FUNCTION_CASE(3, p3);
    JUBE_NATIVE_FUNCTION_CASE(4, p4);
    JUBE_NATIVE_FUNCTION_CASE(5, p5);
    JUBE_NATIVE_FUNCTION_CASE(6, p6);
    JUBE_NATIVE_FUNCTION_CASE(7, p7);
    JUBE_NATIVE_FUNCTION_CASE(8, p8);
    default:
        log_error("jube-callable: unsupported native target arity %d",
            spec.target_arity);
        return ItemError;
    }
#undef JUBE_NATIVE_FUNCTION_CASE
}

static Item jube_host_script_new_closure(JubeNativeFunctionSpec spec,
        Item* env, int env_size) {
    if (spec.constructable) {
        log_error("jube-callable: native closure cannot be constructable");
        return ItemError;
    }
#define JUBE_NATIVE_CLOSURE_CASE(arity, member) \
    case arity: return js_new_native_closure(spec.target.member, \
        spec.adapter_arity, env, env_size)
    switch (spec.target_arity) {
    JUBE_NATIVE_CLOSURE_CASE(1, p1);
    JUBE_NATIVE_CLOSURE_CASE(2, p2);
    JUBE_NATIVE_CLOSURE_CASE(3, p3);
    JUBE_NATIVE_CLOSURE_CASE(4, p4);
    JUBE_NATIVE_CLOSURE_CASE(5, p5);
    default:
        log_error("jube-callable: unsupported native closure target arity %d",
            spec.target_arity);
        return ItemError;
    }
#undef JUBE_NATIVE_CLOSURE_CASE
}

// A module's call back into script runs under the RA10 barrier: it may not
// park the activation whose stack holds the module's frame (D7.4.2).
static Item jube_host_call_function(Item function, Item this_value, Item* args,
        int arg_count) {
    activation_barrier_enter();
    Item result = js_call_function(function, this_value, args, arg_count);
    activation_barrier_leave();
    return result;
}

static Item jube_host_well_known_symbol(const char* name) {
    if (!name) return ItemNull;
    RootFrame roots(1);
    Rooted<Item> key_root(roots, js_name_item(name, strlen(name)));
    return js_symbol_well_known(key_root.get());
}

static const JubeHostScriptAPI jube_host_script_api = {
    jube_host_script_new_function,
    js_function_set_prototype,
    js_set_function_name,
    js_mark_non_enumerable,
    js_get_global_this,
    js_get_global_property,
    js_new_error_with_name,
    js_throw_value,
    js_reflect_own_keys,
    js_object_keys,
    js_reflect_delete_property,
    jube_host_call_function,
    js_is_truthy,
    js_get_intrinsic_prototype_for_class,
    js_make_number,
    js_get_number,
    js_date_new_from,
    js_date_method,
    jube_script_class_id,
    js_to_string,
    js_throw_type_error_code,
    js_object_create,
    js_throw_uri_error_code,
    js_error_lane_payload,
    js_mark_non_writable,
    js_object_freeze,
    jube_host_script_current_this,
    jube_host_script_strict_equal,
    jube_host_script_new_object_with_class,
    jube_host_script_class_is,
    js_typeof,
    js_alloc_env,
    jube_host_script_new_closure,
    js_get_prototype,
    js_set_prototype,
    js_promise_with_resolvers,
    jube_host_script_bigint_from_decimal,
    bigint_to_int64_exact,
    jube_host_well_known_symbol,
    js_get_iterator_proto,
};

extern "C" Item js_formdata_collect_form_entries(void* form_elem, void* submitter_elem);
extern "C" bool dom_focus_first_invalid_form_control(void* form_elem);
extern "C" bool dom_navigate_submit_target(const char* target_name, const char* url);
extern "C" void* dom_popover_target_for_button(void* button);
extern "C" int dom_popover_target_action(void* button);
extern "C" bool dom_activate_popover(void* popover, int action);

// The catalog section: one body per dom_api.def row, in row order. The cast is
// the row's own arity, so a body whose C signature disagrees with its row is a
// compile error here as well as in dom_api_check.cpp.
static const JubeHostDomCatalogAPI jube_host_dom_catalog = {
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv, iface, member, js_name) \
    (JubeDomFn##argc)(body),
#define DOM_RAW(name, cluster, ret, params, body, flags) \
    body,
#include "../dom/dom_api.def"
#undef DOM_OP
#undef DOM_RAW
};

// DS13: the same rows as a flat array, indexed by JubeDomRowIndex, so a member
// bind can name its row with a number a static initializer can hold. Index 0 is
// reserved for "not a row", so the table is offset by one -- built from the same
// .def in the same order, which is what keeps the two in step.
static void* const jube_host_dom_row_slots[] = {
    NULL,
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv, iface, member, js_name) (void*)(body),
#define DOM_RAW(name, cluster, ret, params, body, flags)
#include "../dom/dom_api.def"
#undef DOM_OP
#undef DOM_RAW
};
LAMBDA_STATIC_ASSERT(
    sizeof(jube_host_dom_row_slots) / sizeof(jube_host_dom_row_slots[0])
        == (size_t)JUBE_DOM_ROW_COUNT,
    "row slot array must match the JubeDomRowIndex space");

extern "C" void* jube_host_dom_row_slot(unsigned index) {
    if (index == 0 || index >= (unsigned)JUBE_DOM_ROW_COUNT) return NULL;
    return jube_host_dom_row_slots[index];
}



static const JubeHostRealmAPI jube_host_realm_api = {
    js_get_document_object_value,
    dom_document_proxy_for_doc_bridge,
    dom_document_proxy_get_property,
    dom_document_proxy_set_property,
    dom_get_prototype_value,
    dom_range_get_prototype_value,
    dom_selection_get_prototype_value,
    dom_expando_has_property,
    dom_expando_get_own_property_descriptor,
    dom_expando_delete_property,
    dom_expando_own_property_names,
    dom_live_child_collection_bridge,
    dom_attribute_collection_bridge,
    dom_token_list_operation,
    dom_live_document_forms_bridge,
    dom_live_form_elements_bridge,
    dom_live_document_get_elements_by_tag_name_bridge,
    dom_live_document_get_elements_by_class_name_bridge,
    dom_live_document_get_elements_by_name_bridge,
    dom_live_element_get_elements_by_tag_name_bridge,
    dom_live_element_get_elements_by_class_name_bridge,
    dom_initialize_node_wrapper,
    dom_throw_contenteditable_syntax_error,
    dom_get_selection_function_for_document,
    dom_document_default_view_bridge,
    dom_document_create_event_bridge,
};

static int jube_host_opaque_persistent_root_register(void* session, uint64_t* slot) {
    if (!jube_host_node_session_is_live(session) || !slot) {
        return -1;
    }
    // TLS storage is outside the side-root stack, so register its exact Item
    // slot with the active heap rather than exposing that heap to a module.
    heap_register_gc_root(slot);
    return 0;
}

static int jube_host_opaque_persistent_root_unregister(void* session, uint64_t* slot) {
    if (!jube_host_node_session_is_live(session) || !slot) {
        return -1;
    }
    heap_unregister_gc_root(slot);
    return 0;
}

static void* jube_host_node_current_session(void) {
    return jube_active_node_runtime_session;
}

static bool jube_host_node_session_is_live(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    return node_session && node_session == jube_active_node_runtime_session &&
        node_session->live;
}

static Item jube_host_node_session_global_this(void* session) {
    if (!jube_host_node_session_is_live(session)) return ItemNull;
    return js_get_global_this();
}

static Item jube_host_node_session_process(void* session) {
    if (!jube_host_node_session_is_live(session)) return ItemNull;
    // node-core attaches while the lazy global is being resolved; bypass it to
    // preserve the host-owned process identity without recursive activation.
    return js_get_process_object_value();
}

static Item jube_host_node_current_this(void* session) {
    if (!jube_host_node_session_is_live(session)) return ItemNull;
    return js_get_current_this();
}

static Item jube_host_node_session_active_resources_info(void* session) {
    if (!jube_host_node_session_is_live(session)) return ItemNull;
    return jube_node_resource_active_resources_info();
}

static Item jube_host_node_session_active_handles(void* session) {
    if (!jube_host_node_session_is_live(session)) return ItemNull;
    return jube_node_resource_active_handles();
}

static bool jube_host_node_is_typed_array(Item value) {
    return js_is_typed_array(value);
}

static uint8_t* jube_host_node_typed_array_data(Item value) {
    return (uint8_t*)js_typed_array_current_data_ptr(value);
}

static int jube_host_node_typed_array_length(Item value) {
    return js_typed_array_byte_length(value);
}

static Item jube_host_node_buffer_from_bytes(const uint8_t* data, int length) {
    if (length < 0 || (length > 0 && !data)) return ItemNull;
    return js_buffer_from_bytes((const char*)data, length);
}

static Item jube_host_node_buffer_alloc(int length) {
    return length < 0 ? ItemNull : js_buffer_from_bytes(NULL, length);
}

static uint8_t* jube_host_node_buffer_prepare_write(Item value) {
    return (uint8_t*)js_typed_array_prepare_write_ptr(value);
}

static bool jube_host_node_is_buffer(Item value) {
    if (!js_is_typed_array(value)) return false;
    JsTypedArray* typed_array = js_get_typed_array_ptr(value.map);
    return typed_array && typed_array->is_buffer;
}

static int jube_host_node_describe_binary_view(Item value, JubeBinaryView* out_view) {
    if (!out_view) return JUBE_BINARY_VIEW_NOT_VIEW;
    *out_view = {};
    if (js_is_arraybuffer(value)) {
        JsArrayBuffer* array_buffer = js_get_arraybuffer_ptr_item(value);
        if (!array_buffer || js_arraybuffer_detached(array_buffer)) {
            return JUBE_BINARY_VIEW_DETACHED;
        }
        out_view->array_buffer = value;
        out_view->byte_length = js_arraybuffer_length(array_buffer);
        return JUBE_BINARY_VIEW_OK;
    }
    if (js_is_typed_array(value)) {
        JsTypedArray* typed_array = js_get_typed_array_ptr(value.map);
        if (!typed_array || !typed_array->base.buffer || js_arraybuffer_detached(typed_array->base.buffer)) {
            return JUBE_BINARY_VIEW_DETACHED;
        }
        int byte_length = js_typed_array_byte_length(value);
        int byte_offset = js_typed_array_byte_offset(value);
        int backing_length = js_arraybuffer_length(typed_array->base.buffer);
        if (byte_length < 0 || byte_offset < 0 || byte_offset > backing_length ||
                byte_length > backing_length - byte_offset) {
            return JUBE_BINARY_VIEW_OUT_OF_BOUNDS;
        }
        out_view->array_buffer = typed_array->base.buffer_item
            ? (Item){.item = typed_array->base.buffer_item}
            : js_arraybuffer_wrap(typed_array->base.buffer);
        if (!js_is_arraybuffer(out_view->array_buffer)) return JUBE_BINARY_VIEW_OUT_OF_BOUNDS;
        out_view->byte_offset = byte_offset;
        out_view->byte_length = byte_length;
        return JUBE_BINARY_VIEW_OK;
    }
    if (js_is_dataview(value)) {
        JsDataView* data_view = js_get_dataview_ptr(value);
        if (!data_view || !data_view->buffer || js_arraybuffer_detached(data_view->buffer)) {
            return JUBE_BINARY_VIEW_DETACHED;
        }
        int backing_length = js_arraybuffer_length(data_view->buffer);
        int byte_length = data_view->length_tracking
            ? backing_length - data_view->byte_offset : data_view->byte_length;
        if (data_view->byte_offset < 0 || byte_length < 0 ||
                data_view->byte_offset > backing_length ||
                (!data_view->length_tracking &&
                 data_view->byte_length > backing_length - data_view->byte_offset)) {
            return JUBE_BINARY_VIEW_OUT_OF_BOUNDS;
        }
        out_view->array_buffer = data_view->buffer_item
            ? (Item){.item = data_view->buffer_item}
            : js_arraybuffer_wrap(data_view->buffer);
        if (!js_is_arraybuffer(out_view->array_buffer)) return JUBE_BINARY_VIEW_OUT_OF_BOUNDS;
        out_view->byte_offset = data_view->byte_offset;
        out_view->byte_length = byte_length;
        return JUBE_BINARY_VIEW_OK;
    }
    return JUBE_BINARY_VIEW_NOT_VIEW;
}

static Item jube_host_node_buffer_view(Item array_buffer, int byte_offset, int byte_length) {
    JsArrayBuffer* backing = js_get_arraybuffer_ptr_item(array_buffer);
    if (!backing || js_arraybuffer_detached(backing) || byte_offset < 0 || byte_length < 0 ||
            byte_offset > js_arraybuffer_length(backing) ||
            byte_length > js_arraybuffer_length(backing) - byte_offset) {
        return ItemNull;
    }
    Item result = js_typed_array_new_from_buffer(JS_TYPED_UINT8, array_buffer, byte_offset,
                                                  byte_length);
    JsTypedArray* typed_array = js_is_typed_array(result) ?
        js_get_typed_array_ptr(result.map) : NULL;
    if (!typed_array) return ItemNull;
    typed_array->is_buffer = true;
    return result;
}

static int jube_host_value_kind(Item value) {
    switch (get_type_id(value)) {
        case LMD_TYPE_UNDEFINED: return JUBE_VALUE_UNDEFINED;
        case LMD_TYPE_NULL: return JUBE_VALUE_NULL;
        case LMD_TYPE_BOOL: return JUBE_VALUE_BOOLEAN;
        case LMD_TYPE_INT:
        case LMD_TYPE_INT64:
        case LMD_TYPE_FLOAT: return JUBE_VALUE_NUMBER;
        case LMD_TYPE_STRING: return JUBE_VALUE_STRING;
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_VARRAY: return JUBE_VALUE_ARRAY;
        case LMD_TYPE_MAP:
        case LMD_TYPE_VMAP:
        case LMD_TYPE_VELMT: return JUBE_VALUE_OBJECT;
        case LMD_TYPE_FUNC: return JUBE_VALUE_FUNCTION;
        case LMD_TYPE_SYMBOL: return JUBE_VALUE_SYMBOL;
        case LMD_TYPE_DECIMAL: {
            Decimal* decimal = value.get_decimal();
            return decimal && decimal->storage_kind == DECIMAL_BIGINT
                ? JUBE_VALUE_BIGINT : JUBE_VALUE_OTHER;
        }
        default: return JUBE_VALUE_OTHER;
    }
}

static bool jube_host_value_number_to_int64_exact(Item value, int64_t* out_value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) {
        if (out_value) *out_value = it2i(value);
        return true;
    }
    if (type != LMD_TYPE_FLOAT) return false;
    double number = it2d(value);
    // JavaScript numeric literals commonly arrive as doubles; reject only
    // fractional or out-of-range values instead of losing valid integer APIs.
    if (!isfinite(number) || number < (double)INT64_MIN || number >= (double)INT64_MAX ||
            (double)(int64_t)number != number) {
        return false;
    }
    if (out_value) *out_value = (int64_t)number;
    return true;
}

static Item jube_host_value_native_object_new(const JubeTypeDef* type, void* payload) {
    if (!type) return ItemNull;
    // direct engine seams can produce module types before an explicit import.
    const JubeModuleDef* module = jube_module_for_host_type(type);
    if (module && !jube_activate_module_descriptor(module)) return ItemNull;
    Item object = ItemNull;
    switch (type->carrier) {
    case JUBE_CARRIER_VARRAY:
        object = varray_new((const VArrayVtable*)type->carrier_ops, payload,
                            (const void*)type, payload);
        break;
    case JUBE_CARRIER_VELMT:
        object = velmt_new((const VelmtVtable*)type->carrier_ops, payload,
                           (const void*)type, payload);
        break;
    case JUBE_CARRIER_VMAP:
    default:
        object = vmap_new();
        virtual_host_set(object, (const void*)type, payload);
        break;
    }
    return object;
}

static void* jube_host_value_native_object_data(Item object, const JubeTypeDef* type) {
    if (!type || get_type_id(object) != jube_carrier_type_id(type->carrier) ||
            virtual_host_type(object) != (const void*)type) return NULL;
    return virtual_host_data(object);
}

static bool jube_host_value_string_copy(Item value, char* out, size_t out_size,
                                        size_t* out_length) {
    if (out_length) *out_length = 0;
    if (!out || out_size == 0 || get_type_id(value) != LMD_TYPE_STRING) return false;
    String* string = it2s(value);
    if (!string || string->len >= out_size) return false;
    str_copy(out, out_size, string->chars, string->len);
    if (out_length) *out_length = string->len;
    return true;
}

static Item jube_host_value_string_from_utf8_n(const char* text, size_t length) {
    if (!text || length > INT32_MAX) return ItemNull;
    // Namespace keys may use interned names, but observable path results must
    // retain ordinary JS string semantics (including Win32 separators).
    return js_make_string_len(text, (int)length);
}

static size_t jube_host_value_string_length(Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return 0;
    String* string = it2s(value);
    return string && string->len > 0 ? (size_t)string->len : 0;
}

static const uint8_t* jube_host_value_string_bytes(Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* string = it2s(value);
    return string ? (const uint8_t*)string->chars : NULL;
}

static Item jube_host_value_property_set_own(Item object, Item key, Item value) {
    // Null-prototype dictionaries must retain an own "__proto__" key instead
    // of re-entering the engine's inherited accessor path.
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* text = it2s(key);
        if (text && text->len == 9 && memcmp(text->chars, "__proto__", 9) == 0) {
            // Preserve Object.create(null)'s prototype sentinel before the
            // dictionary gains its observable own __proto__ data field.
            js_mark_own_proto_property(object);
            js_mark_non_enumerable(object,
                (Item){.item = s2it(heap_create_name("__internal_proto__", 18))});
        }
    }
    return js_define_own_key_storage(object, key, value);
}

static bool jube_host_value_property_has_own(Item object, Item key) {
    return it2b(js_has_own_property(object, key));
}

static bool jube_host_value_property_get_own_data(Item object, Item key, Item* out_value) {
    if (out_value) *out_value = ItemNull;
    TypeId object_type = get_type_id(object);
    if ((object_type != LMD_TYPE_MAP && object_type != LMD_TYPE_VMAP) ||
            get_type_id(key) != LMD_TYPE_STRING) return false;
    String* text = it2s(key);
    if (!text) return false;
    bool found = false;
    Item value = js_map_shape_lookup_ext(object.map, text->chars, (int)text->len, &found);
    if (found && out_value) *out_value = value;
    return found;
}

static bool jube_host_value_is_array(Item value) {
    return get_type_id(value) == LMD_TYPE_ARRAY;
}

static Item jube_host_value_array_set(Item array, int64_t index, Item value) {
    return js_elements_set_int(array, index, value);
}

static Item jube_host_script_current_this(void) {
    return js_get_current_this();
}

static Item jube_host_script_strict_equal(Item left, Item right) {
    return js_strict_equal(left, right);
}

static Item jube_host_script_new_object_with_class(int class_id) {
    JsClass host_class = JS_CLASS_NONE;
    switch (class_id) {
        case JUBE_SCRIPT_CLASS_URL: host_class = JS_CLASS_URL; break;
        case JUBE_SCRIPT_CLASS_URL_SEARCH_PARAMS:
            host_class = JS_CLASS_URL_SEARCH_PARAMS;
            break;
        case JUBE_SCRIPT_CLASS_BLOB: host_class = JS_CLASS_BLOB; break;
        case JUBE_SCRIPT_CLASS_EVENT_EMITTER: host_class = JS_CLASS_EVENT_EMITTER; break;
        default: return ItemNull;
    }
    return js_new_object_with_class(host_class);
}

static bool jube_host_script_class_is(Item object, int class_id) {
    JsClass host_class = JS_CLASS_NONE;
    switch (class_id) {
        case JUBE_SCRIPT_CLASS_URL: host_class = JS_CLASS_URL; break;
        case JUBE_SCRIPT_CLASS_URL_SEARCH_PARAMS:
            host_class = JS_CLASS_URL_SEARCH_PARAMS;
            break;
        case JUBE_SCRIPT_CLASS_BLOB: host_class = JS_CLASS_BLOB; break;
        case JUBE_SCRIPT_CLASS_EVENT_EMITTER: host_class = JS_CLASS_EVENT_EMITTER; break;
        default: return false;
    }
    return js_class_id(object) == host_class;
}

static Item jube_host_node_events_als_capture_context(void) {
    return js_als_capture_context();
}

static Item jube_host_node_events_als_context_call(Item context, Item callback, Item this_val,
                                                    Item arg1, int64_t has_arg) {
    return js_als_context_call(context, callback, this_val, arg1, has_arg);
}

static int jube_host_node_events_domain_emit_current_error(Item error) {
    return js_domain_emit_current_error(error);
}

static Item jube_host_node_events_process_emit_warning(Item warning, Item type_item,
                                                        Item code_item) {
    return js_process_emitWarning(warning, type_item, code_item);
}

static bool jube_host_node_events_is_error_like(Item value) {
    JsClass cls = js_class_id(value);
    return js_class_is_error_like(cls) || cls == JS_CLASS_DOM_EXCEPTION;
}

static Item jube_host_node_events_domain_current(void) {
    return js_domain_get_current();
}

static Item jube_host_node_events_domain_call(Item domain, Item callback, Item this_val,
                                              Item* args, int arg_count) {
    return js_domain_call_function(domain, callback, this_val, args, arg_count);
}

static bool jube_host_node_permission_has_fs_read(const char* path) {
    return path && js_permission_has_fs_read(path) != 0;
}

static bool jube_host_node_permission_has_fs_write(const char* path) {
    return path && js_permission_has_fs_write(path) != 0;
}

static Item jube_host_node_permission_check_fs_read(const char* path) {
    if (!path) return ItemNull;
    return js_permission_check_fs_read(path);
}

static Item jube_host_node_permission_check_fs_write(const char* path) {
    if (!path) return ItemNull;
    return js_permission_check_fs_write(path);
}

static bool jube_host_node_permission_enabled(void) {
    return js_permission_enabled() != 0;
}

static Item jube_host_node_throw_type_error_code(void* session, const char* code,
                                                  const char* message) {
    if (!jube_host_node_session_is_live(session) || !code || !message) return ItemNull;
    return js_throw_type_error_code(code, message);
}

static Item jube_host_node_throw_range_error_code(void* session, const char* code,
                                                   const char* message) {
    if (!jube_host_node_session_is_live(session) || !code || !message) return ItemNull;
    return js_throw_range_error_code(code, message);
}

static const char* jube_host_node_zlib_error_code(int status) {
    switch (status) {
    case Z_STREAM_END: return "Z_STREAM_END";
    case Z_NEED_DICT: return "Z_NEED_DICT";
    case Z_ERRNO: return "Z_ERRNO";
    case Z_STREAM_ERROR: return "Z_STREAM_ERROR";
    case Z_DATA_ERROR: return "Z_DATA_ERROR";
    case Z_MEM_ERROR: return "Z_MEM_ERROR";
    case Z_BUF_ERROR: return "Z_BUF_ERROR";
    case Z_VERSION_ERROR: return "Z_VERSION_ERROR";
    default: return "Z_OK";
    }
}

static const char* jube_host_node_zlib_error_detail(int status) {
    switch (status) {
    case Z_NEED_DICT: return "need dictionary";
    case Z_ERRNO: return "zlib errno";
    case Z_STREAM_ERROR: return "stream error";
    case Z_DATA_ERROR: return "data error";
    case Z_MEM_ERROR: return "memory error";
    case Z_BUF_ERROR: return "unexpected end of file";
    case Z_VERSION_ERROR: return "version error";
    default: return "zlib operation failed";
    }
}

static Item jube_host_node_throw_zlib_error(void* session, const char* method, int status) {
    if (!jube_host_node_session_is_live(session) || !method) return ItemNull;
    const char* code = jube_host_node_zlib_error_code(status);
    const char* reason = jube_host_node_zlib_error_detail(status);
    char message[256];
    snprintf(message, sizeof(message), "%s: %s failed: %s", code, method, reason);
    Item error = js_new_error(js_make_string_len(message, (int)strlen(message)));
    js_set_key_default(error, js_make_string_len("code", 4),
        js_make_string_len(code, (int)strlen(code)));
    js_set_key_default(error, js_make_string_len("errno", 5), (Item){.item = i2it(status)});
    return js_throw_value(error);
}

static uint32_t jube_host_node_zlib_crc32(const uint8_t* data, int length, uint32_t seed) {
    return node_zlib_crc32_bytes(data, length, seed);
}

static bool jube_host_node_zlib_codec(enum JubeNodeZlibCodecMode mode, const uint8_t* data,
                                      int length, JubeNodeZlibResult* out_result) {
    if (!out_result) return false;
    NodeZlibBytes host_result = {};
    bool success = false;
    switch (mode) {
    case JUBE_NODE_ZLIB_GZIP:
        success = node_zlib_gzip_encode(data, length, &host_result);
        break;
    case JUBE_NODE_ZLIB_GUNZIP:
        success = node_zlib_gunzip_decode(data, length, &host_result);
        break;
    case JUBE_NODE_ZLIB_DEFLATE:
        success = node_zlib_deflate_encode(data, length, &host_result);
        break;
    case JUBE_NODE_ZLIB_INFLATE:
        success = node_zlib_inflate_decode(data, length, &host_result);
        break;
    case JUBE_NODE_ZLIB_DEFLATE_RAW:
        success = node_zlib_deflate_raw_encode(data, length, &host_result);
        break;
    case JUBE_NODE_ZLIB_INFLATE_RAW:
        success = node_zlib_inflate_raw_decode(data, length, &host_result);
        break;
    case JUBE_NODE_ZLIB_UNZIP:
        success = node_zlib_unzip_decode(data, length, &host_result);
        break;
    }
    // The host allocated this result, so its release must stay paired with the
    // host codec even when a module returns early while shaping a Node error.
    out_result->data = host_result.data;
    out_result->length = host_result.length;
    out_result->status = host_result.status;
    return success;
}

static void jube_host_node_zlib_result_release(JubeNodeZlibResult* result) {
    if (!result) return;
    NodeZlibBytes host_result = {result->data, result->length, result->status};
    node_zlib_bytes_free(&host_result);
    result->data = NULL;
    result->length = 0;
    result->status = 0;
}

static bool jube_host_node_zlib_stream_init(enum JubeNodeZlibCodecMode mode, int window_bits,
                                            int level, int mem_level, int strategy,
                                            void** out_state, int* out_status) {
    return node_zlib_stream_init((enum NodeZlibCodecMode)mode, window_bits, level,
                                 mem_level, strategy, out_state, out_status);
}

static bool jube_host_node_zlib_stream_run(void* state, const uint8_t* data, int length,
                                           int flush, JubeNodeZlibResult* out_result) {
    NodeZlibBytes host_result = {};
    bool success = node_zlib_stream_run(state, data, length, flush, &host_result);
    if (out_result) {
        out_result->data = host_result.data;
        out_result->length = host_result.length;
        out_result->status = host_result.status;
    } else {
        node_zlib_bytes_free(&host_result);
    }
    return success;
}

static void jube_host_node_zlib_stream_free(void* state) {
    node_zlib_stream_free(state);
}

static Item jube_host_node_throw_system_error(void* session, const char* syscall,
                                              int error_number) {
    if (!jube_host_node_session_is_live(session) || !syscall) return ItemNull;
    return js_node_throw_system_error(syscall, error_number);
}

static Item jube_host_node_throw_error_code(void* session, const char* code,
                                            const char* message) {
    if (!jube_host_node_session_is_live(session) || !code || !message) return ItemNull;
    return js_throw_error_with_code(code, message);
}

static Item jube_host_node_throw_network_error(void* session, int status, const char* syscall,
        const char* address, int port) {
    if (!jube_host_node_session_is_live(session)) return ItemNull;
    const char* code = uv_err_name(status);
    if (!code) code = "UNKNOWN";
    char message[512];
    const char* operation = syscall ? syscall : "connect";
    if (address && port >= 0) {
        snprintf(message, sizeof(message), "%s %s %s:%d", operation, code, address, port);
    } else if (address) {
        snprintf(message, sizeof(message), "%s %s %s", operation, code, address);
    } else {
        snprintf(message, sizeof(message), "%s %s", operation, code);
    }
    Item error = js_new_error(js_make_string_len(message, (int)strlen(message)));
    js_set_key_default(error, js_make_string_len("code", 4), js_make_string_len(code, (int)strlen(code)));
    js_set_key_default(error, js_make_string_len("errno", 5), (Item){.item = i2it(status)});
    js_set_key_default(error, js_make_string_len("syscall", 7),
        js_make_string_len(operation, (int)strlen(operation)));
    if (address) {
        js_set_key_default(error, js_make_string_len("address", 7),
            js_make_string_len(address, (int)strlen(address)));
    }
    if (port >= 0) {
        js_set_key_default(error, js_make_string_len("port", 4), (Item){.item = i2it(port)});
    }
    return js_throw_value(error);
}

static Item jube_host_node_emit_callback(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || get_type_id(env[0]) != LMD_TYPE_FUNC) return (Item){.item = ITEM_JS_UNDEFINED};
    if (env[1].item != ItemNull.item) {
        js_call_function(env[0], (Item){.item = ITEM_JS_UNDEFINED}, &env[1], 1);
    } else {
        Item args[2] = {ItemNull, env[2]};
        js_call_function(env[0], (Item){.item = ITEM_JS_UNDEFINED}, args, 2);
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static void jube_host_node_next_tick_callback(void* session, Item callback, Item error, Item result) {
    if (!jube_host_node_session_is_live(session) || get_type_id(callback) != LMD_TYPE_FUNC) return;
    RootFrame roots(3);
    Rooted<Item> callback_root(roots, callback);
    Rooted<Item> error_root(roots, error);
    Rooted<Item> result_root(roots, result);
    Item* env = js_alloc_env(3);
    if (!env) return;
    // js_alloc_env may compact the heap, so store the refreshed rooted values in the queued closure.
    env[0] = callback_root.get();
    env[1] = error_root.get();
    env[2] = result_root.get();
    js_next_tick_enqueue(js_new_native_closure(jube_host_node_emit_callback, 0, env, 3));
}

static void jube_host_node_work_execute(uv_work_t* request) {
    JubeNodeAsyncWork* job = request ? (JubeNodeAsyncWork*)request->data : NULL;
    if (job && job->work) job->work(job->user);
}

static void jube_host_node_work_resource_close(void* user) {
    JubeNodeAsyncWork* job = (JubeNodeAsyncWork*)user;
    if (!job) return;
    // Table removal invalidates the rid before this callback. A queued libuv
    // request still owns the module POD tail until its completion callback.
    job->resource_id = 0;
    if (job->queued) (void)uv_cancel((uv_req_t*)&job->request);
}

static const RuntimeResourceDescriptor* jube_host_node_work_descriptor(
        int resource_kind) {
    switch (resource_kind) {
        case JUBE_ASYNC_RESOURCE_FILESYSTEM_REQUEST:
            return runtime_resource_descriptor_from_legacy_name("FSReqCallback");
        default:
            return NULL;
    }
}

static void jube_host_node_work_complete(uv_work_t* request, int status) {
    JubeNodeAsyncWork* job = request ? (JubeNodeAsyncWork*)request->data : NULL;
    if (!job) return;
    job->queued = false;
    NodeRuntimeSession* session = (NodeRuntimeSession*)job->session;
    // Detached heaps must never receive a late completion; release only the
    // module-owned POD payload after libuv has returned ownership to the host.
    if (session && !session->detaching && jube_host_node_session_is_live(session) &&
            job->complete) {
        job->complete(job->user, status);
    }
    if (job->resource_id != 0 && session && js_active_runtime_state) {
        uint32_t resource_id = job->resource_id;
        job->resource_id = 0;
        runtime_resource_table_remove_owned(js_runtime_resource_table(), session,
            resource_id);
    }
    if (job->destroy) job->destroy(job->user);
    mem_free(job);
}

static int jube_host_node_work_submit(void* session, JubeAsyncWorkCallback work,
                                      JubeAsyncCompletionCallback complete,
                                      JubeAsyncDestroyCallback destroy, void* user,
                                      uint32_t* out_request_id) {
    (void)session;
    (void)work;
    (void)complete;
    (void)destroy;
    (void)user;
    if (out_request_id) *out_request_id = 0;
    // A hosted native request must publish exact owned Items with its rid.
    // The rootless compatibility entry is retained only for ABI decoding.
    return -1;
}

static int jube_host_node_work_submit_root_span(void* session, int resource_kind,
        const Item* root_values, int root_count, JubeAsyncWorkCallback work,
        JubeAsyncCompletionCallback complete, JubeAsyncDestroyCallback destroy,
        void* user, uint32_t* out_resource_id) {
    if (out_resource_id) *out_resource_id = 0;
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    const RuntimeResourceDescriptor* descriptor =
        jube_host_node_work_descriptor(resource_kind);
    if (!jube_host_node_session_is_live(session) || node_session->detaching ||
            !root_values || root_count <= 0 || !work || !destroy || !descriptor ||
            !js_active_runtime_state || !lambda_uv_loop()) return -1;
    JubeNodeAsyncWork* job = (JubeNodeAsyncWork*)mem_calloc(1, sizeof(JubeNodeAsyncWork),
        MEM_CAT_SYSTEM);
    if (!job) return -1;
    job->session = session;
    job->work = work;
    job->complete = complete;
    job->destroy = destroy;
    job->user = user;
    job->request.data = job;
    job->resource_id = runtime_resource_table_add_root_span_owned(
        js_runtime_resource_table(), node_session, root_values, root_count,
        descriptor, jube_host_node_work_resource_close, job, false);
    if (job->resource_id == 0) {
        mem_free(job);
        return -1;
    }
    int status = uv_queue_work(lambda_uv_loop(), &job->request, jube_host_node_work_execute,
        jube_host_node_work_complete);
    if (status != 0) {
        uint32_t resource_id = job->resource_id;
        job->resource_id = 0;
        runtime_resource_table_remove_owned(js_runtime_resource_table(),
            node_session, resource_id);
        mem_free(job);
        return status;
    }
    job->queued = true;
    if (out_resource_id) *out_resource_id = job->resource_id;
    return 0;
}

static int jube_host_node_work_cancel(void* session, uint32_t request_id) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!jube_host_node_session_is_live(session) || request_id == 0 ||
            !js_active_runtime_state) {
        return -1;
    }
    const RuntimeResourceEntry* entry = runtime_resource_table_entry_owned(
        js_runtime_resource_table(), node_session, request_id);
    JubeNodeAsyncWork* job = entry ? (JubeNodeAsyncWork*)entry->close_user : NULL;
    return job && job->queued ? uv_cancel((uv_req_t*)&job->request) : -1;
}

static Item jube_host_node_work_resource_value(void* session,
        uint32_t resource_id, int root_index) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!jube_host_node_session_is_live(session) || resource_id == 0 ||
            root_index < 0 || !js_active_runtime_state) return ItemNull;
    const RuntimeResourceEntry* entry = runtime_resource_table_entry_owned(
        js_runtime_resource_table(), node_session, resource_id);
    if (!entry || root_index >= entry->root_count) return ItemNull;
    return runtime_resource_table_root_value(js_runtime_resource_table(), entry,
        root_index);
}

static int jube_host_node_resolve_namespace(void* session, const char* specifier,
                                             Item* out_namespace) {
    if (out_namespace) *out_namespace = ItemNull;
    if (!jube_host_node_session_is_live(session) || !specifier || !out_namespace) return -1;
    return jube_specifier_resolve(specifier, out_namespace) == JUBE_SPECIFIER_RESOLVED ? 0 : -1;
}

static int jube_host_node_resolve_host_namespace(void* session, const char* specifier,
                                                  Item* out_namespace) {
    if (out_namespace) *out_namespace = ItemNull;
    if (!jube_host_node_session_is_live(session) || !specifier || !out_namespace) return -1;
    typedef Item (*JubeHostNamespaceFactory)(void);
    typedef struct JubeHostNamespaceEntry {
        const char* specifier;
        JubeHostNamespaceFactory factory;
    } JubeHostNamespaceEntry;
    // Runtime primitives stay host-owned while node-core's descriptors control
    // their public Node exposure.
    static const JubeHostNamespaceEntry entries[] = {
        {"module", js_get_node_module_namespace},
        {"url", node_url_namespace},
        {"events", node_events_namespace},
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (strcmp(specifier, entries[i].specifier) == 0) {
            *out_namespace = entries[i].factory();
            return 0;
        }
    }
    return -1;
}

static Item jube_template_value_copy(const JubeTemplateValue* value, int depth) {
    if (!value || depth > 32) return ItemError;
    switch (value->kind) {
    case JUBE_TEMPLATE_NULL: return ItemNull;
    case JUBE_TEMPLATE_BOOL: return {.item = b2it(value->boolean)};
    case JUBE_TEMPLATE_INT: return {.item = i2it(value->integer)};
    case JUBE_TEMPLATE_STRING:
        if (!value->bytes && value->byte_length) return ItemError;
        return {.item = s2it(heap_strcpy(value->bytes ? value->bytes : "",
            value->byte_length))};
    case JUBE_TEMPLATE_BINARY: {
        if (!value->bytes && value->byte_length) return ItemError;
        Binary* binary = heap_binary_from_bytes(value->bytes ? value->bytes : "",
            (int64_t)value->byte_length);
        return binary ? (Item){.item = x2it(binary)} : ItemError;
    }
    case JUBE_TEMPLATE_MAP: {
        if (value->field_count && !value->fields) return ItemError;
        RootFrame roots(3);
        Rooted<Item> map(roots, vmap_new());
        Rooted<Item> key(roots, ItemNull);
        Rooted<Item> child(roots, ItemNull);
        for (size_t i = 0; i < value->field_count; i++) {
            const JubeTemplateField* field = &value->fields[i];
            if (!field->name) return ItemError;
            key.set({.item = s2it(heap_strcpy(field->name, strlen(field->name)))});
            child.set(jube_template_value_copy(field->value, depth + 1));
            if (get_type_id(child.get()) == LMD_TYPE_ERROR) return ItemError;
            vmap_set(map.get(), key.get(), child.get());
        }
        return map.get();
    }
    case JUBE_TEMPLATE_ARRAY: {
        if (value->element_count && !value->elements) return ItemError;
        RootFrame roots(2);
        Rooted<Item> array_item(roots, {.array = array()});
        Rooted<Item> child(roots, ItemNull);
        for (size_t i = 0; i < value->element_count; i++) {
            child.set(jube_template_value_copy(&value->elements[i], depth + 1));
            if (get_type_id(child.get()) == LMD_TYPE_ERROR) return ItemError;
            array_push(array_item.get().array, child.get());
        }
        return array_item.get();
    }
    default: return ItemError;
    }
}

static Item jube_template_target_item(TemplateHostSession* session,
                                      const JubeTemplateTarget* target) {
    Item model{.item = template_host_session_root_word(session)};
    if (!target || !target->state_name) return model;
    TemplateEntry* entry = template_registry_match(g_template_registry,
        model, false, nullptr);
    const char* name = template_entry_state_name(entry, target->state_name);
    Item child = name ? tmpl_state_get(model, entry->template_ref, name) : ItemNull;
    if (get_type_id(child) == LMD_TYPE_NULL && target->fallback_attr)
        child = item_attr(model, target->fallback_attr);
    return child;
}

typedef struct JubeTemplateEmitBridge {
    TemplateHostSession* session;
    JubeTemplateEmitCallback callback;
    void* user;
} JubeTemplateEmitBridge;

class JubeTemplateActivationBarrier {
public:
    JubeTemplateActivationBarrier() { activation_barrier_enter(); }
    ~JubeTemplateActivationBarrier() { activation_barrier_leave(); }
    JubeTemplateActivationBarrier(const JubeTemplateActivationBarrier&) = delete;
    JubeTemplateActivationBarrier& operator=(
        const JubeTemplateActivationBarrier&) = delete;
};

static Item jube_template_emit(void* receiver, Item event_name, Item event_data) {
    JubeTemplateEmitBridge* bridge = (JubeTemplateEmitBridge*)receiver;
    if (!bridge->callback) return ItemNull;
    RootFrame roots(2);
    Rooted<Item> name(roots, event_name);
    Rooted<Item> data(roots, event_data);
    return bridge->callback(bridge->user, bridge->session,
        name.get(), data.get());
}

static int jube_template_dispatch_item(void* opaque, const JubeTemplateTarget* target,
        const char* event_name, Item event_data, JubeTemplateEmitCallback emit,
        void* user) {
    TemplateHostSession* session = (TemplateHostSession*)opaque;
    if (!session || !event_name) return -1;
    TemplateHostBinding binding(session);
    if (!binding.valid()) return -1;
    JubeTemplateActivationBarrier barrier;
    RootFrame roots(3);
    Rooted<Item> model(roots, jube_template_target_item(session, target));
    Rooted<Item> data(roots, event_data);
    Rooted<Item> result(roots, ItemNull);
    if (get_type_id(model.get()) != LMD_TYPE_ELEMENT) return -1;
    JubeTemplateEmitBridge bridge = {session, emit, user};
    LambdaEmitScope scope = {};
    lambda_emit_scope_enter(&scope, jube_template_emit, &bridge);
    bool handled = false;
    result.set(template_dispatch_event(model.get(),
        target && target->edit_mode, event_name, data.get(), &handled));
    lambda_emit_scope_leave(&scope);
    if (!handled || get_type_id(result.get()) == LMD_TYPE_ERROR)
        log_error("jube-template-dispatch: event=%s handled=%d result_type=%d",
            event_name, handled, (int)get_type_id(result.get()));
    return handled && get_type_id(result.get()) != LMD_TYPE_ERROR ? 0 : -1;
}

static int jube_template_dispatch(void* opaque, const JubeTemplateTarget* target,
        const char* event_name, const JubeTemplateValue* event_data,
        JubeTemplateEmitCallback emit, void* user) {
    TemplateHostSession* session = (TemplateHostSession*)opaque;
    if (!session || !event_name || !event_data) return -1;
    TemplateHostBinding binding(session);
    if (!binding.valid()) return -1;
    JubeTemplateActivationBarrier barrier;
    RootFrame roots(1);
    Rooted<Item> payload(roots, jube_template_value_copy(event_data, 0));
    if (get_type_id(payload.get()) == LMD_TYPE_ERROR) return -1;
    return jube_template_dispatch_item(session, target, event_name,
        payload.get(), emit, user);
}

static Item jube_template_attribute(Item value, const char* name) {
    return name ? item_attr(value, name) : ItemNull;
}

static bool jube_template_string_copy(Item value, char* out, size_t capacity,
                                      size_t* out_length) {
    String* source = value.get_string();
    if (out_length) *out_length = source ? source->len : 0;
    if (!source || !out || capacity <= source->len) return false;
    memcpy(out, source->chars, source->len);
    out[source->len] = '\0';
    return true;
}

static int jube_template_render_json(void* opaque, char** out_bytes,
                                     size_t* out_length) {
    if (out_bytes) *out_bytes = NULL;
    if (out_length) *out_length = 0;
    TemplateHostSession* session = (TemplateHostSession*)opaque;
    if (!session || !out_bytes || !out_length) return -1;
    TemplateHostBinding binding(session);
    if (!binding.valid()) return -1;
    JubeTemplateActivationBarrier barrier;
    RootFrame roots(1);
    Rooted<Item> snapshot(roots, fn_apply1({.item =
        template_host_session_root_word(session)}));
    if (get_type_id(snapshot.get()) != LMD_TYPE_ELEMENT) return -1;
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_TEMP, "jube.template.render");
    if (!pool) return -1;
    String* formatted = format_json(pool, snapshot.get());
    if (formatted) {
        *out_bytes = (char*)mem_alloc((size_t)formatted->len + 1, MEM_CAT_SYSTEM);
        if (*out_bytes) {
            memcpy(*out_bytes, formatted->chars, formatted->len);
            (*out_bytes)[formatted->len] = '\0';
            *out_length = formatted->len;
        }
    }
    pool_destroy(pool);
    return *out_bytes ? 0 : -1;
}

static int jube_template_render_item(void* opaque,
        JubeTemplateRenderCallback callback, void* user) {
    TemplateHostSession* session = (TemplateHostSession*)opaque;
    if (!session || !callback) return -1;
    TemplateHostBinding binding(session);
    if (!binding.valid()) return -1;
    JubeTemplateActivationBarrier barrier;
    RootFrame roots(1);
    Rooted<Item> snapshot(roots, fn_apply1({.item =
        template_host_session_root_word(session)}));
    if (get_type_id(snapshot.get()) != LMD_TYPE_ELEMENT) return -1;
    return callback(user, session, snapshot.get());
}

static const JubeHostTemplateAPI jube_host_template_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostTemplateAPI),
    [](const char* source, const char* reference) -> void* {
        JubeTemplateActivationBarrier barrier;
        return template_host_session_open(source, reference);
    },
    [](void* session) { template_host_session_close((TemplateHostSession*)session); },
    jube_template_dispatch,
    jube_template_dispatch_item,
    jube_template_attribute,
    jube_template_string_copy,
    jube_template_render_json,
    [](char* bytes) { mem_free(bytes); },
    jube_template_render_item,
    [](Item value, int64_t index) -> Item { return item_at(value, index); },
};

static JubeHostAPI jube_host_api = {
    JUBE_HOST_API_VERSION,
    sizeof(JubeHostAPI),
    JUBE_HOST_CAP_GC_ROOTS |
        JUBE_HOST_CAP_NODE_RUNTIME |
        JUBE_HOST_CAP_TEMPLATE_SESSION,
    &jube_host_gc_api,
    &jube_host_value_api,
    &jube_host_script_api,
    &jube_host_realm_api,
    &jube_host_dom_catalog,
    &jube_host_node_api,
    &jube_host_template_api,
};

extern "C" const JubeHostAPI* jube_internal_host_api(void) {
    return &jube_host_api;
}

// URL and EventEmitter also serve JS globals and readline. Their lifecycle is
// host-owned so node-core can be a dynamic descriptor without importing host
// symbols or constructing a second copy of either primitive.
static bool jube_node_shared_primitives_initialized = false;
static bool jube_node_shared_primitives_attached = false;

static int jube_node_shared_primitives_init(void) {
    if (jube_node_shared_primitives_initialized) return 0;
    if (node_url_init(&jube_host_api) != 0) return -1;
    if (node_events_init(&jube_host_api) != 0) {
        node_url_shutdown();
        return -1;
    }
    if (node_trace_events_init(&jube_host_api) != 0) {
        node_events_shutdown();
        node_url_shutdown();
        return -1;
    }
    jube_node_shared_primitives_initialized = true;
    return 0;
}

static void jube_node_shared_primitives_shutdown(void) {
    if (!jube_node_shared_primitives_initialized) return;
    node_trace_events_shutdown();
    node_events_shutdown();
    node_url_shutdown();
    jube_node_shared_primitives_attached = false;
    jube_node_shared_primitives_initialized = false;
}

static void jube_node_shared_primitives_attach(void* session) {
    if (!jube_node_shared_primitives_initialized || jube_node_shared_primitives_attached) return;
    node_url_runtime_attach(session);
    node_events_runtime_attach(session);
    jube_node_shared_primitives_attached = true;
}

static void jube_node_shared_primitives_reset(void* session) {
    if (!jube_node_shared_primitives_attached) return;
    node_url_runtime_reset(session);
    node_events_runtime_reset(session);
}

static void jube_node_shared_primitives_detach(void* session) {
    if (!jube_node_shared_primitives_attached) return;
    node_events_runtime_detach(session);
    node_url_runtime_detach(session);
    jube_node_shared_primitives_attached = false;
}

bool jube_activate_node_shared_primitives(void) {
    jube_modules_runtime_attach();
    if (!jube_active_node_runtime_session || !jube_active_node_runtime_session->live ||
            jube_node_shared_primitives_init() != 0) {
        return false;
    }
    // URL and EventTarget are browser globals too; they must have their host
    // tables before global constructors invoke the shared implementation.
    jube_node_shared_primitives_attach(jube_active_node_runtime_session);
    return jube_node_shared_primitives_attached;
}

// size-gated access to the DOM3 additive tail: a field exists only when the
// module's declared struct_size covers it, so v1 descriptors read as "no tail"
static bool jube_module_has_field(const JubeModuleDef* module, size_t field_end) {
    return module && module->struct_size >= field_end;
}

extern "C" const char* jube_module_interface_decl(const JubeModuleDef* module) {
    size_t end = offsetof(JubeModuleDef, interface_decl) + sizeof(module->interface_decl);
    return jube_module_has_field(module, end) ? module->interface_decl : NULL;
}

static const JubeModuleRequirements* jube_module_requirements(const JubeModuleDef* module) {
    size_t end = offsetof(JubeModuleDef, requirements) + sizeof(module->requirements);
    return jube_module_has_field(module, end) ? module->requirements : NULL;
}

const JubeGlobalDef* jube_module_globals(const JubeModuleDef* module, int32_t* count) {
    if (count) *count = 0;
    size_t end = offsetof(JubeModuleDef, global_count) + sizeof(module->global_count);
    if (!jube_module_has_field(module, end)) return NULL;
    if (count) *count = module->global_count;
    return module->globals;
}

static bool jube_module_requirements_are_supported(const JubeModuleDef* module) {
    const JubeModuleRequirements* requirements = jube_module_requirements(module);
    if (!requirements) return true;
    if (requirements->struct_size < JUBE_MODULE_REQUIREMENTS_V1_SIZE) {
        log_error("JUBE_REG: module '%s' has a truncated requirements record", module->name);
        return false;
    }
    size_t node_requirements_end = offsetof(JubeModuleRequirements, min_node_api_size) +
        sizeof(requirements->min_node_api_size);
    if (requirements->struct_size >= node_requirements_end &&
            (!jube_host_api.node ||
             requirements->min_node_api_version > jube_host_api.node->api_version ||
             requirements->min_node_api_size > jube_host_api.node->struct_size)) {
        log_error("JUBE_REG: module '%s' Node requirements are not provided by this host",
                  module->name);
        return false;
    }
    size_t table_requirements_end = offsetof(JubeModuleRequirements, min_root_api_size) +
        sizeof(requirements->min_root_api_size);
    if (requirements->struct_size >= table_requirements_end &&
            (!jube_host_api.value || !jube_host_api.script || !jube_host_api.node ||
             !jube_host_api.node->roots ||
             requirements->min_value_api_size > sizeof(JubeHostValueAPI) ||
             requirements->min_script_api_size > sizeof(JubeHostScriptAPI) ||
             requirements->min_root_api_size > jube_host_api.node->roots->struct_size)) {
        log_error("JUBE_REG: module '%s' table requirements are not provided by this host",
                  module->name);
        return false;
    }
    if (requirements->min_host_api_version > jube_host_api.api_version ||
            requirements->min_host_api_size > jube_host_api.struct_size ||
            (requirements->required_host_capabilities & ~jube_host_api.capabilities) != 0) {
        // This invariant protects init from seeing a partially compatible host
        // and is therefore checked before any module callback can run.
        log_error("JUBE_REG: module '%s' requirements are not provided by this host", module->name);
        return false;
    }
    return true;
}

extern "C" const JubeTypeBinding* jube_module_type_bindings(const JubeModuleDef* module,
                                                            int32_t* count) {
    if (count) *count = 0;
    size_t end = offsetof(JubeModuleDef, type_binding_count) +
                 sizeof(module->type_binding_count);
    if (!jube_module_has_field(module, end)) return NULL;
    if (count) *count = module->type_binding_count;
    return module->type_bindings;
}

extern "C" void radiant_jube_register_static(void);
extern "C" void dom_jube_register_static(void);
extern "C" void hostobj_demo_jube_register_static(void);

static int jube_find_static_module_index(const char* name) {
    if (!name) return -1;
    size_t name_len = strlen(name);
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (module && module->name &&
            str_eq(module->name, strlen(module->name), name, name_len)) return i;
    }
    return -1;
}

static bool jube_env_flag_enabled(const char* name) {
    const char* value = getenv(name);
    if (!value || !*value) return false;
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0;
}

void jube_set_host_executable_path(const char* executable_path) {
    jube_host_module_root[0] = '\0';
    if (!executable_path || !*executable_path) return;
    const char* slash = strrchr(executable_path, '/');
#if defined(_WIN32)
    const char* backslash = strrchr(executable_path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
    if (!slash) return;
    size_t directory_length = (size_t)(slash - executable_path);
    if (directory_length + sizeof("/modules") > sizeof(jube_host_module_root)) return;
    memcpy(jube_host_module_root, executable_path, directory_length);
    memcpy(jube_host_module_root + directory_length, "/modules", sizeof("/modules"));
}

static void jube_close_dynamic_handle(void* handle) {
#if defined(_WIN32)
    if (handle) FreeLibrary((HMODULE)handle);
#else
    if (handle) dlclose(handle);
#endif
}

static bool jube_specifier_normalize(const char* name, char* out, size_t out_size) {
    if (!name || !*name || !out || out_size == 0) return false;
    const char* cursor = name;
    // node:test is prefix-only in the current compatibility dispatcher; do
    // not make a bare `test` package resolve as a builtin while migrating it.
    if (strncmp(cursor, "node:", 5) == 0 && strcmp(cursor, "node:test") != 0) {
        cursor += 5;
    }
    size_t length = strlen(cursor);
    if (str_ends_with_const(cursor, length, ".js")) length -= 3;
    if (length == 0 || length >= out_size) return false;
    str_copy(out, out_size, cursor, length);
    return true;
}

typedef TypedHashMap<JubeSpecifierEntry,
    HashMapCStrMemberKeyOps<JubeSpecifierEntry, &JubeSpecifierEntry::normalized>>
    JubeSpecifierIndex;

static bool jube_specifier_index_init(void) {
    if (jube_specifier_index) return true;
    jube_specifier_index = JubeSpecifierIndex::create(64,
        0x4a5542455f535045ULL, 0x4349464945525f31ULL);
    if (!jube_specifier_index) {
        log_error("JUBE_SPEC: failed to allocate specifier index");
        return false;
    }
    return true;
}

static bool jube_specifier_entry_upsert(const char* specifier, const char* module_name,
                                        const char* manifest_path,
                                        const JubeModuleDef* module,
                                        int namespace_index, uint32_t state) {
    if (!specifier || !module_name || !*module_name || !jube_specifier_index_init()) return false;
    JubeSpecifierEntry entry = {};
    if (!jube_specifier_normalize(specifier, entry.normalized, sizeof(entry.normalized)) ||
            strlen(module_name) >= sizeof(entry.module_name) ||
            (manifest_path && strlen(manifest_path) >= sizeof(entry.manifest_path))) {
        log_error("JUBE_SPEC: invalid provider '%s' for specifier '%s'", module_name,
                  specifier ? specifier : "(null)");
        return false;
    }
    strcpy(entry.module_name, module_name);
    if (manifest_path) strcpy(entry.manifest_path, manifest_path);
    entry.module = module;
    entry.namespace_index = namespace_index;
    entry.state = state;

    const JubeSpecifierEntry* existing =
        (const JubeSpecifierEntry*)hashmap_get(jube_specifier_index, &entry);
    if (existing) {
        if (strcmp(existing->module_name, entry.module_name) != 0) {
            log_error("JUBE_SPEC: duplicate provider '%s' for '%s' conflicts with '%s'",
                      entry.module_name, entry.normalized, existing->module_name);
            jube_specifier_catalog_failed = true;
            return false;
        }
        // The manifest is module-level metadata. A descriptor must later pin
        // every normalized alias to one namespace builder.
        if (existing->namespace_index >= 0 && namespace_index >= 0 &&
                existing->namespace_index != namespace_index) {
            log_error("JUBE_SPEC: module '%s' maps '%s' to multiple namespaces",
                      entry.module_name, entry.normalized);
            jube_specifier_catalog_failed = true;
            return false;
        }
        // catalog roots are scanned in priority order; later fallback copies
        // must not replace a configured or executable-local manifest.
        if (existing->manifest_path[0]) {
            strcpy(entry.manifest_path, existing->manifest_path);
        }
        if (!entry.module && existing->module) entry.module = existing->module;
        if (entry.namespace_index < 0) entry.namespace_index = existing->namespace_index;
        if (entry.state < existing->state) entry.state = existing->state;
    }
    hashmap_set(jube_specifier_index, &entry);
    if (hashmap_oom(jube_specifier_index)) {
        log_error("JUBE_SPEC: failed to record provider '%s' for '%s'", entry.module_name,
                  entry.normalized);
        return false;
    }
    return true;
}

static int jube_specifier_index_module(const JubeModuleDef* module) {
    if (!module || !module->name) return -1;
    if (!module->namespaces || module->namespace_count <= 0) return 0;
    for (int i = 0; i < module->namespace_count; i++) {
        const JubeNamespaceDef* ns = &module->namespaces[i];
        if (!ns || !ns->specifiers || ns->specifier_count <= 0) continue;
        for (int j = 0; j < ns->specifier_count; j++) {
            if (!ns->specifiers[j] || !ns->specifiers[j][0]) continue;
            if (!jube_specifier_entry_upsert(ns->specifiers[j], module->name, NULL,
                    module, i, JUBE_SPECIFIER_OWNER_ACTIVE)) {
                return -1;
            }
        }
    }
    return 0;
}

static int jube_specifier_descriptor_namespace(const JubeModuleDef* module,
                                               const char* normalized) {
    int found_index = -1;
    if (!module || !normalized || !module->namespaces) return -1;
    for (int i = 0; i < module->namespace_count; i++) {
        const JubeNamespaceDef* ns = &module->namespaces[i];
        if (!ns || !ns->specifiers) continue;
        for (int j = 0; j < ns->specifier_count; j++) {
            char candidate[JUBE_SPECIFIER_NAME_CAPACITY];
            if (!jube_specifier_normalize(ns->specifiers[j], candidate, sizeof(candidate)) ||
                    strcmp(candidate, normalized) != 0) {
                continue;
            }
            if (found_index >= 0 && found_index != i) return -2;
            found_index = i;
        }
    }
    return found_index;
}

static bool jube_specifier_descriptor_matches_catalog(const JubeModuleDef* module) {
    if (!module || !module->name || !jube_specifier_index) return true;
    bool has_manifest_provider = false;
    size_t cursor = 0;
    void* item = NULL;
    while (hashmap_iter(jube_specifier_index, &cursor, &item)) {
        const JubeSpecifierEntry* entry = (const JubeSpecifierEntry*)item;
        if (!entry || strcmp(entry->module_name, module->name) != 0 ||
                !entry->manifest_path[0]) {
            continue;
        }
        has_manifest_provider = true;
        int namespace_index = jube_specifier_descriptor_namespace(module, entry->normalized);
        if (namespace_index < 0) {
            log_error("JUBE_SPEC: descriptor '%s' does not attest manifest provider '%s'",
                      module->name, entry->normalized);
            return false;
        }
    }
    if (!has_manifest_provider) return true;
    for (int i = 0; i < module->namespace_count; i++) {
        const JubeNamespaceDef* ns = &module->namespaces[i];
        if (!ns || !ns->specifiers) continue;
        for (int j = 0; j < ns->specifier_count; j++) {
            JubeSpecifierEntry key = {};
            if (!jube_specifier_normalize(ns->specifiers[j], key.normalized,
                                          sizeof(key.normalized))) {
                return false;
            }
            const JubeSpecifierEntry* entry =
                (const JubeSpecifierEntry*)hashmap_get(jube_specifier_index, &key);
            if (!entry || strcmp(entry->module_name, module->name) != 0 ||
                    !entry->manifest_path[0]) {
                log_error("JUBE_SPEC: descriptor '%s' exposes undeclared specifier '%s'",
                          module->name, key.normalized);
                return false;
            }
        }
    }
    return true;
}

bool jube_specifier_catalog_contains(const char* name) {
    if (!name || !*name || !jube_specifier_catalog_ensure() || !jube_specifier_index) return false;
    JubeSpecifierEntry key = {};
    if (!jube_specifier_normalize(name, key.normalized, sizeof(key.normalized))) return false;
    return hashmap_get(jube_specifier_index, &key) != NULL;
}

bool jube_specifier_is_builtin(const char* name) {
    return jube_specifier_catalog_contains(name);
}

static const JubeSpecifierEntry* jube_specifier_lookup(const char* name,
                                                       JubeSpecifierEntry* key) {
    if (!name || !*name || !jube_specifier_catalog_ensure() || !jube_specifier_index ||
            !key || !jube_specifier_normalize(name, key->normalized, sizeof(key->normalized))) {
        return NULL;
    }
    return (const JubeSpecifierEntry*)hashmap_get(jube_specifier_index, key);
}

JubeSpecifierResolveStatus jube_specifier_resolve_active(const char* name, Item* out_namespace) {
    if (out_namespace) *out_namespace = ItemNull;
    JubeSpecifierEntry key = {};
    const JubeSpecifierEntry* found =
        jube_specifier_lookup(name, &key);
    if (!found) return JUBE_SPECIFIER_UNKNOWN;
    JubeSpecifierEntry entry = *found;
    if (!entry.module) return JUBE_SPECIFIER_UNAVAILABLE;
    // A namespace request is the first observable Jube use in this realm.
    // Do not create the session while ordinary JS globals are being built.
    jube_modules_runtime_attach();
    if (!jube_active_node_runtime_session || !jube_active_node_runtime_session->live) {
        return JUBE_SPECIFIER_ACTIVATION_FAILED;
    }
    if (!jube_activate_module_descriptor(entry.module)) {
        return JUBE_SPECIFIER_ACTIVATION_FAILED;
    }
    if (entry.namespace_index < 0 || entry.namespace_index >= entry.module->namespace_count) {
        return JUBE_SPECIFIER_ACTIVATION_FAILED;
    }
    const JubeNamespaceDef* ns = &entry.module->namespaces[entry.namespace_index];
    if (!ns || !ns->build) return JUBE_SPECIFIER_ACTIVATION_FAILED;
    Item result = ns->build();
    if (out_namespace) *out_namespace = result;
    return get_type_id(result) == LMD_TYPE_NULL ?
        JUBE_SPECIFIER_ACTIVATION_FAILED : JUBE_SPECIFIER_RESOLVED;
}

JubeSpecifierResolveStatus jube_specifier_resolve(const char* name, Item* out_namespace) {
    if (out_namespace) *out_namespace = ItemNull;
    JubeSpecifierResolveStatus active = jube_specifier_resolve_active(name, out_namespace);
    if (active != JUBE_SPECIFIER_UNAVAILABLE) return active;

    JubeSpecifierEntry key = {};
    const JubeSpecifierEntry* found = jube_specifier_lookup(name, &key);
    if (!found) return JUBE_SPECIFIER_UNKNOWN;
    JubeSpecifierEntry entry = *found;
    if (entry.manifest_path[0]) {
        if (jube_load_manifest_path_internal(entry.manifest_path, entry.module_name) != 1) {
            log_error("JUBE_SPEC: module '%s' for '%s' is unavailable", entry.module_name,
                      entry.normalized);
            return JUBE_SPECIFIER_UNAVAILABLE;
        }
        found = (const JubeSpecifierEntry*)hashmap_get(jube_specifier_index, &key);
        if (!found || !found->module) return JUBE_SPECIFIER_ACTIVATION_FAILED;
        entry = *found;
    }
    return jube_specifier_resolve_active(name, out_namespace);
}

bool jube_specifier_index_names(JubeSpecifierNameCallback callback, void* user) {
    if (!callback || !jube_specifier_catalog_ensure() || !jube_specifier_index) return false;
    size_t cursor = 0;
    void* item = NULL;
    while (hashmap_iter(jube_specifier_index, &cursor, &item)) {
        const JubeSpecifierEntry* entry = (const JubeSpecifierEntry*)item;
        if (!entry || !callback(entry->normalized, user)) return false;
    }
    return true;
}

static void jube_install_module_globals(const JubeModuleDef* module, void* session) {
    if (!module || !session || !jube_host_node_session_is_live(session) ||
            !jube_host_api.node || !jube_host_api.node->roots || !jube_host_api.value ||
            !jube_host_api.script || !jube_host_api.node->roots->root_frame_begin ||
            !jube_host_api.node->roots->root_frame_take_slot ||
            !jube_host_api.node->roots->root_frame_end ||
            !jube_host_api.value->string_from_utf8_n ||
            !jube_host_api.value->property_set_own ||
            !jube_host_api.value->property_get_own_data ||
            !jube_host_api.script->mark_non_enumerable) {
        return;
    }
    int32_t global_count = 0;
    const JubeGlobalDef* globals = jube_module_globals(module, &global_count);
    if (!globals || global_count <= 0) return;
    JubeRootFrame frame = {};
    if (!jube_host_api.node->roots->root_frame_begin(&frame, 3)) return;
    uint64_t* global_root = jube_host_api.node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = jube_host_api.node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = jube_host_api.node->roots->root_frame_take_slot(&frame);
    if (!global_root || !key_root || !value_root) {
        jube_host_api.node->roots->root_frame_end(&frame);
        return;
    }
    *global_root = jube_host_node_session_global_this(session).item;
    for (int i = 0; i < global_count; i++) {
        const JubeGlobalDef* global_def = &globals[i];
        if (!global_def || !global_def->name || !global_def->name[0] || !global_def->build) continue;
        Item key = jube_host_api.value->string_from_utf8_n(global_def->name,
            strlen(global_def->name));
        *key_root = key.item;
        Item existing = ItemNull;
        if (jube_host_api.value->property_get_own_data(
                (Item){.item = *global_root}, (Item){.item = *key_root}, &existing) &&
                existing.item != ITEM_JS_LAZY_GLOBAL_SENTINEL) {
            // A script may replace a writable lazy global before first read.
            // Activating another surface of the module must preserve that write.
            continue;
        }
        // The builder may allocate before its value is published on globalThis.
        *value_root = global_def->build(session).item;
        jube_host_api.value->property_set_own((Item){.item = *global_root},
            (Item){.item = *key_root}, (Item){.item = *value_root});
        jube_host_api.script->mark_non_enumerable((Item){.item = *global_root},
            (Item){.item = *key_root});
    }
    jube_host_api.node->roots->root_frame_end(&frame);
}

static JubeStaticModuleEntry* jube_module_entry(const JubeModuleDef* module) {
    if (!module) return NULL;
    for (int i = 0; i < jube_static_modules_count; i++) {
        if (jube_static_modules[i].module == module) return &jube_static_modules[i];
    }
    return NULL;
}

static bool jube_module_is_attached_to_session(const JubeStaticModuleEntry* entry,
                                                const NodeRuntimeSession* session) {
    if (!entry || !session) return false;
    jube_runtime_session_lock_acquire();
    bool attached = false;
    if (entry->attached_sessions) {
        for (int i = 0; i < entry->attached_sessions->length; i++) {
            if (entry->attached_sessions->data[i] == session) {
                attached = true;
                break;
            }
        }
    }
    jube_runtime_session_lock_release();
    return attached;
}

static bool jube_module_attach_session(JubeStaticModuleEntry* entry,
                                       NodeRuntimeSession* session, bool* out_new_attachment) {
    if (out_new_attachment) *out_new_attachment = false;
    if (!entry || !session) return false;
    jube_runtime_session_lock_acquire();
    for (int i = 0; entry->attached_sessions && i < entry->attached_sessions->length; i++) {
        if (entry->attached_sessions->data[i] == session) {
            jube_runtime_session_lock_release();
            return true;
        }
    }
    if (!entry->attached_sessions) {
        entry->attached_sessions = arraylist_new(4);
        if (!entry->attached_sessions) {
            jube_runtime_session_lock_release();
            return false;
        }
    }
    bool appended = arraylist_append(entry->attached_sessions, session);
    if (appended && out_new_attachment) *out_new_attachment = true;
    jube_runtime_session_lock_release();
    return appended;
}

static void jube_module_detach_session(JubeStaticModuleEntry* entry,
                                       NodeRuntimeSession* session) {
    if (!entry || !session) return;
    jube_runtime_session_lock_acquire();
    if (entry->attached_sessions) {
        for (int i = 0; i < entry->attached_sessions->length; i++) {
            if (entry->attached_sessions->data[i] == session) {
                arraylist_remove(entry->attached_sessions, i);
                break;
            }
        }
    }
    jube_runtime_session_lock_release();
}

static bool jube_attach_module_to_active_runtime(JubeStaticModuleEntry* entry) {
    if (!entry || entry->activation_state != JUBE_MODULE_ACTIVE) return false;
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    if (!session || !session->live || session->detaching) return true;
    // Mark the generation before invoking module code so a namespace/global
    // builder that re-enters the registry cannot attach the same module twice.
    bool new_attachment = false;
    if (!jube_module_attach_session(entry, session, &new_attachment)) return false;
    if (!new_attachment) return true;
    const JubeModuleDef* module = entry->module;
    if (module && module->name && strcmp(module->name, "node-core") == 0) {
        jube_node_shared_primitives_attach(session);
    }
    size_t attach_end = offsetof(JubeModuleDef, runtime_attach) +
        sizeof(((JubeModuleDef*)NULL)->runtime_attach);
    if (jube_module_has_field(module, attach_end) && module->runtime_attach) {
        module->runtime_attach(session);
    }
    jube_install_module_globals(module, session);
    return true;
}

static bool jube_activate_module_dependencies(const JubeModuleDef* module) {
    size_t dependencies_end = offsetof(JubeModuleDef, dependency_count) +
        sizeof(((JubeModuleDef*)NULL)->dependency_count);
    if (!jube_module_has_field(module, dependencies_end) || !module->dependencies ||
            module->dependency_count <= 0) return true;
    for (int32_t i = 0; i < module->dependency_count; i++) {
        const char* dependency_name = module->dependencies[i];
        if (!dependency_name || !dependency_name[0] ||
                (module->name && strcmp(dependency_name, module->name) == 0)) {
            log_error("JUBE_REG: module '%s' has an invalid dependency edge",
                      module && module->name ? module->name : "(unknown)");
            return false;
        }
        const JubeModuleDef* dependency = jube_find_static_module(dependency_name);
        if (!dependency || !jube_activate_module_descriptor(dependency)) {
            // Static profiles have no manifest-load step, so descriptor edges
            // must activate the dependency before its consumer attaches.
            log_error("JUBE_REG: module '%s' dependency '%s' is unavailable or inactive",
                      module->name, dependency_name);
            return false;
        }
    }
    return true;
}

static bool jube_activate_module_descriptor(const JubeModuleDef* module) {
    JubeStaticModuleEntry* entry = jube_module_entry(module);
    if (!entry) return false;
    jube_runtime_session_lock_acquire();
    uv_thread_t self = uv_thread_self();
    while (entry->activation_state == JUBE_MODULE_ACTIVATING) {
        // a cycle is re-entry on the activating thread. Comparing EvalContexts
        // misfired: parallel AST prebuild workers all have a NULL context, so a
        // worker racing another's activation took it for a cycle (E216).
        if (uv_thread_equal(&entry->activating_thread, &self)) {
            jube_runtime_session_lock_release();
            log_error("JUBE_REG: cyclic activation of module '%s'", module->name);
            return false;
        }
        // Module initialization is a cold publication boundary. Waiters only
        // observe the final immutable descriptor, never a half-built table.
        uv_cond_wait(&jube_runtime_activation_cond, &jube_runtime_session_lock);
    }
    if (entry->activation_state == JUBE_MODULE_ACTIVE) {
        jube_runtime_session_lock_release();
        return jube_attach_module_to_active_runtime(entry);
    }
    if (entry->activation_state == JUBE_MODULE_FAILED) {
        jube_runtime_session_lock_release();
        return false;
    }

    // Descriptor registration is intentionally callback-free. This single
    // transition protects every import/global/language/type activation path.
    entry->activation_state = JUBE_MODULE_ACTIVATING;
    entry->activating_thread = self;
    jube_runtime_session_lock_release();

    if (!jube_activate_module_dependencies(module)) {
        jube_runtime_session_lock_acquire();
        entry->activation_state = JUBE_MODULE_FAILED;
        uv_cond_broadcast(&jube_runtime_activation_cond);
        jube_runtime_session_lock_release();
        return false;
    }
    bool shared_primitives = module->name && strcmp(module->name, "node-core") == 0;
    if (shared_primitives && jube_node_shared_primitives_init() != 0) {
        jube_runtime_session_lock_acquire();
        entry->activation_state = JUBE_MODULE_FAILED;
        uv_cond_broadcast(&jube_runtime_activation_cond);
        jube_runtime_session_lock_release();
        return false;
    }
    if (module->init) {
        int rc = module->init(&jube_host_api);
        if (rc != 0) {
            log_error("JUBE_REG: module '%s' init failed with code %d", module->name, rc);
            if (module->shutdown) module->shutdown();
            if (shared_primitives) jube_node_shared_primitives_shutdown();
            jube_runtime_session_lock_acquire();
            entry->activation_state = JUBE_MODULE_FAILED;
            uv_cond_broadcast(&jube_runtime_activation_cond);
            jube_runtime_session_lock_release();
            return false;
        }
    }
    if (jube_compile_module_interface(module) != 0) {
        log_error("JUBE_REG: module '%s' interface compilation failed", module->name);
        if (module->shutdown) module->shutdown();
        if (shared_primitives) jube_node_shared_primitives_shutdown();
        jube_interface_remove_module(module);
        jube_runtime_session_lock_acquire();
        entry->activation_state = JUBE_MODULE_FAILED;
        uv_cond_broadcast(&jube_runtime_activation_cond);
        jube_runtime_session_lock_release();
        return false;
    }

    jube_runtime_session_lock_acquire();
    entry->activation_state = JUBE_MODULE_ACTIVE;
    uv_cond_broadcast(&jube_runtime_activation_cond);
    jube_runtime_session_lock_release();
    log_info("JUBE_REG: activated module '%s' version '%s'", module->name,
             module->version ? module->version : "(none)");
    return jube_attach_module_to_active_runtime(entry);
}

bool jube_activate_module(const JubeModuleDef* module) {
    return jube_activate_module_descriptor(module);
}

bool jube_resolve_global(const char* name, size_t name_length, Item* out_value) {
    if (out_value) *out_value = ItemNull;
    if (!name || name_length == 0) {
        return false;
    }
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        int32_t global_count = 0;
        const JubeGlobalDef* globals = jube_module_globals(module, &global_count);
        for (int j = 0; globals && j < global_count; j++) {
            const JubeGlobalDef* global_def = &globals[j];
            if (!global_def->name || strlen(global_def->name) != name_length ||
                    memcmp(global_def->name, name, name_length) != 0) {
                continue;
            }
            // The lazy global slot proves that this realm requested Jube.
            // Avoid session creation for realms that never read a Jube global.
            jube_modules_runtime_attach();
            if (!jube_active_node_runtime_session || !jube_active_node_runtime_session->live) {
                return false;
            }
            if (!jube_activate_module_descriptor(module)) return false;
            Item key = jube_host_api.value->string_from_utf8_n(name, name_length);
            Item value = ItemNull;
            Item global = jube_host_node_session_global_this(jube_active_node_runtime_session);
            if (!jube_host_api.value->property_get_own_data(global, key, &value) ||
                    value.item == ITEM_JS_LAZY_GLOBAL_SENTINEL) {
                return false;
            }
            if (out_value) *out_value = value;
            return true;
        }
    }
    return false;
}

static int jube_register_module_descriptor(const JubeModuleDef* module, void* dynamic_handle,
                                           const char* source_label) {
    if (!module || !module->name) {
        log_error("JUBE_REG: cannot register null %s module", source_label ? source_label : "Jube");
        return -1;
    }
    if (module->abi_version != JUBE_ABI_VERSION &&
            module->abi_version != JUBE_ABI_VERSION_LEGACY) {
        log_error("JUBE_REG: module '%s' ABI mismatch: got %u expected %u",
                  module->name, module->abi_version, JUBE_ABI_VERSION);
        return -1;
    }
    if (module->abi_version == JUBE_ABI_VERSION_LEGACY) {
        // v1 lacks scoped native handles. Re-enable only after the guest ABI
        // can publish exact roots for every live Item across MAY_GC calls.
        log_error("JUBE_REG: module '%s' uses retired legacy rooting ABI", module->name);
        return -1;
    }
    // v1 modules stop at JUBE_MODULE_DEF_V1_SIZE; the DOM3 tail is additive and
    // size-gated, so only the frozen v1 prefix is a hard requirement here.
    if (module->struct_size < JUBE_MODULE_DEF_V1_SIZE) {
        log_error("JUBE_REG: module '%s' descriptor is too small: got %u expected %zu",
                  module->name, module->struct_size, (size_t)JUBE_MODULE_DEF_V1_SIZE);
        return -1;
    }
    int existing = jube_find_static_module_index(module->name);
    if (existing >= 0) {
        log_trace("JUBE_REG: %s module '%s' already registered",
                  source_label ? source_label : "Jube", module->name);
        jube_close_dynamic_handle(dynamic_handle);
        return 0;
    }
    if (!jube_module_requirements_are_supported(module)) {
        jube_close_dynamic_handle(dynamic_handle);
        return -1;
    }
    if (!jube_specifier_descriptor_matches_catalog(module)) {
        // Manifest ownership is checked before init so an impersonating image
        // cannot execute callbacks merely by sharing a module name.
        jube_close_dynamic_handle(dynamic_handle);
        return -1;
    }
    if (jube_static_modules_count >= JUBE_STATIC_MODULE_CAPACITY) {
        log_error("JUBE_REG: module capacity exceeded while registering '%s'", module->name);
        jube_close_dynamic_handle(dynamic_handle);
        return -1;
    }

    int slot = jube_static_modules_count++;
    jube_static_modules[slot].module = module;
    jube_static_modules[slot].activation_state = JUBE_MODULE_REGISTERED;
    jube_static_modules[slot].attached_sessions = NULL;
    // Dynamic descriptors and function tables live in the loaded image, so keep the handle open.
    jube_static_modules[slot].dynamic_handle = dynamic_handle;

    if (jube_specifier_index_module(module) != 0) {
        // A descriptor is not visible until every namespace alias has one
        // unambiguous provider in the import catalog.
        log_error("JUBE_SPEC: %s module '%s' has an invalid specifier set",
                  source_label ? source_label : "Jube", module->name);
        if (module->shutdown) module->shutdown();
        jube_interface_remove_module(module);
        jube_static_modules_count--;
        jube_static_modules[slot].module = NULL;
        jube_static_modules[slot].activation_state = JUBE_MODULE_REGISTERED;
        jube_static_modules[slot].attached_sessions = NULL;
        jube_static_modules[slot].dynamic_handle = NULL;
        jube_close_dynamic_handle(dynamic_handle);
        return -1;
    }

    log_info("JUBE_REG: cataloged %s module '%s' version '%s'",
             source_label ? source_label : "Jube", module->name,
             module->version ? module->version : "(none)");
    return 0;
}

static void jube_specifier_forget_module(const JubeModuleDef* module) {
    if (!module || !jube_specifier_index) return;
    for (;;) {
        JubeSpecifierEntry changed = {};
        bool found = false;
        size_t cursor = 0;
        void* item = NULL;
        while (hashmap_iter(jube_specifier_index, &cursor, &item)) {
            const JubeSpecifierEntry* entry = (const JubeSpecifierEntry*)item;
            if (entry && entry->module == module) {
                changed = *entry;
                found = true;
                break;
            }
        }
        if (!found) return;
        if (changed.manifest_path[0]) {
            changed.module = NULL;
            changed.namespace_index = -1;
            changed.state = JUBE_SPECIFIER_OWNER_CATALOGED;
            hashmap_set(jube_specifier_index, &changed);
        } else {
            hashmap_delete(jube_specifier_index, &changed);
        }
    }
}

static void jube_rollback_registered_modules(int first_index) {
    if (first_index < 0) first_index = 0;
    while (jube_static_modules_count > first_index) {
        int index = --jube_static_modules_count;
        JubeStaticModuleEntry entry = jube_static_modules[index];
        jube_static_modules[index].module = NULL;
        jube_static_modules[index].activation_state = JUBE_MODULE_REGISTERED;
        if (jube_static_modules[index].attached_sessions) {
            arraylist_free(jube_static_modules[index].attached_sessions);
            jube_static_modules[index].attached_sessions = NULL;
        }
        jube_static_modules[index].dynamic_handle = NULL;
        if (!entry.module) continue;
        // Dependency loading is transactional: remove every host-side record
        // before closing the image that owns its descriptor and callbacks.
        jube_specifier_forget_module(entry.module);
        jube_interface_remove_module(entry.module);
        if (entry.activation_state == JUBE_MODULE_ACTIVE && entry.module->shutdown) {
            entry.module->shutdown();
        }
        jube_close_dynamic_handle(entry.dynamic_handle);
    }
}

int jube_register_static_module(const JubeModuleDef* module) {
    return jube_register_module_descriptor(module, NULL, "static");
}

typedef const JubeModuleDef* (*JubeDynamicModuleEntry)(void);

static int jube_load_dynamic_module_checked(const char* path, const char* entry_symbol,
                                            const char* expected_name,
                                            const char* expected_version) {
    if (!path || !*path) {
        log_error("JUBE_REG: dynamic module path is empty");
        return -1;
    }
    JubeDynamicModuleEntry entry = NULL;
    void* handle = NULL;
#if defined(_WIN32)
    const char* symbol = (entry_symbol && *entry_symbol) ? entry_symbol : "jube_module";
    HMODULE windows_handle = LoadLibraryA(path);
    if (!windows_handle) {
        log_error("JUBE_REG: LoadLibrary failed for '%s' (error %lu)", path,
                  (unsigned long)GetLastError());
        return -1;
    }
    handle = (void*)windows_handle;
    FARPROC raw_entry = GetProcAddress(windows_handle, symbol);
    if (!raw_entry) {
        log_error("JUBE_REG: GetProcAddress failed for '%s' in '%s' (error %lu)",
                  symbol, path, (unsigned long)GetLastError());
        jube_close_dynamic_handle(handle);
        return -1;
    }
    entry = (JubeDynamicModuleEntry)(void*)raw_entry;
#else
    const char* symbol = (entry_symbol && *entry_symbol) ? entry_symbol : "jube_module";
    handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        log_error("JUBE_REG: dlopen failed for '%s': %s", path, dlerror());
        return -1;
    }

    dlerror();
    entry = (JubeDynamicModuleEntry)dlsym(handle, symbol);
    const char* error = dlerror();
    if (error || !entry) {
        log_error("JUBE_REG: dlsym failed for '%s' in '%s': %s",
                  symbol, path, error ? error : "entry not found");
        jube_close_dynamic_handle(handle);
        return -1;
    }
#endif
    const JubeModuleDef* module = entry();
    if (!module) {
        log_error("JUBE_REG: dynamic module '%s' returned a null descriptor", path);
        jube_close_dynamic_handle(handle);
        return -1;
    }
    if ((expected_name && (!module->name || strcmp(expected_name, module->name) != 0)) ||
        (expected_version && (!module->version ||
            strcmp(expected_version, module->version) != 0))) {
        log_error("JUBE_REG: descriptor from '%s' does not match its manifest identity", path);
        jube_close_dynamic_handle(handle);
        return -1;
    }
    char module_name[JUBE_SPECIFIER_MODULE_NAME_CAPACITY];
    if (!module->name || strlen(module->name) >= sizeof(module_name)) {
        log_error("JUBE_REG: dynamic module from '%s' has an invalid name", path);
        jube_close_dynamic_handle(handle);
        return -1;
    }
    strcpy(module_name, module->name);
    int transaction_start = jube_static_modules_count;
    if (jube_register_module_descriptor(module, handle, "dynamic") != 0) return -1;
    int index = jube_find_static_module_index(module_name);
    if (index < 0 ||
            !jube_activate_module_descriptor(jube_static_modules[index].module)) {
        // Explicit/demand-selected dynamic loads remain synchronous, but share
        // the same lazy activation gate used by resident static descriptors.
        jube_rollback_registered_modules(transaction_start);
        return -1;
    }
    return 0;
}

int jube_load_dynamic_module(const char* path, const char* entry_symbol) {
    return jube_load_dynamic_module_checked(path, entry_symbol, NULL, NULL);
}

static bool jube_manifest_read_file(const char* path, char** out_text) {
    if (out_text) *out_text = NULL;
    if (!path || !out_text) return false;
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length < 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    char* text = (char*)malloc((size_t)length + 1);
    if (!text) {
        fclose(file);
        return false;
    }
    size_t read_length = fread(text, 1, (size_t)length, file);
    fclose(file);
    if (read_length != (size_t)length) {
        free(text);
        return false;
    }
    text[length] = '\0';
    *out_text = text;
    return true;
}

static bool jube_manifest_string(const char* text, const char* key,
                                 char* out, size_t out_size) {
    if (!text || !key || !out || out_size == 0) return false;
    char quoted_key[128];
    int key_length = snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
    if (key_length <= 0 || (size_t)key_length >= sizeof(quoted_key)) return false;
    const char* cursor = strstr(text, quoted_key);
    if (!cursor) return false;
    cursor += key_length;
    cursor = str_skip_ascii_space(cursor);
    if (*cursor != ':') return false;
    cursor++;
    cursor = str_skip_ascii_space(cursor);
    if (*cursor != '\"') return false;
    cursor++;
    const char* end = str_scan_until_any(cursor, "\"\\\\");
    if (*end != '\"' || (size_t)(end - cursor) >= out_size) return false;
    str_copy(out, out_size, cursor, (size_t)(end - cursor));
    return true;
}

static bool jube_manifest_array_contains(const char* text, const char* key,
                                         const char* value) {
    if (!text || !key || !value) return false;
    char quoted_key[128];
    int key_length = snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
    if (key_length <= 0 || (size_t)key_length >= sizeof(quoted_key)) return false;
    const char* cursor = strstr(text, quoted_key);
    if (!cursor) return false;
    cursor += key_length;
    cursor = str_skip_ascii_space(cursor);
    if (*cursor != ':') return false;
    cursor++;
    cursor = str_skip_ascii_space(cursor);
    if (*cursor != '[') return false;
    cursor++;
    while (*cursor && *cursor != ']') {
        cursor = str_skip_chars(cursor, " \t\r\n,");
        if (*cursor != '\"') return false;
        cursor++;
        const char* end = str_scan_until_any(cursor, "\"\\\\");
        if (*end != '\"') return false;
        // Discovery precedes descriptor registration, so aliases and source
        // extensions must normalize here as well as in the loaded registry.
        if (str_ieq_const(cursor, (size_t)(end - cursor), value)) return true;
        cursor = end + 1;
    }
    return false;
}

static bool jube_manifest_string_array(const char* text, const char* key,
                                       char values[][128], int capacity, int* out_count) {
    if (out_count) *out_count = 0;
    if (!text || !key || !values || capacity <= 0 || !out_count) return false;
    char quoted_key[128];
    int key_length = snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
    if (key_length <= 0 || (size_t)key_length >= sizeof(quoted_key)) return false;
    const char* cursor = strstr(text, quoted_key);
    if (!cursor) return true;
    cursor += key_length;
    cursor = str_skip_ascii_space(cursor);
    if (*cursor != ':') return false;
    cursor++;
    cursor = str_skip_ascii_space(cursor);
    if (*cursor != '[') return false;
    cursor++;
    while (*cursor && *cursor != ']') {
        cursor = str_skip_chars(cursor, " \t\r\n,");
        if (*cursor == ']') break;
        if (*cursor != '\"' || *out_count >= capacity) return false;
        cursor++;
        const char* end = str_scan_until_any(cursor, "\"\\\\");
        size_t value_length = (size_t)(end - cursor);
        if (*end != '\"' || value_length == 0 || value_length >= sizeof(values[0])) {
            return false;
        }
        str_copy(values[*out_count], sizeof(values[0]), cursor, value_length);
        (*out_count)++;
        cursor = end + 1;
    }
    return *cursor == ']';
}

static bool jube_manifest_name_matches(const char* text, const char* expected_name) {
    char module_name[128];
    return text && expected_name &&
        jube_manifest_string(text, "name", module_name, sizeof(module_name)) &&
        strcmp(module_name, expected_name) == 0;
}

static bool jube_manifest_resource_path_is_safe(const char* path) {
    if (!path || !*path || path[0] == '/' || path[0] == '\\') return false;
    const char* component = path;
    for (const char* cursor = path;; cursor++) {
        if (*cursor == ':' || *cursor == '\0' || *cursor == '/' || *cursor == '\\') {
            size_t component_length = (size_t)(cursor - component);
            if (component_length == 0 ||
                (component_length == 1 && component[0] == '.') ||
                (component_length == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if (*cursor == '\0') return true;
            component = cursor + 1;
        }
    }
}

static bool jube_manifest_path_has_name(const char* manifest_path, const char* expected_name) {
    char* text = NULL;
    if (!jube_manifest_read_file(manifest_path, &text)) return false;
    bool matches = jube_manifest_name_matches(text, expected_name);
    free(text);
    return matches;
}

static bool jube_find_sibling_manifest(const char* manifest_path, const char* module_name,
                                       char* out_path, size_t out_size) {
    if (!manifest_path || !module_name || !*module_name || !out_path || out_size == 0) {
        return false;
    }
    const char* slash = strrchr(manifest_path, '/');
    if (!slash) return false;
    size_t module_dir_length = (size_t)(slash - manifest_path);
    if (module_dir_length == 0 || module_dir_length >= JUBE_MANIFEST_PATH_CAPACITY) return false;
    char module_dir[JUBE_MANIFEST_PATH_CAPACITY];
    str_copy(module_dir, sizeof(module_dir), manifest_path, module_dir_length);
    char* parent_slash = strrchr(module_dir, '/');
    if (parent_slash) *parent_slash = '\0';
    const char* root = module_dir[0] ? module_dir : "/";
    char candidate[JUBE_MANIFEST_PATH_CAPACITY];
    int written = snprintf(candidate, sizeof(candidate), "%s/module.json", root);
    if (written > 0 && (size_t)written < sizeof(candidate) &&
        jube_manifest_path_has_name(candidate, module_name)) {
        if (strlen(candidate) >= out_size) return false;
        strcpy(out_path, candidate);
        return true;
    }

    char selected[JUBE_MANIFEST_PATH_CAPACITY] = {};
#if defined(_WIN32)
    char search_path[JUBE_MANIFEST_PATH_CAPACITY];
    written = snprintf(search_path, sizeof(search_path), "%s\\*", root);
    if (written <= 0 || (size_t)written >= sizeof(search_path)) return false;
    WIN32_FIND_DATAA entry;
    HANDLE find_handle = FindFirstFileA(search_path, &entry);
    if (find_handle == INVALID_HANDLE_VALUE) return false;
    do {
        if (entry.cFileName[0] == '.' || !(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        written = snprintf(candidate, sizeof(candidate), "%s/%s/module.json", root,
                           entry.cFileName);
        if (written <= 0 || (size_t)written >= sizeof(candidate) ||
            !jube_manifest_path_has_name(candidate, module_name)) continue;
        if (!selected[0] || strcmp(candidate, selected) < 0) strcpy(selected, candidate);
    } while (FindNextFileA(find_handle, &entry));
    FindClose(find_handle);
#else
    DIR* dir = opendir(root);
    if (!dir) return false;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        written = snprintf(candidate, sizeof(candidate), "%s/%s/module.json", root,
                           entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(candidate) ||
            !jube_manifest_path_has_name(candidate, module_name)) continue;
        if (!selected[0] || strcmp(candidate, selected) < 0) strcpy(selected, candidate);
    }
    closedir(dir);
#endif
    if (!selected[0] || strlen(selected) >= out_size) return false;
    strcpy(out_path, selected);
    return true;
}

static bool jube_manifest_uint32(const char* text, const char* key, uint32_t* out) {
    if (out) *out = 0;
    if (!text || !key || !out) return false;
    char quoted_key[128];
    int key_length = snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
    if (key_length <= 0 || (size_t)key_length >= sizeof(quoted_key)) return false;
    const char* cursor = strstr(text, quoted_key);
    if (!cursor) return false;
    cursor += key_length;
    cursor = str_skip_ascii_space(cursor);
    if (*cursor != ':') return false;
    cursor++;
    cursor = str_skip_ascii_space(cursor);
    char* end = NULL;
    unsigned long value = strtoul(cursor, &end, 10);
    if (end == cursor || value > UINT32_MAX) return false;
    *out = (uint32_t)value;
    return true;
}

static const char* jube_manifest_library_key(void) {
#if defined(_WIN32)
    return "library_windows";
#elif defined(__APPLE__)
    return "library_macos";
#else
    return "library_linux";
#endif
}

static const char* jube_manifest_integrity_key(void) {
#if defined(_WIN32)
    return "sha256_windows";
#elif defined(__APPLE__)
    return "sha256_macos";
#else
    return "sha256_linux";
#endif
}

static bool jube_manifest_verify_library_sha256(const char* library_path,
                                                const char* expected_hex) {
    if (!library_path || !expected_hex || strlen(expected_hex) != 64) return false;
    FILE* file = fopen(library_path, "rb");
    if (!file) return false;
    DigestCtx* digest = digest_ctx_new(DIGEST_SHA256);
    if (!digest) {
        fclose(file);
        return false;
    }
    bool ok = true;
    unsigned char buffer[64 * 1024];
    while (ok) {
        size_t read_count = fread(buffer, 1, sizeof(buffer), file);
        if (read_count > 0) ok = digest_update(digest, buffer, read_count);
        if (read_count < sizeof(buffer)) {
            if (ferror(file)) ok = false;
            break;
        }
    }
    unsigned char digest_bytes[32];
    char digest_hex[65];
    ok = ok && digest_finalize(digest, digest_bytes, sizeof(digest_bytes));
    digest_ctx_free(digest);
    fclose(file);
    if (!ok) return false;
    hex_encode(digest_bytes, sizeof(digest_bytes), digest_hex);
    return strcmp(digest_hex, expected_hex) == 0;
}

// Returns 1 when this manifest was loaded, 0 when it does not name the
// expected module, and -1 when a selected manifest or dependency failed.
static int jube_load_manifest_path_internal(const char* manifest_path,
                                            const char* expected_name) {
    char* text = NULL;
    // Roots may contain only per-module subdirectories; a missing direct
    // module.json is not a selected-manifest failure and must not mask them.
    if (!jube_manifest_read_file(manifest_path, &text)) return 0;
    bool selected = jube_manifest_name_matches(text, expected_name);
    if (!selected) {
        free(text);
        return 0;
    }
    if (jube_manifest_loading_depth >= JUBE_MANIFEST_DEPENDENCY_CAPACITY) {
        log_error("JUBE_REG: dependency depth exceeded at manifest '%s'", manifest_path);
        free(text);
        return -1;
    }
    for (int i = 0; i < jube_manifest_loading_depth; i++) {
        if (strcmp(jube_manifest_loading_paths[i], manifest_path) == 0) {
            log_error("JUBE_REG: dependency cycle includes manifest '%s'", manifest_path);
            free(text);
            return -1;
        }
    }
    if (strlen(manifest_path) >= sizeof(jube_manifest_loading_paths[0])) {
        free(text);
        return -1;
    }
    strcpy(jube_manifest_loading_paths[jube_manifest_loading_depth++], manifest_path);
    char library[512];
    char entry[128];
    char module_name[128];
    char module_version[128];
    char manifest_digest[65];
    uint32_t base_abi = 0;
    uint32_t hosted_api = 0;
    bool has_library = jube_manifest_string(text, jube_manifest_library_key(), library,
                                            sizeof(library));
    bool has_entry = jube_manifest_string(text, "entry_symbol", entry, sizeof(entry));
    bool has_digest = jube_manifest_string(text, jube_manifest_integrity_key(), manifest_digest,
                                           sizeof(manifest_digest));
    bool valid = has_library &&
        jube_manifest_string(text, "name", module_name, sizeof(module_name)) &&
        jube_manifest_string(text, "version", module_version, sizeof(module_version)) &&
        jube_manifest_uint32(text, "base_abi_version", &base_abi) &&
        jube_manifest_uint32(text, "hosted_api_version", &hosted_api) &&
        // a manifest negotiates the descriptor ABI and the host service API (D7.3.2)
        base_abi == JUBE_ABI_VERSION && hosted_api == JUBE_HOST_API_VERSION;
    if (!valid) {
        log_error("JUBE_REG: manifest '%s' is malformed or incompatible", manifest_path);
        free(text);
        jube_manifest_loading_depth--;
        return -1;
    }
    char dependencies[JUBE_MANIFEST_DEPENDENCY_CAPACITY][128];
    int dependency_count = 0;
    if (!jube_manifest_string_array(text, "dependencies", dependencies,
                                    JUBE_MANIFEST_DEPENDENCY_CAPACITY, &dependency_count)) {
        log_error("JUBE_REG: manifest '%s' has malformed dependencies", manifest_path);
        free(text);
        jube_manifest_loading_depth--;
        return -1;
    }
    char resources[JUBE_MANIFEST_DEPENDENCY_CAPACITY][128];
    int resource_count = 0;
    if (!jube_manifest_string_array(text, "resources", resources,
                                    JUBE_MANIFEST_DEPENDENCY_CAPACITY, &resource_count)) {
        log_error("JUBE_REG: manifest '%s' has malformed resources", manifest_path);
        free(text);
        jube_manifest_loading_depth--;
        return -1;
    }
    for (int i = 0; i < resource_count; i++) {
        if (!jube_manifest_resource_path_is_safe(resources[i])) {
            log_error("JUBE_REG: manifest '%s' has unsafe resource path '%s'",
                      manifest_path, resources[i]);
            free(text);
            jube_manifest_loading_depth--;
            return -1;
        }
    }
    const char* slash = strrchr(manifest_path, '/');
    if (!slash) {
        free(text);
        jube_manifest_loading_depth--;
        return -1;
    }
    size_t dir_length = (size_t)(slash - manifest_path);
    char library_path[1024];
    if (dir_length + 1 + strlen(library) >= sizeof(library_path)) {
        log_error("JUBE_REG: library path is too long for manifest '%s'", manifest_path);
        free(text);
        jube_manifest_loading_depth--;
        return -1;
    }
    memcpy(library_path, manifest_path, dir_length);
    library_path[dir_length] = '/';
    strcpy(library_path + dir_length + 1, library);
    if (has_digest && !jube_manifest_verify_library_sha256(library_path, manifest_digest)) {
        log_error("JUBE_REG: library integrity check failed for '%s'", library_path);
        free(text);
        jube_manifest_loading_depth--;
        return -1;
    }
    int transaction_start = jube_static_modules_count;
    for (int i = 0; i < dependency_count; i++) {
        const char* dependency_name = dependencies[i];
        if (strcmp(dependency_name, module_name) == 0) {
            log_error("JUBE_REG: manifest '%s' depends on itself", manifest_path);
            jube_rollback_registered_modules(transaction_start);
            free(text);
            jube_manifest_loading_depth--;
            return -1;
        }
        int dependency_index = jube_find_static_module_index(dependency_name);
        if (dependency_index < 0) {
            char dependency_manifest[JUBE_MANIFEST_PATH_CAPACITY];
            if (!jube_find_sibling_manifest(manifest_path, dependency_name,
                                            dependency_manifest, sizeof(dependency_manifest)) ||
                jube_load_manifest_path_internal(dependency_manifest, dependency_name) != 1) {
                log_error("JUBE_REG: manifest '%s' dependency '%s' is unavailable or failed",
                          manifest_path, dependency_name);
                jube_rollback_registered_modules(transaction_start);
                free(text);
                jube_manifest_loading_depth--;
                return -1;
            }
            dependency_index = jube_find_static_module_index(dependency_name);
        }
        if (dependency_index < 0 || !jube_activate_module_descriptor(
                jube_static_modules[dependency_index].module)) {
            // Loading a descriptor is insufficient: its runtime_attach hook
            // must run before a dependent publishes Node-visible objects.
            log_error("JUBE_REG: manifest '%s' dependency '%s' is unavailable or failed",
                      manifest_path, dependency_name);
            jube_rollback_registered_modules(transaction_start);
            free(text);
            jube_manifest_loading_depth--;
            return -1;
        }
    }
    free(text);
    // A selected but unavailable module is still a handled discovery result:
    // callers can issue a useful missing-language diagnostic instead of trying
    // to parse the guest source as Lambda.
    int rc = jube_load_dynamic_module_checked(library_path, has_entry ? entry : NULL,
                                              module_name, module_version);
    if (rc != 0) jube_rollback_registered_modules(transaction_start);
    jube_manifest_loading_depth--;
    return rc == 0 ? 1 : -1;
}

// RDB provider index (RDB4): `rdb:<driver>` entries from engine "rdb"
// manifests. It is deliberately separate from the specifier index, so no
// script import can resolve or activate a driver module.
#define JUBE_RDB_PROVIDER_CAPACITY 16
#define JUBE_RDB_DRIVER_NAME_CAPACITY 64

struct JubeRdbProvider {
    char driver[JUBE_RDB_DRIVER_NAME_CAPACITY];
    char module_name[JUBE_SPECIFIER_MODULE_NAME_CAPACITY];
    char manifest_path[JUBE_MANIFEST_PATH_CAPACITY];
};

static JubeRdbProvider jube_rdb_providers[JUBE_RDB_PROVIDER_CAPACITY];
static int jube_rdb_provider_count = 0;

static const JubeRdbProvider* jube_rdb_provider_find(const char* driver) {
    for (int i = 0; i < jube_rdb_provider_count; i++) {
        if (strcmp(jube_rdb_providers[i].driver, driver) == 0) return &jube_rdb_providers[i];
    }
    return NULL;
}

static bool jube_rdb_provider_add(const char* provided, const char* module_name,
                                  const char* manifest_path) {
    // `rdb:` followed by a lower-case driver identifier
    if (strncmp(provided, "rdb:", 4) != 0) return false;
    const char* driver = provided + 4;
    size_t length = strlen(driver);
    if (length == 0 || length >= JUBE_RDB_DRIVER_NAME_CAPACITY ||
            strlen(module_name) >= JUBE_SPECIFIER_MODULE_NAME_CAPACITY ||
            strlen(manifest_path) >= JUBE_MANIFEST_PATH_CAPACITY) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        char c = driver[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    const JubeRdbProvider* existing = jube_rdb_provider_find(driver);
    if (existing) {
        if (strcmp(existing->module_name, module_name) == 0) return true;
        log_error("JUBE_SPEC: rdb driver '%s' provided by both '%s' and '%s'", driver,
                  existing->module_name, module_name);
        return false;
    }
    if (jube_rdb_provider_count >= JUBE_RDB_PROVIDER_CAPACITY) return false;
    JubeRdbProvider* entry = &jube_rdb_providers[jube_rdb_provider_count++];
    strcpy(entry->driver, driver);
    strcpy(entry->module_name, module_name);
    strcpy(entry->manifest_path, manifest_path);
    return true;
}

static bool jube_specifier_catalog_manifest_path(const char* manifest_path) {
    char* text = NULL;
    if (!jube_manifest_read_file(manifest_path, &text)) return true;

    char module_name[JUBE_SPECIFIER_MODULE_NAME_CAPACITY];
    char kind[128];
    char engine[128];
    char language[128];
    char provides[JUBE_SPECIFIER_CATALOG_CAPACITY][128];
    int provide_count = 0;
    bool has_name = jube_manifest_string(text, "name", module_name, sizeof(module_name));
    bool has_provides = jube_manifest_string_array(text, "provides", provides,
        JUBE_SPECIFIER_CATALOG_CAPACITY, &provide_count);
    bool has_language = jube_manifest_string(text, "language", language, sizeof(language));
    bool ok = has_name && has_provides;
    // known engines only: "js" provides script specifiers, "rdb" provides RDB
    // drivers; any other engine still fails the catalog closed
    bool rdb_engine = false;
    if (ok && provide_count > 0) {
        ok = jube_manifest_string(text, "kind", kind, sizeof(kind)) &&
            jube_manifest_string(text, "engine", engine, sizeof(engine)) &&
            str_ieq_const(kind, strlen(kind), "runtime-library");
        rdb_engine = ok && str_ieq_const(engine, strlen(engine), "rdb");
        ok = ok && (rdb_engine || str_ieq_const(engine, strlen(engine), "js"));
    }
    if (ok && !has_language && provide_count == 0) {
        log_error("JUBE_SPEC: manifest '%s' declares neither language nor provides", manifest_path);
        ok = false;
    }
    if (!ok) {
        log_error("JUBE_SPEC: invalid catalog manifest '%s'", manifest_path);
        free(text);
        jube_specifier_catalog_failed = true;
        return false;
    }
    for (int i = 0; i < provide_count; i++) {
        if (rdb_engine) {
            if (!jube_rdb_provider_add(provides[i], module_name, manifest_path)) {
                log_error("JUBE_SPEC: invalid rdb provider '%s' in '%s'", provides[i],
                          manifest_path);
                free(text);
                jube_specifier_catalog_failed = true;
                return false;
            }
            continue;
        }
        char normalized[JUBE_SPECIFIER_NAME_CAPACITY];
        if (!jube_specifier_normalize(provides[i], normalized, sizeof(normalized)) ||
                strcmp(provides[i], normalized) != 0 ||
                !jube_specifier_entry_upsert(provides[i], module_name, manifest_path, NULL,
                                             -1, JUBE_SPECIFIER_OWNER_CATALOGED)) {
            log_error("JUBE_SPEC: invalid provided specifier '%s' in '%s'", provides[i],
                      manifest_path);
            free(text);
            jube_specifier_catalog_failed = true;
            return false;
        }
    }
    free(text);
    return true;
}

static int jube_specifier_catalog_path_compare(const void* left, const void* right) {
    const char* a = (const char*)left;
    const char* b = (const char*)right;
    return strcmp(a, b);
}

static bool jube_specifier_catalog_scan_root(const char* root) {
    if (!root || !*root) return true;
    char manifest_path[JUBE_MANIFEST_PATH_CAPACITY];
    int written = snprintf(manifest_path, sizeof(manifest_path), "%s/module.json", root);
    if (written > 0 && (size_t)written < sizeof(manifest_path) &&
            !jube_specifier_catalog_manifest_path(manifest_path)) {
        return false;
    }

    char paths[JUBE_SPECIFIER_CATALOG_CAPACITY][JUBE_MANIFEST_PATH_CAPACITY];
    int path_count = 0;
#if defined(_WIN32)
    char search_path[JUBE_MANIFEST_PATH_CAPACITY];
    written = snprintf(search_path, sizeof(search_path), "%s\\*", root);
    if (written <= 0 || (size_t)written >= sizeof(search_path)) return true;
    WIN32_FIND_DATAA entry;
    HANDLE find_handle = FindFirstFileA(search_path, &entry);
    if (find_handle == INVALID_HANDLE_VALUE) return true;
    do {
        if (entry.cFileName[0] == '.' || !(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                path_count >= JUBE_SPECIFIER_CATALOG_CAPACITY) continue;
        written = snprintf(paths[path_count], sizeof(paths[path_count]), "%s/%s/module.json",
                           root, entry.cFileName);
        if (written > 0 && (size_t)written < sizeof(paths[path_count])) path_count++;
    } while (FindNextFileA(find_handle, &entry));
    FindClose(find_handle);
#else
    DIR* dir = opendir(root);
    if (!dir) return true;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || path_count >= JUBE_SPECIFIER_CATALOG_CAPACITY) continue;
        written = snprintf(paths[path_count], sizeof(paths[path_count]), "%s/%s/module.json",
                           root, entry->d_name);
        if (written > 0 && (size_t)written < sizeof(paths[path_count])) path_count++;
    }
    closedir(dir);
#endif
    qsort(paths, (size_t)path_count, sizeof(paths[0]), jube_specifier_catalog_path_compare);
    for (int i = 0; i < path_count; i++) {
        if (!jube_specifier_catalog_manifest_path(paths[i])) return false;
    }
    return true;
}

static bool jube_specifier_catalog_build(void) {
    jube_rdb_provider_count = 0;
    for (int i = 0; i < jube_static_modules_count; i++) {
        if (jube_specifier_index_module(jube_static_modules[i].module) != 0) {
            jube_specifier_catalog_failed = true;
            return false;
        }
    }
    const char* configured_paths = getenv("JUBE_MODULE_PATH");
    if (configured_paths && *configured_paths) {
        const char* cursor = configured_paths;
        while (*cursor) {
#if defined(_WIN32)
            const char* end = strchr(cursor, ';');
#else
            const char* end = strchr(cursor, ':');
#endif
            size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
            char root[JUBE_MANIFEST_PATH_CAPACITY];
            if (length > 0 && length < sizeof(root)) {
                str_copy(root, sizeof(root), cursor, length);
                if (!jube_specifier_catalog_scan_root(root)) return false;
            }
            if (!end) break;
            cursor = end + 1;
        }
    }
    if (!jube_specifier_catalog_scan_root(jube_host_module_root) ||
            !jube_specifier_catalog_scan_root("modules")) {
        return false;
    }
    for (int i = 0; i < jube_static_modules_count; i++) {
        if (!jube_specifier_descriptor_matches_catalog(jube_static_modules[i].module)) {
            jube_specifier_catalog_failed = true;
            return false;
        }
    }
    return !jube_specifier_catalog_failed;
}

static bool jube_specifier_catalog_ensure(void) {
    if (atomic_load32(&jube_specifier_catalog_state) != 0) {
        return !jube_specifier_catalog_failed;
    }
    while (__atomic_exchange_n(&jube_specifier_catalog_lock.v, 1, __ATOMIC_ACQUIRE) != 0) {
    }
    if (atomic_load32(&jube_specifier_catalog_state) == 0) {
        jube_specifier_catalog_build();
        atomic_store32(&jube_specifier_catalog_state, 1);
    }
    __atomic_store_n(&jube_specifier_catalog_lock.v, 0, __ATOMIC_RELEASE);
    return !jube_specifier_catalog_failed;
}

static const RdbDriver* const* jube_module_rdb_drivers(const JubeModuleDef* module,
                                                      int32_t* count) {
    *count = 0;
    size_t end = offsetof(JubeModuleDef, rdb_driver_count) + sizeof(module->rdb_driver_count);
    if (!jube_module_has_field(module, end) || !module->rdb_drivers) return NULL;
    *count = module->rdb_driver_count;
    return module->rdb_drivers;
}

/** D7.3.4: the descriptor's drivers must equal the manifest's `rdb:` provides */
static bool jube_rdb_module_matches_catalog(const JubeModuleDef* module) {
    int32_t count = 0;
    const RdbDriver* const* drivers = jube_module_rdb_drivers(module, &count);
    int provided = 0;
    for (int i = 0; i < jube_rdb_provider_count; i++) {
        if (strcmp(jube_rdb_providers[i].module_name, module->name) == 0) provided++;
    }
    if (count != provided) return false;
    for (int32_t i = 0; i < count; i++) {
        const JubeRdbProvider* provider = drivers[i] && drivers[i]->name ?
            jube_rdb_provider_find(drivers[i]->name) : NULL;
        if (!provider || strcmp(provider->module_name, module->name) != 0) return false;
    }
    return true;
}

const RdbDriver* jube_rdb_resolve_driver(const char* name) {
    if (!name || !jube_specifier_catalog_ensure()) return NULL;
    const JubeRdbProvider* provider = jube_rdb_provider_find(name);
    if (!provider) return NULL;
    int transaction_start = jube_static_modules_count;
    int index = jube_find_static_module_index(provider->module_name);
    if (index < 0) {
        if (jube_load_manifest_path_internal(provider->manifest_path,
                                             provider->module_name) != 1) {
            log_error("JUBE_RDB: driver module '%s' for '%s' is unavailable",
                      provider->module_name, name);
            return NULL;
        }
        index = jube_find_static_module_index(provider->module_name);
        if (index < 0) return NULL;
    }
    const JubeModuleDef* module = jube_static_modules[index].module;
    if (!jube_rdb_module_matches_catalog(module)) {
        log_error("JUBE_RDB: module '%s' drivers do not match its manifest provides",
                  module->name);
        // activation is transactional: a mismatched image is never kept (D7.3.2)
        if (index >= transaction_start) jube_rollback_registered_modules(transaction_start);
        return NULL;
    }
    int32_t count = 0;
    const RdbDriver* const* drivers = jube_module_rdb_drivers(module, &count);
    for (int32_t i = 0; i < count; i++) {
        if (strcmp(drivers[i]->name, name) == 0) return drivers[i];
    }
    return NULL;
}

static void jube_load_dynamic_modules_from_env(void) {
    if (jube_dynamic_modules_from_env_loaded) return;
    jube_dynamic_modules_from_env_loaded = true;
    const char* path = getenv("JUBE_DYNAMIC_MODULE");
    if (!path || !*path) return;
    const char* entry = getenv("JUBE_DYNAMIC_ENTRY");
    if (jube_load_dynamic_module(path, entry) != 0) {
        log_error("JUBE_REG: failed to load env dynamic module '%s'", path);
    }
}

static bool jube_module_set_read_node_module(const char* root, const char* module_name,
                                             bool* found) {
    if (found) *found = false;
    if (!root || !*root || !module_name || !*module_name) return false;
    char path[JUBE_MANIFEST_PATH_CAPACITY];
    int written = snprintf(path, sizeof(path), "%s/module-set.json", root);
    if (written <= 0 || (size_t)written >= sizeof(path)) return false;
    char* text = NULL;
    if (!jube_manifest_read_file(path, &text)) return false;
    if (found) *found = true;
    bool enabled = jube_manifest_array_contains(text, "modules", module_name);
    free(text);
    return enabled;
}

bool jube_node_module_enabled(const char* module_name) {
    if (!module_name || !*module_name) return false;
    const char* configured_paths = getenv("JUBE_MODULE_PATH");
    if (configured_paths && *configured_paths) {
        bool configured_root_seen = false;
        const char* cursor = configured_paths;
        while (*cursor) {
#if defined(_WIN32)
            const char* end = strchr(cursor, ';');
#else
            const char* end = strchr(cursor, ':');
#endif
            size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
            char root[JUBE_MANIFEST_PATH_CAPACITY];
            if (length > 0 && length < sizeof(root)) {
                configured_root_seen = true;
                str_copy(root, sizeof(root), cursor, length);
                bool found = false;
                bool enabled = jube_module_set_read_node_module(root, module_name, &found);
                if (found) {
                    return enabled;
                }
            }
            if (!end) break;
            cursor = end + 1;
        }
        if (configured_root_seen) {
            // An explicit module path owns profile selection. Falling back to
            // the source-tree module set would silently reactivate static
            // node-core and make isolated dynamic probes exercise the host.
            return false;
        }
    }
    bool found = false;
    bool enabled = jube_module_set_read_node_module(jube_host_module_root, module_name, &found);
    if (!found) enabled = jube_module_set_read_node_module("modules", module_name, &found);
    // An absent module set is the minimal bundle: no Node descriptor becomes
    // active and no Node globals or namespace caches are installed.
    return enabled;
}

static void jube_node_resource_cleanup(NodeRuntimeSession* session) {
    if (!session || session != jube_active_node_runtime_session ||
            !js_active_runtime_state) return;
    // The context table stays alive for timers and other hosts; Node detach
    // releases only rows whose lifecycle key is this retiring session.
    runtime_resource_table_clear_owned(js_runtime_resource_table(), session);
}

static uint32_t jube_node_resource_add_impl(void* session_handle, Item value, const char* kind,
        JubeNodeResourceCloseCallback close_callback, void* close_user, bool is_handle) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (!session || session != jube_active_node_runtime_session || !session->live ||
            session->detaching || !value.item || !kind) return 0;
    const RuntimeResourceDescriptor* descriptor =
        runtime_resource_descriptor_from_legacy_name(kind);
    if (!descriptor) {
        log_error("jube-resource: unregistered resource descriptor %s", kind);
        return 0;
    }
    return runtime_resource_table_add_owned(js_runtime_resource_table(), session,
        value, descriptor, close_callback, close_user, is_handle);
}

uint32_t jube_node_resource_add_with_close(void* session_handle, Item value, const char* kind,
        JubeNodeResourceCloseCallback close_callback, void* close_user) {
    return jube_node_resource_add_impl(session_handle, value, kind, close_callback,
                                       close_user, true);
}

uint32_t jube_node_resource_add_native(void* session_handle, Item value, const char* kind,
        JubeNodeResourceCloseCallback close_callback, void* close_user) {
    return jube_node_resource_add_impl(session_handle, value, kind, close_callback,
                                       close_user, false);
}

void jube_node_resource_close_kind(void* session_handle, const char* kind_prefix) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (session != jube_active_node_runtime_session || !session || !kind_prefix) return;
    // Dynamic Node modules link this legacy bridge by name. Keep the ABI
    // narrow while translating it once to the table's typed close group.
    if (strcmp(kind_prefix, "crypto.") == 0) {
        runtime_resource_table_close_group_owned(js_runtime_resource_table(),
            session, RUNTIME_RESOURCE_GROUP_CRYPTO);
    }
}

uint32_t jube_node_resource_add(Item value, const char* kind) {
    return jube_node_resource_add_with_close(jube_active_node_runtime_session, value, kind, NULL, NULL);
}

void jube_node_resource_remove_for_session(void* session_handle, uint32_t resource_id) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (session != jube_active_node_runtime_session) return;
    runtime_resource_table_remove_owned(js_runtime_resource_table(), session,
        resource_id);
}

void jube_node_resource_remove(uint32_t resource_id) {
    jube_node_resource_remove_for_session(jube_active_node_runtime_session, resource_id);
}

void* jube_node_resource_user_data_for_session(void* session_handle, uint32_t resource_id) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (session != jube_active_node_runtime_session) return NULL;
    return runtime_resource_table_user_data_owned(js_runtime_resource_table(),
        session, resource_id);
}

void* jube_node_runtime_current_session(void) {
    return jube_active_node_runtime_session && jube_active_node_runtime_session->live &&
            !jube_active_node_runtime_session->detaching ? jube_active_node_runtime_session : NULL;
}

static NodeRuntimeSession* jube_node_live_session(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    return node_session && node_session == jube_active_node_runtime_session &&
            node_session->live ? node_session : NULL;
}

NodeTraceState* jube_node_trace_state(void* session) {
    NodeRuntimeSession* node_session = jube_node_live_session(session);
    return node_session ? node_session->trace : NULL;
}

NodeTraceState* jube_node_trace_state_ensure(void* session) {
    NodeRuntimeSession* node_session = jube_node_live_session(session);
    if (!node_session) return NULL;
    if (!node_session->trace) {
        node_session->trace = (NodeTraceState*)mem_calloc(1, sizeof(NodeTraceState),
            MEM_CAT_SYSTEM);
    }
    return node_session->trace;
}

JsCjsState* jube_node_cjs_state(void* session) {
    NodeRuntimeSession* node_session = jube_node_live_session(session);
    if (!node_session) return NULL;
    return &node_session->cjs;
}

JsCommonJsCompileCacheState* jube_node_commonjs_compile_cache_state(void* session) {
    NodeRuntimeSession* node_session = jube_node_live_session(session);
    if (!node_session) return NULL;
    if (!node_session->commonjs_compile_cache) {
        node_session->commonjs_compile_cache =
            (JsCommonJsCompileCacheState*)mem_calloc(1, sizeof(JsCommonJsCompileCacheState),
                MEM_CAT_SYSTEM);
    }
    return node_session->commonjs_compile_cache;
}

JsDiagnosticsChannelState* jube_node_diagnostics_channel_state(void* session) {
    NodeRuntimeSession* node_session = jube_node_live_session(session);
    if (!node_session) return NULL;
    if (!node_session->diagnostics_channels) {
        JsDiagnosticsChannelState* state =
            (JsDiagnosticsChannelState*)mem_calloc(1, sizeof(JsDiagnosticsChannelState),
                MEM_CAT_SYSTEM);
        if (!state) return NULL;
        root_vector_bind_external(state, (Context*)context,
            &state->namespace_object, 9, "diagnostics channel state");
        state->namespace_epoch = UINT64_MAX;
        node_session->diagnostics_channels = state;
    }
    return node_session->diagnostics_channels;
}

JsPermissionPolicy* jube_node_permission_policy(void* session) {
    NodeRuntimeSession* node_session = jube_node_live_session(session);
    return node_session ? node_session->permission_policy : NULL;
}

JsPermissionPolicy* jube_node_permission_policy_ensure(void* session) {
    NodeRuntimeSession* node_session = jube_node_live_session(session);
    if (!node_session) return NULL;
    if (!node_session->permission_policy) {
        node_session->permission_policy =
            (JsPermissionPolicy*)mem_calloc(1, sizeof(JsPermissionPolicy), MEM_CAT_SYSTEM);
    }
    return node_session->permission_policy;
}

JsCryptoNativeState* jube_node_crypto_native_state(void* session) {
    NodeRuntimeSession* node_session = jube_node_live_session(session);
    if (!node_session) return NULL;
    return &node_session->crypto_native;
}

void jube_node_resource_clear(void) {
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    if (session && js_active_runtime_state) {
        runtime_resource_table_clear_owned(js_runtime_resource_table(), session);
    }
}

bool jube_node_resource_contains(uint32_t resource_id) {
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    return session && js_active_runtime_state &&
        runtime_resource_table_entry_owned(js_runtime_resource_table(), session,
            resource_id);
}

Item jube_node_resource_active_handles(void) {
    Item handles = js_array_new(0);
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    if (!session) return handles;
    for (int i = 0; i < runtime_resource_table_slot_count(js_runtime_resource_table()); i++) {
        const RuntimeResourceEntry* entry = runtime_resource_table_entry_at(
            js_runtime_resource_table(), i);
        if (entry && entry->lifecycle_owner == session && entry->is_handle) {
            js_array_push(handles, runtime_resource_table_value(js_runtime_resource_table(), entry));
        }
    }
    return handles;
}

Item jube_node_resource_active_resources_info(void) {
    Item resources = js_array_new(0);
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    if (!session) return resources;
    for (int i = 0; i < runtime_resource_table_slot_count(js_runtime_resource_table()); i++) {
        const RuntimeResourceEntry* entry = runtime_resource_table_entry_at(
            js_runtime_resource_table(), i);
        if (!entry || entry->lifecycle_owner != session || !entry->is_handle ||
                !entry->descriptor) continue;
        const char* name = entry->descriptor->display_name;
        js_array_push(resources, js_make_string_len(name, (int)strlen(name)));
    }
    return resources;
}

bool jube_node_core_module_enabled(void) {
    if (jube_node_core_module_set_enabled >= 0) {
        return jube_node_core_module_set_enabled != 0;
    }
    bool enabled = jube_node_module_enabled("node-core");
    jube_node_core_module_set_enabled = enabled ? 1 : 0;
    return enabled;
}

void jube_register_builtin_modules(void) {
    // AST prebuild workers can resolve imports concurrently. Serialize the
    // cold catalog writes so none can see a count before its slot is filled.
    static uv_once_t lock_once = UV_ONCE_INIT;
    static uv_mutex_t registration_lock;
    uv_once(&lock_once, []() { uv_mutex_init(&registration_lock); });
    uv_mutex_lock(&registration_lock);
    radiant_jube_register_static();
    // The Lambda-facing face of the same DOM core the radiant module bridges
    // to (ES36); registered after radiant so the dom_node wrappers its
    // functions return are already a known host type.
    dom_jube_register_static();
    // Phase-7 validation must let the dlopen copy win name registration over the static demo.
    if (!jube_env_flag_enabled("JUBE_HOSTOBJ_DEMO_DYNAMIC_ONLY")) {
        hostobj_demo_jube_register_static();
    }
    jube_load_dynamic_modules_from_env();
    uv_mutex_unlock(&registration_lock);
}

int jube_static_module_count(void) {
    return jube_static_modules_count;
}

const JubeModuleDef* jube_static_module_at(int index) {
    if (index < 0 || index >= jube_static_modules_count) return NULL;
    return jube_static_modules[index].module;
}

void jube_modules_runtime_reset(void) {
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    // Most short-lived JS realms never use Jube. Scanning the module registry
    // here used to make their heap reset pay the Node lifecycle cost anyway.
    if (!session || !session->live || session->detaching) return;
    size_t session_reset_end = offsetof(JubeModuleDef, runtime_reset_session) +
        sizeof(((JubeModuleDef*)NULL)->runtime_reset_session);
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (jube_static_modules[i].activation_state == JUBE_MODULE_ACTIVE &&
                jube_module_is_attached_to_session(&jube_static_modules[i], session) &&
                jube_module_has_field(module, session_reset_end) && module->runtime_reset_session) {
            module->runtime_reset_session(session);
        }
    }
    size_t end = offsetof(JubeModuleDef, runtime_reset) +
        sizeof(((JubeModuleDef*)NULL)->runtime_reset);
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (jube_static_modules[i].activation_state == JUBE_MODULE_ACTIVE &&
                jube_module_is_attached_to_session(&jube_static_modules[i], session) &&
                jube_module_has_field(module, end) && module->runtime_reset) {
            module->runtime_reset();
        }
    }
    jube_node_shared_primitives_reset(session);
    node_trace_events_runtime_reset(session);
}

void jube_modules_runtime_attach(void) {
    if (jube_active_node_runtime_session) return;
    jube_runtime_session_lock_acquire();
    if (!jube_node_runtime_sessions) {
        jube_node_runtime_sessions = arraylist_new(4);
        if (!jube_node_runtime_sessions) {
            jube_runtime_session_lock_release();
            return;
        }
    }
    NodeRuntimeSession* session = (NodeRuntimeSession*)mem_calloc(1,
        sizeof(NodeRuntimeSession), MEM_CAT_SYSTEM);
    if (!session) {
        jube_runtime_session_lock_release();
        return;
    }
    if (!arraylist_append(jube_node_runtime_sessions, session)) {
        mem_free(session);
        jube_runtime_session_lock_release();
        return;
    }
    session->generation = ++jube_node_runtime_generation;
    if (session->generation == 0) session->generation = ++jube_node_runtime_generation;
    session->live = true;
    jube_node_session_state_init(session);
    jube_runtime_session_lock_release();
    if (!jube_active_node_runtime_session_set(session)) {
        arraylist_remove(jube_node_runtime_sessions,
            jube_node_runtime_sessions->length - 1);
        mem_free(session);
        return;
    }

    // a lazy module may activate while the previous session is absent; attach
    // it again here because activation could not publish session-owned state.
    for (int i = 0; i < jube_static_modules_count; i++) {
        JubeStaticModuleEntry* entry = &jube_static_modules[i];
        if (entry->activation_state == JUBE_MODULE_ACTIVE &&
                !jube_module_is_attached_to_session(entry, session)) {
            jube_attach_module_to_active_runtime(entry);
        }
    }
}

void jube_modules_runtime_detach(void) {
    NodeRuntimeSession* active_session = jube_active_node_runtime_session;
    // Keep ordinary JS realm teardown out of the Jube host bridge until a
    // lazy global or namespace request has actually attached a session.
    if (!active_session || !active_session->live || active_session->detaching) return;
    void* session = active_session;
    // Reject new work before module teardown but keep roots usable until every
    // runtime_detach hook has released its session-bound persistent roots.
    active_session->detaching = true;
    size_t detach_end = offsetof(JubeModuleDef, runtime_detach) +
        sizeof(((JubeModuleDef*)NULL)->runtime_detach);
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (jube_static_modules[i].activation_state == JUBE_MODULE_ACTIVE &&
                jube_module_is_attached_to_session(&jube_static_modules[i],
                    active_session) &&
                jube_module_has_field(module, detach_end) && module->runtime_detach) {
            module->runtime_detach(session);
        }
        jube_module_detach_session(&jube_static_modules[i],
            active_session);
    }
    jube_node_shared_primitives_detach(session);
    jube_node_resource_cleanup(active_session);
    jube_node_session_module_states_destroy(active_session);
    jube_node_session_state_clear(active_session);
    // Invalidate before the next attach so stale module tokens cannot resolve
    // namespaces against a replacement JS heap.
    active_session->live = false;
    jube_active_node_runtime_session_set(NULL);
}

void jube_registry_cleanup(void) {
    jube_node_shared_primitives_shutdown();
    if (jube_specifier_index) {
        hashmap_free(jube_specifier_index);
        jube_specifier_index = NULL;
    }
    if (jube_node_runtime_sessions) {
        for (int i = 0; i < jube_node_runtime_sessions->length; i++) {
            NodeRuntimeSession* session = (NodeRuntimeSession*)
                jube_node_runtime_sessions->data[i];
            jube_node_resource_cleanup(session);
            jube_node_session_module_states_destroy(session);
            jube_node_session_state_clear(session);
            mem_free(session);
        }
        arraylist_free(jube_node_runtime_sessions);
        jube_node_runtime_sessions = NULL;
    }
    js_permission_shutdown();
    // Registry cleanup has no execution context to rebind; live contexts are
    // already detached before this process-lifetime catalog is released.
    jube_node_runtime_generation = 0;
    jube_specifier_catalog_failed = false;
    atomic_store32(&jube_specifier_catalog_state, 0);
    jube_node_core_module_set_enabled = -1;
}

const JubeModuleDef* jube_find_static_module(const char* name) {
    int index = jube_find_static_module_index(name);
    if (index < 0) return NULL;
    const JubeModuleDef* module = jube_static_modules[index].module;
    return jube_activate_module_descriptor(module) ? module : NULL;
}

void jube_notify_heap_cleanup(void* heap) {
    if (!heap) return;
    size_t field_end = offsetof(JubeModuleDef, heap_cleanup) +
        sizeof(((JubeModuleDef*)0)->heap_cleanup);
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (!module || jube_static_modules[i].activation_state != JUBE_MODULE_ACTIVE ||
            !jube_module_has_field(module, field_end) || !module->heap_cleanup) {
            continue;
        }
        module->heap_cleanup(heap);
    }
}

static const JubeModuleDef* jube_module_for_host_type(const void* host_type) {
    if (!host_type) return NULL;
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (!module || !module->types || module->type_count <= 0) continue;
        for (int j = 0; j < module->type_count; j++) {
            const JubeTypeDef* type = &module->types[j];
            if ((const void*)type == host_type) {
                return module;
            }
        }
    }
    return NULL;
}

const JubeTypeDef* jube_find_type_by_host_type(const void* host_type) {
    const JubeModuleDef* module = jube_module_for_host_type(host_type);
    return module && jube_activate_module_descriptor(module)
        ? (const JubeTypeDef*)host_type : NULL;
}
