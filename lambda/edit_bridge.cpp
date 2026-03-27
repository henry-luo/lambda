// edit_bridge.cpp — Implementation of C-linkage bridge to MarkEditor
// Wraps MarkEditor's C++ methods as flat C functions for JIT-compiled
// edit handlers. Manages a global MarkEditor instance per edit session.
#include "lambda-data.hpp"
#include "edit_bridge.h"
#include "mark_editor.hpp"
#include "../lib/log.h"
#include <string.h>

// forward declaration: runtime context (defined in mir.c)
extern "C" Context* _lambda_rt;

// ============================================================================
// Global editor instance
// ============================================================================

static MarkEditor* s_editor = NULL;
static Input* s_editor_input = NULL;  // owned Input when created from runtime pool

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
        s_editor_input = input;
        log_debug("edit_bridge_init: created Input from runtime pool");
    }
    s_editor = new MarkEditor(input, EDIT_MODE_IMMUTABLE);
    log_debug("edit_bridge_init: editor created (immutable mode)");
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
