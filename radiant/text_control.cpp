// Phase 6E shared text-control helpers.
// See radiant/text_control.hpp for the public surface and design rationale.

#include "text_control.hpp"
#include "form_control.hpp"
#include "state_store.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/strbuf.h"
#include "../lib/log.h"
#include <new>          // placement new
#include <strings.h>    // strcasecmp
#include <string.h>
#include <stdlib.h>

// dom_element_get_attribute is declared with C++ linkage in dom_element.hpp.

// Phase 8E: per-text-control selectionchange dispatch. Strong impl lives in
// lambda/js/js_dom_selection.cpp (queues a coalesced setTimeout(0) drain).
// Headless/no-JS builds get the no-op weak fallback below.
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_selectionchange(DomElement* elem);
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_selectionchange(DomElement* /*elem*/) {}

void tc_notify_selection_changed(DomElement* elem) {
    if (!elem) return;
    js_dom_queue_textcontrol_selectionchange(elem);
}

// Module-local focus tracker. The JS bindings and event.cpp both write to
// this so Selection.toString() / document.activeElement can resolve to the
// same source of truth even when JS runs without Radiant layout.
static DomElement* g_active_element = nullptr;
static DomElement* g_last_focused_text_control = nullptr;

// ---- DomNode text walker (textarea initial value) ----------------------
// Local to this TU so radiant code does not need to depend on lambda/js/.
static void tc_collect_text(DomNode* n, StrBuf* sb) {
    if (!n) return;
    if (n->is_text()) {
        DomText* t = n->as_text();
        if (t->text) strbuf_append_str(sb, t->text);
        return;
    }
    if (n->is_element()) {
        DomElement* e = n->as_element();
        for (DomNode* c = e->first_child; c; c = c->next_sibling) {
            tc_collect_text(c, sb);
        }
    }
}

// ---- public: identification --------------------------------------------

bool tc_is_text_control(DomElement* elem) {
    if (!elem || !elem->tag_name) return false;
    if (elem->item_prop_type == DomElement::ITEM_PROP_FORM && elem->form) {
        return elem->form->control_type == FORM_CONTROL_TEXT
            || elem->form->control_type == FORM_CONTROL_TEXTAREA;
    }
    if (strcasecmp(elem->tag_name, "textarea") == 0) return true;
    if (strcasecmp(elem->tag_name, "input") == 0) {
        const char* t = dom_element_get_attribute(elem, "type");
        if (!t || !*t) return true;
        return strcasecmp(t, "text") == 0
            || strcasecmp(t, "password") == 0
            || strcasecmp(t, "email") == 0
            || strcasecmp(t, "url") == 0
            || strcasecmp(t, "search") == 0
            || strcasecmp(t, "tel") == 0
            || strcasecmp(t, "number") == 0;
    }
    return false;
}

FormControlProp* tc_get_or_create_form(DomElement* elem) {
    if (elem->form && elem->item_prop_type == DomElement::ITEM_PROP_FORM)
        return elem->form;
    FormControlProp* f = new FormControlProp();
    if (elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0) {
        f->control_type = FORM_CONTROL_TEXTAREA;
    } else {
        f->control_type = FORM_CONTROL_TEXT;
    }
    elem->form = f;
    elem->item_prop_type = DomElement::ITEM_PROP_FORM;
    return f;
}

// ---- public: UTF-8 ↔ UTF-16 --------------------------------------------

uint32_t tc_utf8_to_utf16_length(const char* s, uint32_t byte_len) {
    if (!s) return 0;
    uint32_t n = 0;
    const unsigned char* p = (const unsigned char*)s;
    for (uint32_t i = 0; i < byte_len; i++) {
        unsigned char b = p[i];
        if ((b & 0xC0) != 0x80) {
            if (b < 0x80)      n += 1;
            else if (b < 0xF0) n += 1;
            else               n += 2;  // surrogate pair
        }
    }
    return n;
}

uint32_t tc_utf16_to_utf8_offset(const char* s, uint32_t byte_len, uint32_t u16) {
    if (!s) return 0;
    if (u16 == 0) return 0;
    uint32_t seen = 0;
    const unsigned char* p = (const unsigned char*)s;
    for (uint32_t i = 0; i < byte_len; i++) {
        unsigned char b = p[i];
        if ((b & 0xC0) != 0x80) {
            if (seen >= u16) return i;
            if (b < 0x80)      seen += 1;
            else if (b < 0xF0) seen += 1;
            else               seen += 2;
        }
    }
    return byte_len;
}

uint32_t tc_utf8_to_utf16_offset(const char* s, uint32_t byte_len, uint32_t u8) {
    if (!s) return 0;
    if (u8 > byte_len) u8 = byte_len;
    return tc_utf8_to_utf16_length(s, u8);
}

// ---- internal: initial value resolution --------------------------------

static char* tc_initial_value(DomElement* elem, uint32_t* out_len) {
    *out_len = 0;
    if (!elem) return nullptr;
    if (elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0) {
        StrBuf* sb = strbuf_new_cap(64);
        tc_collect_text((DomNode*)elem, sb);
        size_t len = sb->str ? strlen(sb->str) : 0;
        char* out = (char*)malloc(len + 1);
        if (sb->str) memcpy(out, sb->str, len);
        out[len] = '\0';
        strbuf_free(sb);
        *out_len = (uint32_t)len;
        return out;
    }
    const char* v = dom_element_get_attribute(elem, "value");
    if (!v) v = "";
    size_t len = strlen(v);
    char* out = (char*)malloc(len + 1);
    memcpy(out, v, len);
    out[len] = '\0';
    *out_len = (uint32_t)len;
    return out;
}

// ---- public: lazy init + writes ----------------------------------------

void tc_ensure_init(DomElement* elem) {
    if (!tc_is_text_control(elem)) return;
    FormControlProp* f = tc_get_or_create_form(elem);
    if (f->tc_initialized) return;
    if (!f->current_value) {
        uint32_t len = 0;
        f->current_value = tc_initial_value(elem, &len);
        f->current_value_len = len;
        f->current_value_u16_len = tc_utf8_to_utf16_length(f->current_value, len);
    } else {
        f->current_value_u16_len = tc_utf8_to_utf16_length(
            f->current_value, f->current_value_len);
    }
    f->selection_start = f->current_value_u16_len;
    f->selection_end = f->current_value_u16_len;
    f->selection_direction = 0;
    f->tc_initialized = 1;
    // Mirror live value into legacy display field used by render_form.cpp.
    if (!f->value || f->value != f->current_value) {
        f->value = f->current_value;
    }
}

void tc_set_value(DomElement* elem, const char* new_val, size_t new_len) {
    if (!tc_is_text_control(elem)) return;
    FormControlProp* f = tc_get_or_create_form(elem);
    uint32_t old_start = f->selection_start;
    uint32_t old_end = f->selection_end;
    uint8_t  old_dir = f->selection_direction;
    bool was_initialized = f->tc_initialized;
    if (f->current_value) free(f->current_value);
    char* buf = (char*)malloc(new_len + 1);
    if (new_val && new_len) memcpy(buf, new_val, new_len);
    buf[new_len] = '\0';
    f->current_value = buf;
    f->current_value_len = (uint32_t)new_len;
    f->current_value_u16_len = tc_utf8_to_utf16_length(buf, (uint32_t)new_len);
    f->selection_start = f->current_value_u16_len;
    f->selection_end = f->current_value_u16_len;
    f->selection_direction = 0;
    f->tc_initialized = 1;
    f->value = buf;
    // Notify if value-setter caused the selection to move (e.g. previous
    // selection was past the new length and got clamped). Suppress on
    // initial init so parsing-time setup doesn't fire spurious events.
    if (was_initialized &&
        (f->selection_start != old_start ||
         f->selection_end != old_end ||
         f->selection_direction != old_dir)) {
        tc_notify_selection_changed(elem);
    }
}

void tc_set_selection_range(DomElement* elem,
                            uint32_t start, uint32_t end,
                            uint8_t dir) {
    tc_ensure_init(elem);
    FormControlProp* f = tc_get_or_create_form(elem);
    uint32_t n = f->current_value_u16_len;
    if (start > n) start = n;
    if (end > n) end = n;
    if (start > end) start = end;
    uint32_t old_start = f->selection_start;
    uint32_t old_end = f->selection_end;
    uint8_t  old_dir = f->selection_direction;
    f->selection_start = start;
    f->selection_end = end;
    f->selection_direction = dir;
    if (start != old_start || end != old_end || dir != old_dir) {
        tc_notify_selection_changed(elem);
    }
}

// ---- public: bidirectional sync ---------------------------------------

// Locate the focused element's containing element. The legacy CaretState
// stores a View*; for form controls the View is the ViewBlock that is also
// the DomElement (since DomElement extends ViewBlock in radiant's unified
// DOM). Caller passes the *known* DomElement so we don't have to dispatch.

void tc_sync_legacy_to_form(DomElement* elem, RadiantState* state) {
    if (!elem || !state) return;
    if (!tc_is_text_control(elem)) return;
    tc_ensure_init(elem);
    FormControlProp* f = elem->form;
    const char* val = f->value ? f->value : "";
    uint32_t blen = (uint32_t)strlen(val);

    // Refresh cached lengths in case form->value was reallocated by the
    // legacy text-edit path (resolve_htm_style or future input handlers).
    if (val != f->current_value) {
        // Adopt the new buffer when value changed under us.
        if (f->current_value) free(f->current_value);
        f->current_value = (char*)malloc(blen + 1);
        memcpy(f->current_value, val, blen);
        f->current_value[blen] = '\0';
        f->current_value_len = blen;
        f->value = f->current_value;
    } else {
        f->current_value_len = blen;
    }
    f->current_value_u16_len = tc_utf8_to_utf16_length(f->current_value, blen);

    int caret_byte = state->caret ? state->caret->char_offset : 0;
    if (caret_byte < 0) caret_byte = 0;
    if ((uint32_t)caret_byte > blen) caret_byte = (int)blen;

    if (state->selection && !state->selection->is_collapsed
        && state->selection->anchor_view == state->selection->focus_view) {
        int a = state->selection->anchor_offset;
        int b = state->selection->focus_offset;
        if (a < 0) a = 0; if (b < 0) b = 0;
        if ((uint32_t)a > blen) a = (int)blen;
        if ((uint32_t)b > blen) b = (int)blen;
        uint32_t lo = a < b ? (uint32_t)a : (uint32_t)b;
        uint32_t hi = a < b ? (uint32_t)b : (uint32_t)a;
        f->selection_start = tc_utf8_to_utf16_offset(f->current_value, blen, lo);
        f->selection_end   = tc_utf8_to_utf16_offset(f->current_value, blen, hi);
        f->selection_direction = (a <= b) ? 1 : 2;  // forward / backward
    } else {
        uint32_t u16 = tc_utf8_to_utf16_offset(f->current_value, blen, (uint32_t)caret_byte);
        f->selection_start = u16;
        f->selection_end = u16;
        f->selection_direction = 0;
    }
}

void tc_sync_form_to_legacy(DomElement* elem, RadiantState* state) {
    if (!elem || !state) return;
    if (!tc_is_text_control(elem)) return;
    tc_ensure_init(elem);
    FormControlProp* f = elem->form;
    const char* val = f->current_value ? f->current_value : "";
    uint32_t blen = f->current_value_len;
    uint32_t a8 = tc_utf16_to_utf8_offset(val, blen, f->selection_start);
    uint32_t b8 = tc_utf16_to_utf8_offset(val, blen, f->selection_end);

    View* view = (View*)elem;
    if (state->caret) {
        state->caret->view = view;
        state->caret->char_offset = (int)b8;  // caret follows focus end
    }
    if (state->selection) {
        if (f->selection_start == f->selection_end) {
            // collapsed — clear the selection range
            state->selection->is_collapsed = true;
            state->selection->anchor_view = view;
            state->selection->focus_view = view;
            state->selection->anchor_offset = (int)a8;
            state->selection->focus_offset = (int)b8;
        } else {
            state->selection->is_collapsed = false;
            state->selection->anchor_view = view;
            state->selection->focus_view = view;
            if (f->selection_direction == 2) {
                // backward: anchor at end, focus at start
                state->selection->anchor_offset = (int)b8;
                state->selection->focus_offset = (int)a8;
            } else {
                state->selection->anchor_offset = (int)a8;
                state->selection->focus_offset = (int)b8;
            }
        }
    }
}

// ---- public: focus tracker ---------------------------------------------

void tc_set_active_element(DomElement* elem) { g_active_element = elem; }
DomElement* tc_get_active_element(void) { return g_active_element; }
void tc_set_last_focused_text_control(DomElement* elem) { g_last_focused_text_control = elem; }
DomElement* tc_get_last_focused_text_control(void) { return g_last_focused_text_control; }
DomElement** tc_active_element_slot(void) { return &g_active_element; }
DomElement** tc_last_focused_text_control_slot(void) { return &g_last_focused_text_control; }
void tc_reset_focus_state(void) {
    g_active_element = nullptr;
    g_last_focused_text_control = nullptr;
}

// ---- public: Selection.toString() bridge -------------------------------

const char* tc_active_selected_text(uint32_t* out_byte_len) {
    if (out_byte_len) *out_byte_len = 0;
    DomElement* tc = g_last_focused_text_control;
    if (!tc || !tc_is_text_control(tc)) return nullptr;
    if (g_active_element && tc_is_text_control(g_active_element)
        && g_active_element != tc) {
        tc = g_active_element;
    }
    tc_ensure_init(tc);
    FormControlProp* f = tc->form;
    if (f->selection_start >= f->selection_end) return nullptr;
    uint32_t a8 = tc_utf16_to_utf8_offset(f->current_value, f->current_value_len,
                                          f->selection_start);
    uint32_t b8 = tc_utf16_to_utf8_offset(f->current_value, f->current_value_len,
                                          f->selection_end);
    if (b8 < a8) return nullptr;
    if (out_byte_len) *out_byte_len = b8 - a8;
    return f->current_value + a8;
}
