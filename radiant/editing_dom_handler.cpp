#include "event.hpp"

#include "view.hpp"

#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lib/tagged.hpp"

#include <string.h>

extern "C" void js_dom_notify_mutation(DomJsMutationKind kind, void* target,
                                        void* parent);
extern "C" void js_dom_notify_mutation_detail(DomJsMutationKind kind,
                                               void* target, void* parent,
                                               const char* attribute_name,
                                               const char* old_value);

static bool editing_dom_boundary_equal(DomBoundary left, DomBoundary right) {
    return left.node == right.node && left.offset == right.offset;
}

static bool editing_dom_node_is_within(DomNode* node, DomNode* ancestor) {
    for (DomNode* current = node; current; current = current->parent) {
        if (current == ancestor) return true;
    }
    return false;
}

static DomElement* editing_dom_live_host(DomDocument* document,
                                         const EditingPreparedTransaction* prepared) {
    return editing_prepared_live_host(document, prepared);
}

static bool editing_dom_host_contains_boundary(DomElement* host,
                                               DomBoundary boundary) {
    if (!host || !boundary.node) return false;
    if (!editing_dom_node_is_within(boundary.node, host)) return false;
    EditingHost boundary_host;
    return editing_host_lookup(boundary.node, &boundary_host) &&
        boundary_host.host == host &&
        !boundary_host.target_in_false_island;
}

static bool editing_dom_live_selection_matches(
        const EditingPreparedTransaction* prepared, DomSelection* selection) {
    if (!prepared || !selection || selection->range_count != 1 ||
        prepared->selection_before.kind != EDIT_SEL_DOM_RANGE) {
        return false;
    }
    return editing_dom_boundary_equal(dom_selection_anchor_boundary(selection),
                                      prepared->selection_before.anchor) &&
        editing_dom_boundary_equal(dom_selection_focus_boundary(selection),
                                   prepared->selection_before.focus);
}

static bool editing_dom_prepared_extended_range(
        DomElement* host, const EditingPreparedTransaction* prepared,
        EditingTargetRange* out_range) {
    if (!host || !prepared || !out_range || prepared->target_range_count != 1) {
        return false;
    }
    EditingTargetRange range = prepared->target_ranges[0];
    if (!range.start.node || !range.end.node ||
        editing_dom_boundary_equal(range.start, range.end) ||
        !editing_dom_host_contains_boundary(host, range.start) ||
        !editing_dom_host_contains_boundary(host, range.end)) {
        return false;
    }
    *out_range = range;
    return true;
}

static DomNode* editing_dom_element_child_at(DomElement* element,
                                             uint32_t index) {
    if (!element) return nullptr;
    uint32_t current = 0;
    for (DomNode* child = element->first_child; child;
         child = child->next_sibling, current++) {
        if (current == index) return child;
    }
    return nullptr;
}

static DomText* editing_dom_edge_text(DomNode* node, bool at_end) {
    if (!node) return nullptr;
    if (node->is_text()) return lam::dom_require_text(node);
    if (!node->is_element()) return nullptr;
    DomElement* element = lam::dom_require_element(node);
    if (at_end) {
        for (DomNode* child = element->last_child; child;
             child = child->prev_sibling) {
            DomText* text = editing_dom_edge_text(child, true);
            if (text) return text;
        }
    } else {
        for (DomNode* child = element->first_child; child;
             child = child->next_sibling) {
            DomText* text = editing_dom_edge_text(child, false);
            if (text) return text;
        }
    }
    return nullptr;
}

static bool editing_dom_single_text_range(DomElement* host,
                                          DomBoundary start, DomBoundary end,
                                          DomText** out_text,
                                          uint32_t* out_start,
                                          uint32_t* out_end) {
    if (!host || !out_text || !out_start || !out_end) return false;
    DomText* start_text = nullptr;
    DomText* end_text = nullptr;
    uint32_t start_offset = 0;
    uint32_t end_offset = 0;
    if (start.node && start.node->is_text()) {
        start_text = lam::dom_require_text(start.node);
        start_offset = start.offset;
    } else if (start.node && start.node->is_element()) {
        DomElement* element = lam::dom_require_element(start.node);
        start_text = editing_dom_edge_text(
            editing_dom_element_child_at(element, start.offset), false);
    }
    if (end.node && end.node->is_text()) {
        end_text = lam::dom_require_text(end.node);
        end_offset = end.offset;
    } else if (end.node && end.node->is_element() && end.offset > 0) {
        DomElement* element = lam::dom_require_element(end.node);
        end_text = editing_dom_edge_text(
            editing_dom_element_child_at(element, end.offset - 1), true);
        end_offset = end_text ? dom_text_utf16_length(end_text) : 0;
    }
    if (!start_text || start_text != end_text ||
        !editing_dom_host_contains_boundary(host,
            {static_cast<DomNode*>(start_text), start_offset}) ||
        !editing_dom_host_contains_boundary(host,
            {static_cast<DomNode*>(end_text), end_offset})) {
        return false;
    }
    *out_text = start_text;
    *out_start = start_offset;
    *out_end = end_offset;
    return end_offset >= start_offset;
}

static bool editing_dom_replace_text(DocState* state, DomSelection* selection,
                                     DomText* text, uint32_t start, uint32_t end,
                                     const char* replacement, uint32_t replacement_len,
                                     uint32_t caret_in_replacement_u16) {
    if (!state || !selection || !text || !replacement || end < start) return false;
    const char* current = text->text ? text->text : "";
    char* old_value = mem_strdup(current, MEM_CAT_TEMP);
    if (!old_value) return false;
    uint32_t replacement_u16 = tc_utf8_to_utf16_length(replacement, replacement_len);
    bool changed = dom_text_replace_data_contents(state, text, start, end - start,
                                                  replacement, replacement_len,
                                                  replacement_u16);
    if (changed) {
        js_dom_notify_mutation_detail(DOM_JS_MUTATION_TEXT, text, text->parent,
                                      nullptr, old_value);
        const char* exception = nullptr;
        if (!dom_selection_collapse(selection, static_cast<DomNode*>(text),
                                    start + caret_in_replacement_u16, &exception)) {
            log_error("editing_dom_action: failed to collapse post-edit selection: %s",
                      exception ? exception : "unknown");
            changed = false;
        }
    }
    mem_free(old_value);
    return changed;
}

static bool editing_dom_insert_at_boundary(DocState* state, DomSelection* selection,
                                           DomElement* host,
                                           DomBoundary boundary,
                                           const char* text_data) {
    if (!state || !selection || !host || !text_data || !boundary.node) return false;
    uint32_t byte_len = (uint32_t)strlen(text_data);
    if (boundary.node->is_text()) {
        return editing_dom_replace_text(state, selection,
            lam::dom_require_text(boundary.node), boundary.offset, boundary.offset,
            text_data, byte_len, tc_utf8_to_utf16_length(text_data, byte_len));
    }
    if (!boundary.node->is_element() ||
        !editing_dom_host_contains_boundary(host, boundary)) {
        return false;
    }
    // structured editors place an empty-line caret on a descendant element
    // (for example CodeMirror's `<div class="cm-line"><br></div>`), not on
    // the editing host. It is still a valid same-host DOM insertion boundary.
    DomDocument* doc = host->doc;
    DomText* inserted = DomText::create_detached_copy(doc, text_data, byte_len);
    const char* exception = nullptr;
    if (!inserted || !dom_range_insert_node(selection->ranges[0],
            static_cast<DomNode*>(inserted), &exception)) {
        log_debug("editing_dom_action: element-boundary insertion rejected: %s",
                  exception ? exception : "unknown");
        return false;
    }
    js_dom_notify_mutation(DOM_JS_MUTATION_CHILD_INSERT, inserted, inserted->parent);
    uint32_t u16_len = tc_utf8_to_utf16_length(text_data, byte_len);
    return dom_selection_collapse(selection, static_cast<DomNode*>(inserted),
                                  u16_len, &exception);
}

static uint32_t editing_dom_codepoint_byte_offset(const char* text,
                                                  uint32_t text_len,
                                                  uint32_t codepoint_offset) {
    uint32_t byte_offset = 0;
    uint32_t count = 0;
    while (byte_offset < text_len && count < codepoint_offset) {
        uint32_t codepoint = 0;
        int bytes = str_utf8_decode(text + byte_offset, text_len - byte_offset,
                                    &codepoint);
        if (bytes <= 0) break;
        byte_offset += (uint32_t)bytes; // INT_CAST_OK: decoder byte count is positive.
        count++;
    }
    return byte_offset;
}

static bool editing_dom_text_range_equals(DomText* text, uint32_t start,
                                          uint32_t end, const char* replacement,
                                          uint32_t replacement_len) {
    if (!text || !replacement || end < start) return false;
    uint32_t start_u8 = dom_text_utf16_to_utf8(text, start);
    uint32_t end_u8 = dom_text_utf16_to_utf8(text, end);
    const char* current = text->text ? text->text : "";
    return end_u8 >= start_u8 && end_u8 - start_u8 == replacement_len &&
        memcmp(current + start_u8, replacement, replacement_len) == 0;
}

static bool editing_dom_collapse_selection(DomSelection* selection,
                                           DomText* text,
                                           uint32_t offset_u16) {
    if (!selection || !text) return false;
    const char* exception = nullptr;
    if (dom_selection_collapse(selection, static_cast<DomNode*>(text),
                               offset_u16, &exception)) {
        return true;
    }
    log_error("editing_dom_action: failed to place composition caret: %s",
              exception ? exception : "unknown");
    return false;
}

static bool editing_dom_composition_target(
        DomElement* host, const EditingPreparedTransaction* prepared,
        EditingTargetRange* out_target) {
    if (!host || !prepared || !out_target || prepared->target_range_count != 1) {
        return false;
    }
    EditingTargetRange target = prepared->target_ranges[0];
    if (!target.start.node || target.start.node != target.end.node ||
        !target.start.node->is_text() || target.end.offset < target.start.offset ||
        !editing_dom_host_contains_boundary(host, target.start) ||
        !editing_dom_host_contains_boundary(host, target.end)) {
        return false;
    }
    *out_target = target;
    return true;
}

static EditingActionOutcome editing_dom_handle_composition(
        DocState* state, DomElement* host, DomSelection* selection,
        const EditingPreparedTransaction* prepared) {
    EditingActionOutcome outcome = {EDITING_ACTION_PASS, false, false};
    if (!state || !host || !selection || !prepared) return outcome;
    const EditingIntent* intent = &prepared->intent;
    EditingCompositionState* composition = &state->editing.composition;
    if (!composition->active || composition->surface.owner != host) return outcome;

    if (intent->type == INPUT_INTENT_COMPOSITION_START) {
        // Starting a composition reserves the controller snapshot; the first
        // update captures the exact Selection range that it replaces.
        outcome.status = EDITING_ACTION_CLAIMED;
        return outcome;
    }

    EditingTargetRange target = {};
    if (!editing_dom_composition_target(host, prepared, &target)) return outcome;
    DomText* text = lam::dom_require_text(target.start.node);
    const char* replacement = intent->data ? intent->data : "";
    uint32_t replacement_len = (uint32_t)strlen(replacement);
    uint32_t caret_bytes = intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT
        ? editing_dom_codepoint_byte_offset(replacement, replacement_len,
                                            intent->composition_caret)
        : replacement_len;
    uint32_t caret_u16 = tc_utf8_to_utf16_length(replacement, caret_bytes);
    bool unchanged = editing_dom_text_range_equals(text, target.start.offset,
                                                   target.end.offset, replacement,
                                                   replacement_len);
    bool changed = false;
    if (unchanged) {
        changed = editing_dom_collapse_selection(selection, text,
                                                 target.start.offset + caret_u16);
    } else {
        changed = editing_dom_replace_text(state, selection, text,
            target.start.offset, target.end.offset, replacement, replacement_len,
            caret_u16);
    }
    if (!changed) {
        outcome.status = EDITING_ACTION_ERROR;
        return outcome;
    }

    if (intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT) {
        // A provisional range is anchored after the first replacement so every
        // later update replaces it instead of appending independent text edits.
        composition->anchor_view = static_cast<View*>(text);
        composition->anchor_offset = (int)dom_text_utf16_to_utf8(
            text, target.start.offset); // INT_CAST_OK: DOM text offsets fit the controller's signed anchor field.
        composition->dom_preedit_len = replacement_len;
    }
    outcome.status = EDITING_ACTION_CLAIMED;
    outcome.selection_changed = true;
    return outcome;
}

static bool editing_dom_action_matches(const EditingPreparedTransaction* prepared,
                                       void* user) {
    (void)user;
    return prepared && prepared->route.kind == EDITING_ROUTE_DOM_SCRIPT;
}

static EditingActionOutcome editing_dom_action_handle(
        EventContext* evcon, const EditingPreparedTransaction* prepared,
        void* user) {
    (void)user;
    EditingActionOutcome outcome = {EDITING_ACTION_PASS, false, false};
    DomDocument* document = evcon ? evcon->target_document : nullptr;
    DocState* state = document ? (DocState*)document->state : nullptr;
    DomElement* host = editing_dom_live_host(document, prepared);
    if (!document || !state || !prepared || !host) {
        outcome.status = EDITING_ACTION_ERROR;
        return outcome;
    }

    DomSelection* selection = state->dom_selection;
    const EditingIntent* intent = &prepared->intent;
    if (intent->type == INPUT_INTENT_COMPOSITION_START ||
        intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT ||
        intent->type == INPUT_INTENT_INSERT_FROM_COMPOSITION ||
        intent->type == INPUT_INTENT_DELETE_COMPOSITION_TEXT) {
        return editing_dom_handle_composition(state, host, selection, prepared);
    }
    bool live_selection_matches = editing_dom_live_selection_matches(prepared,
                                                                      selection);
    DomRange* range = selection->ranges[0];
    if (!range || !editing_dom_host_contains_boundary(host, range->start) ||
        !editing_dom_host_contains_boundary(host, range->end)) {
        return outcome;
    }

    EditingTargetRange prepared_range = {};
    bool use_prepared_range = !live_selection_matches &&
        editing_dom_prepared_extended_range(host, prepared, &prepared_range);
    if (!live_selection_matches && !use_prepared_range) return outcome;

    DomBoundary action_start = use_prepared_range ? prepared_range.start
                                                   : range->start;
    DomBoundary action_end = use_prepared_range ? prepared_range.end
                                                 : range->end;

    bool changed = false;
    if ((intent->type == INPUT_INTENT_INSERT_TEXT ||
         intent->type == INPUT_INTENT_INSERT_REPLACEMENT_TEXT) && intent->data) {
        if (!editing_dom_boundary_equal(action_start, action_end)) {
            // First-gate native replacement stays inside one text run. Crossing
            // structural nodes is editor-owned and must not revive rich defaults.
            DomText* text = nullptr;
            uint32_t text_start = 0;
            uint32_t text_end = 0;
            if (!editing_dom_single_text_range(host, action_start, action_end,
                                               &text, &text_start, &text_end)) {
                return outcome;
            }
            // Model editors may normalize an extended DOM range while handling
            // beforeinput to enclosing block boundaries; accept that projection
            // only when it still denotes one editable text run.
            changed = editing_dom_replace_text(state, selection,
                text, text_start, text_end, intent->data,
                (uint32_t)strlen(intent->data),
                tc_utf8_to_utf16_length(intent->data,
                                        (uint32_t)strlen(intent->data)));
        } else {
            changed = editing_dom_insert_at_boundary(state, selection, host,
                                                     action_start, intent->data);
        }
    } else if (intent->type == INPUT_INTENT_DELETE_CONTENT_BACKWARD ||
               intent->type == INPUT_INTENT_DELETE_CONTENT_FORWARD) {
        if (!editing_dom_boundary_equal(action_start, action_end)) {
            DomText* text = nullptr;
            uint32_t text_start = 0;
            uint32_t text_end = 0;
            if (!editing_dom_single_text_range(host, action_start, action_end,
                                               &text, &text_start, &text_end)) {
                return outcome;
            }
            changed = editing_dom_replace_text(state, selection,
                text, text_start, text_end, "", 0, 0);
        } else if (action_start.node->is_text()) {
            DomText* text = lam::dom_require_text(action_start.node);
            uint32_t length = dom_node_boundary_length(action_start.node);
            uint32_t start = action_start.offset;
            uint32_t end = action_start.offset;
            if (intent->type == INPUT_INTENT_DELETE_CONTENT_BACKWARD && start > 0) {
                start--;
            } else if (intent->type == INPUT_INTENT_DELETE_CONTENT_FORWARD && end < length) {
                end++;
            } else {
                return outcome;
            }
            changed = editing_dom_replace_text(state, selection, text, start, end,
                                               "", 0, 0);
        }
    }

    if (changed) {
        outcome.status = EDITING_ACTION_CLAIMED;
        outcome.selection_changed = true;
    }
    return outcome;
}

void editing_dom_action_register(DomDocument* document) {
    if (!document) return;
    EditingActionRegistration registration = {};
    registration.handler_id = "dom-compat";
    registration.route_mask = EDITING_ACTION_ROUTE_DOM_SCRIPT;
    registration.priority = 0;
    registration.matches = editing_dom_action_matches;
    registration.handle = editing_dom_action_handle;
    // Registration is idempotent for a document; callers may prepare several
    // edits in one event turn without adding competing built-in handlers.
    EditingActionRegistry* registry = editing_action_registry_get(document);
    if (!registry) return;
    if (!editing_action_registry_register(document, &registration)) {
        log_error("editing_dom_action: failed to register dom-compat handler");
    }
}
