// js_props.h — canonical ECMAScript property kernels.
// Shape tombstones, accessors, descriptors, and D8.4.3 error propagation
// converge here rather than through parallel property mechanisms.

#ifndef LAMBDA_JS_PROPS_H
#define LAMBDA_JS_PROPS_H

#include "../lambda-data.hpp"

// Tune5 §4.1: a property lookup carries one scalar identity.  The observable
// key Item is deliberately kept separate so ordinary paths do not need to
// allocate or intern a string merely to perform a lookup.
typedef uint64_t JsPropertyLane;

#define JS_PROPERTY_LANE_INDEX_BIT ((JsPropertyLane)1u << 32)
#define JS_PROPERTY_LANE_PAYLOAD_MASK ((JsPropertyLane)0xffffffffu)
#define JS_PROPERTY_INDEX_MAX UINT32_C(0xfffffffe)

static inline JsPropertyLane js_property_lane_from_name_id(NameId name_id) {
    return (JsPropertyLane)name_id;
}

static inline JsPropertyLane js_property_lane_from_index(uint32_t index) {
    return JS_PROPERTY_LANE_INDEX_BIT | (JsPropertyLane)index;
}

static inline bool js_property_lane_is_index(JsPropertyLane lane) {
    return (lane & JS_PROPERTY_LANE_INDEX_BIT) != 0;
}

static inline uint32_t js_property_lane_payload(JsPropertyLane lane) {
    return (uint32_t)(lane & JS_PROPERTY_LANE_PAYLOAD_MASK);
}

static inline bool js_property_lane_is_valid(JsPropertyLane lane) {
    uint32_t payload = js_property_lane_payload(lane);
    return js_property_lane_is_index(lane)
        ? payload <= JS_PROPERTY_INDEX_MAX
        : payload != NAME_ID_NONE;
}

// ItemNull is not a result of ToPropertyKey (which returns a String or
// Symbol), so it is the empty observable-materialization marker.
static inline Item js_property_observable_key_empty(void) {
    return ItemNull;
}

#ifdef __cplusplus
extern "C" {
#endif

// ES §7.1.19 ToPropertyKey. Non-symbol keys become canonical strings.
Item js_to_property_key(Item key);

// Tune5 §2.4: the ordinary array-index classifier is shared by every
// ordinary property consumer.  TypedArray CanonicalNumericIndexString stays
// on its separate exotic classifier.
bool js_property_name_to_array_index(const char* name, int name_len,
                                     uint32_t* out_index);
bool js_property_key_to_array_index(Item key, uint32_t* out_index);
bool js_property_lane_from_key(Item key, JsPropertyLane* out_lane);
// `key` is already the single ToPropertyKey result; this helper only classifies
// it and therefore cannot perform a second coercion or allocation.
JsPropertyLane js_property_lane_for_canonical_key(Item key);
Item js_property_key_from_lane(JsPropertyLane lane);
// Numeric array algorithms carry an index Item through the semantic kernels;
// only descriptor/shape code that must address the companion map asks for the
// canonical pooled spelling.
Item js_property_index_key(int64_t index);
String* js_property_index_name(int64_t index);
const char* js_property_index_chars(int64_t index, int* out_len);

// Tune5 §4.3: final semantic operation ABI.  The legacy runtime entry points
// may delegate to these shells while migration is staged, but new semantic
// callers use only these eight operations.
Item js_get(Item target, JsPropertyLane lane, Item observable_key,
            Item receiver);
Item js_set(Item target, JsPropertyLane lane, Item observable_key,
            Item value, Item receiver);
Item js_delete(Item target, JsPropertyLane lane, Item observable_key);
Item js_has_property(Item target, JsPropertyLane lane, Item observable_key);

// Tune5 P3: one operation-tagged seam for all non-ordinary receivers.  The
// adapter reports handled/not-handled through its return value and carries the
// operation completion in out_result, so false, ordinary fallback, and ERROR
// remain distinct.
typedef enum JsPropertyOperation {
    JS_EXOTIC_GET = 0,
    JS_EXOTIC_SET = 1,
    JS_EXOTIC_DEFINE_OWN = 2,
    JS_EXOTIC_DELETE = 3,
    JS_EXOTIC_HAS_PROPERTY = 4,
    JS_EXOTIC_HAS_OWN = 5,
    JS_EXOTIC_GET_OWN_PROPERTY_DESCRIPTOR = 6,
    JS_EXOTIC_OWN_KEYS = 7,
} JsPropertyOperation;

bool js_dispatch_property_op(JsPropertyOperation operation,
                                Item target, JsPropertyLane lane,
                                Item observable_key, Item receiver,
                                Item descriptor, Item value,
                                bool bypass_accessor_dispatch,
                                Item* out_result);

int64_t js_key_is_symbol_c(Item key);

// Outcome of own-property lookup with accessor dispatch.
typedef enum {
    JS_OWN_NOT_FOUND = 0,  // no own slot; caller should walk prototype chain
    JS_OWN_DELETED   = 1,  // own slot/shape is tombstoned; caller should walk
                           // prototype chain but record the deletion
                           // (Object.prototype top-of-chain semantics)
    JS_OWN_READY     = 2,  // *out_value is the resolved [[Get]] result
                           // (data value, getter return, setter-only undefined,
                           //  or a thrown private-field error). Caller MUST
                           // return *out_value verbatim and NOT walk further.
} JsOwnGetStatus;

// Lowest-level status for Map, Function, and Array companion storage.
typedef enum {
    JS_SHAPE_SLOT_ABSENT   = 0,
    JS_SHAPE_SLOT_DELETED  = 1,
    JS_SHAPE_SLOT_DATA     = 2,
    JS_SHAPE_SLOT_ACCESSOR = 3,
} JsShapeSlotStatus;

JsShapeSlotStatus js_own_shape_slot_status(Item object,
                                            const char* name,
                                            int name_len,
                                            Item* out_slot,
                                            ShapeEntry** out_se);

JsShapeSlotStatus js_own_shape_slot_status_name_id(Item object, NameId name_id,
                                                    Item* out_slot,
                                                    ShapeEntry** out_se);

// Resolve a STRING/SYMBOL property key through the NameId-first matcher. An
// ordinary id-less Input key remains on the byte-confirmed seam.
JsShapeSlotStatus js_own_shape_slot_status_key(Item object, Item key,
                                               Item* out_slot,
                                               ShapeEntry** out_se);

// Hot ordinary-Get variant. The storage walk already knows whether the slot
// came from packed map data, so it reports scalar-home provenance without a
// second object/map lookup in the Get kernel.
JsShapeSlotStatus js_own_shape_slot_status_key_ex(Item object, Item key,
                                                  Item* out_slot,
                                                  ShapeEntry** out_se,
                                                  bool* out_borrowed);

// Return the ordinary property-storage map for Map, Array, or Function values.
// Callers use this only after validating that the value has ordinary storage.
Map* js_obj_underlying_map(Item object);

// Runtime-only storage key for ordinary [[Prototype]] on callable carriers.
extern const char JS_INTERNAL_PROTO_KEY[];
extern const int JS_INTERNAL_PROTO_KEY_LEN;

// Mark a shape entry deleted, optionally materializing a shadowable slot.
bool js_shape_mark_deleted_own(Item object, const char* name, int name_len,
                               bool create_if_missing);

// Own-only [[Get]]. `key` must be canonical; JS_OWN_READY sets out_value.
JsOwnGetStatus js_ordinary_get_own(Item object, Item key, Item Receiver,
                                    Item* out_value);

// Variant used by the hot Get kernel.  `out_borrowed` is true only when the
// returned data value is read from movable shape storage and must be re-homed
// before it crosses the property boundary; extension-map and getter results
// already own their scalar homes.
JsOwnGetStatus js_ordinary_get_own_ex(Item object, Item key, Item Receiver,
                                      Item* out_value, bool* out_borrowed);

// Outcome of inherited accessor-setter dispatch.
typedef enum {
    JS_SET_NOT_FOUND   = 0,  // no IS_ACCESSOR pair found on the chain; caller
                             // should proceed with the normal data write path
    JS_SET_DISPATCHED  = 1,  // pair->setter was called with Receiver as `this`;
                             // caller MUST return value verbatim
    JS_SET_NO_SETTER   = 2,  // pair found but pair->setter == ItemNull;
                             // caller decides strict-throw vs sloppy no-op
    JS_SET_DISPATCH_ERROR = 3, // setter ran but returned an abrupt completion;
                               // error_lane carries its exact ERROR Item
} JsSetterDispatchStatus;

// D8.4.3: an accessor setter's abrupt completion remains in the returned
// value; property dispatch must not stash it in ambient thread-local state.
typedef struct JsSetterDispatchResult {
    JsSetterDispatchStatus status;
    Item error_lane;
} JsSetterDispatchResult;

// Dispatch an inherited accessor setter with Receiver (or object) as this.
JsSetterDispatchResult js_ordinary_set_via_accessor(Item object,
                                                     const char* name,
                                                     int name_len,
                                                     Item value,
                                                     Item Receiver);

JsSetterDispatchResult js_ordinary_set_via_accessor_name_id(Item object,
                                                             NameId name_id,
                                                             Item value,
                                                             Item Receiver);

// Read-only own-slot descriptor classification; never invokes a getter.
typedef enum {
    JS_DESC_NONE     = 0,  // no own slot under this name
    JS_DESC_DELETED  = 1,  // own slot held the deleted sentinel
    JS_DESC_DATA     = 2,  // own data slot; *out_value set, *out_pair NULL
    JS_DESC_ACCESSOR = 3,  // own IS_ACCESSOR slot; *out_pair set, *out_value
                           // unspecified. Either getter or setter (or both)
                           // may be ItemNull — caller checks.
} JsOwnDescKind;

// Inspect an own descriptor without reading it. Either output may be NULL.
JsOwnDescKind js_ordinary_get_own_descriptor(Item object,
                                              const char* name,
                                              int name_len,
                                              JsAccessorPair** out_pair,
                                              Item* out_value);

// Own-only HasProperty for ordinary and companion-map storage.
bool js_ordinary_has_own(Item object, const char* name, int name_len);

// Tri-state own-slot probe. Deleted must not fall through to virtual builtins.
typedef enum {
    JS_HAS_ABSENT  = 0,
    JS_HAS_PRESENT = 1,
    JS_HAS_DELETED = 2,
} JsOwnSlotStatus;
JsOwnSlotStatus js_ordinary_own_status(Item object, const char* name, int name_len);

// OrdinaryHasProperty over the prototype chain; callers handle exotic traps.
bool js_ordinary_has_property(Item object, const char* name, int name_len);

// Ordinary Map delete. The full ABI handles arrays, functions, and proxies.
bool js_ordinary_delete(Item object, const char* name, int name_len);

// Result of shape-iteration value resolution.
typedef enum {
    JS_RESOLVE_DELETED = 0,  // slot held the deleted sentinel; caller should
                             // skip this entry
    JS_RESOLVE_VALUE   = 1,  // *out_value populated (data slot or getter
                             // return); caller proceeds normally
    JS_RESOLVE_THREW   = 2,  // accessor getter threw; caller MUST propagate
                             // the returned Item lane and bail
} JsResolveFieldStatus;

// Resolve a shaped slot for spread/assign, including accessor dispatch.
JsResolveFieldStatus js_ordinary_resolve_shape_value(ShapeEntry* e,
                                                      Map* m,
                                                      Item receiver,
                                                      Item* out_value);

// ES §6.2.5 descriptor represented by presence flags and Item fields.

#define JS_PD_HAS_VALUE        0x01u  // descriptor carries [[Value]]
#define JS_PD_HAS_GET          0x02u  // descriptor carries [[Get]]
#define JS_PD_HAS_SET          0x04u  // descriptor carries [[Set]]
#define JS_PD_HAS_WRITABLE     0x08u  // descriptor carries [[Writable]]
#define JS_PD_HAS_ENUMERABLE   0x10u  // descriptor carries [[Enumerable]]
#define JS_PD_HAS_CONFIGURABLE 0x20u  // descriptor carries [[Configurable]]

#define JS_PD_WRITABLE         0x40u  // [[Writable]] bit (when HAS_WRITABLE set)
#define JS_PD_ENUMERABLE       0x80u  // [[Enumerable]] bit (when HAS_ENUMERABLE)
// [[Configurable]] uses bit in `flags2` to keep this a single byte. Use
// helper functions below.
#define JS_PD_CONFIGURABLE_VALUE (1u << 8)

typedef struct JsPropertyDescriptor {
    uint8_t flags;     // JS_PD_HAS_* + JS_PD_WRITABLE / JS_PD_ENUMERABLE
    uint8_t flags2;    // bit0: configurable; bits1-7 reserved
    uint8_t reserved[6];
    Item value;        // [[Value]] — valid iff JS_PD_HAS_VALUE
    Item getter;       // [[Get]]   — valid iff JS_PD_HAS_GET
    Item setter;       // [[Set]]   — valid iff JS_PD_HAS_SET
} JsPropertyDescriptor;

static inline bool js_pd_is_accessor(const JsPropertyDescriptor* d) {
    return (d->flags & (JS_PD_HAS_GET | JS_PD_HAS_SET)) != 0;
}
static inline bool js_pd_is_data(const JsPropertyDescriptor* d) {
    return (d->flags & (JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE)) != 0;
}
static inline bool js_pd_is_configurable(const JsPropertyDescriptor* d) {
    return (d->flags2 & 0x01u) != 0;
}
static inline void js_pd_set_configurable(JsPropertyDescriptor* d, bool b) {
    if (b) d->flags2 |= 0x01u; else d->flags2 &= (uint8_t)~0x01u;
}

// Synthesize an own descriptor for Map, Function, or Array companion storage.
bool js_get_own_property_descriptor(Item object,
                                     const char* name,
                                     int name_len,
                                     JsPropertyDescriptor* out);

bool js_get_own_property_descriptor_name_id(Item object, NameId name_id,
                                             JsPropertyDescriptor* out);

// ToPropertyDescriptor. Failures return the D8.4.3 merged ERROR Item.
Item js_descriptor_from_object(Item desc_obj, JsPropertyDescriptor* out);

// Apply a validated descriptor to ordinary storage. Callers supply whether
// the property is new and whether its previous form was an accessor.
Item js_define_own_property_from_descriptor(Item object,
                                             const char* name,
                                             int name_len,
                                             const JsPropertyDescriptor* pd,
                                             bool is_new_property,
                                             bool existing_accessor);

Item js_define_own_property_from_descriptor_name_id(Item object, NameId name_id,
                                                     const JsPropertyDescriptor* pd,
                                                     bool is_new_property,
                                                     bool existing_accessor);

#ifdef __cplusplus
}
#endif

#endif  // LAMBDA_JS_PROPS_H
