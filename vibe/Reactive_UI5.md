# Reactive UI Phase 5 — Advanced Todo App with Multi-Panel Layout, Drag-and-Drop, and Rich Text Editing

**Date:** 2026-04-06
**Status:** Proposal
**Prerequisite:** Phases 14–19 complete (Reactive_UI4.md)

---

## Table of Contents

1. [Objective](#1-objective)
2. [Current State Assessment](#2-current-state-assessment)
3. [Target Application: todo2.ls](#3-target-application-todo2ls)
4. [Phase 20: Two-Column Layout with File List](#4-phase-20-two-column-layout-with-file-list)
5. [Phase 21: File Management — Create, Delete, Rename](#5-phase-21-file-management--create-delete-rename)
6. [Phase 22: Inline "+" Add and Direct Text Editing](#6-phase-22-inline--add-and-direct-text-editing)
7. [Phase 23: Multi-Line Text Area](#7-phase-23-multi-line-text-area)
8. [Phase 24: Text Selection in Text Area](#8-phase-24-text-selection-in-text-area)
9. [Phase 25: Copy / Cut / Paste / Delete with Selection](#9-phase-25-copy--cut--paste--delete-with-selection)
10. [Phase 26: Drag-and-Drop Between Categories](#10-phase-26-drag-and-drop-between-categories)
11. [Data Model](#11-data-model)
12. [Implementation Order](#12-implementation-order)
13. [Files Modified / Created](#13-files-modified--created)
14. [Test Plan](#14-test-plan)

---

## 1. Objective

Build `todo2.ls` — a substantially more capable todo application that exercises and extends the reactive UI engine. The app features a two-column layout (file list + todo list), file management operations, drag-and-drop item reordering across categories, inline text editing, multi-line text areas, and clipboard operations with text selection.

This phase serves two purposes:
1. **Application**: Demonstrate Lambda's viability for non-trivial interactive applications
2. **Engine**: Drive implementation of missing infrastructure (textarea editing, element drag-and-drop, paste, multi-line caret navigation)

### Feature Summary

| Feature | App Behavior | Engine Work Required |
|---------|-------------|---------------------|
| Two-column layout | File list (left) + todo list (right) | None — CSS flexbox already works |
| File management | Create / delete / rename todo files | Minor — Lambda `output()` + `input()` exist; need confirm dialog |
| Inline "+" add | Click "+" on category → new editable item appears | Minor — new template pattern, state management |
| Direct text editing | Click todo text → inline edit | Minor — `contenteditable` or swap to `<input>` on click |
| Multi-line text area | Todo text expands to textarea on edit | **Medium** — wire textarea keyboard handling in event.cpp |
| Text selection | Click-drag / Shift+arrow in textarea | **Medium** — extend selection state to textarea context |
| Copy/Cut/Paste/Delete | Cmd+C/X/V/Backspace on selection | **Medium** — paste from clipboard, delete selection range |
| Drag-and-drop | Drag items between categories | **Large** — new event dispatch, visual drag feedback, drop targets |

---

## 2. Current State Assessment

### What Works (from todo.ls and Phases 1–19)

- **Reactive view/edit templates** with state, event handlers, `emit()` bubbling
- **Single-line `<input type="text">`** with caret, arrow keys, backspace, character input
- **Click events** dispatched to Lambda handlers with `target_class`, `target_tag`
- **Keydown events** for Backspace, Delete, Enter, Escape
- **Input events** with `char` and `caret_pos`
- **`emit(name, data)`** for child→parent event bubbling
- **`input()`/`output()`** for file read/write (JSON, etc.)
- **CSS flexbox layout**, gradient backgrounds, box shadows, hover pseudo-classes
- **Text selection** via click-drag and Shift+arrow (visual selection rendering exists)
- **Clipboard copy** (Cmd+C) — copies selected text to system clipboard
- **Incremental DOM patching** — only changed subtree is rebuilt
- **Performance**: ~32ms selective toggle, ~37-42ms text input, 0.04ms no-op

### What's Missing (Engine Gaps)

| Gap | Current State | Needed For |
|-----|--------------|-----------|
| **Textarea keyboard handling** | `FORM_CONTROL_TEXTAREA` recognized but no key dispatch | Multi-line text editing |
| **Textarea text rendering** | Empty box drawn — no text or cursor | Multi-line text editing |
| **Multi-line caret navigation** | `CaretState` has `line`/`column` but unused | Up/Down arrow in textarea |
| **Clipboard paste** | `RDT_KEY_V` defined, no handler | Paste into text fields |
| **Element drag-and-drop** | `drag_target`/`is_dragging` exist for selection drag only | Drag items between categories |
| **Lambda `mousedown`/`mousemove`/`mouseup` events** | Only `click` dispatched to handlers | Drag-and-drop in Lambda |
| **Confirm dialog** | No modal/dialog support | Delete file confirmation |
| **Line wrapping in textarea** | No soft-wrap computation | Multi-line text display |
| **Scrollable textarea** | Scroll exists for main viewport only | Long text in textarea |

---

## 3. Target Application: todo2.ls

### Visual Layout

```
┌─────────────────────────────────────────────────────────────┐
│  Lambda Todo App                                             │
├──────────────┬──────────────────────────────────────────────┤
│              │                                               │
│  Todo Files  │  Work                                         │
│  ─────────── │  ─────────────────────────────────────────    │
│  ● Work      │  ○ Review pull requests            [×]       │
│    Personal  │    Click to edit...                            │
│    Learning  │                                               │
│    Shopping  │  ✓ Update documentation             [×]       │
│              │    Click to edit...                            │
│  [+ New]     │                                               │
│              │  ○ Fix CI pipeline                  [×]       │
│              │    Need to investigate the           │
│              │    failing test on Linux.            │
│              │                                               │
│              │  [+ Add task]                                 │
│              │                                               │
│              │  Personal                                     │
│              │  ─────────────────────────────────────────    │
│              │  ○ Buy groceries                    [×]       │
│              │    Milk, eggs, bread                           │
│              │                                               │
│              │  [+ Add task]                                 │
│              │                                               │
├──────────────┴──────────────────────────────────────────────┤
│  3 of 8 tasks completed                                      │
└─────────────────────────────────────────────────────────────┘
```

### Interaction Summary

- **Left panel**: List of `.json` files in a data directory. Click to load. Active file highlighted.
- **Right panel**: Categories (lists) from the active file. Each todo item shows checkbox, text summary, delete button.
- **File management**: "+ New" creates a file (prompts for name via inline edit). Right-click or dedicated button for rename/delete.
- **Add item**: "+" icon at bottom of each category. Click → new item appears in edit mode.
- **Edit item text**: Click on todo text → expands to multi-line textarea for editing. Click away or press Escape → collapse back to single-line summary.
- **Drag-and-drop**: Press and hold on a todo item → drag → drop onto another category header to move it.
- **Text selection**: In textarea, click-drag or Shift+arrow to select. Cmd+C/X/V/Backspace operate on selection.

---

## 4. Phase 20: Two-Column Layout with File List

### 4.1 Data Directory Structure

Todo files stored in `./data/todo/` (configurable). Each file is a JSON file:

```
data/todo/
  work.json       ← {"name": "Work", "lists": [...]}
  personal.json
  shopping.json
```

### 4.2 App State Model

The top-level `edit` template manages application state:

```lambda
edit <todo_app> state active_file: "", file_list: [], rename_target: "", new_file_mode: false {
  // file_list populated on init from directory listing
  // active_file tracks which .json is currently displayed
}
```

### 4.3 File List Component

```lambda
view <file_entry> state is_hover: false {
  let active_class = if (~.name == ~.active) "file-entry active" else "file-entry"
  <div class:active_class
    <span class:"file-icon"; "📄">
    <span class:"file-name"; ~.name>
    <span class:"file-delete"; "×">
  >
}
```

### 4.4 Two-Column CSS Layout

```css
.app-layout {
  display: flex;
  flex-direction: row;
  height: 100%;
}

.file-panel {
  width: 180px;
  min-width: 180px;
  border-right: 1px solid #ddd;
  background: #f8f9fa;
  display: flex;
  flex-direction: column;
  overflow-y: auto;
}

.todo-panel {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
}
```

### 4.5 File Loading

```lambda
on click(evt) {
  if (evt.target_class == "file-entry" or evt.target_class == "file-name") {
    let filename = evt.target_text  // or pass via emit
    active_file = filename
    // Load file data — triggers re-render of right panel
  }
}
```

### 4.6 Implementation Notes

- **No engine changes needed.** CSS flexbox two-column layout already works (tested in Radiant baseline).
- File list population uses Lambda's `input()` for reading and Path system for directory listing.
- Active file state drives conditional rendering of right panel content.

### 4.7 Risk Assessment

**Low risk.** All required CSS layout and Lambda features exist. The main challenge is structuring the Lambda template hierarchy cleanly with the top-level state management.

---

## 5. Phase 21: File Management — Create, Delete, Rename

### 5.1 Create New File

Click "+ New" button in file panel → switches to inline-edit mode:

```lambda
if (new_file_mode) {
  <div class:"file-entry editing"
    <input type:"text", class:"file-name-input", placeholder:"filename", value:new_file_name>
  >
} else {
  <button class:"new-file-btn"; "+ New">
}
```

On Enter: `output({name: new_file_name, lists: [{name: "Tasks", items: []}]}, path, 'json')` → refresh file list.

### 5.2 Delete File

Click "×" on file entry → confirm via a visual indicator (e.g., entry turns red with "Confirm?" text, second click deletes).

Two-click confirmation pattern (no modal dialog needed):

```lambda
view <file_entry> state pending_delete: false {
  if (pending_delete) {
    <div class:"file-entry confirm-delete"
      <span "Delete "; ~.name; "?">
      <span class:"confirm-yes"; "Yes">
      <span class:"confirm-no"; "No">
    >
  } else {
    // normal rendering
  }
}
```

For actual file deletion, Lambda needs a `delete_file(path)` or `remove(path)` system function. If not available, write an empty/tombstone JSON and filter on load.

### 5.3 Rename File

Click file name (long press or double-click, or dedicated rename button) → inline edit:

```lambda
if (~.name == rename_target) {
  <input type:"text", class:"rename-input", value:~.name>
} else {
  <span class:"file-name"; ~.name>
}
```

On Enter: read old file → `output()` to new path → delete old file → refresh.

### 5.4 Engine Work Required

| Item | Status | Work |
|------|--------|------|
| `output()` — write JSON | ✅ Exists | None |
| `input()` — read JSON | ✅ Exists | None |
| Directory listing | ✅ Path system | Verify works for `./data/todo/` |
| File deletion | ❌ Missing | Add `pn_delete_file(path)` to sys_func_registry |
| File rename | ❌ Missing | Add `pn_rename_file(old, new)` to sys_func_registry |
| Double-click event | ✅ `RDT_EVENT_DBL_CLICK` exists | Verify dispatched to Lambda handlers |

### 5.5 New System Functions

```c
// In lambda/lambda-proc.cpp
Item pn_delete_file(Item path) {
    const char* p = item_to_str(path);
    if (!p) return ItemError;
    if (remove(p) != 0) {
        log_error("delete_file: failed to delete %s: %s", p, strerror(errno));
        return ItemError;
    }
    return ItemNull;
}

Item pn_rename_file(Item old_path, Item new_path) {
    const char* old_p = item_to_str(old_path);
    const char* new_p = item_to_str(new_path);
    if (!old_p || !new_p) return ItemError;
    if (rename(old_p, new_p) != 0) {
        log_error("rename_file: failed: %s", strerror(errno));
        return ItemError;
    }
    return ItemNull;
}
```

Register in `sys_func_registry.c`:
```c
{SYSPROC_DELETE_FILE, "delete_file", 1, ..., "pn_delete_file", ...},
{SYSPROC_RENAME_FILE, "rename_file", 2, ..., "pn_rename_file", ...},
```

### 5.6 Risk Assessment

**Low risk.** File I/O is well-tested. The `delete_file`/`rename_file` functions are trivial wrappers around POSIX `remove()`/`rename()`. The two-click delete confirmation avoids modal dialog complexity.

---

## 6. Phase 22: Inline "+" Add and Direct Text Editing

### 6.1 Inline Add with "+" Icon

Replace the current `<input>` + "Add" button pattern with a "+" icon per category:

```lambda
edit <todo_list> state adding: false, new_text: "" {
  // ... existing items ...
  if (adding) {
    <li class:"todo-item editing"
      <span class:"checkbox"; "○">
      <input type:"text", class:"inline-edit", placeholder:"What needs to be done?", value:new_text>
    >
  }
  <div class:"add-row"
    <span class:"add-icon"; "+">
  >
}
on click(evt) {
  if (evt.target_class == "add-icon") {
    adding = true
    // Focus will be set to the new input automatically
  }
}
on keydown(evt) {
  if (adding and evt.key == "Enter" and new_text != "") {
    let next_id = max_id(~.items) + 1
    ~.items = ~.items ++ [{id: next_id, text: new_text, done: false, notes: ""}]
    adding = false
    new_text = ""
  }
  if (adding and evt.key == "Escape") {
    adding = false
    new_text = ""
  }
}
```

### 6.2 Direct Text Editing on Click

Click on a todo item's text → switch from display `<span>` to editable `<input>`:

```lambda
view <todo_item> state toggled: false, editing: false {
  let done = if (toggled) (!~.done) else ~.done
  <li class:class_name
    <span class:"checkbox"; check_mark>
    if (editing) {
      <input type:"text", class:"inline-edit", value:~.text>
    } else {
      <span class:"todo-text"; ~.text>
    }
    <span class:"delete-btn"; "×">
  >
}
on click(evt) {
  if (evt.target_class == "todo-text") {
    editing = true
  }
  // ...existing toggle/delete logic
}
```

**Problem**: `view` templates cannot mutate the model (`~.text`). The text edit needs to bubble up to the parent `edit` template via `emit()`:

```lambda
on keydown(evt) {
  if (editing and evt.key == "Enter") {
    emit("update_text", {id: ~.id, text: edited_text})
    editing = false
  }
  if (editing and evt.key == "Escape") {
    editing = false
  }
}
```

Parent `edit <todo_list>` handler:
```lambda
on update_text(evt) {
  ~.items = for (item in ~.items)
    if (item.id == evt.id) {id: item.id, text: evt.text, done: item.done}
    else item
}
```

### 6.3 Focus Management

When `adding = true` or `editing = true`, the newly created `<input>` should receive focus automatically. This requires **auto-focus on newly inserted form controls**.

Current behavior: focus is set on click via hit-testing. For programmatically-inserted inputs, add an `autofocus` attribute check in the layout/form-control initialization path:

```cpp
// In radiant/form_control.cpp or resolve_htm_style.cpp
if (dom_elem->has_attribute("autofocus") && !doc->state->focus) {
    caret_set(doc->state, (View*)dom_elem, 0);
}
```

Alternatively, use a simpler approach: after incremental rebuild, if the new subtree contains an `<input>`, set focus to it. This can be done in `rebuild_lambda_doc_incremental` post-patch.

### 6.4 Engine Work Required

| Item | Status | Work |
|------|--------|------|
| Inline `<input>` rendering | ✅ Exists | None |
| `emit()` for text updates | ✅ Exists | None |
| Auto-focus on new `<input>` | ❌ Missing | Add autofocus check after incremental rebuild |
| Blur / focus-out event | ✅ `RDT_EVENT_FOCUS_OUT` exists | Verify dispatched to Lambda as `"blur"` |

### 6.5 New Lambda Event: `blur`

Currently `RDT_EVENT_FOCUS_OUT` is handled internally (clears caret) but not dispatched to Lambda handlers. Add dispatch:

```cpp
// In event.cpp, RDT_EVENT_FOCUS_OUT handling:
if (old_focus_elem) {
    dispatch_lambda_handler(&evcon, old_focus_elem, "blur");
}
```

This lets `on blur(evt)` in templates detect when the user clicks away from an inline edit, triggering save-and-close.

### 6.6 Risk Assessment

**Low risk.** The inline add and edit patterns use existing template state and `emit()` bubbling. Auto-focus is a small addition. The `blur` event dispatch is a one-line addition mirroring the existing `click` dispatch pattern.

---

## 7. Phase 23: Multi-Line Text Area

### 7.1 Application Use

Each todo item has a `notes` field (multi-line text). When editing, the notes field renders as a `<textarea>`:

```lambda
if (editing) {
  <div class:"edit-form"
    <input type:"text", class:"title-edit", value:~.text>
    <textarea class:"notes-edit", rows:"4", value:~.notes>
  >
}
```

### 7.2 Engine Work: Textarea Text Rendering

Current state: `render_textarea` draws an empty bordered box. Need to render text content with line wrapping.

#### 7.2.1 Text Content Storage

Textarea value comes from the `value` attribute on the `<textarea>` DOM element. In Lambda:
```lambda
<textarea value:~.notes>
```

The value string contains `\n` for line breaks. The renderer must:
1. Split value by `\n` into logical lines
2. Soft-wrap each logical line at the textarea width
3. Render each visual line using existing text rendering (font_load_glyph + bitmap blit)

#### 7.2.2 Rendering Implementation

```cpp
// In radiant/render_form.cpp — replace empty render_textarea
void render_textarea(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    float x = block->abs_x, y = block->abs_y;
    float w = block->width, h = block->height;
    float pad = 4.0f;  // internal padding
    float scale = rdcon->ui_context->scale;
    
    // Background + border
    fill_rect(rdcon, x, y, w, h, make_color(255, 255, 255));
    draw_3d_border(rdcon, x, y, w, h, true, 1 * scale);
    
    // Get text value
    const char* text = form->value;  // or from DOM attribute
    if (!text || *text == '\0') {
        // Render placeholder if exists
        render_textarea_placeholder(rdcon, block, form);
        return;
    }
    
    // Render lines
    float line_height = form->font_size * 1.2f;
    float cx = x + pad, cy = y + pad;
    float max_x = x + w - pad;
    
    const char* p = text;
    while (*p && cy + line_height <= y + h) {
        if (*p == '\n') {
            cy += line_height;
            cx = x + pad;
            p++;
            continue;
        }
        // Render character, advance cx
        // On soft-wrap (cx > max_x): cy += line_height, cx = x + pad
        render_glyph_at(rdcon, block, *p, &cx, cy, max_x, line_height);
        p = utf8_next(p);
    }
}
```

#### 7.2.3 Keyboard Handling

Extend `RDT_EVENT_KEY_DOWN` and `RDT_EVENT_TEXT_INPUT` in `event.cpp` to handle `FORM_CONTROL_TEXTAREA`:

```cpp
// In KEY_DOWN handler:
if (focus_elem->form->control_type == FORM_CONTROL_TEXT ||
    focus_elem->form->control_type == FORM_CONTROL_TEXTAREA) {
    // Existing Left/Right/Home/End/Backspace handling
    
    if (focus_elem->form->control_type == FORM_CONTROL_TEXTAREA) {
        // Additional: Up/Down arrow — move caret up/down a visual line
        // Enter — insert newline (dispatch to Lambda as keydown with key="Enter")
        // but do NOT submit form — textarea Enter ≠ input Enter
    }
}
```

**Key differences from single-line `<input>`**:

| Key | `<input>` behavior | `<textarea>` behavior |
|-----|--------------------|-----------------------|
| Enter | Submit / dispatch `keydown` | Insert `\n` / dispatch `keydown` |
| Up | No action | Move caret up one visual line |
| Down | No action | Move caret down one visual line |
| Home | Move to start of text | Move to start of current line |
| End | Move to end of text | Move to end of current line |

#### 7.2.4 Multi-Line Caret Positioning

`CaretState` already has `line` and `column` fields. For textarea:

```cpp
// After Enter key in textarea:
caret->line++;
caret->column = 0;
caret->char_offset = /* position after the \n */;
// Recompute visual x, y from line/column
caret->y += line_height;
caret->x = textarea_x + pad;
```

For Up/Down arrow:
```cpp
// Up arrow:
if (caret->line > 0) {
    caret->line--;
    caret->column = min(caret->column, line_length(text, caret->line));
    caret->char_offset = offset_of(text, caret->line, caret->column);
    recompute_caret_visual_position(caret, block, form);
}
```

#### 7.2.5 Textarea Scroll

For text longer than the visible area, track a `scroll_y` offset on the textarea's form control state:

```cpp
// In FormControlProp or new TextareaState:
float scroll_y;         // vertical scroll offset in pixels
int total_visual_lines;  // computed during rendering
```

Arrow key / mouse wheel adjusts `scroll_y`. Rendering offsets all lines by `-scroll_y`. Characters above/below the visible area are clipped.

### 7.3 Risk Assessment

**Medium risk.** Textarea rendering with soft-wrap, multi-line caret, and scroll is the most complex engine feature in this phase. The core text rendering primitives exist (font_load_glyph, bitmap blit), but line-wrapping logic and multi-line caret math are new.

Key risks:
- Line wrapping must match layout width accurately (off-by-one → visual glitches)
- Caret positioning on wrapped lines requires a char→visual-position mapping
- Scroll offset interaction with caret-follows-cursor behavior

Mitigation: Start with fixed-width (monospace) rendering where wrapping is predictable. Proportional fonts can be added later.

---

## 8. Phase 24: Text Selection in Text Area

### 8.1 Current Selection Infrastructure

`SelectionState` in `state_store.hpp` already tracks:
- `anchor_view` / `focus_view` — start/end views of selection
- `anchor_offset` / `focus_offset` — character offsets
- `anchor_line` / `focus_line` — line numbers (unused, prepared for multi-line)
- `is_selecting` — mouse-drag-in-progress flag

Currently used for text selection across DOM text nodes (click-drag on rendered text). The infrastructure supports cross-view selection but not textarea-internal selection.

### 8.2 Textarea Selection Model

For textarea, selection is **within a single form control** (not across DOM views):

```cpp
typedef struct TextareaSelectionState {
    int anchor_offset;      // char offset where selection started
    int anchor_line;        // line of anchor
    int anchor_column;      // column of anchor
    int focus_offset;       // char offset where selection ends (caret position)
    int focus_line;
    int focus_column;
    bool has_selection;     // true if anchor != focus
} TextareaSelectionState;
```

This can be stored on the `FormControlProp` or as part of `CaretState` (which already has `line`/`column`).

### 8.3 Selection Triggers

| Action | Effect |
|--------|--------|
| Click in textarea | Set caret, clear selection |
| Shift+Click | Extend selection from anchor to click position |
| Click+Drag | Start selection at mousedown, extend to mousemove |
| Shift+Left/Right | Extend selection by one character |
| Shift+Up/Down | Extend selection by one line |
| Shift+Home/End | Extend selection to line start/end |
| Cmd+A (in textarea) | Select all text in textarea |

### 8.4 Selection Rendering

Draw selection highlight rectangles behind selected text:

```cpp
void render_textarea_selection(RenderContext* rdcon, ViewBlock* block,
                                FormControlProp* form, TextareaSelectionState* sel) {
    if (!sel->has_selection) return;
    
    int start = min(sel->anchor_offset, sel->focus_offset);
    int end = max(sel->anchor_offset, sel->focus_offset);
    
    // For each visual line that overlaps [start, end]:
    //   compute start_x, end_x of selected range on that line
    //   fill_rect(selection_color, start_x, line_y, end_x - start_x, line_height)
    
    Color sel_color = make_color(173, 214, 255);  // light blue
    // ... line-by-line highlight rendering
}
```

### 8.5 Engine Changes

1. **Mouse events in textarea**: `MOUSE_DOWN` sets anchor, `MOUSE_MOVE` with button down extends focus
2. **Shift+arrow key handling**: In `KEY_DOWN`, when Shift is held, update focus without clearing anchor
3. **Selection→text extraction**: `get_selected_text(value, anchor_offset, focus_offset)` — substring for clipboard operations
4. **Render order**: Selection highlight → text → caret (back to front)

### 8.6 Risk Assessment

**Medium risk.** The selection model is conceptually straightforward (two offsets into a string), but visual rendering of multi-line selection rectangles requires correct char→pixel mapping on each visual line. Reuses existing `SelectionState` patterns.

---

## 9. Phase 25: Copy / Cut / Paste / Delete with Selection

### 9.1 Current Clipboard State

- **Cmd+C (Copy)**: Implemented — copies selected text to system clipboard via `glfwSetClipboardString()`
- **Cmd+X (Cut)**: Partially implemented — copies to clipboard but has `// TODO: delete selected text`
- **Cmd+V (Paste)**: **NOT implemented** — key code exists but no handler
- **Delete/Backspace on selection**: **NOT implemented** — currently operates character-by-character

### 9.2 Paste Implementation

```cpp
// In event.cpp, KEY_DOWN handler — add Cmd+V:
case RDT_KEY_V:
    if (evcon->mod_super || evcon->mod_ctrl) {
        const char* clipboard = glfwGetClipboardString(window);
        if (clipboard && *clipboard) {
            // If selection exists, delete selected range first
            if (has_textarea_selection(state)) {
                delete_textarea_selection(state, &evcon);
            }
            // Insert clipboard text at caret position
            // Dispatch as synthetic "paste" event to Lambda handler
            dispatch_lambda_handler(&evcon, focus_elem, "paste",
                build_paste_event(clipboard, caret->char_offset));
        }
    }
    break;
```

Lambda handler for paste:
```lambda
on paste(evt) {
  let pos = evt.caret_pos
  let text = evt.text  // clipboard content
  ~.notes = slice(~.notes, 0, pos) ++ text ++ slice(~.notes, pos, len(~.notes))
}
```

### 9.3 Cut Implementation

Complete the existing cut stub:

```cpp
case RDT_KEY_X:
    if (evcon->mod_super || evcon->mod_ctrl) {
        if (has_textarea_selection(state)) {
            Str selected = get_selected_text(form->value, sel);
            glfwSetClipboardString(window, str_cstr(&selected));
            delete_textarea_selection(state, &evcon);
            // Dispatch "cut" event to Lambda handler with deleted range
        }
    }
    break;
```

### 9.4 Delete Selection on Backspace/Delete

```cpp
// In existing Backspace/Delete handler:
if (has_textarea_selection(state)) {
    // Delete entire selection range instead of single character
    delete_textarea_selection(state, &evcon);
    // Dispatch "delete_selection" or modified "keydown" to Lambda handler
    return;
}
// ... existing single-char delete logic
```

### 9.5 `delete_textarea_selection` Helper

```cpp
void delete_textarea_selection(RadiantState* state, EventContext* evcon) {
    int start = min(sel->anchor_offset, sel->focus_offset);
    int end = max(sel->anchor_offset, sel->focus_offset);
    
    // Update caret to start of deleted range
    caret->char_offset = start;
    recompute_caret_line_column(caret, form->value);
    
    // Clear selection
    sel->has_selection = false;
    sel->anchor_offset = sel->focus_offset = start;
    
    // The actual string mutation happens in the Lambda handler
    // Dispatch event with {start, end, caret_pos} so handler can splice
}
```

### 9.6 New Lambda Events

| Event | Fields | Purpose |
|-------|--------|---------|
| `paste` | `{text, caret_pos}` | Clipboard paste at caret |
| `cut` | `{start, end, text}` | Cut selection (handler deletes range) |
| `select_delete` | `{start, end}` | Backspace/Delete on selection |

Alternatively, reuse `keydown` with additional fields:
```lambda
on keydown(evt) {
  if (evt.key == "Backspace") {
    if (evt.selection_start != null) {
      // selection delete
      ~.notes = slice(~.notes, 0, evt.selection_start) ++ slice(~.notes, evt.selection_end, len(~.notes))
    } else {
      // single char delete
    }
  }
}
```

### 9.7 Risk Assessment

**Medium risk.** Paste requires `glfwGetClipboardString()` which returns the system clipboard — well-supported API. The main complexity is coordinating selection deletion + string splicing + caret repositioning between the engine (which manages caret/selection state) and Lambda handlers (which manage the actual text string). A clean event contract is critical.

---

## 10. Phase 26: Drag-and-Drop Between Categories

### 10.1 Application Behavior

1. User presses and holds on a todo item (≥150ms, or immediate with a drag handle icon)
2. Visual feedback: item "lifts" (shadow, slight scale, reduced opacity on original)
3. User drags the item over category headers
4. Drop target highlights when item is over a valid drop zone
5. User releases → item moves from source category to target category
6. If dropped outside any category → item returns to original position (cancel)

### 10.2 Engine Work: Element Drag Events

Currently, only `"click"` is dispatched to Lambda handlers. Drag-and-drop requires exposing mouse lifecycle events:

#### 10.2.1 New Lambda-Dispatched Events

| Engine Event | Lambda Event | When Dispatched |
|-------------|-------------|-----------------|
| `RDT_EVENT_MOUSE_DOWN` | `"mousedown"` | On press (after focus/caret handling) |
| `RDT_EVENT_MOUSE_MOVE` | `"mousemove"` | During move when `is_dragging` flag set |
| `RDT_EVENT_MOUSE_UP` | `"mouseup"` | On release (before `"click"`) |
| — | `"dragstart"` | Synthetic: after mousedown + 5px movement threshold |
| — | `"dragmove"` | Synthetic: during drag, includes current position + drop target |
| — | `"drop"` | Synthetic: on mouseup during drag, dispatched to drop target |
| — | `"dragend"` | Synthetic: cleanup, dispatched to drag source |

#### 10.2.2 Drag State

```cpp
// In RadiantState or new DragState:
typedef struct DragState {
    View* source_view;          // the view being dragged
    DomElement* source_elem;    // DOM element being dragged
    Item source_data;           // Lambda data for the dragged item
    float start_x, start_y;    // mousedown position
    float current_x, current_y; // current drag position
    bool is_dragging;           // true after movement threshold
    View* current_drop_target;  // view currently under cursor
    const char* drag_type;      // application-defined drag type (e.g., "todo-item")
} DragState;
```

#### 10.2.3 Drag Visual Feedback

During drag, render a semi-transparent copy of the source element at the cursor position:

```cpp
void render_drag_overlay(RenderContext* rdcon, DragState* drag) {
    if (!drag->is_dragging) return;
    
    // Save canvas state
    // Translate to cursor position offset
    float dx = drag->current_x - drag->start_x;
    float dy = drag->current_y - drag->start_y;
    
    rdcon->offset_x += dx;
    rdcon->offset_y += dy;
    rdcon->opacity = 0.7f;
    
    // Render the source element's view subtree
    render_block_view(rdcon, (ViewBlock*)drag->source_view);
    
    // Restore canvas state
    rdcon->offset_x -= dx;
    rdcon->offset_y -= dy;
    rdcon->opacity = 1.0f;
}
```

#### 10.2.4 Drop Target Hit Testing

During drag, each mousemove performs hit-testing to find the drop target:

```cpp
View* find_drop_target(DomDocument* doc, float x, float y, const char* drag_type) {
    // Walk the view tree, find deepest view at (x, y) that has
    // a "dropzone" attribute matching drag_type
    // Return NULL if no valid drop target
}
```

In Lambda templates, mark drop zones:
```lambda
<div class:"category-header", dropzone:"todo-item"; ~.name>
```

### 10.3 Lambda Template Integration

```lambda
edit <todo_list> state drag_over: false {
  let header_class = if (drag_over) "category-header drop-active" else "category-header"
  <div class:header_class, dropzone:"todo-item"
    <span ~.name>
  >
  // ... items ...
}
on drop(evt) {
  // evt.item contains the dragged todo item data
  // evt.source_list contains the source list name
  ~.items = ~.items ++ [evt.item]
}
on dragover(evt) {
  drag_over = true
}
on dragleave(evt) {
  drag_over = false
}
```

Source `<todo_item>` emits to parent on dragstart:
```lambda
on dragstart(evt) {
  emit("item_drag_start", {item: ~, list: parent_name})
}
```

Parent `<todo_app>` coordinates the cross-list move:
```lambda
on item_drag_start(evt) {
  drag_source = {item: evt.item, list: evt.list}
}
on drop(evt) {
  // Remove from source list, add to target list
  let source = find_list(~.lists, drag_source.list)
  source.items = for (i in source.items where i.id != drag_source.item.id) i
  let target = find_list(~.lists, evt.target_list)
  target.items = target.items ++ [drag_source.item]
}
```

### 10.4 Risk Assessment

**High risk.** This is the most complex engine feature:
- Mouse event dispatch to Lambda handlers during drag (frequent events, performance sensitive)
- Visual drag overlay rendering (translating a view subtree, opacity)
- Drop target hit-testing during move (must be fast — runs every mousemove)
- Coordination between engine drag state and Lambda template state
- Cancel behavior (Escape key, drop outside valid zone)

Mitigation strategies:
- Throttle `"dragmove"` dispatch to Lambda (max 1 per frame / 16ms)
- Use simple AABB hit-testing for drop targets (reuse existing event hit-test code)
- Start with a simpler drag indicator (colored line / insertion marker) instead of full element ghost
- Build incrementally: dragging detection first, then visual feedback, then drop handling

---

## 11. Data Model

### 11.1 Directory Structure

```
data/todo/
  work.json
  personal.json
  shopping.json
```

### 11.2 File Format

Each JSON file:
```json
{
  "name": "Work",
  "lists": [
    {
      "name": "In Progress",
      "items": [
        {
          "id": 1,
          "text": "Review pull requests",
          "notes": "Check the open PRs on GitHub.\nFocus on the API changes.",
          "done": false
        }
      ]
    },
    {
      "name": "Done",
      "items": [...]
    }
  ]
}
```

### 11.3 Changes from todo.ls Data Model

| Field | todo.ls | todo2.ls | Notes |
|-------|---------|----------|-------|
| `items[].notes` | — | `string` (multi-line) | New field for detailed text |
| `items[].id` | `int` | `int` | Unchanged, must be unique within file |
| Top-level `name` | Embedded in data | From filename | Displayed in file panel |
| Multiple files | — | Directory-based | Each file = one set of categories |

---

## 12. Implementation Order

| # | Phase | Task | Engine Work | App Work | Deps | Risk | Effort |
|---|-------|------|-------------|----------|------|------|--------|
| 1 | 20 | Two-column layout CSS + file list | None | `todo2.ls` skeleton, CSS | — | Low | M |
| 2 | 20 | File loading on click | None | `input()` integration | 1 | Low | S |
| 3 | 21 | `delete_file` / `rename_file` sys functions | 2 functions + registry | — | — | Low | S |
| 4 | 21 | File create / delete / rename UI | None | Templates + handlers | 1, 3 | Low | M |
| 5 | 22 | Inline "+" add item | None | Template state pattern | 1 | Low | S |
| 6 | 22 | Direct text editing (click to edit) | `blur` event dispatch | Template swap pattern | 1 | Low | S |
| 7 | 22 | Auto-focus on new `<input>` | Autofocus in rebuild | — | 6 | Low | S |
| 8 | 23 | Textarea text rendering | `render_textarea` rewrite | — | — | Medium | L |
| 9 | 23 | Textarea keyboard handling | event.cpp textarea dispatch | — | 8 | Medium | M |
| 10 | 23 | Multi-line caret (Up/Down/Home/End) | Caret line math | — | 9 | Medium | M |
| 11 | 23 | Textarea in todo2.ls | None | Notes field UI | 8-10, 5 | Low | S |
| 12 | 24 | Textarea text selection | Selection in form control | — | 8-10 | Medium | M |
| 13 | 24 | Selection rendering (highlight rects) | render_textarea_selection | — | 12 | Medium | M |
| 14 | 25 | Clipboard paste (Cmd+V) | `glfwGetClipboardString` | `paste` event handler | 9 | Medium | S |
| 15 | 25 | Cut completion + delete on selection | Selection deletion | Event fields | 12, 14 | Medium | S |
| 16 | 26 | Mouse event dispatch (down/move/up) | `dispatch_lambda_handler` × 3 | — | — | Medium | S |
| 17 | 26 | Drag detection + drag state | DragState, threshold | — | 16 | Medium | M |
| 18 | 26 | Drop target hit-testing | `dropzone` attr, hit-test | — | 17 | Medium | M |
| 19 | 26 | Drag visual feedback (overlay) | render_drag_overlay | — | 17 | High | M |
| 20 | 26 | Drag-and-drop in todo2.ls | — | Templates + handlers | 16-19, 1 | Medium | M |
| 21 | — | Integration test suite | — | Event test JSON | All | Low | M |

**Effort:** S = small (< half day), M = medium (1–2 days), L = large (2–4 days)

### Recommended Sprint Plan

**Sprint 1 — App Foundation (Low risk, no engine changes):**
- Phase 20: Two-column layout + file list + file loading
- Phase 21: File management (create/delete/rename)
- Phase 22: Inline add + direct text edit

**Sprint 2 — Rich Text Editing (Medium risk, core engine work):**
- Phase 23: Multi-line textarea (rendering + keyboard + caret)
- Phase 24: Text selection in textarea

**Sprint 3 — Clipboard + Drag (Medium-High risk):**
- Phase 25: Copy/Cut/Paste/Delete with selection
- Phase 26: Drag-and-drop between categories

---

## 13. Files Modified / Created

### New Files

| File | Purpose |
|------|---------|
| `test/lambda/ui/todo2.ls` | New todo application |
| `data/todo/work.json` | Default todo data file |
| `data/todo/personal.json` | Default todo data file |
| `test/ui/todo2_basic.json` | UI automation test — basic interactions |
| `test/ui/todo2_textarea.json` | UI automation test — textarea editing |
| `test/ui/todo2_dragdrop.json` | UI automation test — drag and drop |

### Modified Files (Engine)

| File | Phase | Change |
|------|-------|--------|
| `lambda/lambda-proc.cpp` | 21 | `pn_delete_file()`, `pn_rename_file()` |
| `lambda/sys_func_registry.c` | 21 | Register `delete_file`, `rename_file` |
| `radiant/event.cpp` | 22, 23, 25, 26 | `blur` dispatch; textarea key handling; paste handler; mousedown/move/up dispatch; drag detection |
| `radiant/render_form.cpp` | 23, 24 | Textarea text rendering; selection highlight rendering |
| `radiant/form_control.hpp` | 23, 24 | TextareaState (scroll_y, selection offsets) |
| `radiant/cmd_layout.cpp` | 22 | Autofocus on newly inserted `<input>` |
| `radiant/state_store.hpp` | 26 | `DragState` struct |
| `radiant/state_store.cpp` | 26 | DragState initialization |
| `radiant/render.cpp` | 26 | Drag overlay rendering |

---

## 14. Test Plan

### 14.1 Unit Tests

| Test | Covers |
|------|--------|
| `test_textarea_wrap` | Line wrapping at various widths |
| `test_textarea_caret` | Caret positioning: line/column calculation, Up/Down/Home/End |
| `test_textarea_selection` | Selection range from Shift+arrow, click-drag |
| `test_clipboard_paste` | Paste insertion at caret, replacing selection |
| `test_drag_hit_test` | Drop target detection from coordinates |

### 14.2 Integration Tests (Event JSON)

**`todo2_basic.json`** — 50+ events:
1. Assert initial file list rendered
2. Click file → assert right panel loads
3. Toggle item → assert checkbox changes
4. Click "+" → assert new item appears in edit mode
5. Type text + Enter → assert item added
6. Click item text → assert edit mode
7. Type new text + Enter → assert text updated
8. Click delete × → assert item removed
9. Create new file → assert appears in file list
10. Delete file → assert removed from file list

**`todo2_textarea.json`** — 40+ events:
1. Click item to edit → assert textarea visible
2. Type multi-line text (with Enter for newlines)
3. Arrow key navigation between lines
4. Shift+arrow selection
5. Cmd+C copy → paste elsewhere
6. Select text + Backspace → assert deleted
7. Escape → assert edit mode closed, text saved

**`todo2_dragdrop.json`** — 20+ events:
1. Mousedown on item → drag to category header → release
2. Assert item moved to target category
3. Assert source category no longer has item
4. Drag and cancel (drop outside) → assert item returns
5. Drag within same category (reorder)

### 14.3 Regression

All existing tests must continue to pass:
- `make test-lambda-baseline` — 562/562
- `make test-radiant-baseline` — 3713/3716 (3 pre-existing)
- `todo_perf_timing.json` — 57 events, 7 assertions
