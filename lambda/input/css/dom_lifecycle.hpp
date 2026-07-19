#ifndef DOM_LIFECYCLE_HPP
#define DOM_LIFECYCLE_HPP

#include <stddef.h>
#include <stdint.h>

struct DomDocument;
struct DomNode;
struct Element;

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
    size_t retired_primary_bytes;
} DomLifecycleStats;

bool dom_lifecycle_init(DomDocument* doc);
void dom_lifecycle_destroy(DomDocument* doc);
bool dom_node_registry_register(DomDocument* doc, DomNode* node,
                                size_t primary_size, bool recyclable);
bool dom_node_registry_transfer(DomDocument* source, DomDocument* destination,
                                DomNode* node, uint32_t destination_id);
void dom_node_registry_set_backing_source(DomDocument* doc, DomNode* node,
                                          Element* backing_source);
Element* dom_node_registry_backing_source(DomDocument* doc, DomNode* node);
DomNodeRef dom_node_ref(DomNode* node);
DomNode* dom_node_ref_validate(DomDocument* doc, DomNodeRef ref);
bool dom_node_pin(DomDocument* doc, DomNodeRef ref, DomNodePinReason reason);
bool dom_node_unpin(DomDocument* doc, DomNodeRef ref, DomNodePinReason reason);
uint32_t dom_node_pin_count(DomDocument* doc, DomNodeRef ref,
                            DomNodePinReason reason);
void dom_node_clear_reason_pins(DomDocument* doc, DomNodePinReason reason);
void dom_node_schedule_detached(DomDocument* doc, DomNode* root);
void dom_node_cancel_detached(DomDocument* doc, DomNode* root);
void dom_js_mutation_records_reset(DomDocument* doc);
size_t dom_retire_sweep(DomDocument* doc);
void dom_lifecycle_get_stats(DomDocument* doc, DomLifecycleStats* out);

#endif
