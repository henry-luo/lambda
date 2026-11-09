#include "editing_dom_adapter.hpp"
#include "radiant_host_api.hpp"
#include "radiant/editing_dom_internal.hpp"
#include "radiant/editing_dom_host.hpp"
#include "radiant/event.hpp"
#include "radiant/view.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../dom/dom.h"
#include "../../dom/dom_observers.h"
#include "lib/mem.h"

static bool editing_insert_child(DomElement* parent, DomNode* child, DomNode* reference) {
    RootFrame roots(2);
    Rooted<Item> child_item(roots, dom_wrap_element(child));
    Rooted<Item> reference_item(roots, reference ? dom_wrap_element(reference) : ItemNull);
    if (child_item.get().item == ItemNull.item ||
        (reference && reference_item.get().item == ItemNull.item)) return false;
    // Mark-aware bridges preserve backing identity when moving static text.
    Item inserted = reference
        ? dom_insert_before_bridge(parent, child_item.get(), reference_item.get())
        : dom_append_child_bridge(parent, child_item.get());
    return inserted.item != ItemNull.item && child->parent == static_cast<DomNode*>(parent);
}

static bool editing_host_available() {
    // Provider registration is process-wide, but wrappers need an active evaluator.
    return radiant_host_api && eval_context_tls_runtime();
}

static bool editing_remove_child(DomNode* child) {
    RootFrame roots(1);
    Rooted<Item> child_item(roots, dom_wrap_element(child));
    if (child_item.get().item == ItemNull.item) return false;
    return dom_remove_child_bridge(child->parent->as_element(), child_item.get()).item != ItemNull.item;
}

static void editing_notify(DomJsMutationKind kind, DomNode* target, DomNode* parent,
                           const char* attribute_name, const char* old_value) {
    dom_notify_mutation_detail(kind, target, parent, attribute_name, old_value);
}

bool lambda_editing_dom_host_register() {
    static const RadiantEditingDomHostOps operations = {
        editing_host_available, editing_insert_child, editing_remove_child, dom_exec_insert_html, editing_notify,
    };
    return radiant_editing_dom_host_register(&operations);
}

extern "C" bool radiant_dispatch_behavior_exec_command(View* target,
                                                       const InputIntent* intent,
                                                       Item* out_result);
extern "C" bool radiant_dispatch_behavior_design_mode(View* target,
                                                       const InputIntent* intent,
                                                       Item* out_result);

// Native transport identifies the document body for this IDL operation. The
// package receives the raw coerced value, canonicalizes it, and invokes the
// generic Boolean document mechanism; no package means no native fallback.
extern "C" bool radiant_dom_set_design_mode(void* document_ptr,
                                             const char* value) {
    DomDocument* document = static_cast<DomDocument*>(document_ptr);
    DomElement* body = document ? radiant_document_body_element(document) : nullptr;
    // Inline scripts may set designMode while the document is still loading,
    // before layout creates DocState. The package mutation itself is document
    // state, so do not turn that ordinary IDL write into a layout-lifetime gate.
    if (!document || !body || !value) return false;
    InputIntent carrier = {};
    carrier.data = value;
    Item result = ItemNull;
    if (!radiant_dispatch_behavior_design_mode(static_cast<View*>(body),
                                               &carrier, &result)) {
        return false;
    }
    return get_type_id(result) == LMD_TYPE_BOOL && it2b(result);
}

// F14.1: the `execCommand` entry point.
//
// `document.execCommand(cmd, _, value)` used to reach a native bridge that
// implemented exactly one command, `insertHTML`. It now resolves the selection
// into an explicit invocation for the waist and
// offers the command to the package — so `execCommand('bold')` and Cmd+B run one
// implementation rather than two that can drift (the F9/F11 lesson: one rule
// set, two entry points).
extern "C" bool radiant_dom_exec_command(void* document_ptr, const char* command,
                                         const char* value) {
    DomDocument* document = static_cast<DomDocument*>(document_ptr);
    DocState* state = document ? (DocState*)document->state : nullptr;
    if (!state || !command || !*command) return false;

    // An API command may configure document-scoped package state without a
    // Selection.  Start an invocation only when the live Selection resolves
    // to one editable host; otherwise the package receives <body> and decides
    // support/enabled from its descriptor (D10.2.1).
    DomElement* host = radiant_document_body_element(document);
    if (!host) return false;

    DomEditInvocation invocation = {};
    EditingHost host_info = {};
    DomSelection* selection = state->dom_selection;
    if (selection && selection->range_count == 1 && selection->ranges[0]) {
        DomRange* range = selection->ranges[0];
        if (range->start.node &&
            editing_host_lookup(range->start.node, &host_info) && host_info.host &&
            !host_info.target_in_false_island &&
            radiant_editing_dom_host_contains_boundary(host_info.host, range->start) &&
            radiant_editing_dom_host_contains_boundary(host_info.host, range->end) &&
            dom_edit_invocation_begin(state, host_info.host, range->start,
                                      range->end, &invocation)) {
            host = host_info.host;
        }
    }
    InputIntent carrier;
    carrier.command = command;
    carrier.data = value;
    carrier.edit_invocation_id = invocation.id;
    carrier.edit_plaintext_only = host_info.mode == EditingHost::PlaintextOnly;
    RootFrame result_roots(1);
    Rooted<Item> edit_result(result_roots, ItemNull);
    Item raw_result = ItemNull;
    bool claimed = radiant_dispatch_behavior_exec_command(
        static_cast<View*>(host), &carrier, &raw_result);
    edit_result.set(raw_result);
    bool selection_committed = dom_edit_invocation_commit_selection(&invocation);
    bool changed = invocation.changed;
    // D10.2.1: post-commit API event facts come from the structured package
    // result. This bridge carries no command-name or inputType policy.
    bool emit_input = radiant_edit_result_bool(edit_result.get(), "api_input");
    char* input_type = radiant_edit_result_string_copy(edit_result.get(), "api_input_type");
    char* input_data = radiant_edit_result_string_copy(edit_result.get(), "api_input_data");
    if (emit_input && input_type) {
        InputIntent input_intent;
        // The standard intent builder supplies a clipboard snapshot when this
        // package fact represents paste; other API event data remains exactly
        // the package-provided value.
        input_intent_from_name(input_type, &input_intent);
        if (input_data) input_intent.data = input_data;
        radiant_dispatch_api_edit_input(document, host, &input_intent,
                                        input_type, input_data);
    }
    mem_free(input_type);
    mem_free(input_data);
    dom_edit_invocation_end(&invocation);
    // A package claim can intentionally be a compatible no-op (for example an
    // unsupported collapsed format). The mutation outcome is explicit too.
    return claimed || changed || selection_committed;
}

// D10.2.1: legacy query methods only transport a context and command spelling.
// The package owns support, enabled, state, indeterminate, and value policy;
// an absent selection still reaches it through <body> for queryCommandSupported.
Item radiant_dom_query_command(void* document_ptr, const char* query_kind,
                               const char* command) {
    DomDocument* document = static_cast<DomDocument*>(document_ptr);
    DocState* state = document ? (DocState*)document->state : nullptr;
    if (!state || !query_kind || !command || !*command) return ItemNull;

    DomElement* target = radiant_document_body_element(document);
    DomEditInvocation invocation = {};
    DomSelection* selection = state->dom_selection;
    if (selection && selection->range_count == 1 && selection->ranges[0]) {
        DomRange* range = selection->ranges[0];
        EditingHost host_info;
        if (range->start.node &&
            editing_host_lookup(range->start.node, &host_info) && host_info.host &&
            !host_info.target_in_false_island &&
            radiant_editing_dom_host_contains_boundary(host_info.host, range->start) &&
            radiant_editing_dom_host_contains_boundary(host_info.host, range->end) &&
            dom_edit_invocation_begin(state, host_info.host, range->start,
                                      range->end, &invocation)) {
            target = host_info.host;
        }
    }
    if (!target) return ItemNull;

    InputIntent carrier;
    carrier.command = command;
    carrier.edit_query_kind = query_kind;
    carrier.edit_invocation_id = invocation.id;
    carrier.edit_plaintext_only = invocation.plaintext_only;
    Item result = ItemNull;
    radiant_dispatch_behavior_edit_query(static_cast<View*>(target), &carrier,
                                         &result);
    if (invocation.active) dom_edit_invocation_end(&invocation);
    return result;
}
