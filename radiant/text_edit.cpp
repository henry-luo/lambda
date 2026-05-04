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
#include <strings.h>    // strcasecmp (F5 input type checks)

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

    // F5: fire `beforeinput` (informational; cancellation is a JS-bridge
    // concern) before mutating the value so listeners observe pre-state.
    te_dispatch_beforeinput(elem);

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

    // F5: dispatch `input` after the mutation is committed.
    te_dispatch_input(elem);

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

// ---------- F5: events + constraint validation -------------------------

// Weak hooks; the JS DOM bridge overrides these in JS-enabled builds. The
// no-op default keeps headless / unit-test builds free of any cost.
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_input(DomElement* elem);
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_input(DomElement* /*elem*/) {}
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_beforeinput(DomElement* elem);
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_beforeinput(DomElement* /*elem*/) {}

void te_dispatch_beforeinput(DomElement* elem) {
    if (!elem) return;
    js_dom_queue_textcontrol_beforeinput(elem);
}

void te_dispatch_input(DomElement* elem) {
    if (!elem) return;
    js_dom_queue_textcontrol_input(elem);
}

// ---- minimal validators (no allocation; ASCII fast paths) -------------

static bool te_value_is_number(const char* s, uint32_t len) {
    if (!s || len == 0) return false;
    uint32_t i = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    bool has_digit = false;
    while (i < len && s[i] >= '0' && s[i] <= '9') { i++; has_digit = true; }
    if (i < len && s[i] == '.') {
        i++;
        while (i < len && s[i] >= '0' && s[i] <= '9') { i++; has_digit = true; }
    }
    if (!has_digit) return false;
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < len && (s[i] == '+' || s[i] == '-')) i++;
        bool exp_digit = false;
        while (i < len && s[i] >= '0' && s[i] <= '9') { i++; exp_digit = true; }
        if (!exp_digit) return false;
    }
    return i == len;
}

// Loose RFC 5322-ish: <local>@<domain> with at least one dot in the domain
// and no spaces or controls anywhere. Mirrors the spec's "valid email
// address" production used by browsers (HTML §4.10.5.1.5).
static bool te_value_is_email(const char* s, uint32_t len) {
    if (!s || len < 3) return false;
    int at = -1;
    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c <= 0x20 || c == 0x7F) return false;
        if (c == '@') { if (at >= 0) return false; at = (int)i; }
    }
    if (at <= 0 || at >= (int)len - 1) return false;
    bool has_dot = false;
    for (int i = at + 1; i < (int)len; i++) {
        if (s[i] == '.') {
            // dot must not be first/last char of domain
            if (i == at + 1 || i == (int)len - 1) return false;
            has_dot = true;
        }
    }
    return has_dot;
}

// Minimal URL check — must start with a scheme (alpha+ followed by ':')
// and contain no whitespace/control bytes.
static bool te_value_is_url(const char* s, uint32_t len) {
    if (!s || len < 4) return false;
    uint32_t i = 0;
    if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) return false;
    while (i < len && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z')
                       || (s[i] >= '0' && s[i] <= '9') || s[i] == '+'
                       || s[i] == '-' || s[i] == '.')) i++;
    if (i == 0 || i >= len || s[i] != ':') return false;
    for (uint32_t j = 0; j < len; j++) {
        unsigned char c = (unsigned char)s[j];
        if (c <= 0x20 || c == 0x7F) return false;
    }
    return true;
}

void te_validate(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;

    // Mirror static attribute bits as pseudo-states. Cheap and idempotent.
    if (f->required) {
        dom_element_set_pseudo_state(elem, PSEUDO_STATE_REQUIRED);
        dom_element_clear_pseudo_state(elem, PSEUDO_STATE_OPTIONAL);
    } else {
        dom_element_clear_pseudo_state(elem, PSEUDO_STATE_REQUIRED);
        dom_element_set_pseudo_state(elem, PSEUDO_STATE_OPTIONAL);
    }
    if (f->readonly || f->disabled) {
        dom_element_set_pseudo_state(elem, PSEUDO_STATE_READ_ONLY);
        dom_element_clear_pseudo_state(elem, PSEUDO_STATE_READ_WRITE);
    } else {
        dom_element_clear_pseudo_state(elem, PSEUDO_STATE_READ_ONLY);
        dom_element_set_pseudo_state(elem, PSEUDO_STATE_READ_WRITE);
    }

    // Resolve current value bytes for content-based checks.
    const char* val = f->current_value
        ? f->current_value : (f->value ? f->value : "");
    uint32_t vlen = f->current_value
        ? f->current_value_len : (uint32_t)strlen(val);

    bool valid = true;

    // Custom validity overrides everything.
    if (f->custom_validity_msg && f->custom_validity_msg[0] != '\0') {
        valid = false;
    }
    // Required: empty value fails.
    else if (f->required && vlen == 0) {
        valid = false;
    }
    // Type-driven content checks (only when non-empty).
    else if (vlen > 0 && f->input_type) {
        if (strcasecmp(f->input_type, "number") == 0) {
            valid = te_value_is_number(val, vlen);
        } else if (strcasecmp(f->input_type, "email") == 0) {
            valid = te_value_is_email(val, vlen);
        } else if (strcasecmp(f->input_type, "url") == 0) {
            valid = te_value_is_url(val, vlen);
        }
    }
    // TODO(F5): pattern="..." — needs lazy-compiled regex.

    if (valid) {
        dom_element_set_pseudo_state(elem, PSEUDO_STATE_VALID);
        dom_element_clear_pseudo_state(elem, PSEUDO_STATE_INVALID);
    } else {
        dom_element_clear_pseudo_state(elem, PSEUDO_STATE_VALID);
        dom_element_set_pseudo_state(elem, PSEUDO_STATE_INVALID);
    }
}

// ---------- F6: paste sanitization (Radiant_Design_Form_Input.md §3.6) -

uint32_t te_paste(DomElement* elem, RadiantState* state, void* target,
                  const char* text, uint32_t len) {
    if (!elem || !state || !target || !text || len == 0) return 0;
    if (!tc_is_text_control(elem)) return 0;
    FormControlProp* f = elem->form;
    if (!f || f->readonly || f->disabled) return 0;

    bool is_single_line = (f->control_type == FORM_CONTROL_TEXT);

    // Step 1: build a sanitized copy. For <input> single-line controls we
    // collapse CRLF/CR/LF into a single U+0020. For <textarea> we drop
    // bare CR and \r\n -> \n (HTML normalization). Worst case the buffer
    // size equals `len`.
    char* sanitized = (char*)malloc((size_t)len + 1);
    if (!sanitized) return 0;
    uint32_t s_len = 0;
    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\r') {
            // Skip a following \n so CRLF becomes a single newline/space.
            if (i + 1 < len && text[i+1] == '\n') i++;
            sanitized[s_len++] = is_single_line ? ' ' : '\n';
        } else if (c == '\n') {
            sanitized[s_len++] = is_single_line ? ' ' : '\n';
        } else {
            sanitized[s_len++] = (char)c;
        }
    }
    sanitized[s_len] = '\0';

    // Step 2: enforce maxlength. The selection range will be replaced, so
    // the post-paste length budget is current_len - selection_byte_len +
    // s_len. Convert that to codepoint budget vs `maxlength`.
    uint32_t cur_len = 0;
    const char* cur_buf = tc_buffer(f, &cur_len);
    uint32_t sel_a = tc_utf16_to_utf8_offset(cur_buf, cur_len, f->selection_start);
    uint32_t sel_b = tc_utf16_to_utf8_offset(cur_buf, cur_len, f->selection_end);
    if (sel_a > sel_b) { uint32_t t = sel_a; sel_a = sel_b; sel_b = t; }
    if (sel_b > cur_len) sel_b = cur_len;
    if (sel_a > cur_len) sel_a = cur_len;

    if (f->maxlength >= 0) {
        uint32_t cur_cp = tc_utf8_to_utf16_length(cur_buf, cur_len);
        uint32_t sel_cp = tc_utf8_to_utf16_length(cur_buf + sel_a, sel_b - sel_a);
        // Codepoint budget for the paste (post-deletion of selection).
        uint32_t post_cp = (cur_cp >= sel_cp) ? (cur_cp - sel_cp) : 0;
        if (post_cp >= (uint32_t)f->maxlength) {
            // No room left.
            free(sanitized);
            return 0;
        }
        uint32_t budget = (uint32_t)f->maxlength - post_cp;
        // Walk sanitized as UTF-8 and stop at `budget` codepoints.
        uint32_t cp_count = 0, byte_at = 0;
        while (byte_at < s_len && cp_count < budget) {
            unsigned char b = (unsigned char)sanitized[byte_at];
            uint32_t step = (b < 0x80) ? 1 : (b < 0xC0) ? 1
                            : (b < 0xE0) ? 2 : (b < 0xF0) ? 3 : 4;
            if (byte_at + step > s_len) break;
            byte_at += step;
            cp_count++;
        }
        if (byte_at < s_len) {
            log_debug("te_paste: maxlength=%d clamped %u -> %u bytes",
                      f->maxlength, s_len, byte_at);
            s_len = byte_at;
            sanitized[s_len] = '\0';
        }
    }

    if (s_len == 0) { free(sanitized); return 0; }

    // Step 3: replace [sel_a, sel_b) with sanitized. te_replace_byte_range
    // dispatches beforeinput/input and pushes an undo entry.
    bool ok = te_replace_byte_range(elem, state, target,
                                    sel_a, sel_b, sanitized, s_len);
    free(sanitized);
    return ok ? s_len : 0;
}

// ---------- F7: IME composition (Radiant_Design_Form_Input.md §3.7) ----

// Weak hooks so the JS DOM bridge can override per build (mirrors the
// `input` / `beforeinput` pattern above).
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_compositionstart(DomElement* elem);
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_compositionstart(DomElement* /*elem*/) {}
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_compositionupdate(DomElement* elem, const char* data);
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_compositionupdate(DomElement* /*elem*/, const char* /*data*/) {}
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_compositionend(DomElement* elem, const char* data);
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_compositionend(DomElement* /*elem*/, const char* /*data*/) {}

static void te_clear_preedit(FormControlProp* f) {
    if (!f) return;
    if (f->preedit_utf8) { free(f->preedit_utf8); f->preedit_utf8 = nullptr; }
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
    js_dom_queue_textcontrol_compositionstart(elem);
}

void te_ime_update(DomElement* elem, const char* preedit, uint32_t len,
                   uint32_t caret_cp) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;

    // Hide the placeholder while composing (even if value is still empty).
    dom_element_clear_pseudo_state(elem, PSEUDO_STATE_PLACEHOLDER_SHOWN);
    f->placeholder_shown = 0;

    if (len == 0 || !preedit) {
        te_clear_preedit(f);
    } else {
        char* buf = (char*)malloc((size_t)len + 1);
        if (!buf) return;
        memcpy(buf, preedit, len);
        buf[len] = '\0';
        if (f->preedit_utf8) free(f->preedit_utf8);
        f->preedit_utf8 = buf;
        f->preedit_len = len;
        f->preedit_caret = caret_cp;
    }
    log_debug("te_ime_update: %u bytes preedit, caret=%u", len, caret_cp);
    js_dom_queue_textcontrol_compositionupdate(elem,
        f->preedit_utf8 ? f->preedit_utf8 : "");
}

void te_ime_commit(DomElement* elem, RadiantState* state, void* target,
                   const char* committed, uint32_t len) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;

    // Drop preedit BEFORE inserting so the renderer doesn't briefly show
    // both the preedit and the committed text.
    te_clear_preedit(f);

    // Read-only / disabled fields accept the IME session (preedit was
    // shown above and is now cleared) but reject the actual commit so
    // the underlying value is never mutated.
    if (f->readonly || f->disabled) {
        log_debug("te_ime_commit: rejected on readonly/disabled control");
        char tmp_null = '\0';
        js_dom_queue_textcontrol_compositionend(elem,
            committed ? committed : &tmp_null);
        return;
    }

    if (committed && len > 0 && state && target) {
        // Insert at caret (or replace selection) via the same path as
        // text input — gets undo, beforeinput/input, maxlength clamp.
        uint32_t cur_len = 0;
        const char* cur_buf = tc_buffer(f, &cur_len);
        uint32_t a = tc_utf16_to_utf8_offset(cur_buf, cur_len, f->selection_start);
        uint32_t b = tc_utf16_to_utf8_offset(cur_buf, cur_len, f->selection_end);
        if (a > b) { uint32_t t = a; a = b; b = t; }
        if (a > cur_len) a = cur_len;
        if (b > cur_len) b = cur_len;
        te_replace_byte_range(elem, state, target, a, b, committed, len);
    }

    log_debug("te_ime_commit: committed %u bytes", len);
    char tmp_null = '\0';
    js_dom_queue_textcontrol_compositionend(elem,
        committed ? committed : &tmp_null);
}

void te_ime_cancel(DomElement* elem) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* f = elem->form;
    if (!f) return;
    te_clear_preedit(f);
    // Recompute placeholder_shown: visible iff value is empty.
    bool show = (f->current_value_len == 0) && f->placeholder && f->placeholder[0];
    if (show) {
        dom_element_set_pseudo_state(elem, PSEUDO_STATE_PLACEHOLDER_SHOWN);
        f->placeholder_shown = 1;
    } else {
        dom_element_clear_pseudo_state(elem, PSEUDO_STATE_PLACEHOLDER_SHOWN);
        f->placeholder_shown = 0;
    }
    log_debug("te_ime_cancel: composition aborted");
    js_dom_queue_textcontrol_compositionend(elem, "");
}

// ---------- F8: ARIA reflection (Radiant_Design_Form_Input.md §4) -----

static void te_aria_set_or_clear(DomElement* elem, const char* name, bool on,
                                 const char* value) {
    if (on) {
        dom_element_set_attribute(elem, name, value);
    } else {
        // Only clear if currently set; avoids churning the attribute table.
        if (dom_element_get_attribute(elem, name)) {
            dom_element_remove_attribute(elem, name);
        }
    }
}

void te_aria_reflect(DomElement* elem) {
    if (!elem || elem->item_prop_type != DomElement::ITEM_PROP_FORM) return;
    FormControlProp* f = elem->form;
    if (!f) return;

    // Disabled / readonly / required map directly.
    te_aria_set_or_clear(elem, "aria-disabled", f->disabled,  "true");
    te_aria_set_or_clear(elem, "aria-readonly", f->readonly,  "true");
    te_aria_set_or_clear(elem, "aria-required", f->required,  "true");

    // aria-invalid mirrors :invalid pseudo-state. Use "true"/"false"
    // rather than removing — assistive tech treats the explicit "false"
    // value as "validation has run and the control is currently OK".
    bool invalid = (elem->pseudo_state & PSEUDO_STATE_INVALID) != 0;
    dom_element_set_attribute(elem, "aria-invalid", invalid ? "true" : "false");

    // aria-valuenow / valuemin / valuemax for <input type="range">.
    if (f->control_type == FORM_CONTROL_RANGE) {
        char buf[32];
        float val = f->range_min + (f->range_max - f->range_min) * f->range_value;
        snprintf(buf, sizeof(buf), "%g", val);
        dom_element_set_attribute(elem, "aria-valuenow", buf);
        snprintf(buf, sizeof(buf), "%g", f->range_min);
        dom_element_set_attribute(elem, "aria-valuemin", buf);
        snprintf(buf, sizeof(buf), "%g", f->range_max);
        dom_element_set_attribute(elem, "aria-valuemax", buf);
    }
}