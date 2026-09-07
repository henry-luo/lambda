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
#include "../lib/arraylist.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lib/tagged.hpp"

#include <string.h>
#include <stdlib.h>
#include "../lambda/dom/dom.h"
#include "../lambda/dom/dom_observers.h"


// D7.2.5: an edit has no ambient pending range or thread-local result channel.
// The dispatcher owns one stack invocation, exposes only its id to the package,
// and every waist primitive validates that explicit capability before acting.
struct DomMutationTransaction;

static void editing_dom_transaction_note_owned_write(
        DomEditInvocation* invocation);
static uint64_t editing_dom_transaction_expected_epoch(
        const DomMutationTransaction* transaction, uint64_t fallback);

static void editing_dom_record_caret(DomEditInvocation* invocation,
                                     DomNode* node, uint32_t offset,
                                     bool changed) {
    if (!invocation || !invocation->active) return;
    invocation->caret_node = node;
    invocation->caret_offset = offset;
    invocation->changed = invocation->changed || changed;
    invocation->selection_changed = true;
    if (changed && invocation->host && invocation->host->doc) {
        invocation->mutation_epoch = dom_mutation_epoch(invocation->host->doc);
        editing_dom_transaction_note_owned_write(invocation);
    }
}

static bool editing_dom_invocation_epoch_is_current(
        const DomEditInvocation* invocation) {
    if (!invocation || !invocation->active || !invocation->host ||
        !invocation->host->doc) {
        return false;
    }
    // Package plans may compose ordinary DOM setters with waist primitives.
    // Once the generic transaction owns the document clock, compare against
    // its owned-write watermark rather than the invocation's initial epoch.
    uint64_t expected_epoch = editing_dom_transaction_expected_epoch(
        invocation->transaction, invocation->mutation_epoch);
    return dom_mutation_epoch(invocation->host->doc) == expected_epoch;
}

static void editing_dom_invocation_note_mutation(DomEditInvocation* invocation) {
    if (!invocation || !invocation->host || !invocation->host->doc) return;
    invocation->mutation_epoch = dom_mutation_epoch(invocation->host->doc);
    editing_dom_transaction_note_owned_write(invocation);
}

// D7.2.5: generic mutation transaction -------------------------------------------------
//
// The edit package supplies a capability and generic primitives supply DOM
// mechanics.  A text replacement can therefore retain its inverse before the
// first write, defer observable DOM notifications, and either publish the
// completed mutation or restore every live Range without leaking a partial
// beforeinput result.  This deliberately contains no command/inputType data.

typedef struct DomMutationRangeSnapshot {
    DomRange* range;
    DomBoundary start;
    DomBoundary end;
    bool layout_valid;
} DomMutationRangeSnapshot;

typedef struct DomMutationSelectionSnapshot {
    DomSelection* selection;
    DomRange* ranges[DOM_SELECTION_MAX_RANGES];
    uint32_t range_count;
    DomSelectionDirection direction;
    DomBoundary notified_starts[DOM_SELECTION_MAX_RANGES];
    DomBoundary notified_ends[DOM_SELECTION_MAX_RANGES];
    uint32_t notified_range_count;
    DomSelectionDirection notified_direction;
    DomNode* associated_doc_root;
} DomMutationSelectionSnapshot;

// A structural edit can split, extract, move, or remove several existing
// nodes before its final step fails.  Preserve the original node graph and
// text backing before the first write so rollback restores identities rather
// than rebuilding the host from serialized HTML (D7.2.5).
typedef struct DomMutationNodeSnapshot {
    DomNode* node;
    DomNode* parent;
    String* text_string;
    // Attribute values are part of a structural DOM delta. Copy the ordinary
    // DOM names/values rather than serializing a host or reading package
    // policy fields (D7.2.5).
    ArrayList* attributes;
} DomMutationNodeSnapshot;

typedef struct DomMutationAttributeSnapshot {
    char* name;
    char* value;
} DomMutationAttributeSnapshot;

typedef struct DomRetainedSelection {
    bool present;
    DomBoundary anchor;
    DomBoundary focus;
} DomRetainedSelection;

// The package's history stack stores only this opaque id. The retained delta
// is generic DOM state: both snapshots preserve node identity, text, and
// attributes without encoding a command, tag table, or whole-host HTML.
struct DomRetainedMutation {
    uint64_t id;
    DomDocument* document;
    DomElement* host;
    ArrayList* before_nodes;
    ArrayList* after_nodes;
    DomRetainedSelection before_selection;
    DomRetainedSelection after_selection;
    uint64_t expected_epoch;
    struct DomRetainedMutation* next;
};

typedef enum DomMutationUndoKind {
    DOM_MUTATION_UNDO_TEXT_REPLACE = 1
} DomMutationUndoKind;

typedef struct DomMutationUndo {
    DomMutationUndoKind kind;
    DomText* text;
    String* previous_string;
    char* previous_value;
} DomMutationUndo;

typedef struct DomMutationNotification {
    DomJsMutationKind kind;
    DomNode* target;
    DomNode* parent;
    char* attribute_name;
    char* old_value;
} DomMutationNotification;

struct DomMutationTransaction {
    DocState* state;
    DomDocument* document;
    DomElement* host;
    uint64_t expected_epoch;
    ArrayList* range_snapshots;
    ArrayList* node_snapshots;
    ArrayList* undo_entries;
    ArrayList* notifications;
    DomMutationSelectionSnapshot selection_snapshot;
    // The behavior package owns the value. Retaining this opaque root makes
    // session updates atomic with the generic DOM transaction without native
    // code learning any history or command policy (D7.2.5, D5.3.3).
    uint64_t edit_session_root_before;
    bool edit_session_root_before_rooted;
    bool changed;
    bool notification_failed;
};

static uint64_t editing_dom_transaction_expected_epoch(
        const DomMutationTransaction* transaction, uint64_t fallback) {
    return transaction ? transaction->expected_epoch : fallback;
}

static void editing_dom_transaction_note_owned_write(
        DomEditInvocation* invocation) {
    if (!invocation || !invocation->transaction) return;
    // Generic mutations advance the document clock themselves. Advance the
    // transaction's owned-write watermark only at that explicit waist
    // boundary; an unaccounted write still makes commit reject the plan.
    invocation->transaction->expected_epoch = invocation->mutation_epoch;
}

static bool editing_dom_transaction_append_notification(
        DomMutationTransaction* transaction, DomJsMutationKind kind,
        DomNode* target, DomNode* parent, const char* attribute_name,
        const char* old_value) {
    if (!transaction || !transaction->notifications) return false;
    DomMutationNotification* notification =
        static_cast<DomMutationNotification*>(mem_calloc(
            1, sizeof(DomMutationNotification), MEM_CAT_TEMP));
    if (!notification) return false;
    notification->kind = kind;
    notification->target = target;
    notification->parent = parent;
    if (attribute_name) {
        notification->attribute_name = mem_strdup(attribute_name, MEM_CAT_TEMP);
        if (!notification->attribute_name) {
            mem_free(notification);
            return false;
        }
    }
    if (old_value) {
        notification->old_value = mem_strdup(old_value, MEM_CAT_TEMP);
        if (!notification->old_value) {
            mem_free(notification->attribute_name);
            mem_free(notification);
            return false;
        }
    }
    if (!arraylist_append(transaction->notifications, notification)) {
        mem_free(notification->attribute_name);
        mem_free(notification->old_value);
        mem_free(notification);
        return false;
    }
    // The document clock changes at the write boundary even though observers
    // are held until commit, so a nested package call cannot reuse this plan.
    if (transaction->document) transaction->document->mutation_epoch++;
    transaction->changed = true;
    return true;
}

// The DOM core calls this weak seam before delivering an ordinary mutation.
// It contains no editing policy: it merely buffers observations for the one
// active generic transaction on the target document.
extern "C" bool dom_edit_transaction_defer_mutation(
        DomDocument* document, DomJsMutationKind kind, DomNode* target,
        DomNode* parent, const char* attribute_name, const char* old_value) {
    DocState* state = document ? (DocState*)document->state : nullptr;
    DomMutationTransaction* transaction =
        state ? state->editing.dom_mutation_transaction : nullptr;
    if (!transaction || transaction->document != document) return false;
    if (!editing_dom_transaction_append_notification(transaction, kind, target,
                                                     parent, attribute_name,
                                                     old_value)) {
        transaction->notification_failed = true;
    } else {
        // This notification came through an ordinary package-visible DOM
        // primitive while the transaction was active, so it is an owned write
        // rather than a stale external edit (D7.2.5).
        transaction->expected_epoch = dom_mutation_epoch(document);
    }
    return true;
}

extern "C" DomRange** dom_range_state_live_ranges_slot(DocState* state);

extern "C" bool dom_mutation_transaction_active(DocState* state) {
    return state && state->editing.dom_mutation_transaction != nullptr;
}

static void editing_dom_attribute_snapshot_free(
        DomMutationAttributeSnapshot* snapshot) {
    if (!snapshot) return;
    mem_free(snapshot->name);
    mem_free(snapshot->value);
    mem_free(snapshot);
}

static void editing_dom_attribute_snapshots_free(ArrayList* attributes) {
    if (!attributes) return;
    for (int i = 0; i < attributes->length; i++) {
        editing_dom_attribute_snapshot_free(
            static_cast<DomMutationAttributeSnapshot*>(attributes->data[i]));
    }
    arraylist_free(attributes);
}

static void editing_dom_node_snapshot_free(DomDocument* document,
                                           DomMutationNodeSnapshot* snapshot,
                                           bool release_text_string) {
    if (!snapshot) return;
    editing_dom_attribute_snapshots_free(snapshot->attributes);
    if (release_text_string && snapshot->text_string && document &&
        document->document_pool) {
        pool_free(document->document_pool, snapshot->text_string);
    }
    mem_free(snapshot);
}

static void editing_dom_node_snapshots_free(DomDocument* document,
                                            ArrayList* snapshots,
                                            bool release_text_strings) {
    if (!snapshots) return;
    for (int i = 0; i < snapshots->length; i++) {
        editing_dom_node_snapshot_free(document,
            static_cast<DomMutationNodeSnapshot*>(snapshots->data[i]),
            release_text_strings);
    }
    arraylist_free(snapshots);
}

static ArrayList* editing_dom_attribute_snapshots_clone(
        const ArrayList* source) {
    if (!source || source->length == 0) return nullptr;
    ArrayList* copy = arraylist_new(source->length);
    if (!copy) return nullptr;
    for (int i = 0; i < source->length; i++) {
        DomMutationAttributeSnapshot* original =
            static_cast<DomMutationAttributeSnapshot*>(source->data[i]);
        DomMutationAttributeSnapshot* snapshot =
            static_cast<DomMutationAttributeSnapshot*>(mem_calloc(
                1, sizeof(DomMutationAttributeSnapshot), MEM_CAT_TEMP));
        if (!original || !snapshot) {
            editing_dom_attribute_snapshot_free(snapshot);
            editing_dom_attribute_snapshots_free(copy);
            return nullptr;
        }
        snapshot->name = mem_strdup(original->name, MEM_CAT_TEMP);
        snapshot->value = mem_strdup(original->value, MEM_CAT_TEMP);
        if (!snapshot->name || !snapshot->value ||
            !arraylist_append(copy, snapshot)) {
            editing_dom_attribute_snapshot_free(snapshot);
            editing_dom_attribute_snapshots_free(copy);
            return nullptr;
        }
    }
    return copy;
}

static ArrayList* editing_dom_node_snapshots_clone(
        DomDocument* document, const ArrayList* source) {
    if (!document || !source) return nullptr;
    ArrayList* copy = arraylist_new(source->length);
    if (!copy) return nullptr;
    for (int i = 0; i < source->length; i++) {
        DomMutationNodeSnapshot* original =
            static_cast<DomMutationNodeSnapshot*>(source->data[i]);
        DomMutationNodeSnapshot* snapshot =
            static_cast<DomMutationNodeSnapshot*>(mem_calloc(
                1, sizeof(DomMutationNodeSnapshot), MEM_CAT_TEMP));
        if (!original || !snapshot) {
            editing_dom_node_snapshot_free(document, snapshot, true);
            editing_dom_node_snapshots_free(document, copy, true);
            return nullptr;
        }
        snapshot->node = original->node;
        snapshot->parent = original->parent;
        if (original->attributes) {
            snapshot->attributes = editing_dom_attribute_snapshots_clone(
                original->attributes);
            if (!snapshot->attributes) {
                editing_dom_node_snapshot_free(document, snapshot, true);
                editing_dom_node_snapshots_free(document, copy, true);
                return nullptr;
            }
        }
        if (original->text_string) {
            snapshot->text_string = dom_document_create_string(
                document, original->text_string->chars,
                original->text_string->len);
            if (!snapshot->text_string) {
                editing_dom_node_snapshot_free(document, snapshot, true);
                editing_dom_node_snapshots_free(document, copy, true);
                return nullptr;
            }
        }
        if (!arraylist_append(copy, snapshot)) {
            editing_dom_node_snapshot_free(document, snapshot, true);
            editing_dom_node_snapshots_free(document, copy, true);
            return nullptr;
        }
    }
    return copy;
}

static void editing_dom_transaction_free(DomMutationTransaction* transaction,
                                         bool release_previous_strings) {
    if (!transaction) return;
    if (transaction->undo_entries) {
        for (int i = 0; i < transaction->undo_entries->length; i++) {
            DomMutationUndo* undo = static_cast<DomMutationUndo*>(
                transaction->undo_entries->data[i]);
            if (!undo) continue;
            if (release_previous_strings && undo->previous_string &&
                transaction->document && transaction->document->document_pool) {
                pool_free(transaction->document->document_pool,
                          undo->previous_string);
            }
            free(undo->previous_value);
            mem_free(undo);
        }
        arraylist_free(transaction->undo_entries);
    }
    if (transaction->range_snapshots) {
        for (int i = 0; i < transaction->range_snapshots->length; i++) {
            mem_free(transaction->range_snapshots->data[i]);
        }
        arraylist_free(transaction->range_snapshots);
    }
    if (transaction->node_snapshots) {
        editing_dom_node_snapshots_free(transaction->document,
                                        transaction->node_snapshots,
                                        release_previous_strings);
    }
    if (transaction->notifications) {
        for (int i = 0; i < transaction->notifications->length; i++) {
            DomMutationNotification* notification =
                static_cast<DomMutationNotification*>(
                    transaction->notifications->data[i]);
            if (!notification) continue;
            mem_free(notification->attribute_name);
            mem_free(notification->old_value);
            mem_free(notification);
        }
        arraylist_free(transaction->notifications);
    }
    if (transaction->edit_session_root_before_rooted) {
        heap_unregister_gc_root(&transaction->edit_session_root_before);
    }
    mem_free(transaction);
}

bool dom_edit_transaction_snapshot_edit_session(DocState* state) {
    DomMutationTransaction* transaction =
        state ? state->editing.dom_mutation_transaction : nullptr;
    if (!transaction || transaction->state != state) return true;
    if (transaction->edit_session_root_before_rooted) return true;
    transaction->edit_session_root_before = state->editing.dom_edit_session_root;
    if (!heap_try_register_gc_root(&transaction->edit_session_root_before)) {
        transaction->edit_session_root_before = ItemNull.item;
        return false;
    }
    transaction->edit_session_root_before_rooted = true;
    return true;
}

static bool editing_dom_snapshot_attributes(DomElement* element,
                                            ArrayList** out_attributes) {
    if (out_attributes) *out_attributes = nullptr;
    if (!element || !out_attributes) return false;
    int count = 0;
    const char** names = element->attribute_names(&count);
    if (count <= 0) return true;
    ArrayList* attributes = arraylist_new(count);
    if (!attributes) return false;
    for (int i = 0; i < count; i++) {
        const char* name = names ? names[i] : nullptr;
        const char* value = name ? element->get_attribute(name) : nullptr;
        if (!name || !value) continue;
        DomMutationAttributeSnapshot* snapshot =
            static_cast<DomMutationAttributeSnapshot*>(mem_calloc(
                1, sizeof(DomMutationAttributeSnapshot), MEM_CAT_TEMP));
        if (!snapshot) {
            editing_dom_attribute_snapshots_free(attributes);
            return false;
        }
        snapshot->name = mem_strdup(name, MEM_CAT_TEMP);
        snapshot->value = mem_strdup(value, MEM_CAT_TEMP);
        if (!snapshot->name || !snapshot->value ||
            !arraylist_append(attributes, snapshot)) {
            editing_dom_attribute_snapshot_free(snapshot);
            editing_dom_attribute_snapshots_free(attributes);
            return false;
        }
    }
    *out_attributes = attributes;
    return true;
}

static bool editing_dom_snapshot_tree(DomDocument* document,
                                      ArrayList* snapshots,
                                      DomNode* parent) {
    if (!document || !snapshots || !parent || !parent->is_element()) return false;
    DomElement* element = parent->as_element();
    for (DomNode* child = element->first_child; child;
         child = child->next_sibling) {
        DomMutationNodeSnapshot* snapshot =
            static_cast<DomMutationNodeSnapshot*>(mem_calloc(
                1, sizeof(DomMutationNodeSnapshot), MEM_CAT_TEMP));
        if (!snapshot) return false;
        snapshot->node = child;
        snapshot->parent = parent;
        if (child->is_element() && !editing_dom_snapshot_attributes(
                child->as_element(), &snapshot->attributes)) {
            editing_dom_node_snapshot_free(document, snapshot, true);
            return false;
        }
        if (child->is_text()) {
            DomText* text = child->as_text();
            snapshot->text_string = dom_document_create_string(
                document, text->text ? text->text : "", text->length);
            if (!snapshot->text_string) {
                editing_dom_node_snapshot_free(document, snapshot, true);
                return false;
            }
        }
        if (!arraylist_append(snapshots, snapshot)) {
            editing_dom_node_snapshot_free(document, snapshot, true);
            return false;
        }
        if (child->is_element() &&
            !editing_dom_snapshot_tree(document, snapshots, child)) {
            return false;
        }
    }
    return true;
}

static bool editing_dom_transaction_snapshot_tree(
        DomMutationTransaction* transaction, DomNode* parent) {
    return transaction && editing_dom_snapshot_tree(transaction->document,
        transaction->node_snapshots, parent);
}

static void editing_dom_transaction_detach_tree(DomNode* parent) {
    if (!parent || !parent->is_element()) return;
    DomElement* element = parent->as_element();
    while (element->first_child) {
        DomNode* child = element->first_child;
        if (child->is_element()) editing_dom_transaction_detach_tree(child);
        parent->remove_child(child);
    }
}

static void editing_dom_transaction_restore_tree(
        DomMutationTransaction* transaction) {
    if (!transaction || !transaction->host || !transaction->node_snapshots) {
        return;
    }
    // First discard nodes created by the failed plan, then separate every
    // pre-existing node from whatever partial parent the plan left it under.
    editing_dom_transaction_detach_tree(static_cast<DomNode*>(transaction->host));
    for (int i = transaction->node_snapshots->length - 1; i >= 0; i--) {
        DomMutationNodeSnapshot* snapshot =
            static_cast<DomMutationNodeSnapshot*>(
                transaction->node_snapshots->data[i]);
        if (snapshot && snapshot->node && snapshot->node->parent) {
            snapshot->node->parent->remove_child(snapshot->node);
        }
    }
    // Snapshots are preorder, so a node's original parent has been restored
    // before the node itself is appended.  That preserves every original node
    // identity and sibling order without allocating a replacement tree.
    for (int i = 0; i < transaction->node_snapshots->length; i++) {
        DomMutationNodeSnapshot* snapshot =
            static_cast<DomMutationNodeSnapshot*>(
                transaction->node_snapshots->data[i]);
        if (!snapshot || !snapshot->node || !snapshot->parent) continue;
        if (snapshot->node->is_text() && snapshot->text_string) {
            DomText* text = snapshot->node->as_text();
            if (!dom_text_replace_backed_string(text, snapshot->text_string)) {
                dom_text_adopt_document_string(text, transaction->document,
                                                snapshot->text_string);
            }
            snapshot->text_string = nullptr;
        }
        snapshot->parent->append_child(snapshot->node);
    }
}

static DomMutationTransaction* editing_dom_transaction_begin(
        DomEditInvocation* invocation) {
    if (!invocation || !invocation->active || !invocation->state ||
        !invocation->host || !invocation->host->doc) {
        return nullptr;
    }
    if (invocation->transaction) return invocation->transaction;
    DocState* state = invocation->state;
    if (state->editing.dom_mutation_transaction) return nullptr;

    DomMutationTransaction* transaction = static_cast<DomMutationTransaction*>(
        mem_calloc(1, sizeof(DomMutationTransaction), MEM_CAT_TEMP));
    if (!transaction) return nullptr;
    transaction->range_snapshots = arraylist_new(4);
    transaction->node_snapshots = arraylist_new(16);
    transaction->undo_entries = arraylist_new(2);
    transaction->notifications = arraylist_new(4);
    if (!transaction->range_snapshots || !transaction->node_snapshots ||
        !transaction->undo_entries || !transaction->notifications) {
        editing_dom_transaction_free(transaction, true);
        return nullptr;
    }
    transaction->state = state;
    transaction->document = invocation->host->doc;
    transaction->host = invocation->host;
    transaction->expected_epoch = dom_mutation_epoch(transaction->document);
    if (transaction->expected_epoch != invocation->mutation_epoch) {
        editing_dom_transaction_free(transaction, true);
        return nullptr;
    }
    if (!editing_dom_transaction_snapshot_tree(
            transaction, static_cast<DomNode*>(transaction->host))) {
        editing_dom_transaction_free(transaction, true);
        return nullptr;
    }

    DomRange** ranges = dom_range_state_live_ranges_slot(state);
    for (DomRange* range = ranges ? *ranges : nullptr; range;
         range = range->next) {
        DomMutationRangeSnapshot* snapshot =
            static_cast<DomMutationRangeSnapshot*>(mem_calloc(
                1, sizeof(DomMutationRangeSnapshot), MEM_CAT_TEMP));
        if (!snapshot) {
            editing_dom_transaction_free(transaction, true);
            return nullptr;
        }
        snapshot->range = range;
        snapshot->start = range->start;
        snapshot->end = range->end;
        snapshot->layout_valid = range->layout_valid;
        if (!arraylist_append(transaction->range_snapshots, snapshot)) {
            mem_free(snapshot);
            editing_dom_transaction_free(transaction, true);
            return nullptr;
        }
    }

    DomSelection* selection = state->dom_selection;
    if (selection) {
        DomMutationSelectionSnapshot* snapshot = &transaction->selection_snapshot;
        snapshot->selection = selection;
        snapshot->range_count = selection->range_count;
        snapshot->direction = selection->direction;
        snapshot->notified_range_count = selection->notified_range_count;
        snapshot->notified_direction = selection->notified_direction;
        snapshot->associated_doc_root = selection->associated_doc_root;
        for (uint32_t i = 0; i < DOM_SELECTION_MAX_RANGES; i++) {
            snapshot->ranges[i] = selection->ranges[i];
            snapshot->notified_starts[i] = selection->notified_starts[i];
            snapshot->notified_ends[i] = selection->notified_ends[i];
        }
    }
    state->editing.dom_mutation_transaction = transaction;
    invocation->transaction = transaction;
    return transaction;
}

bool dom_edit_invocation_begin_transaction(DomEditInvocation* invocation) {
    return editing_dom_transaction_begin(invocation) != nullptr;
}

static void editing_dom_transaction_restore_ranges(
        DomMutationTransaction* transaction) {
    if (!transaction || !transaction->range_snapshots) return;
    for (int i = 0; i < transaction->range_snapshots->length; i++) {
        DomMutationRangeSnapshot* snapshot =
            static_cast<DomMutationRangeSnapshot*>(
                transaction->range_snapshots->data[i]);
        if (!snapshot || !snapshot->range) continue;
        snapshot->range->start = snapshot->start;
        snapshot->range->end = snapshot->end;
        snapshot->range->layout_valid = snapshot->layout_valid;
    }
}

static void editing_dom_transaction_restore_selection(
        DomMutationTransaction* transaction) {
    if (!transaction) return;
    DomMutationSelectionSnapshot* snapshot = &transaction->selection_snapshot;
    DomSelection* selection = snapshot->selection;
    if (!selection) return;
    // Selection methods replace their range objects. Reattach the exact
    // pre-transaction identities while notifications remain deferred, then
    // restore the observable snapshot before leaving the transaction.
    dom_selection_remove_all_ranges(selection);
    for (uint32_t i = 0; i < snapshot->range_count; i++) {
        DomRange* range = snapshot->ranges[i];
        if (range) dom_selection_add_range(selection, range);
    }
    selection->direction = snapshot->direction;
    selection->notified_range_count = snapshot->notified_range_count;
    selection->notified_direction = snapshot->notified_direction;
    selection->associated_doc_root = snapshot->associated_doc_root;
    for (uint32_t i = 0; i < DOM_SELECTION_MAX_RANGES; i++) {
        selection->notified_starts[i] = snapshot->notified_starts[i];
        selection->notified_ends[i] = snapshot->notified_ends[i];
    }
}

static void editing_dom_transaction_abort(DomEditInvocation* invocation) {
    DomMutationTransaction* transaction = invocation ? invocation->transaction : nullptr;
    if (!transaction) return;
    editing_dom_transaction_restore_tree(transaction);
    for (int i = transaction->undo_entries->length - 1; i >= 0; i--) {
        DomMutationUndo* undo = static_cast<DomMutationUndo*>(
            transaction->undo_entries->data[i]);
        if (!undo || undo->kind != DOM_MUTATION_UNDO_TEXT_REPLACE ||
            !undo->text || !undo->previous_string) {
            continue;
        }
        if (!dom_text_replace_backed_string(undo->text, undo->previous_string)) {
            dom_text_adopt_document_string(undo->text, transaction->document,
                                           undo->previous_string);
        }
        // The restored String is now text-owned; do not free it with the journal.
        undo->previous_string = nullptr;
    }
    editing_dom_transaction_restore_ranges(transaction);
    editing_dom_transaction_restore_selection(transaction);
    if (transaction->edit_session_root_before_rooted && transaction->state &&
        transaction->state->editing.dom_edit_session_rooted) {
        transaction->state->editing.dom_edit_session_root =
            transaction->edit_session_root_before;
    }
    if (transaction->state &&
        transaction->state->editing.dom_mutation_transaction == transaction) {
        transaction->state->editing.dom_mutation_transaction = nullptr;
    }
    dom_mutation_transaction_resync(transaction->state);
    editing_dom_transaction_free(transaction, true);
    invocation->transaction = nullptr;
    invocation->changed = false;
    invocation->selection_changed = false;
    invocation->selection_has_extent = false;
    invocation->failed = true;
}

bool dom_edit_invocation_abort_transaction(DomEditInvocation* invocation) {
    if (!invocation || !invocation->transaction) return false;
    editing_dom_transaction_abort(invocation);
    return true;
}

static bool editing_dom_transaction_commit(DomEditInvocation* invocation) {
    DomMutationTransaction* transaction = invocation ? invocation->transaction : nullptr;
    if (!transaction) return true;
    if (transaction->notification_failed ||
        dom_mutation_epoch(transaction->document) != transaction->expected_epoch) {
        // A foreign DOM write invalidates this capability. The inverse strings
        // were allocated before the first change, so rollback needs no new heap.
        editing_dom_transaction_abort(invocation);
        return false;
    }
    bool changed = transaction->changed;
    if (transaction->state &&
        transaction->state->editing.dom_mutation_transaction == transaction) {
        transaction->state->editing.dom_mutation_transaction = nullptr;
    }
    dom_mutation_transaction_resync(transaction->state);
    for (int i = 0; i < transaction->notifications->length; i++) {
        DomMutationNotification* notification =
            static_cast<DomMutationNotification*>(
                transaction->notifications->data[i]);
        if (!notification) continue;
        dom_notify_mutation_detail(notification->kind, notification->target,
                                   notification->parent,
                                   notification->attribute_name,
                                   notification->old_value);
    }
    editing_dom_transaction_free(transaction, true);
    invocation->transaction = nullptr;
    invocation->changed = invocation->changed || changed;
    editing_dom_invocation_note_mutation(invocation);
    return true;
}

static bool editing_dom_transaction_epoch_is_current(
        const DomEditInvocation* invocation) {
    const DomMutationTransaction* transaction = invocation ? invocation->transaction : nullptr;
    return !transaction || (transaction->document &&
        dom_mutation_epoch(transaction->document) == transaction->expected_epoch);
}

static bool editing_dom_transaction_reject(DomEditInvocation* invocation) {
    if (invocation && invocation->transaction) {
        editing_dom_transaction_abort(invocation);
    }
    return false;
}

static bool editing_dom_transaction_replace_text(
        DomEditInvocation* invocation, DomText* text, uint32_t start_u16,
        uint32_t end_u16, const char* replacement, uint32_t* out_caret_u16) {
    if (!editing_dom_invocation_epoch_is_current(invocation) || !text ||
        end_u16 < start_u16) {
        return false;
    }
    DomMutationTransaction* transaction = editing_dom_transaction_begin(invocation);
    if (!transaction || !text->text || end_u16 > dom_text_utf16_length(text)) {
        return editing_dom_transaction_reject(invocation);
    }
    const char* original = text->text;
    size_t original_len = text->length;
    DomMutationUndo* undo = static_cast<DomMutationUndo*>(mem_calloc(
        1, sizeof(DomMutationUndo), MEM_CAT_TEMP));
    if (!undo) return editing_dom_transaction_reject(invocation);
    undo->previous_string = dom_document_create_string(transaction->document,
                                                        original, original_len);
    undo->previous_value = str_dup(original, original_len);
    if (!undo->previous_string || !undo->previous_value ||
        !arraylist_append(transaction->undo_entries, undo)) {
        if (undo->previous_string && transaction->document->document_pool) {
            pool_free(transaction->document->document_pool, undo->previous_string);
        }
        free(undo->previous_value);
        mem_free(undo);
        return editing_dom_transaction_reject(invocation);
    }
    undo->kind = DOM_MUTATION_UNDO_TEXT_REPLACE;
    undo->text = text;
    const char* repl = replacement ? replacement : "";
    uint32_t repl_bytes = (uint32_t)strlen(repl);
    uint32_t repl_u16 = tc_utf8_to_utf16_length(repl, repl_bytes);
    if (!dom_text_replace_data_contents(transaction->state, text, start_u16,
                                        end_u16 - start_u16, repl, repl_bytes,
                                        repl_u16)) {
        transaction->undo_entries->length--;
        if (transaction->document->document_pool) {
            pool_free(transaction->document->document_pool, undo->previous_string);
        }
        free(undo->previous_value);
        mem_free(undo);
        return editing_dom_transaction_reject(invocation);
    }
    if (!editing_dom_transaction_append_notification(transaction,
            DOM_JS_MUTATION_TEXT, static_cast<DomNode*>(text), text->parent,
            nullptr, undo->previous_value)) {
        return editing_dom_transaction_reject(invocation);
    }
    transaction->changed = true;
    if (out_caret_u16) *out_caret_u16 = start_u16 + repl_u16;
    return true;
}

// F13.3: composition can deliberately move the caret without replacing text.
// That result remains on its explicit invocation rather than affecting an outer
// nested edit.
bool dom_edit_set_caret_u16(DomEditInvocation* invocation, uint32_t caret_u16) {
    if (!invocation || !invocation->active || !invocation->text) return false;
    if (caret_u16 > dom_text_utf16_length(invocation->text)) return false;
    editing_dom_record_caret(invocation, static_cast<DomNode*>(invocation->text),
                             caret_u16, false);
    return true;
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
        dom_notify_mutation_detail(DOM_JS_MUTATION_TEXT, text, text->parent,
                                      nullptr, old_value);
        if (out_caret_u16) *out_caret_u16 = start_u16 + repl_u16;
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

// Range insertion links a node into sibling pointers only. Editing must also
// update the backing Lambda Element, so route the detached text through the
// shared Mark-aware DOM bridge (D5.3.3).
static bool editing_dom_insert_detached_text(DomElement* parent,
                                             DomNode* reference,
                                             DomText* text) {
    if (!parent || !text ||
        (reference && reference->parent != static_cast<DomNode*>(parent))) {
        return false;
    }
    RootFrame roots(2);
    Rooted<Item> text_item(roots, dom_wrap_element(text));
    Rooted<Item> reference_item(roots,
        reference ? dom_wrap_element(reference) : ItemNull);
    if (text_item.get().item == ItemNull.item ||
        (reference && reference_item.get().item == ItemNull.item)) {
        return false;
    }
    Item inserted = reference
        ? dom_insert_before_bridge(parent, text_item.get(), reference_item.get())
        : dom_append_child_bridge(parent, text_item.get());
    return inserted.item != ItemNull.item &&
        text->parent == static_cast<DomNode*>(parent);
}

static bool editing_dom_insert_text_at_boundary(DocState* state,
                                                DomBoundary boundary,
                                                DomText* text) {
    if (!state || !boundary.node || !text) return false;
    DomElement* parent = nullptr;
    DomNode* reference = nullptr;
    if (boundary.node->is_text()) {
        DomText* existing = boundary.node->as_text();
        uint32_t length = dom_text_utf16_length(existing);
        if (!existing->parent || boundary.offset > length) return false;
        parent = existing->parent->as_element();
        if (!parent) return false;
        if (boundary.offset == 0) {
            reference = static_cast<DomNode*>(existing);
        } else if (boundary.offset == length) {
            reference = existing->next_sibling;
        } else {
            DomText* right = dom_text_split_at(state, existing, boundary.offset);
            if (!right) return false;
            reference = static_cast<DomNode*>(right);
        }
    } else if (boundary.node->is_element()) {
        parent = boundary.node->as_element();
        if (boundary.offset > dom_node_boundary_length(boundary.node)) return false;
        reference = editing_dom_element_child_at(parent, boundary.offset);
    } else {
        return false;
    }
    return editing_dom_insert_detached_text(parent, reference, text);
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

bool dom_edit_invocation_begin(DocState* state, DomElement* host,
                               DomBoundary start, DomBoundary end,
                               DomEditInvocation* invocation) {
    if (!state || !host || !invocation || !start.node || !end.node ||
        !dom_boundary_is_valid(&start) || !dom_boundary_is_valid(&end) ||
        !editing_dom_host_contains_boundary(host, start) ||
        !editing_dom_host_contains_boundary(host, end)) {
        return false;
    }
    DomBoundaryOrder order = dom_boundary_compare(&start, &end);
    if (order == DOM_BOUNDARY_DISJOINT || order == DOM_BOUNDARY_AFTER) return false;
    *invocation = {};
    uint64_t id = ++state->editing.next_dom_edit_invocation_id;
    if (id == 0) id = ++state->editing.next_dom_edit_invocation_id;
    invocation->id = id;
    invocation->state = state;
    invocation->host = host;
    EditingHost host_info;
    invocation->plaintext_only = editing_host_lookup(
        static_cast<DomNode*>(host), &host_info) &&
        host_info.host == host && host_info.mode == EditingHost::PlaintextOnly;
    invocation->start = start;
    invocation->end = end;
    invocation->mutation_epoch = dom_mutation_epoch(host->doc);
    invocation->active = true;
    // A Range is always ordered, whereas Selection anchor/focus retain author
    // direction. Keep that invocation fact before generic tree surgery so a
    // wrapped or unwrapped run can restore the same directional extent.
    if (state->dom_selection && !dom_selection_is_collapsed(state->dom_selection)) {
        DomBoundary anchor = dom_selection_anchor_boundary(state->dom_selection);
        DomBoundary focus = dom_selection_focus_boundary(state->dom_selection);
        if (editing_dom_host_contains_boundary(host, anchor) &&
            editing_dom_host_contains_boundary(host, focus)) {
            invocation->selection_was_backward =
                dom_boundary_compare(&anchor, &focus) == DOM_BOUNDARY_AFTER;
        }
    }
    editing_dom_single_text_range(host, start, end, &invocation->text,
                                  &invocation->text_start, &invocation->text_end);
    invocation->next = state->editing.dom_edit_invocations;
    state->editing.dom_edit_invocations = invocation;
    return true;
}

void dom_edit_invocation_end(DomEditInvocation* invocation) {
    if (!invocation || !invocation->state || !invocation->active) return;
    // An edit may be declined after a primitive has provisionally changed a
    // text node. Only selection commit publishes its transaction; ending an
    // uncommitted invocation is therefore an abort, never a partial edit.
    if (invocation->transaction) editing_dom_transaction_abort(invocation);
    DomEditInvocation** link = &invocation->state->editing.dom_edit_invocations;
    while (*link && *link != invocation) link = &(*link)->next;
    if (*link == invocation) *link = invocation->next;
    invocation->active = false;
    invocation->next = nullptr;
}

DomEditInvocation* dom_edit_invocation_lookup_document(DocState* state,
                                                       uint64_t id) {
    if (!state || id == 0) return nullptr;
    for (DomEditInvocation* current = state->editing.dom_edit_invocations;
         current; current = current->next) {
        if (current->id == id && current->active && current->state == state &&
            current->host && editing_dom_invocation_epoch_is_current(current)) {
            return current;
        }
    }
    return nullptr;
}

DomEditInvocation* dom_edit_invocation_lookup(DocState* state,
                                              DomElement* receiver,
                                              uint64_t id) {
    if (!receiver) return nullptr;
    DomEditInvocation* invocation = dom_edit_invocation_lookup_document(state, id);
    // Capability operations that mutate or expose selection remain scoped to
    // the editable host or its descendants; document handlers use edit_target
    // first to obtain that canonical receiver (D7.2.5).
    if (!invocation || invocation->host->doc != receiver->doc ||
        !editing_dom_node_is_within(invocation->host, receiver)) {
        return nullptr;
    }
    return invocation;
}

bool dom_edit_invocation_commit_selection(DomEditInvocation* invocation) {
    if (!invocation || !invocation->active || !invocation->state ||
        !invocation->state->dom_selection) return false;
    if (!editing_dom_transaction_epoch_is_current(invocation)) {
        editing_dom_transaction_abort(invocation);
        return false;
    }
    bool selection_committed = false;
    const char* exception = nullptr;
    if (invocation->selection_has_extent) {
        if (!dom_selection_set_base_and_extent(invocation->state->dom_selection,
                invocation->selection_anchor.node,
                invocation->selection_anchor.offset,
                invocation->selection_focus.node,
                invocation->selection_focus.offset, &exception)) {
            log_error("dom_edit_invocation: failed to apply committed selection: %s",
                      exception ? exception : "unknown");
            editing_dom_transaction_abort(invocation);
            return false;
        }
        selection_committed = true;
    } else if (invocation->selection_changed && invocation->caret_node) {
        if (!dom_selection_collapse(invocation->state->dom_selection,
                                    invocation->caret_node,
                                    invocation->caret_offset, &exception)) {
            log_error("dom_edit_invocation: failed to collapse committed selection: %s",
                      exception ? exception : "unknown");
            editing_dom_transaction_abort(invocation);
            return false;
        }
        selection_committed = true;
    }
    if (!editing_dom_transaction_commit(invocation)) return false;
    return selection_committed;
}

bool dom_edit_invocation_replace_range_u16(DomEditInvocation* invocation,
                                           DomText* text,
                                           uint32_t start_u16,
                                           uint32_t end_u16,
                                           const char* replacement,
                                           uint32_t* out_caret_u16) {
    if (!editing_dom_transaction_replace_text(invocation, text, start_u16,
                                              end_u16, replacement,
                                              out_caret_u16)) {
        return false;
    }
    uint32_t caret = out_caret_u16 ? *out_caret_u16 : start_u16;
    editing_dom_record_caret(invocation, static_cast<DomNode*>(text), caret, true);
    return true;
}

// Reconstitute the explicit range for a structural operation. The single-text
// cache remains a fast path, while the invocation retains both DOM endpoints
// for cross-node replacement and deletion.
static bool editing_dom_invocation_range(DomEditInvocation* invocation,
                                         DomRange* out_range) {
    if (!invocation || !invocation->active || !out_range ||
        !invocation->state || !invocation->host || !invocation->start.node ||
        !invocation->end.node ||
        !editing_dom_host_contains_boundary(invocation->host, invocation->start) ||
        !editing_dom_host_contains_boundary(invocation->host, invocation->end) ||
        !dom_boundary_is_valid(&invocation->start) ||
        !dom_boundary_is_valid(&invocation->end)) {
        return false;
    }
    DomBoundaryOrder order = dom_boundary_compare(&invocation->start,
                                                   &invocation->end);
    if (order == DOM_BOUNDARY_DISJOINT || order == DOM_BOUNDARY_AFTER) return false;
    *out_range = {};
    out_range->state = invocation->state;
    out_range->start = invocation->start;
    out_range->end = invocation->end;
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
        dom_notify_mutation_detail(DOM_JS_MUTATION_TEXT, text, text->parent,
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
    if (!inserted || !editing_dom_insert_text_at_boundary(state, boundary,
                                                            inserted)) {
        log_debug("editing_dom_action: element-boundary insertion rejected");
        return false;
    }
    const char* exception = nullptr;
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
bool dom_edit_insert_at_boundary_u16(DomEditInvocation* invocation,
                                     const char* text_data,
                                     uint32_t* out_caret_u16) {
    if (!editing_dom_invocation_epoch_is_current(invocation) || !text_data) return false;
    DocState* state = invocation->state;
    DomElement* host = invocation->host;
    DomNode* boundary_node = invocation->start.node;
    DomSelection* selection = state ? state->dom_selection : nullptr;
    if (!host || !boundary_node || !selection) return false;
    if (!editing_dom_transaction_begin(invocation)) return false;
    DomBoundary boundary = invocation->start;
    DomText* inserted = nullptr;
    if (!editing_dom_insert_at_boundary(state, selection, host, boundary,
                                        text_data, &inserted)) {
        return editing_dom_transaction_reject(invocation);
    }
    uint32_t bytes = (uint32_t)strlen(text_data);
    // A text-node boundary inserts *within* the node, so the caret lands past
    // the insertion point; a fresh node puts it at the end of what was created.
    uint32_t caret_u16 = boundary_node->is_text()
        ? boundary.offset + tc_utf8_to_utf16_length(text_data, bytes)
        : tc_utf8_to_utf16_length(text_data, bytes);
    editing_dom_record_caret(invocation, static_cast<DomNode*>(inserted),
                             caret_u16, true);
    if (out_caret_u16) *out_caret_u16 = caret_u16;
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

// Wrapping and unwrapping replace the selected text node.  Preserve the
// captured anchor/focus direction instead of treating the ordered Range as a
// forward Selection; this is generic Selection mapping, not format policy.
static void editing_dom_select_mapped_text(DomEditInvocation* invocation,
                                           DomNode* text_node) {
    if (!invocation || !text_node || !text_node->is_text()) return;
    DomText* mapped_text = text_node->as_text();
    if (!mapped_text) return;
    DomBoundary start = {text_node, 0};
    DomBoundary end = {text_node,
                       dom_text_utf16_length(mapped_text)};
    // A partial-range split retires the text wrapper the package initially
    // observed. Keep the explicit invocation capability mapped to its selected
    // replacement, so a follow-up generic attribute write decorates the new
    // wrapper rather than a detached left sibling (D7.2.5).
    invocation->text = mapped_text;
    invocation->text_start = 0;
    invocation->text_end = end.offset;
    invocation->selection_anchor = invocation->selection_was_backward ? end : start;
    invocation->selection_focus = invocation->selection_was_backward ? start : end;
    invocation->selection_has_extent = true;
    invocation->selection_changed = true;
}

// Wrap [start, end) of the resolved text node in a fresh `tag` element.
static bool editing_dom_insert_child(DocState* state, DomElement* parent,
                                     DomNode* child, DomNode* reference);
static bool editing_dom_move_child(DocState* state, DomNode* child,
                                   DomElement* destination, DomNode* reference);
static bool editing_dom_remove_child(DocState* state, DomNode* child);

bool dom_edit_wrap_range_u16(DomEditInvocation* invocation, uint32_t start_u16,
                             uint32_t end_u16, const char* tag) {
    if (!editing_dom_invocation_epoch_is_current(invocation) || !tag || !*tag) return false;
    DocState* state = invocation->state;
    DomElement* host = invocation->host;
    DomText* text = invocation->text;
    if (!host || !text || !text->parent || !state->dom_selection) return false;
    uint32_t total = dom_text_utf16_length(text);
    if (end_u16 > total) end_u16 = total;
    if (start_u16 >= end_u16) return false;
    DomDocument* doc = host->doc;
    if (!doc) return false;
    if (!editing_dom_transaction_begin(invocation)) return false;

    // Split the tail off first. Splitting the head would move `end_u16` into the
    // node the split produced, so the second offset would address the wrong node.
    if (end_u16 < total && !dom_text_split_at(state, text, end_u16)) {
        return editing_dom_transaction_reject(invocation);
    }
    DomText* middle = start_u16 > 0 ? dom_text_split_at(state, text, start_u16)
                                    : text;
    if (!middle) return editing_dom_transaction_reject(invocation);

    DomElement* wrapper = (DomElement*)dom_create_backed_element_bridge(doc, tag);
    if (!wrapper) return editing_dom_transaction_reject(invocation);
    DomNode* wrapper_node = static_cast<DomNode*>(wrapper);
    DomNode* middle_node = static_cast<DomNode*>(middle);
    DomNode* parent = middle_node->parent;
    if (!parent || !parent->is_element() ||
        !editing_dom_insert_child(state, parent->as_element(), wrapper_node,
                                  middle_node)) {
        return editing_dom_transaction_reject(invocation);
    }
    if (!editing_dom_move_child(state, middle_node, wrapper, nullptr)) {
        return editing_dom_transaction_reject(invocation);
    }

    // Formatting leaves the run selected, the way a browser does, so a second
    // command applies to the same span. This is recorded as an explicit
    // selection outcome rather than leaking through a caret side channel.
    editing_dom_select_mapped_text(invocation, middle_node);
    invocation->changed = true;
    editing_dom_invocation_note_mutation(invocation);
    return true;
}

static bool editing_dom_move_child(DocState* state, DomNode* child,
                                   DomElement* destination, DomNode* reference) {
    if (!state || !child || !destination || !child->parent) return false;
    if (reference && reference->parent != static_cast<DomNode*>(destination)) {
        return false;
    }
    RootFrame roots(2);
    Rooted<Item> child_item(roots, dom_wrap_element(child));
    Rooted<Item> reference_item(roots,
        reference ? dom_wrap_element(reference) : ItemNull);
    if (child_item.get().item == ItemNull.item ||
        (reference && reference_item.get().item == ItemNull.item)) {
        return false;
    }
    // The Mark-aware bridges own DOM move semantics. Pre-removing a static
    // text node clears its backing String before the destination can retain it.
    Item inserted = reference
        ? dom_insert_before_bridge(destination, child_item.get(), reference_item.get())
        : dom_append_child_bridge(destination, child_item.get());
    return inserted.item != ItemNull.item &&
        child->parent == static_cast<DomNode*>(destination);
}

static bool editing_dom_insert_child(DocState* state, DomElement* parent,
                                     DomNode* child, DomNode* reference) {
    if (!state || !parent || !child || child->parent ||
        (reference && reference->parent != static_cast<DomNode*>(parent))) {
        return false;
    }
    RootFrame roots(2);
    Rooted<Item> child_item(roots, dom_wrap_element(child));
    Rooted<Item> reference_item(roots,
        reference ? dom_wrap_element(reference) : ItemNull);
    if (child_item.get().item == ItemNull.item ||
        (reference && reference_item.get().item == ItemNull.item)) {
        return false;
    }
    Item inserted = reference
        ? dom_insert_before_bridge(parent, child_item.get(), reference_item.get())
        : dom_append_child_bridge(parent, child_item.get());
    return inserted.item != ItemNull.item &&
        child->parent == static_cast<DomNode*>(parent);
}

static bool editing_dom_remove_child(DocState* state, DomNode* child) {
    if (!state || !child || !child->parent) return false;
    DomNode* parent = child->parent;
    if (!parent->is_element()) return false;
    RootFrame roots(1);
    Rooted<Item> child_item(roots, dom_wrap_element(child));
    if (child_item.get().item == ItemNull.item) return false;
    return dom_remove_child_bridge(parent->as_element(), child_item.get()).item !=
        ItemNull.item;
}

static bool editing_dom_retained_selection_from_boundaries(
        DomBoundary start, DomBoundary end, DomSelectionDirection direction,
        DomRetainedSelection* out_selection) {
    if (!out_selection) return false;
    *out_selection = {};
    if (!start.node || !end.node) return true;
    if (!dom_boundary_is_valid(&start) || !dom_boundary_is_valid(&end)) {
        return false;
    }
    out_selection->present = true;
    if (direction == DOM_SEL_DIR_BACKWARD) {
        out_selection->anchor = end;
        out_selection->focus = start;
    } else {
        out_selection->anchor = start;
        out_selection->focus = end;
    }
    return true;
}

static bool editing_dom_retained_selection_before(
        const DomMutationTransaction* transaction,
        DomRetainedSelection* out_selection) {
    if (!transaction || !out_selection) return false;
    *out_selection = {};
    const DomMutationSelectionSnapshot* selection =
        &transaction->selection_snapshot;
    if (!selection->selection || selection->range_count == 0 ||
        !selection->ranges[0]) {
        return true;
    }
    for (int i = 0; i < transaction->range_snapshots->length; i++) {
        DomMutationRangeSnapshot* range =
            static_cast<DomMutationRangeSnapshot*>(
                transaction->range_snapshots->data[i]);
        if (range && range->range == selection->ranges[0]) {
            return editing_dom_retained_selection_from_boundaries(
                range->start, range->end, selection->direction, out_selection);
        }
    }
    return false;
}

static bool editing_dom_retained_selection_after(
        const DomEditInvocation* invocation,
        DomRetainedSelection* out_selection) {
    if (!invocation || !out_selection) return false;
    *out_selection = {};
    if (invocation->selection_has_extent) {
        out_selection->present = true;
        out_selection->anchor = invocation->selection_anchor;
        out_selection->focus = invocation->selection_focus;
        return dom_boundary_is_valid(&out_selection->anchor) &&
               dom_boundary_is_valid(&out_selection->focus);
    }
    if (invocation->selection_changed && invocation->caret_node) {
        out_selection->present = true;
        out_selection->anchor = {invocation->caret_node, invocation->caret_offset};
        out_selection->focus = out_selection->anchor;
        return dom_boundary_is_valid(&out_selection->anchor);
    }
    DomSelection* selection = invocation->state
        ? invocation->state->dom_selection : nullptr;
    if (!selection || selection->range_count == 0 || !selection->ranges[0]) {
        return true;
    }
    DomRange* range = selection->ranges[0];
    return editing_dom_retained_selection_from_boundaries(
        range->start, range->end, selection->direction, out_selection);
}

static void editing_dom_retained_mutation_free(
        DomRetainedMutation* retained) {
    if (!retained) return;
    editing_dom_node_snapshots_free(retained->document, retained->before_nodes,
                                    true);
    editing_dom_node_snapshots_free(retained->document, retained->after_nodes,
                                    true);
    mem_free(retained);
}

extern "C" void dom_edit_discard_retained_deltas(DocState* state) {
    if (!state) return;
    DomRetainedMutation* retained = state->editing.dom_edit_retained_mutations;
    state->editing.dom_edit_retained_mutations = nullptr;
    while (retained) {
        DomRetainedMutation* next = retained->next;
        editing_dom_retained_mutation_free(retained);
        retained = next;
    }
}

bool dom_edit_release_retained_delta(DocState* state, DomElement* host,
                                     uint64_t delta_id) {
    if (!state || !host || delta_id == 0) return false;
    DomRetainedMutation** link = &state->editing.dom_edit_retained_mutations;
    while (*link) {
        DomRetainedMutation* retained = *link;
        if (retained->id == delta_id && retained->host == host &&
            retained->document == host->doc) {
            *link = retained->next;
            editing_dom_retained_mutation_free(retained);
            return true;
        }
        link = &retained->next;
    }
    return false;
}

uint64_t dom_edit_invocation_retain_delta(DomEditInvocation* invocation) {
    DomMutationTransaction* transaction = invocation ? invocation->transaction : nullptr;
    if (!invocation || !transaction || !transaction->changed ||
        !invocation->active || !invocation->state || !invocation->host ||
        transaction->document != invocation->host->doc ||
        !editing_dom_transaction_epoch_is_current(invocation)) {
        return 0;
    }
    DomRetainedMutation* retained = static_cast<DomRetainedMutation*>(
        mem_calloc(1, sizeof(DomRetainedMutation), MEM_CAT_TEMP));
    if (!retained) return 0;
    retained->document = transaction->document;
    retained->host = invocation->host;
    retained->before_nodes = editing_dom_node_snapshots_clone(
        retained->document, transaction->node_snapshots);
    if (!retained->before_nodes) {
        editing_dom_retained_mutation_free(retained);
        return 0;
    }
    retained->after_nodes = arraylist_new(16);
    if (!retained->after_nodes || !editing_dom_snapshot_tree(retained->document,
            retained->after_nodes, static_cast<DomNode*>(retained->host)) ||
        !editing_dom_retained_selection_before(transaction,
            &retained->before_selection) ||
        !editing_dom_retained_selection_after(invocation,
            &retained->after_selection)) {
        editing_dom_retained_mutation_free(retained);
        return 0;
    }
    uint64_t id = ++invocation->state->editing.next_dom_edit_retained_delta_id;
    if (id == 0) id = ++invocation->state->editing.next_dom_edit_retained_delta_id;
    retained->id = id;
    retained->expected_epoch = dom_mutation_epoch(retained->document);
    retained->next = invocation->state->editing.dom_edit_retained_mutations;
    invocation->state->editing.dom_edit_retained_mutations = retained;
    return id;
}

static bool editing_dom_retained_detach_tree(DocState* state, DomNode* parent) {
    if (!state || !parent || !parent->is_element()) return false;
    DomElement* element = parent->as_element();
    while (element->first_child) {
        DomNode* child = element->first_child;
        if (child->is_element() &&
            !editing_dom_retained_detach_tree(state, child)) {
            return false;
        }
        if (!editing_dom_remove_child(state, child)) return false;
    }
    return true;
}

static bool editing_dom_retained_restore_attributes(
        DocState* state, DomElement* element, const ArrayList* expected) {
    if (!state || !element) return false;
    ArrayList* current = nullptr;
    if (!editing_dom_snapshot_attributes(element, &current)) return false;
    for (int i = 0; current && i < current->length; i++) {
        DomMutationAttributeSnapshot* attribute =
            static_cast<DomMutationAttributeSnapshot*>(current->data[i]);
        if (!attribute || !element->remove_attribute(attribute->name)) {
            editing_dom_attribute_snapshots_free(current);
            return false;
        }
        dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, element,
                                   element->parent, attribute->name,
                                   attribute->value);
    }
    editing_dom_attribute_snapshots_free(current);
    for (int i = 0; expected && i < expected->length; i++) {
        DomMutationAttributeSnapshot* attribute =
            static_cast<DomMutationAttributeSnapshot*>(expected->data[i]);
        if (!attribute || !element->set_attribute(attribute->name,
                                                   attribute->value)) {
            return false;
        }
        dom_notify_mutation_detail(DOM_JS_MUTATION_ATTRIBUTE, element,
                                   element->parent, attribute->name, nullptr);
    }
    return true;
}

static bool editing_dom_retained_prepare_text(DomMutationNodeSnapshot* snapshot) {
    if (!snapshot || !snapshot->node || !snapshot->node->is_text() ||
        !snapshot->text_string) {
        return false;
    }
    DomText* text = snapshot->node->as_text();
    if (text->native_string) return true;
    // Detaching static Mark text clears its native String. Borrow the retained
    // snapshot until the Mark-aware insertion bridge replaces it with the
    // destination's canonical backing; the snapshot remains reusable.
    text->native_string = snapshot->text_string;
    text->text = snapshot->text_string->chars;
    text->length = snapshot->text_string->len;
    text->set_owns_native_string(false);
    return true;
}

static bool editing_dom_retained_restore_snapshot(
        DomEditInvocation* invocation, const ArrayList* snapshots) {
    if (!invocation || !invocation->state || !invocation->host || !snapshots) {
        return false;
    }
    DocState* state = invocation->state;
    if (!editing_dom_retained_detach_tree(state,
            static_cast<DomNode*>(invocation->host))) return false;
    for (int i = snapshots->length - 1; i >= 0; i--) {
        DomMutationNodeSnapshot* snapshot =
            static_cast<DomMutationNodeSnapshot*>(snapshots->data[i]);
        if (snapshot && snapshot->node && snapshot->node->parent &&
            !editing_dom_remove_child(state, snapshot->node)) return false;
    }
    for (int i = 0; i < snapshots->length; i++) {
        DomMutationNodeSnapshot* snapshot =
            static_cast<DomMutationNodeSnapshot*>(snapshots->data[i]);
        if (snapshot && snapshot->node && snapshot->node->is_text() &&
            !editing_dom_retained_prepare_text(snapshot)) return false;
        if (!snapshot || !snapshot->node || !snapshot->parent ||
            !snapshot->parent->is_element() || snapshot->node->parent) return false;
        DomElement* restore_parent = snapshot->parent->as_element();
        if (!editing_dom_insert_child(state, restore_parent, snapshot->node, nullptr)) {
            return false;
        }
        if (snapshot->node->is_element() &&
            !editing_dom_retained_restore_attributes(state,
                snapshot->node->as_element(), snapshot->attributes)) return false;
        if (snapshot->node->is_text() && snapshot->text_string) {
            DomText* text = snapshot->node->as_text();
            const char* old_value = text->text ? text->text : "";
            if (strcmp(old_value, snapshot->text_string->chars) != 0) {
                char* copied_old_value = mem_strdup(old_value, MEM_CAT_TEMP);
                if (!copied_old_value || !dom_text_replace_data_contents(state,
                        text, 0, dom_text_utf16_length(text),
                        snapshot->text_string->chars,
                        snapshot->text_string->len,
                                                 tc_utf8_to_utf16_length(snapshot->text_string->chars,
                                                                         snapshot->text_string->len))) {
                    mem_free(copied_old_value);
                    return false;
                }
                dom_notify_mutation_detail(DOM_JS_MUTATION_TEXT, text,
                                           text->parent, nullptr,
                                           copied_old_value);
                mem_free(copied_old_value);
            }
        }
    }
    return true;
}

static bool editing_dom_retained_restore_selection(DomEditInvocation* invocation,
                                                   DomRetainedSelection selection) {
    if (!invocation || !invocation->state || !invocation->state->dom_selection) {
        return false;
    }
    DomSelection* dom_selection = invocation->state->dom_selection;
    if (!selection.present) {
        dom_selection_remove_all_ranges(dom_selection);
        return true;
    }
    if (!dom_boundary_is_valid(&selection.anchor) ||
        !dom_boundary_is_valid(&selection.focus) ||
        !editing_dom_host_contains_boundary(invocation->host, selection.anchor) ||
        !editing_dom_host_contains_boundary(invocation->host, selection.focus)) {
        return false;
    }
    const char* exception = nullptr;
    return dom_selection_set_base_and_extent(dom_selection, selection.anchor.node,
        selection.anchor.offset, selection.focus.node, selection.focus.offset,
        &exception);
}

static bool editing_dom_retained_attributes_match(
        DomElement* element, const ArrayList* expected) {
    if (!element) return false;
    int count = 0;
    const char** names = element->attribute_names(&count);
    int expected_count = expected ? expected->length : 0;
    if (count != expected_count) return false;
    for (int i = 0; i < expected_count; i++) {
        DomMutationAttributeSnapshot* attribute =
            static_cast<DomMutationAttributeSnapshot*>(expected->data[i]);
        const char* actual = attribute ? element->get_attribute(attribute->name)
                                       : nullptr;
        if (!attribute || !actual || strcmp(actual, attribute->value) != 0) {
            return false;
        }
    }
    (void)names;
    return true;
}

static bool editing_dom_retained_snapshot_matches_tree(
        DomNode* parent, const ArrayList* snapshots, int* index) {
    if (!parent || !snapshots || !index || !parent->is_element()) return false;
    for (DomNode* child = parent->as_element()->first_child; child;
         child = child->next_sibling) {
        if (*index >= snapshots->length) return false;
        DomMutationNodeSnapshot* snapshot =
            static_cast<DomMutationNodeSnapshot*>(snapshots->data[*index]);
        if (!snapshot || snapshot->node != child || snapshot->parent != parent) return false;
        (*index)++;
        if (child->is_element() &&
            (!editing_dom_retained_attributes_match(child->as_element(),
                                                    snapshot->attributes) ||
             !editing_dom_retained_snapshot_matches_tree(child, snapshots,
                                                        index))) {
            return false;
        }
        if (child->is_text() && snapshot->text_string &&
            strcmp(child->as_text()->text ? child->as_text()->text : "",
                   snapshot->text_string->chars) != 0) return false;
    }
    return true;
}

// A document epoch includes unrelated script activity (such as rendering a
// command result elsewhere in the page). History instead validates the exact
// identity-preserving host snapshot it will invert, so only divergent edits in
// the retained host invalidate this delta (D7.2.5).
static bool editing_dom_retained_snapshot_matches(DomElement* host,
                                                  const ArrayList* snapshots) {
    int index = 0;
    return host && snapshots && editing_dom_retained_snapshot_matches_tree(
        static_cast<DomNode*>(host), snapshots, &index) &&
        index == snapshots->length;
}

bool dom_edit_invocation_replay_delta(DomEditInvocation* invocation,
                                      uint64_t delta_id, bool undo) {
    if (!invocation || !invocation->active || !invocation->state ||
        !invocation->host || delta_id == 0) {
        return false;
    }
    DomRetainedMutation* retained =
        invocation->state->editing.dom_edit_retained_mutations;
    while (retained && retained->id != delta_id) retained = retained->next;
    const ArrayList* source = undo ? (retained ? retained->after_nodes : nullptr)
                                   : (retained ? retained->before_nodes : nullptr);
    bool snapshot_matches = retained &&
        editing_dom_retained_snapshot_matches(invocation->host, source);
    if (!retained || retained->host != invocation->host ||
        retained->document != invocation->host->doc || !snapshot_matches ||
        !editing_dom_transaction_begin(invocation)) return false;
    const ArrayList* target = undo ? retained->before_nodes : retained->after_nodes;
    DomRetainedSelection selection = undo ? retained->before_selection
                                          : retained->after_selection;
    if (!editing_dom_retained_restore_snapshot(invocation, target) ||
        !editing_dom_retained_restore_selection(invocation, selection)) {
        return editing_dom_transaction_reject(invocation);
    }
    retained->expected_epoch = dom_mutation_epoch(retained->document);
    invocation->changed = true;
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

// Apply an explicit DOM range and optionally insert text at its start. The
// package chooses deletion, replacement, and any block join; this helper only
// performs generic Range mechanics and records the resulting caret.
static bool editing_dom_apply_range(DomEditInvocation* invocation,
                                    const char* replacement) {
    if (!invocation || !invocation->active) return false;
    DocState* state = invocation->state;
    DomRange operation = {};
    if (!editing_dom_invocation_range(invocation, &operation)) return false;
    DomElement* host = invocation->host;
    const char* repl = replacement ? replacement : "";
    size_t repl_bytes = strlen(repl);
    bool collapsed = dom_range_collapsed(&operation);
    if (collapsed && repl_bytes == 0) return false;
    if (!editing_dom_transaction_begin(invocation)) return false;

    DomBoundary original_start = operation.start;
    if (!collapsed) {
        const char* exception = nullptr;
        if (!dom_range_delete_contents(&operation, &exception) || exception) {
            log_debug("F14.2: pending DOM range delete rejected: %s",
                      exception ? exception : "unknown");
            return editing_dom_transaction_reject(invocation);
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
        return editing_dom_transaction_reject(invocation);
    }

    if (repl_bytes > 0) {
        DomText* inserted = DomText::create_detached_copy(host->doc, repl,
                                                           repl_bytes);
        if (!inserted) return editing_dom_transaction_reject(invocation);
        if (!editing_dom_insert_text_at_boundary(state, caret, inserted)) {
            log_debug("F14.2: pending DOM range insertion rejected");
            return editing_dom_transaction_reject(invocation);
        }
        caret = { static_cast<DomNode*>(inserted),
                  dom_text_utf16_length(inserted) };
    }

    editing_dom_record_caret(invocation, caret.node, caret.offset, true);
    dom_notify_mutation(DOM_JS_MUTATION_TREE_REPLACE, host, host);
    // The aggregate range-replace record is published after the caret fact.
    // Advance the capability only after every mutation in this plan step.
    editing_dom_invocation_note_mutation(invocation);
    return true;
}

static bool editing_dom_prepare_structural_caret(DomEditInvocation* invocation,
                                                 DomBoundary* out_caret) {
    if (!invocation || !invocation->active || !out_caret) return false;
    DomRange operation = {};
    if (!editing_dom_invocation_range(invocation, &operation)) return false;
    if (!dom_range_collapsed(&operation)) {
        if (!editing_dom_apply_range(invocation, "")) return false;
        out_caret->node = invocation->caret_node;
        out_caret->offset = invocation->caret_offset;
    } else {
        *out_caret = operation.start;
    }
    DomElement* host = invocation->host;
    return out_caret->node && dom_boundary_is_valid(out_caret) &&
        editing_dom_host_contains_boundary(host, *out_caret);
}

static bool editing_dom_split_block(DocState* state, DomElement* host,
                                    DomElement* source, const char* tag,
                                    DomBoundary caret, DomBoundary* out_caret) {
    if (!state || !host || !source || !tag || !*tag || !out_caret ||
        !caret.node || !editing_dom_node_is_within(source, host)) {
        return false;
    }
    DomElement* parent = source == host ? host :
        (source->parent && source->parent->is_element()
            ? source->parent->as_element() : nullptr);
    DomNode* source_node = static_cast<DomNode*>(source);
    if (!parent || !source_node ||
        !editing_dom_node_is_within(caret.node, source_node)) return false;

    DomElement* new_block = (DomElement*)dom_create_backed_element_bridge(host->doc, tag);
    if (!new_block) return false;
    DomNode* reference = source == host ? nullptr : source_node->next_sibling;
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

    DomElement* br = (DomElement*)dom_create_backed_element_bridge(parent->doc, "br");
    if (!br || !editing_dom_insert_child(state, parent, static_cast<DomNode*>(br),
                                         reference)) {
        return false;
    }
    *out_caret = { static_cast<DomNode*>(parent),
                   dom_node_child_index(static_cast<DomNode*>(br)) + 1 };
    return true;
}

// F14.2 structural waist entry points. The package names the command; these
// functions expose only explicit range/DOM mechanisms and a caret result.
bool dom_edit_replace_range(DomEditInvocation* invocation,
                            const char* replacement) {
    return editing_dom_apply_range(invocation, replacement);
}

bool dom_edit_delete_range(DomEditInvocation* invocation) {
    return editing_dom_apply_range(invocation, "");
}

// The package classifies blocks and supplies the exact adjacent pair.  Native
// code only validates ownership and performs the requested child moves.
bool dom_edit_merge_adjacent_blocks(DomEditInvocation* invocation,
                                    DomElement* start_block,
                                    DomElement* end_block) {
    if (!invocation || !invocation->active || !start_block || !end_block ||
        !start_block->parent || start_block->parent != end_block->parent ||
        start_block->next_sibling != static_cast<DomNode*>(end_block) ||
        !editing_dom_node_is_within(start_block, invocation->host) ||
        !editing_dom_node_is_within(end_block, invocation->host)) {
        return false;
    }
    if (!editing_dom_transaction_begin(invocation)) return false;
    while (end_block->first_child) {
        if (!editing_dom_move_child(invocation->state, end_block->first_child,
                                    start_block, nullptr)) {
            return editing_dom_transaction_reject(invocation);
        }
    }
    if (!editing_dom_remove_child(invocation->state,
                                  static_cast<DomNode*>(end_block))) {
        return editing_dom_transaction_reject(invocation);
    }
    if (!editing_dom_node_is_within(invocation->caret_node, start_block)) {
        editing_dom_record_caret(invocation, static_cast<DomNode*>(start_block),
                                 dom_node_boundary_length(
                                     static_cast<DomNode*>(start_block)), true);
    }
    editing_dom_invocation_note_mutation(invocation);
    return true;
}

bool dom_edit_insert_paragraph(DomEditInvocation* invocation,
                               DomElement* source, const char* tag) {
    if (!invocation || !invocation->active) return false;
    DocState* state = invocation->state;
    DomElement* host = invocation->host;
    if (!host || !editing_dom_transaction_begin(invocation)) return false;
    DomBoundary caret = {};
    if (!editing_dom_prepare_structural_caret(invocation, &caret)) {
        return editing_dom_transaction_reject(invocation);
    }
    DomBoundary new_caret = {};
    if (!editing_dom_split_block(state, host, source, tag, caret, &new_caret)) {
        return editing_dom_transaction_reject(invocation);
    }
    editing_dom_record_caret(invocation, new_caret.node, new_caret.offset, true);
    dom_notify_mutation(DOM_JS_MUTATION_TREE_REPLACE, host, host);
    editing_dom_invocation_note_mutation(invocation);
    return true;
}

bool dom_edit_insert_line_break(DomEditInvocation* invocation) {
    if (!invocation || !invocation->active) return false;
    DocState* state = invocation->state;
    DomElement* host = invocation->host;
    if (!host || !editing_dom_transaction_begin(invocation)) return false;
    DomBoundary caret = {};
    if (!editing_dom_prepare_structural_caret(invocation, &caret)) {
        return editing_dom_transaction_reject(invocation);
    }
    DomBoundary new_caret = {};
    if (!editing_dom_insert_break_at(state, caret, &new_caret)) {
        return editing_dom_transaction_reject(invocation);
    }
    editing_dom_record_caret(invocation, new_caret.node, new_caret.offset, true);
    dom_notify_mutation(DOM_JS_MUTATION_TREE_REPLACE, host, host);
    editing_dom_invocation_note_mutation(invocation);
    return true;
}

// Selection-only command mechanism. The package selects the host extent; this
// validates the invocation and commits it through the ordinary Selection path.
bool dom_edit_select_host(DomEditInvocation* invocation) {
    if (!editing_dom_invocation_epoch_is_current(invocation) ||
        !invocation->host) {
        return false;
    }
    DomNode* host = static_cast<DomNode*>(invocation->host);
    invocation->selection_anchor = {host, 0};
    invocation->selection_focus = {host, dom_node_boundary_length(host)};
    invocation->selection_has_extent = true;
    invocation->selection_changed = true;
    return true;
}

// Remove a package-selected wrapper over [start, end), promoting its children.
// Partial ranges split the one-text-child formatting shell into left/right
// formatted siblings and move the selected middle text between them. More
// general nested, multi-child, and cross-node unwrap shapes remain future work.
bool dom_edit_unwrap_range_u16(DomEditInvocation* invocation, uint32_t start_u16,
                               uint32_t end_u16, DomElement* fmt) {
    if (!editing_dom_invocation_epoch_is_current(invocation) || !fmt) return false;
    DocState* state = invocation->state;
    DomElement* host = invocation->host;
    DomText* text = invocation->text;
    if (!host || !text || !state->dom_selection) return false;
    if (!fmt->parent || !editing_dom_node_is_within(fmt, host) ||
        !editing_dom_node_is_within(text, fmt)) return false;
    DomNode* text_node = static_cast<DomNode*>(text);
    if (fmt->first_child != text_node || text_node->next_sibling) return false;
    uint32_t total = dom_text_utf16_length(text);
    if (start_u16 >= end_u16 || end_u16 > total) return false;
    if (!editing_dom_transaction_begin(invocation)) return false;

    // Split the selected text before moving any node. The split envelope keeps
    // all live ranges valid, and the resulting sibling identities let the
    // structural move below preserve the exact selected text.
    if (end_u16 < total && !dom_text_split_at(state, text, end_u16)) {
        return editing_dom_transaction_reject(invocation);
    }
    DomText* middle = start_u16 > 0 ? dom_text_split_at(state, text, start_u16)
                                    : text;
    if (!middle) return editing_dom_transaction_reject(invocation);

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
        right_fmt = (DomElement*)dom_create_backed_element_bridge(host->doc,
                                                                    fmt->tag_name);
        if (!right_fmt) return editing_dom_transaction_reject(invocation);
        DomNode* right_fmt_node = static_cast<DomNode*>(right_fmt);
        if (!editing_dom_insert_child(state, parent->as_element(), right_fmt_node,
                                      after_fmt)) {
            return editing_dom_transaction_reject(invocation);
        }
    }

    // With a left side the unformatted middle follows the original shell. With
    // no left side it must precede the shell, which now contains the right side.
    DomNode* reference = left
        ? (right_fmt ? static_cast<DomNode*>(right_fmt) : after_fmt)
        : fmt_node;
    if (!editing_dom_move_child(state, middle_node,
                                lam::dom_require_element(parent), reference)) {
        return editing_dom_transaction_reject(invocation);
    }

    if (right_fmt && !editing_dom_move_child(
            state, right, right_fmt, nullptr)) {
        return editing_dom_transaction_reject(invocation);
    }

    // A full-range unwrap leaves an empty original shell after the selected
    // text has moved before it; remove that shell rather than exposing an empty
    // formatting element in innerHTML.
    if (!left && !right) {
        if (!editing_dom_remove_child(state, fmt_node)) {
            return editing_dom_transaction_reject(invocation);
        }
    }

    editing_dom_select_mapped_text(invocation, middle_node);
    invocation->changed = true;
    editing_dom_invocation_note_mutation(invocation);
    return true;
}


// Insert a parsed fragment over the selection. The parse and the Range splice
// are the JS DOM's own — this is the same mechanism the retired `insertHTML`
// bridge drove, reached from the package instead of from a native special case.
bool dom_edit_insert_html(DomEditInvocation* invocation, const char* html) {
    if (!editing_dom_invocation_epoch_is_current(invocation) || !html) return false;
    DomElement* host = invocation->host;
    if (!host || !host->doc) return false;
    if (!editing_dom_transaction_begin(invocation)) return false;
    // The fragment parser can move the heap, and `html` is a Lambda string the
    // collector may relocate; copy before handing it over, as the bridge did.
    char* stable = mem_strdup(html, MEM_CAT_TEMP);
    if (!stable) return editing_dom_transaction_reject(invocation);
    bool inserted = dom_exec_insert_html(host->doc, stable);
    mem_free(stable);
    if (inserted) {
        invocation->changed = true;
        editing_dom_invocation_note_mutation(invocation);
    }
    else editing_dom_transaction_reject(invocation);
    return inserted;
}

extern "C" bool radiant_dispatch_behavior_exec_command(View* target,
                                                       const InputIntent* intent,
                                                       Item* out_result);
extern "C" bool radiant_dispatch_behavior_design_mode(View* target,
                                                       const InputIntent* intent,
                                                       Item* out_result);

static Item editing_result_field(Item result, const char* name) {
    if (get_type_id(result) != LMD_TYPE_MAP || !result.map || !name) return ItemNull;
    return map_get(result.map, (Item){.item = s2it(heap_create_name(name))});
}

static bool editing_result_bool(Item result, const char* name) {
    Item field = editing_result_field(result, name);
    return get_type_id(field) == LMD_TYPE_BOOL && it2b(field);
}

static char* editing_result_string_copy(Item result, const char* name) {
    Item field = editing_result_field(result, name);
    if (get_type_id(field) == LMD_TYPE_NULL) return nullptr;
    const char* text = fn_to_cstr(field);
    return text ? mem_strdup(text, MEM_CAT_TEMP) : nullptr;
}

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
    // support/enabled from its descriptor (D7.2.5).
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
            editing_dom_host_contains_boundary(host_info.host, range->start) &&
            editing_dom_host_contains_boundary(host_info.host, range->end) &&
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
    // D7.2.5: post-commit API event facts come from the structured package
    // result. This bridge carries no command-name or inputType policy.
    bool emit_input = editing_result_bool(edit_result.get(), "api_input");
    char* input_type = editing_result_string_copy(edit_result.get(), "api_input_type");
    char* input_data = editing_result_string_copy(edit_result.get(), "api_input_data");
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

// D7.2.5: legacy query methods only transport a context and command spelling.
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
            editing_dom_host_contains_boundary(host_info.host, range->start) &&
            editing_dom_host_contains_boundary(host_info.host, range->end) &&
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
