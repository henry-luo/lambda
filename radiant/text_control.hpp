#pragma once

// Phase 6E text-control helpers shared between:
//   - lambda/js/js_dom.cpp    (programmatic API: value, selectionStart/End, ...)
//   - radiant/event.cpp       (mouse/keyboard editing)
//   - radiant/render_form.cpp (caret + selection highlight inside <input>/<textarea>)
//
// See vibe/radiant/Radiant_Design_Selection.md §8.

#include <stdint.h>
#include <stddef.h>

class DomElement;
struct FormControlProp;
struct RadiantState;

// Identification ---------------------------------------------------------

// Treat element as a text control (text-like <input> or <textarea>) without
// requiring Radiant layout to have populated elem->form. Mirrors HTML §4.10.5.1.
bool tc_is_text_control(DomElement* elem);

// Lazy-allocate FormControlProp + control_type for JS-only paths
// (script may run on a parsed DOM with no layout).
FormControlProp* tc_get_or_create_form(DomElement* elem);

// UTF-8 ↔ UTF-16 conversion ----------------------------------------------
// Surrogate pair = 2 UTF-16 code units for codepoints >= U+10000.

uint32_t tc_utf8_to_utf16_length(const char* s, uint32_t byte_len);
uint32_t tc_utf16_to_utf8_offset(const char* s, uint32_t byte_len, uint32_t u16);
uint32_t tc_utf8_to_utf16_offset(const char* s, uint32_t byte_len, uint32_t u8);

// Lazy initialization + writes -------------------------------------------

// Initialize current_value from HTML default (input's value attr / textarea
// children) and collapse selection at end-of-text per HTML §4.10.6.
void tc_ensure_init(DomElement* elem);

// Set the live value. Selection collapses to the new end. Also updates the
// legacy form->value pointer used by render_form.cpp.
void tc_set_value(DomElement* elem, const char* new_val, size_t new_len);

// Clamp + write a (start, end, dir) triple. dir: 0=none, 1=forward, 2=backward.
void tc_set_selection_range(DomElement* elem,
                            uint32_t start, uint32_t end,
                            uint8_t dir);

// Phase 8E: queue a `selectionchange` event on this text control. Coalesced
// per-element via FormControlProp::tc_sc_pending; dispatched as a microtask
// (setTimeout(0)) by the JS-side strong impl.
void tc_notify_selection_changed(DomElement* elem);

// Bidirectional sync with legacy CaretState/SelectionState ---------------
// (Phase 6E. The legacy state still owns the visual caret position; the
// new form->selection_* mirrors it for JS observability.)

// Read state->caret + state->selection (UTF-8 byte offsets into form->value)
// and write form->selection_start/_end/_direction (UTF-16 code units).
// Called at the end of every text-control mouse/keyboard handler in event.cpp.
void tc_sync_legacy_to_form(DomElement* elem, RadiantState* state);

// Read form->selection_start/_end (UTF-16) and write state->caret +
// state->selection (byte). Called from JS when setSelectionRange / value
// setter mutates the control programmatically.
void tc_sync_form_to_legacy(DomElement* elem, RadiantState* state);

// Selection accessor for Selection.toString() integration ----------------

// Returns the active text control's selected substring (UTF-8) with its
// length in bytes, or nullptr / 0 when no text control is focused or the
// selection is empty. Result is a pointer into form->current_value valid
// until the next mutation; copy if the caller needs to persist it.
const char* tc_active_selected_text(uint32_t* out_byte_len);

// Focus tracking (used by document.activeElement and Selection.toString) -

void tc_set_active_element(DomElement* elem);
DomElement* tc_get_active_element(void);
void tc_set_last_focused_text_control(DomElement* elem);
DomElement* tc_get_last_focused_text_control(void);
void tc_reset_focus_state(void);

// Direct slot accessors (used by JS bindings to keep `slot = elem` syntax).
DomElement** tc_active_element_slot(void);
DomElement** tc_last_focused_text_control_slot(void);
