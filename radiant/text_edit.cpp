// F1/F2 text-control editing helpers — see vibe/radiant/Radiant_Design_Form_Input.md
//
// Build target: linked into all Radiant builds via the radiant/ glob in
// build_lambda_config.json.

#include "text_edit.hpp"

#include "form_control.hpp"
#include "text_control.hpp"
#include "state_store.hpp"
#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Forward decls (defined elsewhere — keep this file decoupled from the
// large headers).
void caret_set        (RadiantState* state, View* view, int char_offset);
void selection_start  (RadiantState* state, View* view, int char_offset);
void selection_extend (RadiantState* state, int char_offset);
void selection_clear  (RadiantState* state);

// F4: tc_set_value pushes a history snapshot on every mutation. To prevent
// undo/redo restores from re-pushing (and corrupting the cursor), they
// bracket their tc_set_value call with this guard.
extern "C" void tc_history_guard_enter();
extern "C" void tc_history_guard_exit ();

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

} // namespace

// ---------- word boundary ----------------------------------------------

uint32_t te_word_start(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    if (!buf || buf_len == 0) return 0;
    byte_off = clamp_off(byte_off, buf_len);

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
    if (!buf || buf_len == 0) return 0;
    byte_off = clamp_off(byte_off, buf_len);
    uint32_t i = byte_off;
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
    if (!buf || buf_len == 0) return 0;
    byte_off = clamp_off(byte_off, buf_len);
    uint32_t i = byte_off;
    while (i > 0 && buf[i - 1] != '\n') i--;
    return i;
}

uint32_t te_line_end(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    if (!buf || buf_len == 0) return 0;
    byte_off = clamp_off(byte_off, buf_len);
    uint32_t i = byte_off;
    while (i < buf_len && buf[i] != '\n') i++;
    return i;
}

// ---------- selection helpers (F2) -------------------------------------

bool te_apply_byte_range(RadiantState* state, void* target,
                         uint32_t start, uint32_t end) {
    if (!state || !target) return false;
    if (end < start) { uint32_t t = start; start = end; end = t; }
    View* view = (View*)target;
    selection_start (state, view, (int)start);
    selection_extend(state, (int)end);
    caret_set       (state, view, (int)end);
    if (state->selection) {
        state->selection->is_selecting = false;  // gesture is complete
    }
    log_debug("text_edit: applied selection bytes=[%u..%u] view=%p",
              start, end, view);
    return true;
}

bool te_select_word_at(DomElement* elem, RadiantState* state,
                       void* target, uint32_t byte_off) {
    if (!elem || !state || !target) return false;
    if (!tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    uint32_t blen = 0;
    const char* buf = tc_buffer(f, &blen);
    if (!buf || blen == 0) return false;

    uint32_t s = te_word_start(buf, blen, byte_off);
    uint32_t e = te_word_end  (buf, blen, byte_off);
    if (s == e) return false;  // no word at position
    return te_apply_byte_range(state, target, s, e);
}

bool te_select_line_at(DomElement* elem, RadiantState* state,
                       void* target, uint32_t byte_off) {
    if (!elem || !state || !target) return false;
    if (!tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    uint32_t blen = 0;
    const char* buf = tc_buffer(f, &blen);
    if (!buf) return false;

    uint32_t s = te_line_start(buf, blen, byte_off);
    uint32_t e = te_line_end  (buf, blen, byte_off);
    return te_apply_byte_range(state, target, s, e);
}

bool te_select_all(DomElement* elem, RadiantState* state, void* target) {
    if (!elem || !state || !target) return false;
    if (!tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    uint32_t blen = 0;
    (void)tc_buffer(f, &blen);
    return te_apply_byte_range(state, target, 0, blen);
}

// ---------- F3: word-granularity navigation ----------------------------

uint32_t te_prev_word_byte(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    if (!buf || buf_len == 0) return 0;
    byte_off = clamp_off(byte_off, buf_len);
    uint32_t i = byte_off;
    // Skip separators directly to the left.
    while (i > 0 && !te_is_word_byte((unsigned char)buf[i - 1])) i--;
    // Skip the contiguous run of word bytes.
    while (i > 0 &&  te_is_word_byte((unsigned char)buf[i - 1])) i--;
    return i;
}

uint32_t te_next_word_byte(const char* buf, uint32_t buf_len, uint32_t byte_off) {
    if (!buf || buf_len == 0) return 0;
    byte_off = clamp_off(byte_off, buf_len);
    uint32_t i = byte_off;
    // Skip separators directly to the right.
    while (i < buf_len && !te_is_word_byte((unsigned char)buf[i])) i++;
    // Skip the contiguous run of word bytes.
    while (i < buf_len &&  te_is_word_byte((unsigned char)buf[i])) i++;
    return i;
}

// ---------- F3: range-based mutation -----------------------------------

bool te_replace_byte_range(DomElement* elem, RadiantState* state, void* target,
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
    char* nbuf = (char*)malloc((size_t)new_len + 1);
    if (!nbuf) return false;
    if (start > 0)            memcpy(nbuf,             old_buf,           start);
    if (repl_len > 0 && repl) memcpy(nbuf + start,     repl,              repl_len);
    if (end < old_len)        memcpy(nbuf + start + repl_len,
                                     old_buf + end,
                                     old_len - end);
    nbuf[new_len] = '\0';

    tc_set_value(elem, nbuf, new_len);
    free(nbuf);

    // Place caret at end of inserted text and clear any selection.
    uint32_t new_caret = start + repl_len;
    if (state->selection) selection_clear(state);
    caret_set(state, (View*)target, (int)new_caret);

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
    if (f->value_at_focus) { free(f->value_at_focus); f->value_at_focus = nullptr; }
    f->value_at_focus_len = 0;
    if (buf) {
        f->value_at_focus = (char*)malloc((size_t)blen + 1);
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
    if (f->value_at_focus) { free(f->value_at_focus); f->value_at_focus = nullptr; }
    f->value_at_focus_len = 0;

    log_debug("text_edit: blur_should_dispatch_change elem=%p changed=%d",
              elem, (int)changed);
    return changed;
}

// ---------- undo/redo ring (skeleton) ----------------------------------

EditHistory* te_history_new(uint16_t cap) {
    if (cap == 0) cap = TE_HISTORY_DEFAULT_CAP;
    EditHistory* h = (EditHistory*)calloc(1, sizeof(EditHistory));
    if (!h) return nullptr;
    h->ring = (EditHistoryEntry*)calloc(cap, sizeof(EditHistoryEntry));
    if (!h->ring) { free(h); return nullptr; }
    h->cap = cap;
    return h;
}

void te_history_free(EditHistory* h) {
    if (!h) return;
    if (h->ring) {
        for (uint16_t i = 0; i < h->cap; i++) {
            if (h->ring[i].snapshot) free(h->ring[i].snapshot);
        }
        free(h->ring);
    }
    free(h);
}

static EditHistory* tc_get_or_create_history(FormControlProp* f) {
    if (!f) return nullptr;
    if (!f->history) f->history = te_history_new(TE_HISTORY_DEFAULT_CAP);
    return (EditHistory*)f->history;
}

void te_history_push(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;
    EditHistory* h = tc_get_or_create_history(f);
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
    if (slot->snapshot) { free(slot->snapshot); slot->snapshot = nullptr; }
    slot->length = blen;
    slot->snapshot = (char*)malloc((size_t)blen + 1);
    if (!slot->snapshot) return;
    if (blen) memcpy(slot->snapshot, buf, blen);
    slot->snapshot[blen] = '\0';
    slot->sel_start_u16 = f->selection_start;
    slot->sel_end_u16   = f->selection_end;
    slot->sel_dir       = f->selection_direction;

    h->head = (uint16_t)((h->head + 1) % h->cap);
    if (h->count < h->cap) h->count++;
}

bool te_history_undo(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    if (!f || !f->history) return false;
    EditHistory* h = (EditHistory*)f->history;

    // Need at least one older entry beyond the current state. The newest
    // entry typically represents the current state; cursor++ moves to the
    // one before it.
    if ((uint16_t)(h->cursor + 1) >= h->count) return false;
    h->cursor++;
    uint16_t idx = (uint16_t)((h->head + h->cap - 1 - h->cursor) % h->cap);
    EditHistoryEntry* e = &h->ring[idx];
    if (!e->snapshot) return false;

    tc_history_guard_enter();
    tc_set_value(elem, e->snapshot, e->length);
    tc_history_guard_exit();
    tc_set_selection_range(elem, e->sel_start_u16, e->sel_end_u16, e->sel_dir);
    return true;
}

bool te_history_redo(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return false;
    FormControlProp* f = elem->form;
    if (!f || !f->history) return false;
    EditHistory* h = (EditHistory*)f->history;

    if (h->cursor == 0) return false;
    h->cursor--;
    uint16_t idx = (uint16_t)((h->head + h->cap - 1 - h->cursor) % h->cap);
    EditHistoryEntry* e = &h->ring[idx];
    if (!e->snapshot) return false;

    tc_history_guard_enter();
    tc_set_value(elem, e->snapshot, e->length);
    tc_history_guard_exit();
    tc_set_selection_range(elem, e->sel_start_u16, e->sel_end_u16, e->sel_dir);
    return true;
}
