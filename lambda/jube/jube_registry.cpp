#include "jube_registry.h"
#include "../dom/dom_core.h"
#include "jube_interface.h"
#include "jube_language.h"
#include "../runtime/ast.hpp"
#include "../runtime/module_registry.h"
#include "../runtime/transpiler.hpp"
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
#include "../js/js_network_service.h"
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
#include "../../lib/strbuf.h"
#include "../../lib/mem_factory.h"
#include "../../lib/arraylist.h"
#include "../../lib/hashmap.h"
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
#define JUBE_NODE_MODULE_STATE_SLOTS 64u

typedef struct JubeStaticModuleEntry {
    const JubeModuleDef* module;
    uint32_t activation_state;
    EvalContext* activating_context;
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
static bool jube_manifest_paths_scanned = false;
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
static int jube_load_manifest_path_internal(const char* manifest_path, const char* selector,
                                            const char* expected_name);
static bool jube_activate_module_descriptor(const JubeModuleDef* module);

struct JubeMirCursorSlot {
    MirEmitter* emitter;
    uint32_t generation;
    ArrayList* function_handles;
    ArrayList* label_handles;
    ArrayList* prototype_handles;
};

struct JubeMirModuleSlot {
    MIR_module_t module;
    MIR_context_t context;
    uint32_t generation;
};

struct JubeMirFunctionHandle {
    MIR_item_t item;
    MIR_func_t function;
};

struct JubeMirLabelHandle {
    MIR_label_t label;
};

struct JubeMirPrototypeHandle {
    MIR_item_t item;
};

struct JubeMirFunctionStateToken {
    MIR_item_t function_item;
    MIR_func_t function;
    MirFunctionArgumentState arguments;
};

struct JubeMirStateTokenEntry {
    void* token;
    JubeMirCursorSlot* owner_slot;
    JubeMirFunctionStateToken* state;
};

static ArrayList* jube_mir_cursor_slots = NULL;
static ArrayList* jube_mir_module_slots = NULL;
static ArrayList* jube_mir_state_tokens = NULL;
static uintptr_t jube_mir_state_token_next = 1;

struct NodeRuntimeSession {
    uint64_t generation;
    bool live;
    bool detaching;
    ArrayList* resource_slots;
    ArrayList* async_work;
    uint32_t async_work_next_id;
    void* module_states[JUBE_NODE_MODULE_STATE_SLOTS];
    NodeTraceState trace;
    JsCjsState cjs;
    JsCommonJsCompileCacheState commonjs_compile_cache;
    JsDiagnosticsChannelState diagnostics_channels;
    JsPermissionPolicy permission_policy;
    JsCryptoNativeState crypto_native;
};

struct JubeNodeResource {
    uint32_t id;
    Item value;
    const char* kind;
    JubeNodeResourceCloseCallback close_callback;
    void* close_user;
};

struct JubeNodeResourceSlot {
    uint16_t generation;
    JubeNodeResource* resource;
};

#define JUBE_NODE_RESOURCE_INDEX_BITS 16u
#define JUBE_NODE_RESOURCE_INDEX_MASK ((uint32_t)((1u << JUBE_NODE_RESOURCE_INDEX_BITS) - 1u))

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
    return context ? (NodeRuntimeSession*)((EvalContext*)context)->jube_node_session : NULL;
}

static void jube_active_node_runtime_session_set(NodeRuntimeSession* session) {
    if (context) ((EvalContext*)context)->jube_node_session = session;
}

#define jube_active_node_runtime_session (jube_active_node_runtime_session_current())

void* jube_node_session_module_state_get(void* session_handle, uint32_t slot, size_t size) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (!session || !session->live || slot >= JUBE_NODE_MODULE_STATE_SLOTS || size == 0) return NULL;
    jube_runtime_session_lock_acquire();
    if (!session->module_states[slot]) {
        // Module attach is the only allocation point. Runtime dispatch reads
        // this fixed context-owned slot without a lock or atomic operation.
        session->module_states[slot] = mem_calloc(1, size, MEM_CAT_SYSTEM);
    }
    void* state = session->module_states[slot];
    jube_runtime_session_lock_release();
    return state;
}

void* jube_node_current_module_state(uint32_t slot) {
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    return session && session->live && slot < JUBE_NODE_MODULE_STATE_SLOTS
        ? session->module_states[slot] : NULL;
}

static void jube_node_session_module_states_destroy(NodeRuntimeSession* session) {
    if (!session) return;
    for (uint32_t i = 0; i < JUBE_NODE_MODULE_STATE_SLOTS; i++) {
        mem_free(session->module_states[i]);
        session->module_states[i] = NULL;
    }
}

static void jube_node_session_state_init(NodeRuntimeSession* session) {
    if (!session) return;
    session->cjs.module_stack.roots.slots = session->cjs.module_stack_slots;
    session->cjs.module_stack.roots.slot_count = JS_CJS_STACK_MAX;
    session->cjs.module_stack.roots.name = "CommonJS module stack";
    session->diagnostics_channels.roots.slots =
        &session->diagnostics_channels.namespace_object;
    session->diagnostics_channels.roots.slot_count =
        1 + 2 * JS_DIAGNOSTICS_CHANNEL_MAX + 6 + JS_DIAGNOSTICS_DEFERRED_ERROR_MAX;
    session->diagnostics_channels.roots.name = "diagnostics channel state";
    session->diagnostics_channels.namespace_epoch = UINT64_MAX;
}

static void jube_node_session_state_clear(NodeRuntimeSession* session) {
    if (!session) return;
    memset(&session->cjs, 0, sizeof(session->cjs));
    memset(&session->commonjs_compile_cache, 0, sizeof(session->commonjs_compile_cache));
    memset(&session->diagnostics_channels, 0, sizeof(session->diagnostics_channels));
    memset(&session->permission_policy, 0, sizeof(session->permission_policy));
    memset(&session->crypto_native, 0, sizeof(session->crypto_native));
}

struct JubeNodeAsyncWork {
    uv_work_t request;
    void* session;
    JubeAsyncWorkCallback work;
    JubeAsyncCompletionCallback complete;
    JubeAsyncDestroyCallback destroy;
    void* user;
    uint32_t request_id;
};

// Context identities remain raw host-owned values until every compatibility
// service can be moved to the same tagged registry without changing teardown.
static MIR_context_t jube_host_mir_context_from_handle(void* mir_context) {
    return (MIR_context_t)mir_context;
}

static MIR_context_t jube_host_mir_context_from_argument(void* mir_context) {
    return (MIR_context_t)mir_context;
}

static void jube_host_mir_context_slots_cleanup(void) {
}

static JubeMirModuleSlot* jube_host_mir_module_slot(void* mir_module) {
    uintptr_t token = (uintptr_t)mir_module;
    uint32_t index = (uint32_t)(token & JUBE_MIR_CURSOR_INDEX_MASK);
    uint32_t generation = (uint32_t)(token >> JUBE_MIR_CURSOR_INDEX_BITS);
    if (!index || !generation || !jube_mir_module_slots ||
            index > (uint32_t)jube_mir_module_slots->length) return NULL;
    JubeMirModuleSlot* slot = (JubeMirModuleSlot*)
        jube_mir_module_slots->data[index - 1];
    return slot && slot->module && slot->generation == generation ? slot : NULL;
}

static void* jube_host_mir_module_register(MIR_context_t context, MIR_module_t module) {
    if (!context || !module) return NULL;
    if (!jube_mir_module_slots) {
        jube_mir_module_slots = arraylist_new(8);
        if (!jube_mir_module_slots) return NULL;
    }
    for (int i = 0; i < jube_mir_module_slots->length; i++) {
        JubeMirModuleSlot* slot = (JubeMirModuleSlot*)jube_mir_module_slots->data[i];
        if (!slot || slot->module) continue;
        slot->generation++;
        if (slot->generation == 0) slot->generation++;
        slot->context = context;
        slot->module = module;
        return (void*)(uintptr_t)(((uint64_t)slot->generation <<
            JUBE_MIR_CURSOR_INDEX_BITS) | (uint64_t)(i + 1));
    }
    if (jube_mir_module_slots->length >= (int)JUBE_MIR_CURSOR_INDEX_MASK) return NULL;
    JubeMirModuleSlot* slot = (JubeMirModuleSlot*)mem_calloc(1,
        sizeof(JubeMirModuleSlot), MEM_CAT_SYSTEM);
    if (!slot) return NULL;
    slot->context = context;
    slot->module = module;
    slot->generation = 1;
    if (!arraylist_append(jube_mir_module_slots, slot)) {
        mem_free(slot);
        return NULL;
    }
    int index = jube_mir_module_slots->length;
    return (void*)(uintptr_t)(((uint64_t)slot->generation <<
        JUBE_MIR_CURSOR_INDEX_BITS) | (uint64_t)index);
}

static MIR_module_t jube_host_mir_module_from_handle(MIR_context_t context,
        void* mir_module) {
    JubeMirModuleSlot* slot = jube_host_mir_module_slot(mir_module);
    return slot && slot->context == context ? slot->module : NULL;
}

static void jube_host_mir_module_unregister(void* mir_module) {
    JubeMirModuleSlot* slot = jube_host_mir_module_slot(mir_module);
    if (slot) {
        slot->module = NULL;
        slot->context = NULL;
    }
}

static void jube_host_mir_modules_invalidate_context(MIR_context_t context) {
    if (!context || !jube_mir_module_slots) return;
    for (int i = 0; i < jube_mir_module_slots->length; i++) {
        JubeMirModuleSlot* slot = (JubeMirModuleSlot*)jube_mir_module_slots->data[i];
        if (slot && slot->module && slot->context == context) {
            slot->module = NULL;
            slot->context = NULL;
        }
    }
}

static void jube_host_mir_module_slots_cleanup(void) {
    if (!jube_mir_module_slots) return;
    for (int i = 0; i < jube_mir_module_slots->length; i++) {
        mem_free(jube_mir_module_slots->data[i]);
    }
    arraylist_free(jube_mir_module_slots);
    jube_mir_module_slots = NULL;
}

static JubeMirCursorSlot* jube_host_mir_cursor_slot(void* compiler_cursor) {
    uintptr_t token = (uintptr_t)compiler_cursor;
    uint32_t index = (uint32_t)(token & JUBE_MIR_CURSOR_INDEX_MASK);
    uint32_t generation = (uint32_t)(token >> JUBE_MIR_CURSOR_INDEX_BITS);
    if (!index || !generation || !jube_mir_cursor_slots ||
            index > (uint32_t)jube_mir_cursor_slots->length) return NULL;
    JubeMirCursorSlot* slot = (JubeMirCursorSlot*)
        jube_mir_cursor_slots->data[index - 1];
    return slot && slot->generation == generation ? slot : NULL;
}

static void* jube_host_mir_cursor_register(MirEmitter* emitter) {
    if (!emitter) return NULL;
    if (!jube_mir_cursor_slots) {
        jube_mir_cursor_slots = arraylist_new(8);
        if (!jube_mir_cursor_slots) return NULL;
    }
    for (int i = 0; i < jube_mir_cursor_slots->length; i++) {
        JubeMirCursorSlot* slot = (JubeMirCursorSlot*)jube_mir_cursor_slots->data[i];
        if (!slot || slot->emitter) continue;
        slot->generation++;
        if (slot->generation == 0) slot->generation++;
        slot->emitter = emitter;
        return (void*)(uintptr_t)(((uint64_t)slot->generation <<
            JUBE_MIR_CURSOR_INDEX_BITS) | (uint64_t)(i + 1));
    }
    if (jube_mir_cursor_slots->length >= (int)JUBE_MIR_CURSOR_INDEX_MASK) return NULL;
    JubeMirCursorSlot* slot = (JubeMirCursorSlot*)mem_calloc(1,
        sizeof(JubeMirCursorSlot), MEM_CAT_SYSTEM);
    if (!slot) return NULL;
    slot->emitter = emitter;
    slot->generation = 1;
    if (!arraylist_append(jube_mir_cursor_slots, slot)) {
        mem_free(slot);
        return NULL;
    }
    int index = jube_mir_cursor_slots->length;
    return (void*)(uintptr_t)(((uint64_t)slot->generation <<
        JUBE_MIR_CURSOR_INDEX_BITS) | (uint64_t)index);
}

static MirEmitter* jube_host_mir_cursor_emitter(void* compiler_cursor) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    return slot ? slot->emitter : NULL;
}

static void jube_host_mir_cursor_unregister(void* compiler_cursor) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (slot) slot->emitter = NULL;
}

static void jube_host_mir_cursor_function_handles_cleanup(JubeMirCursorSlot* slot) {
    if (!slot || !slot->function_handles) return;
    for (int i = 0; i < slot->function_handles->length; i++) {
        mem_free(slot->function_handles->data[i]);
    }
    arraylist_free(slot->function_handles);
    slot->function_handles = NULL;
}

static void jube_host_mir_cursor_label_handles_cleanup(JubeMirCursorSlot* slot) {
    if (!slot || !slot->label_handles) return;
    for (int i = 0; i < slot->label_handles->length; i++) {
        mem_free(slot->label_handles->data[i]);
    }
    arraylist_free(slot->label_handles);
    slot->label_handles = NULL;
}

static void jube_host_mir_cursor_prototype_handles_cleanup(JubeMirCursorSlot* slot) {
    if (!slot || !slot->prototype_handles) return;
    for (int i = 0; i < slot->prototype_handles->length; i++) {
        mem_free(slot->prototype_handles->data[i]);
    }
    arraylist_free(slot->prototype_handles);
    slot->prototype_handles = NULL;
}

static bool jube_host_mir_cursor_track_function(void* compiler_cursor,
        MIR_item_t item, MIR_func_t function) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (!slot || !slot->emitter || !item) return false;
    if (!slot->function_handles) {
        slot->function_handles = arraylist_new(8);
        if (!slot->function_handles) return false;
    }
    JubeMirFunctionHandle* handle = (JubeMirFunctionHandle*)mem_calloc(1,
        sizeof(JubeMirFunctionHandle), MEM_CAT_SYSTEM);
    if (!handle) return false;
    handle->item = item;
    handle->function = function;
    if (!arraylist_append(slot->function_handles, handle)) {
        mem_free(handle);
        return false;
    }
    return true;
}

static bool jube_host_mir_cursor_owns_function(void* compiler_cursor,
        MIR_item_t item, MIR_func_t function) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (!slot || !slot->emitter || !slot->function_handles) return false;
    for (int i = 0; i < slot->function_handles->length; i++) {
        JubeMirFunctionHandle* handle = (JubeMirFunctionHandle*)
            slot->function_handles->data[i];
        if (handle && handle->item == item && handle->function == function) return true;
    }
    return false;
}

static bool jube_host_mir_cursor_owns_function_item(void* compiler_cursor,
        MIR_item_t item) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (!slot || !slot->emitter || !slot->function_handles) return false;
    for (int i = 0; i < slot->function_handles->length; i++) {
        JubeMirFunctionHandle* handle = (JubeMirFunctionHandle*)
            slot->function_handles->data[i];
        if (handle && handle->item == item) return true;
    }
    return false;
}

static bool jube_host_mir_cursor_track_prototype(void* compiler_cursor,
        MIR_item_t item) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (!slot || !slot->emitter || !item) return false;
    if (!slot->prototype_handles) {
        slot->prototype_handles = arraylist_new(8);
        if (!slot->prototype_handles) return false;
    }
    JubeMirPrototypeHandle* handle = (JubeMirPrototypeHandle*)mem_calloc(1,
        sizeof(JubeMirPrototypeHandle), MEM_CAT_SYSTEM);
    if (!handle) return false;
    handle->item = item;
    if (!arraylist_append(slot->prototype_handles, handle)) {
        mem_free(handle);
        return false;
    }
    return true;
}

static bool jube_host_mir_cursor_owns_prototype(void* compiler_cursor,
        MIR_item_t item) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (!slot || !slot->emitter || !slot->prototype_handles) return false;
    for (int i = 0; i < slot->prototype_handles->length; i++) {
        JubeMirPrototypeHandle* handle = (JubeMirPrototypeHandle*)
            slot->prototype_handles->data[i];
        if (handle && handle->item == item) return true;
    }
    return false;
}

static bool jube_host_mir_cursor_track_label(void* compiler_cursor, MIR_label_t label) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (!slot || !slot->emitter || !label) return false;
    if (!slot->label_handles) {
        slot->label_handles = arraylist_new(8);
        if (!slot->label_handles) return false;
    }
    JubeMirLabelHandle* handle = (JubeMirLabelHandle*)mem_calloc(1,
        sizeof(JubeMirLabelHandle), MEM_CAT_SYSTEM);
    if (!handle) return false;
    handle->label = label;
    if (!arraylist_append(slot->label_handles, handle)) {
        mem_free(handle);
        return false;
    }
    return true;
}

static bool jube_host_mir_cursor_owns_label(void* compiler_cursor, MIR_label_t label) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (!slot || !slot->emitter || !slot->label_handles) return false;
    for (int i = 0; i < slot->label_handles->length; i++) {
        JubeMirLabelHandle* handle = (JubeMirLabelHandle*)slot->label_handles->data[i];
        if (handle && handle->label == label) return true;
    }
    return false;
}

static JubeMirStateTokenEntry* jube_host_mir_state_token_find(void* token,
        int* out_index) {
    if (out_index) *out_index = -1;
    if (!token || !jube_mir_state_tokens) return NULL;
    for (int i = 0; i < jube_mir_state_tokens->length; i++) {
        JubeMirStateTokenEntry* entry = (JubeMirStateTokenEntry*)
            jube_mir_state_tokens->data[i];
        if (entry && entry->token == token) {
            if (out_index) *out_index = i;
            return entry;
        }
    }
    return NULL;
}

static void jube_host_mir_state_token_remove_at(int index) {
    if (!jube_mir_state_tokens || index < 0 || index >= jube_mir_state_tokens->length) return;
    JubeMirStateTokenEntry* entry = (JubeMirStateTokenEntry*)
        jube_mir_state_tokens->data[index];
    if (entry) {
        mem_free(entry->state);
        mem_free(entry);
    }
    arraylist_remove(jube_mir_state_tokens, index);
}

static void jube_host_mir_state_tokens_discard_slot(JubeMirCursorSlot* slot) {
    if (!slot || !jube_mir_state_tokens) return;
    for (int i = jube_mir_state_tokens->length - 1; i >= 0; i--) {
        JubeMirStateTokenEntry* entry = (JubeMirStateTokenEntry*)
            jube_mir_state_tokens->data[i];
        if (entry && entry->owner_slot == slot) jube_host_mir_state_token_remove_at(i);
    }
}

static void jube_host_mir_state_tokens_cleanup(void) {
    if (!jube_mir_state_tokens) return;
    while (jube_mir_state_tokens->length > 0) {
        jube_host_mir_state_token_remove_at(jube_mir_state_tokens->length - 1);
    }
    arraylist_free(jube_mir_state_tokens);
    jube_mir_state_tokens = NULL;
}

static void* jube_host_mir_state_token_register(void* compiler_cursor,
        JubeMirFunctionStateToken* state) {
    JubeMirCursorSlot* slot = jube_host_mir_cursor_slot(compiler_cursor);
    if (!slot || !slot->emitter || !state ||
            jube_mir_state_token_next > (UINTPTR_MAX >> 1)) return NULL;
    if (!jube_mir_state_tokens) {
        jube_mir_state_tokens = arraylist_new(8);
        if (!jube_mir_state_tokens) return NULL;
    }
    JubeMirStateTokenEntry* entry = (JubeMirStateTokenEntry*)mem_calloc(1,
        sizeof(JubeMirStateTokenEntry), MEM_CAT_SYSTEM);
    if (!entry) return NULL;
    entry->token = (void*)(uintptr_t)((jube_mir_state_token_next++ << 1) | 1u);
    entry->owner_slot = slot;
    entry->state = state;
    if (!arraylist_append(jube_mir_state_tokens, entry)) {
        mem_free(entry);
        return NULL;
    }
    return entry->token;
}

static void jube_host_mir_cursor_dispose(MirEmitter* emitter) {
    if (!emitter) return;
    if (emitter->import_cache) hashmap_free(emitter->import_cache);
    em_frame_dispose(emitter);
    mem_free(emitter);
}

static void jube_host_mir_cursor_invalidate_context(void* mir_context) {
    if (!mir_context || !jube_mir_cursor_slots) return;
    for (int i = 0; i < jube_mir_cursor_slots->length; i++) {
        JubeMirCursorSlot* slot = (JubeMirCursorSlot*)jube_mir_cursor_slots->data[i];
        if (!slot || !slot->emitter || slot->emitter->ctx != mir_context) continue;
        // Context destruction invalidates every dependent cursor first, so a
        // later guest call cannot follow its emitter into a finished MIR context.
        jube_host_mir_cursor_dispose(slot->emitter);
        slot->emitter = NULL;
        jube_host_mir_state_tokens_discard_slot(slot);
        jube_host_mir_cursor_function_handles_cleanup(slot);
        jube_host_mir_cursor_label_handles_cleanup(slot);
        jube_host_mir_cursor_prototype_handles_cleanup(slot);
    }
}

static void jube_host_mir_cursor_slots_cleanup(void) {
    if (jube_mir_cursor_slots) {
        for (int i = 0; i < jube_mir_cursor_slots->length; i++) {
            JubeMirCursorSlot* slot = (JubeMirCursorSlot*)jube_mir_cursor_slots->data[i];
            if (slot) {
                jube_host_mir_cursor_dispose(slot->emitter);
                jube_host_mir_state_tokens_discard_slot(slot);
                jube_host_mir_cursor_function_handles_cleanup(slot);
                jube_host_mir_cursor_label_handles_cleanup(slot);
                jube_host_mir_cursor_prototype_handles_cleanup(slot);
            }
            mem_free(slot);
        }
        arraylist_free(jube_mir_cursor_slots);
        jube_mir_cursor_slots = NULL;
    }
    jube_host_mir_state_tokens_cleanup();
    jube_host_mir_module_slots_cleanup();
    jube_host_mir_context_slots_cleanup();
}
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
extern "C" Item js_get_fs_namespace(void);
extern "C" Item js_get_fs_promises_namespace(void);
extern "C" Item js_get_internal_fs_promises_namespace(void);
extern "C" Item js_get_internal_fs_utils_namespace(void);
extern "C" Item js_get_child_process_namespace(void);
extern "C" Item js_get_net_namespace(void);
extern "C" Item js_get_internal_js_stream_socket_constructor(void);
extern "C" Item js_get_tls_namespace(void);
extern "C" Item js_get_http_namespace(void);
extern "C" Item js_get_https_namespace(void);
extern "C" Item js_get_internal_errors_namespace(void);
extern "C" Item js_get_internal_assert_myers_diff_namespace(void);
extern "C" Item js_get_internal_async_hooks_namespace(void);
extern "C" Item js_get_internal_async_context_frame_namespace(void);
extern "C" Item js_get_internal_stream_add_abort_signal_namespace(void);
extern "C" Item js_get_internal_stream_end_of_stream_namespace(void);
extern "C" Item js_get_internal_stream_state_namespace(void);
extern "C" Item js_get_internal_crypto_util_namespace(void);
extern "C" Item js_get_internal_util_namespace(void);
extern "C" Item js_get_internal_util_inspect_namespace(void);
extern "C" Item js_get_internal_repl_namespace(void);
extern "C" Item js_get_internal_test_binding_namespace(void);
extern "C" Item js_get_node_module_namespace(void);
extern "C" Item js_get_vm_namespace(void);
extern "C" Item js_get_async_hooks_namespace(void);
extern "C" Item js_get_domain_namespace(void);
extern "C" Item js_get_cluster_namespace(void);
extern "C" Item js_get_readline_namespace(void);
extern "C" Item js_get_readline_promises_namespace(void);
extern "C" Item js_get_node_test_namespace(void);
extern "C" Item js_get_buffer_namespace(void);
extern "C" Item js_get_util_namespace(void);
extern "C" Item js_get_assert_namespace(void);
extern "C" Item js_get_stream_namespace(void);
extern "C" Item js_get_stream_promises_namespace(void);
extern "C" Item js_get_stream_web_namespace(void);
extern "C" Item js_get_stream_iter_namespace(void);
extern "C" Item js_get_repl_namespace(void);
extern "C" Item js_get_diagnostics_channel_namespace(void);
extern "C" bool js_net_default_auto_select_family_get(void);
extern "C" void js_net_default_auto_select_family_set(bool enabled);
extern "C" int js_net_default_auto_select_family_timeout_get(void);
extern "C" bool js_net_default_auto_select_family_timeout_set(int timeout_ms);
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
static bool jube_host_node_network_permission_has_net(void);
static Item jube_host_node_network_permission_make_error(void* session, const char* syscall,
                                                          const char* resource);
extern "C" Item js_node_throw_system_error(const char* syscall, int error_number);
extern "C" Item js_throw_error_with_code(const char* code, const char* message);
static int jube_host_node_work_submit(void* session, JubeAsyncWorkCallback work,
                                      JubeAsyncCompletionCallback complete,
                                      JubeAsyncDestroyCallback destroy, void* user,
                                      uint32_t* out_request_id);
static int jube_host_node_work_cancel(void* session, uint32_t request_id);
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
static Item jube_host_node_transform_new(Item options);
static Item jube_host_node_transform_prototype(void);
static Item jube_host_node_readable_push(Item stream, Item chunk);
static void jube_host_node_flush_data_if_flowing(Item stream);
static void jube_host_node_transform_flush_drained(Item stream);
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
extern "C" Item js_transform_new(Item opts);
extern "C" Item js_get_stream_transform_prototype(void);
extern "C" Item js_readable_push(Item self, Item chunk);
extern "C" void js_stream_flush_data_if_flowing(Item self);
extern "C" void js_stream_transform_flush_drained(Item self);
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
extern "C" Item dom_live_document_forms_bridge(void* doc);
extern "C" Item dom_live_form_elements_bridge(void* elem);
extern "C" Item dom_live_document_get_elements_by_tag_name_bridge(void* doc, Item query);
extern "C" Item dom_live_document_get_elements_by_class_name_bridge(void* doc, Item query);
extern "C" Item dom_live_document_get_elements_by_name_bridge(void* doc, Item query);
extern "C" Item dom_live_element_get_elements_by_tag_name_bridge(void* elem, Item query);
extern "C" Item dom_live_element_get_elements_by_class_name_bridge(void* elem, Item query);
extern "C" Item dom_clone_node_bridge(void* elem, Item deep, bool has_deep);
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
extern "C" Item js_fs_createReadStream(Item path, Item options);
extern "C" Item js_fs_createWriteStream(Item path, Item options);
extern "C" Item js_util_promisify_custom_symbol(void);
extern "C" Item js_util_custom_promisify_args_symbol(void);

static void jube_host_dom_notify_mutation(int kind, void* target, void* parent) {
    dom_notify_mutation((DomJsMutationKind)kind, target, parent);
}

static void jube_host_dom_notify_mutation_detail(int kind, void* target, void* parent,
                                                  const char* attribute_name,
                                                  const char* old_value) {
    dom_notify_mutation_detail((DomJsMutationKind)kind, target, parent,
                                  attribute_name, old_value);
}

static void jube_host_node_function_install_promisify_custom(Item function,
        JubeNativeFunctionSpec spec) {
    Item custom = jube_host_script_new_function(spec);
    js_set_key_default(function, js_util_promisify_custom_symbol(), custom);
}

static void jube_host_node_function_install_promisify_args(Item function, const char* first,
                                                            const char* second) {
    if (!first) return;
    JubeRootFrame frame = {};
    if (!jube_host_opaque_root_frame_begin(&frame, 3)) return;
    uint64_t* function_root = jube_host_opaque_root_frame_take_slot(&frame);
    uint64_t* names_root = jube_host_opaque_root_frame_take_slot(&frame);
    uint64_t* symbol_root = jube_host_opaque_root_frame_take_slot(&frame);
    if (!function_root || !names_root || !symbol_root) {
        jube_host_opaque_root_frame_end(&frame);
        return;
    }
    *function_root = function.item;
    Item names = js_array_new(0);
    *names_root = names.item;
    js_array_push((Item){.item = *names_root}, js_make_string_len(first, (int)strlen(first)));
    if (second) js_array_push((Item){.item = *names_root}, js_make_string_len(second, (int)strlen(second)));
    Item symbol = js_util_custom_promisify_args_symbol();
    *symbol_root = symbol.item;
    js_set_key_default((Item){.item = *function_root}, (Item){.item = *symbol_root},
                    (Item){.item = *names_root});
    jube_host_opaque_root_frame_end(&frame);
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

static const JubeHostStreamAPI jube_host_node_stream_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostStreamAPI),
    js_fs_createReadStream,
    js_fs_createWriteStream,
    js_node_stream_tcp_create,
    js_node_stream_tcp_bind,
    js_node_stream_tcp_address,
    js_node_stream_tcp_fd,
    js_node_stream_tcp_adopt_fd,
    js_node_stream_resource_close,
    js_node_stream_resource_ref,
    js_node_stream_resource_is_live,
    jube_host_node_transform_new,
    jube_host_node_transform_prototype,
    jube_host_node_readable_push,
    jube_host_node_flush_data_if_flowing,
    jube_host_node_transform_flush_drained,
};

static const JubeHostNetworkAPI jube_host_node_network_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNetworkAPI),
    js_net_default_auto_select_family_get,
    js_net_default_auto_select_family_set,
    js_net_default_auto_select_family_timeout_get,
    js_net_default_auto_select_family_timeout_set,
    jube_host_node_network_permission_has_net,
    jube_host_node_network_permission_make_error,
    js_node_network_ip_family,
    js_node_network_lookup_sync,
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
    &jube_host_node_network_api,
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
    js_call_function,
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
};

// The catalog section: one body per dom_api.def row, in row order. The cast is
// the row's own arity, so a body whose C signature disagrees with its row is a
// compile error here as well as in dom_api_check.cpp.
static const JubeHostDomCatalogAPI jube_host_dom_catalog = {
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv) \
    (JubeDomFn##argc)(body),
#include "../dom/dom_api.def"
#undef DOM_OP
};

extern "C" Item js_formdata_collect_form_entries(void* form_elem, void* submitter_elem);
extern "C" bool dom_focus_first_invalid_form_control(void* form_elem);
extern "C" bool dom_navigate_submit_target(const char* target_name, const char* url);
extern "C" void* dom_popover_target_for_button(void* button);
extern "C" int dom_popover_target_action(void* button);
extern "C" bool dom_activate_popover(void* popover, int action);

static const JubeHostDomAPI jube_host_dom_api = {
    dom_get_document,
    dom_get_or_create_doc_node,
    dom_unwrap_element_impl,
    dom_is_inline_style_item,
    dom_is_computed_style_item,
    dom_is_stylesheet,
    dom_is_css_rule,
    dom_is_rule_style_decl,
    dom_get_property_impl,
    dom_set_property_impl,
    dom_element_operation_impl,
    dom_computed_style_get_property,
    NULL,
    dom_cssom_rule_decl_get_property,
    dom_cssom_rule_decl_set_property,
    dom_get_foreign_doc,
    dom_swap_active_document,
    dom_restore_active_document,
    dom_owner_document_for_node,
    dom_to_attribute_cstr,
    dom_after_set_attribute,
    dom_after_remove_attribute,
    dom_after_toggle_attribute_remove,
    dom_after_disabled_attribute_set,
    dom_after_default_checked_set,
    dom_after_default_selected_set,
    dom_after_select_multiple_removed,
    dom_set_checked_dirty,
    dom_select_set_value_bridge,
    dom_select_set_selected_index_bridge,
    dom_select_set_length_bridge,
    dom_set_option_selected_dirty,
    dom_set_option_text_bridge,
    dom_after_srcdoc_set,
    dom_set_text_data_property,
    dom_text_control_set_value_bridge,
    dom_text_control_set_selection_start_bridge,
    dom_text_control_set_selection_end_bridge,
    dom_text_control_set_selection_direction_bridge,
    dom_text_control_set_default_value_bridge,
    dom_text_control_set_selection_range_bridge,
    dom_text_control_set_range_text_bridge,
    dom_text_control_select_bridge,
    dom_form_reset_bridge,
    dom_check_validity_bridge,
    dom_report_validity_bridge,
    dom_form_submit_bridge,
    dom_form_request_submit_bridge,
    dom_focus_method_bridge,
    dom_click_method_bridge,
    dom_add_event_listener_bridge,
    dom_remove_event_listener_bridge,
    dom_dispatch_event_bridge,
    dom_get_bounding_client_rect_bridge,
    dom_get_client_rects_bridge,
    dom_scroll_into_view_bridge,
    dom_scroll_operation_bridge,
    dom_text_control_caret_bounds_bridge,
    dom_text_control_boundary_from_point_bridge,
    dom_boundary_from_point_bridge,
    dom_style_set_property_bridge,
    dom_style_remove_property_bridge,
    dom_text_replace_data_bridge,
    dom_text_insert_data_bridge,
    dom_text_append_data_bridge,
    dom_text_delete_data_bridge,
    dom_text_substring_data_bridge,
    dom_append_child_bridge,
    dom_remove_child_bridge,
    dom_insert_before_bridge,
    dom_remove_bridge,
    dom_adopt_node_bridge,
    dom_location_navigate_bridge,
    dom_document_open_bridge,
    dom_document_write_bridge,
    dom_document_element_from_point_bridge,
    dom_create_range,
    dom_get_selection,
    dom_doc_has_browsing_context,
    dom_document_fonts_bridge,
    dom_document_stylesheets_bridge,
    dom_document_implementation_bridge,
    dom_document_design_mode_bridge,
    dom_document_active_element_bridge,
    dom_normalize_bridge,
    dom_clone_node_bridge,
    dom_replace_child_bridge,
    dom_replace_with_bridge,
    dom_insert_adjacent_element_bridge,
    dom_insert_adjacent_html_bridge,
    dom_append_variadic_bridge,
    dom_prepend_variadic_bridge,
    jube_host_dom_notify_mutation,
    jube_host_dom_notify_mutation_detail,
    js_range_get_start_container,
    js_range_get_start_offset,
    js_range_get_end_container,
    js_range_get_end_offset,
    js_range_get_collapsed,
    js_range_get_common_ancestor,
    js_range_set_start,
    js_range_set_end,
    js_range_set_start_before,
    js_range_set_start_after,
    js_range_set_end_before,
    js_range_set_end_after,
    js_range_collapse,
    js_range_select_node,
    js_range_select_node_contents,
    js_range_clone_range,
    js_range_compare_boundary_points,
    js_range_compare_point,
    js_range_is_point_in_range,
    js_range_intersects_node,
    js_range_detach,
    js_range_to_string,
    js_range_get_client_rects,
    js_range_get_bounding_client_rect,
    js_range_delete_contents,
    js_range_extract_contents,
    js_range_clone_contents,
    js_range_insert_node,
    js_range_surround_contents,
    js_selection_get_anchor_node,
    js_selection_get_anchor_offset,
    js_selection_get_focus_node,
    js_selection_get_focus_offset,
    js_selection_get_is_collapsed,
    js_selection_get_range_count,
    js_selection_get_type,
    js_selection_get_direction,
    js_selection_get_range_at,
    js_selection_add_range,
    js_selection_remove_range,
    js_selection_remove_all_ranges,
    js_selection_empty,
    js_selection_collapse,
    js_selection_set_position,
    js_selection_collapse_to_start,
    js_selection_collapse_to_end,
    js_selection_extend,
    js_selection_set_base_and_extent,
    js_selection_select_all_children,
    js_selection_contains_node,
    js_selection_delete_from_document,
    js_selection_to_string,
    js_selection_modify,
    js_selection_force_direction,
    dom_get_style_property,
    dom_set_style_property,
    dom_style_css_has,
    dom_cssom_stylesheet_get_css_rules,
    dom_cssom_stylesheet_get_length,
    dom_cssom_stylesheet_get_disabled,
    dom_cssom_stylesheet_get_type,
    dom_cssom_stylesheet_get_href,
    dom_cssom_stylesheet_get_title,
    dom_cssom_stylesheet_index,
    dom_cssom_insert_rule,
    dom_cssom_delete_rule,
    dom_cssom_rule_get_selector_text,
    dom_cssom_rule_set_selector_text,
    dom_cssom_rule_get_style,
    dom_cssom_rule_get_css_rules,
    dom_cssom_rule_get_css_text,
    dom_cssom_rule_get_type,
    dom_cssom_rule_get_parent_rule,
    dom_cssom_rule_decl_remove_property,
    dom_cssom_decl_css_has,
    dom_get_ui_context,
    dom_has_committed_geometry_snapshot,
    dom_create_tree_walker_bridge,
    dom_document_exec_command_bridge,
    js_formdata_collect_form_entries,
    dom_focus_first_invalid_form_control,
    dom_navigate_submit_target,
    dom_popover_target_for_button,
    dom_popover_target_action,
    dom_activate_popover,
};

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

// H7A source records are intentionally plain C data.  A language can retain
// source text while parsing, but neither its parser nor diagnostics acquire an
// Input/Pool dependency from the host implementation.
static int jube_host_source_read(const char* path, JubeHostedSource* out_source) {
    if (!path || !*path || !out_source ||
        out_source->struct_size < JUBE_HOSTED_SOURCE_V1_SIZE) {
        return -1;
    }
    memset(out_source, 0, sizeof(*out_source));
    out_source->struct_size = JUBE_HOSTED_SOURCE_V1_SIZE;
    char* bytes = read_text_file(path);
    if (!bytes) return -1;
    char* canonical_path = file_realpath(path);
    out_source->bytes = bytes;
    out_source->byte_length = strlen(bytes);
    out_source->canonical_path = canonical_path ? canonical_path : path;
    // canonical_path is independently owned when available; source_release
    // never frees the caller-owned fallback path pointer.
    out_source->host_owner = canonical_path;
    return 0;
}

static void jube_host_source_release(JubeHostedSource* source) {
    if (!source || source->struct_size < JUBE_HOSTED_SOURCE_V1_SIZE) return;
    mem_free((void*)source->bytes);
    mem_free(source->host_owner);
    memset(source, 0, sizeof(*source));
}

static int jube_host_source_line_column(const JubeHostedSource* source, size_t byte_offset,
                                        size_t* out_line, size_t* out_column) {
    if (!source || source->struct_size < JUBE_HOSTED_SOURCE_V1_SIZE ||
        !source->bytes || !out_line || !out_column || byte_offset > source->byte_length) {
        return -1;
    }
    size_t line = 1;
    size_t column = 1;
    for (size_t i = 0; i < byte_offset; i++) {
        unsigned char ch = (unsigned char)source->bytes[i];
        if (ch == '\n') {
            line++;
            column = 1;
        } else if ((ch & 0xc0u) != 0x80u) {
            // Diagnostics present Unicode scalar columns while parser APIs
            // remain byte-based, avoiding a second source-index convention.
            column++;
        }
    }
    *out_line = line;
    *out_column = column;
    return 0;
}

static void jube_host_write_diagnostic(const JubeLanguageRunRequest* request,
                                       const JubeHostedDiagnostic* diagnostic) {
    if (!request || request->struct_size < JUBE_LANGUAGE_RUN_REQUEST_V1_SIZE ||
        !request->write_stderr || !diagnostic ||
        diagnostic->struct_size < JUBE_HOSTED_DIAGNOSTIC_V1_SIZE ||
        !diagnostic->message) {
        return;
    }
    const char* severity = diagnostic->severity == JUBE_HOSTED_DIAGNOSTIC_WARNING
        ? "warning" : diagnostic->severity == JUBE_HOSTED_DIAGNOSTIC_NOTE ? "note" : "error";
    StrBuf* text = strbuf_new_cap(256);
    if (!text) return;
    if (diagnostic->source && diagnostic->source->canonical_path) {
        size_t line = 0;
        size_t column = 0;
        if (jube_host_source_line_column(diagnostic->source, diagnostic->byte_offset,
                                         &line, &column) == 0) {
            strbuf_append_format(text, "%s:%zu:%zu: %s: %s\n",
                diagnostic->source->canonical_path, line, column, severity,
                diagnostic->message);
        }
    }
    if (text->length == 0) {
        strbuf_append_format(text, "%s: %s\n", severity, diagnostic->message);
    }
    request->write_stderr(request->output_user, text->str, text->length);
    strbuf_free(text);
}

static void jube_host_write_result(const JubeLanguageRunRequest* request, Item result) {
    if (!request || request->struct_size < JUBE_LANGUAGE_RUN_REQUEST_V1_SIZE ||
        !request->write_stdout || result.item == ITEM_NULL || result.item == 0) {
        return;
    }
    TypeId result_type = get_type_id(result);
    if (result_type == LMD_TYPE_MAP || result_type == LMD_TYPE_ARRAY ||
        result_type == LMD_TYPE_ELEMENT) {
        Pool* pool = mem_pool_create(NULL, MEM_ROLE_TEMP, "jube.hosted.result");
        if (!pool) return;
        String* json = format_json(pool, result);
        if (json) {
            request->write_stdout(request->output_user, json->chars, (size_t)json->len);
            request->write_stdout(request->output_user, "\n", 1);
        }
        mem_pool_destroy(pool);
        return;
    }
    StrBuf* output = strbuf_new_cap(256);
    if (!output) return;
    print_root_item(output, result);
    request->write_stdout(request->output_user, output->str, output->length);
    request->write_stdout(request->output_user, "\n", 1);
    strbuf_free(output);
}

static void* jube_host_session_alloc(size_t size) {
    return size ? mem_calloc(1, size, MEM_CAT_SYSTEM) : NULL;
}

static void jube_host_session_free(void* memory) {
    mem_free(memory);
}

struct JubeGuestExecution {
    uint32_t magic;
    Runtime runtime;
    Runtime* runtime_owner;
    EvalContext* active_context;
    Context* frame_runtime;
    bool reusing_context;
    bool activation_active;
    bool faulted;
    bool import_execution;
    bool import_retained;
    Input* input;
    uint64_t result_scalar_home;
    JubeGuestExecution* previous_active_execution;
};

// This TLS state belongs exclusively to hosted guest activation. Lambda and
// JavaScript evaluation never read it, preserving their existing hot paths.
static thread_local JubeGuestExecution* jube_active_guest_execution = NULL;

static JubeGuestExecution* jube_guest_execution_from_handle(void* execution_context) {
    JubeGuestExecution* execution = (JubeGuestExecution*)execution_context;
    return execution && execution->magic == JUBE_GUEST_EXECUTION_MAGIC ? execution : NULL;
}

static Runtime* jube_guest_execution_runtime(JubeGuestExecution* execution) {
    return execution ? execution->runtime_owner : NULL;
}

static void jube_guest_execution_poison(JubeGuestExecution* execution) {
    if (!execution) return;
    // A non-local landing skips guest epilogues, so no later guest helper may
    // observe a runtime slot or scalar home from the abandoned activation.
    execution->faulted = true;
    execution->frame_runtime = NULL;
    execution->result_scalar_home = 0;
}

static void jube_host_execution_finish_guest(void* execution_context);

static void* jube_host_execution_create(void) {
    JubeGuestExecution* execution = (JubeGuestExecution*)mem_calloc(
        1, sizeof(JubeGuestExecution), MEM_CAT_SYSTEM);
    if (!execution) return NULL;
    execution->magic = JUBE_GUEST_EXECUTION_MAGIC;
    execution->runtime_owner = &execution->runtime;
    lambda_stack_init();
    runtime_init(&execution->runtime);
    return execution;
}

static void jube_host_execution_destroy(void* execution_context) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    if (!execution) return;
    if (execution->activation_active) {
        // Destruction must follow the same path as normal completion: bypassing
        // it left the active Jube execution pointing at freed import state.
        jube_host_execution_finish_guest(execution_context);
    }
    if (!execution->import_execution) runtime_cleanup(&execution->runtime);
    execution->magic = 0;
    mem_free(execution);
}

void* jube_create_import_execution(void* host_context) {
    Runtime* runtime = (Runtime*)jube_execution_runtime_handle(host_context);
    if (!runtime) return NULL;
    JubeGuestExecution* execution = (JubeGuestExecution*)mem_calloc(
        1, sizeof(JubeGuestExecution), MEM_CAT_SYSTEM);
    if (!execution) return NULL;
    execution->magic = JUBE_GUEST_EXECUTION_MAGIC;
    execution->runtime_owner = runtime;
    execution->import_execution = true;
    lambda_stack_init();
    return execution;
}

void jube_destroy_import_execution(void* execution_context) {
    jube_host_execution_destroy(execution_context);
}

bool jube_import_execution_is_retained(void* execution_context) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    return execution && execution->import_execution && execution->import_retained;
}

void* jube_execution_runtime_handle(void* execution_context) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    return execution ? (void*)jube_guest_execution_runtime(execution) : execution_context;
}

static int jube_host_execution_link_module(void* execution_context, void* mir_context) {
    if (!execution_context || !mir_context) return -1;
    // Import resolution remains host-owned so guest modules cannot depend on
    // the private resolver or raw host symbol names during MIR linking.
    MIR_link((MIR_context_t)mir_context, MIR_set_gen_interface, import_resolver);
    return 0;
}

static void* jube_host_mir_context_create(int optimization_level) {
    return jit_init(optimization_level);
}

static void jube_host_mir_context_destroy(void* mir_context) {
    if (!mir_context) return;
    // A hosted compiler may retain generated function pointers, so its own
    // lifetime policy decides when this host-owned context is released.
    jube_host_mir_cursor_invalidate_context(mir_context);
    jube_host_mir_modules_invalidate_context((MIR_context_t)mir_context);
    MIR_finish((MIR_context_t)mir_context);
}

static void* jube_host_mir_module_create(void* mir_context, const char* module_name) {
    if (!mir_context || !module_name || !*module_name) return NULL;
    MIR_module_t module = MIR_new_module((MIR_context_t)mir_context, module_name);
    if (!module) return NULL;
    // Module handles are host-table identities so a guest cannot pass a raw
    // MIR module from another cursor or reuse one after finalization.
    return jube_host_mir_module_register((MIR_context_t)mir_context, module);
}

static int jube_host_mir_module_finalize_and_load(void* mir_context, void* mir_module) {
    if (!mir_context || !mir_module) return -1;
    MIR_context_t context = (MIR_context_t)mir_context;
    MIR_module_t module = jube_host_mir_module_from_handle(context, mir_module);
    if (!module) return -1;
    MIR_finish_module(context);
    MIR_load_module(context, module);
    jube_host_mir_module_unregister(mir_module);
    return 0;
}

static void* jube_host_mir_function_lookup(void* mir_context, const char* function_name) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !function_name || !*function_name) return NULL;
    return find_func(context, function_name);
}

static void jube_host_mir_function_finish(void* mir_context) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context) return;
    // The host owns MIR function lifecycle so a guest does not bind to MIR's
    // private context representation or finalization symbol.
    MIR_finish_func(context);
}

static int jube_host_mir_item_function_create(void* mir_context, const char* function_name,
        uint32_t parameter_count, const char* const* parameter_names,
        void** out_function_item, void** out_function) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !function_name || !*function_name || !out_function_item ||
        !out_function || (parameter_count > 0 && !parameter_names) ||
        parameter_count > 1024) return -1;
    MIR_var_t* parameters = NULL;
    if (parameter_count > 0) {
        parameters = (MIR_var_t*)mem_calloc(parameter_count, sizeof(MIR_var_t), MEM_CAT_SYSTEM);
        if (!parameters) return -1;
        for (uint32_t i = 0; i < parameter_count; i++) {
            if (!parameter_names[i] || !*parameter_names[i]) {
                mem_free(parameters);
                return -1;
            }
            parameters[i] = {MIR_T_I64, parameter_names[i], 0};
        }
    }
    MIR_type_t result_type = MIR_T_I64;
    MIR_item_t function_item = MIR_new_func_arr(context, function_name,
        1, &result_type, parameter_count, parameters);
    if (parameters) mem_free(parameters);
    if (!function_item) return -1;
    // Keep the host as the only owner of MIR item/function construction; the
    // guest may use these opaque handles only while this context remains live.
    *out_function_item = function_item;
    *out_function = function_item->u.func;
    return 0;
}

static int jube_host_mir_item_function_create_typed(void* mir_context,
        const char* function_name, uint32_t parameter_count,
        const char* const* parameter_names, const uint8_t* parameter_kinds,
        void** out_function_item, void** out_function) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !function_name || !*function_name || !out_function_item ||
            !out_function || (parameter_count > 0 && (!parameter_names || !parameter_kinds)) ||
            parameter_count > 1024) return -1;
    MIR_var_t* parameters = NULL;
    if (parameter_count > 0) {
        parameters = (MIR_var_t*)mem_calloc(parameter_count, sizeof(MIR_var_t), MEM_CAT_SYSTEM);
        if (!parameters) return -1;
        for (uint32_t i = 0; i < parameter_count; i++) {
            if (!parameter_names[i] || !*parameter_names[i] || parameter_kinds[i] > 1) {
                mem_free(parameters);
                return -1;
            }
            parameters[i] = {parameter_kinds[i] ? MIR_T_P : MIR_T_I64,
                parameter_names[i], 0};
        }
    }
    MIR_type_t return_type = MIR_T_I64;
    MIR_item_t item = MIR_new_func_arr(context, function_name,
        1, &return_type, parameter_count, parameters);
    if (parameters) mem_free(parameters);
    if (!item) return -1;
    *out_function_item = item;
    *out_function = MIR_get_item_func(context, item);
    return *out_function ? 0 : -1;
}

static int jube_host_mir_item_function_proto_create_typed(void* mir_context,
        const char* prototype_name, uint32_t parameter_count,
        const uint8_t* parameter_kinds, void** out_prototype_item) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !prototype_name || !*prototype_name || !out_prototype_item ||
            (parameter_count > 0 && !parameter_kinds) || parameter_count > 1024) return -1;
    MIR_var_t* parameters = NULL;
    char (*parameter_names)[16] = NULL;
    if (parameter_count > 0) {
        parameters = (MIR_var_t*)mem_calloc(parameter_count, sizeof(MIR_var_t), MEM_CAT_SYSTEM);
        parameter_names = (char (*)[16])mem_calloc(parameter_count,
            sizeof(*parameter_names), MEM_CAT_SYSTEM);
        if (!parameters || !parameter_names) {
            if (parameters) mem_free(parameters);
            if (parameter_names) mem_free(parameter_names);
            return -1;
        }
        for (uint32_t i = 0; i < parameter_count; i++) {
            if (parameter_kinds[i] > 1) {
                mem_free(parameters);
                mem_free(parameter_names);
                return -1;
            }
            snprintf(parameter_names[i], sizeof(parameter_names[i]), "p%u", i);
            parameters[i] = {parameter_kinds[i] ? MIR_T_P : MIR_T_I64,
                parameter_names[i], 0};
        }
    }
    MIR_type_t result_type = MIR_T_I64;
    MIR_item_t prototype = MIR_new_proto_arr(context,
        prototype_name, 1, &result_type, parameter_count, parameters);
    if (parameters) mem_free(parameters);
    if (parameter_names) mem_free(parameter_names);
    if (!prototype) return -1;
    *out_prototype_item = prototype;
    return 0;
}

static int jube_host_mir_function_frame_runtime_load(void* mir_context,
        void* function_item, void* function, void* frame_runtime_slot,
        uint32_t* out_runtime_register) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    JubeGuestExecution* active = jube_active_guest_execution;
    JubeGuestExecution* owner = active && active->reusing_context &&
        active->previous_active_execution
        ? active->previous_active_execution : active;
    if (!context || !function_item || !function || !frame_runtime_slot ||
            !out_runtime_register || !active || !active->activation_active ||
            !owner || !owner->activation_active ||
            frame_runtime_slot != (void*)&owner->frame_runtime) {
        return -1;
    }
    MIR_item_t item = (MIR_item_t)function_item;
    MIR_func_t target = MIR_get_item_func(context, item);
    if (!target || target != (MIR_func_t)function) return -1;
    // The address belongs to the active host execution; emitting this load in
    // the host prevents a guest compiler from turning another host address
    // into generated-code memory access.
    MIR_reg_t slot_address = MIR_new_func_reg(context, target, MIR_T_I64,
        "jube_frame_runtime_slot");
    MIR_reg_t runtime = MIR_new_func_reg(context, target, MIR_T_I64,
        "jube_frame_runtime");
    if (!slot_address || !runtime) return -1;
    MIR_append_insn(context, item, MIR_new_insn(context, MIR_MOV,
        MIR_new_reg_op(context, slot_address),
        MIR_new_int_op(context, (int64_t)(uintptr_t)frame_runtime_slot)));
    MIR_append_insn(context, item, MIR_new_insn(context, MIR_MOV,
        MIR_new_reg_op(context, runtime),
        MIR_new_mem_op(context, MIR_T_I64, 0, slot_address, 0, 1)));
    *out_runtime_register = (uint32_t)runtime;
    return 0;
}

static int jube_host_mir_function_register_create(void* mir_context, void* function,
        int* inout_register_counter, const char* prefix, uint8_t value_kind,
        uint32_t* out_register) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !function || !inout_register_counter || !prefix || !*prefix ||
            !out_register || *inout_register_counter < 0) return -1;
    MIR_type_t type = MIR_T_I64;
    if (value_kind == JUBE_COMPILER_VALUE_F64) type = MIR_T_D;
    else if (value_kind != JUBE_COMPILER_VALUE_I64 &&
            value_kind != JUBE_COMPILER_VALUE_POINTER) return -1;
    char name[128];
    int written = snprintf(name, sizeof(name), "%s_%d", prefix, *inout_register_counter);
    if (written <= 0 || (size_t)written >= sizeof(name)) return -1;
    // Preserve the shared emitter's monotonic naming sequence while keeping
    // MIR register allocation and pointer-class coercion host-owned.
    (*inout_register_counter)++;
    MIR_reg_t reg = MIR_new_func_reg(context, (MIR_func_t)function,
        type, name);
    if (!reg) return -1;
    *out_register = (uint32_t)reg;
    return 0;
}

static int jube_host_mir_label_create(void* mir_context, void** out_label) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !out_label) return -1;
    MIR_label_t label = MIR_new_label(context);
    if (!label) return -1;
    *out_label = label;
    return 0;
}

static int jube_host_mir_instruction_emit(void* mir_context, void* function_item,
        const JubeCompilerInstruction* instruction) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !function_item || !instruction) {
        return -1;
    }
    MIR_item_t item = (MIR_item_t)function_item;
    MIR_insn_t emitted = NULL;
    switch (instruction->opcode) {
    case JUBE_COMPILER_INSN_MOVE_I64_IMMEDIATE:
        if (!instruction->destination_register) return -1;
        emitted = MIR_new_insn(context, MIR_MOV,
            MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
            MIR_new_int_op(context, instruction->immediate_i64));
        break;
    case JUBE_COMPILER_INSN_MOVE_F64_IMMEDIATE:
        if (!instruction->destination_register) return -1;
        emitted = MIR_new_insn(context, MIR_DMOV,
            MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
            MIR_new_double_op(context, instruction->immediate_f64));
        break;
    case JUBE_COMPILER_INSN_MOVE_I64_REGISTER:
        if (!instruction->destination_register || !instruction->source_register) return -1;
        emitted = MIR_new_insn(context, MIR_MOV,
            MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
            MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register));
        break;
    case JUBE_COMPILER_INSN_MOVE_I64_REFERENCE:
        if (!instruction->destination_register || !instruction->source_reference) return -1;
        emitted = MIR_new_insn(context, MIR_MOV,
            MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
            MIR_new_ref_op(context, (MIR_item_t)instruction->source_reference));
        break;
    case JUBE_COMPILER_INSN_MOVE_F64_REGISTER:
        if (!instruction->destination_register || !instruction->source_register) return -1;
        emitted = MIR_new_insn(context, MIR_DMOV,
            MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
            MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register));
        break;
    case JUBE_COMPILER_INSN_JUMP:
        // Branch descriptors intentionally have no destination register; a
        // move-only precheck would reject valid control-flow emission.
        if (!instruction->target_label) return -1;
        emitted = MIR_new_insn(context, MIR_JMP,
            MIR_new_label_op(context, (MIR_label_t)instruction->target_label));
        break;
    case JUBE_COMPILER_INSN_BRANCH_TRUE:
        if (!instruction->source_register || !instruction->target_label) return -1;
        emitted = MIR_new_insn(context, MIR_BT,
            MIR_new_label_op(context, (MIR_label_t)instruction->target_label),
            MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register));
        break;
    case JUBE_COMPILER_INSN_BRANCH_FALSE:
        if (!instruction->source_register || !instruction->target_label) return -1;
        emitted = MIR_new_insn(context, MIR_BF,
            MIR_new_label_op(context, (MIR_label_t)instruction->target_label),
            MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register));
        break;
    case JUBE_COMPILER_INSN_BRANCH_NOT_EQUAL_I64_IMMEDIATE:
        if (!instruction->source_register || !instruction->target_label) return -1;
        emitted = MIR_new_insn(context, MIR_BNE,
            MIR_new_label_op(context, (MIR_label_t)instruction->target_label),
            MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register),
            MIR_new_int_op(context, instruction->immediate_i64));
        break;
    case JUBE_COMPILER_INSN_BRANCH_GREATER_EQUAL_I64_IMMEDIATE:
        if (!instruction->source_register || !instruction->target_label) return -1;
        emitted = MIR_new_insn(context, MIR_BGE,
            MIR_new_label_op(context, (MIR_label_t)instruction->target_label),
            MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register),
            MIR_new_int_op(context, instruction->immediate_i64));
        break;
    case JUBE_COMPILER_INSN_I64_OPERATION: {
        if (!instruction->destination_register || !instruction->source_register) return -1;
        MIR_insn_code_t opcode = MIR_MOV;
        switch (instruction->operation) {
        case JUBE_COMPILER_I64_LSH: opcode = MIR_LSH; break;
        case JUBE_COMPILER_I64_RSH: opcode = MIR_RSH; break;
        case JUBE_COMPILER_I64_AND: opcode = MIR_AND; break;
        case JUBE_COMPILER_I64_OR: opcode = MIR_OR; break;
        case JUBE_COMPILER_I64_LE: opcode = MIR_LE; break;
        case JUBE_COMPILER_I64_GE: opcode = MIR_GE; break;
        case JUBE_COMPILER_I64_ADD: opcode = MIR_ADD; break;
        case JUBE_COMPILER_I64_SUB: opcode = MIR_SUB; break;
        case JUBE_COMPILER_I64_MUL: opcode = MIR_MUL; break;
        case JUBE_COMPILER_I64_DIV: opcode = MIR_DIV; break;
        case JUBE_COMPILER_I64_MOD: opcode = MIR_MOD; break;
        case JUBE_COMPILER_I64_XOR: opcode = MIR_XOR; break;
        case JUBE_COMPILER_I64_NEG: opcode = MIR_NEG; break;
        case JUBE_COMPILER_I64_LT: opcode = MIR_LT; break;
        case JUBE_COMPILER_I64_GT: opcode = MIR_GT; break;
        case JUBE_COMPILER_I64_EQ: opcode = MIR_EQ; break;
        case JUBE_COMPILER_I64_NE: opcode = MIR_NE; break;
        default: return -1;
        }
        if (instruction->operation == JUBE_COMPILER_I64_NEG) {
            emitted = MIR_new_insn(context, opcode,
                MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
                MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register));
            break;
        }
        MIR_op_t right = instruction->right_register
            ? MIR_new_reg_op(context, (MIR_reg_t)instruction->right_register)
            : MIR_new_int_op(context, instruction->immediate_i64);
        emitted = MIR_new_insn(context, opcode,
            MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
            MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register), right);
        break;
    }
    case JUBE_COMPILER_INSN_F64_OPERATION:
        if (!instruction->destination_register || !instruction->source_register) return -1;
        if (instruction->operation == JUBE_COMPILER_F64_FROM_I64) {
            emitted = MIR_new_insn(context, MIR_I2D,
                MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
                MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register));
        } else if (instruction->right_register) {
            MIR_insn_code_t opcode = MIR_DDIV;
            switch (instruction->operation) {
            case JUBE_COMPILER_F64_ADD: opcode = MIR_DADD; break;
            case JUBE_COMPILER_F64_SUB: opcode = MIR_DSUB; break;
            case JUBE_COMPILER_F64_MUL: opcode = MIR_DMUL; break;
            case JUBE_COMPILER_F64_DIV: opcode = MIR_DDIV; break;
            case JUBE_COMPILER_F64_LT: opcode = MIR_DLT; break;
            case JUBE_COMPILER_F64_LE: opcode = MIR_DLE; break;
            case JUBE_COMPILER_F64_GT: opcode = MIR_DGT; break;
            case JUBE_COMPILER_F64_GE: opcode = MIR_DGE; break;
            case JUBE_COMPILER_F64_EQ: opcode = MIR_DEQ; break;
            case JUBE_COMPILER_F64_NE: opcode = MIR_DNE; break;
            default: return -1;
            }
            emitted = MIR_new_insn(context, opcode,
                MIR_new_reg_op(context, (MIR_reg_t)instruction->destination_register),
                MIR_new_reg_op(context, (MIR_reg_t)instruction->source_register),
                MIR_new_reg_op(context, (MIR_reg_t)instruction->right_register));
        } else {
            return -1;
        }
        break;
    default:
        return -1;
    }
    if (!emitted) return -1;
    // Validate the narrow descriptor before appending so a guest cannot turn
    // an opaque function handle into arbitrary generated MIR state.
    MIR_append_insn(context, item, emitted);
    return 0;
}

static int jube_host_mir_label_emit(void* mir_context, void* function_item, void* label) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !function_item || !label) return -1;
    // Only the host materializes a label instruction, preserving the context
    // ownership of the opaque label identity across the module boundary.
    MIR_append_insn(context, (MIR_item_t)function_item,
        (MIR_label_t)label);
    return 0;
}

static MIR_type_t jube_host_compiler_value_type(uint8_t value_kind) {
    switch (value_kind) {
    case JUBE_COMPILER_VALUE_I64: return MIR_T_I64;
    case JUBE_COMPILER_VALUE_F64: return MIR_T_D;
    case JUBE_COMPILER_VALUE_POINTER: return MIR_T_P;
    default: return MIR_T_UNDEF;
    }
}

static int jube_host_mir_runtime_import_call_emit(void* compiler_cursor,
        const char* function_name, uint8_t result_kind, uint32_t operand_count,
        const JubeCompilerCallOperand* operands, bool discard_result,
        uint32_t* out_result_register) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->func || !emitter->func_item ||
            !function_name || !*function_name || operand_count > LAMBDA_MAX_FUNCTION_ARGS ||
            (operand_count > 0 && !operands) || (!discard_result && !out_result_register)) {
        return -1;
    }
    MIR_type_t result_type = jube_host_compiler_value_type(result_kind);
    if (result_type == MIR_T_UNDEF) return -1;
    MIR_type_t argument_types[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MIR_op_t argument_ops[LAMBDA_MAX_FUNCTION_ARGS] = {};
    for (uint32_t i = 0; i < operand_count; i++) {
        const JubeCompilerCallOperand* operand = &operands[i];
        argument_types[i] = jube_host_compiler_value_type(operand->value_kind);
        if (argument_types[i] == MIR_T_UNDEF) return -1;
        switch (operand->operand_kind) {
        case JUBE_COMPILER_CALL_OPERAND_REGISTER:
            if (!operand->register_id) return -1;
            argument_ops[i] = MIR_new_reg_op(emitter->ctx, (MIR_reg_t)operand->register_id);
            break;
        case JUBE_COMPILER_CALL_OPERAND_I64_IMMEDIATE:
            if (argument_types[i] == MIR_T_D) return -1;
            argument_ops[i] = MIR_new_int_op(emitter->ctx, operand->immediate_i64);
            break;
        case JUBE_COMPILER_CALL_OPERAND_F64_IMMEDIATE:
            if (argument_types[i] != MIR_T_D) return -1;
            argument_ops[i] = MIR_new_double_op(emitter->ctx, operand->immediate_f64);
            break;
        default:
            return -1;
        }
    }
    // The shared emitter owns import caching, exact safepoint roots, exception
    // effects, and scalar-return adoption; bypassing it loses GC invariants.
    if (discard_result) {
        em_call_void_with_args(emitter, function_name, (int)operand_count,
            argument_types, argument_ops, true);
        return 0;
    }
    MIR_reg_t result = em_call_with_args(emitter, function_name, result_type,
        (int)operand_count, argument_types, argument_ops, true);
    if (!result) return -1;
    *out_result_register = (uint32_t)result;
    return 0;
}

static int jube_host_mir_local_direct_call_emit(void* compiler_cursor,
        const char* function_name, void* prototype_item, void* target_item,
        uint32_t result_register, uint32_t operand_count,
        const JubeCompilerCallOperand* operands) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->func_item || !function_name ||
            !*function_name || !prototype_item || !target_item || !result_register ||
            operand_count > 21 || (operand_count > 0 && !operands)) {
        return -1;
    }
    // Direct-call items are private MIR references; reject foreign cursor
    // provenance before building any MIR operand from them.
    if (!jube_host_mir_cursor_owns_prototype(compiler_cursor,
            (MIR_item_t)prototype_item) || !jube_host_mir_cursor_owns_function_item(
            compiler_cursor, (MIR_item_t)target_item)) return -1;
    MIR_op_t call_operands[24] = {};
    call_operands[0] = MIR_new_ref_op(emitter->ctx, (MIR_item_t)prototype_item);
    call_operands[1] = MIR_new_ref_op(emitter->ctx, (MIR_item_t)target_item);
    call_operands[2] = MIR_new_reg_op(emitter->ctx, (MIR_reg_t)result_register);
    for (uint32_t i = 0; i < operand_count; i++) {
        const JubeCompilerCallOperand* operand = &operands[i];
        switch (operand->operand_kind) {
        case JUBE_COMPILER_CALL_OPERAND_REGISTER:
            if (!operand->register_id) return -1;
            call_operands[3 + i] = MIR_new_reg_op(emitter->ctx,
                (MIR_reg_t)operand->register_id);
            break;
        case JUBE_COMPILER_CALL_OPERAND_I64_IMMEDIATE:
            call_operands[3 + i] = MIR_new_int_op(emitter->ctx, operand->immediate_i64);
            break;
        default:
            return -1;
        }
    }
    // A local target borrows its synchronous argument frame; classifying it as
    // unknown would reject valid caller-owned scalar result homes.
    em_emit_borrowed_call(emitter, function_name,
        MIR_new_insn_arr(emitter->ctx, MIR_CALL, (int)(operand_count + 3), call_operands));
    return 0;
}

static int jube_host_mir_item_return_emit(void* compiler_cursor,
        const JubeCompilerCallOperand* value) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->func_item || !value ||
            value->value_kind != JUBE_COMPILER_VALUE_I64) return -1;
    MIR_op_t operand = {};
    switch (value->operand_kind) {
    case JUBE_COMPILER_CALL_OPERAND_REGISTER:
        if (!value->register_id) return -1;
        operand = MIR_new_reg_op(emitter->ctx, (MIR_reg_t)value->register_id);
        break;
    case JUBE_COMPILER_CALL_OPERAND_I64_IMMEDIATE:
        operand = MIR_new_int_op(emitter->ctx, value->immediate_i64);
        break;
    default:
        return -1;
    }
    if (emitter->frame.active) {
        em_emit_insn(emitter, MIR_new_insn(emitter->ctx, MIR_MOV,
            MIR_new_reg_op(emitter->ctx, emitter->frame.return_reg), operand));
        em_emit_insn(emitter, MIR_new_insn(emitter->ctx, MIR_JMP,
            MIR_new_label_op(emitter->ctx, emitter->frame.return_label)));
        return 0;
    }
    em_emit_insn(emitter, MIR_new_ret_insn(emitter->ctx, 1, operand));
    return 0;
}

static int jube_host_mir_function_frame_finalize(void* compiler_cursor,
        const char* debug_name) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->func || !emitter->func_item ||
            !emitter->frame.active || !debug_name || !*debug_name) {
        return -1;
    }
    MirFrameState* frame = &emitter->frame;
    frame->plan.debug_name = debug_name;
    em_emit_label(emitter, frame->return_label);

    MirRootWriteBackResult roots = {};
    em_finalize_semantic_root_write_back(emitter, frame->root_base,
        frame->anchor, false, 0, &frame->gc_candidates,
        &frame->gc_candidate_count, &frame->gc_candidate_capacity,
        &frame->gc_candidate_by_reg, &frame->gc_candidate_by_reg_capacity,
        frame->gc_call_sites, frame->gc_call_site_count, &roots, debug_name);
    frame->root_slot_count = roots.stable_slots + roots.scratch_slots;
    frame->root_store_count = roots.inserted_stores;
    em_finalize_scalar_homes(emitter);

    if (frame->item_return) {
        MIR_reg_t returned = frame->return_reg;
        if (!frame->incoming_scalar_home) return -1;
        returned = em_adopt_scalar_item(emitter, frame->scalar_return_mode,
            returned, frame->runtime, offsetof(Context, side_number_top),
            frame->number_base, frame->incoming_scalar_home);
        if (returned != frame->return_reg) {
            em_emit_insn(emitter, MIR_new_insn(emitter->ctx, MIR_MOV,
                MIR_new_reg_op(emitter->ctx, frame->return_reg),
                MIR_new_reg_op(emitter->ctx, returned)));
        }
    } else {
        em_store_frame_top(emitter, frame->runtime,
            offsetof(Context, side_number_top), frame->number_base);
    }
    if (frame->root_slot_count > 0) {
        em_store_frame_top(emitter, frame->runtime,
            offsetof(Context, side_root_top), frame->root_base);
    }
    em_emit_insn(emitter, MIR_new_ret_insn(emitter->ctx, 1,
        MIR_new_reg_op(emitter->ctx, frame->return_reg)));

    em_finalize_frame_prologue(emitter, frame->plan.entry_mode,
        offsetof(Context, side_root_top), offsetof(Context, side_root_limit),
        offsetof(Context, side_number_top), offsetof(Context, side_number_limit),
        offsetof(Context, side_root_commit_limit),
        offsetof(Context, side_number_commit_limit));
    frame->active = false;
    // Overflow handling follows frame teardown so the callee cannot observe
    // transient root or scalar-home state from a completed guest frame.
    em_call_void_1(emitter, "lambda_stack_overflow_error", MIR_T_P,
        MIR_new_int_op(emitter->ctx, (int64_t)(uintptr_t)"py-side-stack"), true);
    em_emit_insn(emitter, MIR_new_ret_insn(emitter->ctx, 1,
        MIR_new_uint_op(emitter->ctx, (uint64_t)ITEM_NULL)));
    em_finalize_function_metadata(emitter);
    em_frame_dispose(emitter);
    return 0;
}

static int jube_host_mir_note_root_candidate(MirEmitter* emitter,
        MIR_reg_t register_id) {
    if (!emitter || !emitter->frame.active || !register_id) return -1;
    // Call-result roots belong to the active host frame so finalization sees
    // the same candidate set that the shared call builder recorded.
    return em_root_note_candidate(&emitter->frame.gc_candidates,
        &emitter->frame.gc_candidate_count, &emitter->frame.gc_candidate_capacity,
        &emitter->frame.gc_candidate_by_reg,
        &emitter->frame.gc_candidate_by_reg_capacity, register_id,
        JIT_VALUE_UNKNOWN, 0) ? 0 : -1;
}

static int jube_host_mir_frame_root_candidate_note(void* compiler_cursor,
        uint32_t register_id) {
    return jube_host_mir_note_root_candidate(jube_host_mir_cursor_emitter(compiler_cursor),
        (MIR_reg_t)register_id);
}

static int jube_host_mir_function_frame_begin(void* compiler_cursor,
        uint32_t runtime_register) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->func || !emitter->func_item ||
            !runtime_register) {
        return -1;
    }
    if (!emitter->name_pool && ::context) emitter->name_pool = ::context->name_pool;
    // Reset before allocation so a guest cannot carry frame roots or scalar
    // homes across independently lowered functions.
    em_frame_dispose(emitter);
    MirFrameState* frame = &emitter->frame;
    frame->return_type = MIR_T_I64;
    frame->item_return = true;
    frame->scalar_return_mode = MIR_SCALAR_RETURN_DYNAMIC;
    frame->runtime = (MIR_reg_t)runtime_register;
    frame->root_base = em_new_reg(emitter, "py_root_frame", MIR_T_I64);
    frame->root_end = em_new_reg(emitter, "py_root_frame_end", MIR_T_I64);
    frame->number_base = em_new_reg(emitter, "py_number_frame", MIR_T_I64);
    frame->anchor = em_new_label(emitter);
    frame->return_label = em_new_label(emitter);
    frame->return_reg = em_new_reg(emitter, "py_return_value", MIR_T_I64);
    if (!frame->root_base || !frame->number_base || !frame->anchor ||
            !frame->return_label || !frame->return_reg) {
        em_frame_dispose(emitter);
        return -1;
    }
    frame->plan.entry_kind = FN_ENTRY_PUBLIC_WRAPPER;
    frame->plan.entry_mode = MIR_ENTRY_CHECKED;
    frame->active = true;
    em_emit_label(emitter, frame->anchor);
    return 0;
}

static int jube_host_mir_function_frame_scalar_return_home_set(void* compiler_cursor,
        uint32_t home_register) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->frame.active || !home_register) return -1;
    MirFrameState* frame = &emitter->frame;
    frame->incoming_scalar_home = (MIR_reg_t)home_register;
    frame->plan.scalar_home_lane_mask = FN_RETURN_HOME_NORMAL;
    frame->plan.accepts_caller_scalar_home = true;
    // v3 descriptor: a guest body that accepts a caller home is exactly one
    // that may return a wide scalar, i.e. the universal pair shape.
    frame->plan.return_shape = RETURN_SHAPE_ITEM_SCALAR;
    return 0;
}

static int jube_host_mir_compiler_import_cache_init(void* compiler_cursor,
        uint32_t initial_capacity) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !initial_capacity || emitter->import_cache) return -1;
    emitter->import_cache = em_import_cache_new((int)initial_capacity);
    return emitter->import_cache ? 0 : -1;
}

static void jube_host_mir_compiler_import_cache_destroy(void* compiler_cursor) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->import_cache) return;
    hashmap_free(emitter->import_cache);
    emitter->import_cache = NULL;
}

static int jube_host_mir_local_direct_call_prototype_get_or_create(
        void* compiler_cursor, const char* cache_key, const char* prototype_name,
        uint32_t parameter_count, const uint8_t* parameter_kinds,
        void** out_prototype_item) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->import_cache || !cache_key ||
            !*cache_key || !prototype_name || !*prototype_name ||
            !out_prototype_item || (parameter_count && !parameter_kinds)) {
        return -1;
    }
    MirImportCacheEntry key = {};
    key.name = mir_em_persist_cstr(emitter, cache_key).str;
    MirImportCacheEntry* cached =
        (MirImportCacheEntry*)hashmap_get(emitter->import_cache, &key);
    if (cached) {
        if (!cached->entry.proto) return -1;
        if (!jube_host_mir_cursor_owns_prototype(compiler_cursor,
                cached->entry.proto)) return -1;
        *out_prototype_item = cached->entry.proto;
        return 0;
    }
    void* prototype_item = NULL;
    if (jube_host_mir_item_function_proto_create_typed(emitter->ctx, prototype_name,
            parameter_count, parameter_kinds, &prototype_item) != 0 || !prototype_item) {
        return -1;
    }
    MirImportCacheEntry new_entry = {};
    new_entry.name = mir_em_persist_cstr(emitter, cache_key).str;
    new_entry.entry.proto = (MIR_item_t)prototype_item;
    hashmap_set(emitter->import_cache, &new_entry);
    if (!jube_host_mir_cursor_track_prototype(compiler_cursor,
            (MIR_item_t)prototype_item)) return -1;
    *out_prototype_item = prototype_item;
    return 0;
}

static int jube_host_mir_function_state_suspend(void* compiler_cursor,
        void** out_state_token) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !out_state_token) return -1;
    JubeMirFunctionStateToken* state = (JubeMirFunctionStateToken*)mem_calloc(1,
        sizeof(JubeMirFunctionStateToken), MEM_CAT_SYSTEM);
    if (!state) return -1;
    state->function_item = emitter->func_item;
    state->function = emitter->func;
    state->arguments = em_function_arguments_suspend(emitter);
    void* token = jube_host_mir_state_token_register(compiler_cursor, state);
    if (!token) {
        em_function_arguments_restore(emitter, state->arguments);
        mem_free(state);
        return -1;
    }
    *out_state_token = token;
    return 0;
}

static int jube_host_mir_function_select(void* compiler_cursor,
        void* function_item, void* function) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !function_item || !function) return -1;
    MIR_item_t item = (MIR_item_t)function_item;
    // MIR_get_item_func only checks the item kind, so host provenance must
    // reject a handle from another cursor before it reaches MIR state.
    if (!jube_host_mir_cursor_owns_function(compiler_cursor, item,
            (MIR_func_t)function)) return -1;
    MIR_func_t selected = MIR_get_item_func(emitter->ctx, item);
    if (!selected || selected != (MIR_func_t)function) return -1;
    emitter->func_item = item;
    emitter->func = selected;
    em_function_arguments_clear(emitter);
    return 0;
}

static int jube_host_mir_function_state_restore(void* compiler_cursor,
        void* state_token) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    int state_index = -1;
    JubeMirStateTokenEntry* entry = jube_host_mir_state_token_find(state_token,
        &state_index);
    // The token registry validates opaque identity before examining private
    // state, so forged or stale token values cannot dereference freed memory.
    if (!emitter || !entry || entry->owner_slot != jube_host_mir_cursor_slot(
            compiler_cursor) || !entry->state) return -1;
    JubeMirFunctionStateToken* state = entry->state;
    emitter->func_item = state->function_item;
    emitter->func = state->function;
    em_function_arguments_restore(emitter, state->arguments);
    jube_host_mir_state_token_remove_at(state_index);
    return 0;
}

static int jube_host_mir_function_register_lookup_current(void* compiler_cursor,
        const char* register_name, uint32_t* out_register) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->func || !register_name ||
            !*register_name || !out_register) return -1;
    MIR_reg_t register_id = MIR_reg(emitter->ctx, register_name, emitter->func);
    if (!register_id) return -1;
    // Argument identities feed the host liveness analysis; recording them here
    // prevents guests from mutating the emitter's tracking array directly.
    em_function_argument_register(emitter, register_id);
    *out_register = (uint32_t)register_id;
    return 0;
}

static int jube_host_mir_function_register_create_current(void* compiler_cursor,
        const char* prefix, uint8_t value_kind, uint32_t* out_register) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->func) return -1;
    return jube_host_mir_function_register_create(emitter->ctx, emitter->func,
        &emitter->reg_counter, prefix, value_kind, out_register);
}

static int jube_host_mir_label_create_current(void* compiler_cursor,
        void** out_label) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !out_label) return -1;
    if (jube_host_mir_label_create(emitter->ctx, out_label) != 0) return -1;
    return jube_host_mir_cursor_track_label(compiler_cursor,
        (MIR_label_t)*out_label) ? 0 : -1;
}

static int jube_host_mir_instruction_emit_current(void* compiler_cursor,
        const JubeCompilerInstruction* instruction) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    return emitter && emitter->ctx && emitter->func_item
        ? jube_host_mir_instruction_emit(emitter->ctx, emitter->func_item, instruction) : -1;
}

static int jube_host_mir_label_emit_current(void* compiler_cursor, void* label) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !emitter->func_item ||
            !jube_host_mir_cursor_owns_label(compiler_cursor, (MIR_label_t)label)) {
        return -1;
    }
    return jube_host_mir_label_emit(emitter->ctx, emitter->func_item, label);
}

static int jube_host_mir_function_frame_runtime_load_current(void* compiler_cursor,
        void* frame_runtime_slot, uint32_t* out_runtime_register) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    return emitter && emitter->ctx && emitter->func_item && emitter->func
        ? jube_host_mir_function_frame_runtime_load(emitter->ctx, emitter->func_item,
            emitter->func, frame_runtime_slot, out_runtime_register) : -1;
}

static void jube_host_mir_function_finish_current(void* compiler_cursor) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (emitter && emitter->ctx) jube_host_mir_function_finish(emitter->ctx);
}

static int jube_host_mir_function_forward_create(void* mir_context,
        const char* function_name, void** out_function_item);

static int jube_host_mir_item_function_create_typed_current(void* compiler_cursor,
        const char* function_name, uint32_t parameter_count,
        const char* const* parameter_names, const uint8_t* parameter_kinds,
        void** out_function_item, void** out_function) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !out_function_item || !out_function) return -1;
    if (jube_host_mir_item_function_create_typed(emitter->ctx, function_name,
            parameter_count, parameter_names, parameter_kinds, out_function_item,
            out_function) != 0) return -1;
    return jube_host_mir_cursor_track_function(compiler_cursor,
        (MIR_item_t)*out_function_item, (MIR_func_t)*out_function) ? 0 : -1;
}

static int jube_host_mir_function_forward_create_current(void* compiler_cursor,
        const char* function_name, void** out_function_item) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->ctx || !out_function_item) return -1;
    if (jube_host_mir_function_forward_create(emitter->ctx, function_name,
            out_function_item) != 0) return -1;
    return jube_host_mir_cursor_track_function(compiler_cursor,
        (MIR_item_t)*out_function_item, NULL) ? 0 : -1;
}

static int jube_host_mir_module_finalize_and_load_current(void* compiler_cursor,
        void* mir_module) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    return emitter && emitter->ctx
        ? jube_host_mir_module_finalize_and_load(emitter->ctx, mir_module) : -1;
}

static void* jube_host_mir_function_lookup_current(void* compiler_cursor,
        const char* function_name) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    return emitter && emitter->ctx
        ? jube_host_mir_function_lookup(emitter->ctx, function_name) : NULL;
}

static int jube_host_mir_scalar_home_create_current(void* compiler_cursor,
        int32_t* out_home_id, uint32_t* out_address_register) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !out_home_id || !out_address_register) return -1;
    int home_id = em_scalar_home_new(emitter);
    MIR_reg_t address = em_materialize_frame_ref(emitter,
        em_scalar_home_ref(emitter, home_id));
    if (home_id <= 0 || !address) return -1;
    // The active frame owns scalar-home fixups, so guest lowering cannot
    // allocate a result address that escapes its current compilation frame.
    *out_home_id = home_id;
    *out_address_register = (uint32_t)address;
    return 0;
}

static int jube_host_mir_scalar_home_bind_current(void* compiler_cursor,
        int32_t home_id, uint32_t value_register) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || home_id <= 0 || !value_register) return -1;
    em_scalar_home_bind(emitter, home_id, (MIR_reg_t)value_register);
    return 0;
}

static bool jube_host_mir_lookup_import_metadata(const char* name,
        JitImportMetadata* out_metadata) {
    return name && out_metadata && jit_import_get_metadata(name, out_metadata);
}

static void jube_host_mir_root_call_value(void* compiler_cursor,
        MIR_reg_t register_id, JitValueClass value_class) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter || !emitter->frame.active || !register_id) return;
    em_root_note_candidate(&emitter->frame.gc_candidates,
        &emitter->frame.gc_candidate_count, &emitter->frame.gc_candidate_capacity,
        &emitter->frame.gc_candidate_by_reg,
        &emitter->frame.gc_candidate_by_reg_capacity, register_id,
        value_class, 0);
}

static int jube_host_mir_compiler_cursor_create(void* mir_context,
        void** out_compiler_cursor) {
    MIR_context_t context = jube_host_mir_context_from_handle(mir_context);
    if (!context || !out_compiler_cursor) return -1;
    MirEmitter* emitter = (MirEmitter*)mem_calloc(1, sizeof(MirEmitter), MEM_CAT_SYSTEM);
    if (!emitter) return -1;
    emitter->ctx = context;
    // Jube cache keys share the active guest's persistent name owner; a
    // temporary key buffer must never become a hashmap-held pointer.
    emitter->name_pool = ::context ? ::context->name_pool : NULL;
    void* compiler_cursor = jube_host_mir_cursor_register(emitter);
    if (!compiler_cursor) {
        mem_free(emitter);
        return -1;
    }
    emitter->call_owner = compiler_cursor;
    // The host keeps call metadata and root tracking in the same emitter that
    // owns frame fixups, so no guest-built compiler state can diverge.
    emitter->root_call_value = jube_host_mir_root_call_value;
    emitter->lookup_import_metadata = jube_host_mir_lookup_import_metadata;
    *out_compiler_cursor = compiler_cursor;
    return 0;
}

static void jube_host_mir_compiler_cursor_destroy(void* compiler_cursor) {
    MirEmitter* emitter = jube_host_mir_cursor_emitter(compiler_cursor);
    if (!emitter) return;
    // Invalidate the opaque handle before freeing emitter state so stale guest
    // uses are rejected by the handle table instead of dereferencing memory.
    jube_host_mir_cursor_unregister(compiler_cursor);
    jube_host_mir_state_tokens_discard_slot(
        jube_host_mir_cursor_slot(compiler_cursor));
    jube_host_mir_cursor_function_handles_cleanup(
        jube_host_mir_cursor_slot(compiler_cursor));
    jube_host_mir_cursor_label_handles_cleanup(
        jube_host_mir_cursor_slot(compiler_cursor));
    jube_host_mir_cursor_prototype_handles_cleanup(
        jube_host_mir_cursor_slot(compiler_cursor));
    jube_host_mir_cursor_dispose(emitter);
}

static void jube_host_mir_debug_dump_if_enabled(void* mir_context) {
#if !defined(NDEBUG)
    MIR_context_t context = jube_host_mir_context_from_handle(mir_context);
    if (context && mir_dump_instrumentation_enabled()) {
        mir_dump_write_context(context, "temp/py_mir_dump.txt", false);
    }
#else
    (void)mir_context;
#endif
}

static int jube_host_mir_function_forward_create(void* mir_context,
        const char* function_name, void** out_function_item) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !function_name || !*function_name || !out_function_item) return -1;
    MIR_item_t function_item = MIR_new_forward(context, function_name);
    if (!function_item) return -1;
    // A forward item is owned by the module context; publishing its raw MIR
    // layout would let a guest outlive or mutate the host compilation graph.
    *out_function_item = function_item;
    return 0;
}

static int jube_host_mir_item_function_proto_create(void* mir_context,
        const char* prototype_name, uint32_t parameter_count, void** out_prototype_item) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !prototype_name || !*prototype_name || !out_prototype_item ||
        parameter_count > 1024) return -1;
    MIR_var_t* parameters = NULL;
    char (*parameter_names)[16] = NULL;
    if (parameter_count > 0) {
        parameters = (MIR_var_t*)mem_calloc(parameter_count, sizeof(MIR_var_t), MEM_CAT_SYSTEM);
        parameter_names = (char (*)[16])mem_calloc(parameter_count, sizeof(*parameter_names),
            MEM_CAT_SYSTEM);
        if (!parameters || !parameter_names) {
            if (parameters) mem_free(parameters);
            if (parameter_names) mem_free(parameter_names);
            return -1;
        }
        for (uint32_t i = 0; i < parameter_count; i++) {
            snprintf(parameter_names[i], sizeof(parameter_names[i]), "p%u", i);
            parameters[i] = {MIR_T_I64, parameter_names[i], 0};
        }
    }
    MIR_type_t result_type = MIR_T_I64;
    MIR_item_t prototype = MIR_new_proto_arr(context, prototype_name,
        1, &result_type, parameter_count, parameters);
    if (parameters) mem_free(parameters);
    if (parameter_names) mem_free(parameter_names);
    if (!prototype) return -1;
    // Parameter descriptors are temporary host construction state; the guest
    // receives only the context-bound opaque prototype handle.
    *out_prototype_item = prototype;
    return 0;
}

static int jube_host_mir_function_register_lookup(void* mir_context, void* function,
        const char* register_name, uint32_t* out_register) {
    MIR_context_t context = jube_host_mir_context_from_argument(mir_context);
    if (!context || !function || !register_name || !*register_name || !out_register) {
        return -1;
    }
    // MIR's function register table stays host-private; only the numeric
    // register identity required by the lowering service crosses this boundary.
    *out_register = MIR_reg(context, register_name, (MIR_func_t)function);
    return 0;
}

static int jube_host_execution_activate(void* execution_context, void** out_input) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    if (!execution || !out_input || execution->activation_active) return -1;

    execution->reusing_context = context && context->heap;
    if (execution->reusing_context) {
        execution->active_context = context;
    } else {
        execution->active_context = runtime_get_eval_context(
            jube_guest_execution_runtime(execution));
        if (!execution->active_context) return -1;
        if (!eval_context_init(execution->active_context)) return -1;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create_runtime(context->pool);
        context->type_list = arraylist_new(64);
    }
    // Generated guests load this execution-owned pointer before their frame
    // prologue. It is fixed for the activation, so no generated path observes
    // a shared mutable runtime cell.
    execution->frame_runtime = (Context*)context;
    execution->input = Input::create(context->pool);
    if (!execution->input) {
        mir_guest_finish_context(jube_guest_execution_runtime(execution),
                                 execution->reusing_context);
        return -1;
    }
    execution->activation_active = true;
    execution->faulted = false;
    execution->previous_active_execution = jube_active_guest_execution;
    jube_active_guest_execution = execution;
    *out_input = execution->input;
    return 0;
}

static int jube_host_execution_activate_import(void* execution_context, void** out_input,
                                               bool* out_retained_until_heap_cleanup) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    if (!execution || !execution->import_execution || !out_retained_until_heap_cleanup) return -1;
    if (jube_host_execution_activate(execution, out_input) != 0) return -1;
    // A newly created context owns a heap transferred to the caller Runtime;
    // keep this wrapper alive so its cleanup restores TLS at that runtime's end.
    execution->import_retained = !execution->reusing_context;
    *out_retained_until_heap_cleanup = execution->import_retained;
    return 0;
}

typedef Item (*JubeGuestMainFn)(Context* runtime);
typedef Item (*JubeGuestMainIntoFn)(Context* runtime, uint64_t* result_home);

static int jube_host_execution_run_main(void* execution_context, void* entry_function,
                                        Item* out_result) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    JubeGuestMainFn entry = (JubeGuestMainFn)entry_function;
    if (!execution || !execution->activation_active || execution->faulted || !entry || !out_result) return -1;

    LambdaRecoveryFrame* recovery_frame = lambda_recovery_frame_begin_for(
        (Context*)execution->active_context,
        LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY |
            LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER);
    if (!recovery_frame) {
        log_error("jube-guest: failed to allocate recovery frame");
        jube_guest_execution_poison(execution);
        *out_result = lambda_recovery_publish_fault_item(
            (Context*)execution->active_context, LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK);
        return -1;
    }
    if (LAMBDA_RECOVERY_FRAME_SETJMP(recovery_frame)) {
        Item recovered = ItemError;
        if (!lambda_recovery_frame_restore_landing(recovery_frame)) {
            log_error("jube-guest: recovery frame landing invariant failed");
            recovered = lambda_recovery_publish_fault_item((Context*)execution->active_context,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            recovered = lambda_recovery_frame_fault_item((Context*)execution->active_context,
                recovery_frame);
        }
        _lambda_stack_overflow_flag = false;
        lambda_recovery_frame_end(recovery_frame);
        jube_guest_execution_poison(execution);
        *out_result = recovered;
        return -1;
    }
    if (!lambda_recovery_frame_arm(recovery_frame)) {
        log_error("jube-guest: failed to arm recovery frame");
        lambda_recovery_frame_end(recovery_frame);
        jube_guest_execution_poison(execution);
        *out_result = lambda_recovery_publish_fault_item(
            (Context*)execution->active_context,
            LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        return -1;
    }
    *out_result = entry((Context*)context);
    lambda_recovery_frame_end(recovery_frame);
    return 0;
}

static int jube_host_execution_run_main_into(void* execution_context,
        void* entry_function, Item* out_result) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    JubeGuestMainIntoFn entry = (JubeGuestMainIntoFn)entry_function;
    if (!execution || !execution->activation_active || execution->faulted || !entry || !out_result) return -1;

    LambdaRecoveryFrame* recovery_frame = lambda_recovery_frame_begin_for(
        (Context*)execution->active_context,
        LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY |
            LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER);
    if (!recovery_frame) {
        log_error("jube-guest: failed to allocate result-home recovery frame");
        jube_guest_execution_poison(execution);
        *out_result = lambda_recovery_publish_fault_item(
            (Context*)execution->active_context, LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK);
        return -1;
    }
    if (LAMBDA_RECOVERY_FRAME_SETJMP(recovery_frame)) {
        Item recovered = ItemError;
        if (!lambda_recovery_frame_restore_landing(recovery_frame)) {
            log_error("jube-guest: result-home recovery frame landing invariant failed");
            recovered = lambda_recovery_publish_fault_item((Context*)execution->active_context,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            recovered = lambda_recovery_frame_fault_item((Context*)execution->active_context,
                recovery_frame);
        }
        _lambda_stack_overflow_flag = false;
        lambda_recovery_frame_end(recovery_frame);
        jube_guest_execution_poison(execution);
        *out_result = recovered;
        return -1;
    }
    if (!lambda_recovery_frame_arm(recovery_frame)) {
        log_error("jube-guest: failed to arm result-home recovery frame");
        lambda_recovery_frame_end(recovery_frame);
        jube_guest_execution_poison(execution);
        *out_result = lambda_recovery_publish_fault_item(
            (Context*)execution->active_context,
            LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        return -1;
    }
    // The execution owns this slot until its caller has consumed the result.
    execution->result_scalar_home = 0;
    *out_result = entry((Context*)context, &execution->result_scalar_home);
    lambda_recovery_frame_end(recovery_frame);
    return 0;
}

static void jube_host_execution_finish_guest(void* execution_context) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    if (!execution || !execution->activation_active) return;
    // Restore nested activation state before releasing the opaque data token.
    jube_active_guest_execution = execution->previous_active_execution;
    execution->previous_active_execution = NULL;
    mir_guest_finish_context(jube_guest_execution_runtime(execution),
                             execution->reusing_context);
    execution->input = NULL;
    execution->active_context = NULL;
    execution->frame_runtime = NULL;
    execution->faulted = false;
    execution->reusing_context = false;
    execution->activation_active = false;
}

static void* jube_host_execution_frame_runtime_slot(void* execution_context) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    if (!execution || !execution->activation_active || execution->faulted ||
            execution != jube_active_guest_execution) {
        return NULL;
    }
    // Nested imports reuse the caller heap but their wrapper is released after
    // loading; generated functions must therefore retain the caller's live
    // runtime slot instead of the import wrapper's cleared slot (D3.4.7).
    JubeGuestExecution* owner = execution->reusing_context &&
        execution->previous_active_execution
        ? execution->previous_active_execution : execution;
    return owner->activation_active ? &owner->frame_runtime : NULL;
}

static Input* jube_host_data_input(void* session) {
    JubeGuestExecution* execution = jube_active_guest_execution;
    if (!execution || !execution->activation_active || execution->faulted || !execution->input ||
        session != execution->input) {
        return NULL;
    }
    return execution->input;
}

static int jube_host_opaque_persistent_root_register(void* session, uint64_t* slot) {
    if ((!jube_host_data_input(session) && !jube_host_node_session_is_live(session)) || !slot) {
        return -1;
    }
    // TLS storage is outside the side-root stack, so register its exact Item
    // slot with the active guest heap rather than exposing that heap to a module.
    heap_register_gc_root(slot);
    return 0;
}

static int jube_host_opaque_persistent_root_unregister(void* session, uint64_t* slot) {
    if ((!jube_host_data_input(session) && !jube_host_node_session_is_live(session)) || !slot) {
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
    return js_get_global_property((Item){.item = s2it(heap_create_name("process", 7))});
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
        if (!typed_array || !typed_array->buffer || js_arraybuffer_detached(typed_array->buffer)) {
            return JUBE_BINARY_VIEW_DETACHED;
        }
        int byte_length = js_typed_array_byte_length(value);
        int byte_offset = js_typed_array_byte_offset(value);
        int backing_length = js_arraybuffer_length(typed_array->buffer);
        if (byte_length < 0 || byte_offset < 0 || byte_offset > backing_length ||
                byte_length > backing_length - byte_offset) {
            return JUBE_BINARY_VIEW_OUT_OF_BOUNDS;
        }
        out_view->array_buffer = typed_array->buffer_item
            ? (Item){.item = typed_array->buffer_item}
            : js_arraybuffer_wrap(typed_array->buffer);
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
        case LMD_TYPE_ARRAY: return JUBE_VALUE_ARRAY;
        case LMD_TYPE_MAP:
        case LMD_TYPE_VMAP: return JUBE_VALUE_OBJECT;
        case LMD_TYPE_FUNC: return JUBE_VALUE_FUNCTION;
        case LMD_TYPE_SYMBOL: return JUBE_VALUE_SYMBOL;
        case LMD_TYPE_DECIMAL: {
            Decimal* decimal = value.get_decimal();
            return decimal && decimal->unlimited == DECIMAL_BIGINT
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
    if (!type || !(type->flags & JUBE_TYPE_OWNING_NATIVE)) return ItemNull;
    Item object = vmap_new();
    if (get_type_id(object) != LMD_TYPE_VMAP || !object.vmap) return ItemNull;
    // Only the host writes VMap metadata: modules receive a branded wrapper
    // without coupling their ABI to its moving-GC object layout.
    object.vmap->host_type = (const void*)type;
    object.vmap->host_data = payload;
    return object;
}

static void* jube_host_value_native_object_data(Item object, const JubeTypeDef* type) {
    if (!type || get_type_id(object) != LMD_TYPE_VMAP || !object.vmap ||
            object.vmap->host_type != (const void*)type) return NULL;
    return object.vmap->host_data;
}

static bool jube_host_value_string_copy(Item value, char* out, size_t out_size,
                                        size_t* out_length) {
    if (out_length) *out_length = 0;
    if (!out || out_size == 0 || get_type_id(value) != LMD_TYPE_STRING) return false;
    String* string = it2s(value);
    if (!string || string->len >= out_size) return false;
    memcpy(out, string->chars, string->len);
    out[string->len] = '\0';
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

static Item jube_host_node_transform_new(Item options) {
    return js_transform_new(options);
}

static Item jube_host_node_transform_prototype(void) {
    // initialize the host stream prototypes before handing the opaque parent
    // to a leaf module; the module must not resolve a host namespace itself.
    (void)js_get_stream_namespace();
    return js_get_stream_transform_prototype();
}

static Item jube_host_node_readable_push(Item stream, Item chunk) {
    return js_readable_push(stream, chunk);
}

static void jube_host_node_flush_data_if_flowing(Item stream) {
    js_stream_flush_data_if_flowing(stream);
}

static void jube_host_node_transform_flush_drained(Item stream) {
    js_stream_transform_flush_drained(stream);
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

static bool jube_host_node_network_permission_has_net(void) {
    return js_permission_has_net() != 0;
}

static Item jube_host_node_network_permission_make_error(void* session, const char* syscall,
                                                          const char* resource) {
    if (!jube_host_node_session_is_live(session) || !syscall) return ItemNull;
    // The permission helper constructs the Node-visible ERR_ACCESS_DENIED
    // object without throwing, so async DNS can deliver it on its callback turn.
    return js_permission_make_net_error(syscall, resource);
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

static void jube_node_async_work_remove(JubeNodeAsyncWork* job) {
    NodeRuntimeSession* session = job ? (NodeRuntimeSession*)job->session : NULL;
    if (!session || !session->async_work) return;
    for (int i = 0; i < session->async_work->length; i++) {
        if (session->async_work->data[i] == job) {
            arraylist_remove(session->async_work, i);
            return;
        }
    }
}

static void jube_host_node_work_execute(uv_work_t* request) {
    JubeNodeAsyncWork* job = request ? (JubeNodeAsyncWork*)request->data : NULL;
    if (job && job->work) job->work(job->user);
}

static void jube_host_node_work_complete(uv_work_t* request, int status) {
    JubeNodeAsyncWork* job = request ? (JubeNodeAsyncWork*)request->data : NULL;
    if (!job) return;
    jube_node_async_work_remove(job);
    NodeRuntimeSession* session = (NodeRuntimeSession*)job->session;
    // Detached heaps must never receive a late completion; release only the
    // module-owned POD payload after libuv has returned ownership to the host.
    if (session && !session->detaching && jube_host_node_session_is_live(session) &&
            job->complete) {
        job->complete(job->user, status);
    }
    if (job->destroy) job->destroy(job->user);
    mem_free(job);
}

static int jube_host_node_work_submit(void* session, JubeAsyncWorkCallback work,
                                      JubeAsyncCompletionCallback complete,
                                      JubeAsyncDestroyCallback destroy, void* user,
                                      uint32_t* out_request_id) {
    if (out_request_id) *out_request_id = 0;
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!jube_host_node_session_is_live(session) || node_session->detaching || !work ||
            !destroy || !lambda_uv_loop()) return -1;
    if (!node_session->async_work) {
        node_session->async_work = arraylist_new(8);
        if (!node_session->async_work) return -1;
    }
    JubeNodeAsyncWork* job = (JubeNodeAsyncWork*)mem_calloc(1, sizeof(JubeNodeAsyncWork),
        MEM_CAT_SYSTEM);
    if (!job) return -1;
    uint32_t request_id = ++node_session->async_work_next_id;
    if (request_id == 0) request_id = ++node_session->async_work_next_id;
    job->session = session;
    job->work = work;
    job->complete = complete;
    job->destroy = destroy;
    job->user = user;
    job->request_id = request_id;
    job->request.data = job;
    if (!arraylist_append(node_session->async_work, job)) {
        mem_free(job);
        return -1;
    }
    int status = uv_queue_work(lambda_uv_loop(), &job->request, jube_host_node_work_execute,
        jube_host_node_work_complete);
    if (status != 0) {
        jube_node_async_work_remove(job);
        mem_free(job);
        return status;
    }
    if (out_request_id) *out_request_id = request_id;
    return 0;
}

static int jube_host_node_work_cancel(void* session, uint32_t request_id) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!jube_host_node_session_is_live(session) || request_id == 0 || !node_session->async_work) {
        return -1;
    }
    for (int i = 0; i < node_session->async_work->length; i++) {
        JubeNodeAsyncWork* job = (JubeNodeAsyncWork*)node_session->async_work->data[i];
        if (job && job->session == session && job->request_id == request_id) {
            return uv_cancel((uv_req_t*)&job->request);
        }
    }
    return -1;
}

static void jube_node_async_cancel_session(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!node_session || !node_session->async_work) return;
    for (int i = 0; i < node_session->async_work->length; i++) {
        JubeNodeAsyncWork* job = (JubeNodeAsyncWork*)node_session->async_work->data[i];
        if (job && job->session == session) {
            (void)uv_cancel((uv_req_t*)&job->request);
        }
    }
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
    // The table is the single host-resident compatibility boundary. Jube
    // descriptors, rather than js_runtime's legacy dispatcher, decide which
    // profiles expose these names while their implementations migrate.
    static const JubeHostNamespaceEntry entries[] = {
        {"events", node_events_namespace},
        {"fs", js_get_fs_namespace},
        {"fs/promises", js_get_fs_promises_namespace},
        {"internal/fs/promises", js_get_internal_fs_promises_namespace},
        {"internal/fs/utils", js_get_internal_fs_utils_namespace},
        {"child_process", js_get_child_process_namespace},
        {"net", js_get_net_namespace},
        {"internal/js_stream_socket", js_get_internal_js_stream_socket_constructor},
        {"tls", js_get_tls_namespace},
        {"http", js_get_http_namespace},
        {"_http_agent", js_get_http_namespace},
        {"_http_common", js_get_http_namespace},
        {"_http_server", js_get_http_namespace},
        {"_http_outgoing", js_get_http_namespace},
        {"https", js_get_https_namespace},
        {"internal/errors", js_get_internal_errors_namespace},
        {"internal/assert/myers_diff", js_get_internal_assert_myers_diff_namespace},
        {"internal/async_hooks", js_get_internal_async_hooks_namespace},
        {"internal/async_context_frame", js_get_internal_async_context_frame_namespace},
        {"internal/streams/add-abort-signal", js_get_internal_stream_add_abort_signal_namespace},
        {"internal/streams/end-of-stream", js_get_internal_stream_end_of_stream_namespace},
        {"internal/streams/state", js_get_internal_stream_state_namespace},
        {"internal/crypto/util", js_get_internal_crypto_util_namespace},
        {"internal/util", js_get_internal_util_namespace},
        {"internal/util/inspect", js_get_internal_util_inspect_namespace},
        {"internal/repl", js_get_internal_repl_namespace},
        {"internal/test/binding", js_get_internal_test_binding_namespace},
        {"buffer", js_get_buffer_namespace},
        {"util", js_get_util_namespace},
        {"url", node_url_namespace},
        {"assert", js_get_assert_namespace},
        {"stream", js_get_stream_namespace},
        {"stream/consumers", js_get_stream_namespace},
        {"stream/promises", js_get_stream_promises_namespace},
        {"stream/web", js_get_stream_web_namespace},
        {"stream/iter", js_get_stream_iter_namespace},
        {"repl", js_get_repl_namespace},
        {"diagnostics_channel", js_get_diagnostics_channel_namespace},
        {"module", js_get_node_module_namespace},
        {"vm", js_get_vm_namespace},
        {"async_hooks", js_get_async_hooks_namespace},
        {"trace_events", node_trace_events_namespace},
        {"domain", js_get_domain_namespace},
        {"cluster", js_get_cluster_namespace},
        {"readline", js_get_readline_namespace},
        {"readline/promises", js_get_readline_promises_namespace},
        {"test", js_get_node_test_namespace},
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (strcmp(specifier, entries[i].specifier) == 0) {
            *out_namespace = entries[i].factory();
            return 0;
        }
    }
    return -1;
}

static Item jube_host_data_name_from_utf8(void* session, const char* text) {
    if (!jube_host_data_input(session) || !text) return ItemNull;
    return (Item){.item = s2it(heap_create_name(text))};
}

static Item jube_host_data_name_from_utf8_n(void* session, const char* text,
                                             size_t length) {
    if (!jube_host_data_input(session) || !text) return ItemNull;
    return (Item){.item = s2it(heap_create_name(text, length))};
}

static int jube_host_data_map_set(void* session, Item map, Item key, Item value) {
    Input* input = jube_host_data_input(session);
    if (!input || get_type_id(map) != LMD_TYPE_MAP || get_type_id(key) != LMD_TYPE_STRING) {
        return -1;
    }
    Map* target = it2map(map);
    String* name = it2s(key);
    if (!target || !name) return -1;
    map_put_heap(target, name, value, input);
    return 0;
}

static Item jube_host_data_float_from_f64(void* session, double value) {
    if (!jube_host_data_input(session)) return ItemNull;
    return push_d(value);
}

static Item jube_host_data_format_json(void* session, Item value) {
    Input* input = jube_host_data_input(session);
    if (!input) return ItemNull;
    String* result = format_json(input->pool, value);
    return result ? (Item){.item = s2it(result)} : ItemNull;
}

static int jube_host_data_closure_env_item_count(void* session, void* environment) {
    Input* input = jube_host_data_input(session);
    if (!input || !environment || !context || !context->heap || !context->heap->gc ||
        !gc_is_managed(context->heap->gc, environment)) {
        return 0;
    }
    gc_header_t* header = gc_get_header(environment);
    if (!header || header->type_tag != GC_TYPE_JS_ENV || header->alloc_size == 0) return 0;
    return (int)(header->alloc_size / (2 * sizeof(Item)));
}

static void* jube_host_data_closure_env_alloc(void* session, size_t item_count) {
    if (!jube_host_data_input(session) || item_count == 0) return NULL;
    return heap_calloc_closure_env(item_count * sizeof(Item));
}

static int jube_host_data_closure_env_store(void* session, void* environment,
                                             int slot, Item value) {
    int item_count = jube_host_data_closure_env_item_count(session, environment);
    if (item_count <= 0 || slot < 0 || slot >= item_count) return -1;
    owned_item_slot_store((Item*)environment, item_count, slot, value);
    return 0;
}

static int jube_host_data_closure_env_load(void* session, void* environment,
                                            int slot, Item* out_value) {
    int item_count = jube_host_data_closure_env_item_count(session, environment);
    if (!out_value || item_count <= 0 || slot < 0 || slot >= item_count) return -1;
    *out_value = owned_item_slot_read((Item*)environment, item_count, slot, false);
    return 0;
}

static int jube_host_data_item_slots_store(void* session, Item* storage,
                                            int64_t item_count, int64_t slot,
                                            Item value) {
    if (!jube_host_data_input(session) || !storage || item_count <= 0 ||
        slot < 0 || slot >= item_count) return -1;
    owned_item_slot_store(storage, item_count, slot, value);
    return 0;
}

static int jube_host_data_item_slots_load(void* session, Item* storage,
                                          int64_t item_count, int64_t slot,
                                          Item* out_value) {
    if (!jube_host_data_input(session) || !storage || !out_value || item_count <= 0 ||
        slot < 0 || slot >= item_count) return -1;
    *out_value = owned_item_slot_read(storage, item_count, slot, false);
    return 0;
}

static Item jube_host_data_map_new(void* session) {
    if (!jube_host_data_input(session)) return ItemNull;
    Map* map = (Map*)heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, 1);
    if (!map) return ItemNull;
    map->type_id = LMD_TYPE_MAP;
    map->type = &EmptyMap;
    return (Item){.map = map};
}

static Item jube_host_data_function_new(void* session, void* function_ptr, int param_count) {
    if (!jube_host_data_input(session) || !function_ptr || param_count < 0 || param_count > 255) {
        return ItemNull;
    }
    Function* function = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    if (!function) return ItemNull;
    function->type_id = LMD_TYPE_FUNC;
    function->entry_abi = FN_ENTRY_ABI_FOREIGN;
    function->ptr = (fn_ptr)function_ptr;
    function->arity = (uint8_t)param_count;
    return (Item){.function = function};
}

static const JubeHostDataAPI jube_host_data_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostDataAPI),
    jube_host_data_name_from_utf8,
    jube_host_data_map_set,
    jube_host_data_float_from_f64,
    jube_host_data_format_json,
    jube_host_data_closure_env_alloc,
    jube_host_data_closure_env_store,
    jube_host_data_closure_env_load,
    jube_host_data_item_slots_store,
    jube_host_data_item_slots_load,
    jube_host_data_map_new,
    jube_host_data_function_new,
    jube_host_data_name_from_utf8_n,
};

static int jube_host_module_loading_namespace(void* execution_context,
                                              const char* source_path,
                                              Item* out_namespace) {
    if (!execution_context || !source_path || !*source_path || !out_namespace) return -1;
    *out_namespace = ItemNull;
    Runtime* runtime = (Runtime*)jube_execution_runtime_handle(execution_context);
    ModuleDescriptor* module = module_get_for_runtime(runtime, source_path);
    if (!module || !module->loading) return 0;
    // The registry owns partial namespaces so hosted compilers do not depend
    // on ModuleDescriptor layout when preserving circular-import semantics.
    *out_namespace = module->namespace_obj;
    return out_namespace->item == ItemNull.item ? 0 : 1;
}

static int jube_host_load_lambda_module(void* execution_context, const char* source_path,
                                        const char* importer_path, Item* out_namespace) {
    (void)importer_path;
    if (!execution_context || !source_path || !*source_path || !out_namespace) return -1;
    *out_namespace = ItemNull;
    Runtime* runtime = (Runtime*)jube_execution_runtime_handle(execution_context);
    if (!runtime) return -1;
    // cross-language exports need the native MIR entrypoint; the planner's
    // T0 path intentionally leaves imported Lambda scripts without a callable
    // main function.
    Script* script = load_script_mir_direct(runtime, source_path, NULL, true);
    if (!script || !script->ast_root) return -1;
    *out_namespace = module_build_lambda_namespace(script);
    return out_namespace->item == ItemNull.item ? -1 : 0;
}

static const ModuleNamespaceOps* jube_host_namespace_ops(
        const JubeModuleNamespaceOps* namespace_ops) {
    // The hosted ABI deliberately mirrors the registry membrane fields; this
    // cast keeps the registry implementation private without copying a
    // language-owned callback table into every graph entry.
    static_assert(sizeof(JubeModuleNamespaceOps) == sizeof(ModuleNamespaceOps),
        "Jube and registry namespace membranes must remain layout-compatible");
    return (const ModuleNamespaceOps*)namespace_ops;
}

static int jube_host_module_state(void* execution_context, const char* source_path,
                                  Item* out_namespace) {
    if (!execution_context || !source_path || !*source_path || !out_namespace) return -1;
    *out_namespace = ItemNull;
    Runtime* runtime = (Runtime*)jube_execution_runtime_handle(execution_context);
    ModuleDescriptor* module = module_get_for_runtime(runtime, source_path);
    if (!module) return 0;
    *out_namespace = module->namespace_obj;
    return module->initialized ? 2 : module->loading ? 1 : 0;
}

static int jube_host_module_begin_loading(void* execution_context, const char* source_path,
                                          const char* language,
                                          const JubeModuleNamespaceOps* namespace_ops) {
    if (!execution_context || !source_path || !*source_path || !language || !*language ||
        !namespace_ops) return -1;
    Runtime* runtime = (Runtime*)jube_execution_runtime_handle(execution_context);
    ModuleDescriptor* module = module_register_loading_with_namespace_ops_for_runtime(
        runtime, source_path, language, jube_host_namespace_ops(namespace_ops));
    return module ? 0 : -1;
}

static int jube_host_module_publish(void* execution_context, const char* source_path,
                                    const char* language, Item namespace_obj, void* mir_context,
                                    const JubeModuleNamespaceOps* namespace_ops) {
    if (!execution_context || !source_path || !*source_path || !language || !*language ||
        !namespace_ops || namespace_obj.item == ItemNull.item) return -1;
    Runtime* runtime = (Runtime*)jube_execution_runtime_handle(execution_context);
    module_register_with_namespace_ops_for_runtime(
        runtime, source_path, language, namespace_obj, mir_context,
        jube_host_namespace_ops(namespace_ops));
    return 0;
}

static const JubeSourceAPI jube_host_source_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeSourceAPI),
    jube_host_source_read,
    jube_host_source_release,
    jube_host_source_line_column,
};

static const JubeDiagnosticAPI jube_host_diagnostic_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeDiagnosticAPI),
    jube_host_write_diagnostic,
};

static const JubeOutputAPI jube_host_output_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeOutputAPI),
    jube_host_write_result,
};

static const JubeSessionMemoryAPI jube_host_session_memory_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeSessionMemoryAPI),
    jube_host_session_alloc,
    jube_host_session_free,
};

static const JubeGuestExecutionAPI jube_host_execution_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeGuestExecutionAPI),
    jube_host_execution_create,
    jube_host_execution_destroy,
    jube_host_execution_link_module,
    jube_host_mir_context_create,
    jube_host_mir_context_destroy,
    jube_host_mir_module_create,
    jube_host_mir_module_finalize_and_load,
    jube_host_mir_function_lookup,
    jube_host_mir_function_finish,
    jube_host_execution_activate,
    jube_host_execution_activate_import,
    jube_host_execution_run_main,
    jube_host_execution_finish_guest,
    jube_host_execution_frame_runtime_slot,
    jube_host_mir_item_function_create,
    jube_host_mir_function_forward_create,
    jube_host_mir_item_function_proto_create,
    jube_host_mir_function_register_lookup,
    jube_host_execution_run_main_into,
    jube_host_mir_item_function_create_typed,
    jube_host_mir_item_function_proto_create_typed,
    jube_host_mir_function_frame_runtime_load,
    jube_host_mir_function_register_create,
    jube_host_mir_label_create,
    jube_host_mir_instruction_emit,
    jube_host_mir_label_emit,
    jube_host_mir_runtime_import_call_emit,
    jube_host_mir_local_direct_call_emit,
    jube_host_mir_item_return_emit,
    jube_host_mir_function_frame_finalize,
    jube_host_mir_debug_dump_if_enabled,
    jube_host_mir_frame_root_candidate_note,
    jube_host_mir_function_frame_begin,
    jube_host_mir_function_frame_scalar_return_home_set,
    jube_host_mir_compiler_import_cache_init,
    jube_host_mir_compiler_import_cache_destroy,
    jube_host_mir_local_direct_call_prototype_get_or_create,
    jube_host_mir_function_state_suspend,
    jube_host_mir_function_select,
    jube_host_mir_function_state_restore,
    jube_host_mir_function_register_lookup_current,
    jube_host_mir_function_register_create_current,
    jube_host_mir_label_create_current,
    jube_host_mir_instruction_emit_current,
    jube_host_mir_label_emit_current,
    jube_host_mir_function_frame_runtime_load_current,
    jube_host_mir_function_finish_current,
    jube_host_mir_item_function_create_typed_current,
    jube_host_mir_function_forward_create_current,
    jube_host_mir_module_finalize_and_load_current,
    jube_host_mir_function_lookup_current,
    jube_host_mir_scalar_home_create_current,
    jube_host_mir_scalar_home_bind_current,
    jube_host_mir_compiler_cursor_create,
    jube_host_mir_compiler_cursor_destroy,
};

static const JubeModuleGraphAPI jube_host_module_graph_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeModuleGraphAPI),
    jube_host_module_loading_namespace,
    jube_host_load_lambda_module,
    jube_host_module_state,
    jube_host_module_begin_loading,
    jube_host_module_publish,
};

static int jube_host_runtime_catalog_register_imports(
        const JubeRuntimeImport* imports, int import_count,
        const char* owner_name) {
    if (!imports || import_count <= 0 || !owner_name || !*owner_name) return -1;
    JitImport* host_imports = (JitImport*)mem_calloc(
        (size_t)import_count, sizeof(JitImport), MEM_CAT_SYSTEM);
    if (!host_imports) return -1;
    for (int i = 0; i < import_count; i++) {
        if (!imports[i].name || !imports[i].function) {
            mem_free(host_imports);
            return -1;
        }
        host_imports[i].name = imports[i].name;
        host_imports[i].func = imports[i].function;
        // Zero-initialized metadata deliberately preserves the existing
        // conservative MAY_GC/default-representation import contract.
    }
    if (!jit_register_module_imports(host_imports, import_count, owner_name)) {
        mem_free(host_imports);
        return -1;
    }
    // The resolver hashmap copies each descriptor; retaining this translation
    // buffer would leak once per accepted module activation in debug builds.
    mem_free(host_imports);
    return 0;
}

static int jube_host_runtime_catalog_lookup_import_metadata(
        const char* name, JubeRuntimeImportMetadata* out_metadata) {
    if (!name || !*name || !out_metadata) return -1;
    JitImportMetadata metadata = {};
    if (!jit_import_get_metadata(name, &metadata)) return -1;
    out_metadata->gc_effect = (uint32_t)metadata.gc_effect;
    out_metadata->reentry_effect = (uint32_t)metadata.reentry_effect;
    out_metadata->result_class = (uint32_t)metadata.ret_class;
    out_metadata->argument_classes = metadata.arg_classes;
    out_metadata->flags = metadata.flags;
    out_metadata->exception_effect = (uint32_t)metadata.exception_effect;
    out_metadata->argument_effects = metadata.arg_effects;
    return 0;
}

static const JubeRuntimeCatalogAPI jube_host_runtime_catalog_api = {
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeRuntimeCatalogAPI),
    jube_host_runtime_catalog_register_imports,
    jube_host_runtime_catalog_lookup_import_metadata,
};

static const JubeHostLangAPI jube_host_lang_api = {
    JUBE_HOST_LANG_API_VERSION,
    sizeof(JubeHostLangAPI),
    JUBE_HOST_CAP_GC_ROOTS |
        JUBE_HOST_CAP_NEUTRAL_DATA |
        JUBE_HOST_CAP_RUNTIME_CATALOG |
        JUBE_HOST_CAP_MODULE_GRAPH |
        JUBE_HOST_CAP_GUEST_EXECUTION |
        JUBE_HOSTED_LANG_CAP_SOURCE |
        JUBE_HOSTED_LANG_CAP_DIAGNOSTICS |
        JUBE_HOSTED_LANG_CAP_RESULT_FORMAT |
        JUBE_HOSTED_LANG_CAP_SESSION_MEMORY |
        JUBE_HOSTED_LANG_CAP_EXECUTION |
        JUBE_HOSTED_LANG_CAP_MODULE_GRAPH,
    JUBE_HOST_BUILD_ID,
    &jube_host_source_api,
    &jube_host_diagnostic_api,
    &jube_host_output_api,
    &jube_host_session_memory_api,
    &jube_host_execution_api,
    &jube_host_module_graph_api,
    &jube_host_root_api,
};

static JubeHostAPI jube_host_api = {
    JUBE_HOST_API_VERSION,
    sizeof(JubeHostAPI),
    JUBE_HOST_CAP_GC_ROOTS |
        JUBE_HOST_CAP_NEUTRAL_DATA |
        JUBE_HOST_CAP_RUNTIME_CATALOG |
        JUBE_HOST_CAP_MODULE_GRAPH |
        JUBE_HOST_CAP_GUEST_EXECUTION |
        JUBE_HOST_CAP_NODE_RUNTIME,
    JUBE_HOST_BUILD_ID,
    &jube_host_lang_api,
    &jube_host_gc_api,
    &jube_host_value_api,
    &jube_host_script_api,
    &jube_host_dom_api,
    &jube_host_realm_api,
    &jube_host_dom_catalog,
    &jube_host_runtime_catalog_api,
    &jube_host_data_api,
    &jube_host_node_api,
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

static bool jube_module_name_equals(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static int jube_find_static_module_index(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (module && jube_module_name_equals(module->name, name)) return i;
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
    if (length > 3 && memcmp(cursor + length - 3, ".js", 3) == 0) length -= 3;
    if (length == 0 || length >= out_size) return false;
    memcpy(out, cursor, length);
    out[length] = '\0';
    return true;
}

static uint64_t jube_specifier_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JubeSpecifierEntry* entry = (const JubeSpecifierEntry*)item;
    return hashmap_sip(entry->normalized, strlen(entry->normalized), seed0, seed1);
}

static int jube_specifier_entry_compare(const void* left, const void* right, void* user) {
    (void)user;
    const JubeSpecifierEntry* a = (const JubeSpecifierEntry*)left;
    const JubeSpecifierEntry* b = (const JubeSpecifierEntry*)right;
    return strcmp(a->normalized, b->normalized);
}

static bool jube_specifier_index_init(void) {
    if (jube_specifier_index) return true;
    jube_specifier_index = hashmap_new(sizeof(JubeSpecifierEntry), 64,
        0x4a5542455f535045ULL, 0x4349464945525f31ULL,
        jube_specifier_entry_hash, jube_specifier_entry_compare, NULL, NULL);
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
        if (!entry.manifest_path[0] && existing->manifest_path[0]) {
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
        if (jube_load_manifest_path_internal(entry.manifest_path, NULL, entry.module_name) != 1) {
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
    while (entry->activation_state == JUBE_MODULE_ACTIVATING) {
        if (entry->activating_context == context) {
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
    entry->activating_context = context;
    jube_runtime_session_lock_release();

    if (!jube_activate_module_dependencies(module)) {
        jube_runtime_session_lock_acquire();
        entry->activation_state = JUBE_MODULE_FAILED;
        entry->activating_context = NULL;
        uv_cond_broadcast(&jube_runtime_activation_cond);
        jube_runtime_session_lock_release();
        return false;
    }
    bool shared_primitives = module->name && strcmp(module->name, "node-core") == 0;
    if (shared_primitives && jube_node_shared_primitives_init() != 0) {
        jube_runtime_session_lock_acquire();
        entry->activation_state = JUBE_MODULE_FAILED;
        entry->activating_context = NULL;
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
            entry->activating_context = NULL;
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
        entry->activating_context = NULL;
        uv_cond_broadcast(&jube_runtime_activation_cond);
        jube_runtime_session_lock_release();
        return false;
    }

    jube_runtime_session_lock_acquire();
    entry->activation_state = JUBE_MODULE_ACTIVE;
    entry->activating_context = NULL;
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
        log_debug("JUBE_REG: %s module '%s' already registered",
                  source_label ? source_label : "Jube", module->name);
        jube_close_dynamic_handle(dynamic_handle);
        return 0;
    }
    // Duplicate module registration is idempotent; validate language aliases
    // only for a descriptor that will become newly visible to the registry.
    if (jube_language_validate_registration(module) != 0) {
        jube_close_dynamic_handle(dynamic_handle);
        return -1;
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
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != ':') return false;
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != '\"') return false;
    cursor++;
    const char* end = cursor;
    while (*end && *end != '\"') {
        if (*end == '\\') return false; // manifests use literal UTF-8 paths/names
        end++;
    }
    if (*end != '\"' || (size_t)(end - cursor) >= out_size) return false;
    memcpy(out, cursor, (size_t)(end - cursor));
    out[end - cursor] = '\0';
    return true;
}

static bool jube_manifest_value_ascii_equal(const char* value, size_t value_length,
                                            const char* expected) {
    if (!value || !expected || value_length != strlen(expected)) return false;
    for (size_t i = 0; i < value_length; i++) {
        char left = value[i];
        char right = expected[i];
        if (left >= 'A' && left <= 'Z') left = (char)(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z') right = (char)(right + ('a' - 'A'));
        if (left != right) return false;
    }
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
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != ':') return false;
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != '[') return false;
    cursor++;
    while (*cursor && *cursor != ']') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n' || *cursor == ',') cursor++;
        if (*cursor != '\"') return false;
        cursor++;
        const char* end = cursor;
        while (*end && *end != '\"') {
            if (*end == '\\') return false;
            end++;
        }
        if (*end != '\"') return false;
        // Discovery precedes descriptor registration, so aliases and source
        // extensions must normalize here as well as in the loaded registry.
        if (jube_manifest_value_ascii_equal(cursor, (size_t)(end - cursor), value)) return true;
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
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != ':') return false;
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != '[') return false;
    cursor++;
    while (*cursor && *cursor != ']') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n' || *cursor == ',') cursor++;
        if (*cursor == ']') break;
        if (*cursor != '\"' || *out_count >= capacity) return false;
        cursor++;
        const char* end = cursor;
        while (*end && *end != '\"') {
            if (*end == '\\') return false;
            end++;
        }
        size_t value_length = (size_t)(end - cursor);
        if (*end != '\"' || value_length == 0 || value_length >= sizeof(values[0])) {
            return false;
        }
        memcpy(values[*out_count], cursor, value_length);
        values[*out_count][value_length] = '\0';
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
    memcpy(module_dir, manifest_path, module_dir_length);
    module_dir[module_dir_length] = '\0';
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
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor != ':') return false;
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor++;
    char* end = NULL;
    unsigned long value = strtoul(cursor, &end, 10);
    if (end == cursor || value > UINT32_MAX) return false;
    *out = (uint32_t)value;
    return true;
}

static const char* jube_selector_extension(const char* selector) {
    if (!selector) return NULL;
    const char* extension = NULL;
    for (const char* cursor = selector; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') extension = NULL;
        else if (*cursor == '.') extension = cursor;
    }
    return extension && extension[1] ? extension : NULL;
}

static bool jube_manifest_matches_selector(const char* text, const char* selector) {
    if (!text || !selector || !*selector) return false;
    const char* extension = jube_selector_extension(selector);
    if (extension) return jube_manifest_array_contains(text, "extensions", extension);
    char language[128];
    if (jube_manifest_string(text, "language", language, sizeof(language)) &&
        jube_manifest_value_ascii_equal(language, strlen(language), selector)) return true;
    return jube_manifest_array_contains(text, "aliases", selector);
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

// Returns 1 when this manifest was loaded, 0 when it does not match the
// requested selector, and -1 when a selected manifest or dependency failed.
static int jube_load_manifest_path_internal(const char* manifest_path, const char* selector,
                                            const char* expected_name) {
    char* text = NULL;
    // Roots may contain only per-module subdirectories; a missing direct
    // module.json is not a selected-manifest failure and must not mask them.
    if (!jube_manifest_read_file(manifest_path, &text)) return 0;
    bool selected = selector ? jube_manifest_matches_selector(text, selector) :
        jube_manifest_name_matches(text, expected_name);
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
    char manifest_build_id[128];
    char manifest_digest[65];
    uint32_t base_abi = 0;
    uint32_t hosted_api = 0;
    bool has_library = jube_manifest_string(text, jube_manifest_library_key(), library,
                                            sizeof(library));
    bool has_entry = jube_manifest_string(text, "entry_symbol", entry, sizeof(entry));
    bool has_build_id = jube_manifest_string(text, "host_build_id", manifest_build_id,
                                              sizeof(manifest_build_id));
    bool has_digest = jube_manifest_string(text, jube_manifest_integrity_key(), manifest_digest,
                                           sizeof(manifest_digest));
    bool valid = has_library &&
        jube_manifest_string(text, "name", module_name, sizeof(module_name)) &&
        jube_manifest_string(text, "version", module_version, sizeof(module_version)) &&
        jube_manifest_uint32(text, "base_abi_version", &base_abi) &&
        jube_manifest_uint32(text, "hosted_api_version", &hosted_api) &&
        base_abi == JUBE_ABI_VERSION && hosted_api == JUBE_HOST_LANG_API_VERSION &&
        // Build-local manifest fields are optional. When supplied by an
        // explicit distribution or negative-test fixture, they still constrain loading.
        (!has_build_id || strcmp(manifest_build_id, JUBE_HOST_BUILD_ID) == 0);
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
                jube_load_manifest_path_internal(dependency_manifest, NULL, dependency_name) != 1) {
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

static bool jube_load_manifest_path(const char* manifest_path, const char* selector) {
    return jube_load_manifest_path_internal(manifest_path, selector, NULL) != 0;
}

static bool jube_manifest_path_matches_selector(const char* manifest_path,
                                                 const char* selector) {
    char* text = NULL;
    if (!jube_manifest_read_file(manifest_path, &text)) return false;
    bool selected = jube_manifest_matches_selector(text, selector);
    free(text);
    return selected;
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
    if (ok && provide_count > 0) {
        ok = jube_manifest_string(text, "kind", kind, sizeof(kind)) &&
            jube_manifest_string(text, "engine", engine, sizeof(engine)) &&
            jube_manifest_value_ascii_equal(kind, strlen(kind), "runtime-library") &&
            jube_manifest_value_ascii_equal(engine, strlen(engine), "js");
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
                memcpy(root, cursor, length);
                root[length] = '\0';
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

static bool jube_scan_manifest_root(const char* root, const char* selector) {
    if (!root || !*root) return false;
    char manifest_path[1024];
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/module.json", root) > 0 &&
        jube_load_manifest_path(manifest_path, selector)) return true;

    char selected_manifest_path[1024] = {};
#if defined(_WIN32)
    char search_path[1024];
    int search_written = snprintf(search_path, sizeof(search_path), "%s\\*", root);
    if (search_written <= 0 || (size_t)search_written >= sizeof(search_path)) return false;
    WIN32_FIND_DATAA entry;
    HANDLE find_handle = FindFirstFileA(search_path, &entry);
    if (find_handle == INVALID_HANDLE_VALUE) return false;
    do {
        if (entry.cFileName[0] == '.' || !(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        int written = snprintf(manifest_path, sizeof(manifest_path), "%s/%s/module.json",
                               root, entry.cFileName);
        if (written <= 0 || (size_t)written >= sizeof(manifest_path)) continue;
        if (jube_manifest_path_matches_selector(manifest_path, selector) &&
            (!selected_manifest_path[0] || strcmp(manifest_path, selected_manifest_path) < 0)) {
            strcpy(selected_manifest_path, manifest_path);
        }
    } while (FindNextFileA(find_handle, &entry));
    FindClose(find_handle);
#else
    DIR* dir = opendir(root);
    if (!dir) return false;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        int written = snprintf(manifest_path, sizeof(manifest_path), "%s/%s/module.json",
                               root, entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(manifest_path)) continue;
        if (jube_manifest_path_matches_selector(manifest_path, selector) &&
            (!selected_manifest_path[0] || strcmp(manifest_path, selected_manifest_path) < 0)) {
            strcpy(selected_manifest_path, manifest_path);
        }
    }
    closedir(dir);
#endif
    // Directory enumeration order is unspecified.  A language root may carry
    // duplicate aliases while it is being assembled, so choose the stable
    // lexicographically first matching manifest instead of leaking that order
    // into language dispatch.
    return selected_manifest_path[0] &&
        jube_load_manifest_path(selected_manifest_path, selector);
}

bool jube_discover_hosted_language(const char* selector) {
    if (!selector || !*selector || jube_find_language(selector) ||
        jube_find_language_for_path(selector)) return true;
    // A non-core source extension is a hosted-language request even when no
    // bundle is installed, so the CLI can report a missing module instead of
    // incorrectly parsing that source as Lambda.
    bool requested_source_extension = jube_selector_extension(selector) != NULL;
    if (jube_manifest_paths_scanned) return requested_source_extension;
    jube_manifest_paths_scanned = true;
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
            char root[1024];
            if (length > 0 && length < sizeof(root)) {
                memcpy(root, cursor, length);
                root[length] = '\0';
                if (jube_scan_manifest_root(root, selector)) return true;
            }
            if (!end) break;
            cursor = end + 1;
        }
    }
    if (jube_host_module_root[0] && jube_scan_manifest_root(jube_host_module_root, selector)) {
        return true;
    }
    return jube_scan_manifest_root("modules", selector) || requested_source_extension;
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
                memcpy(root, cursor, length);
                root[length] = '\0';
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
    if (!session || !session->resource_slots) return;
    for (int i = 0; i < session->resource_slots->length; i++) {
        JubeNodeResourceSlot* slot = (JubeNodeResourceSlot*)session->resource_slots->data[i];
        if (!slot) continue;
        if (slot->resource) {
            // A libuv handle can outlive its JS wrapper until its close turn;
            // close the host payload before releasing the rooted JS owner.
            if (slot->resource->close_callback) {
                slot->resource->close_callback(slot->resource->close_user);
            }
            heap_unregister_gc_root(&slot->resource->value.item);
            mem_free(slot->resource);
        }
        mem_free(slot);
    }
    arraylist_free(session->resource_slots);
    session->resource_slots = NULL;
}

static JubeNodeResourceSlot* jube_node_resource_slot(NodeRuntimeSession* session,
        uint32_t resource_id) {
    if (!session || !session->resource_slots || resource_id == 0) return NULL;
    uint32_t index = resource_id & JUBE_NODE_RESOURCE_INDEX_MASK;
    uint16_t generation = (uint16_t)(resource_id >> JUBE_NODE_RESOURCE_INDEX_BITS);
    if (index == 0 || generation == 0 || index > (uint32_t)session->resource_slots->length) {
        return NULL;
    }
    JubeNodeResourceSlot* slot = (JubeNodeResourceSlot*)
        session->resource_slots->data[index - 1];
    return slot && slot->generation == generation && slot->resource ? slot : NULL;
}

uint32_t jube_node_resource_add_with_close(void* session_handle, Item value, const char* kind,
        JubeNodeResourceCloseCallback close_callback, void* close_user) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (!session || session != jube_active_node_runtime_session || !session->live ||
            session->detaching || !value.item || !kind) return 0;
    if (!session->resource_slots) {
        session->resource_slots = arraylist_new(8);
        if (!session->resource_slots) return 0;
    }
    for (int i = 0; i < session->resource_slots->length; i++) {
        JubeNodeResourceSlot* slot = (JubeNodeResourceSlot*)session->resource_slots->data[i];
        JubeNodeResource* resource = slot ? slot->resource : NULL;
        if (resource && resource->value.item == value.item) {
            // A new native owner for an already rooted JS handle would leak:
            // the existing slot must remain that handle's sole close authority.
            return close_callback ? 0 : resource->id;
        }
    }
    JubeNodeResourceSlot* slot = NULL;
    int slot_index = -1;
    for (int i = 0; i < session->resource_slots->length; i++) {
        JubeNodeResourceSlot* candidate = (JubeNodeResourceSlot*)session->resource_slots->data[i];
        if (candidate && !candidate->resource) {
            slot = candidate;
            slot_index = i;
            break;
        }
    }
    if (!slot) {
        if (session->resource_slots->length >= (int)JUBE_NODE_RESOURCE_INDEX_MASK) return 0;
        slot = (JubeNodeResourceSlot*)mem_calloc(1, sizeof(JubeNodeResourceSlot), MEM_CAT_SYSTEM);
        if (!slot || !arraylist_append(session->resource_slots, slot)) {
            mem_free(slot);
            return 0;
        }
        slot_index = session->resource_slots->length - 1;
    }
    slot->generation++;
    if (slot->generation == 0) slot->generation++;
    JubeNodeResource* resource = (JubeNodeResource*)mem_calloc(1, sizeof(JubeNodeResource),
        MEM_CAT_SYSTEM);
    if (!resource) return 0;
    resource->id = ((uint32_t)slot->generation << JUBE_NODE_RESOURCE_INDEX_BITS) |
        (uint32_t)(slot_index + 1);
    resource->value = value;
    resource->kind = kind;
    resource->close_callback = close_callback;
    resource->close_user = close_user;
    // Resource slots own the JS edge until close removes it; net callbacks can
    // otherwise outlive a compacting collection between libuv turns.
    heap_register_gc_root(&resource->value.item);
    slot->resource = resource;
    return resource->id;
}

uint32_t jube_node_resource_add(Item value, const char* kind) {
    return jube_node_resource_add_with_close(jube_active_node_runtime_session, value, kind, NULL, NULL);
}

void jube_node_resource_remove_for_session(void* session_handle, uint32_t resource_id) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (session != jube_active_node_runtime_session) return;
    JubeNodeResourceSlot* slot = jube_node_resource_slot(session, resource_id);
    if (!slot) return;
    if (slot->resource->close_callback) {
        slot->resource->close_callback(slot->resource->close_user);
    }
    heap_unregister_gc_root(&slot->resource->value.item);
    mem_free(slot->resource);
    slot->resource = NULL;
}

void jube_node_resource_remove(uint32_t resource_id) {
    jube_node_resource_remove_for_session(jube_active_node_runtime_session, resource_id);
}

void* jube_node_resource_user_data_for_session(void* session_handle, uint32_t resource_id) {
    NodeRuntimeSession* session = (NodeRuntimeSession*)session_handle;
    if (session != jube_active_node_runtime_session) return NULL;
    JubeNodeResourceSlot* slot = jube_node_resource_slot(session, resource_id);
    return slot ? slot->resource->close_user : NULL;
}

void* jube_node_runtime_current_session(void) {
    return jube_active_node_runtime_session && jube_active_node_runtime_session->live &&
            !jube_active_node_runtime_session->detaching ? jube_active_node_runtime_session : NULL;
}

NodeTraceState* jube_node_trace_state(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!node_session || node_session != jube_active_node_runtime_session ||
            !node_session->live) return NULL;
    return &node_session->trace;
}

JsCjsState* jube_node_cjs_state(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!node_session || node_session != jube_active_node_runtime_session ||
            !node_session->live) return NULL;
    return &node_session->cjs;
}

JsCommonJsCompileCacheState* jube_node_commonjs_compile_cache_state(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!node_session || node_session != jube_active_node_runtime_session ||
            !node_session->live) return NULL;
    return &node_session->commonjs_compile_cache;
}

JsDiagnosticsChannelState* jube_node_diagnostics_channel_state(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!node_session || node_session != jube_active_node_runtime_session ||
            !node_session->live) return NULL;
    return &node_session->diagnostics_channels;
}

JsPermissionPolicy* jube_node_permission_policy(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!node_session || node_session != jube_active_node_runtime_session ||
            !node_session->live) return NULL;
    return &node_session->permission_policy;
}

JsCryptoNativeState* jube_node_crypto_native_state(void* session) {
    NodeRuntimeSession* node_session = (NodeRuntimeSession*)session;
    if (!node_session || node_session != jube_active_node_runtime_session ||
            !node_session->live) return NULL;
    return &node_session->crypto_native;
}

void jube_node_resource_clear(void) {
    jube_node_resource_cleanup(jube_active_node_runtime_session);
}

bool jube_node_resource_contains(uint32_t resource_id) {
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    return jube_node_resource_slot(session, resource_id) != NULL;
}

Item jube_node_resource_active_handles(void) {
    Item handles = js_array_new(0);
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    if (!session || !session->resource_slots) return handles;
    for (int i = 0; i < session->resource_slots->length; i++) {
        JubeNodeResourceSlot* slot = (JubeNodeResourceSlot*)session->resource_slots->data[i];
        JubeNodeResource* resource = slot ? slot->resource : NULL;
        if (resource) js_array_push(handles, resource->value);
    }
    return handles;
}

Item jube_node_resource_active_resources_info(void) {
    Item resources = js_array_new(0);
    NodeRuntimeSession* session = jube_active_node_runtime_session;
    if (!session || !session->resource_slots) return resources;
    for (int i = 0; i < session->resource_slots->length; i++) {
        JubeNodeResourceSlot* slot = (JubeNodeResourceSlot*)session->resource_slots->data[i];
        JubeNodeResource* resource = slot ? slot->resource : NULL;
        if (!resource) continue;
        js_array_push(resources, js_make_string_len(resource->kind, (int)strlen(resource->kind)));
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
    jube_active_node_runtime_session_set(session);

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
    node_trace_events_runtime_detach(session);
    jube_node_async_cancel_session(session);
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
    // Retired cursor slots are process-lifetime metadata, but must be released
    // before memtrack shutdown after their emitters have been destroyed.
    jube_host_mir_cursor_slots_cleanup();
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
            if (session->async_work) {
                if (session->async_work->length != 0) {
                    log_error("JUBE_ASYNC: %d work requests survived registry cleanup",
                        session->async_work->length);
                } else {
                    arraylist_free(session->async_work);
                }
            }
            mem_free(session);
        }
        arraylist_free(jube_node_runtime_sessions);
        jube_node_runtime_sessions = NULL;
    }
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

const JubeTypeDef* jube_find_type_by_host_type(const void* host_type) {
    if (!host_type) return NULL;
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (!module || !module->types || module->type_count <= 0) continue;
        for (int j = 0; j < module->type_count; j++) {
            const JubeTypeDef* type = &module->types[j];
            if ((const void*)type == host_type) {
                return jube_activate_module_descriptor(module) ? type : NULL;
            }
        }
    }
    return NULL;
}
