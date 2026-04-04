/**
 * transpile_shared.cpp — Shared transpiler utilities
 *
 * Functions used by both C2MIR transpiler (transpile.cpp) and MIR Direct
 * transpiler (transpile-mir.cpp) + module_registry. Extracted to allow
 * excluding transpile.cpp from core lambda build.
 */

#include "transpiler.hpp"
#include "../lib/log.h"

extern Type TYPE_ANY, TYPE_INT;

bool has_typed_params(AstFuncNode* fn_node) {
    AstNamedNode *param = fn_node->param;
    while (param) {
        TypeParam* pt = (TypeParam*)param->type;
        if (pt) {
            TypeId tid = pt->type_id;
            if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 ||
                tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL ||
                tid == LMD_TYPE_STRING || tid == LMD_TYPE_BINARY ||
                tid == LMD_TYPE_SYMBOL || tid == LMD_TYPE_DECIMAL ||
                tid == LMD_TYPE_DTIME ||
                tid == LMD_TYPE_MAP || tid == LMD_TYPE_OBJECT ||
                tid == LMD_TYPE_ELEMENT) {
                return true;
            }
            // typed array parameters (int[], float[], etc.)
            if (tid == LMD_TYPE_TYPE && pt->kind == TYPE_KIND_UNARY) {
                return true;
            }
        }
        param = (AstNamedNode*)param->next;
    }
    return false;
}

bool needs_fn_call_wrapper(AstFuncNode* fn_node) {
    if (fn_node->captures) return false;  // closures already use Item ABI
    TypeFunc* fn_type = (TypeFunc*)fn_node->type;

    // Functions with typed params need param unboxing wrapper
    if (has_typed_params(fn_node)) return true;

    // can_raise non-closure/non-method functions return RetItem from the main function,
    // which doesn't match fn_call*'s Item ABI — need a _b wrapper
    if (fn_type->can_raise) return true;

    // Functions with ALL untyped params: body uses Item-level ops → effectively returns Item
    // No wrapper needed (fn_call* can use the function directly)
    if (fn_node->param) return false;

    // Functions with NO params: body may return raw native values → needs wrapper
    {
        Type *ret_type = fn_type->returned;
        if (!ret_type && fn_node->body) ret_type = fn_node->body->type;
        if (!ret_type) ret_type = &TYPE_ANY;
        TypeId ret_tid = ret_type->type_id;
        if (ret_tid == LMD_TYPE_INT || ret_tid == LMD_TYPE_INT64 ||
            ret_tid == LMD_TYPE_FLOAT || ret_tid == LMD_TYPE_BOOL ||
            ret_tid == LMD_TYPE_STRING || ret_tid == LMD_TYPE_BINARY ||
            ret_tid == LMD_TYPE_SYMBOL || ret_tid == LMD_TYPE_DECIMAL ||
            ret_tid == LMD_TYPE_DTIME) {
            return true;
        }
    }

    return false;
}

void write_fn_name_ex(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import, const char* suffix) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    strbuf_append_char(strbuf, '_');
    if (fn_node->name && fn_node->name->chars) {
        strbuf_append_str_n(strbuf, fn_node->name->chars, fn_node->name->len);
    } else {
        strbuf_append_char(strbuf, 'f');
    }
    // add suffix before offset for clarity: _square_b_15 vs _square_15
    if (suffix) {
        strbuf_append_str(strbuf, suffix);
    }
    // _ + char offset ensures the fn name is unique across the script
    strbuf_append_char(strbuf, '_');
    strbuf_append_int(strbuf, ts_node_start_byte(fn_node->node));
}

void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import) {
    write_fn_name_ex(strbuf, fn_node, import, NULL);
}

void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    // user var name starts with '_'
    strbuf_append_char(strbuf, '_');
    strbuf_append_str_n(strbuf, asn_node->name->chars, asn_node->name->len);
}
