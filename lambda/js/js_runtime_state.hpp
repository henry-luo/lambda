#pragma once

// js_runtime_state.hpp - shared JS runtime state surface.
//
// This header intentionally exposes only the mutable runtime-state capsule and
// legacy-name aliases used while the runtime is migrated away from free globals.

#include "js_runtime.h"
#include "js_builtin_catalog.hpp"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../runtime/runtime-state.h"
#include "../runtime/async.h"
#include "../runtime/root_vector.h"
#include "../../lib/hashmap.h"
#include "../../lib/arraylist.h"

#define JS_REGEXP_MAX_PAREN 9

struct JsFunction;
struct JsInterpEnv;
struct JsInterpGeneratorLoopContinuation;
struct JsInterpGeneratorListContinuation;
struct JsInterpGeneratorArrayBindingContinuation;
struct AstNode;
struct DomDocument;
struct DomElement;
struct UiContext;

// Namespace selection is the JS profile's counterpart to the shared module
// slab scope.  Keep restoration identical for AST and MIR module entries.
class JsModuleNamespaceScope {
public:
    JsModuleNamespaceScope(Item namespace_obj = ItemNull, bool active = false)
            : previous_(active ? js_set_active_module_namespace(namespace_obj) : ItemNull),
              active_(active) {}
    JsModuleNamespaceScope(const JsModuleNamespaceScope&) = delete;
    JsModuleNamespaceScope& operator=(const JsModuleNamespaceScope&) = delete;
    ~JsModuleNamespaceScope() {
        if (active_) js_set_active_module_namespace(previous_);
    }

private:
    Item previous_;
    bool active_;
};

struct JsTemplateRegistryEntry {
    int64_t site_id = 0;
    int count = 0;
    Item object = {};
    JsTemplateRegistryEntry* next = NULL;
};

struct JsMockSchedulerWait {
    // One scheduled wait owns its four async edges; a dynamic collection of
    // these records replaces the former capped parallel-root array.
    RootVector values = {};
    int64_t due_ms = 0;
};

struct JsEventLoopQueueState {
    // Each queue owns a GC Array of one RuntimeJob shape. Queue order stays
    // policy-specific while storage and captured context are shared (JSCU30).
    Item queue_storage[3] = {};
    RuntimeJobQueue next_tick_queue = {};
    RuntimeJobQueue microtask_queue = {};
    RuntimeJobQueue animation_frame_queue = {};
    bool microtask_running = false;

    int64_t next_raf_id = 1;
    bool auto_close_mode = false;
    // Static capture closes only after the document's load lifecycle completes.
    bool auto_close_after_load = false;
    double auto_close_settle_ms = 0.0;
    bool shutting_down = false;
    // Dynamic source compiled from a queued callback belongs to its parent turn.
    bool callback_running = false;
};

struct JsEventLoopTimerState {
    // The shared resource table owns live timer handles and their script
    // owners; each timer keeps only its scheduling/job-specific native tail.
    RuntimeResourceTable resources = {};
    int64_t next_id = 1;
    uint64_t progress_generation = 0;
    bool force_shutdown = false;
    bool nan_warning_emitted = false;
    bool negative_warning_emitted = false;
    bool virtual_clock_enabled = false;
    double virtual_clock_ms = 0.0;

    bool mock_scheduler_enabled = false;
    int64_t mock_scheduler_now_ms = 0;
    ArrayList* mock_waits = NULL;
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
// JSCU14(b): backed by the one runtime RootVector — no fixed slot array, no
// per-stack epoch, no reset-registry callback.
struct JsItemStack {
    RootVector vec = {};
};

static inline int js_item_stack_depth(JsItemStack* stack) {
    return stack ? (int)root_vector_count(&stack->vec) : 0;
}

// index must be below the current depth; an out-of-range index faults loudly
static inline Item& js_item_stack_at(JsItemStack* stack, int index) {
    return *root_vector_at(&stack->vec, index);
}

struct JsWithScopeState {
    JsItemStack stack = {};
    Item last_binding_slots[2] = {};
    JsRootRange last_binding_roots = {};
    bool last_binding_valid = false;
};

// Generated closures retain only their MIR context and source buffer. Names
// and observable strings are materialized through the owning module NameId
// table, so compiler pools never cross the compile/execute boundary.
// JSCU16: one owner per compiled unit -- its MIR context and the source
// buffer that unit's generated code still reads -- held in a dynamic store
// instead of two parallel fixed arrays with a hard cap.
struct JsCompiledArtifact {
    void* mir_context = NULL;
    char* source_owner = NULL;   // freed with the context; may be NULL
};

struct JsCodeStore {
    JsCompiledArtifact* artifacts = NULL;
    int count = 0;
    int capacity = 0;
};

bool js_code_store_push(JsCodeStore* store, void* mir_context);
// Hands the last pushed artifact ownership of a source buffer.
void js_code_store_attach_source(JsCodeStore* store, char* source_owner);
void* js_code_store_last_context(JsCodeStore* store);
void js_code_store_destroy(JsCodeStore* store);

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

struct JsReadlineInput {
    int64_t root_slot = -1;
};

struct JsReadlineState : JsNamespaceState {
    Item promises_namespace = {};
    Item completion_interface = {};
    RootVector input_values = {};
    ArrayList* inputs = NULL;
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
    // was a function-local `static Item`, which a precise collector never scans
    Item internal_add_abort_signal_namespace = {};
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
    // Assertion instances outlive the creating call and share one dynamic
    // root collection instead of a capped side registry.
    RootVector instances = {};
    uint64_t key_epoch = 0;

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
    bool redirect_stdout_to_stderr = false;
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
    Item default_view = {};
    Item title = {};
    Item fonts = {};
    bool design_mode = false;
    DomElement* active_element = NULL;
    DomDocument* current_document = NULL;
    UiContext* current_ui_context = NULL;
    bool host_driven_loop = false;
    DomDocument* main_document = NULL;
    // A queued response is either an owned UTF-8 string or NULL for Cancel.
    ArrayList* prompt_responses = NULL;
};

struct JsDomStorageEntry {
    char* key = NULL;
    char* value = NULL;
};

struct JsDomStorageState {
    Item object = {};
    ArrayList* entries = NULL;
};

struct JsDomMediaQueryState {
    int64_t object_slot = -1;
    char* query = NULL;
    bool matches = false;
};

struct JsDomPlatformState {
    JsDomStorageState local_storage = {};
    JsDomStorageState session_storage = {};
    ArrayList* media_queries = NULL;
    RootVector media_query_objects = {};
    bool media_query_roots_initialized = false;
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

// One global-environment row represents either a declarative lexical binding,
// an object-backed global var declaration, or a module-slot bridge. The row's
// key/value pair resides in the environment RootVector so GC ownership is
// independent of the native metadata tail (D5.3.5; JSCU29).
enum JsGlobalBindingKind : uint8_t {
    JS_GLOBAL_BINDING_LEXICAL,
    JS_GLOBAL_BINDING_OBJECT_VAR,
    JS_GLOBAL_BINDING_MODULE_VAR,
};

struct JsGlobalBinding {
    int64_t root_slot = -1;     // key at root_slot, value at root_slot + 1
    int module_index = -1;
    uint32_t module_state_id = UINT32_MAX;
    JsGlobalBindingKind kind = JS_GLOBAL_BINDING_LEXICAL;
    bool immutable = false;
};

enum JsGlobalEnvironmentSlot : int {
    JS_GLOBAL_ENV_GLOBAL_THIS,
    JS_GLOBAL_ENV_WINDOW_EVENT,
    JS_GLOBAL_ENV_OBJECT_VAR_GLOBAL,
    JS_GLOBAL_ENV_LEXICAL_GLOBAL,
    JS_GLOBAL_ENV_MODULE_GLOBAL,
    JS_GLOBAL_ENV_SLOT_COUNT,
};

struct JsGlobalEnvironment {
    RootVector roots = {};
    JsGlobalBinding* bindings = NULL;
    int binding_count = 0;
    int binding_capacity = 0;
    uint64_t object_var_epoch = 0;
    uint64_t lexical_epoch = 0;
    uint64_t module_epoch = 0;
    bool window_event_intercept_enabled = false;
};

bool js_global_environment_init(JsGlobalEnvironment* environment,
    Context* owner);
void js_global_environment_destroy(JsGlobalEnvironment* environment);
bool js_global_environment_ensure(JsGlobalEnvironment* environment);
Item& js_global_environment_slot(JsGlobalEnvironment* environment,
    JsGlobalEnvironmentSlot slot);
int js_global_environment_find(JsGlobalEnvironment* environment,
    JsGlobalBindingKind kind, Item key);
JsGlobalBinding* js_global_environment_binding_at(
    JsGlobalEnvironment* environment, int index);
Item js_global_environment_binding_key(JsGlobalEnvironment* environment,
    const JsGlobalBinding* binding);
Item js_global_environment_binding_value(JsGlobalEnvironment* environment,
    const JsGlobalBinding* binding);
void js_global_environment_set_binding_value(JsGlobalEnvironment* environment,
    JsGlobalBinding* binding, Item value);
bool js_global_environment_upsert(JsGlobalEnvironment* environment,
    JsGlobalBindingKind kind, Item key, Item value, bool immutable,
    int module_index, uint32_t module_state_id);
void js_global_environment_clear_bindings(JsGlobalEnvironment* environment);
void js_global_environment_remove_kind(JsGlobalEnvironment* environment,
    JsGlobalBindingKind kind);
void js_global_environment_release_module_bindings(
    JsGlobalEnvironment* environment, uint32_t first_module_state_id);

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

struct JsTest262AgentReport {
    int64_t root_slot = -1;
    int waiter_id = 0;
};

struct JsTest262AgentState : JsRootedState {
    Item object = {};
    // Agent slots contain only callback identities; report rows carry the
    // paired waiter metadata with their one rooted report value.
    RootVector callbacks = {};
    RootVector report_values = {};
    ArrayList* reports = NULL;
    int current_slot = -1;
    int64_t eval_script_active = 0;
    // Atomics waiter records are allocated only when the Atomics namespace is
    // materialized. They belong to this realm's Test262 agent simulation.
    void* atomics_waiter_state = NULL;
};

// Node process state is realm-local apart from the operating-system process.
// The one dynamic listener map is the authority for every process event.
struct JsProcessState : JsRootedState {
    Item argv = {};
    Item exec_argv = {};
    Item object = {};
    Item listener_map = {};
    Item ipc_pending_messages = {};
    int exit_code = 0;
    bool exit_requested = false;
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
    // Hook and deferred-destroy collections outlive native calls, so each
    // owns one growable rooted store instead of a capped parallel table.
    RootVector hooks = {};
    RootVector pending_destroy_resources = {};
    int64_t next_id = 2;
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
    RuntimeJobQueue unhandled_queue = {};
    // Keep the three GC-visible owner slots contiguous; the deque control
    // record is native metadata and must never be scanned as an Item range.
    Item unhandled_storage = {};
    Item domain_current = {};
    Item domain_namespace = {};
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

struct JsAsyncLocalStorageState {
    // Instances survive callbacks and therefore need stable, growable roots.
    RootVector instances = {};
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

// The common durable part of generator and async execution. It owns exactly
// the outliving activation edges; each state machine keeps its own semantic
// tail (D5.1.1v2, D6.2.2v2; JSCU32).
struct JsSuspendedActivation {
    TypeId type_id = LMD_TYPE_MAP;
    Context* runtime_context = NULL;
    void* state_fn = NULL;
    Item* env = NULL;
    int env_size = 0;
    int64_t state = 0;
    Item ast_function = {};
    Item ast_arguments = {};
    JsInterpEnv* ast_function_env = NULL;
    JsInterpEnv* ast_body_env = NULL;
    // Generator yields and async awaits replay through the same Item ledger.
    Item ast_replay_values = {};
    int64_t ast_replay_skip = 0;
    JsInterpGeneratorLoopContinuation* ast_loop_continuations = NULL;
    bool ast_initialized = false;
};

struct JsGeneratorStateRecord : JsSuspendedActivation {
    bool done = false;
    bool started = false;
    bool executing = false;
    bool is_async = false;
    Item private_home_class = {};
    Item delegate = {};
    int64_t delegate_resume = -1;
    int delegate_idx = 0;
    Item ast_this = {};
    // Terminal-yield loop continuations keep AST generators resumable without
    // replaying completed iterations on every next().
    JsInterpGeneratorListContinuation* ast_list_continuation = NULL;
    // A destructuring target can suspend after IteratorStep. Retain its
    // iterator/value cursor so replay does not advance the iterator twice.
    JsInterpGeneratorArrayBindingContinuation* ast_array_binding_continuations = NULL;
    bool ast_resumable_loop_active = false;
    // Retain an injected throw/return while a finally block yields before it
    // can finish propagating that abrupt completion.
    int64_t ast_pending_resume_yield = 0;
    Item ast_pending_resume_input = {};
};

struct JsAsyncContextStateRecord : JsSuspendedActivation {
    Item promise = {};
    // resumed MIR property names must use the module image that compiled the body.
    uint32_t module_state_id = UINT32_MAX;
    Item this_val = {};
    AstNode* ast_resume_statement = NULL;
};

bool js_root_range_ensure_registered(JsRootRange* range);
void js_root_range_unregister(JsRootRange* range);
void js_root_range_clear(JsRootRange* range);
bool js_root_range_register_reset(JsRootRange* range, void* owner,
                                  JsRootRangeResetFn reset);
void js_root_range_reset_all(void);
void js_readline_state_destroy(JsReadlineState* state);
void js_test262_agent_state_destroy(JsTest262AgentState* state);
void js_item_stack_init(JsItemStack* stack, Context* owner, const char* name);
void js_item_stack_destroy(JsItemStack* stack);
bool js_item_stack_push(JsItemStack* stack, Item value);
void js_item_stack_pop(JsItemStack* stack);
void js_item_stack_clear(JsItemStack* stack);
void js_item_stack_shrink(JsItemStack* stack, int depth);

// Source records span a runtime eval or a VM-originated function call. One
// row owns the complete source-context fact; its Item fields live in the
// paired RootVector slots so the native metadata remains unscanned.
struct JsEvalSourceRecord {
    int64_t filename_slot = -1;
    int64_t source_slot = -1;
    int64_t line_offset = 0;
    int64_t column_offset = 0;
    bool compact_stack = false;
};

struct JsEvalSourceState {
    RootVector values = {};
    ArrayList* records = NULL;
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
// Each group's binding count is its key lane's count; the POD columns are
// indexed by the same position and bounded by the explicit *_BIND_MAX.
struct JsEvalBridgeState {
    RootVector env_keys = {};
    RootVector env_old_values = {};
    bool env_had_own[JS_EVAL_ENV_BIND_MAX] = {};
    bool env_from_journal[JS_EVAL_ENV_BIND_MAX] = {};
    int env_frame_marks[JS_EVAL_ENV_FRAME_MAX] = {};
    int env_frame_depth = 0;

    RootVector global_lexical_keys = {};
    RootVector global_lexical_old_values = {};
    bool global_lexical_had_own[JS_EVAL_ENV_BIND_MAX] = {};
    bool global_lexical_immutable[JS_EVAL_ENV_BIND_MAX] = {};
    int global_lexical_frame_marks[JS_EVAL_ENV_FRAME_MAX] = {};
    int global_lexical_frame_depth = 0;

    RootVector private_unscoped_keys = {};
    RootVector private_scoped_keys = {};
    int private_frame_marks[JS_EVAL_LOCAL_FRAME_MAX] = {};
    int private_frame_depth = 0;
};

typedef struct JsEvalLocalFrameMarks {
    int local_mark;
    int lexical_mark;
    int immutable_mark;
} JsEvalLocalFrameMarks;

// Caller-local records survive multiple direct eval calls in one generated
// function. They deliberately do not share bridge or source depths.
struct JsEvalLocalState {
    RootVector keys = {};
    RootVector values = {};
    JsEvalLocalFrameMarks frame_marks[JS_EVAL_LOCAL_FRAME_MAX] = {};
    int frame_depth = 0;
    RootVector lexical_keys = {};
    RootVector immutable_keys = {};
};

struct JsEvalState {
    JsEvalSourceState source = {};
    JsEvalBridgeState bridge = {};
    JsEvalLocalState local = {};
};

// One catalog of the eval journals' rooted lanes for init/destroy/clear.
#define JS_EVAL_STATE_VECTORS(M, state) \
    M(&(state)->source.values, "eval source values") \
    M(&(state)->bridge.env_keys, "eval env keys") \
    M(&(state)->bridge.env_old_values, "eval env old values") \
    M(&(state)->bridge.global_lexical_keys, "eval global lexical keys") \
    M(&(state)->bridge.global_lexical_old_values, "eval global lexical old values") \
    M(&(state)->bridge.private_unscoped_keys, "eval private unscoped keys") \
    M(&(state)->bridge.private_scoped_keys, "eval private scoped keys") \
    M(&(state)->local.keys, "eval local keys") \
    M(&(state)->local.values, "eval local values") \
    M(&(state)->local.lexical_keys, "eval lexical keys") \
    M(&(state)->local.immutable_keys, "eval immutable keys")

void js_eval_state_vectors_init(JsEvalState* state, Context* owner);
void js_eval_state_vectors_destroy(JsEvalState* state);

void js_eval_state_reset(JsEvalState* state);
void js_eval_state_assert_clear(JsEvalState* state, const char* reset_name);

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

// A synchronous call has one ambient owner. Its Item homes are either the
// context-owned base RootVector or one exact native RootFrame, so a nested
// call replaces one activation link instead of mutating parallel globals
// (D5.1.1v2, D6.2.2v2; JSCU28).
typedef enum JsCallActivationItem {
    JS_CALL_ACTIVATION_THIS = 0,
    JS_CALL_ACTIVATION_NEW_TARGET,
    JS_CALL_ACTIVATION_GENERATOR_CALLEE_PROTO,
    JS_CALL_ACTIVATION_PRIVATE_HOME_CLASS,
    JS_CALL_ACTIVATION_CALLEE,
    JS_CALL_ACTIVATION_SUPER_THIS,
    JS_CALL_ACTIVATION_ITEM_COUNT,
} JsCallActivationItem;

struct JsCallActivation {
    JsCallActivation* previous = NULL;
    Item* items[JS_CALL_ACTIVATION_ITEM_COUNT] = {};
    Item* args = NULL;
    int arg_count = 0;
    const char* source = NULL;
    int source_len = 0;
    int args_is_strict = 0;
    bool derived_constructor = false;
    bool super_this_bound = false;
};

struct JsExecutionState {
    // The base activation remains live outside a JS call and is rooted by the
    // runtime's one growable root primitive. Before a heap exists, the
    // fallback keeps bootstrap bookkeeping valid; it is copied into the
    // precise homes before JavaScript can publish an Item.
    RootVector base_activation_items = {};
    Item base_activation_fallback[JS_CALL_ACTIVATION_ITEM_COUNT] = {};
    JsCallActivation base_activation = {};
    JsCallActivation* current_activation = NULL;
    int call_depth = 0;
    int call_stack_limit = 4096;
};

struct JsRuntimeState {
    JsDnsState dns = {};
    JsBuiltinCacheState* builtin_cache = NULL;
    JsReadlineState* readline = NULL;
    JsBufferState buffer = {};
    JsHttpsState https = {};
    JsUtilState util = {};
    JsCryptoState crypto = {};
    JsChildProcessState child_process = {};
    JsTlsState tls = {};
    JsStreamState stream = {};
    JsHttpState http = {};
    JsAssertState* assert = NULL;
    JsNetState net = {};
    JsHostHooksState host_hooks = {};
    JsFsState fs = {};
    JsClipboardState clipboard = {};
    JsDomState dom = {};
    // Listener records contain native precise-root slots and DOM pins. Keep
    // their opaque storage with the owning realm; dispatch reads it directly
    // after the context has been bound, with no shared synchronization.
    // JSCU17: DOM/web state moved to context capsules (peers of this JS
    // capsule, not children of it); see runtime/context_capsule.h.
    HashMap* dom_attached_expando_roots = NULL;
    JsStringConcatState* string_concat = NULL;
    JsGlobalEnvironment* global_environment = NULL;   // one dynamic realm binding table (JSCU29)
    JsRuntimeCoreCacheState runtime_core_cache = {};
    JsFunctionPrototypeState function_prototypes = {};
    JsGlobalStringCacheState* global_string_caches = NULL;
    JsConstructorCacheState* constructors = NULL;
    JsRuntimeNamespaceState* namespaces = NULL;
    // VM namespaces and generated module identifiers are observable realm
    // state. Keeping them here prevents a new document from accepting an
    // equal epoch and reusing an Item from a retired document heap.
    JsVmRuntimeState vm = {};
    JsTest262AgentState* test262_agent = NULL;
    JsProcessState* process = NULL;
    JsIteratorState* iterators = NULL;
    JsConsoleState console = {};
    JsRuntimeOperationState operations = {};
    JsWellKnownRefs well_known = {};
    JsAsyncHooksState* async_hooks = NULL;   // JSCU16: allocated with the realm, not embedded
    JsPromiseRuntimeState promises = {};
    JsModuleRuntimeState modules = {};
    JsClusterState cluster = {};
    JsAsyncLocalStorageState* async_local_storage = NULL;
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
    JsIntrinsicState* intrinsics = NULL;
    JsEvalState eval = {};
    JsEventLoopQueueState* event_loop = NULL;   // JSCU16: allocated with the realm, not embedded
    JsEventLoopTimerState* timers = NULL;   // JSCU16: allocated with the realm, not embedded
    JsWithScopeState with_scope = {};
    JsCodeStore code_store = {};
    void* dynamic_function_cache_state = NULL;
    // Timeout recovery may interrupt JS compilation before the ordinary
    // teardown path runs.  Its compiler owners stay with this realm, never in
    // process globals; compilation is cold and generated code never reads it.
    void* mir_compile_recovery_state = NULL;
    // Wrapper identity is observable through .prototype and must therefore be
    // private to the context that owns the function objects and their heap.
    // §14.1: the two fixed 512-entry tables were 12,288 B — 57 % of this
    // record — while a bare realm already reaches thousands of functions, so
    // the cache saturated in ordinary use and simply stopped deduplicating.
    // The cached wrappers are pool-backed and carry no registered GC root, so
    // the arrays may move freely.
    struct JsFunctionCacheKey {
        uint64_t target_bits;
        int16_t arity;
        uint8_t kind;
        uint8_t policy;
        uint8_t capabilities;
    };
    JsFunctionCacheKey* function_cache_keys = nullptr;
    JsFunction** function_cache_values = nullptr;
    int function_cache_count = 0;
    int function_cache_capacity = 0;
    int function_cache_suppress_depth = 0;
    // Resumable code retains function environments after its creating native
    // frame has returned.  The fixed tables are context-owned so resumes never
    // consult process-global state or contend with another isolate.
    // JSCU10: async activations are GC-owned frames, not a fixed table. Only
    // the single await scratch value keeps an epoch-guarded root.
    Item async_resolved_value = {};
    void* async_roots_registered_gc = NULL;
    uint64_t async_roots_registered_epoch = UINT64_MAX;
    int dynamic_func_counter = 0;

    // Test262 keeps its harness in one module slab while each script needs an
    // isolated copy of that binding prefix. These ids are per-runtime state,
    // never process-global, because harness closures retain their owner slab.
    uint32_t batch_test_module_state_id = UINT32_MAX;
    uint32_t batch_preamble_module_state_id = UINT32_MAX;
    uint32_t batch_preamble_var_count = 0;
    uint64_t heap_epoch = 1;

    JsRegexpLastMatch regexp_last_match = {};

    JsExecutionState execution = {};
    Map* cached_object_proto = NULL;
    bool resolving_object_proto = false;
    bool private_field_initializing = false;
    bool eval_initializer_context = false;

    // Each context owns both range descriptors and their growable native
    // registry. A heap replacement in one runtime must never touch another.
    JsRootRange event_loop_queue_roots = {};
    ArrayList* root_range_registry = NULL;
};

// This derived TLS cache is initialized once after the eval thread acquires
// its context. It must stay paired with `context` until thread teardown.
extern __thread JsRuntimeState* js_active_runtime_state;
static inline JsRuntimeState* js_runtime_state_for(EvalContext* owner) {
    return owner ? (JsRuntimeState*)context_capsule(owner, CONTEXT_CAPSULE_JS_RUNTIME) : NULL;
}

static inline JsCallActivation* js_call_activation_current(void) {
    JsExecutionState* execution = &js_active_runtime_state->execution;
    return execution->current_activation ? execution->current_activation
        : &execution->base_activation;
}

static inline Item& js_call_activation_item(JsCallActivationItem slot) {
    Item* item = js_call_activation_current()->items[slot];
    // Runtime initialization publishes every base home before JS dispatch;
    // side-rooted activations copy the same complete slot shape.
    return *item;
}

static inline void js_call_activation_push(JsCallActivation* activation) {
    if (!activation) return;
    activation->previous = js_call_activation_current();
    js_active_runtime_state->execution.current_activation = activation;
}

static inline void js_call_activation_pop(JsCallActivation* activation) {
    if (!activation || js_active_runtime_state->execution.current_activation != activation) return;
    js_active_runtime_state->execution.current_activation = activation->previous;
}
bool js_runtime_state_init(EvalContext* context);
bool js_runtime_state_thread_matches(const EvalContext* context);
bool js_runtime_state_shutdown(EvalContext* context);
void js_runtime_state_release_heap_resources(void);
void js_runtime_state_destroy_context(void);
extern "C" bool js_promise_initial_unhandled_rejections_strict(void);

#define js_runtime_state (*js_active_runtime_state)

// The caller's `with`-scope depth is a per-call dispatch input. The state is
// owner-local, so dispatch keeps the old direct-load cost without a call,
// lock, atomic, or shared-cache probe.
#define js_with_stack_state (js_runtime_state.with_scope.stack)

#define js_input (js_runtime_state.input)
#define js_strict_mode (js_runtime_state.strict_mode)
#define js_intrinsic_state (*js_runtime_state.intrinsics)
#define g_array_sym_iter_ever_set (js_intrinsic_state.array_sym_iter_ever_set)
#define js_heap_epoch (js_runtime_state.heap_epoch)
#define js_regexp_last_match (js_runtime_state.regexp_last_match)
#define js_current_this js_call_activation_item(JS_CALL_ACTIVATION_THIS)
#define js_new_target js_call_activation_item(JS_CALL_ACTIVATION_NEW_TARGET)
#define js_generator_callee_proto js_call_activation_item(JS_CALL_ACTIVATION_GENERATOR_CALLEE_PROTO)
#define js_current_private_home_class js_call_activation_item(JS_CALL_ACTIVATION_PRIVATE_HOME_CLASS)
#define js_pending_args_callee js_call_activation_item(JS_CALL_ACTIVATION_CALLEE)
#define js_super_this_value js_call_activation_item(JS_CALL_ACTIVATION_SUPER_THIS)
#define js_pending_call_args (js_call_activation_current()->args)
#define js_pending_call_argc (js_call_activation_current()->arg_count)
#define js_pending_call_source (js_call_activation_current()->source)
#define js_pending_call_source_len (js_call_activation_current()->source_len)
#define js_cached_object_proto (js_runtime_state.cached_object_proto)
#define js_resolving_object_proto (js_runtime_state.resolving_object_proto)
#define js_private_field_initializing (js_runtime_state.private_field_initializing)
#define js_eval_initializer_context (js_runtime_state.eval_initializer_context)
#define js_deferred_instance_field_class (js_runtime_state.operations.deferred_instance_field_class)
#define js_pending_args_is_strict (js_call_activation_current()->args_is_strict)
