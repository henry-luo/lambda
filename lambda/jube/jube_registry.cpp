#include "jube_registry.h"
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
#include "../runtime/lambda-stack.h"
#include "../runtime/side_stack.h"
#include "../../lib/file.h"
#include "../../lib/digest.h"
#include "../../lib/hex.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/strbuf.h"
#include "../../lib/mem_factory.h"
#include "../../lib/arraylist.h"
#include "../runtime/gc/gc_heap.h"
#include <string.h>
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
#define JUBE_GUEST_EXECUTION_MAGIC 0x4A474558u
#define JUBE_MIR_CURSOR_INDEX_BITS 16u
#define JUBE_MIR_CURSOR_INDEX_MASK ((uintptr_t)((1u << JUBE_MIR_CURSOR_INDEX_BITS) - 1u))

typedef struct JubeStaticModuleEntry {
    const JubeModuleDef* module;
    bool initialized;
    void* dynamic_handle;
} JubeStaticModuleEntry;

static JubeStaticModuleEntry jube_static_modules[JUBE_STATIC_MODULE_CAPACITY];
static int jube_static_modules_count = 0;
static bool jube_dynamic_modules_from_env_loaded = false;
static bool jube_manifest_paths_scanned = false;
static char jube_host_module_root[1024];
static char jube_manifest_loading_paths[JUBE_MANIFEST_DEPENDENCY_CAPACITY][JUBE_MANIFEST_PATH_CAPACITY];
static int jube_manifest_loading_depth = 0;

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
extern "C" Context* _lambda_rt;
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);
extern "C" void heap_register_gc_weak(uint64_t* slot,
    JubeGcWeakClearFn on_clear, void* context);
extern "C" void heap_unregister_gc_weak(uint64_t* slot);
extern "C" void* import_resolver(const char* name);

static bool jube_host_root_frame_begin(LambdaRootFrame* frame,
        size_t slot_count) {
    return lambda_root_frame_begin((Context*)context, frame, slot_count);
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
    return lambda_root_frame_begin((Context*)context, host_frame, slot_count);
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
extern "C" Item vmap_new(void);
extern "C" Item js_new_object(void);
extern "C" Item js_array_new(int capacity);
extern "C" Item js_array_push(Item array, Item value);
extern "C" int64_t js_array_length(Item array);
extern "C" Item js_array_get_int(Item array, int64_t index);
extern "C" Item js_property_get(Item object, Item key);
extern "C" Item js_property_set(Item object, Item key, Item value);
extern "C" Item js_new_function(void* func_ptr, int param_count);
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" void js_set_function_name(Item fn_item, Item name_item);
extern "C" void js_mark_non_enumerable(Item object, Item name);
extern "C" Item js_get_global_this(void);
extern "C" Item js_get_global_property(Item key);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" void js_throw_value(Item error);
extern "C" Item js_reflect_own_keys(Item obj);
extern "C" Item js_reflect_delete_property(Item obj, Item key);
extern "C" Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
extern "C" int js_check_exception(void);
extern "C" bool js_is_truthy(Item value);
extern "C" Item js_get_intrinsic_prototype_for_class(int class_id);
Item js_make_number(double value);
double js_get_number(Item value);
extern "C" Item js_date_new_from(Item value);
extern "C" Item js_date_method(Item date, int method_id);
extern "C" Item js_to_string(Item value);

static int jube_script_class_id(Item value) {
    return (int)js_class_id(value);
}
extern "C" void* js_dom_get_document(void);
extern "C" void* js_dom_get_ui_context(void);
extern "C" bool js_dom_has_committed_geometry_snapshot(void* doc);
extern "C" Item js_get_document_object_value(void);
extern "C" void* js_dom_get_or_create_doc_node(void* doc);
extern "C" Item js_dom_document_proxy_for_doc_bridge(void* doc);
extern "C" void* js_dom_unwrap_element_impl(Item item);
extern "C" void js_dom_initialize_node_wrapper(void* dom_elem);
extern "C" bool js_is_css_namespace(Item item);
extern "C" bool js_is_inline_style_item(Item item);
extern "C" bool js_is_computed_style_item(Item item);
extern "C" bool js_is_stylesheet(Item item);
extern "C" bool js_is_css_rule(Item item);
extern "C" bool js_is_rule_style_decl(Item item);
extern "C" Item js_dom_get_property_impl(Item elem_item, Item prop_name);
extern "C" Item js_dom_set_property_impl(Item elem_item, Item prop_name, Item value);
extern "C" Item js_dom_element_method_impl(Item elem_item, Item method_name, Item* args, int argc);
extern "C" Item js_computed_style_get_property(Item style_item, Item prop_name);
extern "C" Item js_dom_get_prototype_value(Item obj);
extern "C" Item js_cssom_rule_decl_get_property(Item decl_item, Item prop_name);
extern "C" Item js_cssom_rule_decl_set_property(Item decl_item, Item prop_name, Item value);
extern "C" void* js_get_foreign_doc(Item item);
extern "C" void* js_dom_swap_active_document(void* new_doc);
extern "C" void js_dom_restore_active_document(void* prev_doc);
extern "C" Item js_document_proxy_get_property(Item prop_name);
extern "C" Item js_document_proxy_set_property(Item prop_name, Item value);
extern "C" Item js_document_proxy_method(Item method_name, Item* args, int argc);
extern "C" bool js_dom_item_is_range(Item item);
extern "C" bool js_dom_item_is_selection(Item item);
extern "C" Item js_dom_range_get_prototype_value(void);
extern "C" Item js_dom_selection_get_prototype_value(void);
extern "C" bool js_dom_expando_has_property(Item obj, Item key);
extern "C" Item js_dom_expando_get_own_property_descriptor(Item obj, Item key);
extern "C" Item js_dom_expando_delete_property(Item obj, Item key);
extern "C" Item js_dom_expando_own_property_names(Item obj);
extern "C" Item js_css_namespace_method(Item obj, Item method_name, Item* args, int argc);
extern "C" Item js_dom_owner_document_for_node(void* node);
extern "C" const char* js_dom_to_attribute_cstr(Item value);
extern "C" void js_dom_after_set_attribute(void* elem, const char* attr_name, const char* attr_value);
extern "C" void js_dom_after_remove_attribute(void* elem, const char* attr_name);
extern "C" void js_dom_after_toggle_attribute_remove(void* elem, const char* attr_name);
extern "C" void js_dom_after_disabled_attribute_set(void* elem);
extern "C" void js_dom_after_default_checked_set(void* elem, bool checked);
extern "C" void js_dom_after_default_selected_set(void* elem, bool selected);
extern "C" void js_dom_after_select_multiple_removed(void* elem);
extern "C" void js_dom_set_checked_dirty(void* elem, bool checked);
extern "C" void js_dom_select_set_value_bridge(void* elem, const char* value);
extern "C" void js_dom_select_set_selected_index_bridge(void* elem, Item value);
extern "C" void js_dom_select_set_length_bridge(void* elem, Item value);
extern "C" void js_dom_set_option_selected_dirty(void* elem, bool selected);
extern "C" void js_dom_set_option_text_bridge(void* elem, const char* value);
extern "C" void js_dom_after_srcdoc_set(void* elem);
extern "C" void js_dom_throw_contenteditable_syntax_error(void);
extern "C" Item js_dom_set_text_data_property(void* text, Item value);
extern "C" Item js_dom_text_control_set_value_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_selection_start_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_selection_end_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_selection_direction_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_default_value_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_selection_range_bridge(void* elem, Item start, Item end, Item dir);
extern "C" Item js_dom_text_control_set_range_text_bridge(void* elem, Item replacement, Item start,
                                                          Item end, Item mode);
extern "C" Item js_dom_text_control_select_bridge(void* elem);
extern "C" Item js_dom_form_reset_bridge(Item form_item);
extern "C" Item js_dom_check_validity_bridge(Item elem_item);
extern "C" Item js_dom_report_validity_bridge(Item elem_item);
extern "C" Item js_dom_form_submit_bridge(Item form_item);
extern "C" Item js_dom_form_request_submit_bridge(Item form_item, Item submitter);
extern "C" Item js_dom_focus_method_bridge(void* elem, bool focus);
extern "C" Item js_dom_click_method_bridge(Item elem_item);
extern "C" Item js_dom_add_event_listener_bridge(Item target_item, Item type, Item callback, Item opts);
extern "C" Item js_dom_remove_event_listener_bridge(Item target_item, Item type, Item callback, Item opts);
extern "C" Item js_dom_dispatch_event_bridge(Item target_item, Item event_item);
extern "C" Item js_dom_get_bounding_client_rect_bridge(void* elem);
extern "C" Item js_dom_get_client_rects_bridge(void* elem);
extern "C" Item js_dom_scroll_into_view_bridge(void* elem);
extern "C" Item js_dom_scroll_method_bridge(Item elem_item, Item method_name, Item* args, int argc);
extern "C" Item js_dom_text_control_caret_bounds_bridge(void* elem);
extern "C" Item js_dom_text_control_boundary_from_point_bridge(void* elem, Item x, Item y);
extern "C" Item js_dom_boundary_from_point_bridge(void* elem, Item x, Item y, Item behavior);
extern "C" Item js_dom_style_set_property_bridge(void* elem, Item prop, Item value,
                                                 Item priority, bool has_priority);
extern "C" Item js_dom_style_remove_property_bridge(void* elem, Item prop);
extern "C" Item js_dom_text_replace_data_bridge(void* text, Item offset, Item count, Item data);
extern "C" Item js_dom_text_insert_data_bridge(void* text, Item offset, Item data);
extern "C" Item js_dom_text_append_data_bridge(void* text, Item data);
extern "C" Item js_dom_text_delete_data_bridge(void* text, Item offset, Item count);
extern "C" Item js_dom_text_substring_data_bridge(void* text, Item offset, Item count);
extern "C" Item js_dom_append_child_bridge(void* parent, Item child);
extern "C" Item js_dom_remove_child_bridge(void* parent, Item child);
extern "C" Item js_dom_insert_before_bridge(void* parent, Item new_child, Item ref_child);
extern "C" Item js_dom_remove_bridge(void* node);
extern "C" Item js_dom_adopt_node_bridge(Item node);
extern "C" Item js_dom_location_method_bridge(void* doc, Item method_name, Item* args, int argc);
extern "C" Item js_dom_document_open_bridge(void* doc);
extern "C" Item js_dom_document_write_bridge(void* doc, Item text);
extern "C" Item js_dom_document_element_from_point_bridge(void* doc, Item x, Item y);
extern "C" Item js_dom_create_range(void);
extern "C" Item js_dom_get_selection(void);
extern "C" Item js_dom_get_selection_function_for_document(void* doc);
extern "C" bool js_doc_has_browsing_context(void* doc);
extern "C" Item js_dom_document_fonts_bridge(void);
extern "C" Item js_dom_document_stylesheets_bridge(void);
extern "C" Item js_dom_document_default_view_bridge(void* doc);
extern "C" Item js_dom_document_implementation_bridge(void);
extern "C" Item js_dom_document_design_mode_bridge(void);
extern "C" Item js_dom_document_active_element_bridge(void* doc);
extern "C" Item js_dom_normalize_bridge(void* elem);
extern "C" Item js_dom_live_child_collection_bridge(void* elem, bool elements_only);
extern "C" Item js_dom_live_document_forms_bridge(void* doc);
extern "C" Item js_dom_live_form_elements_bridge(void* elem);
extern "C" Item js_dom_live_document_get_elements_by_tag_name_bridge(void* doc, Item query);
extern "C" Item js_dom_live_document_get_elements_by_class_name_bridge(void* doc, Item query);
extern "C" Item js_dom_live_document_get_elements_by_name_bridge(void* doc, Item query);
extern "C" Item js_dom_live_element_get_elements_by_tag_name_bridge(void* elem, Item query);
extern "C" Item js_dom_live_element_get_elements_by_class_name_bridge(void* elem, Item query);
extern "C" Item js_dom_clone_node_bridge(void* elem, Item deep, bool has_deep);
extern "C" Item js_dom_replace_child_bridge(void* parent, Item new_child, Item old_child);
extern "C" Item js_dom_replace_with_bridge(void* node, Item* args, int argc);
extern "C" Item js_dom_insert_adjacent_element_bridge(void* elem, Item position, Item new_node);
extern "C" Item js_dom_insert_adjacent_html_bridge(void* elem, Item position, Item html);
extern "C" Item js_dom_append_variadic_bridge(void* elem, Item* args, int argc);
extern "C" Item js_dom_prepend_variadic_bridge(void* elem, Item* args, int argc);
// DOM3 Phase 2: receiver-explicit CSSOM behavior entries
extern "C" Item js_cssom_stylesheet_get_css_rules(Item sheet);
extern "C" Item js_cssom_stylesheet_get_length(Item sheet);
extern "C" Item js_cssom_stylesheet_get_disabled(Item sheet);
extern "C" Item js_cssom_stylesheet_get_type(Item sheet);
extern "C" Item js_cssom_stylesheet_get_href(Item sheet);
extern "C" Item js_cssom_stylesheet_get_title(Item sheet);
extern "C" Item js_cssom_stylesheet_index(Item sheet, int64_t index);
extern "C" Item js_cssom_insert_rule(Item sheet, Item text, Item index);
extern "C" Item js_cssom_delete_rule(Item sheet, Item index);
extern "C" Item js_cssom_rule_get_selector_text(Item rule);
extern "C" Item js_cssom_rule_set_selector_text(Item rule, Item value);
extern "C" Item js_cssom_rule_get_style(Item rule);
extern "C" Item js_cssom_rule_get_css_rules(Item rule);
extern "C" Item js_cssom_rule_get_css_text(Item rule);
extern "C" Item js_cssom_rule_get_type(Item rule);
extern "C" Item js_cssom_rule_get_parent_rule(Item rule);
extern "C" Item js_cssom_rule_decl_remove_property(Item decl, Item prop);
extern "C" Item js_cssom_decl_css_has(Item decl, Item prop);
// DOM3 Phase 3: style-host behavior entries
extern "C" Item js_dom_get_style_property(Item elem_item, Item prop_name);
extern "C" Item js_dom_set_style_property(Item elem_item, Item prop_name, Item value);
extern "C" Item js_style_css_has(Item style_item, Item prop_name);
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
extern "C" void js_dom_notify_mutation(DomJsMutationKind kind, void* target, void* parent);
extern "C" void js_dom_notify_mutation_detail(DomJsMutationKind kind, void* target,
                                                void* parent, const char* attribute_name,
                                                const char* old_value);

static void jube_host_dom_notify_mutation(int kind, void* target, void* parent) {
    js_dom_notify_mutation((DomJsMutationKind)kind, target, parent);
}

static void jube_host_dom_notify_mutation_detail(int kind, void* target, void* parent,
                                                  const char* attribute_name,
                                                  const char* old_value) {
    js_dom_notify_mutation_detail((DomJsMutationKind)kind, target, parent,
                                  attribute_name, old_value);
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
};

static const JubeHostValueAPI jube_host_value_api = {
    vmap_new,
    js_new_object,
    js_array_new,
    js_array_push,
    js_array_length,
    js_array_get_int,
    js_property_get,
    js_property_set,
};

static const JubeHostScriptAPI jube_host_script_api = {
    js_new_function,
    js_function_set_prototype,
    js_set_function_name,
    js_mark_non_enumerable,
    js_get_global_this,
    js_get_global_property,
    js_new_error_with_name,
    js_throw_value,
    js_reflect_own_keys,
    js_reflect_delete_property,
    js_call_function,
    js_check_exception,
    js_is_truthy,
    js_get_intrinsic_prototype_for_class,
    js_make_number,
    js_get_number,
    js_date_new_from,
    js_date_method,
    jube_script_class_id,
    js_to_string,
};

static const JubeHostDomAPI jube_host_dom_api = {
    js_dom_get_document,
    js_get_document_object_value,
    js_dom_get_or_create_doc_node,
    js_dom_document_proxy_for_doc_bridge,
    js_dom_unwrap_element_impl,
    js_dom_initialize_node_wrapper,
    js_is_css_namespace,
    js_is_inline_style_item,
    js_is_computed_style_item,
    js_is_stylesheet,
    js_is_css_rule,
    js_is_rule_style_decl,
    js_dom_get_property_impl,
    js_dom_set_property_impl,
    js_dom_element_method_impl,
    js_computed_style_get_property,
    NULL,  // js_dom_style_resource_has_property retired: style hosts are record-driven (DOM3)
    NULL,  // js_dom_style_method retired: style hosts are record-driven (DOM3)
    js_dom_get_prototype_value,
    NULL,  // js_cssom_resource_has_property retired: CSSOM types are record-driven (DOM3)
    NULL,  // js_cssom_stylesheet_get_property retired: CSSOM types are record-driven (DOM3)
    NULL,  // js_cssom_rule_get_property retired: CSSOM types are record-driven (DOM3)
    NULL,  // js_cssom_rule_set_property retired: CSSOM types are record-driven (DOM3)
    js_cssom_rule_decl_get_property,
    js_cssom_rule_decl_set_property,
    js_get_foreign_doc,
    js_dom_swap_active_document,
    js_dom_restore_active_document,
    js_document_proxy_get_property,
    js_document_proxy_set_property,
    js_document_proxy_method,
    js_dom_item_is_range,
    js_dom_item_is_selection,
    NULL,  // js_dom_range_get_property retired: range/selection are record-driven (DOM3)
    NULL,  // js_dom_range_set_property retired: range/selection are record-driven (DOM3)
    NULL,  // js_dom_selection_get_property retired: range/selection are record-driven (DOM3)
    NULL,  // js_dom_selection_set_property retired: range/selection are record-driven (DOM3)
    js_dom_range_get_prototype_value,
    js_dom_selection_get_prototype_value,
    NULL,  // js_dom_range_native_property retired: range/selection are record-driven (DOM3)
    NULL,  // js_dom_selection_native_property retired: range/selection are record-driven (DOM3)
    js_dom_expando_has_property,
    NULL,  // js_dom_range_expando_has_property retired: range/selection are record-driven (DOM3)
    NULL,  // js_dom_selection_expando_has_property retired: range/selection are record-driven (DOM3)
    js_dom_expando_get_own_property_descriptor,
    NULL,  // js_dom_range_expando_get_own_property_descriptor retired: range/selection are record-driven (DOM3)
    NULL,  // js_dom_selection_expando_get_own_property_descriptor retired: range/selection are record-driven (DOM3)
    js_dom_expando_delete_property,
    NULL,  // js_dom_range_expando_delete_property retired: range/selection are record-driven (DOM3)
    NULL,  // js_dom_selection_expando_delete_property retired: range/selection are record-driven (DOM3)
    js_dom_expando_own_property_names,
    NULL,  // js_dom_range_expando_own_property_names retired: range/selection are record-driven (DOM3)
    NULL,  // js_dom_selection_expando_own_property_names retired: range/selection are record-driven (DOM3)
    js_css_namespace_method,
    NULL,  // js_cssom_stylesheet_method retired: CSSOM types are record-driven (DOM3)
    NULL,  // js_cssom_rule_decl_method retired: CSSOM types are record-driven (DOM3)
    js_dom_owner_document_for_node,
    js_dom_to_attribute_cstr,
    js_dom_after_set_attribute,
    js_dom_after_remove_attribute,
    js_dom_after_toggle_attribute_remove,
    js_dom_after_disabled_attribute_set,
    js_dom_after_default_checked_set,
    js_dom_after_default_selected_set,
    js_dom_after_select_multiple_removed,
    js_dom_set_checked_dirty,
    js_dom_select_set_value_bridge,
    js_dom_select_set_selected_index_bridge,
    js_dom_select_set_length_bridge,
    js_dom_set_option_selected_dirty,
    js_dom_set_option_text_bridge,
    js_dom_after_srcdoc_set,
    js_dom_throw_contenteditable_syntax_error,
    js_dom_set_text_data_property,
    js_dom_text_control_set_value_bridge,
    js_dom_text_control_set_selection_start_bridge,
    js_dom_text_control_set_selection_end_bridge,
    js_dom_text_control_set_selection_direction_bridge,
    js_dom_text_control_set_default_value_bridge,
    js_dom_text_control_set_selection_range_bridge,
    js_dom_text_control_set_range_text_bridge,
    js_dom_text_control_select_bridge,
    js_dom_form_reset_bridge,
    js_dom_check_validity_bridge,
    js_dom_report_validity_bridge,
    js_dom_form_submit_bridge,
    js_dom_form_request_submit_bridge,
    js_dom_focus_method_bridge,
    js_dom_click_method_bridge,
    js_dom_add_event_listener_bridge,
    js_dom_remove_event_listener_bridge,
    js_dom_dispatch_event_bridge,
    js_dom_get_bounding_client_rect_bridge,
    js_dom_get_client_rects_bridge,
    js_dom_scroll_into_view_bridge,
    js_dom_scroll_method_bridge,
    js_dom_text_control_caret_bounds_bridge,
    js_dom_text_control_boundary_from_point_bridge,
    js_dom_boundary_from_point_bridge,
    js_dom_style_set_property_bridge,
    js_dom_style_remove_property_bridge,
    js_dom_text_replace_data_bridge,
    js_dom_text_insert_data_bridge,
    js_dom_text_append_data_bridge,
    js_dom_text_delete_data_bridge,
    js_dom_text_substring_data_bridge,
    js_dom_append_child_bridge,
    js_dom_remove_child_bridge,
    js_dom_insert_before_bridge,
    js_dom_remove_bridge,
    js_dom_adopt_node_bridge,
    js_dom_location_method_bridge,
    js_dom_document_open_bridge,
    js_dom_document_write_bridge,
    js_dom_document_element_from_point_bridge,
    js_dom_create_range,
    js_dom_get_selection,
    js_dom_get_selection_function_for_document,
    js_doc_has_browsing_context,
    js_dom_document_fonts_bridge,
    js_dom_document_stylesheets_bridge,
    js_dom_document_default_view_bridge,
    js_dom_document_implementation_bridge,
    js_dom_document_design_mode_bridge,
    js_dom_document_active_element_bridge,
    js_dom_normalize_bridge,
    js_dom_live_child_collection_bridge,
    js_dom_live_document_forms_bridge,
    js_dom_live_form_elements_bridge,
    js_dom_live_document_get_elements_by_tag_name_bridge,
    js_dom_live_document_get_elements_by_class_name_bridge,
    js_dom_live_document_get_elements_by_name_bridge,
    js_dom_live_element_get_elements_by_tag_name_bridge,
    js_dom_live_element_get_elements_by_class_name_bridge,
    js_dom_clone_node_bridge,
    js_dom_replace_child_bridge,
    js_dom_replace_with_bridge,
    js_dom_insert_adjacent_element_bridge,
    js_dom_insert_adjacent_html_bridge,
    js_dom_append_variadic_bridge,
    js_dom_prepend_variadic_bridge,
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
    js_dom_get_style_property,
    js_dom_set_style_property,
    js_style_css_has,
    js_cssom_stylesheet_get_css_rules,
    js_cssom_stylesheet_get_length,
    js_cssom_stylesheet_get_disabled,
    js_cssom_stylesheet_get_type,
    js_cssom_stylesheet_get_href,
    js_cssom_stylesheet_get_title,
    js_cssom_stylesheet_index,
    js_cssom_insert_rule,
    js_cssom_delete_rule,
    js_cssom_rule_get_selector_text,
    js_cssom_rule_set_selector_text,
    js_cssom_rule_get_style,
    js_cssom_rule_get_css_rules,
    js_cssom_rule_get_css_text,
    js_cssom_rule_get_type,
    js_cssom_rule_get_parent_rule,
    js_cssom_rule_decl_remove_property,
    js_cssom_decl_css_has,
    js_dom_get_ui_context,
    js_dom_has_committed_geometry_snapshot,
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
    EvalContext activation_context;
    EvalContext* previous_context;
    bool reusing_context;
    bool activation_active;
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
    if (!context || !function_item || !function || !frame_runtime_slot ||
            !out_runtime_register || !jube_active_guest_execution ||
            !jube_active_guest_execution->activation_active ||
            frame_runtime_slot != (void*)&_lambda_rt) {
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
            !function_name || !*function_name || operand_count > MIR_SHARED_MAX_CALL_ARGS ||
            (operand_count > 0 && !operands) || (!discard_result && !out_result_register)) {
        return -1;
    }
    MIR_type_t result_type = jube_host_compiler_value_type(result_kind);
    if (result_type == MIR_T_UNDEF) return -1;
    MIR_type_t argument_types[MIR_SHARED_MAX_CALL_ARGS] = {};
    MIR_op_t argument_ops[MIR_SHARED_MAX_CALL_ARGS] = {};
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
        offsetof(Context, side_root_commit_limit));
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
    // Reset before allocation so a guest cannot carry frame roots or scalar
    // homes across independently lowered functions.
    em_frame_dispose(emitter);
    MirFrameState* frame = &emitter->frame;
    frame->return_type = MIR_T_I64;
    frame->item_return = true;
    frame->scalar_return_mode = MIR_SCALAR_RETURN_DYNAMIC;
    frame->runtime = (MIR_reg_t)runtime_register;
    frame->root_base = em_new_reg(emitter, "py_root_frame", MIR_T_I64);
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
    int key_length = snprintf(key.name, sizeof(key.name), "%s", cache_key);
    if (key_length <= 0 || (size_t)key_length >= sizeof(key.name)) return -1;
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
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", cache_key);
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
        MIR_reg_t register_id) {
    jube_host_mir_note_root_candidate(jube_host_mir_cursor_emitter(compiler_cursor), register_id);
}

static int jube_host_mir_compiler_cursor_create(void* mir_context,
        void** out_compiler_cursor) {
    MIR_context_t context = jube_host_mir_context_from_handle(mir_context);
    if (!context || !out_compiler_cursor) return -1;
    MirEmitter* emitter = (MirEmitter*)mem_calloc(1, sizeof(MirEmitter), MEM_CAT_SYSTEM);
    if (!emitter) return -1;
    emitter->ctx = context;
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

    execution->previous_context = context;
    execution->reusing_context = context && context->heap;
    if (!execution->reusing_context) {
        memset(&execution->activation_context, 0, sizeof(execution->activation_context));
        context = &execution->activation_context;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, NULL);
        context->type_list = arraylist_new(64);
    }
    _lambda_rt = (Context*)context;
    execution->input = Input::create(context->pool);
    if (!execution->input) {
        mir_guest_finish_context(jube_guest_execution_runtime(execution), execution->previous_context,
                                 execution->reusing_context);
        return -1;
    }
    execution->activation_active = true;
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
    if (!execution || !execution->activation_active || !entry || !out_result) return -1;

    LambdaRecoveryCheckpoint checkpoint = lambda_recovery_checkpoint_capture((Context*)context);
#if defined(__APPLE__) || defined(__linux__)
    if (sigsetjmp(_lambda_recovery_point, 1)) {
#elif defined(_WIN32)
    if (setjmp(_lambda_recovery_point)) {
#else
    if (0) {
#endif
        _lambda_recovery_armed = 0;
        _lambda_stack_overflow_flag = false;
        lambda_recovery_checkpoint_restore(&checkpoint);
        // The recovery boundary, not the language module, owns converting a
        // JIT stack escape into the runtime's standard error result.
        lambda_stack_overflow_error("hosted-guest");
        *out_result = ItemError;
        return -1;
    }
    _lambda_recovery_armed = 1;
    *out_result = entry((Context*)context);
    _lambda_recovery_armed = 0;
    lambda_recovery_checkpoint_disarm(&checkpoint);
    return 0;
}

static int jube_host_execution_run_main_into(void* execution_context,
        void* entry_function, Item* out_result) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    JubeGuestMainIntoFn entry = (JubeGuestMainIntoFn)entry_function;
    if (!execution || !execution->activation_active || !entry || !out_result) return -1;

    LambdaRecoveryCheckpoint checkpoint = lambda_recovery_checkpoint_capture((Context*)context);
#if defined(__APPLE__) || defined(__linux__)
    if (sigsetjmp(_lambda_recovery_point, 1)) {
#elif defined(_WIN32)
    if (setjmp(_lambda_recovery_point)) {
#else
    if (0) {
#endif
        _lambda_recovery_armed = 0;
        _lambda_stack_overflow_flag = false;
        lambda_recovery_checkpoint_restore(&checkpoint);
        lambda_stack_overflow_error("hosted-guest");
        *out_result = ItemError;
        return -1;
    }
    _lambda_recovery_armed = 1;
    // The execution owns this slot until its caller has consumed the result.
    execution->result_scalar_home = 0;
    *out_result = entry((Context*)context, &execution->result_scalar_home);
    _lambda_recovery_armed = 0;
    lambda_recovery_checkpoint_disarm(&checkpoint);
    return 0;
}

static void jube_host_execution_finish_guest(void* execution_context) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    if (!execution || !execution->activation_active) return;
    // Restore nested activation state before releasing the opaque data token.
    jube_active_guest_execution = execution->previous_active_execution;
    execution->previous_active_execution = NULL;
    mir_guest_finish_context(jube_guest_execution_runtime(execution), execution->previous_context,
                             execution->reusing_context);
    execution->input = NULL;
    execution->previous_context = NULL;
    execution->reusing_context = false;
    execution->activation_active = false;
}

static void* jube_host_execution_frame_runtime_slot(void* execution_context) {
    JubeGuestExecution* execution = jube_guest_execution_from_handle(execution_context);
    if (!execution || !execution->activation_active || execution != jube_active_guest_execution) {
        return NULL;
    }
    // The slot preserves the proven load-before-frame-prologue order while
    // preventing a guest compiler from importing mutable host storage itself.
    return &_lambda_rt;
}

static Input* jube_host_data_input(void* session) {
    JubeGuestExecution* execution = jube_active_guest_execution;
    if (!execution || !execution->activation_active || !execution->input ||
        session != execution->input) {
        return NULL;
    }
    return execution->input;
}

static int jube_host_opaque_persistent_root_register(void* session, uint64_t* slot) {
    if (!jube_host_data_input(session) || !slot) return -1;
    // TLS storage is outside the side-root stack, so register its exact Item
    // slot with the active guest heap rather than exposing that heap to a module.
    heap_register_gc_root(slot);
    return 0;
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
    map_put(target, name, value, input);
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
    ModuleDescriptor* module = module_get(source_path);
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
    Script* script = load_script(runtime, source_path, NULL, true);
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
    ModuleDescriptor* module = module_get(source_path);
    if (!module) return 0;
    *out_namespace = module->namespace_obj;
    return module->initialized ? 2 : module->loading ? 1 : 0;
}

static int jube_host_module_begin_loading(void* execution_context, const char* source_path,
                                          const char* language,
                                          const JubeModuleNamespaceOps* namespace_ops) {
    if (!execution_context || !source_path || !*source_path || !language || !*language ||
        !namespace_ops) return -1;
    ModuleDescriptor* module = module_register_loading_with_namespace_ops(
        source_path, language, jube_host_namespace_ops(namespace_ops));
    return module ? 0 : -1;
}

static int jube_host_module_publish(void* execution_context, const char* source_path,
                                    const char* language, Item namespace_obj, void* mir_context,
                                    const JubeModuleNamespaceOps* namespace_ops) {
    if (!execution_context || !source_path || !*source_path || !language || !*language ||
        !namespace_ops || namespace_obj.item == ItemNull.item) return -1;
    module_register_with_namespace_ops(source_path, language, namespace_obj, mir_context,
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
        JUBE_HOST_CAP_GUEST_EXECUTION,
    JUBE_HOST_BUILD_ID,
    &jube_host_lang_api,
    &jube_host_gc_api,
    &jube_host_value_api,
    &jube_host_script_api,
    &jube_host_dom_api,
    &jube_host_runtime_catalog_api,
    &jube_host_data_api,
};

extern "C" const JubeHostAPI* jube_internal_host_api(void) {
    return &jube_host_api;
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

// release strips log_info arguments, so keep diagnostic-only helpers out of NDEBUG builds.
#if !defined(NDEBUG)
static int jube_host_ops_count(const JubeHostObjectOps* ops) {
    if (!ops) return 0;
    int count = 0;
    if (ops->get_property) count++;
    if (ops->set_property) count++;
    if (ops->call_method) count++;
    if (ops->has_property) count++;
    if (ops->delete_property) count++;
    if (ops->get_own_property_descriptor) count++;
    if (ops->own_property_keys) count++;
    if (ops->prototype) count++;
    if (ops->invalidate) count++;
    if (ops->destroy) count++;
    return count;
}

static void jube_log_module_type_ops(const JubeModuleDef* module) {
    if (!module || !module->types || module->type_count <= 0) return;
    for (int i = 0; i < module->type_count; i++) {
        const JubeTypeDef* type = &module->types[i];
        if (!type || !(type->flags & (JUBE_TYPE_NON_OWNING_HOST | JUBE_TYPE_OWNING_NATIVE))) {
            continue;
        }
        log_info("JUBE_REG: type %s.%s host_ops=%d/10",
                 module->name ? module->name : "(module)",
                 type->name ? type->name : "(type)",
                 jube_host_ops_count(type->host_ops));
    }
}
#endif

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
    if (jube_static_modules_count >= JUBE_STATIC_MODULE_CAPACITY) {
        log_error("JUBE_REG: module capacity exceeded while registering '%s'", module->name);
        jube_close_dynamic_handle(dynamic_handle);
        return -1;
    }

    int slot = jube_static_modules_count++;
    jube_static_modules[slot].module = module;
    jube_static_modules[slot].initialized = false;
    // Dynamic descriptors and function tables live in the loaded image, so keep the handle open.
    jube_static_modules[slot].dynamic_handle = dynamic_handle;

    if (module->init) {
        int rc = module->init(&jube_host_api);
        if (rc != 0) {
            log_error("JUBE_REG: %s module '%s' init failed with code %d",
                      source_label ? source_label : "Jube", module->name, rc);
            // init may allocate module-owned state before reporting failure, so
            // pair it with shutdown before the descriptor becomes unreachable.
            if (module->shutdown) module->shutdown();
            jube_static_modules_count--;
            jube_static_modules[slot].module = NULL;
            jube_static_modules[slot].initialized = false;
            jube_static_modules[slot].dynamic_handle = NULL;
            // Dynamic descriptors live in their image; never dereference one
            // after dlclose while reporting registration rollback.
            jube_close_dynamic_handle(dynamic_handle);
            return -1;
        }
    }

    // DOM3: compile the module's interface declaration against its binding
    // tables; a half-valid interface must fail registration, not limp along.
    if (jube_compile_module_interface(module) != 0) {
        log_error("JUBE_REG: %s module '%s' interface compilation failed",
                  source_label ? source_label : "Jube", module->name);
        if (module->shutdown) module->shutdown();
        jube_interface_remove_module(module);
        jube_static_modules_count--;
        jube_static_modules[slot].module = NULL;
        jube_static_modules[slot].initialized = false;
        jube_static_modules[slot].dynamic_handle = NULL;
        jube_close_dynamic_handle(dynamic_handle);
        return -1;
    }

    log_info("JUBE_REG: registered %s module '%s' version '%s'",
             source_label ? source_label : "Jube", module->name,
             module->version ? module->version : "(none)");
#if !defined(NDEBUG)
    jube_log_module_type_ops(module);
#endif
    // A descriptor without init() is still an active module. Lifecycle hooks
    // such as heap_cleanup must run for it after successful registration.
    jube_static_modules[slot].initialized = true;
    return 0;
}

static void jube_rollback_registered_modules(int first_index) {
    if (first_index < 0) first_index = 0;
    while (jube_static_modules_count > first_index) {
        int index = --jube_static_modules_count;
        JubeStaticModuleEntry entry = jube_static_modules[index];
        jube_static_modules[index].module = NULL;
        jube_static_modules[index].initialized = false;
        jube_static_modules[index].dynamic_handle = NULL;
        if (!entry.module) continue;
        // Dependency loading is transactional: remove every host-side record
        // before closing the image that owns its descriptor and callbacks.
        jube_interface_remove_module(entry.module);
        if (entry.initialized && entry.module->shutdown) entry.module->shutdown();
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
    return jube_register_module_descriptor(module, handle, "dynamic");
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
    bool valid = has_library &&
        jube_manifest_string(text, "name", module_name, sizeof(module_name)) &&
        jube_manifest_string(text, "version", module_version, sizeof(module_version)) &&
        jube_manifest_string(text, "host_build_id", manifest_build_id,
                             sizeof(manifest_build_id)) &&
        jube_manifest_string(text, jube_manifest_integrity_key(), manifest_digest,
                             sizeof(manifest_digest)) &&
        jube_manifest_uint32(text, "base_abi_version", &base_abi) &&
        jube_manifest_uint32(text, "hosted_api_version", &hosted_api) &&
        base_abi == JUBE_ABI_VERSION && hosted_api == JUBE_HOST_LANG_API_VERSION &&
        strcmp(manifest_build_id, JUBE_HOST_BUILD_ID) == 0;
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
    if (!jube_manifest_verify_library_sha256(library_path, manifest_digest)) {
        log_error("JUBE_REG: library integrity check failed for '%s'", library_path);
        free(text);
        jube_manifest_loading_depth--;
        return -1;
    }
    int transaction_start = jube_static_modules_count;
    for (int i = 0; i < dependency_count; i++) {
        const char* dependency_name = dependencies[i];
        if (strcmp(dependency_name, module_name) == 0 ||
            jube_find_static_module_index(dependency_name) >= 0) {
            if (strcmp(dependency_name, module_name) == 0) {
                log_error("JUBE_REG: manifest '%s' depends on itself", manifest_path);
                jube_rollback_registered_modules(transaction_start);
                free(text);
                jube_manifest_loading_depth--;
                return -1;
            }
            continue;
        }
        char dependency_manifest[JUBE_MANIFEST_PATH_CAPACITY];
        if (!jube_find_sibling_manifest(manifest_path, dependency_name,
                                        dependency_manifest, sizeof(dependency_manifest)) ||
            jube_load_manifest_path_internal(dependency_manifest, NULL, dependency_name) != 1 ||
            jube_find_static_module_index(dependency_name) < 0) {
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

void jube_register_builtin_modules(void) {
    radiant_jube_register_static();
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
    size_t end = offsetof(JubeModuleDef, runtime_reset) +
        sizeof(((JubeModuleDef*)NULL)->runtime_reset);
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (jube_module_has_field(module, end) && module->runtime_reset) {
            module->runtime_reset();
        }
    }
}

void jube_registry_cleanup(void) {
    // Retired cursor slots are process-lifetime metadata, but must be released
    // before memtrack shutdown after their emitters have been destroyed.
    jube_host_mir_cursor_slots_cleanup();
}

const JubeModuleDef* jube_find_static_module(const char* name) {
    int index = jube_find_static_module_index(name);
    if (index < 0) return NULL;
    return jube_static_modules[index].module;
}

void jube_notify_heap_cleanup(void* heap) {
    if (!heap) return;
    size_t field_end = offsetof(JubeModuleDef, heap_cleanup) +
        sizeof(((JubeModuleDef*)0)->heap_cleanup);
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (!module || !jube_static_modules[i].initialized ||
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
            if ((const void*)type == host_type) return type;
        }
    }
    return NULL;
}
