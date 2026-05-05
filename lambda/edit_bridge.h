// edit_bridge.h — C-linkage bridge to MarkEditor for JIT-compiled edit handlers
// Provides flat C functions wrapping MarkEditor methods. A global MarkEditor
// instance is managed per edit session. Edit handlers emit calls to these
// functions instead of direct in-place mutations.
#pragma once

#include "lambda.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle: set up the global editor from an Input*, tear down when done.
// Returns 0 on success, -1 on failure.
int edit_bridge_init(void* input_ptr);
void edit_bridge_destroy(void);

// Check if the global editor is active
bool edit_bridge_active(void);

// ========================================================================
// Edit session API for rich-text editing
// ========================================================================

#define EDIT_SOURCE_PATH_MAX 32

typedef struct EditSession EditSession;
typedef struct EditSchema EditSchema;

typedef struct SourcePath {
	uint32_t len;
	uint32_t* indices;
} SourcePath;

typedef struct SourcePos {
	SourcePath path;
	uint32_t offset;
} SourcePos;

typedef enum EditEventKind {
	EDIT_EVENT_CHANGE = 1,
	EDIT_EVENT_SELECTION = 2
} EditEventKind;

typedef void (*EditCallback)(EditSession* session, EditEventKind kind, Item payload, void* user_data);

EditSession* edit_session_new(Item root, EditSchema* schema);
EditSession* edit_session_new_with_input(void* input_ptr, Item root, EditSchema* schema);
void edit_session_destroy(EditSession* session);
bool edit_session_exec(EditSession* session, const char* cmd_name, Item args);
Item edit_session_current(EditSession* session);
EditSchema* edit_session_schema(EditSession* session);
bool edit_session_set_selection(EditSession* session, SourcePos anchor, SourcePos head);
SourcePos edit_session_selection_anchor(EditSession* session);
SourcePos edit_session_selection_head(EditSession* session);
void edit_session_subscribe(EditSession* session, EditEventKind kind, EditCallback callback, void* user_data);

// ========================================================================
// Map operations
// ========================================================================

// map_update(map, key, value) → updated map Item
Item edit_map_update(Item map, const char* key, Item value);

// map_delete(map, key) → updated map Item
Item edit_map_delete(Item map, const char* key);

// ========================================================================
// Element operations
// ========================================================================

// element attribute update
Item edit_elmt_update_attr(Item element, const char* attr_name, Item value);

// element attribute delete
Item edit_elmt_delete_attr(Item element, const char* attr_name);

// element child insert at index (index=-1 for append)
Item edit_elmt_insert_child(Item element, int index, Item child);

// element child delete at index
Item edit_elmt_delete_child(Item element, int index);

// element child replace at index
Item edit_elmt_replace_child(Item element, int index, Item new_child);

// ========================================================================
// Array operations
// ========================================================================

Item edit_array_set(Item array, int64_t index, Item value);
Item edit_array_insert(Item array, int64_t index, Item value);
Item edit_array_delete(Item array, int64_t index);
Item edit_array_append(Item array, Item value);

// ========================================================================
// Version control
// ========================================================================

// Commit current state as a version. Returns version number.
int edit_commit(const char* description);

// Undo last committed change. Returns true if successful.
bool edit_undo(void);

// Redo last undone change. Returns true if successful.
bool edit_redo(void);

// Get current document root after edits.
Item edit_current(void);

#ifdef __cplusplus
}
#endif
