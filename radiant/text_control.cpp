// Phase 6E shared text-control helpers.
// See radiant/text_control.hpp for the public surface and design rationale.

#include "event.hpp"
#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/strbuf.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include <strings.h>    // strcasecmp
#include <string.h>

// Phase 8E: per-text-control selectionchange dispatch. Strong impl lives in
// lambda/js/js_dom_selection.cpp (queues a coalesced setTimeout(0) drain).
// Headless/no-JS builds get the no-op weak fallback below.
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_selectionchange(DomElement* elem);
extern "C" __attribute__((weak)) void js_dom_queue_textcontrol_selectionchange(DomElement* /*elem*/) {}
extern void font_prop_release_handle(FontProp* fprop);

void tc_notify_selection_changed(DomElement* elem) {
    if (!elem) return;
    // Selection projection updates anchor and focus through nested StateStore
    // transitions; queue `select` with selectionchange so handlers never
    // re-enter selectionStart/End against the half-committed projection.
    js_dom_queue_textcontrol_selectionchange(elem);
}

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
    if (elem->form_control()) {
        return elem->form->control_type == FORM_CONTROL_TEXT
            || elem->form->control_type == FORM_CONTROL_TEXTAREA;
    }
    if (strcasecmp(elem->tag_name, "textarea") == 0) return true;
    if (strcasecmp(elem->tag_name, "input") == 0) {
        const char* t = elem->get_attribute("type");
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

void form_control_prop_init(FormControlProp* f) {
    // Pre-zeroed (pool_calloc / mem_calloc) memory is assumed. Apply only
    // the non-zero defaults — every other field is intentionally zero.
    f->control_type = FORM_CONTROL_NONE;
    f->size = FormDefaults::TEXT_SIZE_CHARS;
    f->cols = FormDefaults::TEXTAREA_COLS;
    f->rows = FormDefaults::TEXTAREA_ROWS;
    f->maxlength = -1;
    f->range_max = 100;
    f->range_step = 1;
    f->range_value = 0.5f;
    f->selected_index = -1;
    f->hover_index = -1;
    f->placeholder_opacity = 1.0f;
    f->caret_on = 1;
}

void form_control_prop_release(FormControlProp* f) {
    if (!f) return;
    if (f->current_value) { mem_free(f->current_value); f->current_value = nullptr; }
    if (f->custom_validity_msg) { mem_free(f->custom_validity_msg); f->custom_validity_msg = nullptr; }
    if (f->value_at_focus) { mem_free(f->value_at_focus); f->value_at_focus = nullptr; }
    if (f->history) { te_history_free((EditHistory*)f->history); f->history = nullptr; }
    if (f->preedit_utf8) { mem_free(f->preedit_utf8); f->preedit_utf8 = nullptr; }
}

FormControlProp* tc_get_or_create_form(DomElement* elem) {
    if (elem->form && elem->role_kind() == DomElement::ROLE_FORM)
        return elem->form;
    FormControlProp* f = (FormControlProp*)mem_calloc(1, sizeof(FormControlProp), MEM_CAT_LAYOUT);
    if (!f) return nullptr;
    form_control_prop_init(f);
    f->heap_allocated = 1;
    f->state_ref = elem && elem->doc ? (DocState*)elem->doc->state : nullptr;
    if (elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0) {
        f->control_type = FORM_CONTROL_TEXTAREA;
    } else {
        f->control_type = FORM_CONTROL_TEXT;
    }
    elem->form = f;
    elem->set_role_kind(DomElement::ROLE_FORM);
    return f;
}

void form_control_release_prop(DomElement* elem) {
    if (!elem || elem->role_kind() != DomElement::ROLE_FORM || !elem->form) {
        return;
    }

    FormControlProp* form = elem->form;
    if (form->placeholder_font) {
        font_prop_release_handle(form->placeholder_font);
        form->placeholder_font = nullptr;
    }
    form_control_prop_release(form);
    if (form->heap_allocated) {
        mem_free(form);
    }
    elem->form = nullptr;
    elem->set_role_kind(DomElement::ROLE_NONE);
}

// ---- internal: initial value resolution --------------------------------

static char* tc_initial_value(DomElement* elem, uint32_t* out_len) {
    *out_len = 0;
    if (!elem) return nullptr;
    if (elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0) {
        StrBuf* sb = strbuf_new_cap(64);
        tc_collect_text((DomNode*)elem, sb);
        size_t len = sb->str ? strlen(sb->str) : 0;
        const char* attr_value = nullptr;
        if (len == 0) {
            attr_value = elem->get_attribute("value");
            if (attr_value) len = strlen(attr_value);
        }
        char* out = (char*)mem_alloc(len + 1, MEM_CAT_DOM);
        if (attr_value) memcpy(out, attr_value, len);
        else if (sb->str) memcpy(out, sb->str, len);
        out[len] = '\0';
        strbuf_free(sb);
        *out_len = (uint32_t)len;
        return out;
    }
    const char* v = elem->get_attribute("value");
    if (!v) v = "";
    size_t len = strlen(v);
    char* out = (char*)mem_alloc(len + 1, MEM_CAT_DOM);
    memcpy(out, v, len);
    out[len] = '\0';
    *out_len = (uint32_t)len;
    return out;
}

static void tc_materialize_current_value(DomElement* elem, FormControlProp* f) {
    if (!elem || !f || f->current_value) return;
    uint32_t len = 0;
    f->current_value = tc_initial_value(elem, &len);
    f->current_value_len = len;
    f->current_value_u16_len = tc_utf8_to_utf16_length(f->current_value, len);
    f->value = f->current_value;
}

static void tc_clamp_selection_to_current_value(FormControlProp* f) {
    if (!f) return;
    if (f->selection_start > f->current_value_u16_len) {
        f->selection_start = f->current_value_u16_len;
    }
    if (f->selection_end > f->current_value_u16_len) {
        f->selection_end = f->current_value_u16_len;
    }
    if (f->selection_end < f->selection_start) {
        f->selection_end = f->selection_start;
    }
    if (f->selection_direction > 2) f->selection_direction = 0;
}

// ---- public: lazy init + writes ----------------------------------------

void tc_ensure_init(DomElement* elem) {
    if (!tc_is_text_control(elem)) return;
    FormControlProp* f = tc_get_or_create_form(elem);
    if (f->tc_initialized) {
        tc_materialize_current_value(elem, f);
        tc_clamp_selection_to_current_value(f);
        if (!f->value || f->value != f->current_value) {
            f->value = f->current_value;
        }
        return;
    }
    DocState* state = f->state_ref
        ? f->state_ref
        : (elem && elem->doc ? (DocState*)elem->doc->state : nullptr);
    f->state_ref = state;
    bool restored = form_control_restore_text_control_state(state, (View*)elem);
    if (!restored) {
        if (!f->current_value) {
            tc_materialize_current_value(elem, f);
        } else {
            f->current_value_u16_len = tc_utf8_to_utf16_length(
                f->current_value, f->current_value_len);
        }
        f->selection_start = f->current_value_u16_len;
        f->selection_end = f->current_value_u16_len;
        f->selection_direction = 0;
        f->tc_initialized = 1;
        form_control_sync_text_control_state(f->state_ref, (View*)elem);
    }
    // Mirror live value into legacy display field used by render_form.cpp.
    if (!f->value || f->value != f->current_value) {
        f->value = f->current_value;
    }
    // F4: seed :placeholder-shown after initial value load.
    tc_refresh_placeholder_shown(elem, f);

    // F5: seed :valid / :invalid / :required / :read-only on first init so
    // CSS selectors match before any user interaction.
    te_validate(elem);
    // F8: ARIA reflection — push disabled/readonly/required/invalid bits
    // onto matching aria-* attributes for AT consumers.
    te_aria_reflect(elem);
}

// F4 (Radiant_Design_Form_Input.md §3.8): refresh :placeholder-shown bit.
// Set when the control is empty AND a placeholder is configured.
void tc_refresh_placeholder_shown(DomElement* elem, FormControlProp* f) {
    if (!elem || !f) return;
    bool show = (f->current_value_len == 0) && f->placeholder && f->placeholder[0];
    state_set_bool(f->state_ref ? f->state_ref : (elem->doc ? (DocState*)elem->doc->state : nullptr),
        elem, STATE_PLACEHOLDER, show);
}

// F4 (Radiant_Design_Form_Input.md §3.5): suppress recursive history push
// when undo/redo restore is itself calling tc_set_value.
static thread_local int g_tc_history_guard = 0;
extern "C" void tc_history_guard_enter() { g_tc_history_guard++; }
extern "C" void tc_history_guard_exit () { if (g_tc_history_guard > 0) g_tc_history_guard--; }

void tc_set_value(DomElement* elem, const char* new_val, size_t new_len) {
    if (!tc_is_text_control(elem)) return;
    FormControlProp* f = tc_get_or_create_form(elem);
    DocState* state = f->state_ref ? f->state_ref : (elem && elem->doc ? (DocState*)elem->doc->state : nullptr);

    // F4: enforce HTML `maxlength` on every value mutation. The attribute
    // counts UTF-16 code units in the spec; we approximate with codepoints
    // for the truncation step (matches our existing UTF-8↔UTF-16 helpers).
    // Truncation happens at a UTF-8 character boundary so we never split a
    // multi-byte sequence. Skipped for the initial init load (when the
    // legacy `value` attribute may be longer than the new maxlength).
    if (f->tc_initialized && f->maxlength >= 0 && new_val && new_len > 0) {
        uint32_t cp_count = 0;
        size_t   byte_at  = 0;
        while (byte_at < new_len && cp_count < (uint32_t)f->maxlength) {
            unsigned char b = (unsigned char)new_val[byte_at];
            size_t step = (b < 0x80) ? 1 : (b < 0xC0) ? 1
                          : (b < 0xE0) ? 2 : (b < 0xF0) ? 3 : 4;
            if (byte_at + step > new_len) break;
            byte_at += step;
            cp_count++;
        }
        if (byte_at < new_len) {
            log_debug("tc_set_value: maxlength=%d clamped %zu -> %zu bytes",
                      f->maxlength, new_len, byte_at);
            new_len = byte_at;
        }
    }

    uint32_t old_start = f->selection_start;
    uint32_t old_end = f->selection_end;
    uint8_t  old_dir = f->selection_direction;
    bool was_initialized = f->tc_initialized;
    if (f->current_value) mem_free(f->current_value);
    char* buf = (char*)mem_alloc(new_len + 1, MEM_CAT_DOM);
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
    form_control_sync_text_control_state(state, (View*)elem);
    if (state) {
        state_store_set_text_control_selection(state, elem,
            f->selection_start, f->selection_end, f->selection_direction);
    }
    form_control_sync_text_control_focus_state(state, (View*)elem);
    // Notify if value-setter caused the selection to move (e.g. previous
    // selection was past the new length and got clamped). Suppress on
    // initial init so parsing-time setup doesn't fire spurious events.
    if (was_initialized &&
        (f->selection_start != old_start ||
         f->selection_end != old_end ||
         f->selection_direction != old_dir)) {
        tc_notify_selection_changed(elem);
    }

    // F4: snapshot the post-mutation state into the undo ring (skipped
    // during initial init and while undo/redo is restoring).
    if (was_initialized && g_tc_history_guard == 0) {
        te_history_push(elem);
    }

    // F4: refresh :placeholder-shown after value changes (incl. clear).
    tc_refresh_placeholder_shown(elem, f);

    // F5: re-evaluate :valid / :invalid (and friends) after every mutation.
    te_validate(elem);
    // F8: keep aria-invalid in sync with the new validity state.
    te_aria_reflect(elem);
}

void tc_set_selection_range(DomElement* elem,
                            uint32_t start, uint32_t end,
                            uint8_t dir) {
    tc_ensure_init(elem);
    FormControlProp* f = tc_get_or_create_form(elem);
    DocState* state = f->state_ref ? f->state_ref : (elem && elem->doc ? (DocState*)elem->doc->state : nullptr);
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
    form_control_sync_text_control_state(state, (View*)elem);
    if (state) {
        state_store_set_text_control_selection(state, elem,
            f->selection_start, f->selection_end, f->selection_direction);
    }
    form_control_sync_text_control_focus_state(state, (View*)elem);
    if (start != old_start || end != old_end || dir != old_dir) {
        tc_notify_selection_changed(elem);
    }
}

// ---- public: text-control projection sync -----------------------------

// Locate the focused element's containing element. The caret projection stores
// a View*; for form controls the View is the ViewBlock that is also
// the DomElement (since DomElement extends ViewBlock in radiant's unified
// DOM). Caller passes the *known* DomElement so we don't have to dispatch.

void tc_sync_selection_to_form(DomElement* elem, DocState* state) {
    if (!elem || !state) return;
    if (!tc_is_text_control(elem)) return;
    tc_ensure_init(elem);
    FormControlProp* f = elem->form;
    if (!f->current_value) {
        const char* val = f->value ? f->value : "";
        uint32_t blen = (uint32_t)strlen(val);
        f->current_value = (char*)mem_alloc(blen + 1, MEM_CAT_DOM);
        if (!f->current_value) return;
        memcpy(f->current_value, val, blen);
        f->current_value[blen] = '\0';
        f->current_value_len = blen;
    }
    uint32_t blen = f->current_value_len;
    f->value = f->current_value;
    f->current_value_u16_len = tc_utf8_to_utf16_length(f->current_value, blen);

    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL &&
        state->sel.control == elem) {
        uint8_t direction = f->selection_direction;
        if (state->sel.start_u16 != state->sel.end_u16) {
            direction = state->sel.direction == DOM_SEL_DIR_BACKWARD ? 2 : 1;
        }
        state_store_set_text_control_selection(state, elem,
            state->sel.start_u16, state->sel.end_u16, direction);
        return;
    }

    bool have_projection_range = false;
    int anchor_byte = 0;
    int focus_byte = 0;
    View* view = (View*)elem;
    View* anchor_view = nullptr;
    View* focus_view = nullptr;
    if (selection_get_anchor_snapshot(state, &anchor_view, &anchor_byte, nullptr) &&
        selection_get_focus_snapshot(state, &focus_view, &focus_byte,
            nullptr, nullptr, nullptr) &&
        anchor_view == view && focus_view == view) {
        have_projection_range = true;
    } else if (caret_get_position(state, &focus_view, &focus_byte) &&
               focus_view == view) {
        anchor_byte = focus_byte;
        have_projection_range = true;
    } else if (state->dom_selection && state->dom_selection->range_count > 0) {
        DomSelection* ds = state->dom_selection;
        DomBoundary anchor = dom_selection_anchor_boundary(ds);
        DomBoundary focus = dom_selection_focus_boundary(ds);
        if (anchor.node == (DomNode*)elem && focus.node == (DomNode*)elem) {
            anchor_byte = static_cast<int>(anchor.offset); // INT_CAST_OK: fallback DomSelection text-control offsets are bytes.
            focus_byte = static_cast<int>(focus.offset); // INT_CAST_OK: fallback DomSelection text-control offsets are bytes.
            have_projection_range = true;
        }
    }

    if (!have_projection_range) {
        form_control_sync_text_control_state(state, (View*)elem);
        return;
    }

    if (anchor_byte < 0) anchor_byte = 0;
    if (focus_byte < 0) focus_byte = 0;
    if ((uint32_t)anchor_byte > blen) anchor_byte = static_cast<int>(blen); // INT_CAST_OK: text-control byte offsets use StateStore int APIs.
    if ((uint32_t)focus_byte > blen) focus_byte = static_cast<int>(blen); // INT_CAST_OK: text-control byte offsets use StateStore int APIs.
    uint32_t lo = anchor_byte < focus_byte ? (uint32_t)anchor_byte : (uint32_t)focus_byte;
    uint32_t hi = anchor_byte < focus_byte ? (uint32_t)focus_byte : (uint32_t)anchor_byte;
    uint8_t direction = 0;
    if (anchor_byte < focus_byte) {
        direction = 1;
    } else if (anchor_byte > focus_byte) {
        direction = 2;
    }
    uint32_t start_u16 = tc_utf8_to_utf16_offset(f->current_value, blen, lo);
    uint32_t end_u16 = tc_utf8_to_utf16_offset(f->current_value, blen, hi);
    state_store_set_text_control_selection(state, elem, start_u16, end_u16, direction);
}

// ---- public: focus tracker ---------------------------------------------

static DocState* tc_resolve_state(DocState* state, DomElement* elem) {
    if (state) return state;
    return elem && elem->doc ? (DocState*)elem->doc->state : nullptr;
}

void tc_set_active_element(DocState* state, DomElement* elem) {
    DocState* resolved = tc_resolve_state(state, elem);
    if (!resolved) return;
    if (elem && focus_get(resolved) != (View*)elem) return;
    resolved->active_text_control = elem;
}

DomElement* tc_get_active_element(DocState* state) {
    return state ? state->active_text_control : nullptr;
}

void tc_set_last_focused_text_control(DocState* state, DomElement* elem) {
    DocState* resolved = tc_resolve_state(state, elem);
    if (!resolved) return;
    resolved->last_focused_text_control = elem;
}

DomElement* tc_get_last_focused_text_control(DocState* state) {
    return state ? state->last_focused_text_control : nullptr;
}

void tc_reset_focus_state(DocState* state) {
    if (!state) return;
    state->active_text_control = nullptr;
    state->last_focused_text_control = nullptr;
}

// ---- public: Selection.toString() bridge -------------------------------

const char* tc_active_selected_text(DocState* state, uint32_t* out_byte_len) {
    if (out_byte_len) *out_byte_len = 0;
    DomElement* tc = tc_get_last_focused_text_control(state);
    if (!tc || !tc_is_text_control(tc)) return nullptr;
    DomElement* active = tc_get_active_element(state);
    if (active && tc_is_text_control(active) && active != tc) {
        tc = active;
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
