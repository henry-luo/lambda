#pragma once

// js_runtime_state.hpp - shared JS runtime state surface.
//
// This header intentionally exposes only the mutable runtime-state capsule and
// legacy-name aliases used while the runtime is migrated away from free globals.

#include "js_runtime.h"
#include "js_builtin_catalog.hpp"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../runtime/async.h"
#include "../../lib/hashmap.h"

#define JS_REGEXP_MAX_PAREN 9
#define JS_ROOT_RANGE_REGISTRY_MAX 64
#define JS_EVENT_RAF_CAPACITY 1024
#define JS_EVENT_TIMER_CAPACITY 1024
#define JS_EVENT_MOCK_WAIT_CAPACITY 128
#define JS_WITH_STACK_MAX 16
#define JS_DEFERRED_MIR_MAX 4096
#define JS_FUNCTION_CACHE_CAPACITY 512
#define JS_MAX_GENERATORS 4096
#define JS_MAX_ASYNC_CONTEXTS 256
#define JS_READLINE_INPUT_MAP_MAX 256
#define JS_GLOBAL_VAR_MODULE_BINDING_CAP 512
#define JS_GLOBAL_LEX_BIND_MAX 1024
#define JS_TEST262_AGENT_MAX 16
#define JS_TEST262_AGENT_REPORT_MAX 64
#define JS_PROCESS_LISTENER_MAX 32
#define JS_ASYNC_HOOK_STATE_MAX 256
#define JS_ASYNC_PENDING_DESTROY_STATE_MAX 1024
#define JS_DOMAIN_STACK_MAX 64
#define JS_MAX_ALS_INSTANCES 256
#define JS_DOM_STORAGE_ENTRY_CAP 128
#define JS_DOM_MEDIA_QUERY_CAP 64
#define JS_DOM_PROMPT_QUEUE_CAP 32

struct JsFunction;
struct JsInterpEnv;
struct JsInterpGeneratorLoopContinuation;
struct JsInterpGeneratorListContinuation;
struct JsInterpGeneratorArrayBindingContinuation;
struct AstNode;
struct DomDocument;
struct DomElement;
struct UiContext;

struct JsTemplateRegistryEntry {
    int64_t site_id = 0;
    int count = 0;
    Item object = {};
    JsTemplateRegistryEntry* next = NULL;
};

struct JsMockSchedulerWait {
    Item promise = {};
    Item resolve = {};
    Item reject = {};
    Item signal = {};
    int64_t due_ms = 0;
    bool active = false;
};

struct JsEventLoopQueueState {
    Item queue_storage[2] = {};
    RuntimeAsyncDeque next_tick_deque = {};
    RuntimeAsyncDeque microtask_deque = {};
    bool microtask_running = false;

    Item raf_callback[JS_EVENT_RAF_CAPACITY] = {};
    int64_t raf_id[JS_EVENT_RAF_CAPACITY] = {};
    int raf_head = 0;
    int raf_tail = 0;
    int raf_count = 0;
    int64_t next_raf_id = 1;
    bool auto_close_mode = false;
    bool shutting_down = false;
    // Dynamic source compiled from a queued callback belongs to its parent turn.
    bool callback_running = false;
};

struct JsAstInterpreterState {
    // Nested eval, Function, require, and module execution share one turn.
    int execution_depth = 0;
};

struct JsEventLoopTimerState {
    // The native timer records are individually allocated; this context-owned
    // index is their only shared owner and is never published process-wide.
    void* handles[JS_EVENT_TIMER_CAPACITY] = {};
    int handle_count = 0;
    int64_t next_id = 1;
    uint64_t progress_generation = 0;
    bool force_shutdown = false;
    bool nan_warning_emitted = false;
    bool negative_warning_emitted = false;
    bool virtual_clock_enabled = false;
    double virtual_clock_ms = 0.0;

    bool mock_scheduler_enabled = false;
    int64_t mock_scheduler_now_ms = 0;
    JsMockSchedulerWait mock_waits[JS_EVENT_MOCK_WAIT_CAPACITY] = {};
    uint64_t mock_roots_epoch = 0;
};
struct JsRegexpLastMatch {
    String* input;
    String* match;
    String* groups[JS_REGEXP_MAX_PAREN];
    int group_count;
    int match_start;
    int match_end;
};

typedef void (*JsRootRangeResetFn)(void* owner);

// A fixed Item range whose address outlives every heap epoch.  Registration is
// deliberately owned by the range so clients cannot publish a live Item before
// the collector knows where that Item resides.
struct JsRootRange {
    Item* slots = NULL;
    int slot_count = 0;
    uint64_t roots_epoch = 0;
    const char* name = NULL;
    void* reset_owner = NULL;
    JsRootRangeResetFn reset = NULL;
    bool reset_registered = false;
};

// All context-owned caches expose the same precise root owner; keeping that
// invariant in one base state prevents realm subsystems from drifting into
// ad-hoc GC registration fields (D5.3).
struct JsRootedState {
    JsRootRange roots = {};
};

struct JsNamespaceState : JsRootedState {
    Item namespace_object = {};
};

// Use this only for actual single-Item LIFO storage.  Clients with replacement,
// replay, or multi-field records retain those semantic operations themselves.
struct JsItemStack : JsRootedState {
    int depth = 0;
};

struct JsWithScopeState {
    Item stack_slots[JS_WITH_STACK_MAX] = {};
    JsItemStack stack = {};
    Item last_binding_slots[2] = {};
    JsRootRange last_binding_roots = {};
    bool last_binding_valid = false;
};

// Generated closures retain only their MIR context and source buffer. Names
// and observable strings are materialized through the owning module NameId
// table, so compiler pools never cross the compile/execute boundary.
struct JsDeferredMirState {
    void* contexts[JS_DEFERRED_MIR_MAX] = {};
    char* source_buffers[JS_DEFERRED_MIR_MAX] = {};
    int count = 0;
};

// Node DNS exports are per-realm objects and must not retain values from a
// different heap through file-static cache slots.
struct JsDnsState {
    Item namespace_object = {};
    Item promises_namespace = {};
    Item resolver_prototype = {};
    Item promises_resolver_prototype = {};
    Item default_servers = {};
    uint64_t roots_epoch = 0;
};

struct JsBuiltinCacheState : JsRootedState {
    Item entries[JS_INTRINSIC_BINDING_COUNT] = {};
    bool initialized = false;
};

struct JsReadlineState : JsNamespaceState {
    Item promises_namespace = {};
    Item completion_interface = {};
    Item inputs[JS_READLINE_INPUT_MAP_MAX] = {};
    Item interfaces[JS_READLINE_INPUT_MAP_MAX] = {};
    int input_count = 0;
    bool create_promises_mode = false;
};

struct JsBufferState : JsNamespaceState {
    Item prototype = {};
};

struct JsHttpsState : JsNamespaceState {
    Item agent_prototype = {};
};

struct JsUtilState : JsNamespaceState {
};

struct JsCryptoState : JsNamespaceState {
};

struct JsChildProcessState : JsNamespaceState {
};

struct JsTlsState : JsNamespaceState {
    Item ca_bundled = {};
    Item ca_extra = {};
    Item ca_system = {};
    Item ca_default = {};
    void* client_ticket_states = NULL;
    void* secure_context_owners = NULL;
};

struct JsStreamState : JsNamespaceState {
    Item key_on = {}; Item key_emit = {}; Item key_push = {}; Item key_write = {};
    Item key_end = {}; Item key_pipe = {}; Item key_read = {}; Item key_destroy = {};
    Item key_readable = {}; Item key_writable = {}; Item key_flowing = {}; Item key_ended = {};
    Item key_finished = {}; Item key_destroyed = {}; Item key_listeners = {}; Item key_buffer = {};
    Item key_readable_state = {}; Item key_writable_state = {}; Item key_end_pending = {};
    Item key_end_emitted = {}; Item key_reading = {}; Item key_reading_sync = {}; Item key_paused = {};
    Item key_finish_emitted = {}; Item key_close_emitted = {}; Item key_closed = {};
    Item key_capture_rejections = {}; Item key_auto_destroy = {}; Item key_readable_side_enabled = {};
    Item key_writable_side_enabled = {}; Item key_destroy_pending = {}; Item key_listener_fn = {};
    Item key_listener_context = {};
    Item readable_prototype = {}; Item writable_prototype = {}; Item duplex_prototype = {};
    Item transform_prototype = {}; Item passthrough_prototype = {}; Item internal_state_namespace = {};
    Item internal_end_of_stream_namespace = {}; Item iterator_namespace = {}; Item web_namespace = {};
    Item promises_namespace = {};
    bool keys_initialized = false;
    int64_t default_byte_hwm = 16 * 1024;
    int64_t default_object_hwm = 16;
};

struct JsHttpState : JsNamespaceState {
    Item server_prototype = {};
    Item incoming_message_prototype = {};
    Item server_response_prototype = {};
    Item outgoing_message_prototype = {};
};

struct JsAssertMockSlot {
    Item calls = {};
    Item original = {};
    int call_count = 0;
    bool in_use = false;
};

// assert and node:test retain namespace identity, hook closures, and mock
// call records. They are realm values, so this fixed context slab keeps their
// repeated test-runner access direct and isolated.
struct JsAssertState {
    Item namespace_object = {};
    Item internal_errors_namespace = {};
    Item internal_myers_diff_namespace = {};
    Item options_key = {};
    Item diff_key = {};
    Item instances[64] = {};
    int instance_count = 0;
    uint64_t key_epoch = 0;
    uint64_t instances_roots_epoch = 0;

    Item node_test_namespace = {};
    Item before_each_store = {};
    Item after_each_store = {};
    Item event_queue = {};
    int node_test_total_count = 0;
    int node_test_pass_count = 0;
    int node_test_fail_count = 0;
    int64_t node_test_next_id = 1;
    uint64_t node_test_roots_epoch = 0;
    Item before_each_hooks[64] = {};
    Item after_each_hooks[64] = {};
    int before_each_count = 0;
    int after_each_count = 0;
    JsAssertMockSlot mock_slots[64] = {};
    int mock_slot_count = 0;
};

struct JsNetState : JsNamespaceState {
    Item socket_prototype = {};
    Item server_prototype = {};
    Item socket_connect_function = {};
    Item stream_socket_constructor = {};
    // Native network defaults and BlockList objects are realm-local. The
    // capsule stays lazy so contexts that never load net pay no allocation.
    void* native_state = NULL;
};

// Optional Node-leaf callbacks are semantic to one realm. Keep their function
// pointers beside that realm rather than letting the last initialized module
// overwrite a process-wide callback for every context.
struct JsHostHooksState {
    void (*shutdown_participant)(void) = NULL;
    Item (*ipc_accept_hook)(void*) = NULL;
    void (*cluster_online_hook)(Item) = NULL;
    Item (*console_format_hook)(Item) = NULL;
};

// fs owns several lazily assembled namespace and prototype objects. They are
// all realm values, so one exact context range replaces the old per-slot
// process-global registrations.
struct JsFsState : JsNamespaceState {
    Item internal_binding_namespace = {};
    Item internal_default_fstat = {};
    Item stats_prototype = {};
    Item filehandle_constructor = {};
    Item filehandle_prototype = {};
    Item internal_promises_namespace = {};
    // Outstanding callback-style requests are owned by the context that
    // registered their precise roots; teardown detaches this list before the
    // heap disappears.
    void* pending_requests = NULL;
};

// Clipboard wrappers and the active synthetic drag session are realm state;
// the platform clipboard store itself remains an external service boundary.
struct JsClipboardState : JsRootedState {
    Item blob_prototype = {};
    Item file_prototype = {};
    Item clipboard_item_prototype = {};
    Item clipboard_event_prototype = {};
    Item data_transfer_prototype = {};
    Item file_list_prototype = {};
    Item drag_data_transfer = {};
    int64_t generation = 1;
};

// DOM singleton wrappers are per browsing context. The native document itself
// is owned by Radiant; this range owns only JS heap values that reference it.
struct JsDomState : JsRootedState {
    Item implementation = {};
    Item document_proxy = {};
    Item default_view = {};
    Item title = {};
    Item fonts = {};
    bool design_mode = false;
    DomElement* active_element = NULL;
    DomDocument* current_document = NULL;
    UiContext* current_ui_context = NULL;
    bool host_driven_loop = false;
    DomDocument* main_document = NULL;
    char* prompt_queue[JS_DOM_PROMPT_QUEUE_CAP] = {};
    int prompt_head = 0;
    int prompt_tail = 0;
};

struct JsDomStorageEntry {
    char* key = NULL;
    char* value = NULL;
};

struct JsDomStorageState {
    Item object = {};
    JsDomStorageEntry entries[JS_DOM_STORAGE_ENTRY_CAP] = {};
    int count = 0;
};

struct JsDomMediaQueryState {
    Item object = {};
    char* query = NULL;
    bool matches = false;
};

struct JsDomPlatformState {
    JsDomStorageState local_storage = {};
    JsDomStorageState session_storage = {};
    JsDomMediaQueryState media_queries[JS_DOM_MEDIA_QUERY_CAP] = {};
    int media_query_count = 0;
    uint64_t roots_epoch = 0;
};

// String-concatenation fast tables are read on a hot path. They are plain
// owner-thread fields: no lock, atomic, or per-call context lookup is needed.
struct JsStringConcatState : JsRootedState {
    Item last_four_byte_escape = {};
    Item percent_prefixes[16] = {};
    Item percent_bytes[256] = {};
    uint32_t last_four_byte_cp = 0;
    uint64_t last_four_byte_epoch = 0;
};

// Optimized global-var module slots are execution semantics. The fixed table
// is context-local so ordinary property writes never serialize across realms.
struct JsGlobalVarModuleBindingState : JsRootedState {
    Item global = {};
    Item keys[JS_GLOBAL_VAR_MODULE_BINDING_CAP] = {};
    int indices[JS_GLOBAL_VAR_MODULE_BINDING_CAP] = {};
    uint32_t module_state_ids[JS_GLOBAL_VAR_MODULE_BINDING_CAP] = {};
    int count = 0;
    uint64_t epoch = 0;
};

struct JsRuntimeCoreCacheState : JsRootedState {
    Item proto_key = {};
};

struct JsFunctionPrototypeState : JsRootedState {
    Item generator_function = {};
    Item async_generator_function = {};
    Item async_function = {};
};

// URI and one-byte-string fast caches are hit inside primitive operations.
// Keep them context-local so those operations remain direct loads/stores.
struct JsGlobalStringCacheState : JsRootedState {
    Item uri_last_four_byte_string = {};
    Item last_from_char_code_string = {};
    Item decode_uri_component_error = {};
    Item decode_uri_error = {};
    Item ascii_chars[128] = {};
    Item test262_percent_hex[256] = {};
    Item test262_cached_percent_left = {};
    uint32_t test262_percent_byte0 = 0;
    uint32_t test262_percent_byte1 = 0;
    uint32_t test262_percent_byte2 = 0;
    uint32_t uri_last_four_byte_cp = 0;
    uint64_t uri_last_four_byte_epoch = 0;
    int last_from_char_code_cp = -1;
    uint64_t last_from_char_code_epoch = 0;
    uint64_t ascii_chars_epoch = ~0ULL;
    uint64_t decode_uri_component_error_epoch = 0;
    uint64_t decode_uri_error_epoch = 0;
};

// Global-object and lexical-environment caches are realm semantics. Item
// storage is grouped before scalar metadata for one exact, one-time root range.
struct JsGlobalBindingState : JsRootedState {
    Item global_this = {};
    Item var_defined_keys[64] = {};
    Item var_defined_global = {};
    Item window_event = {};
    Item lexical_global = {};
    Item lexical_keys[JS_GLOBAL_LEX_BIND_MAX] = {};
    Item lexical_values[JS_GLOBAL_LEX_BIND_MAX] = {};
    int var_defined_count = 0;
    uint64_t var_defined_epoch = 0;
    bool window_event_intercept_enabled = false;
    int lexical_count = 0;
    uint64_t lexical_epoch = 0;
    bool lexical_immutable[JS_GLOBAL_LEX_BIND_MAX] = {};
};

#define JS_TYPED_ARRAY_CACHE_TYPE_COUNT 12

// Constructor identity is observable (`Array === globalThis.Array`), so these
// caches must be private to a realm even though construction is infrequent.
struct JsConstructorCacheState : JsRootedState {
    Item global_builtin_functions[JS_BUILTIN_GLOBAL_MAX] = {};
    Item constructors[JS_CTOR_MAX] = {};
    Item typed_array_base = {};
    Item typed_array_base_prototype = {};
    Item typed_array_prototypes[JS_TYPED_ARRAY_CACHE_TYPE_COUNT] = {};
    bool global_builtin_initialized = false;
    bool constructors_initialized = false;
};

struct JsRuntimeNamespaceState : JsRootedState {
    Item math = {};
    Item json = {};
    Item css = {};
    Item intl = {};
    Item console = {};
    Item test262 = {};
    Item reflect = {};
    Item atomics = {};
};

struct JsVmRuntimeState {
    Item namespace_object = {};
    uint64_t namespace_epoch = 0;
    int source_text_identifier_counter = 0;
};

struct JsTest262AgentState : JsRootedState {
    Item object = {};
    Item callbacks[JS_TEST262_AGENT_MAX] = {};
    Item reports[JS_TEST262_AGENT_REPORT_MAX] = {};
    int report_waiters[JS_TEST262_AGENT_REPORT_MAX] = {};
    int callback_count = 0;
    int report_head = 0;
    int report_count = 0;
    int current_slot = -1;
    int64_t eval_script_active = 0;
    // Atomics waiter records are allocated only when the Atomics namespace is
    // materialized. They belong to this realm's Test262 agent simulation.
    void* atomics_waiter_state = NULL;
};

// Node process state is realm-local apart from the operating-system process.
// The fixed listener and pending-message tables are exactly rooted once.
struct JsProcessState : JsRootedState {
    Item argv = {};
    Item exec_argv = {};
    Item object = {};
    Item exit_listeners[JS_PROCESS_LISTENER_MAX] = {};
    Item uncaught_listeners[JS_PROCESS_LISTENER_MAX] = {};
    Item listener_map = {};
    Item ipc_pending_messages = {};
    const char** argv_raw = NULL;
    const char** exec_argv_raw = NULL;
    int argc_raw = 0;
    int exec_argc_raw = 0;
    int exit_code = 0;
    bool exit_requested = false;
    int exit_listener_count = 0;
    int uncaught_listener_count = 0;
    bool exiting = false;
    int total_listener_count = 0;
    int ipc_liveness_listener_count = 0;
    bool ipc_active = false;
    bool ipc_closing = false;
    bool ipc_disconnect_emitted = false;
    bool ipc_force_ref = false;
    void* ipc_pipe = NULL;
    char* ipc_buffer = NULL;
    size_t ipc_length = 0;
    size_t ipc_capacity = 0;
};

struct JsIteratorState : JsRootedState {
    Item generator_return_marker = {};
    Item generator_throw_marker = {};
    Item iterator_prototype = {};
    Item array_iterator_prototype = {};
    Item string_iterator_prototype = {};
    Item map_iterator_prototype = {};
    Item set_iterator_prototype = {};
    Item regexp_string_iterator_prototype = {};
    Item generator_proto_depth1 = {};
    Item async_generator_proto_depth1 = {};
    Item generator_proto_depth2 = {};
    Item async_generator_proto_depth2 = {};
    Item async_iterator_prototype = {};
};

// Console counters/timers are observable per JS realm, not process-wide logs.
struct JsConsoleState {
    int count_values[64] = {};
    uint32_t count_keys[64] = {};
    int count_used = 0;
    double timers[32] = {};
    uint32_t timer_keys[32] = {};
    int timer_used = 0;
    int group_depth = 0;
};

// Small execution-state flags and Symbol registries are semantic per realm.
struct JsRuntimeOperationState {
    bool reflect_define_property_mode = false;
    bool reflect_define_property_failed = false;
    bool private_define_active = false;
    Item deferred_instance_field_class = {};
    const char* regex_property_cache_chars = NULL;
    int regex_property_cache_len = 0;
    int regex_property_cache_mode = 0;
    bool regex_property_cache_result = false;
    // RegExp instances share one fixed own-property layout.  It belongs to the
    // active Input pool and is cleared with the other regex pool-backed caches.
    void* regex_instance_shape = NULL;
    uint64_t next_symbol_id = 100;
    HashMap* symbol_registry = NULL;
    HashMap* symbol_description_registry = NULL;
};

// Generated records are process-pinned, but the table is realm-owned so hot
// paths never resolve a catalog ID repeatedly and future realm policy stays
// out of mutable process-global state.
struct JsWellKnownRefs {
    NameId constructor = NAME_ID_NONE;
    NameId prototype = NAME_ID_NONE;
    NameId name = NAME_ID_NONE;
    NameId to_string = NAME_ID_NONE;
    NameId value_of = NAME_ID_NONE;
    NameId symbol_iterator = NAME_ID_NONE;
    NameId symbol_to_primitive = NAME_ID_NONE;
    NameId symbol_has_instance = NAME_ID_NONE;
    NameId symbol_to_string_tag = NAME_ID_NONE;
    NameId symbol_async_iterator = NAME_ID_NONE;
    NameId symbol_species = NAME_ID_NONE;
    NameId symbol_match = NAME_ID_NONE;
    NameId symbol_replace = NAME_ID_NONE;
    NameId symbol_search = NAME_ID_NONE;
    NameId symbol_split = NAME_ID_NONE;
    NameId symbol_unscopables = NAME_ID_NONE;
    NameId symbol_is_concat_spreadable = NAME_ID_NONE;
    NameId symbol_match_all = NAME_ID_NONE;
    NameId symbol_async_dispose = NAME_ID_NONE;
    NameId symbol_dispose = NAME_ID_NONE;
};

struct JsAsyncHooksState : JsRootedState {
    Item root_resource = {};
    Item current_resource = {};
    Item hooks[JS_ASYNC_HOOK_STATE_MAX] = {};
    Item pending_destroy_resources[JS_ASYNC_PENDING_DESTROY_STATE_MAX] = {};
    int enabled_count = 0;
    int64_t next_id = 2;
    int hook_count = 0;
    int pending_destroy_count = 0;
};

enum JsPromiseState {
    JS_PROMISE_PENDING,
    JS_PROMISE_FULFILLED,
    JS_PROMISE_REJECTED,
};

// GC-owned Promise carrier. Reactions are stored in a growable GC Array so
// each live Promise owns its precise edge set without a context-wide cap.
struct JsPromise : VMap {
    JsPromiseState state = JS_PROMISE_PENDING;
    Item result = {};
    uint64_t result_scalar = 0;
    Item reactions = {};
    Item reject_domain = {};
    Item expando = {};
    Item prototype_override = {};
    bool has_prototype_override = false;
    bool extensible = true;
    bool rejection_handled = false;
    bool unhandled_check_scheduled = false;
    bool unhandled_reported = false;
    int64_t unhandled_epoch = 0;
};

struct JsPromiseRuntimeState : JsRootedState {
    RuntimeAsyncDeque unhandled_deque = {};
    // Keep the three GC-visible owner slots contiguous; the deque control
    // record is native metadata and must never be scanned as an Item range.
    Item unhandled_storage = {};
    Item domain_current = {};
    Item domain_namespace = {};
    Item domain_stack_slots[JS_DOMAIN_STACK_MAX] = {};
    int pending_count = 0;
    int live_count = 0;
    int peak_live_count = 0;
    int64_t unhandled_epoch = 0;
    bool unhandled_strict = false;
    JsItemStack domain_stack = {};
};

// Module scheduling state is context-owned; canonical descriptors and their
// namespace/TLA edges live in Runtime::module_registry.
struct JsModuleRuntimeState {
    Item active_namespace = {};
    int module_depth = 0;
    int async_eval_order_counter = 0;
    int draining_depth = 0;
    uint64_t roots_epoch = 0;
};

struct JsClusterState {
    Item primary_options = {};
    uint64_t primary_options_root_epoch = 0;
    int64_t next_worker_id = 1;
};

struct JsAsyncLocalStorageState : JsRootedState {
    Item instances[JS_MAX_ALS_INSTANCES] = {};
    int instance_count = 0;
};

struct JsPerformanceState {
    uint64_t origin_epoch = UINT64_MAX;
    double origin_monotonic_ms = 0.0;
    double origin_epoch_ms = 0.0;
    bool frame_clock_active = false;
    double frame_clock_ms = 0.0;
    bool virtual_clock_enabled = false;
    double virtual_clock_ms = 0.0;
};

struct JsGeneratorStateRecord {
    TypeId type_id = LMD_TYPE_MAP;
    Context* runtime_context = NULL;
    void* state_fn = NULL;
    Item* env = NULL;
    int env_size = 0;
    int64_t state = 0;
    bool done = false;
    bool started = false;
    bool executing = false;
    bool is_async = false;
    Item private_home_class = {};
    Item delegate = {};
    int64_t delegate_resume = -1;
    int delegate_idx = 0;
    // AST generators retain the same GC-owned lexical records as ordinary
    // interpreted functions; their yield count is a resumable program point.
    Item ast_function = {};
    Item ast_arguments = {};
    Item ast_this = {};
    JsInterpEnv* ast_function_env = NULL;
    JsInterpEnv* ast_body_env = NULL;
    int64_t ast_yield_skip = 0;
    // Replay uses prior next() values when a nested yield resumes before its
    // enclosing yield is reached again.
    Item ast_yield_values = {};
    // Terminal-yield loop continuations keep AST generators resumable without
    // replaying completed iterations on every next().
    JsInterpGeneratorLoopContinuation* ast_loop_continuations = NULL;
    JsInterpGeneratorListContinuation* ast_list_continuation = NULL;
    // A destructuring target can suspend after IteratorStep. Retain its
    // iterator/value cursor so replay does not advance the iterator twice.
    JsInterpGeneratorArrayBindingContinuation* ast_array_binding_continuations = NULL;
    bool ast_resumable_loop_active = false;
    // Retain an injected throw/return while a finally block yields before it
    // can finish propagating that abrupt completion.
    int64_t ast_pending_resume_yield = 0;
    Item ast_pending_resume_input = {};
    bool ast_initialized = false;
};

struct JsAsyncContextStateRecord {
    Context* runtime_context = NULL;
    void* state_fn = NULL;
    Item* env = NULL;
    int env_size = 0;
    int state = 0;
    Item promise = {};
    // resumed MIR property names must use the module image that compiled the body.
    uint32_t module_state_id = UINT32_MAX;
    Item this_val = {};
    // AST continuations use the same context/promise table as MIR async
    // functions, but retain interpreter-owned lexical environments.
    Item ast_function = {};
    Item ast_arguments = {};
    JsInterpEnv* ast_function_env = NULL;
    JsInterpEnv* ast_body_env = NULL;
    AstNode* ast_resume_statement = NULL;
    Item ast_await_values = {};
    int64_t ast_await_skip = 0;
    bool ast_initialized = false;
};

bool js_root_range_ensure_registered(JsRootRange* range);
void js_root_range_clear(JsRootRange* range);
bool js_root_range_register_reset(JsRootRange* range, void* owner,
                                  JsRootRangeResetFn reset);
void js_root_range_reset_all(void);
bool js_item_stack_push(JsItemStack* stack, Item value);
void js_item_stack_pop(JsItemStack* stack);
void js_item_stack_clear(JsItemStack* stack);
void js_item_stack_shrink(JsItemStack* stack, int depth);

#define JS_EVAL_SOURCE_STACK_MAX 16

// Source records span a runtime eval or a VM-originated function call. Their
// Item fields are separate exact ranges; POD metadata is never scanned as an
// Item merely because it is adjacent to source roots.
struct JsEvalSourceState {
    Item filename_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    Item code_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    int64_t line_offset_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    int64_t column_offset_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    bool compact_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    int depth = 0;
    JsRootRange filename_roots = {};
    JsRootRange code_roots = {};
};

#define JS_EVAL_ENV_BIND_MAX 512
#define JS_EVAL_ENV_FRAME_MAX 32
#define JS_EVAL_LOCAL_BIND_MAX 512
#define JS_EVAL_LOCAL_FRAME_MAX 64
#define JS_EVAL_LEXICAL_BIND_MAX 512
#define JS_EVAL_IMMUTABLE_BIND_MAX 512
#define JS_EVAL_PRIVATE_BIND_MAX 256

// A direct-eval bridge exists for one generated eval call.  Item columns are
// structure-of-arrays so each exact GC range excludes the bool metadata.
struct JsEvalBridgeState {
    Item env_keys[JS_EVAL_ENV_BIND_MAX] = {};
    Item env_old_values[JS_EVAL_ENV_BIND_MAX] = {};
    bool env_had_own[JS_EVAL_ENV_BIND_MAX] = {};
    bool env_from_journal[JS_EVAL_ENV_BIND_MAX] = {};
    int env_count = 0;
    int env_frame_marks[JS_EVAL_ENV_FRAME_MAX] = {};
    int env_frame_depth = 0;

    Item global_lexical_keys[JS_EVAL_ENV_BIND_MAX] = {};
    Item global_lexical_old_values[JS_EVAL_ENV_BIND_MAX] = {};
    bool global_lexical_had_own[JS_EVAL_ENV_BIND_MAX] = {};
    bool global_lexical_immutable[JS_EVAL_ENV_BIND_MAX] = {};
    int global_lexical_count = 0;
    int global_lexical_frame_marks[JS_EVAL_ENV_FRAME_MAX] = {};
    int global_lexical_frame_depth = 0;

    Item private_unscoped_keys[JS_EVAL_PRIVATE_BIND_MAX] = {};
    Item private_scoped_keys[JS_EVAL_PRIVATE_BIND_MAX] = {};
    int private_count = 0;
    int private_frame_marks[JS_EVAL_LOCAL_FRAME_MAX] = {};
    int private_frame_depth = 0;

    JsRootRange env_key_roots = {};
    JsRootRange env_old_value_roots = {};
    JsRootRange global_lexical_key_roots = {};
    JsRootRange global_lexical_old_value_roots = {};
    JsRootRange private_unscoped_key_roots = {};
    JsRootRange private_scoped_key_roots = {};
};

typedef struct JsEvalLocalFrameMarks {
    int local_mark;
    int lexical_mark;
    int immutable_mark;
} JsEvalLocalFrameMarks;

// Caller-local records survive multiple direct eval calls in one generated
// function. They deliberately do not share bridge or source depths.
struct JsEvalLocalState {
    Item keys[JS_EVAL_LOCAL_BIND_MAX] = {};
    Item values[JS_EVAL_LOCAL_BIND_MAX] = {};
    int count = 0;
    JsEvalLocalFrameMarks frame_marks[JS_EVAL_LOCAL_FRAME_MAX] = {};
    int frame_depth = 0;
    Item lexical_keys[JS_EVAL_LEXICAL_BIND_MAX] = {};
    int lexical_count = 0;
    Item immutable_keys[JS_EVAL_IMMUTABLE_BIND_MAX] = {};
    int immutable_count = 0;

    JsRootRange key_roots = {};
    JsRootRange value_roots = {};
    JsRootRange lexical_key_roots = {};
    JsRootRange immutable_key_roots = {};
};

struct JsEvalState {
    JsEvalSourceState source = {};
    JsEvalBridgeState bridge = {};
    JsEvalLocalState local = {};
};

void js_eval_state_reset(JsEvalState* state);
void js_eval_state_assert_clear(const JsEvalState* state, const char* reset_name);

struct JsIntrinsicState {
    // Prototype cache slots are precise GC roots so moving collection updates
    // every cached Item; name Items are active-name-pool owned.
    uint64_t* prototype_roots[JS_CLASS__COUNT] = {};
    bool prototype_resolving[JS_CLASS__COUNT] = {};
    Item constructor_names[JS_CLASS__COUNT] = {};
    Item prototype_name = {0};
    uint64_t mutation_versions[JS_CLASS__COUNT] = {};
    uint64_t mutation_serial = 1;
    uint64_t owner_heap_epoch = 0;
    uint64_t array_proto_clean_epoch = 0;
    bool array_proto_clean = false;
    uint32_t initialization_depth = 0;
    int array_sym_iter_ever_set = 0;
};

// Test262 AST-native batches retain one heap per manifest. This template owns
// rooted pristine global/namespace values, an empty module slab, and the Input
// used to restore a fresh realm after a test.
struct JsTest262RealmTemplateState {
    // Keep the snapshot roots contiguous for the context's exact GC range.
    Item global = {};
    Item namespaces[8] = {};
    JsRootRange roots = {};
    Input* input = NULL;
    uint32_t module_state_id = UINT32_MAX;
    bool active = false;
};

struct JsRuntimeState {
    JsDnsState dns = {};
    JsBuiltinCacheState builtin_cache = {};
    JsReadlineState readline = {};
    JsBufferState buffer = {};
    JsHttpsState https = {};
    JsUtilState util = {};
    JsCryptoState crypto = {};
    JsChildProcessState child_process = {};
    JsTlsState tls = {};
    JsStreamState stream = {};
    JsHttpState http = {};
    JsAssertState assert = {};
    JsNetState net = {};
    JsHostHooksState host_hooks = {};
    JsFsState fs = {};
    JsClipboardState clipboard = {};
    JsDomState dom = {};
    JsDomPlatformState dom_platform = {};
    // Listener records contain native precise-root slots and DOM pins. Keep
    // their opaque storage with the owning realm; dispatch reads it directly
    // after the context has been bound, with no shared synchronization.
    void* dom_event_state = NULL;
    void* dom_observer_state = NULL;
    void* xhr_state = NULL;
    void* history_state = NULL;
    HashMap* dom_attached_expando_roots = NULL;
    void* dom_collection_state = NULL;
    void* dom_foreign_document_state = NULL;
    void* fetch_state = NULL;
    void* canvas_state = NULL;
    JsStringConcatState string_concat = {};
    JsGlobalVarModuleBindingState global_var_module_bindings = {};
    JsRuntimeCoreCacheState runtime_core_cache = {};
    JsFunctionPrototypeState function_prototypes = {};
    JsGlobalStringCacheState global_string_caches = {};
    JsGlobalBindingState global_bindings = {};
    JsConstructorCacheState constructors = {};
    JsRuntimeNamespaceState namespaces = {};
    // VM namespaces and generated module identifiers are observable realm
    // state. Keeping them here prevents a new document from accepting an
    // equal epoch and reusing an Item from a retired document heap.
    JsVmRuntimeState vm = {};
    JsTest262AgentState test262_agent = {};
    JsProcessState process = {};
    JsIteratorState iterators = {};
    JsConsoleState console = {};
    JsRuntimeOperationState operations = {};
    JsWellKnownRefs well_known = {};
    JsAsyncHooksState async_hooks = {};
    JsPromiseRuntimeState promises = {};
    JsModuleRuntimeState modules = {};
    JsTest262RealmTemplateState test262_realm_template = {};
    JsClusterState cluster = {};
    JsAsyncLocalStorageState async_local_storage = {};
    JsPerformanceState performance = {};
    // Native buffer ownership and tagged-template identity are realm-local
    // caches. Their pointer lookups remain ordinary context-local accesses.
    HashMap* array_runtime_items = NULL;
    JsTemplateRegistryEntry* template_registry = NULL;
    void* prototype_snapshot_state = NULL;
    void* regex_compile_cache = NULL;
    void* regex_permanent_cache = NULL;
    Input* input = NULL;
    bool strict_mode = false;
    JsIntrinsicState intrinsics = {};
    JsEvalState eval = {};
    JsEventLoopQueueState event_loop = {};
    JsAstInterpreterState ast_interpreter = {};
    JsEventLoopTimerState timers = {};
    JsWithScopeState with_scope = {};
    JsDeferredMirState deferred_mir = {};
    void* dynamic_function_cache_state = NULL;
    // Timeout recovery may interrupt JS compilation before the ordinary
    // teardown path runs.  Its compiler owners stay with this realm, never in
    // process globals; compilation is cold and generated code never reads it.
    void* mir_compile_recovery_state = NULL;
    // Wrapper identity is observable through .prototype and must therefore be
    // private to the context that owns the function objects and their heap.
    struct JsFunctionCacheKey {
        uint64_t target_bits;
        int16_t arity;
        uint8_t kind;
        uint8_t policy;
        uint8_t capabilities;
    } function_cache_keys[JS_FUNCTION_CACHE_CAPACITY] = {};
    JsFunction* function_cache_values[JS_FUNCTION_CACHE_CAPACITY] = {};
    int function_cache_count = 0;
    int function_cache_suppress_depth = 0;
    // Resumable code retains function environments after its creating native
    // frame has returned.  The fixed tables are context-owned so resumes never
    // consult process-global state or contend with another isolate.
    JsGeneratorStateRecord generators[JS_MAX_GENERATORS] = {};
    int generator_count = 0;
    JsAsyncContextStateRecord async_contexts[JS_MAX_ASYNC_CONTEXTS] = {};
    int async_context_count = 0;
    Item async_resolved_value = {};
    void* async_roots_registered_gc = NULL;
    uint64_t async_roots_registered_epoch = UINT64_MAX;
    int dynamic_func_counter = 0;

    int module_var_count = 0;
    // Test262 keeps its harness in one module slab while each script needs an
    // isolated copy of that binding prefix. These ids are per-runtime state,
    // never process-global, because harness closures retain their owner slab.
    uint32_t batch_test_module_state_id = UINT32_MAX;
    uint32_t batch_preamble_module_state_id = UINT32_MAX;
    uint32_t batch_preamble_var_count = 0;
    uint64_t heap_epoch = 1;

    JsRegexpLastMatch regexp_last_match = {};

    Item current_this = {0};
    // Call/constructor bindings are active execution semantics, not process
    // diagnostics.  They are owner-thread fields so dispatch stays ordinary
    // loads and stores with no lock or atomic operation.
    Item generator_callee_proto = {0};
    Item current_private_home_class = {0};
    int current_private_home_class_index = -1;
    int call_depth = 0;
    int call_stack_limit = 4096;
    Item new_target = {0};
    bool super_this_bound_stack[128] = {};
    Item super_this_value_slots[128] = {};
    JsItemStack super_this_values = {};
    Item* pending_call_args = NULL;
    int pending_call_argc = 0;
    const char* pending_call_source = NULL;
    int pending_call_source_len = 0;
    Map* cached_object_proto = NULL;
    bool resolving_object_proto = false;
    bool private_field_initializing = false;
    bool eval_initializer_context = false;
    int pending_args_is_strict = 0;
    Item pending_args_callee = {0};

    // Each context owns both range descriptors and the registry that resets
    // them. A heap replacement in one runtime must never touch another.
    JsRootRange event_loop_queue_roots = {};
    JsRootRange event_loop_raf_roots = {};
    JsRootRange* root_range_registry[JS_ROOT_RANGE_REGISTRY_MAX] = {};
    int root_range_registry_count = 0;
};

// This derived TLS cache is initialized once after the eval thread acquires
// its context. It must stay paired with `context` until thread teardown.
extern __thread JsRuntimeState* js_active_runtime_state;
bool js_runtime_state_init(EvalContext* context);
bool js_runtime_state_thread_matches(const EvalContext* context);
bool js_runtime_state_shutdown(EvalContext* context);
void js_runtime_state_release_heap_resources(void);
void js_runtime_state_destroy_context(void);
extern "C" bool js_promise_initial_unhandled_rejections_strict(void);

#define js_runtime_state (*js_active_runtime_state)

extern "C" Item* js_ensure_active_module_vars(void);
extern "C" Item** js_active_module_vars_slot(void);

// The caller's `with`-scope depth is a per-call dispatch input. The state is
// owner-local, so dispatch keeps the old direct-load cost without a call,
// lock, atomic, or shared-cache probe.
#define js_with_stack_state (js_runtime_state.with_scope.stack)
static inline int js_with_stack_depth_now(void) { return js_with_stack_state.depth; }

static inline Item*& js_active_module_vars_ref() {
    return *js_active_module_vars_slot();
}

#define js_input (js_runtime_state.input)
#define js_strict_mode (js_runtime_state.strict_mode)
#define js_intrinsic_state (js_runtime_state.intrinsics)
#define g_array_sym_iter_ever_set (js_intrinsic_state.array_sym_iter_ever_set)
#define js_module_vars (js_active_module_vars)
#define js_active_module_vars (js_active_module_vars_ref())
#define js_module_var_count (js_runtime_state.module_var_count)
#define js_heap_epoch (js_runtime_state.heap_epoch)
#define js_regexp_last_match (js_runtime_state.regexp_last_match)
#define js_current_this (js_runtime_state.current_this)
#define js_new_target (js_runtime_state.new_target)
#define js_super_this_bound_stack (js_runtime_state.super_this_bound_stack)
#define js_super_this_value_stack (js_runtime_state.super_this_values.roots.slots)
#define js_super_this_bound_depth (js_runtime_state.super_this_values.depth)
#define js_pending_call_args (js_runtime_state.pending_call_args)
#define js_pending_call_argc (js_runtime_state.pending_call_argc)
#define js_pending_call_source (js_runtime_state.pending_call_source)
#define js_pending_call_source_len (js_runtime_state.pending_call_source_len)
#define js_cached_object_proto (js_runtime_state.cached_object_proto)
#define js_resolving_object_proto (js_runtime_state.resolving_object_proto)
#define js_private_field_initializing (js_runtime_state.private_field_initializing)
#define js_eval_initializer_context (js_runtime_state.eval_initializer_context)
#define js_deferred_instance_field_class (js_runtime_state.operations.deferred_instance_field_class)
#define js_pending_args_is_strict (js_runtime_state.pending_args_is_strict)
#define js_pending_args_callee (js_runtime_state.pending_args_callee)
