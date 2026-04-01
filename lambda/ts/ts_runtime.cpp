// ts_runtime.cpp — TypeScript runtime helper functions
//
// These functions are callable from MIR JIT code and implement TS-specific
// runtime semantics: typeof (TS-aware), structural shape checks, type assertions.

#include "ts_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../js/js_runtime.h"
#include "../../lib/log.h"
#include <cstring>

// shorthand for strbuf_append_str_n
#define sbuf_add(buf, s, n) strbuf_append_str_n(buf, s, n)

// ============================================================================
// ts_typeof — TS-aware typeof returning the TS type name
// ============================================================================

extern "C" Item ts_typeof(Item value) {
    // delegates to JS typeof — TS typeof has same runtime semantics
    return js_typeof(value);
}

// ============================================================================
// ts_box_type — box a Type* pointer into an Item (TypeType container)
// ============================================================================

extern "C" Item ts_box_type(Type* type) {
    if (!type) return (Item){.item = ITEM_NULL};
    TypeType* tt = (TypeType*)heap_calloc(sizeof(TypeType), LMD_TYPE_TYPE);
    tt->type_id = LMD_TYPE_TYPE;
    tt->type = type;
    Item result;
    result.container = (Container*)tt;
    return result;
}

// ============================================================================
// ts_check_shape — structural compatibility check
//
// Checks whether an object (Item of type MAP/ELEMENT) has all required fields
// specified in a TypeMap type. Returns the object if compatible, or
// ItemError with a log message if not.
// ============================================================================

extern "C" Item ts_check_shape(Item obj, Item type_item) {
    TypeId obj_type = get_type_id(obj);
    if (obj_type != LMD_TYPE_MAP && obj_type != LMD_TYPE_ELEMENT &&
        obj_type != LMD_TYPE_OBJECT) {
        log_error("ts shape check: expected object, got type %s",
                  get_type_name(obj_type));
        return (Item){.item = ITEM_ERROR};
    }

    // extract the Type* from the boxed TypeType item
    TypeId ti_type = get_type_id(type_item);
    if (ti_type != LMD_TYPE_TYPE) {
        // no type info available — pass through
        return obj;
    }
    TypeType* tt = (TypeType*)type_item.container;
    if (!tt || !tt->type) return obj;

    Type* target = tt->type;
    if (target->type_id != LMD_TYPE_MAP) {
        // not a TypeMap shape — pass through
        return obj;
    }

    TypeMap* shape_type = (TypeMap*)target;
    if (!shape_type->shape) return obj;  // empty shape — all objects match

    // iterate required fields in the shape and check each exists in the object
    // Object has the same memory layout as Map, so cast to Map* for field checks
    Map* map = (obj_type == LMD_TYPE_MAP || obj_type == LMD_TYPE_OBJECT)
                   ? (Map*)obj.container : NULL;
    Element* elmt = (obj_type == LMD_TYPE_ELEMENT) ? obj.element : NULL;

    for (ShapeEntry* field = shape_type->shape; field; field = field->next) {
        if (!field->name) continue;
        const char* fname = field->name->str;
        if (!fname) continue;

        bool found = false;
        if (map) {
            found = map->has_field(fname);
        } else if (elmt) {
            found = elmt->has_attr(fname);
        }

        if (!found) {
            log_error("ts shape check: missing required field '%s'", fname);
            return (Item){.item = ITEM_ERROR};
        }
    }

    return obj;
}

// ============================================================================
// ts_assert_type — runtime type assertion for `as` expressions
//
// Verifies the value's runtime type is compatible with the target type.
// Returns the value on success, logs a warning + returns value on mismatch
// (soft assertion — doesn't abort, matches TS semantics).
// ============================================================================

// check if a runtime type is compatible with an expected TypeId
static bool ts_type_compatible(TypeId actual, TypeId expected) {
    if (expected == LMD_TYPE_ANY) return true;
    if (actual == expected) return true;
    // number compatibility: int and float are both "number" in TS
    if (expected == LMD_TYPE_FLOAT &&
        (actual == LMD_TYPE_INT || actual == LMD_TYPE_INT64)) return true;
    if (expected == LMD_TYPE_INT &&
        (actual == LMD_TYPE_FLOAT || actual == LMD_TYPE_INT64)) return true;
    // object compatibility: map, element, object are all "object"
    if (expected == LMD_TYPE_MAP &&
        (actual == LMD_TYPE_ELEMENT || actual == LMD_TYPE_OBJECT)) return true;
    return false;
}

extern "C" Item ts_assert_type(Item value, Item type_item) {
    TypeId ti_type = get_type_id(type_item);
    if (ti_type != LMD_TYPE_TYPE) {
        // no type info — pass through
        return value;
    }

    TypeType* tt = (TypeType*)type_item.container;
    if (!tt || !tt->type) return value;

    TypeId val_type = get_type_id(value);
    TypeId target_id = tt->type->type_id;

    // for TypeMap targets, delegate to structural check
    if (target_id == LMD_TYPE_MAP) {
        return ts_check_shape(value, type_item);
    }

    if (!ts_type_compatible(val_type, target_id)) {
        log_debug("ts assert_type: value type '%s' does not match target type '%s'",
                  get_type_name(val_type), get_type_name(target_id));
    }
    return value;
}

// ============================================================================
// ts_type_info — return full TS type information as a string
//
// Returns a human-readable type string that includes TS specifics:
//   - union types: "number | string"
//   - function types: "(number, string) => boolean"
//   - interface shapes: "{ x: number; y: number }"
//   - primitive types: "number", "string", "boolean", etc.
// ============================================================================

// forward declaration
static void ts_format_type(Type* type, StrBuf* buf);

static void ts_format_type(Type* type, StrBuf* buf) {
    if (!type) {
        sbuf_add(buf, "any", 3);
        return;
    }

    switch (type->type_id) {
    case LMD_TYPE_NULL:      sbuf_add(buf, "null", 4); break;
    case LMD_TYPE_BOOL:      sbuf_add(buf, "boolean", 7); break;
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:     sbuf_add(buf, "number", 6); break;
    case LMD_TYPE_STRING:    sbuf_add(buf, "string", 6); break;
    case LMD_TYPE_SYMBOL:    sbuf_add(buf, "symbol", 6); break;
    case LMD_TYPE_UNDEFINED: sbuf_add(buf, "undefined", 9); break;
    case LMD_TYPE_ANY:       sbuf_add(buf, "any", 3); break;
    case LMD_TYPE_ERROR:     sbuf_add(buf, "never", 5); break;

    case LMD_TYPE_ARRAY: {
        TypeArray* arr = (TypeArray*)type;
        if (arr && arr->nested) {
            ts_format_type(arr->nested, buf);
            sbuf_add(buf, "[]", 2);
        } else {
            sbuf_add(buf, "any[]", 5);
        }
        break;
    }

    case LMD_TYPE_MAP: {
        TypeMap* tm = (TypeMap*)type;
        if (tm->struct_name) {
            sbuf_add(buf, tm->struct_name, strlen(tm->struct_name));
        } else if (tm->shape) {
            sbuf_add(buf, "{ ", 2);
            bool first = true;
            for (ShapeEntry* f = tm->shape; f; f = f->next) {
                if (!first) sbuf_add(buf, "; ", 2);
                first = false;
                if (f->name && f->name->str) {
                    sbuf_add(buf, f->name->str, f->name->length);
                    sbuf_add(buf, ": ", 2);
                    ts_format_type(f->type, buf);
                }
            }
            sbuf_add(buf, " }", 2);
        } else {
            sbuf_add(buf, "object", 6);
        }
        break;
    }

    case LMD_TYPE_FUNC: {
        TypeFunc* tf = (TypeFunc*)type;
        sbuf_add(buf, "(", 1);
        TypeParam* p = tf->param;
        bool first = true;
        while (p) {
            if (!first) sbuf_add(buf, ", ", 2);
            first = false;
            // use full_type for complex types, otherwise format from base TypeId
            if (p->full_type) {
                ts_format_type(p->full_type, buf);
            } else {
                ts_format_type((Type*)p, buf);
            }
            p = p->next;
        }
        sbuf_add(buf, ") => ", 5);
        ts_format_type(tf->returned, buf);
        break;
    }

    default: {
        // for union/intersection types (TypeBinary)
        TypeBinary* tb = (TypeBinary*)type;
        // check if this looks like a TypeBinary (has valid left/right pointers)
        if (tb->left && tb->right && tb->op == OPERATOR_UNION) {
            ts_format_type(tb->left, buf);
            sbuf_add(buf, " | ", 3);
            ts_format_type(tb->right, buf);
        } else {
            const char* name = get_type_name(type->type_id);
            if (name) {
                sbuf_add(buf, name, strlen(name));
            } else {
                sbuf_add(buf, "unknown", 7);
            }
        }
        break;
    }
    }
}

extern "C" Item ts_type_info(Item value) {
    TypeId type = get_type_id(value);

    // for TypeType items, format the inner type
    if (type == LMD_TYPE_TYPE) {
        TypeType* tt = (TypeType*)value.container;
        if (tt && tt->type) {
            StrBuf* buf = strbuf_new_cap(64);
            ts_format_type(tt->type, buf);
            Item result = (Item){.item = s2it(heap_create_name(buf->str))};
            strbuf_free(buf);
            return result;
        }
    }

    // for function items with attached TypeFunc
    if (type == LMD_TYPE_FUNC) {
        Function* func = value.function;
        if (func && func->fn_type) {
            TypeFunc* tf = (TypeFunc*)func->fn_type;
            if (tf->type_id == LMD_TYPE_FUNC) {
                StrBuf* buf = strbuf_new_cap(64);
                ts_format_type((Type*)tf, buf);
                Item result = (Item){.item = s2it(heap_create_name(buf->str))};
                strbuf_free(buf);
                return result;
            }
        }
    }

    // fallback: return TS-style type name
    const char* ts_name;
    switch (type) {
    case LMD_TYPE_NULL:      ts_name = "null"; break;
    case LMD_TYPE_UNDEFINED: ts_name = "undefined"; break;
    case LMD_TYPE_BOOL:      ts_name = "boolean"; break;
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:     ts_name = "number"; break;
    case LMD_TYPE_STRING:    ts_name = "string"; break;
    case LMD_TYPE_SYMBOL:    ts_name = "symbol"; break;
    case LMD_TYPE_FUNC:      ts_name = "function"; break;
    case LMD_TYPE_ARRAY:     ts_name = "array"; break;
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT:    ts_name = "object"; break;
    default:                 ts_name = get_type_name(type); break;
    }

    return (Item){.item = s2it(heap_create_name(ts_name))};
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
