#include "event.hpp"

#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/runtime/render_map.h"
#include "../lambda/runtime/template_registry.h"
#include "../lib/log.h"
#include "../lib/tagged.hpp"

extern __thread EvalContext* context;

static bool editing_template_route_resolve(const EditingSurface* surface,
                                           EditingRouteSnapshot* out) {
    if (out) *out = {EDITING_ROUTE_NONE, nullptr, 0};
    if (!surface || !surface->owner || !context || !g_template_registry) {
        return false;
    }
    for (DomNode* node = static_cast<DomNode*>(surface->owner); node;
         node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* element = lam::dom_require_element(node);
        if (element->is_synthetic()) continue;
        Item element_item = {};
        element_item.element = dom_element_render_source(element);
        RenderMapLookup lookup = {};
        if (!render_map_reverse_lookup(element_item, &lookup)) continue;
        TemplateEntry* entry = template_registry_find_ref(g_template_registry,
                                                           lookup.template_ref);
        if (!entry || !entry->is_edit) continue;
        if (out) {
            out->kind = EDITING_ROUTE_RADIANT_TEMPLATE;
            out->owner = entry;
            out->owner_generation = surface->owner->doc && surface->owner->doc->state
                ? ((DocState*)surface->owner->doc->state)->version : 0;
        }
        return true;
    }
    return false;
}

EditingRouteSnapshot editing_route_snapshot(const EditingSurface* surface) {
    EditingRouteSnapshot route = {EDITING_ROUTE_DOM_SCRIPT, nullptr, 0};
    EditingRouteSnapshot template_route = {};
    if (editing_template_route_resolve(surface, &template_route)) {
        route = template_route;
    }
    return route;
}

static bool editing_template_action_matches(
        const EditingPreparedTransaction* prepared, void* user) {
    (void)user;
    return prepared && prepared->route.kind == EDITING_ROUTE_RADIANT_TEMPLATE;
}

static EditingActionOutcome editing_template_action_handle(
        EventContext* evcon, const EditingPreparedTransaction* prepared,
        void* user) {
    (void)user;
    EditingActionOutcome outcome = {EDITING_ACTION_PASS, false, false};
    if (!prepared || !evcon || !prepared->surface.view) return outcome;

    EditingRouteSnapshot current_route = editing_route_snapshot(&prepared->surface);
    if (current_route.kind != EDITING_ROUTE_RADIANT_TEMPLATE ||
        current_route.owner != prepared->route.owner ||
        current_route.owner_generation != prepared->route.owner_generation) {
        // Notification code may rebuild the host, but an already-selected
        // template action must never retarget an unrelated replacement entry.
        outcome.status = EDITING_ACTION_ERROR;
        return outcome;
    }

    bool model_reconciled = false;
    bool handler_found = editing_template_invoke_handler(
        evcon, prepared->surface.view, "beforeinput", &prepared->intent,
        &model_reconciled);
    if (handler_found && model_reconciled) {
        outcome.status = EDITING_ACTION_CLAIMED;
        outcome.model_reconciled = true;
    }
    return outcome;
}

void editing_template_action_register(DomDocument* document) {
    if (!document) return;
    EditingActionRegistration registration = {};
    registration.handler_id = "radiant-template";
    registration.route_mask = EDITING_ACTION_ROUTE_RADIANT_TEMPLATE;
    registration.priority = 0;
    registration.matches = editing_template_action_matches;
    registration.handle = editing_template_action_handle;
    uint64_t registration_id = editing_action_registry_register(document, &registration);
    if (!registration_id) {
        log_error("editing_template_action: failed to register radiant-template handler");
    }
}
