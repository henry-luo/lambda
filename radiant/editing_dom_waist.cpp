// The DOM-range waist (F13).
//
// This file was `editing_dom_handler.cpp`, a native editing implementation that
// decided what each `beforeinput` intent should do to a contenteditable. None of
// that survives: inserts, replacements, both delete intents and every
// composition intent belong to `lambda/package/dom/dom_edit.ls`.
//
// What is here is the geometry the template drives — resolving a transaction's
// boundaries to a single text node, splicing that node, creating one at an
// element boundary, placing the caret, and converting UTF-16 to the codepoints
// every Lambda-facing offset uses. The entry point resolves the range, hands it
// to the template, and reports what came back; it makes no editing decision.
//
// Renamed rather than deleted because that is what retiring the handler meant:
// the decisions left, the mechanism stayed, and a file called `_handler` that
// handles nothing would have been the misleading half of the outcome.

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

// F13: the DOM-range waist. The transaction stashes its resolved single-text
// range here; `radiant.dom_edit_range` reads it and `radiant.dom_replace_range`
// splices, bumping the epoch so the caller can tell the template applied.
static __thread uint64_t s_dom_edit_apply_epoch = 0;
// Where the splice left the caret, so the caller can collapse the selection
// there. Reported alongside the epoch rather than returned, because the value
// crosses back from Lambda through a primitive that has no way to reach the
// selection itself.
static __thread uint32_t s_dom_edit_caret_u16 = 0;
static __thread DomNode* s_dom_edit_caret_node = nullptr;

DomNode* dom_edit_caret_node(void) { return s_dom_edit_caret_node; }

// F13.3: place the caret without editing. Composition needs the cursor inside
// the run it just inserted, and the *unchanged* preedit case needs a claim with
// no mutation at all — an IME resends the same text on every keystroke of a
// sequence, and replacing identical text would fire a DOM mutation per key.
//
// A separate primitive rather than a fifth argument to dom_replace_range: no
// radiant-module primitive has ever taken more than four, and the fifth
// miscompiles into a SIGSEGV in an unrelated handler rather than being rejected.
// Bumping the same epoch is right — the epoch means "the template handled it",
// and a deliberate caret placement is handling.
bool dom_edit_set_caret_u16(DocState* state, uint32_t caret_u16) {
    if (!state) return false;
    DomText* text = state->editing.pending_dom_edit_text;
    if (!text) return false;
    s_dom_edit_caret_node = static_cast<DomNode*>(text);
    s_dom_edit_caret_u16 = caret_u16;
    s_dom_edit_apply_epoch++;
    return true;
}
uint32_t dom_edit_caret_offset_u16(void) { return s_dom_edit_caret_u16; }

uint64_t dom_edit_apply_epoch(void) { return s_dom_edit_apply_epoch; }

void dom_edit_set_pending_range(DocState* state, DomElement* host, DomText* text,
                                uint32_t start_u16, uint32_t end_u16,
                                DomNode* boundary_node, uint32_t boundary_offset) {
    if (!state) return;
    state->editing.pending_dom_edit_host = host;
    state->editing.pending_dom_edit_text = text;
    state->editing.pending_dom_edit_start = start_u16;
    state->editing.pending_dom_edit_end = end_u16;
    state->editing.pending_dom_edit_boundary_node = boundary_node;
    state->editing.pending_dom_edit_boundary_offset = boundary_offset;
}

// Splice a text node and leave the caret after the inserted text. Offsets in,
// UTF-16 out to the DOM: the conversion happens in the module primitive so every
// Lambda-facing offset stays a codepoint (ES9).
bool dom_edit_replace_range_u16(DocState* state, DomText* text,
                                uint32_t start_u16, uint32_t end_u16,
                                const char* replacement, uint32_t* out_caret_u16) {
    if (!state || !text || end_u16 < start_u16) return false;
    const char* repl = replacement ? replacement : "";
    uint32_t repl_bytes = (uint32_t)strlen(repl);
    uint32_t repl_u16 = tc_utf8_to_utf16_length(repl, repl_bytes);
    const char* current = text->text ? text->text : "";
    char* old_value = mem_strdup(current, MEM_CAT_TEMP);
    if (!old_value) return false;
    bool changed = dom_text_replace_data_contents(state, text, start_u16,
                                                  end_u16 - start_u16,
                                                  repl, repl_bytes, repl_u16);
    if (changed) {
        js_dom_notify_mutation_detail(DOM_JS_MUTATION_TEXT, text, text->parent,
                                      nullptr, old_value);
        s_dom_edit_apply_epoch++;
        s_dom_edit_caret_u16 = start_u16 + repl_u16;
        s_dom_edit_caret_node = static_cast<DomNode*>(text);
        if (out_caret_u16) *out_caret_u16 = s_dom_edit_caret_u16;
    }
    mem_free(old_value);
    return changed;
}

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
        start_text = dom_range_edge_text(
            editing_dom_element_child_at(element, start.offset), false);
    }
    if (end.node && end.node->is_text()) {
        end_text = lam::dom_require_text(end.node);
        end_offset = end.offset;
    } else if (end.node && end.node->is_element() && end.offset > 0) {
        DomElement* element = lam::dom_require_element(end.node);
        end_text = dom_range_edge_text(
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
                                           const char* text_data,
                                           DomText** out_inserted = nullptr) {
    if (!state || !selection || !host || !text_data || !boundary.node) return false;
    if (out_inserted) *out_inserted = nullptr;
    uint32_t byte_len = (uint32_t)strlen(text_data);
    if (boundary.node->is_text()) {
        DomText* text = lam::dom_require_text(boundary.node);
        bool changed = editing_dom_replace_text(state, selection,
            text, boundary.offset, boundary.offset,
            text_data, byte_len, tc_utf8_to_utf16_length(text_data, byte_len));
        if (changed && out_inserted) *out_inserted = text;
        return changed;
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
    bool collapsed = dom_selection_collapse(selection, static_cast<DomNode*>(inserted),
                                            u16_len, &exception);
    if (collapsed && out_inserted) *out_inserted = inserted;
    return collapsed;
}

// F13.4: the waist's insert-at-boundary. Wraps the native boundary insertion so
// a template can create a text node where none exists — the empty
// `<div contenteditable>` case, which `dom_replace_range` cannot express because
// it addresses an existing node.
bool dom_edit_insert_at_boundary_u16(DocState* state, const char* text_data,
                                     uint32_t* out_caret_u16) {
    if (!state || !text_data) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    DomNode* boundary_node = state->editing.pending_dom_edit_boundary_node;
    DomSelection* selection = state->dom_selection;
    if (!host || !boundary_node || !selection) return false;
    DomBoundary boundary = {boundary_node, state->editing.pending_dom_edit_boundary_offset};
    DomText* inserted = nullptr;
    if (!editing_dom_insert_at_boundary(state, selection, host, boundary,
                                        text_data, &inserted)) {
        return false;
    }
    uint32_t bytes = (uint32_t)strlen(text_data);
    s_dom_edit_apply_epoch++;
    s_dom_edit_caret_node = static_cast<DomNode*>(inserted);
    // A text-node boundary inserts *within* the node, so the caret lands past
    // the insertion point; a fresh node puts it at the end of what was created.
    s_dom_edit_caret_u16 = boundary_node->is_text()
        ? boundary.offset + tc_utf8_to_utf16_length(text_data, bytes)
        : tc_utf8_to_utf16_length(text_data, bytes);
    if (out_caret_u16) *out_caret_u16 = s_dom_edit_caret_u16;
    return true;
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
    if (!target.start.node || !target.end.node ||
        !editing_dom_host_contains_boundary(host, target.start) ||
        !editing_dom_host_contains_boundary(host, target.end)) {
        return false;
    }
    bool text_range = target.start.node == target.end.node &&
        target.start.node->is_text() && target.end.offset >= target.start.offset;
    bool collapsed_element_boundary = editing_dom_boundary_equal(target.start, target.end) &&
        target.start.node->is_element();
    if (!text_range && !collapsed_element_boundary) return false;
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

    // The target may not resolve — COMPOSITION_START arrives before there is a
    // range to name — so resolution is no longer a precondition for asking the
    // template.
    EditingTargetRange target = {};
    bool have_target = editing_dom_composition_target(host, prepared, &target);

    // F13.3: offer the composition edit to the template. The target comes from
    // `prepared->target_ranges`, not the live selection — an IME names the range
    // it is replacing — so this stashes that range rather than the one the
    // insert/delete path resolves.
    //
    // Every composition intent is offered, COMPOSITION_START included: the
    // dispatch now reports through two channels, so a template can claim a
    // transaction it handled by doing nothing.
    {
        DomText* target_text = (have_target && target.start.node->is_text())
            ? lam::dom_require_text(target.start.node) : nullptr;
        dom_edit_set_pending_range(state, host, target_text,
                                   have_target ? target.start.offset : 0,
                                   have_target ? target.end.offset : 0,
                                   have_target ? target.start.node : nullptr,
                                   have_target ? target.start.offset : 0);
        uint64_t apply_epoch_before = dom_edit_apply_epoch();
        InputIntent comp_carrier;
        comp_carrier.type = intent->type;
        if (intent->data) comp_carrier.data = intent->data;
        comp_carrier.composition_caret = intent->composition_caret;
        // Two channels meaning different things. The epoch says an edit was
        // *applied*; the dispatch's own return says the template *claimed* the
        // transaction. Composition needs both, because two of its cases are
        // legitimately handled by doing nothing — reserving the session on
        // start, and an empty replacement at an element boundary — and the epoch
        // alone cannot tell "handled, no change" from "declined".
        bool claimed = radiant_dispatch_behavior_dom_edit(static_cast<View*>(host),
                                                          &comp_carrier);
        bool applied = dom_edit_apply_epoch() != apply_epoch_before;
        DomNode* caret_node = dom_edit_caret_node();
        uint32_t caret_off = dom_edit_caret_offset_u16();
        dom_edit_set_pending_range(state, nullptr, nullptr, 0, 0, nullptr, 0);
        if (applied && caret_node && caret_node->is_text()) {
            DomText* caret_text = lam::dom_require_text(caret_node);
            editing_dom_collapse_selection(selection, caret_text, caret_off);
            // The session anchor stays native (ES18): the preedit run has to be
            // findable again on the next update, and that is storage, not policy.
            if (intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT) {
                composition->anchor_view = static_cast<View*>(caret_text);
                composition->anchor_offset = (int)dom_text_utf16_to_utf8(
                    caret_text, target.start.offset);  // INT_CAST_OK: DOM offsets fit the controller's signed anchor.
                composition->dom_preedit_len =
                    (uint32_t)strlen(intent->data ? intent->data : "");
            }
            outcome.status = EDITING_ACTION_CLAIMED;
            outcome.selection_changed = true;
            return outcome;
        }
        if (claimed) {
            // Handled without changing anything: no caret to move and no
            // mutation to report, but the transaction is the template's.
            outcome.status = EDITING_ACTION_CLAIMED;
            return outcome;
        }
        if (!have_target) return outcome;
    }

    // Nothing left to decide here. Every composition transaction is the
    // template's: the edits through the waist, and the two that are handled by
    // doing nothing through the dispatch verdict. Reaching this point means the
    // template declined, which for composition means no default action exists.
    return outcome;
}

EditingActionOutcome editing_dom_route_apply(
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
    // F13.1: offer the transaction to the <body> template before applying it
    // natively. The range is resolved here — element-offset-to-child, edge-text
    // descent and host containment are geometry over the tree, not policy — and
    // stashed for `radiant.dom_edit_*` to read. Dispatched with no EventContext
    // for the reason F11 established: a script editor that prevents beforeinput
    // still relies on the engine, so a translation-style hook must not be
    // suppressed by default_prevented.
    {
        // The resolved range when the boundaries land in one text node, and the
        // raw boundary always — the latter is what makes an insertion possible
        // where no text node exists yet.
        DomText* pending_text = nullptr;
        uint32_t pending_start = 0, pending_end = 0;
        editing_dom_single_text_range(host, action_start, action_end,
                                      &pending_text, &pending_start, &pending_end);
        dom_edit_set_pending_range(state, host, pending_text, pending_start,
                                   pending_end, action_start.node, action_start.offset);
        uint64_t apply_epoch_before = dom_edit_apply_epoch();
        InputIntent dom_carrier;
        dom_carrier.type = intent->type;
        if (intent->data) dom_carrier.data = intent->data;
        radiant_dispatch_behavior_dom_edit(static_cast<View*>(host), &dom_carrier);
        bool applied = dom_edit_apply_epoch() != apply_epoch_before;
        DomNode* caret_node = dom_edit_caret_node();
        uint32_t caret_off = dom_edit_caret_offset_u16();
        dom_edit_set_pending_range(state, nullptr, nullptr, 0, 0, nullptr, 0);
        if (applied) {
            // Collapse where the edit actually left the caret. Both waist
            // operations report it, so this is uniform across a splice and a
            // freshly created text node.
            if (caret_node && caret_node->is_text()) {
                editing_dom_collapse_selection(selection,
                                               lam::dom_require_text(caret_node),
                                               caret_off);
            }
            outcome.status = EDITING_ACTION_CLAIMED;
            outcome.selection_changed = true;
            return outcome;
        }
    }

    // Every intent this handler once applied is now the template's: inserts and
    // replacements through `dom_replace_range` and `dom_insert_at_boundary`,
    // both delete intents through the same range waist. What is left below is
    // composition (F13.3) and the dispatch above. `editing_dom_insert_at_boundary`
    // survives as the *mechanism* behind the boundary primitive, not as a path
    // this function takes.



    if (changed) {
        outcome.status = EDITING_ACTION_CLAIMED;
        outcome.selection_changed = true;
    }
    return outcome;
}

