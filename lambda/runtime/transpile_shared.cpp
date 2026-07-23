/**
 * transpile_shared.cpp — Shared transpiler utilities
 *
 * Functions used by both C2MIR transpiler (transpile.cpp) and MIR Direct
 * transpiler (transpile-mir.cpp) + module_registry. Extracted to allow
 * excluding transpile.cpp from core lambda build.
 */

#include "transpiler.hpp"
#include "../lib/log.h"
#include <string.h>

extern Type TYPE_ANY, TYPE_INT;

bool has_typed_params(AstFuncNode* fn_node) {
    AstNamedNode *param = fn_node->param;
    while (param) {
        TypeParam* pt = (TypeParam*)param->type;
        if (pt) {
            TypeId tid = pt->type_id;
            if (is_typed_wrapper_param_type_id(tid)) {
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
        if (is_fn_call_wrapper_return_type_id(ret_tid)) {
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
    if (fn_node->name) {
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

ShapeEntry* find_shape_field_by_name(TypeMap* map_type, const char* name, int name_len) {
    ShapeEntry* field = map_type->shape;
    while (field) {
        if (field->name && (int)field->name->length == name_len &&
            strncmp(field->name->str, name, name_len) == 0) {
            return field;
        }
        field = field->next;
    }
    return NULL;
}

bool has_fixed_shape(TypeMap* map_type) {
    if (!map_type->struct_name) return false;
    if (!map_type->shape || map_type->length == 0) return false;
    ShapeEntry* field = map_type->shape;
    while (field) {
        if (!field->name) return false;
        if (field->byte_offset % sizeof(void*) != 0) return false;
        field = field->next;
    }
    return true;
}

bool is_direct_access_type(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_BOOL: case LMD_TYPE_INT: case LMD_TYPE_INT64: case LMD_TYPE_UINT64:
    case LMD_TYPE_FLOAT: case LMD_TYPE_DTIME: case LMD_TYPE_DECIMAL:
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_BINARY:
    case LMD_TYPE_RANGE: case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT: case LMD_TYPE_TYPE: case LMD_TYPE_FUNC:
    case LMD_TYPE_PATH:
        return true;
    default:
        return false;
    }
}

TypeId resolve_field_type_id(ShapeEntry* field, bool unwrap_type_type) {
    Type* t = field->type;
    if (unwrap_type_type && t && t->type_id == LMD_TYPE_TYPE) {
        Type* inner = ((TypeType*)t)->type;
        if (inner) return inner->type_id;
    }
    return t ? t->type_id : LMD_TYPE_ANY;
}

static bool has_disqualifying_array_item(AstNode* item, bool disqualify_assign) {
    while (item) {
        if (item->node_type == AST_NODE_FOR_EXPR || item->node_type == AST_NODE_SPREAD ||
            item->node_type == AST_NODE_PIPE ||
            (disqualify_assign && item->node_type == AST_NODE_ASSIGN)) {
            return true;
        }
        item = item->next;
    }
    return false;
}

int detect_ndim_literal(AstNode* node, int64_t* shape_out, int max_ndim,
                        ArrayNumElemType* elem_type_out, bool disqualify_assign) {
    while (node && node->node_type == AST_NODE_PRIMARY) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    if (!node || node->node_type != AST_NODE_ARRAY) return 0;
    AstArrayNode* arr = (AstArrayNode*)node;
    TypeArray* type = (TypeArray*)arr->type;
    if (!type || type->length <= 0 || !type->nested) return 0;
    if (has_disqualifying_array_item(arr->item, disqualify_assign)) return 0;

    TypeId nid = type->nested->type_id;
    if (nid == LMD_TYPE_INT)   { shape_out[0] = type->length; *elem_type_out = ELEM_INT;   return 1; }
    if (nid == LMD_TYPE_INT64) { shape_out[0] = type->length; *elem_type_out = ELEM_INT64; return 1; }
    if (nid == LMD_TYPE_UINT64) { shape_out[0] = type->length; *elem_type_out = ELEM_UINT64; return 1; }
    if (nid == LMD_TYPE_FLOAT) { shape_out[0] = type->length; *elem_type_out = ELEM_FLOAT64; return 1; }
    if (nid == LMD_TYPE_NUM_SIZED) {
        shape_out[0] = type->length;
        *elem_type_out = num_sized_to_elem_type(type_num_sized_kind(type->nested));
        return 1;
    }
    if (nid == LMD_TYPE_ARRAY) {
        if (max_ndim <= 1) return 0;
        int64_t inner_shape[32];
        ArrayNumElemType inner_etype;
        int inner_ndim = detect_ndim_literal(arr->item, inner_shape, max_ndim - 1,
                                             &inner_etype, disqualify_assign);
        if (inner_ndim == 0) return 0;
        AstNode* sib = arr->item->next;
        while (sib) {
            int64_t sib_shape[32];
            ArrayNumElemType sib_etype;
            int sib_ndim = detect_ndim_literal(sib, sib_shape, max_ndim - 1,
                                               &sib_etype, disqualify_assign);
            if (sib_ndim != inner_ndim || sib_etype != inner_etype) return 0;
            for (int i = 0; i < sib_ndim; i++) {
                if (sib_shape[i] != inner_shape[i]) return 0;
            }
            sib = sib->next;
        }
        shape_out[0] = type->length;
        for (int i = 0; i < inner_ndim; i++) shape_out[i + 1] = inner_shape[i];
        *elem_type_out = inner_etype;
        return inner_ndim + 1;
    }
    return 0;
}
