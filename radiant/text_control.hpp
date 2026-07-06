#pragma once

// Phase 6E text-control helpers shared between:
//   - lambda/js/js_dom.cpp    (programmatic API: value, selectionStart/End, ...)
//   - radiant/event.cpp       (mouse/keyboard editing)
//   - radiant/render_form.cpp (caret + selection highlight inside <input>/<textarea>)
//
// See vibe/radiant/Radiant_Design_Selection.md §8.

#include <stdint.h>
#include <stddef.h>

struct DomElement;
struct FormControlProp;
struct DocState;

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

// Text-control selection projection sync ---------------------------------
// StateStore's EditingSelection is canonical for text controls.
// form->selection_* remains the HTML-facing mirror for JS observability;
// StateStore projection fields preserve existing renderer and event helper
// contracts.

// Publish the active text-control selection into the form mirror. Older event
// paths can still call this after projection-cache changes; when state->sel
// already targets the control, it is treated as source of truth.
void tc_sync_selection_to_form(DomElement* elem, DocState* state);

// Selection accessor for Selection.toString() integration ----------------

// Returns the active text control's selected substring (UTF-8) with its
// length in bytes, or nullptr / 0 when no text control is focused or the
// selection is empty. Result is a pointer into form->current_value valid
// until the next mutation; copy if the caller needs to persist it.
const char* tc_active_selected_text(DocState* state, uint32_t* out_byte_len);

// Focus tracking (used by document.activeElement and Selection.toString) -

void tc_set_active_element(DocState* state, DomElement* elem);
DomElement* tc_get_active_element(DocState* state);
void tc_set_last_focused_text_control(DocState* state, DomElement* elem);
DomElement* tc_get_last_focused_text_control(DocState* state);
void tc_reset_focus_state(DocState* state);
