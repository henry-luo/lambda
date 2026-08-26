// F1/F2 text-control editing helpers — see vibe/radiant/Radiant_Design_Form_Input.md
//
// Build target: linked into all Radiant builds via the radiant/ glob in
// build_lambda_config.json.

#include "event.hpp"

#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <stdint.h>
#include <string.h>
#include <strings.h>    // strcasecmp (F5 input type checks)

// F4: tc_set_value pushes a history snapshot on every mutation. To prevent
// undo/redo restores from re-pushing (and corrupting the cursor), they
// bracket their tc_set_value call with this guard.
extern "C" void tc_history_guard_enter(DocState* state);
extern "C" void tc_history_guard_exit(DocState* state);

extern "C" __attribute__((weak)) void radiant_text_edit_history_notify(
    DomElement* elem, const char* action, const char* input_type,
    uint32_t depth, uint32_t cursor);
extern "C" __attribute__((weak)) void radiant_text_edit_history_notify(
    DomElement* /*elem*/, const char* /*action*/, const char* /*input_type*/,
    uint32_t /*depth*/, uint32_t /*cursor*/) {}

// Default ring capacity for the undo history. 64 entries is enough for
// typical interactive editing sessions while bounding worst-case memory.
static constexpr uint16_t TE_HISTORY_DEFAULT_CAP = 64;

// ---------- internals --------------------------------------------------

namespace {

// Resolve the live editable buffer + length from a text control's
// FormControlProp. Prefers `current_value` (the live IDL `.value`); falls
// back to the legacy HTML attribute mirror in `value`.
static const char* tc_buffer(FormControlProp* f, uint32_t* out_len) {
    if (!f) { if (out_len) *out_len = 0; return nullptr; }
    if (f->current_value) {
        if (out_len) *out_len = f->current_value_len;
        return f->current_value;
    }
    const char* v = f->value;
    if (out_len) *out_len = v ? (uint32_t)strlen(v) : 0;
    return v;
}

static bool te_is_password_control(DomElement* elem) {
    return elem && elem->form && elem->form->input_type &&
        strcasecmp(elem->form->input_type, "password") == 0;
}

static uint32_t te_last_codepoint_start(const char* text, uint32_t len) {
    if (!text || len == 0) return 0;
    uint32_t last = 0;
    uint32_t i = 0;
    while (i < len) {
        last = i;
        unsigned char b = (unsigned char)text[i];
        uint32_t step = 1;
        if (b >= 0xF0) step = 4;
        else if (b >= 0xE0) step = 3;
        else if (b >= 0xC0) step = 2;
        if (i + step > len) step = 1;
        i += step;
    }
    return last;
}

static void te_password_reveal_update(DomElement* elem,
                                      uint32_t insert_start,
                                      const char* repl,
                                      uint32_t repl_len,
                                      uint32_t new_len) {
    if (!elem || !elem->form || !te_is_password_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!repl || repl_len == 0) {
        te_password_reveal_clear(elem);
        return;
    }

    uint32_t last = te_last_codepoint_start(repl, repl_len);
    f->password_reveal_start = insert_start + last;
    f->password_reveal_end = insert_start + repl_len;
    if (f->password_reveal_start > new_len) f->password_reveal_start = new_len;
    if (f->password_reveal_end > new_len) f->password_reveal_end = new_len;
    f->password_reveal_active = f->password_reveal_start < f->password_reveal_end ? 1 : 0;
    f->password_reveal_elapsed = 0.0;
}

// ASCII-fast path word-character classifier. Treats letter/digit/underscore
// as word; treats any non-ASCII byte (>= 0x80) as word too, which avoids a
// full UCD lookup and matches browser dblclick behavior for most scripts.
static inline bool te_is_word_byte(unsigned char b) {
    if (b >= 0x80) return true;
    if (b >= '0' && b <= '9') return true;
    if (b >= 'A' && b <= 'Z') return true;
    if (b >= 'a' && b <= 'z') return true;
    return b == '_';
}

static inline uint32_t clamp_off(uint32_t off, uint32_t len) {
    return off > len ? len : off;
}

static bool te_prepare_scan(const char* buf, uint32_t buf_len, uint32_t byte_off,
                            uint32_t* out_offset) {
    *out_offset = 0;
    if (!buf || buf_len == 0) return false;
    *out_offset = clamp_off(byte_off, buf_len);
    return true;
}

} // namespace

// ---------- word boundary ----------------------------------------------

bool te_password_reveal_clear(DomElement* elem) {
    if (!elem || !elem->form || !te_is_password_control(elem)) return false;
    FormControlProp* f = elem->form;
    bool changed = f->password_reveal_active != 0 ||
        f->password_reveal_start != 0 ||
        f->password_reveal_end != 0 ||
        f->password_reveal_elapsed != 0.0;
    f->password_reveal_active = 0;
    f->password_reveal_start = 0;
    f->password_reveal_end = 0;
    f->password_reveal_elapsed = 0.0;
    return changed;
}

uint32_t te_word_start(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    if (!te_prepare_scan(buf, buf_len, byte_off, &byte_off)) return 0;

    // If the position itself is on (or past) a separator, snap left to the
    // first preceding word byte. If none exists, return byte_off unchanged
    // so the caller treats it as "no word here".
    uint32_t i = byte_off;
    while (i > 0 && !te_is_word_byte((unsigned char)buf[i - 1])) {
        if (i == byte_off) {
            // Cursor is on a separator AND no word byte to the left —
            // report no expansion.
            // (We continue scanning to confirm.)
        }
        i--;
    }
    if (i == 0 && (byte_off == 0 || !te_is_word_byte((unsigned char)buf[0]))) {
        // No word character anywhere to the left of byte_off.
        // Check whether byte_off itself starts a word.
        if (byte_off < buf_len && te_is_word_byte((unsigned char)buf[byte_off])) {
            return byte_off;
        }
        return byte_off;  // caller treats start==end as "no word".
    }
    // i is now positioned just after a word byte (or 0 with buf[0] word).
    while (i > 0 && te_is_word_byte((unsigned char)buf[i - 1])) i--;
    return i;
}

uint32_t te_word_end(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    uint32_t i;
    if (!te_prepare_scan(buf, buf_len, byte_off, &i)) return 0;
    // If byte_off is on a separator, don't extend.
    if (i < buf_len && !te_is_word_byte((unsigned char)buf[i])) {
        // But if there's a word byte immediately to the left (i.e. caret
        // sits at the right edge of a word), expand from there instead.
        if (i > 0 && te_is_word_byte((unsigned char)buf[i - 1])) {
            // already inside word for the start helper; nothing to do here.
            return i;
        }
        return i;
    }
    while (i < buf_len && te_is_word_byte((unsigned char)buf[i])) i++;
    return i;
}

// ---------- line boundary ----------------------------------------------

uint32_t te_line_start(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    uint32_t i;
    if (!te_prepare_scan(buf, buf_len, byte_off, &i)) return 0;
    while (i > 0 && buf[i - 1] != '\n') i--;
    return i;
}

uint32_t te_line_end(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    uint32_t i;
    if (!te_prepare_scan(buf, buf_len, byte_off, &i)) return 0;
    while (i < buf_len && buf[i] != '\n') i++;
    return i;
}

bool te_apply_byte_range(DocState* state, void* target,
                         uint32_t start, uint32_t end) {
    if (!state || !target) return false;
    if (end < start) { uint32_t t = start; start = end; end = t; }
    View* view = (View*)target;
    state_store_selection_start_pointer(state, view, (int)start);
    state_store_selection_extend_to_offset(state, (int)end);
    state_store_caret_collapse_to_view_offset(state, view, (int)end);
    selection_finish_active_gesture(state);
    log_debug("text_edit: applied selection bytes=[%u..%u] view=%p",
              start, end, view);
    return true;
}

// ---------- F3: word-granularity navigation ----------------------------

uint32_t te_prev_word_byte(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    uint32_t i;
    if (!te_prepare_scan(buf, buf_len, byte_off, &i)) return 0;
    // Skip separators directly to the left.
    while (i > 0 && !te_is_word_byte((unsigned char)buf[i - 1])) i--;
    // Skip the contiguous run of word bytes.
    while (i > 0 &&  te_is_word_byte((unsigned char)buf[i - 1])) i--;
    return i;
}

uint32_t te_next_word_byte(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    uint32_t i;
    if (!te_prepare_scan(buf, buf_len, byte_off, &i)) return 0;
    // Skip separators directly to the right.
    while (i < buf_len && !te_is_word_byte((unsigned char)buf[i])) i++;
    // Skip the contiguous run of word bytes.
    while (i < buf_len &&  te_is_word_byte((unsigned char)buf[i])) i++;
    return i;
}

// ---------- F3: range-based mutation -----------------------------------

bool te_replace_byte_range(DomElement* elem, DocState* state, void* target,
                           uint32_t start, uint32_t end,
                           const char* repl, uint32_t repl_len) {
    if (!elem || !state || !target) return false;

    bool ok = te_replace_byte_range_no_events(elem, state, target,
                                              start, end, repl, repl_len);
    if (!ok) return false;

    // Legacy callers without an EventContext cannot synthesize cancellable
    // beforeinput. Live editing routes through dispatch_form_text_replace().
    te_dispatch_input(elem);
    return true;
}

bool te_replace_byte_range_no_events(DomElement* elem, DocState* state, void* target,
                                     uint32_t start, uint32_t end,
                                     const char* repl, uint32_t repl_len) {
    if (!elem || !state || !target) return false;
    if (!tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    uint32_t old_len = 0;
    const char* old_buf = tc_buffer(f, &old_len);
    if (!old_buf && old_len > 0) return false;
    if (start > end) { uint32_t t = start; start = end; end = t; }
    if (end > old_len) end = old_len;
    if (start > old_len) start = old_len;

    // Build new buffer: old[0..start) + repl[0..repl_len) + old[end..old_len)
    uint32_t new_len = (old_len - (end - start)) + repl_len;
    char* nbuf = (char*)mem_alloc((size_t)new_len + 1, MEM_CAT_TEMP);
    if (!nbuf) return false;
    if (start > 0)            memcpy(nbuf,             old_buf,           start);
    if (repl_len > 0 && repl) memcpy(nbuf + start,     repl,              repl_len);
    if (end < old_len)        memcpy(nbuf + start + repl_len,
                                     old_buf + end,
                                     old_len - end);
    nbuf[new_len] = '\0';

    tc_set_value(elem, nbuf, new_len);
    mem_free(nbuf);
    te_password_reveal_update(elem, start, repl, repl_len, new_len);

    // Place caret at end of inserted text and clear any selection.
    uint32_t new_caret = start + repl_len;
    if (selection_has_projection(state)) state_store_selection_clear(state);
    state_store_caret_collapse_to_view_offset(state, (View*)target, (int)new_caret);
    // tc_set_value() temporarily collapses text controls at value-end; publish
    // the replacement caret afterward so fallback reflow keeps the live cursor.
    tc_sync_selection_to_form(elem, state);

    // tc_set_value already pushed an undo entry; just notify selection
    // observers and we're done.
    tc_notify_selection_changed(elem);

    log_debug("text_edit: replace_byte_range elem=%p [%u..%u) repl_len=%u new_len=%u",
              elem, start, end, repl_len, new_len);
    return true;
}

// ---------- change-event commit (F1 §3.1) ------------------------------

void te_focus_capture_value(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return;
    // Lazy-init so newly-focused, never-edited controls still snapshot
    // their initial value.
    tc_ensure_init(elem);
    FormControlProp* f = elem->form;
    if (!f) return;

    uint32_t blen = 0;
    const char* buf = tc_buffer(f, &blen);

    // Free previous snapshot (if any) and replace with a fresh copy.
    if (f->value_at_focus) { mem_free(f->value_at_focus); f->value_at_focus = nullptr; }
    f->value_at_focus_len = 0;
    if (buf) {
        f->value_at_focus = (char*)mem_alloc((size_t)blen + 1, MEM_CAT_DOM);
        if (f->value_at_focus) {
            if (blen) memcpy(f->value_at_focus, buf, blen);
            f->value_at_focus[blen] = '\0';
            f->value_at_focus_len = blen;
        }
    }
    log_debug("text_edit: focus_capture elem=%p len=%u", elem, blen);

    // F4: also seed the undo history with the at-focus state so the user
    // can undo back to what was originally there. te_history_push dedupes,
    // so calling this on every focus is safe.
    te_history_push(elem);
}

bool te_blur_should_dispatch_change(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    if (!f) return false;

    uint32_t cur_len = 0;
    const char* cur = tc_buffer(f, &cur_len);

    bool changed = false;
    if (f->value_at_focus) {
        if (cur_len != f->value_at_focus_len) {
            changed = true;
        } else if (cur_len > 0) {
            changed = (memcmp(cur, f->value_at_focus, cur_len) != 0);
        }
    } else {
        // No snapshot — first blur after init. Treat as no-change so we
        // don't spuriously fire `change` on every focus transition.
        changed = false;
    }

    // Always clear the snapshot; the next focus will re-capture.
    if (f->value_at_focus) { mem_free(f->value_at_focus); f->value_at_focus = nullptr; }
    f->value_at_focus_len = 0;

    log_debug("text_edit: blur_should_dispatch_change elem=%p changed=%d",
              elem, (int)changed);
    return changed;
}

// ---------- undo/redo ring (skeleton) ----------------------------------

EditHistory* te_history_new(uint16_t cap) {
    EditHistory* h = (EditHistory*)mem_calloc(1, sizeof(EditHistory), MEM_CAT_DOM);
    if (!h) return nullptr;
    if (!h->init(cap)) {
        mem_free(h);
        return nullptr;
    }
    return h;
}

void te_history_free(EditHistory* h) {
    if (!h) return;
    h->destroy();
    mem_free(h);
}

bool EditHistory::init(uint16_t capacity) {
    if (capacity == 0) capacity = TE_HISTORY_DEFAULT_CAP;
    ring = (EditHistoryEntry*)mem_calloc(capacity, sizeof(EditHistoryEntry), MEM_CAT_DOM);
    if (!ring) return false;
    cap = capacity;
    return true;
}

void EditHistory::destroy() {
    if (ring) {
        for (uint16_t i = 0; i < cap; i++) {
            if (ring[i].snapshot) mem_free(ring[i].snapshot);
        }
        mem_free(ring);
        ring = nullptr;
    }
    cap = 0;
    head = 0;
    count = 0;
    cursor = 0;
}

static DocState* te_history_state(DomElement* elem);

// The ring lives on the form ViewState in the State Store, not on
// FormControlProp: the prop is released and rebuilt as views churn, which used
// to take undo history with it (ESO43). The ViewState is keyed by the element
// and already holds the rest of the canonical form state.
static EditHistory* te_history_of(DomElement* elem) {
    DocState* state = te_history_state(elem);
    if (!state) return nullptr;
    return (EditHistory*)form_control_history_get(state, (View*)elem);
}

static EditHistory* tc_get_or_create_history(DomElement* elem) {
    DocState* state = te_history_state(elem);
    if (!state) return nullptr;
    EditHistory* h = (EditHistory*)form_control_history_get(state, (View*)elem);
    if (!h) {
        h = te_history_new(TE_HISTORY_DEFAULT_CAP);
        if (!h) return nullptr;
        form_control_history_set(state, (View*)elem, h);
    }
    return h;
}

static DocState* te_history_state(DomElement* elem) {
    if (!elem) return nullptr;
    if (elem->form && elem->form->state_ref) return elem->form->state_ref;
    return elem->doc ? (DocState*)elem->doc->state : nullptr;
}

const char* te_history_input_type_set(DocState* state, const char* input_type) {
    if (!state) return nullptr;
    const char* previous = state->text_edit_history_input_type;
    state->text_edit_history_input_type = input_type;
    return previous;
}

void te_history_input_type_restore(DocState* state, const char* previous) {
    if (state) state->text_edit_history_input_type = previous;
}

void te_history_push(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;
    EditHistory* h = tc_get_or_create_history(elem);
    if (!h || !h->ring || h->cap == 0) return;

    uint32_t blen = 0;
    const char* buf = tc_buffer(f, &blen);

    // Drop redo-tail: any entries newer than the cursor become unreachable.
    if (h->cursor > 0) {
        h->count -= h->cursor;
        h->head   = (uint16_t)((h->head + h->cap - h->cursor) % h->cap);
        h->cursor = 0;
    }

    // Dedupe against the most recent entry.
    if (h->count > 0) {
        uint16_t newest = (uint16_t)((h->head + h->cap - 1) % h->cap);
        EditHistoryEntry* prev = &h->ring[newest];
        if (prev->length == blen &&
            (blen == 0 || (prev->snapshot && memcmp(prev->snapshot, buf, blen) == 0)) &&
            prev->sel_start_u16 == f->selection_start &&
            prev->sel_end_u16   == f->selection_end &&
            prev->sel_dir       == f->selection_direction) {
            return;
        }
    }

    EditHistoryEntry* slot = &h->ring[h->head];
    if (slot->snapshot) { mem_free(slot->snapshot); slot->snapshot = nullptr; }
    slot->length = blen;
    slot->snapshot = (char*)mem_alloc((size_t)blen + 1, MEM_CAT_DOM);
    if (!slot->snapshot) return;
    if (blen) memcpy(slot->snapshot, buf, blen);
    slot->snapshot[blen] = '\0';
    slot->sel_start_u16 = f->selection_start;
    slot->sel_end_u16   = f->selection_end;
    slot->sel_dir       = f->selection_direction;

    h->head = (uint16_t)((h->head + 1) % h->cap);
    if (h->count < h->cap) h->count++;
    DocState* state = te_history_state(elem);
    radiant_text_edit_history_notify(elem, "push",
                                     state ? state->text_edit_history_input_type : nullptr,
                                     h->count, h->cursor);
}

static bool te_history_apply_current(DomElement* elem, EditHistory* history) {
    uint16_t index = (uint16_t)((history->head + history->cap - 1 - history->cursor) % history->cap);
    EditHistoryEntry* entry = &history->ring[index];
    if (!entry->snapshot) return false;

    DocState* state = te_history_state(elem);
    tc_history_guard_enter(state);
    tc_set_value(elem, entry->snapshot, entry->length);
    tc_history_guard_exit(state);
    tc_set_selection_range(elem, entry->sel_start_u16, entry->sel_end_u16, entry->sel_dir);
    return true;
}

// ES17 option 2: hand the entry to whoever will install it, without moving the
// cursor. The cursor advances only once the entry is actually consumed, which
// may be the template (which applies it and prevents) or the native path below
// — a beforeinput cancelled by JS must leave both the value and the cursor
// exactly where they were.
bool te_history_peek(DomElement* elem, bool redo, const char** out_value,
                     uint32_t* out_len, uint32_t* out_sel_start_u16,
                     uint32_t* out_sel_end_u16) {
    if (out_value) *out_value = nullptr;
    if (out_len) *out_len = 0;
    if (out_sel_start_u16) *out_sel_start_u16 = 0;
    if (out_sel_end_u16) *out_sel_end_u16 = 0;
    if (!elem || !tc_is_text_control(elem)) return false;
    EditHistory* h = te_history_of(elem);
    if (!h) return false;

    // Mirror the bounds te_history_undo/redo enforce, one step ahead of the
    // cursor rather than moving it.
    uint16_t cursor;
    if (redo) {
        if (h->cursor == 0) return false;
        cursor = (uint16_t)(h->cursor - 1);
    } else {
        if ((uint16_t)(h->cursor + 1) >= h->count) return false;
        cursor = (uint16_t)(h->cursor + 1);
    }
    uint16_t index = (uint16_t)((h->head + h->cap - 1 - cursor) % h->cap);
    EditHistoryEntry* entry = &h->ring[index];
    if (!entry->snapshot) return false;
    if (out_value) *out_value = entry->snapshot;
    if (out_len) *out_len = entry->length;
    if (out_sel_start_u16) *out_sel_start_u16 = entry->sel_start_u16;
    if (out_sel_end_u16) *out_sel_end_u16 = entry->sel_end_u16;
    return true;
}

// Move the cursor onto the entry te_history_peek reported, without touching the
// value: used when a template installed that entry itself.
bool te_history_step(DomElement* elem, bool redo) {
    if (!elem || !tc_is_text_control(elem)) return false;
    EditHistory* h = te_history_of(elem);
    if (!h) return false;
    if (redo) {
        if (h->cursor == 0) return false;
        h->cursor--;
    } else {
        if ((uint16_t)(h->cursor + 1) >= h->count) return false;
        h->cursor++;
    }
    return true;
}

bool te_history_undo(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return false;
    EditHistory* h = te_history_of(elem);
    if (!h) return false;

    // Need at least one older entry beyond the current state. The newest
    // entry typically represents the current state; cursor++ moves to the
    // one before it.
    if ((uint16_t)(h->cursor + 1) >= h->count) return false;
    h->cursor++;
    return te_history_apply_current(elem, h);
}

bool te_history_redo(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return false;
    EditHistory* h = te_history_of(elem);
    if (!h) return false;

    if (h->cursor == 0) return false;
    h->cursor--;
    return te_history_apply_current(elem, h);
}

// ---------- F5: events + constraint validation -------------------------

// Weak hook for legacy callers that still mutate a text control without an
// EventContext. Cancellable beforeinput is owned by editing_dispatch.cpp.
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_input(DomElement* elem);
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_input(DomElement* /*elem*/) {}

void te_dispatch_input(DomElement* elem) {
    if (!elem) return;
    js_dom_queue_textcontrol_input(elem);
}

// ---------- paste: range only ------------------------------------------

// Resolve the buffer range a paste replaces — the current selection, in bytes.
//
// Sanitization and the maxlength clamp used to live here. Both are policy, and
// policy now sits in the dom package's applier (F6), which is handed the raw
// clipboard text and decides what actually goes in — including the textarea
// CR/CRLF normalization this used to own. What remains is mechanism: the
// permission gate and the range.
bool te_prepare_paste_range(DomElement* elem, DocState* state,
                            uint32_t* out_start, uint32_t* out_end) {
    if (out_start) *out_start = 0;
    if (out_end) *out_end = 0;
    if (!elem || !state || !out_start || !out_end) return false;
    if (!tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    // one predicate for "the user may not alter this control" — the readonly
    // attribute or disabled, which HTML treats alike for editing (F6)
    if (!f || form_control_is_user_readonly(state, (View*)elem)) return false;

    uint32_t cur_len = 0;
    const char* cur_buf = tc_buffer(f, &cur_len);
    uint32_t sel_a = tc_utf16_to_utf8_offset(cur_buf, cur_len, f->selection_start);
    uint32_t sel_b = tc_utf16_to_utf8_offset(cur_buf, cur_len, f->selection_end);
    if (sel_a > sel_b) { uint32_t t = sel_a; sel_a = sel_b; sel_b = t; }
    if (sel_b > cur_len) sel_b = cur_len;
    if (sel_a > cur_len) sel_a = cur_len;
    *out_start = sel_a;
    *out_end = sel_b;
    return true;
}

uint32_t te_paste(DomElement* elem, DocState* state, void* target,
                  const char* text, uint32_t len) {
    if (!target || !text || len == 0) return 0;
    uint32_t sel_a = 0, sel_b = 0;
    if (!te_prepare_paste_range(elem, state, &sel_a, &sel_b)) return 0;

    // Fallback for callers with no event context: this path never reaches the
    // applier (te_replace_byte_range emits only the legacy post-mutation input
    // hook, no beforeinput), so the clipboard text goes in as-is. Live callers
    // — including the context menu, which installs a hook routing to
    // dispatch_form_text_paste — get the package's policy instead.
    bool ok = te_replace_byte_range(elem, state, target, sel_a, sel_b, text, len);
    return ok ? len : 0;
}

// ---------- F7: IME composition (Radiant_Design_Form_Input.md §3.7) ----

static void te_clear_preedit(FormControlProp* f) {
    if (!f) return;
    if (f->preedit_utf8) { mem_free(f->preedit_utf8); f->preedit_utf8 = nullptr; }
    f->preedit_len = 0;
    f->preedit_caret = 0;
}

bool te_ime_is_composing(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return false;
    return elem->form && elem->form->preedit_utf8 != nullptr;
}

void te_ime_begin(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;
    // Begin from a clean slate; if a previous composition was orphaned,
    // drop it.
    te_clear_preedit(f);
    log_debug("te_ime_begin: starting composition");
}

void te_ime_update(DomElement* elem, const char* preedit, uint32_t len,
                   uint32_t caret_cp) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;

    // Hide the placeholder while composing (even if value is still empty).
    state_set_bool(f->state_ref ? f->state_ref : (elem->doc ? (DocState*)elem->doc->state : nullptr),
        elem, STATE_PLACEHOLDER, false);

    if (len == 0 || !preedit) {
        te_clear_preedit(f);
    } else {
        char* buf = (char*)mem_alloc((size_t)len + 1, MEM_CAT_DOM);
        if (!buf) return;
        memcpy(buf, preedit, len);
        buf[len] = '\0';
        if (f->preedit_utf8) mem_free(f->preedit_utf8);
        f->preedit_utf8 = buf;
        f->preedit_len = len;
        f->preedit_caret = caret_cp;
    }
    log_debug("te_ime_update: %u bytes preedit, caret=%u", len, caret_cp);
}

bool te_ime_commit_prepare(DomElement* elem, DocState* state,
                           const char* committed, uint32_t len,
                           uint32_t* out_start, uint32_t* out_end,
                           bool* out_should_mutate) {
    if (out_start) *out_start = 0;
    if (out_end) *out_end = 0;
    if (out_should_mutate) *out_should_mutate = false;
    if (!elem || !tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    if (!f || !out_start || !out_end || !out_should_mutate) return false;

    // Drop preedit BEFORE inserting so the renderer doesn't briefly show
    // both the preedit and the committed text.
    te_clear_preedit(f);

    // Read-only / disabled fields accept the IME session (preedit was
    // shown above and is now cleared) but reject the actual commit so
    // the underlying value is never mutated.
    if (form_control_is_user_readonly(state, (View*)elem)) {
        log_debug("te_ime_commit: rejected on readonly/disabled control");
        return true;
    }

    if (committed && len > 0 && state) {
        // Insert at caret (or replace selection) via the same path as
        // text input — gets undo, beforeinput/input, maxlength clamp.
        uint32_t cur_len = 0;
        const char* cur_buf = tc_buffer(f, &cur_len);
        uint32_t a = tc_utf16_to_utf8_offset(cur_buf, cur_len, f->selection_start);
        uint32_t b = tc_utf16_to_utf8_offset(cur_buf, cur_len, f->selection_end);
        if (a > b) { uint32_t t = a; a = b; b = t; }
        if (a > cur_len) a = cur_len;
        if (b > cur_len) b = cur_len;
        *out_start = a;
        *out_end = b;
        *out_should_mutate = true;
    }

    return true;
}

void te_ime_commit_finish(DomElement* elem, const char* committed,
                          uint32_t len) {
    if (!elem || !tc_is_text_control(elem)) return;
    log_debug("te_ime_commit: committed %u bytes", len);
}

void te_ime_cancel(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;
    te_clear_preedit(f);
    tc_refresh_placeholder_shown(elem, f);
    log_debug("te_ime_cancel: composition aborted");
}

// ---------- F8: ARIA reflection (Radiant_Design_Form_Input.md §4) -----

static void te_aria_set_or_clear(DomElement* elem, const char* name, bool on,
                                 const char* value) {
    if (on) {
        elem->set_attribute(name, value);
    } else {
        // Only clear if currently set; avoids churning the attribute table.
        if (elem->get_attribute(name)) {
            elem->remove_attribute(name);
        }
    }
}

void te_aria_reflect(DomElement* elem) {
    if (!elem || elem->role_kind() != DomElement::ROLE_FORM) return;
    FormControlProp* f = elem->form;
    if (!f) return;

    DocState* state = f->state_ref ? f->state_ref : (elem->doc ? (DocState*)elem->doc->state : nullptr);

    // Disabled / readonly / required map directly.
    te_aria_set_or_clear(elem, "aria-disabled", form_control_is_disabled(state, (View*)elem), "true");
    te_aria_set_or_clear(elem, "aria-readonly", form_control_is_readonly(state, (View*)elem), "true");
    te_aria_set_or_clear(elem, "aria-required", form_control_is_required(state, (View*)elem), "true");

    // aria-invalid mirrors :invalid pseudo-state. Use "true"/"false"
    // rather than removing — assistive tech treats the explicit "false"
    // value as "validation has run and the control is currently OK".
    bool invalid = state_get_pseudo_state(state, (View*)elem, PSEUDO_STATE_INVALID);
    elem->set_attribute("aria-invalid", invalid ? "true" : "false");

    // aria-valuenow / valuemin / valuemax for <input type="range">.
    if (f->control_type == FORM_CONTROL_RANGE) {
        char buf[32];
        float range_value = form_control_get_range_value(state, (View*)elem);
        float val = f->range_min + (f->range_max - f->range_min) * range_value;
        snprintf(buf, sizeof(buf), "%g", val);
        elem->set_attribute("aria-valuenow", buf);
        snprintf(buf, sizeof(buf), "%g", f->range_min);
        elem->set_attribute("aria-valuemin", buf);
        snprintf(buf, sizeof(buf), "%g", f->range_max);
        elem->set_attribute("aria-valuemax", buf);
    }
}
