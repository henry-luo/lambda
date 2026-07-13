// F8 (Radiant_Design_Form_Input.md §3.10): native context menu impl.

#include "view.hpp"
#include "render.hpp"
#include "event.hpp"
#include "form_control.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <string.h>

// Local copies of helpers from render_form.cpp (static there). Mirrors the
// dropdown popup style.
static inline Color ctx_make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    Color c; c.r = r; c.g = g; c.b = b; c.a = a; return c;
}
static inline void ctx_fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color) {
    Rect rect = {x, y, w, h};
    rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, color.c, &rdcon->block.clip,
                         rdcon->clip_shapes, rdcon->clip_shape_depth);
}

static const char* CTX_MENU_LABELS[CTX_MENU_ITEM_COUNT] = {
    "Cut", "Copy", "Paste", "Delete", "Select All",
};

// Logical CSS-pixel sizing — multiplied by RenderContext::scale at draw.
static const float CTX_MENU_WIDTH       = 140.0f;
static const float CTX_MENU_ITEM_HEIGHT = 24.0f;
static const float CTX_MENU_PADDING_X   = 12.0f;

static DomElement* ctx_menu_target_elem(DocState* state) {
    if (!state || !state->context_menu_target) return nullptr;
    View* v = state->context_menu_target;
    if (!v->is_element()) return nullptr;
    DomElement* e = lam::dom_require_element(v);
    return tc_is_text_control(e) ? e : nullptr;
}

static bool ctx_menu_has_selection(DocState* state, DomElement* elem) {
    if (!elem || !elem->form) return false;
    uint32_t start = 0, end = 0;
    form_control_get_selection(state, static_cast<View*>(elem), &start, &end, NULL);
    return start != end;
}

bool context_menu_item_enabled(DocState* state, int item) {
    DomElement* elem = ctx_menu_target_elem(state);
    if (!elem || !elem->form) return false;
    bool readonly = form_control_is_readonly(state, static_cast<View*>(elem));
    bool disabled = form_control_is_disabled(state, static_cast<View*>(elem));
    if (disabled) return false;
    bool has_sel  = ctx_menu_has_selection(state, elem);
    bool has_val  = elem->form->current_value_len > 0;
    const char* clip = clipboard_get_text();
    bool has_clip = clip && *clip;

    switch (item) {
        case CTX_MENU_CUT:        return has_sel && !readonly;
        case CTX_MENU_COPY:       return has_sel;
        case CTX_MENU_PASTE:      return has_clip && !readonly;
        case CTX_MENU_DELETE:     return has_sel && !readonly;
        case CTX_MENU_SELECT_ALL: return has_val;
        default: return false;
    }
}

void context_menu_open(DocState* state, View* target, float x, float y) {
    if (!state || !target) return;
    if (!target->is_element()) return;
    DomElement* e = lam::dom_require_element(target);
    if (!tc_is_text_control(e)) return;

    // Width/height are nominal CSS-px values; render scales them.
    doc_state_open_context_menu(state, target, x, y,
        CTX_MENU_WIDTH, CTX_MENU_ITEM_HEIGHT * CTX_MENU_ITEM_COUNT);
    log_debug("context_menu_open at (%.1f, %.1f)", x, y);
}

void context_menu_close(DocState* state) {
    if (!state || !state->context_menu_target) return;
    doc_state_close_context_menu(state);
    log_debug("context_menu_close");
}

bool context_menu_contains(DocState* state, float x, float y) {
    if (!state || !state->context_menu_target) return false;
    return x >= state->context_menu_x &&
           x <  state->context_menu_x + state->context_menu_width &&
           y >= state->context_menu_y &&
           y <  state->context_menu_y + state->context_menu_height;
}

bool context_menu_hover(DocState* state, float x, float y) {
    if (!context_menu_contains(state, x, y)) {
        if (state && state->context_menu_target && state->context_menu_hover != -1) {
            doc_state_set_context_menu_hover(state, -1);
        }
        return false;
    }
    int idx = (int)((y - state->context_menu_y) / CTX_MENU_ITEM_HEIGHT);
    if (idx < 0) idx = 0;
    if (idx >= CTX_MENU_ITEM_COUNT) idx = CTX_MENU_ITEM_COUNT - 1;
    if (state->context_menu_hover != idx) {
        doc_state_set_context_menu_hover(state, idx);
    }
    return true;
}

// Convert UTF-16 selection [a, b) to UTF-8 byte range over current_value.
static bool ctx_menu_selection_bytes(DomElement* elem, uint32_t* out_a, uint32_t* out_b) {
    if (!elem || !elem->form || !elem->form->current_value) return false;
    FormControlProp* f = elem->form;
    uint32_t a16 = f->selection_start, b16 = f->selection_end;
    if (a16 == b16) return false;
    if (a16 > b16) { uint32_t t = a16; a16 = b16; b16 = t; }
    uint32_t a = tc_utf16_to_utf8_offset(f->current_value, f->current_value_len, a16);
    uint32_t b = tc_utf16_to_utf8_offset(f->current_value, f->current_value_len, b16);
    if (a > b) { uint32_t t = a; a = b; b = t; }
    if (a > f->current_value_len) a = f->current_value_len;
    if (b > f->current_value_len) b = f->current_value_len;
    *out_a = a;
    *out_b = b;
    return true;
}

static void ctx_menu_exec_copy(DomElement* elem) {
    uint32_t a, b;
    if (!ctx_menu_selection_bytes(elem, &a, &b)) return;
    char* tmp = (char*)mem_alloc((size_t)(b - a) + 1, MEM_CAT_TEMP);
    if (!tmp) return;
    memcpy(tmp, elem->form->current_value + a, b - a);
    tmp[b - a] = '\0';
    clipboard_copy_text(tmp);
    mem_free(tmp);
}

static void ctx_menu_exec_cut(DomElement* elem, DocState* state,
                              const ContextMenuEditHooks* hooks) {
    uint32_t a, b;
    if (!ctx_menu_selection_bytes(elem, &a, &b)) return;
    ctx_menu_exec_copy(elem);
    if (hooks && hooks->cut_selection &&
        hooks->cut_selection(hooks->user, elem, state, a, b)) {
        return;
    }
    te_replace_byte_range(elem, state, static_cast<View*>(elem), a, b, nullptr, 0);
}

static void ctx_menu_exec_delete(DomElement* elem, DocState* state,
                                 const ContextMenuEditHooks* hooks) {
    uint32_t a, b;
    if (!ctx_menu_selection_bytes(elem, &a, &b)) return;
    if (hooks && hooks->delete_selection &&
        hooks->delete_selection(hooks->user, elem, state, a, b)) {
        return;
    }
    te_replace_byte_range(elem, state, static_cast<View*>(elem), a, b, nullptr, 0);
}

static void ctx_menu_exec_paste(DomElement* elem, DocState* state,
                                const ContextMenuEditHooks* hooks) {
    const char* clip = clipboard_get_text();
    if (!clip || !*clip) return;
    if (hooks && hooks->paste_text &&
        hooks->paste_text(hooks->user, elem, state, clip,
                          (uint32_t)strlen(clip))) {
        return;
    }
    te_paste(elem, state, static_cast<View*>(elem), clip, (uint32_t)strlen(clip));
}

static void ctx_menu_exec_select_all(DomElement* elem, DocState* state,
                                     const ContextMenuEditHooks* hooks) {
    if (!elem || !elem->form) return;
    if (hooks && hooks->select_all &&
        hooks->select_all(hooks->user, elem, state)) {
        return;
    }
    tc_set_selection_range(elem, 0, elem->form->current_value_u16_len, 1);
}

bool context_menu_click(DocState* state, float x, float y) {
    return context_menu_click_with_hooks(state, x, y, nullptr);
}

bool context_menu_click_with_hooks(DocState* state, float x, float y,
                                   const ContextMenuEditHooks* hooks) {
    if (!context_menu_contains(state, x, y)) return false;
    int idx = (int)((y - state->context_menu_y) / CTX_MENU_ITEM_HEIGHT);
    if (idx < 0 || idx >= CTX_MENU_ITEM_COUNT) {
        context_menu_close(state);
        return true;
    }
    DomElement* elem = ctx_menu_target_elem(state);
    if (!elem || !context_menu_item_enabled(state, idx)) {
        context_menu_close(state);
        return true;
    }
    log_info("context_menu_click: %s", CTX_MENU_LABELS[idx]);
    switch (idx) {
        case CTX_MENU_CUT:        ctx_menu_exec_cut(elem, state, hooks); break;
        case CTX_MENU_COPY:       ctx_menu_exec_copy(elem); break;
        case CTX_MENU_PASTE:      ctx_menu_exec_paste(elem, state, hooks); break;
        case CTX_MENU_DELETE:     ctx_menu_exec_delete(elem, state, hooks); break;
        case CTX_MENU_SELECT_ALL: ctx_menu_exec_select_all(elem, state, hooks); break;
    }
    context_menu_close(state);
    return true;
}

void context_menu_render(RenderContext* rdcon, DocState* state) {
    if (!state || !state->context_menu_target) return;
    if (!rdcon || !rdcon->ui_context || !rdcon->ui_context->surface) return;

    float s = rdcon->scale;
    float x = state->context_menu_x;
    float y = state->context_menu_y;
    float w = state->context_menu_width  * s;
    float ih = CTX_MENU_ITEM_HEIGHT * s;
    float h = state->context_menu_height * s;

    // Override clip to viewport so the popup isn't bound by parent clip rects.
    Bound saved_clip = rdcon->block.clip;
    rdcon->block.clip.left   = 0;
    rdcon->block.clip.top    = 0;
    rdcon->block.clip.right  = rdcon->ui_context->surface->width;
    rdcon->block.clip.bottom = rdcon->ui_context->surface->height;

    // Background + 1px border.
    Color bg     = ctx_make_color(255, 255, 255);
    Color border = ctx_make_color(118, 118, 118);
    ctx_fill_rect(rdcon, x, y, w, h, bg);
    float bw = 1 * s;
    ctx_fill_rect(rdcon, x, y, w, bw, border);
    ctx_fill_rect(rdcon, x, y + h - bw, w, bw, border);
    ctx_fill_rect(rdcon, x, y, bw, h, border);
    ctx_fill_rect(rdcon, x + w - bw, y, bw, h, border);

    // Pull a font from the target text control so labels match the page
    // font stack.
    DomElement* elem = ctx_menu_target_elem(state);
    FontProp* font = nullptr;
    if (elem) {
        ViewBlock* tblock = lam::view_require_block(static_cast<View*>(elem));
        font = tblock->font;
    }
    Color text_enabled  = ctx_make_color(0, 0, 0);
    Color text_disabled = ctx_make_color(160, 160, 160);
    Color hover_bg      = ctx_make_color(0, 120, 215);
    Color text_hover    = ctx_make_color(255, 255, 255);

    for (int i = 0; i < CTX_MENU_ITEM_COUNT; i++) {
        float iy = y + i * ih;
        bool enabled = context_menu_item_enabled(state, i);
        bool hovered = (state->context_menu_hover == i) && enabled;
        if (hovered) {
            ctx_fill_rect(rdcon, x + bw, iy, w - 2 * bw, ih, hover_bg);
        }
        if (font) {
            Color tc = hovered ? text_hover : (enabled ? text_enabled : text_disabled);
            float font_h = font->font_height * s;
            float text_y = iy + (ih - font_h) / 2;
            float text_x = x + CTX_MENU_PADDING_X * s;
            render_simple_string(rdcon, CTX_MENU_LABELS[i], text_x, text_y, font, tc);
        }
    }

    rdcon->block.clip = saved_clip;
}
