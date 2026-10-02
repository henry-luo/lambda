#include "dom_lifecycle.hpp"

#include "dom_element.hpp"
#include "dom_node.hpp"
#include "../../lambda-data.hpp"
#include "../../../lib/arena.h"
#include "../../../lib/log.h"
#include "../../../lib/time_util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct DomNodeRecord {
    DomNode* address;
    Arena* primary_arena;
    uint32_t id;
    DomNodeType type;
    DomNodeLifeState state;
    bool recyclable;
    bool candidate;
    bool current_owner;
    bool retiring;
    size_t primary_size;
    Element* backing_source;
    uint32_t pins[DOM_NODE_PIN_REASON_COUNT];
    struct DomNodeRecord* bucket_next;
    struct DomNodeRecord* all_next;
    struct DomNodeRecord* retire_next;
} DomNodeRecord;

typedef struct DomNodeRegistry {
    DomNodeRecord** buckets;
    size_t bucket_count;
    size_t record_count;
    DomNodeRecord* all_records;
    DomDocument* document;
    DomNodeRecord* pending_free;
    struct DomNodeRegistry* queue_prev;
    struct DomNodeRegistry* queue_next;
    bool queued;
    bool sweep_requested;
    bool destroying;
    DomLifecycleStats stats;
} DomNodeRegistry;

static thread_local bool dom_retirement_deferred;
static thread_local DomNodeRegistry* dom_retirement_queue;
static thread_local DomNodeRegistry* dom_retirement_queue_tail;

static void dom_retire_dequeue(DomNodeRegistry* registry) {
    if (!registry || !registry->queued) return;
    if (registry->queue_prev) registry->queue_prev->queue_next = registry->queue_next;
    else dom_retirement_queue = registry->queue_next;
    if (registry->queue_next) registry->queue_next->queue_prev = registry->queue_prev;
    else dom_retirement_queue_tail = registry->queue_prev;
    registry->queue_prev = registry->queue_next = nullptr;
    registry->queued = false;
}

static void dom_retire_enqueue(DomNodeRegistry* registry) {
    if (registry->queued || registry->destroying) return;
    registry->queue_prev = dom_retirement_queue_tail;
    if (dom_retirement_queue_tail) dom_retirement_queue_tail->queue_next = registry;
    else dom_retirement_queue = registry;
    dom_retirement_queue_tail = registry;
    registry->queued = true;
}

// DOM-only unit targets do not link the Radiant view teardown implementation.
__attribute__((weak)) void view_tree_release_retired_subtree(ViewTree*, DomNode*) {}
__attribute__((weak)) void view_tree_release_detached_embedded_documents(ViewTree*, DomNode*) {}
__attribute__((weak)) void view_tree_prepare_detached_subtree(DomNode*) {}
__attribute__((weak)) void view_pool_release_detached_form_props(DomNode*) {}
__attribute__((weak)) void form_control_release_prop(DomElement*) {}
__attribute__((weak)) void dom_range_refresh_lifecycle_pins(DomDocument*) {}
__attribute__((weak)) void dom_retire_release_render_result(DomDocument*, Item) {}
extern "C" __attribute__((weak)) void dom_expando_attachment_changed(
    DomDocument*, DomNode*, bool) {}

static void dom_lifecycle_fail(const char* operation, DomNodeRef ref,
                               DomNodePinReason reason) {
    log_error("DOM_LIFECYCLE_INVARIANT: op=%s address=%p expected_id=%u reason=%u",
              operation ? operation : "unknown", (void*)ref.address,
              ref.expected_id, (unsigned)reason);
#ifndef NDEBUG
    assert(false && "DOM lifecycle registry invariant failed");
#else
    abort();
#endif
}

static DomNodeRegistry* dom_registry(DomDocument* doc) {
    return doc ? (DomNodeRegistry*)doc->services.node_registry : nullptr;
}

static size_t dom_node_bucket(DomNodeRegistry* registry, DomNode* address) {
    uintptr_t value = (uintptr_t)address;
    value ^= value >> 17u;
    value *= (uintptr_t)0xed5ad4bbu;
    value ^= value >> 11u;
    return (size_t)(value & (registry->bucket_count - 1u));
}

static DomNodeRecord* dom_record_find(DomNodeRegistry* registry, DomNode* address) {
    if (!registry || !registry->buckets || !address) return nullptr;
    size_t bucket = dom_node_bucket(registry, address);
    for (DomNodeRecord* record = registry->buckets[bucket]; record;
         record = record->bucket_next) {
        if (record->address == address) return record;
    }
    return nullptr;
}

static Arena* dom_node_primary_arena(DomDocument* doc, DomNode* node) {
    if (!doc || !node) return nullptr;
    if (doc->node_arena && arena_owns(doc->node_arena, node)) {
        return doc->node_arena;
    }
    // UI-mode Lambda values embed DomNode storage in the retained Input arena.
    // Recording that physical owner keeps the single arena recycler valid for
    // both parsed nodes and fat Lambda-backed nodes.
    if (doc->input && doc->input->arena &&
        arena_owns(doc->input->arena, node)) {
        return doc->input->arena;
    }
    return nullptr;
}

static bool dom_registry_resize(DomDocument* doc, size_t bucket_count) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || !doc->document_pool || bucket_count < 64) return false;
    DomNodeRecord** buckets = (DomNodeRecord**)pool_calloc(
        doc->document_pool, bucket_count * sizeof(DomNodeRecord*));
    if (!buckets) return false;
    DomNodeRecord** old_buckets = registry->buckets;
    size_t old_count = registry->bucket_count;
    registry->buckets = buckets;
    registry->bucket_count = bucket_count;
    for (DomNodeRecord* record = registry->all_records; record;
         record = record->all_next) {
        size_t bucket = dom_node_bucket(registry, record->address);
        record->bucket_next = buckets[bucket];
        buckets[bucket] = record;
    }
    if (old_buckets && old_count) pool_free(doc->document_pool, old_buckets);
    return true;
}

bool dom_lifecycle_init(DomDocument* doc) {
    if (!doc || !doc->document_pool) return false;
    if (doc->services.node_registry) return true;
    DomNodeRegistry* registry = (DomNodeRegistry*)pool_calloc(
        doc->document_pool, sizeof(DomNodeRegistry));
    if (!registry) return false;
    registry->document = doc;
    doc->services.node_registry = registry;
    if (!dom_registry_resize(doc, 256)) {
        doc->services.node_registry = nullptr;
        pool_free(doc->document_pool, registry);
        return false;
    }
    return true;
}

void dom_lifecycle_destroy(DomDocument* doc) {
    if (!doc) return;
    dom_retire_dequeue(dom_registry(doc));
    // Registry records are document-pool allocations and disappear in the
    // immediately following pool destruction; clearing the owner blocks any
    // teardown callback from validating already-destroyed arena storage.
    doc->services.node_registry = nullptr;
}

static bool dom_node_registry_register_owned(DomDocument* doc, DomNode* node,
        Arena* primary_arena, uint32_t id, DomNodeType type,
        size_t primary_size, bool recyclable, Element* backing_source) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || !node || !id || !primary_size) return false;
    if (recyclable && !primary_arena) {
        log_error("DOM_LIFECYCLE_INVARIANT: recyclable node %p has no owning arena",
                  (void*)node);
        return false;
    }
    DomNodeRecord* record = dom_record_find(registry, node);
    if (record) {
        if (record->state != DOM_NODE_RETIRED) {
            if (record->current_owner && record->id == id &&
                record->primary_size == primary_size) return true;
            dom_lifecycle_fail("duplicate-live-register", dom_node_ref(node),
                               DOM_NODE_PIN_EXTERNAL);
            return false;
        }
        memset(record->pins, 0, sizeof(record->pins));
        record->id = id;
        record->primary_arena = primary_arena;
        record->type = type;
        record->state = DOM_NODE_LIVE;
        record->recyclable = recyclable;
        record->candidate = false;
        record->current_owner = true;
        record->primary_size = primary_size;
        record->backing_source = backing_source;
        registry->stats.reused_addresses++;
        registry->stats.registered_nodes++;
        return true;
    }

    if ((registry->record_count + 1u) * 4u > registry->bucket_count * 3u) {
        if (!dom_registry_resize(doc, registry->bucket_count * 2u)) return false;
    }
    record = (DomNodeRecord*)pool_calloc(doc->document_pool, sizeof(DomNodeRecord));
    if (!record) return false;
    record->address = node;
    record->primary_arena = primary_arena;
    record->id = id;
    record->type = type;
    record->state = DOM_NODE_LIVE;
    record->recyclable = recyclable;
    record->current_owner = true;
    record->primary_size = primary_size;
    record->backing_source = backing_source;
    size_t bucket = dom_node_bucket(registry, node);
    record->bucket_next = registry->buckets[bucket];
    registry->buckets[bucket] = record;
    record->all_next = registry->all_records;
    registry->all_records = record;
    registry->record_count++;
    registry->stats.registered_nodes++;
    return true;
}

bool dom_node_registry_register(DomDocument* doc, DomNode* node,
                                size_t primary_size, bool recyclable) {
    Arena* primary_arena = dom_node_primary_arena(doc, node);
    return dom_node_registry_register_owned(doc, node, primary_arena,
        node ? node->id : 0, node ? node->node_type : DOM_NODE_ELEMENT,
        primary_size, recyclable, nullptr);
}

bool dom_node_registry_transfer(DomDocument* source, DomDocument* destination,
                                DomNode* node, uint32_t* destination_id) {
    if (!source || !destination || !node || !destination_id || !*destination_id) return false;
    DomNodeRecord* source_record = dom_record_find(dom_registry(source), node);
    if (!source_record || !source_record->current_owner ||
        source_record->state == DOM_NODE_RETIRED ||
        source_record->id != node->id) {
        log_error("DOM_LIFECYCLE_TRANSFER: source record is stale for node %p", (void*)node);
        return false;
    }
    DomNodeRecord* destination_record = dom_record_find(dom_registry(destination), node);
    if (destination_record && destination_record->state != DOM_NODE_RETIRED) {
        if (destination_record->current_owner) {
            log_error("DOM_LIFECYCLE_TRANSFER: destination already owns node %p", (void*)node);
            return false;
        }
        bool retained_ref = false;
        for (int reason = 0; reason < DOM_NODE_PIN_REASON_COUNT; reason++) {
            if (destination_record->pins[reason]) retained_ref = true;
        }
        // A returning node must keep an earlier generation while wrappers or
        // ranges still pin it; their saved references use that generation ID.
        if (retained_ref) *destination_id = destination_record->id;
        destination_record->id = *destination_id;
        destination_record->primary_arena = source_record->primary_arena;
        destination_record->type = source_record->type;
        destination_record->state = DOM_NODE_LIVE;
        destination_record->recyclable = source_record->recyclable;
        destination_record->candidate = false;
        destination_record->current_owner = true;
        destination_record->primary_size = source_record->primary_size;
        destination_record->backing_source = source_record->backing_source;
    } else if (!dom_node_registry_register_owned(destination, node,
                   source_record->primary_arena, *destination_id, source_record->type,
                   source_record->primary_size, source_record->recyclable,
                   source_record->backing_source)) {
        return false;
    }
    // The source registry may still own wrapper/expando pins that must unpin
    // normally, but its detached candidate must never recycle adopted storage.
    source_record->candidate = false;
    source_record->state = DOM_NODE_LIVE;
    source_record->current_owner = false;
    return true;
}

void dom_node_registry_set_backing_source(DomDocument* doc, DomNode* node,
                                          Element* backing_source) {
    DomNodeRecord* record = dom_record_find(dom_registry(doc), node);
    if (!record || record->state == DOM_NODE_RETIRED || record->id != node->id) {
        dom_lifecycle_fail("set-backing-source-stale", dom_node_ref(node),
                           DOM_NODE_PIN_EXTERNAL);
        return;
    }
    record->backing_source = backing_source;
}

Element* dom_node_registry_backing_source(DomDocument* doc, DomNode* node) {
    DomNodeRecord* record = dom_record_find(dom_registry(doc), node);
    if (!record || record->state == DOM_NODE_RETIRED || record->id != node->id) {
        return nullptr;
    }
    return record->backing_source;
}

DomNodeRef dom_node_ref(DomNode* node) {
    DomNodeRef ref = {node, node ? node->id : 0};
    return ref;
}

DomNode* dom_node_ref_validate(DomDocument* doc, DomNodeRef ref) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || !ref.address || !ref.expected_id) return nullptr;
    // Lookup is address-only and intentionally does not read the target bytes;
    // an arena slot may already contain a different node generation.
    DomNodeRecord* record = dom_record_find(registry, ref.address);
    if (!record || record->state == DOM_NODE_RETIRED || record->id != ref.expected_id) {
        registry->stats.stale_ref_rejections++;
        return nullptr;
    }
    return ref.address;
}

bool dom_node_registry_owns(DomDocument* doc, DomNode* node) {
    DomNodeRecord* record = dom_record_find(dom_registry(doc), node);
    return record && record->current_owner && record->state != DOM_NODE_RETIRED &&
        record->id == node->id;
}

bool dom_node_pin(DomDocument* doc, DomNodeRef ref, DomNodePinReason reason) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || reason >= DOM_NODE_PIN_REASON_COUNT) return false;
    DomNodeRecord* record = dom_record_find(registry, ref.address);
    if (!record || record->state == DOM_NODE_RETIRED || record->id != ref.expected_id) {
        dom_lifecycle_fail("pin-stale", ref, reason);
        return false;
    }
    record->pins[reason]++;
    return true;
}

bool dom_node_unpin(DomDocument* doc, DomNodeRef ref, DomNodePinReason reason) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || reason >= DOM_NODE_PIN_REASON_COUNT) return false;
    DomNodeRecord* record = dom_record_find(registry, ref.address);
    if (!record || record->state == DOM_NODE_RETIRED || record->id != ref.expected_id ||
        record->pins[reason] == 0) {
        dom_lifecycle_fail("unpin-stale-or-underflow", ref, reason);
        return false;
    }
    record->pins[reason]--;
    return true;
}

uint32_t dom_node_pin_count(DomDocument* doc, DomNodeRef ref,
                            DomNodePinReason reason) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || reason >= DOM_NODE_PIN_REASON_COUNT) return 0;
    DomNodeRecord* record = dom_record_find(registry, ref.address);
    return record && record->id == ref.expected_id ? record->pins[reason] : 0;
}

void dom_node_clear_reason_pins(DomDocument* doc, DomNodePinReason reason) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || reason >= DOM_NODE_PIN_REASON_COUNT) return;
    for (DomNodeRecord* record = registry->all_records; record;
         record = record->all_next) {
        if (record->state != DOM_NODE_RETIRED) record->pins[reason] = 0;
    }
}

static bool dom_node_is_within(DomNode* node, DomNode* ancestor) {
    for (DomNode* current = node; current; current = current->parent) {
        if (current == ancestor) return true;
    }
    return false;
}

void dom_node_schedule_detached(DomDocument* doc, DomNode* root) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || !root || root == (DomNode*)doc->root) return;
    DomNodeRecord* record = dom_record_find(registry, root);
    if (!record || !record->current_owner || record->id != root->id ||
        record->state == DOM_NODE_RETIRED) return;
    for (DomNodeRecord* other = registry->all_records; other; other = other->all_next) {
        if (!other->candidate || other == record) continue;
        if (dom_node_is_within(root, other->address)) return;
        if (dom_node_is_within(other->address, root)) {
            other->candidate = false;
            other->state = DOM_NODE_LIVE;
        }
    }
    if (!record->candidate) {
        record->candidate = true;
        record->state = DOM_NODE_DETACHED_CANDIDATE;
        registry->stats.scheduled_candidates++;
    }
    // A detached table can retain anonymous layout boxes beyond the next view
    // pool reset; restore its authored child chain while those boxes are live.
    if (doc->view_tree) view_tree_prepare_detached_subtree(root);
    // Detached wrapper-owned state must stop being a native-tree GC root;
    // a live JS wrapper remains the sole owner until possible reattachment.
    dom_expando_attachment_changed(doc, root, false);
}

void dom_node_cancel_detached(DomDocument* doc, DomNode* root) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || !root) return;
    DomNodeRecord* record = dom_record_find(registry, root);
    if (!record || !record->current_owner || record->id != root->id ||
        record->state == DOM_NODE_RETIRED) return;
    if (record && record->candidate && record->id == root->id) {
        record->candidate = false;
        record->state = DOM_NODE_LIVE;
        registry->stats.cancelled_candidates++;
    }
    // Reinsertion calls this before linking the parent; the lifecycle decision
    // itself confirms that the subtree is becoming attached again.
    dom_expando_attachment_changed(doc, root, true);
}

static bool dom_subtree_can_retire(DomDocument* doc, DomNode* node,
                                   DomNodeRecord** blocked) {
    // Layout-only nodes need not be in the script DOM registry. Their view
    // owner releases them; they must not keep an authored subtree alive.
    if (node && node->is_element() && node->as_element()->is_synthetic()) {
        return true;
    }
    DomNodeRegistry* registry = dom_registry(doc);
    DomNodeRecord* record = dom_record_find(registry, node);
    // A retired descendant can remain in a detached parent's old sibling
    // chain while the registry is the only safe source of its generation.
    if (!record || record->state == DOM_NODE_RETIRED || !record->recyclable ||
        !record->primary_size || record->id != node->id) {
        if (blocked) *blocked = record;
        return false;
    }
    for (int reason = 0; reason < DOM_NODE_PIN_REASON_COUNT; reason++) {
        if (record->pins[reason]) {
            if (blocked) *blocked = record;
            return false;
        }
    }
    if (node->is_element()) {
        for (DomNode* child = node->as_element()->first_child; child;
             child = child->next_sibling) {
            if (!dom_subtree_can_retire(doc, child, blocked)) return false;
        }
    }
    return true;
}

static DomNode* dom_retire_resolve_edge(DomNodeRegistry* registry, DomNode* node,
                                       bool forward) {
    DomNode* live = node;
    while (live) {
        DomNodeRecord* record = dom_record_find(registry, live);
        if (!record || !record->retiring) break;
        live = forward ? live->next_sibling : live->prev_sibling;
    }
    // Compress shared stale chains before any node storage is reclaimed.
    while (node != live) {
        DomNode*& edge = forward ? node->next_sibling : node->prev_sibling;
        DomNode* next = edge;
        edge = live;
        node = next;
    }
    return live;
}

static void dom_retire_unlink_inbound_edges(DomNodeRegistry* registry) {
    for (DomNodeRecord* record = registry->all_records; record;
         record = record->all_next) {
        registry->stats.retirement_edge_visits++;
        if (record->state == DOM_NODE_RETIRED) continue;
        DomNode* other = record->address;
        other->next_sibling = dom_retire_resolve_edge(registry, other->next_sibling, true);
        other->prev_sibling = dom_retire_resolve_edge(registry, other->prev_sibling, false);
        if (other->is_element()) {
            DomElement* element = other->as_element();
            element->first_child = dom_retire_resolve_edge(registry, element->first_child, true);
            element->last_child = dom_retire_resolve_edge(registry, element->last_child, false);
        }
    }
}

static size_t dom_retire_subtree(DomDocument* doc, DomNode* node,
                                 DomNodeRecord** pending) {
    if (node && node->is_element() && node->as_element()->is_synthetic()) {
        return 0;
    }
    DomNodeRegistry* registry = dom_registry(doc);
    DomNodeRecord* record = dom_record_find(registry, node);
    // Only a fat node recycles its Lambda result identity. Ordinary DOM
    // wrappers borrow parser values that remain valid after wrapper retirement.
    Item result = ItemNull;
    if (node->is_element() && record->backing_source == dom_element_to_element(node->as_element())) {
        result.element = record->backing_source;
    } else if (node->is_text() && node->as_text()->native_string ==
               (String*)(node->as_text() + 1)) {
        result.item = s2it(node->as_text()->native_string);
    }
    if (result.item != ItemNull.item) dom_retire_release_render_result(doc, result);
    size_t retired = 0;
    if (node->is_element()) {
        DomNode* child = node->as_element()->first_child;
        while (child) {
            DomNode* next = child->next_sibling;
            retired += dom_retire_subtree(doc, child, pending);
            child = next;
        }
        dom_element_release_retired_storage(node->as_element());
    } else if (node->is_text()) {
        dom_text_release_retired_storage(doc, node->as_text());
    }

    size_t primary_size = record->primary_size;
    record->candidate = false;
    record->state = DOM_NODE_RETIRED;
    record->retiring = true;
    record->retire_next = *pending;
    *pending = record;
    registry->stats.retired_nodes++;
    registry->stats.retired_primary_bytes += primary_size;
    return retired + 1u;
}

static size_t dom_retire_collect(DomDocument* doc) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || !doc->node_arena) return 0;
    // Range endpoints mutate through many DOM-spec algorithms. Recomputing
    // their pins at the single sweep boundary makes the registry authoritative
    // without allowing a missed setter to expose a detached live endpoint.
    dom_range_refresh_lifecycle_pins(doc);
    size_t retired = 0;
    DomNodeRecord* pending = nullptr;
    for (DomNodeRecord* record = registry->all_records; record;) {
        DomNodeRecord* next = record->all_next;
        if (record->candidate && record->state == DOM_NODE_DETACHED_CANDIDATE) {
            DomNode* root = record->address;
            if (!dom_node_ref_validate(doc, {root, record->id})) {
                record->candidate = false;
            } else if (root->is_element() && root->as_element()->is_synthetic()) {
                // synthetic layout nodes are released by their view owner;
                // lifecycle retirement must not recycle their DOM addresses.
                record->candidate = false;
                record->state = DOM_NODE_LIVE;
            } else if (root->parent) {
                record->candidate = false;
                record->state = DOM_NODE_LIVE;
                registry->stats.rejected_attached++;
            } else {
                // An embedded document pins its iframe host. Release that
                // ownership edge before testing the detached subtree's pins,
                // otherwise iframe removal forms a retention cycle.
                if (doc->view_tree) {
                    view_tree_release_detached_embedded_documents(doc->view_tree, root);
                }
                DomNodeRecord* blocked = nullptr;
                if (dom_subtree_can_retire(doc, root, &blocked)) {
                    if (doc->view_tree) {
                        view_tree_release_retired_subtree(doc->view_tree, root);
                    }
                    retired += dom_retire_subtree(doc, root, &pending);
                } else {
                    registry->stats.rejected_pinned++;
                }
            }
        }
        record = next;
    }
    if (pending) {
        // MarkEditor can leave raw inbound edges outside a detached subtree.
        // Repair all of them once per batch, while sibling chains remain live.
        dom_retire_unlink_inbound_edges(registry);
        while (pending) {
            DomNodeRecord* record = pending;
            pending = record->retire_next;
            record->retire_next = nullptr;
            record->retiring = false;
            if (!registry->destroying) {
                record->retire_next = registry->pending_free;
                registry->pending_free = record;
                registry->stats.pending_primary_bytes += record->primary_size;
            }
        }
    }
    return retired;
}

static void dom_retire_recycle_one(DomNodeRegistry* registry) {
    DomNodeRecord* record = registry->pending_free;
    registry->pending_free = record->retire_next;
    record->retire_next = nullptr;
    // Edges and generation refs are invalidated before a slot becomes reusable.
    memset(record->address, 0xdd, record->primary_size);
    // DOM nodes are the sanctioned arena reuse case: the slot is retained on
    // its arena's retired list for later node allocations, never discarded.
    arena_retire(record->primary_arena, record->address, record->primary_size);
    registry->stats.pending_primary_bytes -= record->primary_size;
    registry->stats.recycled_nodes++;
}

size_t dom_retire_sweep(DomDocument* doc) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry) return 0;
    if (dom_retirement_deferred && !registry->destroying) {
        registry->sweep_requested = true;
        dom_retire_enqueue(registry);
        return 0;
    }
    size_t retired = dom_retire_collect(doc);
    while (registry->pending_free) dom_retire_recycle_one(registry);
    return retired;
}

bool dom_retire_set_deferred(bool enabled) {
    bool previous = dom_retirement_deferred;
    dom_retirement_deferred = enabled;
    return previous;
}

bool dom_retire_idle(uint64_t budget_us) {
    uint64_t start = time_now_us();
    while (dom_retirement_queue) {
        DomNodeRegistry* registry = dom_retirement_queue;
        dom_retire_dequeue(registry);
        if (registry->sweep_requested) {
            registry->sweep_requested = false;
            dom_retire_collect(registry->document);
        }
        // Arena allocation and recycling stay on their owning thread (D4.1.4v4).
        // Yield between frees so coalescing a large retired page cannot block
        // the next input event until the entire page has been recycled.
        while (registry->pending_free && time_now_us() - start < budget_us) {
            dom_retire_recycle_one(registry);
        }
        if (registry->pending_free || registry->sweep_requested) dom_retire_enqueue(registry);
        if (time_now_us() - start >= budget_us) break;
    }
    return dom_retirement_queue != nullptr;
}

void dom_retire_begin_destroy(DomDocument* doc) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry) return;
    dom_retire_dequeue(registry);
    registry->destroying = true;
    registry->sweep_requested = false;
    // The owning arenas are about to be destroyed wholesale. Recycling their
    // individual slots here adds coalescing work without saving any memory.
    registry->pending_free = nullptr;
    registry->stats.pending_primary_bytes = 0;
}

static bool dom_node_is_attached_to_document(DomNodeRegistry* registry,
                                             DomNode* node,
                                             DomNode* document_root) {
    // Follow only nodes confirmed by the registry. Detached DOM trees can
    // retain obsolete sibling links after a child has been retired.
    for (size_t depth = 0; node && depth <= registry->record_count; depth++) {
        if (node == document_root) return true;
        DomNodeRecord* record = dom_record_find(registry, node);
        if (!record || record->state == DOM_NODE_RETIRED) return false;
        node = node->parent;
    }
    return false;
}

static void dom_lifecycle_release_form_props(DomDocument* doc,
                                             bool include_attached) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry) return;
    for (DomNodeRecord* record = registry->all_records; record;
         record = record->all_next) {
        DomNode* node = record->address;
        if (record->state == DOM_NODE_RETIRED || !node || !node->is_element()) {
            continue;
        }
        bool attached = dom_node_is_attached_to_document(registry, node,
            (DomNode*)doc->root);
        if (include_attached || !attached) form_control_release_prop(node->as_element());
    }
}

void dom_lifecycle_release_unattached_form_props(DomDocument* doc) {
    // Releasing each validated element avoids walking stale child links in a
    // detached script-created subtree during document teardown.
    dom_lifecycle_release_form_props(doc, false);
}

void dom_lifecycle_release_all_form_props(DomDocument* doc) {
    dom_lifecycle_release_form_props(doc, true);
}

void dom_lifecycle_get_stats(DomDocument* doc, DomLifecycleStats* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    DomNodeRegistry* registry = dom_registry(doc);
    if (registry) *out = registry->stats;
}

void dom_js_mutation_records_reset(DomDocument* doc) {
    if (!doc) return;
    for (int i = 0; i < doc->js.mutation_record_count; i++) {
        DomJsMutationRecord* record = &doc->js.mutation_records[i];
        if (record->target && record->target_id) {
            dom_node_unpin(doc, {record->target, record->target_id},
                           DOM_NODE_PIN_RECONCILE);
        }
        if (record->parent && record->parent_id) {
            dom_node_unpin(doc, {record->parent, record->parent_id},
                           DOM_NODE_PIN_RECONCILE);
        }
        memset(record, 0, sizeof(*record));
    }
    doc->js.mutation_count = 0;
    doc->js.mutation_sequence = 0;
    doc->js.mutation_kind_mask = 0;
    doc->js.mutation_record_count = 0;
    doc->js.mutation_record_overflow = 0;
    doc->js.inline_stylesheet_mutation_count = 0;
    // Mutation records hold raw nodes across the reconcile pass; release all
    // pins before the single quiescent-point sweep.
    dom_retire_sweep(doc);
}
