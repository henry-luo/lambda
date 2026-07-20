#include "dom_lifecycle.hpp"

#include "dom_element.hpp"
#include "dom_node.hpp"
#include "../../lambda-data.hpp"
#include "../../../lib/arena.h"
#include "../../../lib/log.h"

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
    size_t primary_size;
    Element* backing_source;
    uint32_t pins[DOM_NODE_PIN_REASON_COUNT];
    struct DomNodeRecord* bucket_next;
    struct DomNodeRecord* all_next;
} DomNodeRecord;

typedef struct DomNodeRegistry {
    DomNodeRecord** buckets;
    size_t bucket_count;
    size_t record_count;
    DomNodeRecord* all_records;
    DomLifecycleStats stats;
} DomNodeRegistry;

// DOM-only unit targets do not link the Radiant view teardown implementation.
__attribute__((weak)) void view_tree_release_retired_subtree(ViewTree*, DomNode*) {}
__attribute__((weak)) void dom_range_refresh_lifecycle_pins(DomDocument*) {}
extern "C" __attribute__((weak)) void js_dom_expando_attachment_changed(
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
            if (record->id == id && record->primary_size == primary_size) return true;
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
                                DomNode* node, uint32_t destination_id) {
    if (!source || !destination || !node || !destination_id) return false;
    DomNodeRecord* source_record = dom_record_find(dom_registry(source), node);
    if (!source_record || source_record->state == DOM_NODE_RETIRED ||
        source_record->id != node->id) {
        log_error("DOM_LIFECYCLE_TRANSFER: source record is stale for node %p", (void*)node);
        return false;
    }
    if (!dom_node_registry_register_owned(destination, node,
            source_record->primary_arena, destination_id, source_record->type,
            source_record->primary_size, source_record->recyclable,
            source_record->backing_source)) {
        return false;
    }
    // The source registry may still own wrapper/expando pins that must unpin
    // normally, but its detached candidate must never recycle adopted storage.
    source_record->candidate = false;
    source_record->state = DOM_NODE_LIVE;
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
    if (!record || record->id != root->id || record->state == DOM_NODE_RETIRED) return;
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
    // Detached wrapper-owned state must stop being a native-tree GC root;
    // a live JS wrapper remains the sole owner until possible reattachment.
    js_dom_expando_attachment_changed(doc, root, false);
}

void dom_node_cancel_detached(DomDocument* doc, DomNode* root) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || !root) return;
    DomNodeRecord* record = dom_record_find(registry, root);
    if (!record || record->id != root->id || record->state == DOM_NODE_RETIRED) return;
    if (record && record->candidate && record->id == root->id) {
        record->candidate = false;
        record->state = DOM_NODE_LIVE;
        registry->stats.cancelled_candidates++;
    }
    // Reinsertion calls this before linking the parent; the lifecycle decision
    // itself confirms that the subtree is becoming attached again.
    js_dom_expando_attachment_changed(doc, root, true);
}

static bool dom_subtree_can_retire(DomDocument* doc, DomNode* node,
                                   DomNodeRecord** blocked) {
    DomNodeRegistry* registry = dom_registry(doc);
    DomNodeRecord* record = dom_record_find(registry, node);
    if (!record || record->state == DOM_NODE_RETIRED || record->id != node->id ||
        !record->recyclable || !record->primary_size) {
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

static size_t dom_retire_subtree(DomDocument* doc, DomNode* node) {
    DomNodeRegistry* registry = dom_registry(doc);
    size_t retired = 0;
    if (node->is_element()) {
        DomNode* child = node->as_element()->first_child;
        while (child) {
            DomNode* next = child->next_sibling;
            retired += dom_retire_subtree(doc, child);
            child = next;
        }
        dom_element_release_retired_storage(node->as_element());
    } else if (node->is_text()) {
        dom_text_release_retired_storage(doc, node->as_text());
    }

    DomNodeRecord* record = dom_record_find(registry, node);
    size_t primary_size = record->primary_size;
    record->candidate = false;
    record->state = DOM_NODE_RETIRED;
    registry->stats.retired_nodes++;
    registry->stats.retired_primary_bytes += primary_size;
    // The live generation disappears from the registry before its bytes enter
    // the generic recycler, so stale refs never inspect a repurposed header.
    memset(node, 0xdd, primary_size);
    arena_free(record->primary_arena, node, primary_size);
    return retired + 1u;
}

size_t dom_retire_sweep(DomDocument* doc) {
    DomNodeRegistry* registry = dom_registry(doc);
    if (!registry || !doc->node_arena) return 0;
    // Range endpoints mutate through many DOM-spec algorithms. Recomputing
    // their pins at the single sweep boundary makes the registry authoritative
    // without allowing a missed setter to expose a detached live endpoint.
    dom_range_refresh_lifecycle_pins(doc);
    size_t retired = 0;
    for (DomNodeRecord* record = registry->all_records; record;) {
        DomNodeRecord* next = record->all_next;
        if (record->candidate && record->state == DOM_NODE_DETACHED_CANDIDATE) {
            DomNode* root = record->address;
            if (!dom_node_ref_validate(doc, {root, record->id})) {
                record->candidate = false;
            } else if (root->parent) {
                record->candidate = false;
                record->state = DOM_NODE_LIVE;
                registry->stats.rejected_attached++;
            } else {
                DomNodeRecord* blocked = nullptr;
                if (dom_subtree_can_retire(doc, root, &blocked)) {
                    if (doc->view_tree) {
                        view_tree_release_retired_subtree(doc->view_tree, root);
                    }
                    retired += dom_retire_subtree(doc, root);
                } else {
                    registry->stats.rejected_pinned++;
                }
            }
        }
        record = next;
    }
    return retired;
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
    // Mutation records hold raw nodes across the reconcile pass; release all
    // pins before the single quiescent-point sweep.
    dom_retire_sweep(doc);
}
