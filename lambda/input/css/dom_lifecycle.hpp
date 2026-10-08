#ifndef DOM_LIFECYCLE_HPP
#define DOM_LIFECYCLE_HPP

#include <stddef.h>
#include <stdint.h>

struct DomDocument;
struct DomNode;
struct Element;
struct Item;
struct Pool;

typedef enum DomNodePinReason : uint8_t {
    DOM_NODE_PIN_WRAPPER = 0,
    DOM_NODE_PIN_RANGE,
    DOM_NODE_PIN_OBSERVER,
    DOM_NODE_PIN_EVENT_QUEUE,
    DOM_NODE_PIN_LIVE_COLLECTION,
    DOM_NODE_PIN_STATE,
    DOM_NODE_PIN_RECONCILE,
    DOM_NODE_PIN_EXTERNAL,
    DOM_NODE_PIN_REASON_COUNT,
} DomNodePinReason;

typedef enum DomNodeLifeState : uint8_t {
    DOM_NODE_LIVE = 1,
    DOM_NODE_DETACHED_CANDIDATE,
    DOM_NODE_RETIRED,
} DomNodeLifeState;

typedef struct DomNodeRef {
    DomNode* address;
    uint32_t expected_id;
} DomNodeRef;

typedef struct DomLifecycleStats {
    uint64_t registered_nodes;
    uint64_t reused_addresses;
    uint64_t scheduled_candidates;
    uint64_t cancelled_candidates;
    uint64_t retired_nodes;
    uint64_t rejected_pinned;
    uint64_t rejected_attached;
    uint64_t stale_ref_rejections;
    uint64_t retirement_edge_visits;
    uint64_t recycled_nodes;
    size_t retired_primary_bytes;
    size_t pending_primary_bytes;
    // primary bytes of detached subtrees whose only pins are script wrappers
    size_t wrapper_stranded_bytes;
} DomLifecycleStats;

bool dom_lifecycle_init(DomDocument* doc);
void dom_lifecycle_destroy(DomDocument* doc);
void dom_lifecycle_release_backing_roots(DomDocument* doc);
bool dom_node_registry_register(DomDocument* doc, DomNode* node,
                                size_t primary_size, bool recyclable);
bool dom_node_registry_transfer(DomDocument* source, DomDocument* destination,
                                DomNode* node, uint32_t* destination_id);
void dom_node_registry_set_backing_source(DomDocument* doc, DomNode* node,
                                          Element* backing_source);
void dom_node_registry_set_backing_value(DomDocument* doc, DomNode* node,
                                         Item backing_value);
Element* dom_node_registry_backing_source(DomDocument* doc, DomNode* node);
Pool* dom_node_registry_owned_string_pool(DomDocument* doc, DomNode* node);
void dom_node_registry_set_owned_string_pool(DomDocument* doc, DomNode* node, Pool* pool);
void dom_node_registry_refresh_backing(DomDocument* doc, DomNode* node);
DomNodeRef dom_node_ref(DomNode* node);
DomNode* dom_node_ref_validate(DomDocument* doc, DomNodeRef ref);
bool dom_node_registry_owns(DomDocument* doc, DomNode* node);
bool dom_node_pin(DomDocument* doc, DomNodeRef ref, DomNodePinReason reason);
bool dom_node_unpin(DomDocument* doc, DomNodeRef ref, DomNodePinReason reason);
uint32_t dom_node_pin_count(DomDocument* doc, DomNodeRef ref,
                            DomNodePinReason reason);
void dom_node_clear_reason_pins(DomDocument* doc, DomNodePinReason reason);
void dom_node_schedule_detached(DomDocument* doc, DomNode* root);
void dom_node_cancel_detached(DomDocument* doc, DomNode* root);
void dom_js_mutation_records_reset(DomDocument* doc);
bool dom_js_mutation_records_reserve(DomDocument* doc, int count);
void dom_js_mutation_records_destroy(DomDocument* doc);
size_t dom_retire_sweep(DomDocument* doc);
// Hosted windows defer sweeps until the event loop reaches a quiescent point.
bool dom_retire_set_deferred(bool enabled);
bool dom_retire_idle(uint64_t budget_us);
// Detached subtrees held only by script wrappers are released by a collection,
// which GC-heap allocation alone may never trigger. The host collects at an
// idle point once `threshold` more such bytes are stranded than after its last
// collection, then reports that collection so pacing restarts from there.
bool dom_retire_wrapper_collection_due(DomDocument* doc, size_t threshold);
void dom_retire_wrapper_collection_done(DomDocument* doc);
void dom_retire_begin_destroy(DomDocument* doc);
void dom_lifecycle_release_unattached_form_props(DomDocument* doc);
void dom_lifecycle_release_all_form_props(DomDocument* doc);
void dom_lifecycle_get_stats(DomDocument* doc, DomLifecycleStats* out);

#endif
