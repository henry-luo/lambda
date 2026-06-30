#include "editing.hpp"

#include "editing_host.hpp"
#include "editing_intent.hpp"
#include "form_control.hpp"
#include "state_store.hpp"
#include "text_control.hpp"
#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/tagged.hpp"

#include <strings.h>

static bool element_has_data_editable(DomElement* elem) {
    return elem && elem->has_attribute("data-editable");
}

static bool element_is_password_text_control(DomElement* elem) {
    if (!elem || !elem->tag_name || strcasecmp(elem->tag_name, "input") != 0) {
        return false;
    }
    const char* type = dom_element_get_attribute(elem, "type");
    return type && strcasecmp(type, "password") == 0;
}

static bool element_is_textarea(DomElement* elem) {
    return elem && elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0;
}

static DocState* element_doc_state(DomElement* elem) {
    return (elem && elem->doc) ? (DocState*)elem->doc->state : nullptr;
}

void editing_surface_clear(EditingSurface* out) {
    if (!out) return;
    out->kind = EDIT_SURFACE_NONE;
    out->mode = EDIT_MODE_RICH;
    out->owner = nullptr;
    out->view = nullptr;
    out->readonly = false;
    out->disabled = false;
    out->target_in_false_island = false;
}

bool editing_surface_from_target(View* target, EditingSurface* out) {
    if (out) editing_surface_clear(out);
    if (!target) return false;

    DomNode* node = static_cast<DomNode*>(target);
    for (DomNode* p = node; p; p = p->parent) {
        if (!p->is_element()) continue;
        DomElement* elem = p->as_element();

        if (tc_is_text_control(elem)) {
            if (out) {
                DocState* state = element_doc_state(elem);
                out->kind = EDIT_SURFACE_TEXT_CONTROL;
                out->mode = element_is_textarea(elem)
                    ? EDIT_MODE_MULTI_LINE_TEXT
                    : (element_is_password_text_control(elem)
                        ? EDIT_MODE_PASSWORD_TEXT
                        : EDIT_MODE_SINGLE_LINE_TEXT);
                out->owner = elem;
                out->view = static_cast<View*>(elem);
                out->readonly = form_control_is_readonly(state, static_cast<View*>(elem));
                out->disabled = form_control_is_disabled(state, static_cast<View*>(elem));
                out->target_in_false_island = false;
            }
            return true;
        }

        if (element_has_data_editable(elem)) {
            if (out) {
                out->kind = EDIT_SURFACE_LAMBDA_TEMPLATE;
                out->mode = EDIT_MODE_RICH;
                out->owner = elem;
                out->view = target;
                out->readonly = false;
                out->disabled = false;
                out->target_in_false_island = false;
            }
            return true;
        }

        EditingHost host;
        if (editing_host_lookup(elem, &host)) {
            if (out) {
                out->kind = EDIT_SURFACE_CONTENTEDITABLE;
                out->mode = host.mode == EditingHost::PlaintextOnly
                    ? EDIT_MODE_PLAINTEXT_ONLY
                    : EDIT_MODE_RICH;
                out->owner = host.host;
                out->view = target;
                out->readonly = false;
                out->disabled = false;
                out->target_in_false_island = host.target_in_false_island;
            }
            return true;
        }
    }

    return false;
}

bool editing_surface_from_focus(DocState* state, EditingSurface* out) {
    if (out) editing_surface_clear(out);
    if (!state) return false;
    View* focused = focus_get(state);
    if (!focused) return false;
    return editing_surface_from_target(focused, out);
}

bool editing_surface_is_rich(const EditingSurface* surface) {
    return surface &&
        (surface->kind == EDIT_SURFACE_CONTENTEDITABLE ||
         surface->kind == EDIT_SURFACE_LAMBDA_TEMPLATE);
}

bool editing_surface_is_text_control(const EditingSurface* surface) {
    return surface && surface->kind == EDIT_SURFACE_TEXT_CONTROL;
}

// Layer-A helpers retained from the retired editing_rich_transaction.cpp.
DomText* editing_rich_find_text_descendant(DomNode* node, bool last) {
    if (!node) return nullptr;
    if (node->is_text()) return lam::dom_require_text(node);
    if (!node->is_element()) return nullptr;

    DomElement* elem = lam::dom_require_element(node);
    DomText* found = nullptr;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        DomText* text = editing_rich_find_text_descendant(child, last);
        if (!text) continue;
        if (!last) return text;
        found = text;
    }
    return found;
}

bool editing_rich_is_composition_intent(const EditingIntent* intent) {
    return intent &&
        (intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT ||
         intent->type == INPUT_INTENT_INSERT_FROM_COMPOSITION ||
         intent->type == INPUT_INTENT_DELETE_COMPOSITION_TEXT);
}

const char* editing_surface_kind_name(EditingSurfaceKind kind) {
    switch (kind) {
        case EDIT_SURFACE_NONE: return "none";
        case EDIT_SURFACE_TEXT_CONTROL: return "text_control";
        case EDIT_SURFACE_CONTENTEDITABLE: return "contenteditable";
        case EDIT_SURFACE_LAMBDA_TEMPLATE: return "lambda_template";
        default: return "unknown";
    }
}

const char* editing_mode_name(EditingMode mode) {
    switch (mode) {
        case EDIT_MODE_RICH: return "rich";
        case EDIT_MODE_PLAINTEXT_ONLY: return "plaintext_only";
        case EDIT_MODE_SINGLE_LINE_TEXT: return "single_line_text";
        case EDIT_MODE_MULTI_LINE_TEXT: return "multi_line_text";
        case EDIT_MODE_PASSWORD_TEXT: return "password_text";
        default: return "unknown";
    }
}
