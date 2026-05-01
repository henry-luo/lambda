#pragma once

// F1/F2 text-control editing helpers — see vibe/radiant/Radiant_Design_Form_Input.md.
//
// Layered above text_control.hpp / form_control.hpp. Provides:
//   - word/line boundary detection on UTF-8 buffers.
//   - dblclick / tripleclick selection helpers (F2).
//   - focus snapshot + change-event commit logic (F1 §3.1).
//   - undo/redo ring skeleton (F1 §3.2).
//
// Caret/selection offsets here use UTF-8 *byte* indices, matching the legacy
// CaretState/SelectionState used in radiant/event.cpp. The companion
// tc_set_selection_range path in text_control.hpp uses UTF-16 code units;
// the bidirectional sync (tc_sync_legacy_to_form / tc_sync_form_to_legacy)
// keeps the two views consistent.

#include <stdint.h>
#include <stddef.h>

class DomElement;
struct FormControlProp;
struct RadiantState;

// ---------- word / line boundary ---------------------------------------

// Scan UTF-8 buffer for the start/end of the word containing `byte_off`.
// Treats ASCII alphanumeric, '_' and any byte >= 0x80 (non-ASCII) as word
// characters; everything else is a separator. End is exclusive.
//
// If `byte_off` lies on a separator, both helpers return `byte_off` so the
// caller can distinguish "no word at this position" from a real word.
uint32_t te_word_start(const char* buf, uint32_t buf_len, uint32_t byte_off);
uint32_t te_word_end  (const char* buf, uint32_t buf_len, uint32_t byte_off);

// For textarea: start of the logical line containing byte_off (offset just
// after the previous '\n', or 0). End is the position of the next '\n', or
// buf_len. Endpoints are byte offsets.
uint32_t te_line_start(const char* buf, uint32_t buf_len, uint32_t byte_off);
uint32_t te_line_end  (const char* buf, uint32_t buf_len, uint32_t byte_off);

// ---------- selection helpers (F2) -------------------------------------

// Apply the (start, end) byte range as a selection to the legacy
// CaretState/SelectionState owned by `state`, anchored on `target` view.
// Caret moves to `end`. Returns true on success.
//
// `target` must be the view associated with the text control (typically
// the ViewBlock for an <input>/<textarea>; or the ViewText that holds the
// content for static text under a text node).
bool te_apply_byte_range(RadiantState* state, void* target,
                         uint32_t start, uint32_t end);

// Select the word containing byte_off in the form's current_value (or
// `value` fallback). Returns false when not a text control or no word at
// position.
bool te_select_word_at(DomElement* elem, RadiantState* state,
                       void* target, uint32_t byte_off);

// Select the logical line containing byte_off (textarea), or the entire
// value (single-line <input>).
bool te_select_line_at(DomElement* elem, RadiantState* state,
                       void* target, uint32_t byte_off);

// Select all text in the control.
bool te_select_all(DomElement* elem, RadiantState* state, void* target);

// ---------- F3: word-granularity navigation ----------------------------

// Walk to the previous/next word boundary using a "Unix" rule: skip over
// any run of separators adjacent to byte_off, then skip over the run of
// word characters. The returned offset is on a UTF-8 boundary.
//
// te_prev_word_byte:  byte_off → first byte of the word that begins to its
//                     left (or 0 when none).
// te_next_word_byte:  byte_off → first byte AFTER the word that ends to
//                     its right (or buf_len when none).
uint32_t te_prev_word_byte(const char* buf, uint32_t buf_len, uint32_t byte_off);
uint32_t te_next_word_byte(const char* buf, uint32_t buf_len, uint32_t byte_off);

// ---------- F3: range-based mutation -----------------------------------

// Replace bytes [start, end) in the form's current_value with `repl`.
// Updates the buffer via tc_set_value (which handles legacy/JS sync), then
// places the caret at `start + repl_len`, clears any active selection, and
// pushes a history entry. Dispatches "input" via the legacy event bus.
//
// `repl` may be NULL with repl_len=0 to perform a pure deletion. Returns
// false if `elem` is not a text control or the range is invalid.
bool te_replace_byte_range(DomElement* elem, RadiantState* state, void* target,
                           uint32_t start, uint32_t end,
                           const char* repl, uint32_t repl_len);

// ---------- change-event commit (F1 §3.1) ------------------------------

// Snapshot current_value into form->value_at_focus. Idempotent. Call from
// update_focus_state() when a text control gains focus.
void te_focus_capture_value(DomElement* elem);

// Compare current_value against value_at_focus; returns true if they differ
// (i.e. the caller should dispatch a `change` event before clearing focus).
// Always clears the snapshot afterwards.
bool te_blur_should_dispatch_change(DomElement* elem);

// ---------- undo/redo ring (skeleton — used by future F4) --------------

struct EditHistoryEntry {
    char*    snapshot;        // UTF-8 value copy (malloc'd)
    uint32_t length;          // bytes
    uint32_t sel_start_u16;
    uint32_t sel_end_u16;
    uint8_t  sel_dir;         // 0 none, 1 fwd, 2 bwd
};

struct EditHistory {
    EditHistoryEntry* ring;   // bounded ring (cap entries)
    uint16_t          head;   // newest+1 (write index)
    uint16_t          count;  // valid entries
    uint16_t          cursor; // 0 = at newest; N = N undos back
    uint16_t          cap;    // ring capacity
};

EditHistory* te_history_new(uint16_t cap);
void         te_history_free(EditHistory* h);

// Push a snapshot of the current state. No-op if it equals the most recent
// entry (deduplication). Drops the oldest when full.
void te_history_push(DomElement* elem);

// Move cursor backward/forward through the ring; restores value + selection.
// Returns false if no further undo/redo is available.
bool te_history_undo(DomElement* elem);
bool te_history_redo(DomElement* elem);
