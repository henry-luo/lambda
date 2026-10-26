#include "event.hpp"
#include "render.hpp"
#include "layout.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include "../lib/mem.h"
#include <string.h>
#include <math.h>
#include <limits.h>

extern void handle_event(UiContext* ui, DomDocument* doc, RdtEvent* event);
extern "C" bool radiant_editing_focused_caret_rect(UiContext*, float*, float*, float*, float*);

static const float IME_CELL_PADDING = 8.0f;
static const float IME_POPUP_PADDING = 3.0f;

DomElement* input_method_target(DocState* state) {
    EditingSurface surface = {};
    if (!state || !editing_surface_from_focus(state, &surface) ||
        (!editing_surface_is_text_control(&surface) && !editing_surface_is_rich(&surface))) return nullptr;
    return surface.owner;
}

bool input_method_queue(DomElement* target, const char* phase, const char* text, uint32_t caret) {
    if (!target || !target->doc || !target->doc->state || !phase) return false;
    EventType type;
    if (strcmp(phase, "begin") == 0) type = RDT_EVENT_COMPOSITION_START;
    else if (strcmp(phase, "update") == 0) type = RDT_EVENT_COMPOSITION_UPDATE;
    else if (strcmp(phase, "commit") == 0 || strcmp(phase, "cancel") == 0) type = RDT_EVENT_COMPOSITION_END;
    else return false;
    DocState* state = target->doc->state;
    if (state->editing.composition.active && !state->editing.composition.scripted) return false;
    ScriptCompositionRequest* request = (ScriptCompositionRequest*)mem_calloc(1, sizeof(ScriptCompositionRequest), MEM_CAT_DOM);
    if (!request) return false;
    request->target = dom_node_ref(target);
    request->type = type;
    request->text = mem_strdup(strcmp(phase, "cancel") == 0 ? "" : (text ? text : ""), MEM_CAT_DOM);
    request->caret = caret;
    if (!request->text) { mem_free(request); return false; }
    InputMethodTransport* transport = &state->input_method;
    if (transport->tail) transport->tail->next = request;
    else transport->head = request;
    transport->tail = request;
    doc_state_request_repaint(state);
    log_debug("ime-transport: queued type=%d target=%u", type, request->target.expected_id);
    return true;
}

void input_method_drain(UiContext* ui, DomDocument* doc) {
    if (!ui || !doc || !doc->state || doc->state->input_method.draining) return;
    DocState* state = doc->state;
    InputMethodTransport* transport = &state->input_method;
    transport->draining = true;
    ScriptCompositionRequest* boundary = transport->tail;
    // the request owns its text until the ordinary composition dispatcher returns.
    while (transport->head) {
        ScriptCompositionRequest* request = transport->head;
        transport->head = request->next;
        if (!transport->head) transport->tail = nullptr;
        DomNode* target = dom_node_ref_validate(doc, request->target);
        DomElement* focused = input_method_target(state);
        bool owner = state->editing.composition.active && state->editing.composition.scripted &&
            state->editing.composition.surface.owner == target;
        log_debug("ime-transport: drain type=%d target=%u focused=%d active=%d scripted=%d owner=%d",
            request->type, request->target.expected_id, target == focused,
            state->editing.composition.active, state->editing.composition.scripted, owner);
        if (target && dom_element_is_connected(lam::dom_require_element(target)) &&
            (target == focused || (owner && request->type == RDT_EVENT_COMPOSITION_END)) &&
            (!state->editing.composition.active || state->editing.composition.scripted)) {
            // composition must not erase the pending keydown's character-cancellation decision.
            bool pending = state->editing.pending_text_input;
            bool prevented = state->editing.pending_text_input_prevented;
            int key = state->editing.pending_text_input_key;
            RdtEvent event = {};
            event.composition.type = request->type;
            event.composition.text = request->text;
            event.composition.preedit_caret = request->caret;
            event.composition.scripted = true;
            handle_event(ui, doc, &event);
            state->editing.pending_text_input = pending;
            state->editing.pending_text_input_prevented = prevented;
            state->editing.pending_text_input_key = key;
        }
        bool last = request == boundary;
        mem_free(request->text);
        mem_free(request);
        // requests from a composition listener belong to the next host turn.
        if (last) break;
    }
    transport->draining = false;
}


static void popup_label(const InputMethodTransport* popup, int row, StrBuf* label) {
    strbuf_reset(label);
    if (row >= 0) {
        int shortcut = popup->expanded ? row - popup->shortcut_start : row;
        if (!popup->expanded || (shortcut >= 0 && shortcut < popup->preferred_columns))
            strbuf_append_format(label, "%d ", shortcut + 1);
        strbuf_append_str(label, popup->labels[row]);
    }
    else {
        strbuf_append_str(label, popup->chinese ? "中" : "EN");
        if (popup->expanded) strbuf_append_format(label, " · %d", popup->count);
        else if (popup->pages) strbuf_append_format(label, " %d/%d", popup->page + 1, popup->pages);
    }
}

static FontProp* place_popup(UiContext* ui, InputMethodTransport* popup) {
    DomElement* target = lam::dom_as<DOM_NODE_ELEMENT>(
        dom_node_ref_validate(ui->document, popup->popup_target));
    FontProp* source = target && target->font ? target->font.get() : &ui->default_font;
    FontProp* font = &popup->font;
    // retain the editor's family and zoom while giving the native strip smaller typography.
    font_prop_copy(font, source, nullptr);
    font->font_size = fminf(16.0f, source->font_size * 0.85f);
    FontBox box = {};
    setup_font(ui, &box, font);
    FontHandle* handle = font_box_handle(&box);
    float scale = ui_context_raster_scale(ui);
    float cell_height = fmaxf(28.0f, font_prop_used_size(font) + 12.0f);
    float zoom = font->used_zoom > 0.0f ? font->used_zoom : 1.0f;
    float limit = fmaxf(1.0f, ui->viewport_width - 8.0f);
    float padding = fminf(IME_POPUP_PADDING, limit / 4.0f);
    float cursor = padding, top = padding, right = padding;
    StrBuf* label = strbuf_new();
    float widest = 96.0f * zoom, mode_width = 0.0f;
    // labels are measured before grid placement, so responsive columns match hit geometry.
    for (int i = 0; i <= popup->count; i++) {
        int row = i == popup->count ? -1 : i;
        popup_label(popup, row, label);
        if (popup->expanded && row >= 0) {
            // reserve shortcut width on every cell so highlight changes cannot reflow the grid.
            strbuf_reset(label);
            strbuf_append_format(label, "%d %s", row % popup->preferred_columns + 1, popup->labels[row]);
        }
        float text_width = handle ? layout_measure_utf8_text_width(handle, font,
            label->str, label->length, scale) : 0.0f;
        float width = fminf(text_width + 2.0f * IME_CELL_PADDING, limit - 2.0f * padding);
        if (row < 0) mode_width = width;
        else widest = fmaxf(widest, width);
        if (!popup->expanded) {
            if (cursor > padding && cursor + width > limit - padding) {
                cursor = padding;
                top += cell_height;
            }
            popup->cells[row + 1] = {cursor, top, width, cell_height};
            cursor += width;
            right = fmaxf(right, cursor);
        }
    }
    strbuf_free(label);
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    bool caret = radiant_editing_focused_caret_rect(ui, &x, &y, &width, &height);
    int previous_columns = popup->columns;
    popup->columns = popup->preferred_columns;
    if (popup->expanded) {
        float grid_limit = fminf(limit, 760.0f * zoom);
        float available = fmaxf(1.0f, grid_limit - 2.0f * padding - 12.0f);
        float cell_width = fminf(available, fminf(widest, 180.0f * zoom));
        // INT_CAST_OK: columns count bounded candidate cells, not a layout dimension.
        int fitting = (int)floorf(available / cell_width);
        popup->columns = fitting < popup->preferred_columns ? fitting : popup->preferred_columns;
        if (popup->columns < 1) popup->columns = 1;
        int rows = (popup->count + popup->columns - 1) / popup->columns;
        float content_height = rows * cell_height;
        float below = ui->viewport_height - y - height - 3.0f;
        float budget = caret ? fmaxf(below, y - 3.0f) : ui->viewport_height;
        popup->body_height = fminf(content_height, fminf(5.0f * cell_height,
            fmaxf(0.0f, budget - cell_height - 2.0f * padding)));
        popup->scroll_max = fmaxf(0.0f, content_height - popup->body_height);
        if ((popup->ensure_highlight || previous_columns != popup->columns) &&
            popup->highlight >= 0 && popup->highlight < popup->count) {
            float selected_top = (popup->highlight / popup->columns) * cell_height;
            if (selected_top < popup->scroll_y) popup->scroll_y = selected_top;
            else if (selected_top + cell_height > popup->scroll_y + popup->body_height)
                popup->scroll_y = selected_top + cell_height - popup->body_height;
        }
        popup->scroll_y = fmaxf(0.0f, fminf(popup->scroll_y, popup->scroll_max));
        popup->ensure_highlight = false;
        popup->width = fmaxf(mode_width + 2.0f * padding,
            popup->columns * cell_width + 2.0f * padding + 12.0f);
        popup->height = popup->body_height + cell_height + 2.0f * padding;
        popup->cells[0] = {padding, padding + popup->body_height, mode_width, cell_height};
        for (int i = 0; i < popup->count; i++)
            popup->cells[i + 1] = {padding + (i % popup->columns) * cell_width,
                padding + (i / popup->columns) * cell_height - popup->scroll_y, cell_width, cell_height};
    } else {
        popup->width = right + padding;
        popup->height = top + cell_height + padding;
        popup->body_height = popup->height;
        popup->scroll_y = popup->scroll_max = 0.0f;
    }
    popup->x = fmaxf(0.0f, fminf(x, ui->viewport_width - popup->width));
    popup->y = caret ? y + height + 3.0f : ui->viewport_height - popup->height;
    if (popup->y + popup->height > ui->viewport_height) popup->y = fmaxf(0.0f, y - popup->height);
    for (int i = 0; i <= popup->count; i++) {
        popup->cells[i].x += popup->x;
        popup->cells[i].y += popup->y;
    }
    return font;
}

bool input_method_popup(UiContext* ui, DomElement* target, Item frame, const char* mode) {
    if (!ui || !target || !target->doc || !target->doc->state || !mode) return false;
    DocState* state = target->doc->state;
    InputMethodTransport* popup = &state->input_method;
    bool visible = strcmp(mode, "off") != 0;
    if (!visible && !popup->visible) return true;
    bool was_expanded = popup->expanded;
    int previous_highlight = popup->highlight;
    input_method_clear_labels(popup);
    popup->visible = visible;
    popup->chinese = strcmp(mode, "chinese") == 0;
    popup->popup_target = dom_node_ref(target);
    MarkReader mark(frame);
    MapReader reader = mark.getRoot().asMap();
    ArrayReader items = reader.get("items").asArray();
    popup->page = reader.get("page").asInt32();
    popup->pages = reader.get("pages").asInt32();
    popup->highlight = reader.get("highlight").asInt32();
    popup->expanded = reader.get("expanded").asBool();
    popup->preferred_columns = reader.get("columns").asInt32();
    if (popup->preferred_columns < 1 || popup->preferred_columns > 9) popup->preferred_columns = 7;
    popup->shortcut_start = popup->page * popup->preferred_columns;
    popup->ensure_highlight = !was_expanded || previous_highlight != popup->highlight;
    if (!was_expanded || !popup->expanded) popup->scroll_y = 0.0f;
    // frame ownership is native; all returned candidates survive the originating GC value.
    if (items.length() < 0 || items.length() >= INT_MAX) {
        input_method_cleanup(state);
        return false;
    }
    popup->labels = (char**)mem_calloc(items.length(), sizeof(char*), MEM_CAT_DOM);
    popup->cells = (Rect*)mem_calloc(items.length() + 1, sizeof(Rect), MEM_CAT_DOM);
    if ((!popup->labels && items.length()) || !popup->cells) {
        input_method_cleanup(state);
        return false;
    }
    for (int64_t i = 0; i < items.length(); i++) {
        const char* text = items.get(i).asMap().get("text").cstring();
        if (!text) continue;
        char* label = mem_strdup(text, MEM_CAT_DOM);
        if (!label) { input_method_cleanup(state); return false; }
        popup->labels[popup->count++] = label;
    }
    place_popup(ui, popup);
    doc_state_mark_dirty(state);
    return true;
}

int input_method_popup_hit(UiContext* ui, DocState* state, float x, float y) {
    if (!ui || !state || !state->input_method.visible) return -2;
    InputMethodTransport* popup = &state->input_method;
    place_popup(ui, popup);
    if (x < popup->x || x >= popup->x + popup->width || y < popup->y || y >= popup->y + popup->height) return -2;
    for (int i = 0; i <= popup->count; i++) {
        const Rect& cell = popup->cells[i];
        if (popup->expanded && i > 0 && (y < popup->y + IME_POPUP_PADDING ||
            y >= popup->y + IME_POPUP_PADDING + popup->body_height)) continue;
        if (x >= cell.x && x < cell.x + cell.width && y >= cell.y && y < cell.y + cell.height)
            return i - 1;
    }
    return -3; // padding belongs to the popup and must not focus the underlying control.

}

bool input_method_popup_scroll(UiContext* ui, DocState* state, float x, float y, float delta) {
    if (!ui || !state || !state->input_method.expanded ||
        input_method_popup_hit(ui, state, x, y) == -2) return false;
    InputMethodTransport* popup = &state->input_method;
    popup->scroll_y = fmaxf(0.0f, fminf(popup->scroll_max, popup->scroll_y + delta));
    return true;
}

bool input_method_popup_row_rect(UiContext* ui, DocState* state, int row, Rect* out) {
    if (!ui || !state || !out || !state->input_method.visible || row < -1 ||
        row >= state->input_method.count) return false;
    InputMethodTransport* popup = &state->input_method;
    place_popup(ui, popup);
    *out = popup->cells[row + 1];
    if (popup->expanded && row >= 0) {
        float top = fmaxf(out->y, popup->y + IME_POPUP_PADDING);
        float bottom = fminf(out->y + out->height, popup->y + IME_POPUP_PADDING + popup->body_height);
        if (bottom <= top) return false;
        out->y = top;
        out->height = bottom - top;
    }
    return true;
}

void input_method_popup_render(RasterRenderContext* rdcon, DocState* state) {
    if (!rdcon || !rdcon->ui_context || !rdcon->ui_context->surface ||
        !state || !state->input_method.visible) return;
    InputMethodTransport* popup = &state->input_method;
    DomNode* target = dom_node_ref_validate(rdcon->ui_context->document, popup->popup_target);
    if (!target || !dom_element_is_connected(lam::dom_require_element(target))) {
        input_method_cleanup(state);
        return;
    }
    FontProp* font = place_popup(rdcon->ui_context, popup);
    float scale = rdcon->raster_scale;
    float x = popup->x * scale, y = popup->y * scale, width = popup->width * scale;
    Bound clip = rdcon->block.clip;
    auto depth = rdcon->clip_shape_depth;
    rdcon->block.clip.left = rdcon->block.clip.top = 0;
    rdcon->block.clip.right = (float)rdcon->ui_context->surface->width;
    rdcon->block.clip.bottom = (float)rdcon->ui_context->surface->height;
    rdcon->clip_shape_depth = 0;
    Color background = {.c = 0xffffffff}, ink = {.c = 0xff202020};
    Color border = {.c = 0xffb8b4b0}, selected = {.c = 0xffff7b00}, muted = {.c = 0xff777777};
    float height = popup->height * scale;
    rc_fill_rounded_rect(rdcon, x, y, width, height, 14.0f * scale, 14.0f * scale, border);
    rc_fill_rounded_rect(rdcon, x + scale, y + scale, width - 2.0f * scale,
        height - 2.0f * scale, 13.0f * scale, 13.0f * scale, background);
    StrBuf* label = strbuf_new();
    RenderClipScope body_scope = {};
    for (int row = -1; row < popup->count; row++) {
        if (row == 0 && popup->expanded) {
            Bound body = rdcon->block.clip;
            body.left = (popup->x + IME_POPUP_PADDING) * scale;
            body.right = (popup->x + popup->width - IME_POPUP_PADDING) * scale;
            body.top = (popup->y + IME_POPUP_PADDING) * scale;
            body.bottom = body.top + popup->body_height * scale;
            body_scope = render_clip_push_rect_scope(rdcon, &body);
        }
        const Rect& cell = popup->cells[row + 1];
        Bound saved_clip = rdcon->block.clip;
        if (popup->expanded && row >= 0) {
            rdcon->block.clip.top = (popup->y + IME_POPUP_PADDING) * scale;
            rdcon->block.clip.bottom = rdcon->block.clip.top + popup->body_height * scale;
            if ((cell.y + cell.height) * scale <= rdcon->block.clip.top ||
                cell.y * scale >= rdcon->block.clip.bottom) {
                rdcon->block.clip = saved_clip;
                continue;
            }
        }
        bool active = row >= 0 && row == popup->highlight;
        if (active) {
            float radius = (cell.height - 4.0f) * scale / 2.0f;
            rc_fill_rounded_rect(rdcon, (cell.x + 2.0f) * scale, (cell.y + 2.0f) * scale,
                (cell.width - 4.0f) * scale, (cell.height - 4.0f) * scale,
                radius, radius, selected);
        }
        popup_label(popup, row, label);
        rdcon->block.clip.left = cell.x * scale;
        rdcon->block.clip.right = (cell.x + cell.width) * scale;
        render_simple_string(rdcon, label->str, (cell.x + IME_CELL_PADDING) * scale,
            (cell.y + (cell.height - font_prop_used_size(font)) / 2.0f) * scale,
            font, active ? background : (row < 0 ? muted : ink));
        rdcon->block.clip = saved_clip;
    }
    render_clip_pop_scope(rdcon, &body_scope);
    if (popup->expanded) {
        Color separator = {.c = 0xffe5e5e5};
        rc_fill_rect(rdcon, (popup->x + IME_POPUP_PADDING) * scale,
            (popup->y + IME_POPUP_PADDING + popup->body_height) * scale,
            (popup->width - 2.0f * IME_POPUP_PADDING) * scale, scale, separator);
    }
    if (popup->expanded && popup->scroll_max > 0.0f) {
        float track = popup->body_height;
        float thumb = fmaxf(16.0f, track * track / (track + popup->scroll_max));
        float thumb_y = popup->scroll_y / popup->scroll_max * (track - thumb);
        rc_fill_rounded_rect(rdcon, (popup->x + popup->width - 9.0f) * scale,
            (popup->y + IME_POPUP_PADDING + thumb_y) * scale, 5.0f * scale, thumb * scale,
            2.5f * scale, 2.5f * scale, muted);
    }
    strbuf_free(label);
    rdcon->block.clip = clip;
    rdcon->clip_shape_depth = depth;
}
