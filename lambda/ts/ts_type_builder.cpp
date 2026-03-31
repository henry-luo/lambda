// ts_type_builder.cpp — Resolve TypeScript type syntax nodes to Lambda Type* structs
//
// Walks TsTypeNode AST subtrees and fills in resolved_type with Lambda Type*
// values. Types are allocated from the transpiler's AST pool.

#include "ts_transpiler.hpp"
#include "ts_runtime.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/hashmap.h"
#include <cstring>

// ============================================================================
// Predefined type name → TypeId mapping
// ============================================================================

TypeId ts_predefined_name_to_type_id(const char* name, int len) {
    if (len == 6 && memcmp(name, "number", 6) == 0)    return LMD_TYPE_FLOAT;
    if (len == 6 && memcmp(name, "string", 6) == 0)    return LMD_TYPE_STRING;
    if (len == 7 && memcmp(name, "boolean", 7) == 0)   return LMD_TYPE_BOOL;
    if (len == 4 && memcmp(name, "null", 4) == 0)      return LMD_TYPE_NULL;
    if (len == 9 && memcmp(name, "undefined", 9) == 0) return LMD_TYPE_NULL;
    if (len == 4 && memcmp(name, "void", 4) == 0)      return LMD_TYPE_NULL;
    if (len == 3 && memcmp(name, "any", 3) == 0)       return LMD_TYPE_ANY;
    if (len == 7 && memcmp(name, "unknown", 7) == 0)   return LMD_TYPE_ANY;
    if (len == 5 && memcmp(name, "never", 5) == 0)     return LMD_TYPE_ERROR;
    if (len == 6 && memcmp(name, "object", 6) == 0)    return LMD_TYPE_MAP;
    if (len == 6 && memcmp(name, "symbol", 6) == 0)    return LMD_TYPE_SYMBOL;
    if (len == 6 && memcmp(name, "bigint", 6) == 0)    return LMD_TYPE_INT64;
    return LMD_TYPE_ANY;
}

// ============================================================================
// Type resolution
// ============================================================================

// helper: create a base Type with a given TypeId from pool
static Type* make_base_type(Pool* pool, TypeId tid) {
    Type* t = (Type*)alloc_type(pool, tid, sizeof(Type));
    return t;
}

// build TypeMap with ShapeEntry chain and populate hash table
static void typemap_hash_build(TypeMap* tm) {
    tm->field_count = 0;
    memset(tm->field_index, 0, sizeof(tm->field_index));
    for (ShapeEntry* e = tm->shape; e; e = e->next) {
        typemap_hash_insert(tm, e);
    }
}

Type* ts_resolve_type(TsTranspiler* tp, TsTypeNode* node) {
    if (!node) return make_base_type(tp->ast_pool, LMD_TYPE_ANY);
    if (node->resolved_type) return node->resolved_type;

    Pool* pool = tp->ast_pool;

    switch ((TsAstNodeType)node->base.node_type) {

    case TS_AST_NODE_PREDEFINED_TYPE: {
        TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)node;
        node->resolved_type = make_base_type(pool, pn->predefined_id);
        return node->resolved_type;
    }

    case TS_AST_NODE_UNION_TYPE: {
        TsUnionTypeNode* un = (TsUnionTypeNode*)node;
        if (un->type_count == 0) {
            node->resolved_type = make_base_type(pool, LMD_TYPE_ANY);
            return node->resolved_type;
        }
        // build left-associative TypeBinary chain: A | B | C → (A | B) | C
        Type* result = ts_resolve_type(tp, un->types[0]);
        for (int i = 1; i < un->type_count; i++) {
            Type* right = ts_resolve_type(tp, un->types[i]);
            TypeBinary* tb = (TypeBinary*)alloc_type(pool, LMD_TYPE_TYPE, sizeof(TypeBinary));
            tb->kind = TYPE_KIND_BINARY;
            tb->op = OPERATOR_UNION;
            tb->left = result;
            tb->right = right;
            result = (Type*)tb;
        }
        node->resolved_type = result;
        return result;
    }

    case TS_AST_NODE_INTERSECTION_TYPE: {
        TsIntersectionTypeNode* in_node = (TsIntersectionTypeNode*)node;
        if (in_node->type_count == 0) {
            node->resolved_type = make_base_type(pool, LMD_TYPE_ANY);
            return node->resolved_type;
        }
        Type* result = ts_resolve_type(tp, in_node->types[0]);
        for (int i = 1; i < in_node->type_count; i++) {
            Type* right = ts_resolve_type(tp, in_node->types[i]);
            TypeBinary* tb = (TypeBinary*)alloc_type(pool, LMD_TYPE_TYPE, sizeof(TypeBinary));
            tb->kind = TYPE_KIND_BINARY;
            tb->op = OPERATOR_INTERSECT;
            tb->left = result;
            tb->right = right;
            result = (Type*)tb;
        }
        node->resolved_type = result;
        return result;
    }

    case TS_AST_NODE_ARRAY_TYPE: {
        TsArrayTypeNode* an = (TsArrayTypeNode*)node;
        Type* elem = ts_resolve_type(tp, an->element_type);
        TypeArray* ta = (TypeArray*)alloc_type(pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
        ta->nested = elem;
        node->resolved_type = (Type*)ta;
        return (Type*)ta;
    }

    case TS_AST_NODE_TUPLE_TYPE: {
        TsTupleTypeNode* tn = (TsTupleTypeNode*)node;
        // represent as TypeArray with known length
        TypeArray* ta = (TypeArray*)alloc_type(pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
        ta->length = tn->element_count;
        if (tn->element_count > 0) {
            ta->nested = ts_resolve_type(tp, tn->element_types[0]);
        }
        node->resolved_type = (Type*)ta;
        return (Type*)ta;
    }

    case TS_AST_NODE_FUNCTION_TYPE: {
        TsFunctionTypeNode* fn = (TsFunctionTypeNode*)node;
        TypeFunc* tf = (TypeFunc*)alloc_type(pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
        tf->returned = fn->return_type ? ts_resolve_type(tp, fn->return_type) : make_base_type(pool, LMD_TYPE_ANY);
        tf->param_count = fn->param_count;
        tf->required_param_count = fn->param_count;
        TypeParam* prev_p = NULL;
        for (int i = 0; i < fn->param_count; i++) {
            TypeParam* tp_param = (TypeParam*)pool_calloc(pool, sizeof(TypeParam));
            tp_param->type_id = LMD_TYPE_TYPE;
            // note: TypeParam has no name field — parameter names are in the AST, not the type
            Type* pt = ts_resolve_type(tp, fn->param_types[i]);
            tp_param->full_type = pt;
            if (prev_p) prev_p->next = tp_param;
            else tf->param = tp_param;
            prev_p = tp_param;
        }
        node->resolved_type = (Type*)tf;
        return (Type*)tf;
    }

    case TS_AST_NODE_OBJECT_TYPE: {
        TsObjectTypeNode* on = (TsObjectTypeNode*)node;
        TypeMap* tm = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
        tm->length = on->member_count;
        ShapeEntry* prev_se = NULL;
        for (int i = 0; i < on->member_count; i++) {
            ShapeEntry* se = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
            if (on->member_names && on->member_names[i]) {
                se->name = (StrView*)pool_calloc(pool, sizeof(StrView));
                se->name->str = on->member_names[i]->chars;
                se->name->length = on->member_names[i]->len;
            }
            Type* field_type = ts_resolve_type(tp, on->member_types[i]);
            if (on->member_optional && on->member_optional[i]) {
                // wrap in TypeUnary OPTIONAL
                TypeUnary* tu = (TypeUnary*)alloc_type(pool, LMD_TYPE_TYPE, sizeof(TypeUnary));
                tu->kind = TYPE_KIND_UNARY;
                tu->op = OPERATOR_OPTIONAL;
                tu->operand = field_type;
                se->type = (Type*)tu;
            } else {
                se->type = field_type;
            }
            if (prev_se) prev_se->next = se;
            else tm->shape = se;
            prev_se = se;
        }
        tm->last = prev_se;
        typemap_hash_build(tm);
        node->resolved_type = (Type*)tm;
        return (Type*)tm;
    }

    case TS_AST_NODE_TYPE_REFERENCE: {
        TsTypeReferenceNode* rn = (TsTypeReferenceNode*)node;
        // look up in type registry
        Type* found = ts_type_registry_lookup(tp, rn->name->chars);
        if (!found) {
            // check well-known generic types
            if (rn->name->len == 5 && memcmp(rn->name->chars, "Array", 5) == 0) {
                TypeArray* ta = (TypeArray*)alloc_type(pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
                if (rn->type_arg_count > 0) {
                    ta->nested = ts_resolve_type(tp, rn->type_args[0]);
                }
                node->resolved_type = (Type*)ta;
                return (Type*)ta;
            }
            if (rn->name->len == 6 && memcmp(rn->name->chars, "Record", 6) == 0) {
                node->resolved_type = make_base_type(pool, LMD_TYPE_MAP);
                return node->resolved_type;
            }
            if (rn->name->len == 7 && memcmp(rn->name->chars, "Promise", 7) == 0) {
                // resolve to the inner type argument
                if (rn->type_arg_count > 0) {
                    node->resolved_type = ts_resolve_type(tp, rn->type_args[0]);
                } else {
                    node->resolved_type = make_base_type(pool, LMD_TYPE_ANY);
                }
                return node->resolved_type;
            }
            log_error("ts type unresolved: %.*s", rn->name->len, rn->name->chars);
            found = make_base_type(pool, LMD_TYPE_ANY);
        }
        node->resolved_type = found;
        return found;
    }

    case TS_AST_NODE_PARENTHESIZED_TYPE: {
        TsParenthesizedTypeNode* pn = (TsParenthesizedTypeNode*)node;
        node->resolved_type = ts_resolve_type(tp, pn->inner);
        return node->resolved_type;
    }

    case TS_AST_NODE_LITERAL_TYPE: {
        TsLiteralTypeNode* ln = (TsLiteralTypeNode*)node;
        switch (ln->literal_type) {
            case JS_LITERAL_NUMBER:  node->resolved_type = make_base_type(pool, LMD_TYPE_FLOAT); break;
            case JS_LITERAL_STRING:  node->resolved_type = make_base_type(pool, LMD_TYPE_STRING); break;
            case JS_LITERAL_BOOLEAN: node->resolved_type = make_base_type(pool, LMD_TYPE_BOOL); break;
            case JS_LITERAL_NULL:    node->resolved_type = make_base_type(pool, LMD_TYPE_NULL); break;
            default:                 node->resolved_type = make_base_type(pool, LMD_TYPE_ANY); break;
        }
        return node->resolved_type;
    }

    case TS_AST_NODE_TYPE_ANNOTATION: {
        TsTypeAnnotationNode* an = (TsTypeAnnotationNode*)node;
        node->resolved_type = ts_resolve_type(tp, an->type_expr);
        return node->resolved_type;
    }

    default:
        // unresolved complex types fall back to any
        node->resolved_type = make_base_type(pool, LMD_TYPE_ANY);
        return node->resolved_type;
    }
}

// ============================================================================
// Type resolution: walk AST and resolve all type annotations
// ============================================================================

static void ts_resolve_types_in_node(TsTranspiler* tp, JsAstNode* node) {
    if (!node) return;

    int nt = node->node_type;

    // resolve type annotations on TS-specific nodes
    if (nt >= TS_AST_NODE_TYPE_ANNOTATION && nt < TS_AST_NODE__MAX) {
        TsTypeNode* tn = (TsTypeNode*)node;
        ts_resolve_type(tp, tn);
    }

    // resolve type annotations on extended variable declarators
    // (handled during transpilation when encountered)

    // recurse through linked list siblings
    for (JsAstNode* sibling = node->next; sibling; sibling = sibling->next) {
        ts_resolve_types_in_node(tp, sibling);
    }
}

void ts_resolve_all_types(TsTranspiler* tp, JsAstNode* root) {
    if (!root) return;
    ts_resolve_types_in_node(tp, root);
}

// ============================================================================
// Type registry
// ============================================================================

static int ts_type_reg_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((TsTypeRegistryEntry*)a)->name, ((TsTypeRegistryEntry*)b)->name);
}

static uint64_t ts_type_reg_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((TsTypeRegistryEntry*)item)->name,
        strlen(((TsTypeRegistryEntry*)item)->name), seed0, seed1);
}

void ts_type_registry_init(TsTranspiler* tp) {
    tp->type_registry = hashmap_new(sizeof(TsTypeRegistryEntry), 32, 0, 0,
        ts_type_reg_hash, ts_type_reg_cmp, NULL, NULL);
}

void ts_type_registry_add(TsTranspiler* tp, const char* name, Type* type) {
    TsTypeRegistryEntry entry;
    memset(&entry, 0, sizeof(entry));
    size_t name_len = strlen(name);
    if (name_len >= sizeof(entry.name)) name_len = sizeof(entry.name) - 1;
    memcpy(entry.name, name, name_len);
    entry.name[name_len] = '\0';
    entry.type = type;
    hashmap_set(tp->type_registry, &entry);
}

Type* ts_type_registry_lookup(TsTranspiler* tp, const char* name) {
    TsTypeRegistryEntry query;
    memset(&query, 0, sizeof(query));
    size_t name_len = strlen(name);
    if (name_len >= sizeof(query.name)) name_len = sizeof(query.name) - 1;
    memcpy(query.name, name, name_len);
    query.name[name_len] = '\0';
    const TsTypeRegistryEntry* found = (const TsTypeRegistryEntry*)hashmap_get(tp->type_registry, &query);
    return found ? found->type : NULL;
}
