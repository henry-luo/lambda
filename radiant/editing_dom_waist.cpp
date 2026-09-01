// The DOM-range waist (F13).
//
// This file was `editing_dom_handler.cpp`, a native editing implementation that
// decided what each `beforeinput` intent should do to a contenteditable. None of
// that survives: inserts, replacements, both delete intents and every
// composition intent belong to `lambda/package/dom/dom_edit.ls`.
//
// What is here is the geometry and mutation mechanism the package drives —
// resolving boundaries to a text node, splicing that node, creating one at an
// element boundary, placing the caret, and converting UTF-16 to the codepoints
// every Lambda-facing offset uses. Dispatch decides ownership; this waist does
// not select or invoke handlers.
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
#include "../lambda/dom/dom.h"
#include "../lambda/dom/dom_observers.h"


// F13: the DOM-range waist. The edit dispatch stashes its resolved single-text
// range here; `radiant.dom_edit_range` reads it and `radiant.dom_replace_range`
// splices, bumping the epoch so the caller can tell the package applied.
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
    state->editing.pending_dom_edit_range_end_node = nullptr;
    state->editing.pending_dom_edit_range_end_offset = 0;
    // Clear the caret channel with the range it belongs to. A primitive that
    // moves the epoch without placing a caret (wrapping a range, for one) would
    // otherwise leave the previous edit's node standing, and the dispatch
    // reads both — it would collapse the selection into a node this edit never
    // touched.
    s_dom_edit_caret_node = nullptr;
    s_dom_edit_caret_u16 = 0;
}

void dom_edit_set_pending_range_end(DocState* state, DomNode* boundary_node,
                                    uint32_t boundary_offset) {
    if (!state) return;
    state->editing.pending_dom_edit_range_end_node = boundary_node;
    state->editing.pending_dom_edit_range_end_offset = boundary_offset;
}

void dom_edit_clear_pending_range(DocState* state) {
    dom_edit_set_pending_range(state, nullptr, nullptr, 0, 0, nullptr, 0);
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

static bool editing_dom_node_is_within(DomNode* node, DomNode* ancestor) {
    for (DomNode* current = node; current; current = current->parent) {
        if (current == ancestor) return true;
    }
    return false;
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

bool dom_edit_prepare_pending_range(DocState* state, DomElement* host,
                                    DomBoundary start, DomBoundary end) {
    if (!state || !host || !start.node || !end.node ||
        !dom_boundary_is_valid(&start) || !dom_boundary_is_valid(&end) ||
        !editing_dom_host_contains_boundary(host, start) ||
        !editing_dom_host_contains_boundary(host, end)) {
        return false;
    }
    DomBoundaryOrder order = dom_boundary_compare(&start, &end);
    if (order == DOM_BOUNDARY_DISJOINT || order == DOM_BOUNDARY_AFTER) {
        return false;
    }
    DomText* text = nullptr;
    uint32_t text_start = 0;
    uint32_t text_end = 0;
    editing_dom_single_text_range(host, start, end, &text, &text_start,
                                  &text_end);
    dom_edit_set_pending_range(state, host, text, text_start, text_end,
                               start.node, start.offset);
    dom_edit_set_pending_range_end(state, end.node, end.offset);
    return true;
}

// Reconstitute the raw pending range for a structural operation. The single
// text cache remains the fast path for ordinary typing; F14.2 must retain both
// DOM endpoints so a delete or replacement can cross formatting and block nodes.
static bool editing_dom_pending_range(DocState* state, DomRange* out_range) {
    if (!state || !out_range) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    DomBoundary start = {
        state->editing.pending_dom_edit_boundary_node,
        state->editing.pending_dom_edit_boundary_offset
    };
    DomBoundary end = {
        state->editing.pending_dom_edit_range_end_node,
        state->editing.pending_dom_edit_range_end_offset
    };
    if (!host || !start.node || !end.node ||
        !editing_dom_host_contains_boundary(host, start) ||
        !editing_dom_host_contains_boundary(host, end) ||
        !dom_boundary_is_valid(&start) || !dom_boundary_is_valid(&end)) {
        return false;
    }
    DomBoundaryOrder order = dom_boundary_compare(&start, &end);
    if (order == DOM_BOUNDARY_DISJOINT || order == DOM_BOUNDARY_AFTER) {
        return false;
    }
    *out_range = {};
    out_range->state = state;
    out_range->start = start;
    out_range->end = end;
    out_range->is_live = false;
    return true;
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



// ---------------------------------------------------------------------------
// F14.1: the formatting primitives.
//
// Every waist primitive before these addressed one existing text node. Wrapping
// a range in an element is the first *structural* one, and it is what full UA
// editing needs: `bold` wraps, `unbold` unwraps, and the same pair underlies
// every inline command. Which tag a command wraps in, and whether it toggles on
// or off, stays in `lambda/package/dom/commands.ls` — what is here is the tree
// surgery, mechanism the way the splice is.
// ---------------------------------------------------------------------------

// The formatting element of `tag` between `node` and the editing host. Stopping
// at the host matters: a command toggles off only what it would itself have
// created, and only inside the element being edited.
static DomElement* editing_dom_format_ancestor(DomElement* host, DomNode* node,
                                               const char* tag) {
    if (!host || !node || !tag) return nullptr;
    for (DomNode* cur = node; cur && cur != static_cast<DomNode*>(host);
         cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = cur->as_element();
        if (elem->tag_name && strcasecmp(elem->tag_name, tag) == 0) return elem;
    }
    return nullptr;
}

// ES16: formatting state is read off the tree, never cached. This is what
// `queryCommandState('bold')` answers with, and it is also how the package
// decides whether a command toggles on or off.
bool dom_edit_range_in_format(DocState* state, const char* tag) {
    if (!state || !tag) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    DomText* text = state->editing.pending_dom_edit_text;
    if (!host || !text) return false;
    return editing_dom_format_ancestor(host, static_cast<DomNode*>(text),
                                       tag) != nullptr;
}

// Wrap [start, end) of the resolved text node in a fresh `tag` element.
bool dom_edit_wrap_range_u16(DocState* state, uint32_t start_u16,
                             uint32_t end_u16, const char* tag) {
    if (!state || !tag || !*tag) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    DomText* text = state->editing.pending_dom_edit_text;
    DomSelection* selection = state->dom_selection;
    if (!host || !text || !text->parent || !selection) return false;
    uint32_t total = dom_text_utf16_length(text);
    if (end_u16 > total) end_u16 = total;
    if (start_u16 >= end_u16) return false;
    DomDocument* doc = host->doc;
    if (!doc) return false;

    // Split the tail off first. Splitting the head would move `end_u16` into the
    // node the split produced, so the second offset would address the wrong node.
    if (end_u16 < total && !dom_text_split_at(state, text, end_u16)) return false;
    DomText* middle = start_u16 > 0 ? dom_text_split_at(state, text, start_u16)
                                    : text;
    if (!middle) return false;

    DomElement* wrapper = dom_element_create(doc, tag, nullptr);
    if (!wrapper) return false;
    DomNode* wrapper_node = static_cast<DomNode*>(wrapper);
    DomNode* middle_node = static_cast<DomNode*>(middle);
    DomNode* parent = middle_node->parent;
    if (!parent || !parent->insert_before(wrapper_node, middle_node)) return false;
    dom_mutation_post_insert(state, parent, wrapper_node);
    dom_mutation_pre_remove(state, middle_node);
    parent->remove_child(middle_node);
    wrapper_node->append_child(middle_node);
    dom_mutation_post_insert(state, wrapper_node, middle_node);
    js_dom_notify_mutation(DOM_JS_MUTATION_CHILD_INSERT, wrapper_node, parent);

    // Formatting leaves the run selected, the way a browser does, so a second
    // command applies to the same span. The caret channel is deliberately left
    // clear: the dispatch collapses to it when it is set, which is exactly the
    // wrong outcome for a command that changed no text.
    const char* exception = nullptr;
    if (!dom_selection_set_base_and_extent(selection, middle_node, 0,
                                           middle_node,
                                           dom_text_utf16_length(middle),
                                           &exception)) {
        log_error("dom_edit_wrap_range: failed to reselect wrapped run: %s",
                  exception ? exception : "unknown");
    }
    s_dom_edit_apply_epoch++;
    return true;
}

// Move a child between the raw DOM chains while keeping live Range boundaries
// and the JS mutation ledger in step. The waist owns this structural move so a
// package command does not accidentally invoke a second editing decision.
static bool editing_dom_insert_child(DocState* state, DomElement* parent,
                                     DomNode* child, DomNode* reference);

static bool editing_dom_move_child(DocState* state, DomNode* child,
                                   DomElement* destination, DomNode* reference) {
    if (!state || !child || !destination || !child->parent) return false;
    if (reference && reference->parent != static_cast<DomNode*>(destination)) {
        return false;
    }
    DomNode* source = child->parent;
    dom_mutation_pre_remove(state, child);
    if (!source->remove_child(child)) return false;
    js_dom_notify_mutation(DOM_JS_MUTATION_CHILD_REMOVE, child, source);
    return editing_dom_insert_child(state, destination, child, reference);
}

// F14.2 keeps block surgery deliberately small: these are the paragraph-like
// containers a contenteditable command may split or join. Lists and table
// cells retain their own editing semantics and are left to the native target
// range policy until a command explicitly covers them.
static bool editing_dom_is_structural_block(const DomElement* element) {
    if (!element || !element->tag_name) return false;
    const char* tag = element->tag_name;
    return strcasecmp(tag, "address") == 0 ||
        strcasecmp(tag, "article") == 0 ||
        strcasecmp(tag, "aside") == 0 ||
        strcasecmp(tag, "blockquote") == 0 ||
        strcasecmp(tag, "div") == 0 ||
        strcasecmp(tag, "figcaption") == 0 ||
        strcasecmp(tag, "figure") == 0 ||
        strcasecmp(tag, "footer") == 0 ||
        strcasecmp(tag, "header") == 0 ||
        strcasecmp(tag, "h1") == 0 ||
        strcasecmp(tag, "h2") == 0 ||
        strcasecmp(tag, "h3") == 0 ||
        strcasecmp(tag, "h4") == 0 ||
        strcasecmp(tag, "h5") == 0 ||
        strcasecmp(tag, "h6") == 0 ||
        strcasecmp(tag, "main") == 0 ||
        strcasecmp(tag, "nav") == 0 ||
        strcasecmp(tag, "p") == 0 ||
        strcasecmp(tag, "pre") == 0 ||
        strcasecmp(tag, "section") == 0;
}

static DomElement* editing_dom_structural_block_ancestor(DomElement* host,
                                                           DomNode* node) {
    if (!host || !node) return nullptr;
    for (DomNode* current = node;
         current && current != static_cast<DomNode*>(host);
         current = current->parent) {
        if (current->is_element() &&
            editing_dom_is_structural_block(current->as_element())) {
            return current->as_element();
        }
    }
    return nullptr;
}

static bool editing_dom_mergeable_blocks(DomElement* start_block,
                                         DomElement* end_block) {
    if (!start_block || !end_block ||
        !editing_dom_is_structural_block(start_block) ||
        !editing_dom_is_structural_block(end_block) ||
        !start_block->tag_name || !end_block->tag_name) {
        return false;
    }
    // Preserve the authored paragraph kind. Joining a <p> to a <div>, for
    // example, would silently change the surviving block's semantics.
    return strcasecmp(start_block->tag_name, end_block->tag_name) == 0;
}

static bool editing_dom_insert_child(DocState* state, DomElement* parent,
                                     DomNode* child, DomNode* reference) {
    if (!state || !parent || !child || child->parent ||
        (reference && reference->parent != static_cast<DomNode*>(parent))) {
        return false;
    }
    bool inserted = reference
        ? static_cast<DomNode*>(parent)->insert_before(child, reference)
        : static_cast<DomNode*>(parent)->append_child(child);
    if (!inserted) return false;
    dom_mutation_post_insert(state, static_cast<DomNode*>(parent), child);
    js_dom_notify_mutation(DOM_JS_MUTATION_CHILD_INSERT, child,
                           static_cast<DomNode*>(parent));
    return true;
}

static bool editing_dom_append_empty_break(DocState* state, DomElement* parent,
                                           DomNode* reference = nullptr) {
    if (!state || !parent) return false;
    DomElement* br = dom_element_create(parent->doc, "br", nullptr);
    return br && editing_dom_insert_child(state, parent,
                                          static_cast<DomNode*>(br), reference);
}

static bool editing_dom_remove_child(DocState* state, DomNode* child) {
    if (!state || !child || !child->parent) return false;
    DomNode* parent = child->parent;
    dom_mutation_pre_remove(state, child);
    if (!parent->remove_child(child)) return false;
    js_dom_notify_mutation(DOM_JS_MUTATION_CHILD_REMOVE, child, parent);
    return true;
}

static bool editing_dom_move_fragment_children(DocState* state,
                                               DomElement* fragment,
                                               DomElement* destination) {
    if (!state || !fragment || !destination) return false;
    while (fragment->first_child) {
        if (!editing_dom_move_child(state, fragment->first_child,
                                    destination, nullptr)) {
            return false;
        }
    }
    return true;
}

// Apply a raw pending DOM range and optionally insert text at its start. The
// package chooses whether this means delete, typing, or replacement; this
// helper only performs Range deletion, adjacent same-kind block merging, and
// the resulting tree insertion.
static bool editing_dom_apply_pending_range(DocState* state,
                                            const char* replacement) {
    DomRange operation = {};
    if (!editing_dom_pending_range(state, &operation)) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    const char* repl = replacement ? replacement : "";
    size_t repl_bytes = strlen(repl);
    bool collapsed = dom_range_collapsed(&operation);
    if (collapsed && repl_bytes == 0) return false;

    DomBoundary original_start = operation.start;
    DomElement* start_block = editing_dom_structural_block_ancestor(
        host, operation.start.node);
    DomElement* end_block = editing_dom_structural_block_ancestor(
        host, operation.end.node);

    if (!collapsed) {
        const char* exception = nullptr;
        if (!dom_range_delete_contents(&operation, &exception) || exception) {
            log_debug("F14.2: pending DOM range delete rejected: %s",
                      exception ? exception : "unknown");
            return false;
        }
    }

    // Range deletion computes a legal collapsed boundary, but that boundary is
    // allowed to move to the end of the partially-contained parent. For typing
    // over a cross-node selection the caret must remain at the original start
    // text offset, before the surviving suffix; use the computed boundary only
    // when the original start node was removed.
    DomBoundary caret = original_start;
    if (!caret.node || !editing_dom_host_contains_boundary(host, caret)) {
        caret = operation.start;
    }
    if (caret.node && caret.node->is_text()) {
        uint32_t length = dom_text_utf16_length(caret.node->as_text());
        if (caret.offset > length) caret.offset = length;
    } else if (caret.node && caret.node->is_element()) {
        uint32_t length = dom_node_boundary_length(caret.node);
        if (caret.offset > length) caret.offset = length;
    }
    if (!caret.node || !dom_boundary_is_valid(&caret) ||
        !editing_dom_host_contains_boundary(host, caret)) {
        return false;
    }

    if (!collapsed && editing_dom_mergeable_blocks(start_block, end_block) &&
        start_block->parent &&
        start_block->parent == end_block->parent &&
        start_block->next_sibling == static_cast<DomNode*>(end_block)) {
        while (end_block->first_child) {
            if (!editing_dom_move_child(state, end_block->first_child,
                                        start_block, nullptr)) {
                return false;
            }
        }
        if (!editing_dom_remove_child(state, static_cast<DomNode*>(end_block))) {
            return false;
        }
        if (!editing_dom_node_is_within(caret.node,
                                        static_cast<DomNode*>(start_block))) {
            caret = { static_cast<DomNode*>(start_block),
                      dom_node_boundary_length(static_cast<DomNode*>(start_block)) };
        }
    }

    if (repl_bytes > 0) {
        DomText* inserted = DomText::create_detached_copy(host->doc, repl,
                                                           repl_bytes);
        if (!inserted) return false;
        operation.start = caret;
        operation.end = caret;
        const char* exception = nullptr;
        if (!dom_range_insert_node(&operation, static_cast<DomNode*>(inserted),
                                   &exception) || exception) {
            log_debug("F14.2: pending DOM range insertion rejected: %s",
                      exception ? exception : "unknown");
            return false;
        }
        caret = { static_cast<DomNode*>(inserted),
                  dom_text_utf16_length(inserted) };
    }

    s_dom_edit_caret_node = caret.node;
    s_dom_edit_caret_u16 = caret.offset;
    s_dom_edit_apply_epoch++;
    js_dom_notify_mutation(DOM_JS_MUTATION_TREE_REPLACE, host, host);
    return true;
}

static bool editing_dom_prepare_structural_caret(DocState* state,
                                                 DomBoundary* out_caret) {
    if (!state || !out_caret) return false;
    DomRange pending = {};
    if (!editing_dom_pending_range(state, &pending)) return false;
    if (!dom_range_collapsed(&pending)) {
        if (!editing_dom_apply_pending_range(state, "")) return false;
        out_caret->node = dom_edit_caret_node();
        out_caret->offset = dom_edit_caret_offset_u16();
    } else {
        *out_caret = pending.start;
    }
    DomElement* host = state->editing.pending_dom_edit_host;
    return out_caret->node && dom_boundary_is_valid(out_caret) &&
        editing_dom_host_contains_boundary(host, *out_caret);
}

static bool editing_dom_split_block(DocState* state, DomElement* host,
                                    DomBoundary caret, DomBoundary* out_caret) {
    if (!state || !host || !out_caret || !caret.node) return false;
    DomElement* block = editing_dom_structural_block_ancestor(host, caret.node);
    DomElement* source = block ? block : host;
    DomElement* parent = block && block->parent && block->parent->is_element()
        ? block->parent->as_element() : host;
    DomNode* source_node = static_cast<DomNode*>(source);
    if (!parent || !source_node ||
        !editing_dom_node_is_within(caret.node, source_node)) return false;

    const char* tag = block && block->tag_name ? block->tag_name : "div";
    DomElement* new_block = dom_element_create(host->doc, tag, nullptr);
    if (!new_block) return false;
    DomNode* reference = block
        ? static_cast<DomNode*>(block)->next_sibling : nullptr;
    if (!editing_dom_insert_child(state, parent, static_cast<DomNode*>(new_block),
                                  reference)) {
        return false;
    }

    DomRange suffix = {};
    suffix.state = state;
    suffix.start = caret;
    suffix.end = { source_node, dom_node_boundary_length(source_node) };
    if (dom_boundary_compare(&suffix.start, &suffix.end) == DOM_BOUNDARY_AFTER) {
        editing_dom_remove_child(state, static_cast<DomNode*>(new_block));
        return false;
    }
    const char* exception = nullptr;
    DomElement* fragment = dom_range_extract_contents(&suffix, &exception);
    if (!fragment || exception ||
        !editing_dom_move_fragment_children(state, fragment, new_block)) {
        editing_dom_remove_child(state, static_cast<DomNode*>(new_block));
        return false;
    }

    if (!block) {
        // When the host had no direct prefix, retain an explicit empty line
        // before the newly-created block rather than losing the left caret.
        if (new_block->prev_sibling == nullptr &&
            !editing_dom_append_empty_break(state, host,
                static_cast<DomNode*>(new_block))) {
            return false;
        }
    } else if (!block->first_child &&
               !editing_dom_append_empty_break(state, block, nullptr)) {
        return false;
    }
    if (!new_block->first_child &&
        !editing_dom_append_empty_break(state, new_block, nullptr)) {
        return false;
    }

    DomText* first_text = dom_range_edge_text(static_cast<DomNode*>(new_block),
                                              false);
    if (first_text) {
        *out_caret = { static_cast<DomNode*>(first_text), 0 };
    } else {
        *out_caret = { static_cast<DomNode*>(new_block), 0 };
    }
    return true;
}

static bool editing_dom_insert_break_at(DocState* state, DomBoundary caret,
                                        DomBoundary* out_caret) {
    if (!state || !out_caret || !caret.node) return false;
    DomElement* parent = nullptr;
    DomNode* reference = nullptr;
    if (caret.node->is_text()) {
        DomText* text = caret.node->as_text();
        if (!text->parent || !text->parent->is_element()) return false;
        parent = text->parent->as_element();
        uint32_t length = dom_text_utf16_length(text);
        if (caret.offset > length) return false;
        if (caret.offset == 0) {
            reference = static_cast<DomNode*>(text);
        } else if (caret.offset == length) {
            reference = text->next_sibling;
        } else {
            DomText* right = dom_text_split_at(state, text, caret.offset);
            if (!right) return false;
            reference = static_cast<DomNode*>(right);
        }
    } else if (caret.node->is_element()) {
        parent = caret.node->as_element();
        if (caret.offset > dom_node_boundary_length(caret.node)) return false;
        reference = editing_dom_element_child_at(parent, caret.offset);
    } else {
        return false;
    }

    DomElement* br = dom_element_create(parent->doc, "br", nullptr);
    if (!br || !editing_dom_insert_child(state, parent, static_cast<DomNode*>(br),
                                         reference)) {
        return false;
    }
    *out_caret = { static_cast<DomNode*>(parent),
                   dom_node_child_index(static_cast<DomNode*>(br)) + 1 };
    return true;
}

// F14.2 structural waist entry points. The package names the command; these
// functions expose only the range/DOM mechanism and the post-edit caret.
bool dom_edit_replace_pending_range(DocState* state, const char* replacement) {
    return editing_dom_apply_pending_range(state, replacement);
}

bool dom_edit_delete_pending_range(DocState* state) {
    return editing_dom_apply_pending_range(state, "");
}

bool dom_edit_insert_paragraph(DocState* state) {
    if (!state) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    DomBoundary caret = {};
    if (!host || !editing_dom_prepare_structural_caret(state, &caret)) {
        return false;
    }
    DomBoundary new_caret = {};
    if (!editing_dom_split_block(state, host, caret, &new_caret)) return false;
    s_dom_edit_caret_node = new_caret.node;
    s_dom_edit_caret_u16 = new_caret.offset;
    s_dom_edit_apply_epoch++;
    js_dom_notify_mutation(DOM_JS_MUTATION_TREE_REPLACE, host, host);
    return true;
}

bool dom_edit_insert_line_break(DocState* state) {
    if (!state) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    DomBoundary caret = {};
    if (!host || !editing_dom_prepare_structural_caret(state, &caret)) {
        return false;
    }
    DomBoundary new_caret = {};
    if (!editing_dom_insert_break_at(state, caret, &new_caret)) return false;
    s_dom_edit_caret_node = new_caret.node;
    s_dom_edit_caret_u16 = new_caret.offset;
    s_dom_edit_apply_epoch++;
    js_dom_notify_mutation(DOM_JS_MUTATION_TREE_REPLACE, host, host);
    return true;
}

// Remove the `tag` formatting over [start, end), promoting its children.
// Partial ranges split the one-text-child formatting shell into left/right
// formatted siblings and move the selected middle text between them. More
// general nested, multi-child, and cross-node unwrap shapes remain future work.
bool dom_edit_unwrap_range_u16(DocState* state, uint32_t start_u16,
                               uint32_t end_u16, const char* tag) {
    if (!state || !tag || !*tag) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    DomText* text = state->editing.pending_dom_edit_text;
    DomSelection* selection = state->dom_selection;
    if (!host || !text || !selection) return false;
    DomElement* fmt = editing_dom_format_ancestor(host,
                                                  static_cast<DomNode*>(text), tag);
    if (!fmt || !fmt->parent) return false;
    DomNode* text_node = static_cast<DomNode*>(text);
    if (fmt->first_child != text_node || text_node->next_sibling) return false;
    uint32_t total = dom_text_utf16_length(text);
    if (start_u16 >= end_u16 || end_u16 > total) return false;

    // Split the selected text before moving any node. The split envelope keeps
    // all live ranges valid, and the resulting sibling identities let the
    // structural move below preserve the exact selected text.
    if (end_u16 < total && !dom_text_split_at(state, text, end_u16)) return false;
    DomText* middle = start_u16 > 0 ? dom_text_split_at(state, text, start_u16)
                                    : text;
    if (!middle) return false;

    DomNode* middle_node = static_cast<DomNode*>(middle);
    DomNode* left = middle_node->prev_sibling;
    DomNode* right = middle_node->next_sibling;

    DomNode* fmt_node = static_cast<DomNode*>(fmt);
    DomNode* parent = fmt_node->parent;
    DomNode* after_fmt = fmt_node->next_sibling;
    DomElement* right_fmt = nullptr;

    // If both sides remain formatted, preserve the formatting shell on the
    // right as a fresh sibling. A command-created shell has no author attrs;
    // the tag is the mechanism's only formatting state in this stage.
    if (left && right) {
        right_fmt = dom_element_create(host->doc, fmt->tag_name, nullptr);
        if (!right_fmt) return false;
        DomNode* right_fmt_node = static_cast<DomNode*>(right_fmt);
        if (!parent->insert_before(right_fmt_node, after_fmt)) return false;
        dom_mutation_post_insert(state, parent, right_fmt_node);
        js_dom_notify_mutation(DOM_JS_MUTATION_CHILD_INSERT, right_fmt_node,
                               parent);
    }

    // With a left side the unformatted middle follows the original shell. With
    // no left side it must precede the shell, which now contains the right side.
    DomNode* reference = left
        ? (right_fmt ? static_cast<DomNode*>(right_fmt) : after_fmt)
        : fmt_node;
    if (!editing_dom_move_child(state, middle_node,
                                lam::dom_require_element(parent), reference)) {
        return false;
    }

    if (right_fmt && !editing_dom_move_child(
            state, right, right_fmt, nullptr)) {
        return false;
    }

    // A full-range unwrap leaves an empty original shell after the selected
    // text has moved before it; remove that shell rather than exposing an empty
    // formatting element in innerHTML.
    if (!left && !right) {
        dom_mutation_pre_remove(state, fmt_node);
        if (!parent->remove_child(fmt_node)) return false;
        js_dom_notify_mutation(DOM_JS_MUTATION_CHILD_REMOVE, fmt_node, parent);
    }

    const char* exception = nullptr;
    if (!dom_selection_set_base_and_extent(selection, middle_node, 0,
                                           middle_node,
                                           dom_text_utf16_length(middle),
                                           &exception)) {
        log_error("dom_edit_unwrap_range: failed to reselect unwrapped run: %s",
                  exception ? exception : "unknown");
    }
    s_dom_edit_apply_epoch++;
    return true;
}


// Insert a parsed fragment over the selection. The parse and the Range splice
// are the JS DOM's own — this is the same mechanism the retired `insertHTML`
// bridge drove, reached from the package instead of from a native special case.
bool dom_edit_insert_html(DocState* state, const char* html) {
    if (!state || !html) return false;
    DomElement* host = state->editing.pending_dom_edit_host;
    if (!host || !host->doc) return false;
    // The fragment parser can move the heap, and `html` is a Lambda string the
    // collector may relocate; copy before handing it over, as the bridge did.
    char* stable = mem_strdup(html, MEM_CAT_TEMP);
    if (!stable) return false;
    bool inserted = js_dom_exec_insert_html(host->doc, stable);
    mem_free(stable);
    if (inserted) s_dom_edit_apply_epoch++;
    return inserted;
}

extern "C" bool radiant_dispatch_behavior_exec_command(View* target,
                                                       const InputIntent* intent);

// F14.1: the `execCommand` entry point.
//
// `document.execCommand(cmd, _, value)` used to reach a native bridge that
// implemented exactly one command, `insertHTML`. It now resolves the selection
    // the way a `beforeinput` edit does, stashes the range for the waist, and
// offers the command to the package — so `execCommand('bold')` and Cmd+B run one
// implementation rather than two that can drift (the F9/F11 lesson: one rule
// set, two entry points).
extern "C" bool radiant_dom_exec_command(void* document_ptr, const char* command,
                                         const char* value) {
    DomDocument* document = static_cast<DomDocument*>(document_ptr);
    DocState* state = document ? (DocState*)document->state : nullptr;
    DomSelection* selection = state ? state->dom_selection : nullptr;
    if (!command || !*command || !selection || selection->range_count != 1) {
        return false;
    }
    DomRange* range = selection->ranges[0];
    if (!range || !range->start.node) return false;
    // Resolved from the live tree, never from `active_surface.view`: that can be
    // a node from a superseded render generation whose parent chain no longer
    // reaches <body> (§3.15's trap).
    EditingHost host_info;
    if (!editing_host_lookup(range->start.node, &host_info) || !host_info.host ||
        host_info.target_in_false_island) {
        return false;
    }
    DomElement* host = host_info.host;
    if (!editing_dom_host_contains_boundary(host, range->start) ||
        !editing_dom_host_contains_boundary(host, range->end)) {
        return false;
    }

    DomText* text = nullptr;
    uint32_t start = 0, end = 0;
    editing_dom_single_text_range(host, range->start, range->end, &text,
                                  &start, &end);
    dom_edit_set_pending_range(state, host, text, start, end,
                               range->start.node, range->start.offset);
    dom_edit_set_pending_range_end(state, range->end.node, range->end.offset);
    uint64_t epoch_before = dom_edit_apply_epoch();
    InputIntent carrier;
    carrier.command = command;
    carrier.data = value;
    bool claimed = radiant_dispatch_behavior_exec_command(
        static_cast<View*>(host), &carrier);
    bool applied = dom_edit_apply_epoch() != epoch_before;
    DomNode* caret_node = dom_edit_caret_node();
    uint32_t caret_off = dom_edit_caret_offset_u16();
    dom_edit_set_pending_range(state, nullptr, nullptr, 0, 0, nullptr, 0);
    if (applied && caret_node) {
        const char* exception = nullptr;
        if (!dom_selection_collapse(selection, caret_node, caret_off,
                                    &exception)) {
            log_error("radiant_dom_exec_command: failed to collapse selection: %s",
                      exception ? exception : "unknown");
        }
    }
    // Two channels, unchanged from F13.3: the epoch says an edit was applied,
    // the dispatch verdict says the command was handled without one — which is
    // what `queryCommandState`-shaped commands and a declined toggle both need.
    return applied || claimed;
}
