// edit_bridge.cpp — Implementation of C-linkage bridge to MarkEditor
// Wraps MarkEditor's C++ methods as flat C functions for JIT-compiled
// edit handlers. Manages a global MarkEditor instance per edit session.
#include "lambda-data.hpp"
#include "edit_bridge.h"
#include "mark_editor.hpp"
#include "mark_reader.hpp"
#include "../lib/log.h"
#include <string.h>

// forward declaration: runtime context (defined in mir.c)
extern "C" Context* _lambda_rt;

// ============================================================================
// Global editor instance
// ============================================================================

static MarkEditor* s_editor = NULL;
static Input* s_editor_input = NULL;  // owned Input when created from runtime pool

typedef struct EditSubscription {
    EditEventKind kind;
    EditCallback callback;
    void* user_data;
    struct EditSubscription* next;
} EditSubscription;

typedef struct EditSelectionVersion {
    int version_number;
    uint32_t anchor_indices[EDIT_SOURCE_PATH_MAX];
    uint32_t head_indices[EDIT_SOURCE_PATH_MAX];
    SourcePos anchor;
    SourcePos head;
    struct EditSelectionVersion* prev;
    struct EditSelectionVersion* next;
} EditSelectionVersion;

struct EditSession {
    Input* input;
    MarkEditor* editor;
    EditSchema* schema;
    uint32_t anchor_indices[EDIT_SOURCE_PATH_MAX];
    uint32_t head_indices[EDIT_SOURCE_PATH_MAX];
    SourcePos anchor;
    SourcePos head;
    EditSubscription* subscriptions;
    EditSelectionVersion* selection_head;
    EditSelectionVersion* current_selection_version;
};

static void edit_session_init_pos(EditSession* session) {
    session->anchor_indices[0] = 0;
    session->head_indices[0] = 0;
    session->anchor.path.len = 0;
    session->anchor.path.indices = session->anchor_indices;
    session->anchor.offset = 0;
    session->head.path.len = 0;
    session->head.path.indices = session->head_indices;
    session->head.offset = 0;
}

static bool edit_session_copy_pos(SourcePos* dst, uint32_t* dst_indices, SourcePos src) {
    if (src.path.len > EDIT_SOURCE_PATH_MAX) {
        log_error("edit_session_copy_pos: source path too deep: %u", src.path.len);
        return false;
    }
    dst->path.len = src.path.len;
    dst->path.indices = dst_indices;
    dst->offset = src.offset;
    for (uint32_t i = 0; i < src.path.len; i++) {
        dst_indices[i] = src.path.indices[i];
    }
    return true;
}

static void edit_session_notify(EditSession* session, EditEventKind kind, Item payload) {
    EditSubscription* sub = session->subscriptions;
    while (sub) {
        if (sub->kind == kind && sub->callback) {
            sub->callback(session, kind, payload, sub->user_data);
        }
        sub = sub->next;
    }
}

static void edit_selection_version_init(EditSelectionVersion* version) {
    version->anchor.path.indices = version->anchor_indices;
    version->head.path.indices = version->head_indices;
}

static EditSelectionVersion* edit_session_create_selection_version(EditSession* session,
                                                                  int version_number) {
    EditSelectionVersion* version = (EditSelectionVersion*)pool_calloc(
        session->input->pool, sizeof(EditSelectionVersion));
    if (!version) {
        log_error("edit_session_create_selection_version: failed to allocate selection snapshot");
        return NULL;
    }
    edit_selection_version_init(version);
    version->version_number = version_number;
    if (!edit_session_copy_pos(&version->anchor, version->anchor_indices, session->anchor)) {
        return NULL;
    }
    if (!edit_session_copy_pos(&version->head, version->head_indices, session->head)) {
        return NULL;
    }
    return version;
}

static bool edit_session_restore_selection_version(EditSession* session,
                                                  EditSelectionVersion* version) {
    if (!version) { return false; }
    if (!edit_session_copy_pos(&session->anchor, session->anchor_indices, version->anchor)) {
        return false;
    }
    if (!edit_session_copy_pos(&session->head, session->head_indices, version->head)) {
        return false;
    }
    edit_session_notify(session, EDIT_EVENT_SELECTION, ItemNull);
    return true;
}

static bool edit_session_commit_selection(EditSession* session, int version_number) {
    EditSelectionVersion* version = edit_session_create_selection_version(session, version_number);
    if (!version) { return false; }
    if (session->current_selection_version) {
        session->current_selection_version->next = version;
        version->prev = session->current_selection_version;
    } else {
        session->selection_head = version;
    }
    session->current_selection_version = version;
    return true;
}

static Input* edit_session_create_input(Item root) {
    if (!_lambda_rt || !_lambda_rt->pool) {
        log_error("edit_session_new: no runtime pool available");
        return NULL;
    }
    Input* input = Input::create(_lambda_rt->pool);
    if (!input) {
        log_error("edit_session_new: failed to create Input from runtime pool");
        return NULL;
    }
    if (_lambda_rt->ui_mode) {
        input->ui_mode = true;
    }
    input->root = root;
    return input;
}

static bool edit_session_is_error(Item item) {
    return get_type_id(item) == LMD_TYPE_ERROR;
}

static bool edit_session_set_current(EditSession* session, Item next) {
    if (edit_session_is_error(next)) {
        log_error("edit_session_set_current: command returned error item");
        return false;
    }
    session->input->root = next;
    edit_session_notify(session, EDIT_EVENT_CHANGE, next);
    return true;
}

static MapReader edit_session_args_map(Item args) {
    if (get_type_id(args) != LMD_TYPE_MAP) {
        return MapReader();
    }
    return MapReader::fromItem(args);
}

static const char* edit_session_arg_string(MapReader args, const char* key) {
    ItemReader value = args.get(key);
    if (value.isString()) {
        return value.cstring();
    }
    return NULL;
}

static Item edit_session_arg_item(MapReader args, const char* key) {
    return args.get(key).item();
}

static int64_t edit_session_arg_int(MapReader args, const char* key, int64_t fallback) {
    ItemReader value = args.get(key);
    if (value.isInt()) {
        return value.asInt();
    }
    return fallback;
}

static bool edit_session_is_map_command(const char* cmd_name) {
    return strcmp(cmd_name, "map_update") == 0 || strcmp(cmd_name, "update_map") == 0 ||
           strcmp(cmd_name, "set_field") == 0 || strcmp(cmd_name, "map_delete") == 0 ||
           strcmp(cmd_name, "delete_field") == 0;
}

static bool edit_session_is_array_command(const char* cmd_name) {
    return strcmp(cmd_name, "array_append") == 0 || strcmp(cmd_name, "append") == 0 ||
           strcmp(cmd_name, "array_insert") == 0 || strcmp(cmd_name, "insert") == 0 ||
           strcmp(cmd_name, "array_set") == 0 || strcmp(cmd_name, "set") == 0 ||
           strcmp(cmd_name, "array_delete") == 0 || strcmp(cmd_name, "delete") == 0;
}

static bool edit_session_is_element_command(const char* cmd_name) {
    return strcmp(cmd_name, "element_update_attr") == 0 || strcmp(cmd_name, "update_attr") == 0 ||
           strcmp(cmd_name, "element_delete_attr") == 0 || strcmp(cmd_name, "delete_attr") == 0 ||
           strcmp(cmd_name, "element_insert_child") == 0 || strcmp(cmd_name, "insert_child") == 0 ||
           strcmp(cmd_name, "element_replace_child") == 0 || strcmp(cmd_name, "replace_child") == 0 ||
           strcmp(cmd_name, "element_delete_child") == 0 || strcmp(cmd_name, "delete_child") == 0;
}

static bool edit_session_exec_map_command(EditSession* session, const char* cmd_name, Item args_item) {
    if (!edit_session_is_map_command(cmd_name)) { return false; }
    MapReader args = edit_session_args_map(args_item);
    if (!args.isValid()) {
        log_error("edit_session_exec: '%s' requires map args", cmd_name);
        return false;
    }
    const char* key = edit_session_arg_string(args, "key");
    if (!key) {
        log_error("edit_session_exec: '%s' requires string arg 'key'", cmd_name);
        return false;
    }

    Item current = session->input->root;
    if (strcmp(cmd_name, "map_update") == 0 || strcmp(cmd_name, "update_map") == 0 ||
        strcmp(cmd_name, "set_field") == 0) {
        Item value = edit_session_arg_item(args, "value");
        return edit_session_set_current(session, session->editor->map_update(current, key, value));
    }
    if (strcmp(cmd_name, "map_delete") == 0 || strcmp(cmd_name, "delete_field") == 0) {
        return edit_session_set_current(session, session->editor->map_delete(current, key));
    }
    return false;
}

static bool edit_session_exec_array_command(EditSession* session, const char* cmd_name, Item args_item) {
    if (!edit_session_is_array_command(cmd_name)) { return false; }
    MapReader args = edit_session_args_map(args_item);
    if (!args.isValid()) {
        log_error("edit_session_exec: '%s' requires map args", cmd_name);
        return false;
    }

    Item current = session->input->root;
    if (strcmp(cmd_name, "array_append") == 0 || strcmp(cmd_name, "append") == 0) {
        Item value = edit_session_arg_item(args, "value");
        return edit_session_set_current(session, session->editor->array_append(current, value));
    }
    if (strcmp(cmd_name, "array_insert") == 0 || strcmp(cmd_name, "insert") == 0) {
        int64_t index = edit_session_arg_int(args, "index", -1);
        Item value = edit_session_arg_item(args, "value");
        return edit_session_set_current(session, session->editor->array_insert(current, index, value));
    }
    if (strcmp(cmd_name, "array_set") == 0 || strcmp(cmd_name, "set") == 0) {
        int64_t index = edit_session_arg_int(args, "index", -1);
        Item value = edit_session_arg_item(args, "value");
        return edit_session_set_current(session, session->editor->array_set(current, index, value));
    }
    if (strcmp(cmd_name, "array_delete") == 0 || strcmp(cmd_name, "delete") == 0) {
        int64_t index = edit_session_arg_int(args, "index", -1);
        return edit_session_set_current(session, session->editor->array_delete(current, index));
    }
    return false;
}

static bool edit_session_exec_element_command(EditSession* session, const char* cmd_name, Item args_item) {
    if (!edit_session_is_element_command(cmd_name)) { return false; }
    MapReader args = edit_session_args_map(args_item);
    if (!args.isValid()) {
        log_error("edit_session_exec: '%s' requires map args", cmd_name);
        return false;
    }

    Item current = session->input->root;
    if (strcmp(cmd_name, "element_update_attr") == 0 || strcmp(cmd_name, "update_attr") == 0) {
        const char* key = edit_session_arg_string(args, "key");
        if (!key) { key = edit_session_arg_string(args, "attr"); }
        if (!key) {
            log_error("edit_session_exec: '%s' requires string arg 'key' or 'attr'", cmd_name);
            return false;
        }
        Item value = edit_session_arg_item(args, "value");
        return edit_session_set_current(session, session->editor->elmt_update_attr(current, key, value));
    }
    if (strcmp(cmd_name, "element_delete_attr") == 0 || strcmp(cmd_name, "delete_attr") == 0) {
        const char* key = edit_session_arg_string(args, "key");
        if (!key) { key = edit_session_arg_string(args, "attr"); }
        if (!key) {
            log_error("edit_session_exec: '%s' requires string arg 'key' or 'attr'", cmd_name);
            return false;
        }
        return edit_session_set_current(session, session->editor->elmt_delete_attr(current, key));
    }
    if (strcmp(cmd_name, "element_insert_child") == 0 || strcmp(cmd_name, "insert_child") == 0) {
        int64_t index = edit_session_arg_int(args, "index", -1);
        Item child = edit_session_arg_item(args, "child");
        return edit_session_set_current(session, session->editor->elmt_insert_child(current, (int)index, child));
    }
    if (strcmp(cmd_name, "element_replace_child") == 0 || strcmp(cmd_name, "replace_child") == 0) {
        int64_t index = edit_session_arg_int(args, "index", -1);
        Item child = edit_session_arg_item(args, "child");
        return edit_session_set_current(session, session->editor->elmt_replace_child(current, (int)index, child));
    }
    if (strcmp(cmd_name, "element_delete_child") == 0 || strcmp(cmd_name, "delete_child") == 0) {
        int64_t index = edit_session_arg_int(args, "index", -1);
        return edit_session_set_current(session, session->editor->elmt_delete_child(current, (int)index));
    }
    return false;
}

// ============================================================================
// Edit session API
// ============================================================================

EditSession* edit_session_new(Item root, EditSchema* schema) {
    Input* input = edit_session_create_input(root);
    if (!input) { return NULL; }
    return edit_session_new_with_input(input, root, schema);
}

EditSession* edit_session_new_with_input(void* input_ptr, Item root, EditSchema* schema) {
    Input* input = (Input*)input_ptr;
    if (!input) {
        log_error("edit_session_new_with_input: input is null");
        return NULL;
    }
    input->root = root;
    EditSession* session = new EditSession;
    if (!session) {
        log_error("edit_session_new_with_input: failed to allocate session");
        return NULL;
    }
    memset(session, 0, sizeof(EditSession));
    session->input = input;
    session->schema = schema;
    session->editor = new MarkEditor(input, EDIT_MODE_IMMUTABLE);
    if (!session->editor) {
        log_error("edit_session_new_with_input: failed to allocate MarkEditor");
        delete session;
        return NULL;
    }
    edit_session_init_pos(session);
    log_debug("edit_session_new_with_input: rich-text session created");
    return session;
}

void edit_session_destroy(EditSession* session) {
    if (!session) { return; }
    if (session->editor) {
        delete session->editor;
        session->editor = NULL;
    }
    delete session;
    log_debug("edit_session_destroy: rich-text session destroyed");
}

bool edit_session_exec(EditSession* session, const char* cmd_name, Item args) {
    if (!session || !session->editor || !cmd_name) {
        log_error("edit_session_exec: invalid session or command");
        return false;
    }
    if (strcmp(cmd_name, "commit") == 0) {
        int version = session->editor->commit(NULL);
        bool ok = version >= 0 && edit_session_commit_selection(session, version);
        if (ok) { edit_session_notify(session, EDIT_EVENT_CHANGE, session->input->root); }
        return ok;
    }
    if (strcmp(cmd_name, "undo") == 0 || strcmp(cmd_name, "historyUndo") == 0) {
        bool ok = session->editor->undo();
        if (ok) {
            if (session->current_selection_version && session->current_selection_version->prev) {
                session->current_selection_version = session->current_selection_version->prev;
                edit_session_restore_selection_version(session, session->current_selection_version);
            }
            edit_session_notify(session, EDIT_EVENT_CHANGE, session->input->root);
        }
        return ok;
    }
    if (strcmp(cmd_name, "redo") == 0 || strcmp(cmd_name, "historyRedo") == 0) {
        bool ok = session->editor->redo();
        if (ok) {
            if (session->current_selection_version && session->current_selection_version->next) {
                session->current_selection_version = session->current_selection_version->next;
                edit_session_restore_selection_version(session, session->current_selection_version);
            }
            edit_session_notify(session, EDIT_EVENT_CHANGE, session->input->root);
        }
        return ok;
    }
    if (strcmp(cmd_name, "set_root") == 0 || strcmp(cmd_name, "set_doc") == 0 || strcmp(cmd_name, "replace_root") == 0) {
        return edit_session_set_current(session, args);
    }
    if (edit_session_is_map_command(cmd_name)) {
        return edit_session_exec_map_command(session, cmd_name, args);
    }
    if (edit_session_is_array_command(cmd_name)) {
        return edit_session_exec_array_command(session, cmd_name, args);
    }
    if (edit_session_is_element_command(cmd_name)) {
        return edit_session_exec_element_command(session, cmd_name, args);
    }
    log_error("edit_session_exec: unknown command '%s'", cmd_name);
    return false;
}

Item edit_session_current(EditSession* session) {
    if (!session || !session->editor) {
        log_error("edit_session_current: invalid session");
        return ItemNull;
    }
    return session->input->root;
}

EditSchema* edit_session_schema(EditSession* session) {
    if (!session) { return NULL; }
    return session->schema;
}

bool edit_session_set_selection(EditSession* session, SourcePos anchor, SourcePos head) {
    if (!session) {
        log_error("edit_session_set_selection: invalid session");
        return false;
    }
    if (!edit_session_copy_pos(&session->anchor, session->anchor_indices, anchor)) { return false; }
    if (!edit_session_copy_pos(&session->head, session->head_indices, head)) { return false; }
    edit_session_notify(session, EDIT_EVENT_SELECTION, ItemNull);
    return true;
}

SourcePos edit_session_selection_anchor(EditSession* session) {
    SourcePos empty;
    memset(&empty, 0, sizeof(SourcePos));
    if (!session) { return empty; }
    return session->anchor;
}

SourcePos edit_session_selection_head(EditSession* session) {
    SourcePos empty;
    memset(&empty, 0, sizeof(SourcePos));
    if (!session) { return empty; }
    return session->head;
}

void edit_session_subscribe(EditSession* session, EditEventKind kind, EditCallback callback, void* user_data) {
    if (!session || !callback) { return; }
    EditSubscription* sub = (EditSubscription*)pool_calloc(session->input->pool, sizeof(EditSubscription));
    if (!sub) {
        log_error("edit_session_subscribe: failed to allocate subscription");
        return;
    }
    sub->kind = kind;
    sub->callback = callback;
    sub->user_data = user_data;
    sub->next = session->subscriptions;
    session->subscriptions = sub;
}

// ============================================================================
// Lifecycle
// ============================================================================

int edit_bridge_init(void* input_ptr) {
    if (s_editor) {
        log_debug("edit_bridge_init: editor already active, destroying old");
        delete s_editor;
        s_editor = NULL;
    }
    Input* input = (Input*)input_ptr;
    if (!input) {
        // create a lightweight Input from runtime pool
        if (!_lambda_rt || !_lambda_rt->pool) {
            log_error("edit_bridge_init: no input and no runtime pool available");
            return -1;
        }
        input = Input::create(_lambda_rt->pool);
        if (!input) {
            log_error("edit_bridge_init: failed to create Input from runtime pool");
            return -1;
        }
        // Propagate ui_mode so MarkEditor knows data buffers are arena-allocated
        // and must NOT be freed via pool_free (they live on the result_arena).
        if (_lambda_rt->ui_mode) {
            input->ui_mode = true;
        }
        s_editor_input = input;
        log_debug("edit_bridge_init: created Input from runtime pool");
    }
    s_editor = new MarkEditor(input, EDIT_MODE_INLINE);
    log_debug("edit_bridge_init: editor created (inline mode)");
    return 0;
}

void edit_bridge_destroy(void) {
    if (s_editor) {
        delete s_editor;
        s_editor = NULL;
        log_debug("edit_bridge_destroy: editor destroyed");
    }
}

bool edit_bridge_active(void) {
    return s_editor != NULL;
}

// ============================================================================
// Map operations
// ============================================================================

Item edit_map_update(Item map, const char* key, Item value) {
    if (!s_editor) {
        log_error("edit_map_update: no editor active");
        return map;
    }
    // handle both maps and elements — the transpiler may not know the
    // runtime type at compile time (model typed as ANY)
    TypeId tid = get_type_id(map);
    if (tid == LMD_TYPE_ELEMENT) {
        return s_editor->elmt_update_attr(map, key, value);
    }
    return s_editor->map_update(map, key, value);
}

Item edit_map_delete(Item map, const char* key) {
    if (!s_editor) {
        log_error("edit_map_delete: no editor active");
        return map;
    }
    return s_editor->map_delete(map, key);
}

// ============================================================================
// Element operations
// ============================================================================

Item edit_elmt_update_attr(Item element, const char* attr_name, Item value) {
    if (!s_editor) {
        log_error("edit_elmt_update_attr: no editor active");
        return element;
    }
    return s_editor->elmt_update_attr(element, attr_name, value);
}

Item edit_elmt_delete_attr(Item element, const char* attr_name) {
    if (!s_editor) {
        log_error("edit_elmt_delete_attr: no editor active");
        return element;
    }
    return s_editor->elmt_delete_attr(element, attr_name);
}

Item edit_elmt_insert_child(Item element, int index, Item child) {
    if (!s_editor) {
        log_error("edit_elmt_insert_child: no editor active");
        return element;
    }
    return s_editor->elmt_insert_child(element, index, child);
}

Item edit_elmt_delete_child(Item element, int index) {
    if (!s_editor) {
        log_error("edit_elmt_delete_child: no editor active");
        return element;
    }
    return s_editor->elmt_delete_child(element, index);
}

Item edit_elmt_replace_child(Item element, int index, Item new_child) {
    if (!s_editor) {
        log_error("edit_elmt_replace_child: no editor active");
        return element;
    }
    return s_editor->elmt_replace_child(element, index, new_child);
}

// ============================================================================
// Array operations
// ============================================================================

Item edit_array_set(Item array, int64_t index, Item value) {
    if (!s_editor) {
        log_error("edit_array_set: no editor active");
        return array;
    }
    return s_editor->array_set(array, index, value);
}

Item edit_array_insert(Item array, int64_t index, Item value) {
    if (!s_editor) {
        log_error("edit_array_insert: no editor active");
        return array;
    }
    return s_editor->array_insert(array, index, value);
}

Item edit_array_delete(Item array, int64_t index) {
    if (!s_editor) {
        log_error("edit_array_delete: no editor active");
        return array;
    }
    return s_editor->array_delete(array, index);
}

Item edit_array_append(Item array, Item value) {
    if (!s_editor) {
        log_error("edit_array_append: no editor active");
        return array;
    }
    return s_editor->array_append(array, value);
}

// ============================================================================
// Version control
// ============================================================================

int edit_commit(const char* description) {
    if (!s_editor) {
        log_error("edit_commit: no editor active");
        return -1;
    }
    return s_editor->commit(description);
}

bool edit_undo(void) {
    if (!s_editor) {
        log_error("edit_undo: no editor active");
        return false;
    }
    return s_editor->undo();
}

bool edit_redo(void) {
    if (!s_editor) {
        log_error("edit_redo: no editor active");
        return false;
    }
    return s_editor->redo();
}

Item edit_current(void) {
    if (!s_editor) {
        log_error("edit_current: no editor active");
        return ItemNull;
    }
    return s_editor->current();
}

// ============================================================================
// System function wrappers (return Item for sys_func convention)
// extern "C" so they have C linkage matching sys_func_registry.c declarations
// ============================================================================

extern "C" {

Item fn_undo(void) {
    bool ok = edit_undo();
    return (Item){.item = ok ? ITEM_TRUE : ITEM_FALSE};
}

Item fn_redo(void) {
    bool ok = edit_redo();
    return (Item){.item = ok ? ITEM_TRUE : ITEM_FALSE};
}

Item fn_commit0(void) {
    int ver = edit_commit(NULL);
    return (Item){.item = i2it(ver)};
}

Item fn_commit1(Item description) {
    const char* desc = NULL;
    if (get_type_id(description) == LMD_TYPE_STRING) {
        String* s = description.get_string();
        if (s) desc = s->chars;
    }
    int ver = edit_commit(desc);
    return (Item){.item = i2it(ver)};
}

} // extern "C"
