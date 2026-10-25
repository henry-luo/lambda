#include "event.hpp"
#include "render.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include "../lib/mem.h"
#include <string.h>
#include <math.h>

extern void handle_event(UiContext* ui, DomDocument* doc, RdtEvent* event);
extern "C" bool radiant_editing_focused_caret_rect(UiContext*, float*, float*, float*, float*);



static const float IME_ROW_HEIGHT = 30.0f;

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


static void place_popup(UiContext* ui, InputMethodTransport* popup) {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    bool caret = radiant_editing_focused_caret_rect(ui, &x, &y, &width, &height);
    popup->width = fminf(260.0f, ui->viewport_width);
    popup->height = IME_ROW_HEIGHT * (popup->count + 1);
    popup->x = fmaxf(0.0f, fminf(x, ui->viewport_width - popup->width));
    popup->y = caret ? y + height + 3.0f : ui->viewport_height - popup->height;
    if (popup->y + popup->height > ui->viewport_height) popup->y = fmaxf(0.0f, y - popup->height);
}

bool input_method_popup(UiContext* ui, DomElement* target, Item frame, const char* mode) {
    if (!ui || !target || !target->doc || !target->doc->state || !mode) return false;
    DocState* state = target->doc->state;
    InputMethodTransport* popup = &state->input_method;
    bool visible = strcmp(mode, "off") != 0;
    if (!visible && !popup->visible) return true;
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
    // the public reducer's page size is 1..9; native owns only the displayed page.
    for (int64_t i = 0; i < items.length() && i < 9; i++) {
        const char* text = items.get(i).asMap().get("text").cstring();
        if (!text) continue;
        popup->labels[popup->count++] = mem_strdup(text, MEM_CAT_DOM);
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
    // INT_CAST_OK: popup row is a bounded array index, not a layout dimension.
    return (int)((y - popup->y) / IME_ROW_HEIGHT) - 1;
}

bool input_method_popup_row_rect(UiContext* ui, DocState* state, int row, Rect* out) {
    if (!ui || !state || !out || !state->input_method.visible || row < -1 ||
        row >= state->input_method.count) return false;
    InputMethodTransport* popup = &state->input_method;
    place_popup(ui, popup);
    *out = {popup->x, popup->y + (row + 1) * IME_ROW_HEIGHT, popup->width, IME_ROW_HEIGHT};
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
    place_popup(rdcon->ui_context, popup);
    float scale = rdcon->raster_scale;
    float x = popup->x * scale, y = popup->y * scale, width = popup->width * scale;
    Bound clip = rdcon->block.clip;
    auto depth = rdcon->clip_shape_depth;
    rdcon->block.clip.left = rdcon->block.clip.top = 0;
    rdcon->block.clip.right = (float)rdcon->ui_context->surface->width;
    rdcon->block.clip.bottom = (float)rdcon->ui_context->surface->height;
    rdcon->clip_shape_depth = 0;
    Color background = {.c = 0xffffffff}, ink = {.c = 0xff202020}, selected = {.c = 0xffffe7d5};
    Rect box = {x, y, width, popup->height * scale};
    rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &box, ink.c, &rdcon->block.clip, nullptr, 0);
    box = {x + scale, y + scale, width - 2 * scale, popup->height * scale - 2 * scale};
    rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &box, background.c, &rdcon->block.clip, nullptr, 0);
    FontProp* font = &rdcon->ui_context->default_font;
    StrBuf* label = strbuf_new();
    strbuf_append_str(label, popup->chinese ? "中文" : "EN");
    if (popup->pages) strbuf_append_format(label, "  %d/%d", popup->page + 1, popup->pages);
    render_simple_string(rdcon, label->str, x + 10.0f * scale, y + 5.0f * scale, font, ink);
    for (int i = 0; i < popup->count; i++) {
        float row_y = y + (i + 1) * IME_ROW_HEIGHT * scale;
        if (i == popup->highlight) {
            box = {x + scale, row_y, width - 2 * scale, IME_ROW_HEIGHT * scale};
            rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &box, selected.c, &rdcon->block.clip, nullptr, 0);
        }
        strbuf_reset(label);
        strbuf_append_format(label, "%d  %s", i + 1, popup->labels[i]);
        render_simple_string(rdcon, label->str, x + 10.0f * scale, row_y + 5.0f * scale, font, ink);
    }
    strbuf_free(label);
    rdcon->block.clip = clip;
    rdcon->clip_shape_depth = depth;
}
