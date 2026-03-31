// ts_runtime.cpp — TypeScript runtime helper functions
//
// These functions are callable from MIR JIT code and implement TS-specific
// runtime semantics: typeof (TS-aware), structural shape checks, type assertions.

#include "ts_runtime.h"
#include "../lambda-data.hpp"
#include "../js/js_runtime.h"
#include "../../lib/log.h"
#include <cstring>

// ============================================================================
// ts_typeof — TS-aware typeof returning the TS type name
// ============================================================================

extern "C" Item ts_typeof(Item value) {
    // delegates to JS typeof for now — TS typeof has same runtime semantics
    return js_typeof(value);
}

// ============================================================================
// ts_check_shape — structural compatibility check
//
// Checks whether an object (Item of type MAP) has all required fields
// specified in a TypeMap type. Returns the object if compatible, or
// ItemError if not.
// ============================================================================

extern "C" Item ts_check_shape(Item obj, Item type_item) {
    TypeId obj_type = get_type_id(obj);
    if (obj_type != LMD_TYPE_MAP && obj_type != LMD_TYPE_ELEMENT) {
        log_error("ts shape check: expected object, got type %d", obj_type);
        return (Item){.item = ITEM_ERROR};
    }

    // type_item should be a TypeType wrapping a TypeMap
    TypeId ti_type = get_type_id(type_item);
    if (ti_type != LMD_TYPE_TYPE) {
        // no type info available — pass through
        return obj;
    }

    // for now, pass through — full structural checking requires
    // runtime access to the Type* from the Item, which needs the
    // TypeType → TypeMap chain. This will be implemented when
    // the TypeType boxing infrastructure is wired up.
    return obj;
}

// ============================================================================
// ts_assert_type — runtime type assertion for `as` expressions in debug mode
// ============================================================================

extern "C" Item ts_assert_type(Item value, Item type_item) {
    // in debug mode, verify the value's runtime type is compatible
    // with the target type. For now, log a warning on mismatch.
    TypeId val_type = get_type_id(value);
    TypeId target_type = get_type_id(type_item);

    if (target_type == LMD_TYPE_TYPE) {
        // type_item is a boxed Type* — extract the TypeId from it
        // for now, pass through since we need TypeType boxing
        return value;
    }

    if (val_type != target_type && target_type != LMD_TYPE_ANY) {
        log_debug("ts assert_type: value type %d does not match target type %d",
            val_type, target_type);
    }
    return value;
}

// ============================================================================
// Enum support
// ============================================================================

extern "C" Item ts_enum_create(int member_count) {
    // create a new map object to represent the enum
    extern Item js_new_object();
    return js_new_object();
}

extern "C" Item ts_enum_add_member(Item enum_obj, Item name, Item value) {
    // add forward mapping (name → value) and reverse mapping (value → name)
    extern Item js_property_set(Item, Item, Item);
    js_property_set(enum_obj, name, value);
    // reverse mapping for numeric enums
    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_INT || value_type == LMD_TYPE_FLOAT) {
        js_property_set(enum_obj, value, name);
    }
    return enum_obj;
}

extern "C" Item ts_enum_freeze(Item enum_obj) {
    // mark as frozen (immutable) — for now just return as-is
    // full freeze support would set a flag on the Container
    return enum_obj;
}
