// build_ts_ast.cpp — TypeScript AST builder
//
// Wraps the JavaScript AST builder. Delegates JS nodes to build_js_ast_node(),
// and handles TS-specific nodes (type annotations, interfaces, enums, etc.)
// by building TsTypeNode subtrees with type expressions fully preserved.

#include "ts_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/strbuf.h"
#include <cstring>

// ============================================================================
// AST node allocation
// ============================================================================

JsAstNode* alloc_ts_ast_node(TsTranspiler* tp, int node_type, TSNode node, size_t size) {
    JsAstNode* ast_node = (JsAstNode*)pool_alloc(tp->ast_pool, size);
    memset(ast_node, 0, size);
    ast_node->node_type = (JsAstNodeType)node_type;
    ast_node->node = node;
    return ast_node;
}

// ============================================================================
// Utility: get source text for a tree-sitter node
// ============================================================================

static const char* ts_node_text(TsTranspiler* tp, TSNode node, int* out_len) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    *out_len = (int)(end - start);
    return tp->source + start;
}

// allocate a String* from the AST pool containing a copy of (src, len)
static String* ts_pool_string(TsTranspiler* tp, const char* src, int len) {
    String* s = (String*)pool_alloc(tp->ast_pool, sizeof(String) + len + 1);
    s->len = len;
    s->is_ascii = 1;
    memcpy(s->chars, src, len);
    s->chars[len] = '\0';
    return s;
}

// ============================================================================
// Type node builders
// ============================================================================

// forward declarations
static TsTypeNode* build_ts_type_expr(TsTranspiler* tp, TSNode node);

static TsTypeNode* build_ts_predefined_type(TsTranspiler* tp, TSNode node) {
    int len;
    const char* text = ts_node_text(tp, node, &len);
    TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_PREDEFINED_TYPE, node, sizeof(TsPredefinedTypeNode));
    pn->predefined_id = ts_predefined_name_to_type_id(text, len);
    return (TsTypeNode*)pn;
}

static TsTypeNode* build_ts_type_reference(TsTranspiler* tp, TSNode node) {
    TsTypeReferenceNode* rn = (TsTypeReferenceNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_TYPE_REFERENCE, node, sizeof(TsTypeReferenceNode));

    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count > 0) {
        TSNode name_node = ts_node_named_child(node, 0);
        int len;
        const char* text = ts_node_text(tp, name_node, &len);
        rn->name = ts_pool_string(tp, text, len);

        // check for type arguments (<T, U>)
        if (child_count > 1) {
            TSNode args_node = ts_node_named_child(node, 1);
            uint32_t arg_count = ts_node_named_child_count(args_node);
            if (arg_count > 0) {
                rn->type_args = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * arg_count);
                rn->type_arg_count = (int)arg_count;
                for (uint32_t i = 0; i < arg_count; i++) {
                    rn->type_args[i] = build_ts_type_expr(tp, ts_node_named_child(args_node, i));
                }
            }
        }
    } else {
        // fallback: use node text as name
        int len;
        const char* text = ts_node_text(tp, node, &len);
        rn->name = ts_pool_string(tp, text, len);
    }
    return (TsTypeNode*)rn;
}

static TsTypeNode* build_ts_union_type(TsTranspiler* tp, TSNode node) {
    TsUnionTypeNode* un = (TsUnionTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_UNION_TYPE, node, sizeof(TsUnionTypeNode));

    uint32_t child_count = ts_node_named_child_count(node);
    un->types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * child_count);
    un->type_count = (int)child_count;
    for (uint32_t i = 0; i < child_count; i++) {
        un->types[i] = build_ts_type_expr(tp, ts_node_named_child(node, i));
    }
    return (TsTypeNode*)un;
}

static TsTypeNode* build_ts_intersection_type(TsTranspiler* tp, TSNode node) {
    TsIntersectionTypeNode* in_node = (TsIntersectionTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_INTERSECTION_TYPE, node, sizeof(TsIntersectionTypeNode));

    uint32_t child_count = ts_node_named_child_count(node);
    in_node->types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * child_count);
    in_node->type_count = (int)child_count;
    for (uint32_t i = 0; i < child_count; i++) {
        in_node->types[i] = build_ts_type_expr(tp, ts_node_named_child(node, i));
    }
    return (TsTypeNode*)in_node;
}

static TsTypeNode* build_ts_array_type(TsTranspiler* tp, TSNode node) {
    TsArrayTypeNode* an = (TsArrayTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_ARRAY_TYPE, node, sizeof(TsArrayTypeNode));

    if (ts_node_named_child_count(node) > 0) {
        an->element_type = build_ts_type_expr(tp, ts_node_named_child(node, 0));
    }
    return (TsTypeNode*)an;
}

static TsTypeNode* build_ts_tuple_type(TsTranspiler* tp, TSNode node) {
    TsTupleTypeNode* tn = (TsTupleTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_TUPLE_TYPE, node, sizeof(TsTupleTypeNode));

    uint32_t child_count = ts_node_named_child_count(node);
    tn->element_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * child_count);
    tn->element_count = (int)child_count;
    for (uint32_t i = 0; i < child_count; i++) {
        tn->element_types[i] = build_ts_type_expr(tp, ts_node_named_child(node, i));
    }
    return (TsTypeNode*)tn;
}

static TsTypeNode* build_ts_function_type(TsTranspiler* tp, TSNode node) {
    TsFunctionTypeNode* fn = (TsFunctionTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_FUNCTION_TYPE, node, sizeof(TsFunctionTypeNode));

    // function types: parameters are named children before the return type
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count > 0) {
        // last named child is the return type
        fn->return_type = build_ts_type_expr(tp, ts_node_named_child(node, child_count - 1));

        // preceding children are params (formal_parameters node with children)
        if (child_count > 1) {
            TSNode params_node = ts_node_named_child(node, 0);
            uint32_t param_count = ts_node_named_child_count(params_node);
            fn->param_count = (int)param_count;
            fn->param_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * param_count);
            fn->param_names = (String**)pool_calloc(tp->ast_pool, sizeof(String*) * param_count);
            for (uint32_t i = 0; i < param_count; i++) {
                TSNode param = ts_node_named_child(params_node, i);
                // each param may have a name and a type_annotation
                uint32_t pc = ts_node_named_child_count(param);
                fn->param_types[i] = NULL;
                for (uint32_t j = 0; j < pc; j++) {
                    TSNode child = ts_node_named_child(param, j);
                    const char* child_type = ts_node_type(child);
                    if (strcmp(child_type, "identifier") == 0) {
                        int len;
                        const char* text = ts_node_text(tp, child, &len);
                        fn->param_names[i] = ts_pool_string(tp, text, len);
                    } else if (strcmp(child_type, "type_annotation") == 0) {
                        if (ts_node_named_child_count(child) > 0) {
                            fn->param_types[i] = build_ts_type_expr(tp, ts_node_named_child(child, 0));
                        }
                    }
                }
                if (!fn->param_types[i]) {
                    // fallback: any
                    fn->param_types[i] = (TsTypeNode*)alloc_ts_ast_node(tp,
                        TS_AST_NODE_PREDEFINED_TYPE, param, sizeof(TsPredefinedTypeNode));
                    ((TsPredefinedTypeNode*)fn->param_types[i])->predefined_id = LMD_TYPE_ANY;
                }
            }
        }
    }
    return (TsTypeNode*)fn;
}

static TsTypeNode* build_ts_object_type(TsTranspiler* tp, TSNode node) {
    TsObjectTypeNode* on = (TsObjectTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_OBJECT_TYPE, node, sizeof(TsObjectTypeNode));

    uint32_t child_count = ts_node_named_child_count(node);
    on->member_count = (int)child_count;
    on->member_types = (TsTypeNode**)pool_calloc(tp->ast_pool, sizeof(TsTypeNode*) * child_count);
    on->member_names = (String**)pool_calloc(tp->ast_pool, sizeof(String*) * child_count);
    on->member_optional = (bool*)pool_calloc(tp->ast_pool, sizeof(bool) * child_count);
    on->member_readonly = (bool*)pool_calloc(tp->ast_pool, sizeof(bool) * child_count);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode member = ts_node_named_child(node, i);
        uint32_t mc = ts_node_named_child_count(member);
        for (uint32_t j = 0; j < mc; j++) {
            TSNode child = ts_node_named_child(member, j);
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "property_identifier") == 0 ||
                strcmp(child_type, "identifier") == 0) {
                int len;
                const char* text = ts_node_text(tp, child, &len);
                on->member_names[i] = ts_pool_string(tp, text, len);
            } else if (strcmp(child_type, "type_annotation") == 0) {
                if (ts_node_named_child_count(child) > 0) {
                    on->member_types[i] = build_ts_type_expr(tp, ts_node_named_child(child, 0));
                }
            }
        }
        // check for optional marker '?' in anonymous children
        uint32_t total_children = ts_node_child_count(member);
        for (uint32_t j = 0; j < total_children; j++) {
            TSNode child = ts_node_child(member, j);
            if (!ts_node_is_named(child)) {
                int len;
                const char* text = ts_node_text(tp, child, &len);
                if (len == 1 && text[0] == '?') {
                    on->member_optional[i] = true;
                }
            }
        }
        // check for 'readonly' keyword
        for (uint32_t j = 0; j < total_children; j++) {
            TSNode child = ts_node_child(member, j);
            int len;
            const char* text = ts_node_text(tp, child, &len);
            if (len == 8 && memcmp(text, "readonly", 8) == 0) {
                on->member_readonly[i] = true;
            }
        }
        if (!on->member_types[i]) {
            on->member_types[i] = (TsTypeNode*)alloc_ts_ast_node(tp,
                TS_AST_NODE_PREDEFINED_TYPE, member, sizeof(TsPredefinedTypeNode));
            ((TsPredefinedTypeNode*)on->member_types[i])->predefined_id = LMD_TYPE_ANY;
        }
    }
    return (TsTypeNode*)on;
}

static TsTypeNode* build_ts_parenthesized_type(TsTranspiler* tp, TSNode node) {
    TsParenthesizedTypeNode* pn = (TsParenthesizedTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_PARENTHESIZED_TYPE, node, sizeof(TsParenthesizedTypeNode));
    if (ts_node_named_child_count(node) > 0) {
        pn->inner = build_ts_type_expr(tp, ts_node_named_child(node, 0));
    }
    return (TsTypeNode*)pn;
}

static TsTypeNode* build_ts_conditional_type(TsTranspiler* tp, TSNode node) {
    TsConditionalTypeNode* cn = (TsConditionalTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_CONDITIONAL_TYPE, node, sizeof(TsConditionalTypeNode));
    uint32_t cc = ts_node_named_child_count(node);
    if (cc >= 4) {
        cn->check_type   = build_ts_type_expr(tp, ts_node_named_child(node, 0));
        cn->extends_type = build_ts_type_expr(tp, ts_node_named_child(node, 1));
        cn->true_type    = build_ts_type_expr(tp, ts_node_named_child(node, 2));
        cn->false_type   = build_ts_type_expr(tp, ts_node_named_child(node, 3));
    }
    return (TsTypeNode*)cn;
}

// dispatch for all type expression nodes
static TsTypeNode* build_ts_type_expr(TsTranspiler* tp, TSNode node) {
    const char* type_str = ts_node_type(node);

    if (strcmp(type_str, "predefined_type") == 0)
        return build_ts_predefined_type(tp, node);
    if (strcmp(type_str, "type_identifier") == 0 || strcmp(type_str, "identifier") == 0)
        return build_ts_type_reference(tp, node);
    if (strcmp(type_str, "generic_type") == 0)
        return build_ts_type_reference(tp, node);
    if (strcmp(type_str, "union_type") == 0)
        return build_ts_union_type(tp, node);
    if (strcmp(type_str, "intersection_type") == 0)
        return build_ts_intersection_type(tp, node);
    if (strcmp(type_str, "array_type") == 0)
        return build_ts_array_type(tp, node);
    if (strcmp(type_str, "tuple_type") == 0)
        return build_ts_tuple_type(tp, node);
    if (strcmp(type_str, "function_type") == 0)
        return build_ts_function_type(tp, node);
    if (strcmp(type_str, "object_type") == 0)
        return build_ts_object_type(tp, node);
    if (strcmp(type_str, "parenthesized_type") == 0)
        return build_ts_parenthesized_type(tp, node);
    if (strcmp(type_str, "conditional_type") == 0)
        return build_ts_conditional_type(tp, node);
    if (strcmp(type_str, "literal_type") == 0) {
        TsLiteralTypeNode* ln = (TsLiteralTypeNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_LITERAL_TYPE, node, sizeof(TsLiteralTypeNode));
        // determine literal kind from child
        if (ts_node_named_child_count(node) > 0) {
            TSNode child = ts_node_named_child(node, 0);
            const char* ct = ts_node_type(child);
            if (strcmp(ct, "number") == 0) ln->literal_type = JS_LITERAL_NUMBER;
            else if (strcmp(ct, "string") == 0) ln->literal_type = JS_LITERAL_STRING;
            else if (strcmp(ct, "true") == 0) ln->literal_type = JS_LITERAL_BOOLEAN;
            else if (strcmp(ct, "false") == 0) ln->literal_type = JS_LITERAL_BOOLEAN;
            else if (strcmp(ct, "null") == 0) ln->literal_type = JS_LITERAL_NULL;
        }
        return (TsTypeNode*)ln;
    }

    // fallback: create predefined ANY node
    log_debug("ts ast: unhandled type node '%s'", type_str);
    TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_PREDEFINED_TYPE, node, sizeof(TsPredefinedTypeNode));
    pn->predefined_id = LMD_TYPE_ANY;
    return (TsTypeNode*)pn;
}

// ============================================================================
// Type annotation builder
// ============================================================================

TsTypeNode* build_ts_type_annotation(TsTranspiler* tp, TSNode node) {
    TsTypeAnnotationNode* an = (TsTypeAnnotationNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_TYPE_ANNOTATION, node, sizeof(TsTypeAnnotationNode));
    if (ts_node_named_child_count(node) > 0) {
        an->type_expr = build_ts_type_expr(tp, ts_node_named_child(node, 0));
    }
    return (TsTypeNode*)an;
}

// ============================================================================
// TS declaration builders
// ============================================================================

static JsAstNode* build_ts_interface_declaration(TsTranspiler* tp, TSNode node) {
    TsInterfaceNode* iface = (TsInterfaceNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_INTERFACE, node, sizeof(TsInterfaceNode));

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "type_identifier") == 0 || strcmp(child_type, "identifier") == 0) {
            int len;
            const char* text = ts_node_text(tp, child, &len);
            iface->name = ts_pool_string(tp, text, len);
        } else if (strcmp(child_type, "object_type") == 0 || strcmp(child_type, "interface_body") == 0) {
            iface->body = (TsObjectTypeNode*)build_ts_object_type(tp, child);
        } else if (strcmp(child_type, "extends_type_clause") == 0 ||
                   strcmp(child_type, "extends_clause") == 0) {
            uint32_t ext_count = ts_node_named_child_count(child);
            iface->extends_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * ext_count);
            iface->extends_count = (int)ext_count;
            for (uint32_t j = 0; j < ext_count; j++) {
                iface->extends_types[j] = build_ts_type_expr(tp, ts_node_named_child(child, j));
            }
        }
    }

    // register interface in type registry
    if (iface->name && iface->body) {
        Type* resolved = ts_resolve_type(tp, (TsTypeNode*)iface->body);
        iface->resolved_type = resolved;
        if (resolved && resolved->type_id == LMD_TYPE_MAP) {
            ((TypeMap*)resolved)->struct_name = iface->name->chars;
        }
        ts_type_registry_add(tp, iface->name->chars, resolved);
    }

    return (JsAstNode*)iface;
}

static JsAstNode* build_ts_type_alias_declaration(TsTranspiler* tp, TSNode node) {
    TsTypeAliasNode* alias = (TsTypeAliasNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_TYPE_ALIAS, node, sizeof(TsTypeAliasNode));

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "type_identifier") == 0 || strcmp(child_type, "identifier") == 0) {
            int len;
            const char* text = ts_node_text(tp, child, &len);
            alias->name = ts_pool_string(tp, text, len);
        } else {
            // the type expression itself
            if (!alias->type_expr) {
                alias->type_expr = build_ts_type_expr(tp, child);
            }
        }
    }

    // register alias in type registry
    if (alias->name && alias->type_expr) {
        Type* resolved = ts_resolve_type(tp, alias->type_expr);
        alias->resolved_type = resolved;
        ts_type_registry_add(tp, alias->name->chars, resolved);
    }

    return (JsAstNode*)alias;
}

static JsAstNode* build_ts_enum_declaration(TsTranspiler* tp, TSNode node) {
    TsEnumDeclarationNode* enum_node = (TsEnumDeclarationNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_ENUM_DECLARATION, node, sizeof(TsEnumDeclarationNode));

    // check for const enum
    uint32_t total_children = ts_node_child_count(node);
    for (uint32_t i = 0; i < total_children; i++) {
        TSNode child = ts_node_child(node, i);
        if (!ts_node_is_named(child)) {
            int len;
            const char* text = ts_node_text(tp, child, &len);
            if (len == 5 && memcmp(text, "const", 5) == 0) {
                enum_node->is_const = true;
            }
        }
    }

    // collect name and members
    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "identifier") == 0) {
            int len;
            const char* text = ts_node_text(tp, child, &len);
            enum_node->name = ts_pool_string(tp, text, len);
        } else if (strcmp(child_type, "enum_body") == 0) {
            uint32_t mc = ts_node_named_child_count(child);
            enum_node->members = (JsAstNode**)pool_alloc(tp->ast_pool, sizeof(JsAstNode*) * mc);
            enum_node->member_count = (int)mc;
            int auto_val = 0;
            for (uint32_t j = 0; j < mc; j++) {
                TSNode mem = ts_node_named_child(child, j);
                TsEnumMemberNode* em = (TsEnumMemberNode*)alloc_ts_ast_node(tp,
                    TS_AST_NODE_ENUM_MEMBER, mem, sizeof(TsEnumMemberNode));
                // get member name
                if (ts_node_named_child_count(mem) > 0) {
                    TSNode name_node = ts_node_named_child(mem, 0);
                    int nlen;
                    const char* ntext = ts_node_text(tp, name_node, &nlen);
                    em->name = ts_pool_string(tp, ntext, nlen);
                }
                em->auto_value = auto_val++;
                // TODO: handle explicit initializer
                enum_node->members[j] = (JsAstNode*)em;
            }
        }
    }

    return (JsAstNode*)enum_node;
}

// ============================================================================
// Main TS AST builder — dispatches to TS or JS builders
// ============================================================================

// forward declaration
static JsAstNode* build_ts_node(TsTranspiler* tp, TSNode node);

static JsAstNode* build_ts_statement_list(TsTranspiler* tp, TSNode parent) {
    uint32_t child_count = ts_node_named_child_count(parent);
    JsAstNode* first = NULL;
    JsAstNode* last = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(parent, i);
        JsAstNode* stmt = build_ts_node(tp, child);
        if (!stmt) continue;

        if (!first) {
            first = stmt;
            last = stmt;
        } else {
            last->next = stmt;
            last = stmt;
        }
        // follow linked list to real end
        while (last->next) last = last->next;
    }
    return first;
}

static JsAstNode* build_ts_node(TsTranspiler* tp, TSNode node) {
    if (ts_node_is_null(node)) return NULL;

    const char* type_str = ts_node_type(node);

    // TS-specific declarations
    if (strcmp(type_str, "interface_declaration") == 0)
        return build_ts_interface_declaration(tp, node);
    if (strcmp(type_str, "type_alias_declaration") == 0)
        return build_ts_type_alias_declaration(tp, node);
    if (strcmp(type_str, "enum_declaration") == 0)
        return build_ts_enum_declaration(tp, node);
    if (strcmp(type_str, "ambient_declaration") == 0) {
        // declare ... — parse for type info, no code emitted
        // for now, skip
        return NULL;
    }

    // TS expression wrappers
    if (strcmp(type_str, "as_expression") == 0) {
        TsTypeExprNode* as_node = (TsTypeExprNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_AS_EXPRESSION, node, sizeof(TsTypeExprNode));
        uint32_t cc = ts_node_named_child_count(node);
        if (cc >= 2) {
            as_node->inner = build_ts_node(tp, ts_node_named_child(node, 0));
            as_node->target_type = build_ts_type_expr(tp, ts_node_named_child(node, 1));
        }
        return (JsAstNode*)as_node;
    }
    if (strcmp(type_str, "satisfies_expression") == 0) {
        TsTypeExprNode* sat_node = (TsTypeExprNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_SATISFIES_EXPRESSION, node, sizeof(TsTypeExprNode));
        uint32_t cc = ts_node_named_child_count(node);
        if (cc >= 2) {
            sat_node->inner = build_ts_node(tp, ts_node_named_child(node, 0));
            sat_node->target_type = build_ts_type_expr(tp, ts_node_named_child(node, 1));
        }
        return (JsAstNode*)sat_node;
    }
    if (strcmp(type_str, "non_null_expression") == 0) {
        TsNonNullNode* nn = (TsNonNullNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_NON_NULL_EXPRESSION, node, sizeof(TsNonNullNode));
        if (ts_node_named_child_count(node) > 0) {
            nn->inner = build_ts_node(tp, ts_node_named_child(node, 0));
        }
        return (JsAstNode*)nn;
    }

    // program node
    if (strcmp(type_str, "program") == 0) {
        JsProgramNode* prog = (JsProgramNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_PROGRAM, node, sizeof(JsProgramNode));
        prog->body = build_ts_statement_list(tp, node);
        return (JsAstNode*)prog;
    }

    // for all other nodes, delegate to the JS AST builder
    // cast TsTranspiler to JsTranspiler (layout-compatible for core fields)
    JsAstNode* result = build_js_statement((JsTranspiler*)tp, node);
    if (!result) {
        result = build_js_expression((JsTranspiler*)tp, node);
    }

    // after building a JS node, check if it has a type_annotation child
    // and attach it
    if (result) {
        uint32_t child_count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(node, i);
            const char* ct = ts_node_type(child);
            if (strcmp(ct, "type_annotation") == 0) {
                // store the type annotation in the AST node's type field
                TsTypeNode* type_ann = build_ts_type_annotation(tp, child);
                if (type_ann) {
                    Type* resolved = ts_resolve_type(tp, type_ann);
                    result->type = resolved;
                }
                break;
            }
        }
    }

    return result;
}

JsAstNode* build_ts_ast(TsTranspiler* tp, TSNode root) {
    return build_ts_node(tp, root);
}

// ============================================================================
// Error handling
// ============================================================================

void ts_error(TsTranspiler* tp, TSNode node, const char* format, ...) {
    tp->has_errors = true;
    TSPoint pos = ts_node_start_point(node);
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "ts error [%d:%d]: ", pos.row + 1, pos.column + 1);
    if (tp->error_buf) {
        strbuf_append_str(tp->error_buf, prefix);
    }
    log_error("%s", prefix);
    (void)format; // TODO: full va_list formatting
}

void ts_warning(TsTranspiler* tp, TSNode node, const char* format, ...) {
    TSPoint pos = ts_node_start_point(node);
    log_debug("ts warning [%d:%d]", pos.row + 1, pos.column + 1);
    (void)format;
}
