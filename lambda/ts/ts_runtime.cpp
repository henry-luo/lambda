// ts_runtime.cpp — TypeScript runtime helper functions
//
// The direct parser erases TypeScript-only runtime forms. The remaining helper
// formats the type() intrinsic's Type* result for the JavaScript runtime.

#include "ts_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../js/js_runtime.h"
#include "../js/js_function.hpp"
#include "../../lib/log.h"
#include <cstring>

// shorthand for strbuf_append_str_n
#define sbuf_add(buf, s, n) strbuf_append_str_n(buf, s, n)

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

    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM: { // numeric backing is an implementation detail of TS array values
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
        JsFunction* js_func = (JsFunction*)value.function;
        // JsFunction and core Function deliberately share only their tagged
        // prefix. Reading JsFunction::func_ptr as Function::fn_type made the
        // TS introspector dereference executable code after Tune4 canonicalized
        // JS callable layout (D6.2.2v2).
        Function* func = js_func && js_func->layout_magic != JS_FUNCTION_LAYOUT_MAGIC
            ? value.function : NULL;
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
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM: // packed numeric arrays have the same TS runtime surface
        // numeric arrays share the JavaScript/TypeScript array surface; the
        // packed numeric carrier is an internal Lambda representation.
        ts_name = "array";
        break;
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT:    ts_name = "object"; break;
    default:                 ts_name = get_type_name(type); break;
    }

    return (Item){.item = s2it(heap_create_name(ts_name))};
}
