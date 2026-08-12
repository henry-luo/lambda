// js_props.h — canonical ECMAScript property kernels.
// Shape tombstones, accessors, descriptors, and D8.4.3 error propagation
// converge here rather than through parallel property mechanisms.

#ifndef LAMBDA_JS_PROPS_H
#define LAMBDA_JS_PROPS_H

#include "../lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// ES §7.1.19 ToPropertyKey. Non-symbol keys become canonical strings.
Item js_to_property_key(Item key);

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
