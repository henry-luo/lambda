// build_ts_ast.cpp — TypeScript AST builder
//
// Wraps the JavaScript AST builder. Delegates JS nodes to build_js_ast_node(),
// and handles TS-specific nodes (type annotations, interfaces, enums, etc.)
// by building TsTypeNode subtrees with type expressions fully preserved.
//
// With the tree-sitter-typescript parser, function parameters appear as
// required_parameter / optional_parameter nodes (not plain identifiers as in JS).
// Variable declarators may have a type_annotation child. Functions may have
// return_type and type_parameters. This builder intercepts those cases.

#include "ts_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdlib>

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
            bool auto_val_valid = true; // false after a string initializer
            for (uint32_t j = 0; j < mc; j++) {
                TSNode mem = ts_node_named_child(child, j);
                const char* mem_type = ts_node_type(mem);
                TsEnumMemberNode* em = (TsEnumMemberNode*)alloc_ts_ast_node(tp,
                    TS_AST_NODE_ENUM_MEMBER, mem, sizeof(TsEnumMemberNode));

                if (strcmp(mem_type, "enum_assignment") == 0) {
                    // member with explicit initializer: Name = value
                    uint32_t mem_cc = ts_node_named_child_count(mem);
                    if (mem_cc > 0) {
                        TSNode name_node = ts_node_named_child(mem, 0);
                        int nlen;
                        const char* ntext = ts_node_text(tp, name_node, &nlen);
                        em->name = ts_pool_string(tp, ntext, nlen);
                    }
                    if (mem_cc > 1) {
                        TSNode init_node = ts_node_named_child(mem, 1);
                        em->initializer = build_ts_expression(tp, init_node);
                        // resolve numeric literal for auto_val continuation
                        const char* init_type = ts_node_type(init_node);
                        if (strcmp(init_type, "number") == 0) {
                            int ilen;
                            const char* itext = ts_node_text(tp, init_node, &ilen);
                            char buf[64];
                            int copy_len = ilen < 63 ? ilen : 63;
                            memcpy(buf, itext, copy_len);
                            buf[copy_len] = '\0';
                            auto_val = (int)strtol(buf, NULL, 0);
                            em->auto_value = auto_val;
                            auto_val++;
                            auto_val_valid = true;
                        } else if (strcmp(init_type, "string") == 0 ||
                                   strcmp(init_type, "template_string") == 0) {
                            // string enum member — auto_val sequence breaks
                            em->auto_value = -1;
                            auto_val_valid = false;
                        } else {
                            // computed expression — use current auto_val as best guess
                            em->auto_value = auto_val_valid ? auto_val++ : -1;
                        }
                    }
                } else {
                    // bare member (property_identifier or similar): uses auto_val
                    if (ts_node_named_child_count(mem) > 0) {
                        TSNode name_node = ts_node_named_child(mem, 0);
                        int nlen;
                        const char* ntext = ts_node_text(tp, name_node, &nlen);
                        em->name = ts_pool_string(tp, ntext, nlen);
                    } else {
                        // node itself is the name (bare property_identifier)
                        int nlen;
                        const char* ntext = ts_node_text(tp, mem, &nlen);
                        em->name = ts_pool_string(tp, ntext, nlen);
                    }
                    em->auto_value = auto_val_valid ? auto_val++ : -1;
                }

                enum_node->members[j] = (JsAstNode*)em;
            }
        }
    }

    return (JsAstNode*)enum_node;
}

// ============================================================================
// TS expression builder — wraps JS expression builder with TS node interception
// ============================================================================

// forward declarations
static JsAstNode* build_ts_node(TsTranspiler* tp, TSNode node);
static JsAstNode* build_ts_function(TsTranspiler* tp, TSNode func_node);

// TS-aware expression builder: intercepts function nodes with TS-specific
// parameter handling, delegates everything else to build_js_expression
static JsAstNode* build_ts_expression(TsTranspiler* tp, TSNode node) {
    const char* type_str = ts_node_type(node);

    // intercept function expressions/arrows that have TS parameter nodes
    if (strcmp(type_str, "arrow_function") == 0 ||
        strcmp(type_str, "function_expression") == 0 ||
        strcmp(type_str, "generator_function") == 0) {
        return build_ts_function(tp, node);
    }

    // TS expression wrappers
    if (strcmp(type_str, "as_expression") == 0) {
        TsTypeExprNode* as_node = (TsTypeExprNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_AS_EXPRESSION, node, sizeof(TsTypeExprNode));
        uint32_t cc = ts_node_named_child_count(node);
        if (cc >= 2) {
            as_node->inner = build_ts_expression(tp, ts_node_named_child(node, 0));
            as_node->target_type = build_ts_type_expr(tp, ts_node_named_child(node, 1));
        }
        return (JsAstNode*)as_node;
    }
    if (strcmp(type_str, "satisfies_expression") == 0) {
        TsTypeExprNode* sat_node = (TsTypeExprNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_SATISFIES_EXPRESSION, node, sizeof(TsTypeExprNode));
        uint32_t cc = ts_node_named_child_count(node);
        if (cc >= 2) {
            sat_node->inner = build_ts_expression(tp, ts_node_named_child(node, 0));
            sat_node->target_type = build_ts_type_expr(tp, ts_node_named_child(node, 1));
        }
        return (JsAstNode*)sat_node;
    }
    if (strcmp(type_str, "non_null_expression") == 0) {
        TsNonNullNode* nn = (TsNonNullNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_NON_NULL_EXPRESSION, node, sizeof(TsNonNullNode));
        if (ts_node_named_child_count(node) > 0) {
            nn->inner = build_ts_expression(tp, ts_node_named_child(node, 0));
        }
        return (JsAstNode*)nn;
    }

    // delegate to JS expression builder
    return build_js_expression((JsTranspiler*)tp, node);
}

// Override hook called from build_js_expression for unknown node types.
// Only handles TS-specific expressions; returns NULL for JS nodes.
JsAstNode* ts_expr_override(void* tp_void, TSNode node) {
    TsTranspiler* tp = (TsTranspiler*)tp_void;
    const char* type_str = ts_node_type(node);

    if (strcmp(type_str, "as_expression") == 0) {
        TsTypeExprNode* as_node = (TsTypeExprNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_AS_EXPRESSION, node, sizeof(TsTypeExprNode));
        uint32_t cc = ts_node_named_child_count(node);
        if (cc >= 2) {
            as_node->inner = build_ts_expression(tp, ts_node_named_child(node, 0));
            as_node->target_type = build_ts_type_expr(tp, ts_node_named_child(node, 1));
        }
        return (JsAstNode*)as_node;
    }
    if (strcmp(type_str, "satisfies_expression") == 0) {
        TsTypeExprNode* sat_node = (TsTypeExprNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_SATISFIES_EXPRESSION, node, sizeof(TsTypeExprNode));
        uint32_t cc = ts_node_named_child_count(node);
        if (cc >= 2) {
            sat_node->inner = build_ts_expression(tp, ts_node_named_child(node, 0));
            sat_node->target_type = build_ts_type_expr(tp, ts_node_named_child(node, 1));
        }
        return (JsAstNode*)sat_node;
    }
    if (strcmp(type_str, "non_null_expression") == 0) {
        TsNonNullNode* nn = (TsNonNullNode*)alloc_ts_ast_node(tp,
            TS_AST_NODE_NON_NULL_EXPRESSION, node, sizeof(TsNonNullNode));
        if (ts_node_named_child_count(node) > 0) {
            nn->inner = build_ts_expression(tp, ts_node_named_child(node, 0));
        }
        return (JsAstNode*)nn;
    }

    // not a TS-specific expression — let JS builder handle it
    return NULL;
}

// ============================================================================
// TS function builder — handles required_parameter / optional_parameter
// ============================================================================

// Build a single TS parameter (required_parameter or optional_parameter)
static JsAstNode* build_ts_parameter(TsTranspiler* tp, TSNode param_node, bool is_optional) {
    const char* ptype = ts_node_type(param_node);

    // rest_pattern: ...args (same in TS and JS grammars)
    if (strcmp(ptype, "rest_pattern") == 0) {
        JsSpreadElementNode* rest = (JsSpreadElementNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_REST_ELEMENT, param_node, sizeof(JsSpreadElementNode));
        if (ts_node_named_child_count(param_node) > 0) {
            TSNode inner = ts_node_named_child(param_node, 0);
            rest->argument = build_js_expression((JsTranspiler*)tp, inner);
        }
        rest->base.type = &TYPE_ARRAY;
        return (JsAstNode*)rest;
    }

    // plain identifier (single-param arrow: x => ...) — no type annotation
    if (strcmp(ptype, "identifier") == 0) {
        return build_js_identifier((JsTranspiler*)tp, param_node);
    }

    // required_parameter or optional_parameter from the TS grammar
    // Fields: name (pattern), type (type_annotation), value (default)
    if (strcmp(ptype, "required_parameter") == 0 || strcmp(ptype, "optional_parameter") == 0) {

        // extract the name/pattern via "pattern" field first, fallback to "name" field
        TSNode name_node = ts_node_child_by_field_name(param_node, "pattern", 7);
        if (ts_node_is_null(name_node)) {
            name_node = ts_node_child_by_field_name(param_node, "name", 4);
        }

        // extract type annotation — build it for type registry but don't use for codegen yet
        TSNode type_node = ts_node_child_by_field_name(param_node, "type", 4);
        if (!ts_node_is_null(type_node)) {
            build_ts_type_annotation(tp, type_node);
        }

        // extract default value (optional)
        TSNode value_node = ts_node_child_by_field_name(param_node, "value", 5);
        JsAstNode* default_value = NULL;
        if (!ts_node_is_null(value_node)) {
            default_value = build_ts_expression(tp, value_node);
        }

        // build the name/pattern as an identifier
        JsAstNode* name_ast = NULL;
        if (!ts_node_is_null(name_node)) {
            const char* ntype = ts_node_type(name_node);
            if (strcmp(ntype, "identifier") == 0) {
                name_ast = build_js_identifier((JsTranspiler*)tp, name_node);
            } else {
                name_ast = build_ts_expression(tp, name_node);
            }
        }

        // if there's a default value, wrap in assignment_pattern
        if (default_value && name_ast) {
            JsAssignmentPatternNode* assign = (JsAssignmentPatternNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_ASSIGNMENT_PATTERN, param_node, sizeof(JsAssignmentPatternNode));
            assign->left = name_ast;
            assign->right = default_value;
            assign->base.type = &TYPE_ANY;
            return (JsAstNode*)assign;
        }

        return name_ast;
    }

    // assignment_pattern: param = defaultValue (can appear in TS formal_parameters too)
    if (strcmp(ptype, "assignment_pattern") == 0) {
        return build_js_expression((JsTranspiler*)tp, param_node);
    }

    // fallback: delegate to JS expression builder
    return build_js_expression((JsTranspiler*)tp, param_node);
}

// Build a function from the TS parser CST — handles TS-specific parameter nodes
static JsAstNode* build_ts_function(TsTranspiler* tp, TSNode func_node) {
    const char* node_type = ts_node_type(func_node);
    bool is_arrow = (strcmp(node_type, "arrow_function") == 0);
    bool is_generator = (strcmp(node_type, "generator_function") == 0 ||
                         strcmp(node_type, "generator_function_declaration") == 0);
    if (strcmp(node_type, "method_definition") == 0 && !is_generator) {
        uint32_t ccount = ts_node_child_count(func_node);
        for (uint32_t ci = 0; ci < ccount; ci++) {
            TSNode child = ts_node_child(func_node, ci);
            const char* ctype = ts_node_type(child);
            if (strcmp(ctype, "*") == 0) { is_generator = true; break; }
            if (strcmp(ctype, "formal_parameters") == 0 || strcmp(ctype, "statement_block") == 0) break;
        }
    }

    bool is_expression = is_arrow || (strcmp(node_type, "function_expression") == 0) ||
                         strcmp(node_type, "generator_function") == 0;

    JsAstNodeType ast_type = is_arrow ? JS_AST_NODE_ARROW_FUNCTION :
                             is_expression ? JS_AST_NODE_FUNCTION_EXPRESSION :
                             JS_AST_NODE_FUNCTION_DECLARATION;

    // allocate TsFunctionNode (extends JsFunctionNode with return_type, type_params)
    TsFunctionNode* ts_func = (TsFunctionNode*)alloc_ts_ast_node(tp,
        ast_type, func_node, sizeof(TsFunctionNode));
    JsFunctionNode* func = &ts_func->base;

    func->is_arrow = is_arrow;
    func->is_generator = is_generator;

    // detect async
    func->is_async = false;
    {
        uint32_t ccount = ts_node_child_count(func_node);
        for (uint32_t ci = 0; ci < ccount; ci++) {
            TSNode child = ts_node_child(func_node, ci);
            const char* ctype = ts_node_type(child);
            if (strcmp(ctype, "async") == 0) { func->is_async = true; break; }
            if (strcmp(ctype, "function") == 0 || strcmp(ctype, "=>") == 0) break;
        }
    }

    // function name (optional for expressions)
    TSNode name_node = ts_node_child_by_field_name(func_node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        int len;
        const char* text = ts_node_text(tp, name_node, &len);
        func->name = name_pool_create_strview(tp->name_pool, {.str = text, .length = (size_t)len});
    }

    // return type annotation (TS-specific)
    TSNode return_type_node = ts_node_child_by_field_name(func_node, "return_type", 11);
    if (!ts_node_is_null(return_type_node)) {
        ts_func->return_type = (TsTypeAnnotationNode*)build_ts_type_annotation(tp, return_type_node);
    }

    // type parameters (TS-specific: <T, U>)
    TSNode type_params_node = ts_node_child_by_field_name(func_node, "type_parameters", 15);
    if (!ts_node_is_null(type_params_node)) {
        uint32_t tp_count = ts_node_named_child_count(type_params_node);
        ts_func->type_params = (TsTypeParamNode**)pool_alloc(tp->ast_pool,
            sizeof(TsTypeParamNode*) * tp_count);
        ts_func->type_param_count = (int)tp_count;
        for (uint32_t i = 0; i < tp_count; i++) {
            TSNode tpn = ts_node_named_child(type_params_node, i);
            TsTypeParamNode* tpp = (TsTypeParamNode*)alloc_ts_ast_node(tp,
                TS_AST_NODE_TYPE_PARAMETER, tpn, sizeof(TsTypeParamNode));
            // get name
            if (ts_node_named_child_count(tpn) > 0) {
                TSNode name = ts_node_named_child(tpn, 0);
                int nlen;
                const char* ntext = ts_node_text(tp, name, &nlen);
                tpp->name = ts_pool_string(tp, ntext, nlen);
            }
            ts_func->type_params[i] = tpp;
        }
    }

    // parameters — TS parser produces required_parameter / optional_parameter
    TSNode params_node = ts_node_child_by_field_name(func_node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
        uint32_t param_count = ts_node_named_child_count(params_node);
        JsAstNode* prev_param = NULL;

        for (uint32_t i = 0; i < param_count; i++) {
            TSNode param_node = ts_node_named_child(params_node, i);
            JsAstNode* param = build_ts_parameter(tp, param_node, false);

            if (param) {
                if (!prev_param) {
                    func->params = param;
                } else {
                    prev_param->next = param;
                }
                prev_param = param;
            }
        }
    } else {
        // single parameter (arrow function without parens: x => x * 2)
        TSNode param_node = ts_node_child_by_field_name(func_node, "parameter", 9);
        if (!ts_node_is_null(param_node)) {
            func->params = build_ts_parameter(tp, param_node, false);
        }
    }

    // function body
    TSNode body_node = ts_node_child_by_field_name(func_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        const char* body_type = ts_node_type(body_node);
        if (strcmp(body_type, "statement_block") == 0) {
            func->body = build_js_block_statement((JsTranspiler*)tp, body_node);
        } else {
            // arrow function with expression body
            func->body = build_ts_expression(tp, body_node);
        }
    }

    func->base.type = &TYPE_FUNC;

    // add function to scope if named (not method definitions)
    bool is_method_def = (strcmp(node_type, "method_definition") == 0);
    if (func->name && !is_method_def) {
        js_scope_define((JsTranspiler*)tp, func->name, (JsAstNode*)func, JS_VAR_VAR);
    }

    return (JsAstNode*)func;
}

// ============================================================================
// TS class declaration builder — handles constructor parameter properties
// ============================================================================

// helper: create an identifier AST node with a given name
static JsAstNode* make_ts_identifier(TsTranspiler* tp, TSNode node, const char* name, int len) {
    JsIdentifierNode* id = (JsIdentifierNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_IDENTIFIER, node, sizeof(JsIdentifierNode));
    id->name = name_pool_create_len(tp->name_pool, name, len);
    id->base.type = &TYPE_ANY;
    return (JsAstNode*)id;
}

// helper: create `this.name = name;` expression statement
static JsAstNode* make_this_assignment(TsTranspiler* tp, TSNode node, const char* name, int len) {
    // this.name
    JsMemberNode* member = (JsMemberNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_MEMBER_EXPRESSION, node, sizeof(JsMemberNode));
    member->object = make_ts_identifier(tp, node, "this", 4);
    member->property = make_ts_identifier(tp, node, name, len);
    member->computed = false;
    member->optional = false;
    member->base.type = &TYPE_ANY;

    // this.name = name
    JsAssignmentNode* assign = (JsAssignmentNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_ASSIGNMENT_EXPRESSION, node, sizeof(JsAssignmentNode));
    assign->op = JS_OP_ASSIGN;
    assign->left = (JsAstNode*)member;
    assign->right = make_ts_identifier(tp, node, name, len);
    assign->base.type = &TYPE_ANY;

    // expression statement wrapping the assignment
    JsExpressionStatementNode* expr_stmt = (JsExpressionStatementNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_EXPRESSION_STATEMENT, node, sizeof(JsExpressionStatementNode));
    expr_stmt->expression = (JsAstNode*)assign;
    expr_stmt->base.type = &TYPE_NULL;

    return (JsAstNode*)expr_stmt;
}

// Build class body with constructor param property desugaring.
// For constructor params with accessibility modifiers (public/private/protected),
// inject `this.param = param` assignments at the beginning of the constructor body.
static JsAstNode* build_ts_class_body(TsTranspiler* tp, TSNode body_node) {
    JsBlockNode* body = (JsBlockNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_BLOCK_STATEMENT, body_node, sizeof(JsBlockNode));

    uint32_t child_count = ts_node_named_child_count(body_node);
    JsAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child_node = ts_node_named_child(body_node, i);
        const char* child_type = ts_node_type(child_node);

        JsAstNode* member = NULL;
        if (strcmp(child_type, "field_definition") == 0) {
            // delegate to JS field builder (TS field_definition may have type annotation
            // but layout is the same)
            member = build_js_field_definition((JsTranspiler*)tp, child_node);
        } else if (strcmp(child_type, "method_definition") == 0) {
            // build the method with TS parameter handling
            JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_METHOD_DEFINITION, child_node, sizeof(JsMethodDefinitionNode));
            method->computed = false;
            method->static_method = false;
            method->kind = JsMethodDefinitionNode::JS_METHOD_METHOD;

            // detect static, get, set
            uint32_t cc = ts_node_child_count(child_node);
            for (uint32_t ci = 0; ci < cc; ci++) {
                TSNode ch = ts_node_child(child_node, ci);
                const char* ct = ts_node_type(ch);
                if (strcmp(ct, "static") == 0) method->static_method = true;
                else if (strcmp(ct, "get") == 0) method->kind = JsMethodDefinitionNode::JS_METHOD_GET;
                else if (strcmp(ct, "set") == 0) method->kind = JsMethodDefinitionNode::JS_METHOD_SET;
            }

            // get method key
            TSNode key_node = ts_node_child_by_field_name(child_node, "name", 4);
            if (!ts_node_is_null(key_node)) {
                const char* key_type = ts_node_type(key_node);
                if (strcmp(key_type, "computed_property_name") == 0) {
                    method->computed = true;
                }
                method->key = build_js_expression((JsTranspiler*)tp, key_node);

                // detect constructor
                int klen;
                const char* ktext = ts_node_text(tp, key_node, &klen);
                if (klen == 11 && memcmp(ktext, "constructor", 11) == 0) {
                    method->kind = JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR;
                }
            }

            // build function value using TS function builder (handles TS params)
            method->value = build_ts_function(tp, child_node);

            // constructor parameter property desugaring:
            // for each param with accessibility modifier, prepend `this.x = x;` to body
            if (method->kind == JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR && method->value) {
                JsFunctionNode* ctor_fn = (JsFunctionNode*)method->value;

                // scan the CST params for accessibility modifiers
                TSNode params_node = ts_node_child_by_field_name(child_node, "parameters", 10);
                if (!ts_node_is_null(params_node)) {
                    // collect param property assignments to prepend
                    JsAstNode* assign_first = NULL;
                    JsAstNode* assign_last = NULL;

                    uint32_t param_count = ts_node_named_child_count(params_node);
                    for (uint32_t pi = 0; pi < param_count; pi++) {
                        TSNode param_cst = ts_node_named_child(params_node, pi);
                        const char* ptype = ts_node_type(param_cst);

                        if (strcmp(ptype, "required_parameter") == 0 ||
                            strcmp(ptype, "optional_parameter") == 0) {
                            // check for accessibility_modifier among children
                            uint32_t pcc = ts_node_named_child_count(param_cst);
                            bool has_accessibility = false;
                            for (uint32_t pci = 0; pci < pcc; pci++) {
                                TSNode pc = ts_node_named_child(param_cst, pci);
                                if (strcmp(ts_node_type(pc), "accessibility_modifier") == 0) {
                                    has_accessibility = true;
                                    break;
                                }
                            }
                            // also check for 'readonly' without accessibility
                            if (!has_accessibility) {
                                uint32_t ptc = ts_node_child_count(param_cst);
                                for (uint32_t pci = 0; pci < ptc; pci++) {
                                    TSNode pc = ts_node_child(param_cst, pci);
                                    int rlen;
                                    const char* rtxt = ts_node_text(tp, pc, &rlen);
                                    if (rlen == 8 && memcmp(rtxt, "readonly", 8) == 0) {
                                        has_accessibility = true;
                                        break;
                                    }
                                }
                            }

                            if (has_accessibility) {
                                // get the parameter name
                                TSNode pname = ts_node_child_by_field_name(param_cst, "pattern", 7);
                                if (ts_node_is_null(pname)) {
                                    pname = ts_node_child_by_field_name(param_cst, "name", 4);
                                }
                                if (!ts_node_is_null(pname)) {
                                    int nlen;
                                    const char* ntext = ts_node_text(tp, pname, &nlen);
                                    JsAstNode* assign_stmt = make_this_assignment(tp, pname, ntext, nlen);
                                    if (!assign_first) {
                                        assign_first = assign_stmt;
                                        assign_last = assign_stmt;
                                    } else {
                                        assign_last->next = assign_stmt;
                                        assign_last = assign_stmt;
                                    }
                                }
                            }
                        }
                    }

                    // prepend assignments to constructor body
                    if (assign_first && ctor_fn->body) {
                        if (ctor_fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                            JsBlockNode* block = (JsBlockNode*)ctor_fn->body;
                            assign_last->next = block->statements;
                            block->statements = assign_first;
                        }
                    }
                }
            }

            method->base.type = &TYPE_FUNC;
            member = (JsAstNode*)method;
        } else {
            // other class body members — delegate to JS
            member = build_js_method_definition((JsTranspiler*)tp, child_node);
        }

        if (member) {
            if (!prev) {
                body->statements = member;
            } else {
                prev->next = member;
            }
            prev = member;
        }
    }

    body->base.type = &TYPE_NULL;
    return (JsAstNode*)body;
}

static JsAstNode* build_ts_class_declaration(TsTranspiler* tp, TSNode class_node) {
    // collect decorators (children of class_declaration in the TS parser)
    TsDecoratorNode* decorators[16];
    int deco_count = 0;
    uint32_t child_count = ts_node_named_child_count(class_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(class_node, i);
        if (strcmp(ts_node_type(child), "decorator") == 0 && deco_count < 16) {
            TsDecoratorNode* dec = (TsDecoratorNode*)alloc_ts_ast_node(tp,
                TS_AST_NODE_DECORATOR, child, sizeof(TsDecoratorNode));
            if (ts_node_named_child_count(child) > 0) {
                dec->expression = build_ts_expression(tp, ts_node_named_child(child, 0));
            }
            decorators[deco_count++] = dec;
        }
    }

    JsClassNode* class_decl = (JsClassNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_CLASS_DECLARATION, class_node, sizeof(JsClassNode));

    // get class name — TS parser uses type_identifier for class names
    TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        int len;
        const char* text = ts_node_text(tp, name_node, &len);
        class_decl->name = name_pool_create_len(tp->name_pool, text, len);
    }

    // get superclass (optional) — inside class_heritage
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(class_node, i);
        if (strcmp(ts_node_type(child), "class_heritage") == 0) {
            TSNode super_expr = ts_node_named_child(child, 0);
            if (!ts_node_is_null(super_expr)) {
                class_decl->superclass = build_js_expression((JsTranspiler*)tp, super_expr);
            }
            break;
        }
    }

    // get class body — use TS-aware builder for constructor param properties
    TSNode body_node = ts_node_child_by_field_name(class_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        class_decl->body = build_ts_class_body(tp, body_node);
    }

    class_decl->base.type = &TYPE_FUNC;

    // add class to scope
    if (class_decl->name) {
        js_scope_define((JsTranspiler*)tp, class_decl->name, (JsAstNode*)class_decl, JS_VAR_VAR);
    }

    // if decorators found, store them on the class for lowering phase
    if (deco_count > 0) {
        // store decorator count and pointers in a linked list via TsDecoratorNode.base.next
        for (int i = 0; i < deco_count - 1; i++) {
            decorators[i]->base.next = (JsAstNode*)decorators[i + 1];
        }
        decorators[deco_count - 1]->base.next = NULL;
        // prepend decorators to the class body linked list as a marker
        // the strip phase will detect and apply them
        JsAstNode* result = (JsAstNode*)decorators[0];
        decorators[deco_count - 1]->base.next = (JsAstNode*)class_decl;
        return result;
    }

    return (JsAstNode*)class_decl;
}

// ============================================================================
// TS variable declaration builder — handles type_annotation on variable_declarator
// ============================================================================

static JsAstNode* build_ts_variable_declaration(TsTranspiler* tp, TSNode var_node) {
    JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_VARIABLE_DECLARATION, var_node, sizeof(JsVariableDeclarationNode));

    // determine variable kind (var, let, const)
    TSNode first_child = ts_node_child(var_node, 0);
    int flen;
    const char* ftext = ts_node_text(tp, first_child, &flen);
    if (flen >= 3 && memcmp(ftext, "var", 3) == 0) {
        var_decl->kind = JS_VAR_VAR;
    } else if (flen >= 3 && memcmp(ftext, "let", 3) == 0) {
        var_decl->kind = JS_VAR_LET;
    } else if (flen >= 5 && memcmp(ftext, "const", 5) == 0) {
        var_decl->kind = JS_VAR_CONST;
    }

    // build declarators
    uint32_t child_count = ts_node_named_child_count(var_node);
    JsAstNode* prev_declarator = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode declarator_node = ts_node_named_child(var_node, i);
        const char* declarator_type = ts_node_type(declarator_node);

        if (strcmp(declarator_type, "variable_declarator") != 0) continue;

        JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_VARIABLE_DECLARATOR, declarator_node, sizeof(JsVariableDeclaratorNode));

        // get name via field
        TSNode name_node = ts_node_child_by_field_name(declarator_node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            const char* id_type = ts_node_type(name_node);
            if (strcmp(id_type, "array_pattern") == 0 || strcmp(id_type, "object_pattern") == 0) {
                declarator->id = build_js_expression((JsTranspiler*)tp, name_node);
            } else {
                declarator->id = build_js_identifier((JsTranspiler*)tp, name_node);
            }
        }

        // get type annotation (TS-specific field "type") — preserve but don't
        // override base.type yet (Phase 2 will use resolved types for codegen)
        TSNode type_node = ts_node_child_by_field_name(declarator_node, "type", 4);
        if (!ts_node_is_null(type_node)) {
            // build and resolve the type annotation for later use
            build_ts_type_annotation(tp, type_node);
        }

        // get initializer via field "value"
        TSNode value_node = ts_node_child_by_field_name(declarator_node, "value", 5);
        if (!ts_node_is_null(value_node)) {
            declarator->init = build_ts_expression(tp, value_node);
            if (declarator->init) {
                declarator->base.type = declarator->init->type;
            } else {
                declarator->base.type = &TYPE_ANY;
            }
        } else {
            declarator->init = NULL;
            declarator->base.type = &TYPE_NULL;
        }

        // add to scope
        if (declarator->id && declarator->id->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)declarator->id;
            js_scope_define((JsTranspiler*)tp, id->name, (JsAstNode*)declarator, (JsVarKind)var_decl->kind);
        }

        if (!prev_declarator) {
            var_decl->declarations = (JsAstNode*)declarator;
        } else {
            prev_declarator->next = (JsAstNode*)declarator;
        }
        prev_declarator = (JsAstNode*)declarator;
    }

    var_decl->base.type = &TYPE_NULL;
    return (JsAstNode*)var_decl;
}

// ============================================================================
// TS namespace declaration builder
// ============================================================================

static JsAstNode* build_ts_namespace_declaration(TsTranspiler* tp, TSNode node) {
    TsNamespaceDeclarationNode* ns = (TsNamespaceDeclarationNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_NAMESPACE_DECLARATION, node, sizeof(TsNamespaceDeclarationNode));

    // get namespace name
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        int len;
        const char* text = ts_node_text(tp, name_node, &len);
        ns->name = name_pool_create_len(tp->name_pool, text, len);
    }

    // build body statements from the statement_block
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        uint32_t child_count = ts_node_named_child_count(body_node);
        if (child_count > 0) {
            ns->body = (JsAstNode**)pool_alloc(tp->ast_pool, sizeof(JsAstNode*) * child_count);
            ns->body_count = 0;
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_named_child(body_node, i);
                JsAstNode* stmt = build_ts_node(tp, child);
                if (stmt) {
                    ns->body[ns->body_count++] = stmt;
                }
            }
        }
    }

    return (JsAstNode*)ns;
}

// ============================================================================
// TS decorator builder
// ============================================================================

static JsAstNode* build_ts_decorator(TsTranspiler* tp, TSNode node) {
    TsDecoratorNode* dec = (TsDecoratorNode*)alloc_ts_ast_node(tp,
        TS_AST_NODE_DECORATOR, node, sizeof(TsDecoratorNode));

    // the decorator expression is the first named child (identifier, call, member)
    if (ts_node_named_child_count(node) > 0) {
        TSNode expr_node = ts_node_named_child(node, 0);
        dec->expression = build_ts_expression(tp, expr_node);
    }

    return (JsAstNode*)dec;
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
    if (strcmp(type_str, "class_declaration") == 0 || strcmp(type_str, "class") == 0)
        return build_ts_class_declaration(tp, node);
    if (strcmp(type_str, "ambient_declaration") == 0) {
        // declare ... — parse for type info, no code emitted
        // for now, skip
        return NULL;
    }
    if (strcmp(type_str, "internal_module") == 0 ||
        strcmp(type_str, "module") == 0) {
        return build_ts_namespace_declaration(tp, node);
    }
    if (strcmp(type_str, "decorator") == 0)
        return build_ts_decorator(tp, node);

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

    // TS function declarations — must be intercepted because the TS parser produces
    // required_parameter / optional_parameter instead of plain identifiers
    if (strcmp(type_str, "function_declaration") == 0 ||
        strcmp(type_str, "generator_function_declaration") == 0 ||
        strcmp(type_str, "function_expression") == 0 ||
        strcmp(type_str, "generator_function") == 0 ||
        strcmp(type_str, "arrow_function") == 0 ||
        strcmp(type_str, "method_definition") == 0) {
        return build_ts_function(tp, node);
    }

    // TS variable declarations — must be intercepted because the TS parser's
    // variable_declarator has a "type" field for type annotations
    if (strcmp(type_str, "variable_declaration") == 0 ||
        strcmp(type_str, "lexical_declaration") == 0) {
        return build_ts_variable_declaration(tp, node);
    }

    // expression_statement wrapping internal_module (namespace) — the TS parser
    // wraps namespace in expression_statement; intercept and unwrap it
    if (strcmp(type_str, "expression_statement") == 0) {
        uint32_t cc = ts_node_named_child_count(node);
        if (cc > 0) {
            TSNode inner = ts_node_named_child(node, 0);
            const char* inner_type = ts_node_type(inner);
            if (strcmp(inner_type, "internal_module") == 0 ||
                strcmp(inner_type, "module") == 0) {
                return build_ts_namespace_declaration(tp, inner);
            }
        }
    }

    // export_statement — unwrap and build the inner declaration
    if (strcmp(type_str, "export_statement") == 0) {
        uint32_t cc = ts_node_named_child_count(node);
        JsAstNode* inner_result = NULL;
        for (uint32_t i = 0; i < cc; i++) {
            TSNode inner = ts_node_named_child(node, i);
            const char* inner_type = ts_node_type(inner);
            // skip 'export' keyword, '*', specifiers — just build the declaration
            if (strcmp(inner_type, "function_declaration") == 0 ||
                strcmp(inner_type, "lexical_declaration") == 0 ||
                strcmp(inner_type, "variable_declaration") == 0 ||
                strcmp(inner_type, "class_declaration") == 0 ||
                strcmp(inner_type, "interface_declaration") == 0 ||
                strcmp(inner_type, "type_alias_declaration") == 0 ||
                strcmp(inner_type, "enum_declaration") == 0) {
                inner_result = build_ts_node(tp, inner);
                break;
            }
        }
        if (inner_result) {
            // wrap in export node so namespace lowering can detect it
            JsExportNode* export_node = (JsExportNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_EXPORT_DECLARATION, node, sizeof(JsExportNode));
            export_node->declaration = inner_result;
            return (JsAstNode*)export_node;
        }
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
