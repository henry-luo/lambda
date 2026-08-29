// js_parser_compare.cpp — pointer-free JS/TS AST and fact snapshots.
//
// The reference and direct parsers allocate in separate pools, so pointer
// identity cannot be evidence of parity. This serializer records source spans,
// semantic payloads, child order, scopes, bindings, and indexed facts only.

#include "js_transpiler.hpp"
#include "../ts/ts_ast.hpp"
#include "../ts/ts_transpiler.hpp"
#include "../runtime/type_contract.hpp"
#include "../../lib/arraylist.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"

#include <cstring>

#ifdef JS_C_PRODUCTION
bool js_transpiler_parse_compare(JsTranspiler* tp, const char* source,
                                 size_t length) {
    (void)tp;
    (void)source;
    (void)length;
    return false;
}
bool js_transpiler_parse_compare_mode(JsTranspiler* tp, const char* source,
        size_t length, JsParseMode mode) {
    (void)tp;
    (void)source;
    (void)length;
    (void)mode;
    return false;
}
#else
extern "C" const TSLanguage* tree_sitter_typescript(void);

namespace {

struct CanonicalContext {
    JsTranspiler* transpiler;
    StrBuf* output;
};

static void append_bool(StrBuf* output, const char* name, bool value) {
    strbuf_append_format(output, " %s=%d", name, value ? 1 : 0);
}

static void append_string(StrBuf* output, const char* name, const char* value,
                          size_t length) {
    strbuf_append_format(output, " %s=\"", name);
    if (value) {
        for (size_t i = 0; i < length; i++) {
            unsigned char ch = (unsigned char)value[i];
            switch (ch) {
            case '\\': strbuf_append_str(output, "\\\\"); break;
            case '"': strbuf_append_str(output, "\\\""); break;
            case '\n': strbuf_append_str(output, "\\n"); break;
            case '\r': strbuf_append_str(output, "\\r"); break;
            case '\t': strbuf_append_str(output, "\\t"); break;
            default:
                if (ch < 0x20) {
                    strbuf_append_format(output, "\\x%02x", ch);
                } else {
                    strbuf_append_char(output, (char)ch);
                }
                break;
            }
        }
    }
    strbuf_append_char(output, '"');
}

static void append_name(StrBuf* output, const char* name, String* value) {
    append_string(output, name, value ? value->chars : NULL,
        value ? value->len : 0);
}

static void append_span(StrBuf* output, SourceSpan span) {
    strbuf_append_format(output, " span=%u:%u", span.start_byte, span.end_byte);
}

static void append_type(StrBuf* output, const char* name, Type* type) {
    if (!type) {
        strbuf_append_format(output, " %s=-", name);
        return;
    }
    char buffer[256];
    lambda_type_format_name(type, buffer, sizeof(buffer));
    append_string(output, name, buffer, strlen(buffer));
}

static void append_source_view(StrBuf* output, const char* name, StrView value) {
    append_string(output, name, value.str, value.length);
}

static int compare_scope_entries(ArrayListValue lhs_value,
        ArrayListValue rhs_value) {
    const NameEntry* lhs = (const NameEntry*)lhs_value;
    const NameEntry* rhs = (const NameEntry*)rhs_value;
    SourceSpan lhs_span = lhs && lhs->node ? lhs->node->source_span
        : (SourceSpan){0, 0};
    SourceSpan rhs_span = rhs && rhs->node ? rhs->node->source_span
        : (SourceSpan){0, 0};
    if (lhs_span.start_byte != rhs_span.start_byte) {
        return lhs_span.start_byte < rhs_span.start_byte ? -1 : 1;
    }
    if (lhs_span.end_byte != rhs_span.end_byte) {
        return lhs_span.end_byte < rhs_span.end_byte ? -1 : 1;
    }
    JsAstNodeType lhs_kind = lhs && lhs->node ? lhs->node->node_type
        : (JsAstNodeType)-1;
    JsAstNodeType rhs_kind = rhs && rhs->node ? rhs->node->node_type
        : (JsAstNodeType)-1;
    if (lhs_kind != rhs_kind) return lhs_kind < rhs_kind ? -1 : 1;
    const char* lhs_name = lhs && lhs->name ? lhs->name->chars : "";
    const char* rhs_name = rhs && rhs->name ? rhs->name->chars : "";
    return strcmp(lhs_name, rhs_name);
}

static void append_scope_entry(StrBuf* output, NameEntry* entry, bool* first) {
    if (!entry || !first) return;
    if (!*first) strbuf_append_char(output, ';');
    *first = false;
    strbuf_append_char(output, '{');
    append_name(output, "name", entry->name);
    if (entry->node) {
        strbuf_append_format(output, " decl_kind=%d", (int)entry->node->node_type);
        append_span(output, entry->node->source_span);
    } else {
        strbuf_append_str(output, " decl_kind=- span=-");
    }
    append_bool(output, "mutable", entry->is_mutable);
    append_bool(output, "param", entry->is_var_param);
    append_bool(output, "proc_param", entry->is_proc_param);
    append_bool(output, "annotation", entry->has_type_annotation);
    append_bool(output, "widened", entry->type_widened);
    append_bool(output, "lexical", entry->is_lexical);
    append_bool(output, "for_head", entry->is_for_in_head);
    append_bool(output, "const", entry->is_const);
    append_bool(output, "tdz", entry->tdz_active);
    append_bool(output, "exported", entry->is_exported);
    strbuf_append_format(output, " slot=%d storage=%d assigned=%d",
        entry->slot, (int)entry->binding_storage,
        entry->storage_assigned ? 1 : 0);
    append_type(output, "declared", entry->declared_type);
    strbuf_append_char(output, '}');
}

static void append_scope_chain(StrBuf* output, NameScope* scope) {
    strbuf_append_str(output, " scope=");
    if (!scope) {
        strbuf_append_char(output, '-');
        return;
    }
    strbuf_append_char(output, '[');
    NameScope* chain[64];
    int count = 0;
    for (NameScope* current = scope; current && count < 64;
            current = current->parent) {
        chain[count++] = current;
    }
    for (int index = count - 1; index >= 0; index--) {
        NameScope* current = chain[index];
        if (index != count - 1) strbuf_append_char(output, ',');
        strbuf_append_format(output, "%d/%d/%d", (int)current->kind,
            current->strict ? 1 : 0, current->is_proc ? 1 : 0);
    }
    strbuf_append_char(output, ']');
    strbuf_append_str(output, " bindings=[");
    // Scope insertion order differs between the reference adapter's
    // predeclaration walk and the direct parser's reconstruction pass, while
    // the binding facts themselves are the same. Canonical snapshots sort by
    // declaration identity so parity checks compare semantics, not pass order.
    ArrayList* entries = arraylist_new(0);
    if (entries) {
        for (NameEntry* entry = scope->first; entry; entry = entry->next) {
            if (!arraylist_append(entries, entry)) break;
        }
        arraylist_sort(entries, compare_scope_entries);
    }
    bool first = true;
    int entry_count = entries ? arraylist_length(entries) : 0;
    for (int entry_index = 0; entry_index < entry_count; entry_index++) {
        NameEntry* entry = (NameEntry*)arraylist_get(entries, entry_index);
        append_scope_entry(output, entry, &first);
    }
    if (!entries) {
        for (NameEntry* entry = scope->first; entry; entry = entry->next) {
            append_scope_entry(output, entry, &first);
        }
    }
    if (entries) arraylist_free(entries);
    strbuf_append_char(output, ']');
}

static void append_function_attrs(StrBuf* output, JsFunctionNode* function) {
    append_name(output, "name", function->name);
    append_bool(output, "arrow", function->is_arrow);
    append_bool(output, "async", function->is_async);
    append_bool(output, "generator", function->is_generator);
    append_bool(output, "use_strict", function->has_use_strict_directive);
    append_type(output, "return_type", function->ts_return_type
        ? function->ts_return_type->resolved_type : NULL);
    append_scope_chain(output, function->vars);
    if (function->analysis) {
        strbuf_append_format(output, " captures=%d params=%d variants=%d",
            function->analysis->capture_count, function->analysis->param_count,
            function->analysis->variant_count);
    }
}

static void append_node_attrs(CanonicalContext* context, JsAstNode* node) {
    StrBuf* output = context->output;
    append_type(output, "type", node->type);
    switch ((int)node->node_type) {
    case JS_AST_NODE_PROGRAM: {
        JsProgramNode* program = (JsProgramNode*)node;
        append_bool(output, "use_strict", program->has_use_strict_directive);
        append_scope_chain(output, program->global_vars);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT:
        append_scope_chain(output, ((JsBlockNode*)node)->vars);
        break;
    case JS_AST_NODE_IDENTIFIER:
        append_name(output, "name", ((JsIdentifierNode*)node)->name);
        if (((JsIdentifierNode*)node)->entry) {
            NameEntry* entry = ((JsIdentifierNode*)node)->entry;
            strbuf_append_format(output, " binding_kind=%d", entry->node
                ? (int)entry->node->node_type : -1);
            append_span(output, entry->node ? entry->node->source_span
                : (SourceSpan){0, 0});
            append_scope_chain(output, entry->scope);
        } else {
            strbuf_append_str(output, " binding=-");
        }
        break;
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* literal = (JsLiteralNode*)node;
        strbuf_append_format(output, " literal=%d decimal=%d bigint=%d",
            (int)literal->literal_type, literal->has_decimal ? 1 : 0,
            literal->is_bigint ? 1 : 0);
        if (literal->literal_type == JS_LITERAL_STRING) {
            append_name(output, "value", literal->value.string_value);
        } else if (literal->literal_type == JS_LITERAL_BOOLEAN) {
            append_bool(output, "value", literal->value.boolean_value);
        } else if (literal->literal_type == JS_LITERAL_NUMBER) {
            strbuf_append_format(output, " value=%.*g", 17,
                literal->value.number_value);
        }
        append_name(output, "bigint", literal->bigint_str);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* binary = (JsBinaryNode*)node;
        strbuf_append_format(output, " op=%d", (int)binary->op);
        append_source_view(output, "op_text", binary->op_str);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* unary = (JsUnaryNode*)node;
        strbuf_append_format(output, " op=%d", (int)unary->op);
        append_source_view(output, "op_text", unary->op_str);
        append_bool(output, "prefix", unary->prefix);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        JsAssignmentNode* assignment = (JsAssignmentNode*)node;
        strbuf_append_format(output, " op=%d", (int)assignment->op);
        append_bool(output, "lhs_parenthesized", assignment->lhs_is_parenthesized);
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION:
        append_bool(output, "computed", ((JsMemberNode*)node)->computed);
        append_bool(output, "optional", ((JsMemberNode*)node)->optional);
        break;
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        append_bool(output, "pipe_inject", call->pipe_inject);
        append_bool(output, "propagate", call->propagate);
        append_bool(output, "can_raise", call->can_raise);
        append_bool(output, "optional", call->optional);
        append_bool(output, "proc_method", call->is_proc_method);
        break;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION:
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_SEQUENCE_EXPRESSION:
        strbuf_append_format(output, " length=%d", ((JsArrayNode*)node)->length);
        break;
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)node;
        strbuf_append_format(output, " kind=%d using=%d await_using=%d",
            declaration->kind, declaration->is_using ? 1 : 0,
            declaration->is_await_using ? 1 : 0);
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* property = (JsPropertyNode*)node;
        append_bool(output, "computed", property->computed);
        append_bool(output, "method", property->method);
        append_bool(output, "getter", property->is_getter);
        append_bool(output, "setter", property->is_setter);
        append_bool(output, "shorthand", property->shorthand);
        break;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        append_function_attrs(output, (JsFunctionNode*)node);
        break;
    case JS_AST_NODE_METHOD_DEFINITION: {
        JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)node;
        append_function_attrs(output, (JsFunctionNode*)node);
        strbuf_append_format(output, " method_kind=%d", (int)method->kind);
        append_bool(output, "computed", method->computed);
        append_bool(output, "static", method->static_method);
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION: {
        JsClassNode* class_node = (JsClassNode*)node;
        append_name(output, "name", class_node->name);
        append_scope_chain(output, class_node->expression_scope);
        break;
    }
    case JS_AST_NODE_FIELD_DEFINITION: {
        JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)node;
        append_bool(output, "static", field->is_static);
        append_bool(output, "private", field->is_private);
        append_bool(output, "computed", field->computed);
        break;
    }
    case AST_NODE_LOOP:
        append_scope_chain(output, ((AstLoopControlNode*)node)->vars);
        break;
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* loop = (JsForOfNode*)node;
        strbuf_append_format(output, " kind=%d declares=%d await=%d",
            loop->kind, loop->declares_binding ? 1 : 0,
            loop->is_await ? 1 : 0);
        append_scope_chain(output, loop->vars);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE:
        append_scope_chain(output, ((JsCatchNode*)node)->vars);
        break;
    case JS_AST_NODE_SWITCH_STATEMENT:
        strbuf_append_format(output, " arms=%d", ((JsSwitchNode*)node)->arm_count);
        append_scope_chain(output, ((JsSwitchNode*)node)->vars);
        break;
    case JS_AST_NODE_SWITCH_CASE:
        append_bool(output, "body_braced", ((JsSwitchCaseNode*)node)->body_braced);
        break;
    case JS_AST_NODE_BREAK_STATEMENT:
    case JS_AST_NODE_CONTINUE_STATEMENT:
        append_string(output, "label", ((JsBreakContinueNode*)node)->label,
            ((JsBreakContinueNode*)node)->label_len);
        break;
    case JS_AST_NODE_IMPORT_DECLARATION: {
        JsImportNode* import_node = (JsImportNode*)node;
        append_name(output, "source", import_node->source);
        append_name(output, "default", import_node->default_name);
        append_name(output, "namespace", import_node->namespace_name);
        append_name(output, "alias", import_node->alias);
        append_source_view(output, "module", import_node->module);
        append_bool(output, "relative", import_node->is_relative);
        append_bool(output, "cross_lang", import_node->is_cross_lang);
        break;
    }
    case JS_AST_NODE_IMPORT_SPECIFIER: {
        JsImportSpecifierNode* specifier = (JsImportSpecifierNode*)node;
        append_name(output, "local", specifier->local_name);
        append_name(output, "remote", specifier->remote_name);
        break;
    }
    case JS_AST_NODE_EXPORT_DECLARATION: {
        JsExportNode* export_node = (JsExportNode*)node;
        append_name(output, "source", export_node->source);
        append_bool(output, "default", export_node->is_default);
        append_bool(output, "star", export_node->is_star);
        append_bool(output, "namespace", export_node->is_namespace);
        break;
    }
    case JS_AST_NODE_EXPORT_SPECIFIER: {
        JsExportSpecifierNode* specifier = (JsExportSpecifierNode*)node;
        append_name(output, "local", specifier->local_name);
        append_name(output, "export", specifier->export_name);
        break;
    }
    case JS_AST_NODE_REGEX: {
        JsRegexNode* regex = (JsRegexNode*)node;
        append_string(output, "pattern", regex->pattern, regex->pattern_len);
        append_string(output, "flags", regex->flags, regex->flags_len);
        break;
    }
    case JS_AST_NODE_TEMPLATE_ELEMENT: {
        JsTemplateElementNode* element = (JsTemplateElementNode*)node;
        append_name(output, "raw", element->raw);
        append_name(output, "cooked", element->cooked);
        append_bool(output, "tail", element->tail);
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT:
        append_string(output, "label", ((JsLabeledStatementNode*)node)->label,
            ((JsLabeledStatementNode*)node)->label_len);
        break;
    case TS_AST_NODE_TYPE_ANNOTATION:
        append_type(output, "resolved", ((TsTypeAnnotationNode*)node)->resolved_type);
        break;
    case TS_AST_NODE_TYPE_ALIAS: {
        TsTypeAliasNode* alias = (TsTypeAliasNode*)node;
        append_name(output, "name", alias->name);
        strbuf_append_format(output, " type_params=%d", alias->type_param_count);
        append_type(output, "resolved", alias->resolved_type);
        break;
    }
    case TS_AST_NODE_INTERFACE: {
        TsInterfaceNode* interface_node = (TsInterfaceNode*)node;
        append_name(output, "name", interface_node->name);
        strbuf_append_format(output, " type_params=%d extends=%d",
            interface_node->type_param_count, interface_node->extends_count);
        append_type(output, "resolved", interface_node->resolved_type);
        break;
    }
    case TS_AST_NODE_TYPE_PARAMETER: {
        TsTypeParamNode* parameter = (TsTypeParamNode*)node;
        append_name(output, "name", parameter->name);
        break;
    }
    case TS_AST_NODE_PREDEFINED_TYPE:
        strbuf_append_format(output, " predefined=%d",
            (int)((TsPredefinedTypeNode*)node)->predefined_id);
        break;
    case TS_AST_NODE_TYPE_REFERENCE: {
        TsTypeReferenceNode* reference = (TsTypeReferenceNode*)node;
        append_name(output, "name", reference->name);
        strbuf_append_format(output, " args=%d", reference->type_arg_count);
        break;
    }
    case TS_AST_NODE_UNION_TYPE:
        strbuf_append_format(output, " members=%d",
            ((TsUnionTypeNode*)node)->type_count);
        break;
    case TS_AST_NODE_INTERSECTION_TYPE:
        strbuf_append_format(output, " members=%d",
            ((TsIntersectionTypeNode*)node)->type_count);
        break;
    case TS_AST_NODE_ARRAY_TYPE:
        break;
    case TS_AST_NODE_TUPLE_TYPE:
        strbuf_append_format(output, " members=%d",
            ((TsTupleTypeNode*)node)->element_count);
        break;
    case TS_AST_NODE_FUNCTION_TYPE:
        strbuf_append_format(output, " params=%d",
            ((TsFunctionTypeNode*)node)->param_count);
        break;
    case TS_AST_NODE_OBJECT_TYPE:
    case TS_AST_NODE_MAPPED_TYPE:
        strbuf_append_format(output, " members=%d",
            ((TsObjectTypeNode*)node)->member_count);
        break;
    case TS_AST_NODE_AS_EXPRESSION:
    case TS_AST_NODE_SATISFIES_EXPRESSION:
    case TS_AST_NODE_TYPE_ASSERTION:
        break;
    case TS_AST_NODE_NON_NULL_EXPRESSION:
        break;
    case TS_AST_NODE_ENUM_DECLARATION: {
        TsEnumDeclarationNode* enum_node = (TsEnumDeclarationNode*)node;
        append_name(output, "name", enum_node->name);
        append_bool(output, "const", enum_node->is_const);
        strbuf_append_format(output, " members=%d", enum_node->member_count);
        append_type(output, "resolved", enum_node->resolved_type);
        break;
    }
    case TS_AST_NODE_ENUM_MEMBER: {
        TsEnumMemberNode* member = (TsEnumMemberNode*)node;
        append_name(output, "name", member->name);
        strbuf_append_format(output, " auto=%d", member->auto_value);
        break;
    }
    case TS_AST_NODE_NAMESPACE_DECLARATION: {
        TsNamespaceDeclarationNode* namespace_node =
            (TsNamespaceDeclarationNode*)node;
        append_name(output, "name", namespace_node->name);
        strbuf_append_format(output, " members=%d", namespace_node->body_count);
        break;
    }
    case TS_AST_NODE_DECORATOR:
        break;
    case TS_AST_NODE_PARAMETER: {
        TsParameterNode* parameter = (TsParameterNode*)node;
        append_bool(output, "optional", parameter->optional);
        append_bool(output, "readonly", parameter->readonly);
        strbuf_append_format(output, " accessibility=%d", parameter->accessibility);
        break;
    }
    default:
        break;
    }
}

static void append_canonical_node(CanonicalContext* context, JsAstNode* node);

static void append_child(JsAstNode* child, void* opaque) {
    append_canonical_node((CanonicalContext*)opaque, child);
}

static void append_canonical_node(CanonicalContext* context, JsAstNode* node) {
    StrBuf* output = context->output;
    if (!node) {
        strbuf_append_str(output, "null");
        return;
    }
    strbuf_append_format(output, "(node=%d", (int)node->node_type);
    append_span(output, node->source_span);
    append_node_attrs(context, node);
    strbuf_append_str(output, " children=[");
    bool first = true;
    if (node->node_type < JS_AST_NODE_TEMPLATE_LITERAL) {
        // Core rows and their JS-owned TypeScript extensions are distinct
        // ranges; this preserves source order without duplicating template
        // rows handled by js_ast_children.cpp.
        js_ast_visit_children(node, append_child, context);
        js_ast_visit_extension_children((AstNode*)node,
            [](AstNode* child, AstNode* parent, void* opaque) {
                (void)parent;
                CanonicalContext* child_context = (CanonicalContext*)opaque;
                append_canonical_node(child_context, (JsAstNode*)child);
            }, context);
    } else if ((int)node->node_type < (int)TS_AST_NODE_TYPE_ANNOTATION) {
        js_ast_visit_children(node, append_child, context);
    } else {
        js_ast_visit_extension_children((AstNode*)node,
            [](AstNode* child, AstNode* parent, void* opaque) {
                (void)parent;
                CanonicalContext* child_context = (CanonicalContext*)opaque;
                append_canonical_node(child_context, (JsAstNode*)child);
            }, context);
    }
    (void)first;
    strbuf_append_char(output, ']');
    strbuf_append_char(output, ')');
}

static void append_index(CanonicalContext* context) {
    StrBuf* output = context->output;
    JsTranspiler* tp = context->transpiler;
    strbuf_append_format(output, "\n(index count=%u functions=%u)",
        tp->ast_index.count, tp->ast_index.function_count);
    for (uint32_t index = 0; index < tp->ast_index.count; index++) {
        AstNode* node = tp->ast_index.nodes[index];
        AstNode* parent = tp->ast_index.parents[index];
        strbuf_append_format(output, "\n(fact id=%u kind=%d span=%u:%u",
            index, node ? (int)node->node_type : -1,
            node ? node->source_span.start_byte : 0,
            node ? node->source_span.end_byte : 0);
        if (parent) {
            strbuf_append_format(output, " parent=%d:%u:%u", (int)parent->node_type,
                parent->source_span.start_byte, parent->source_span.end_byte);
        } else {
            strbuf_append_str(output, " parent=-");
        }
        if (tp->ast_index.facts && node) {
            AstNodeFacts* facts = &tp->ast_index.facts[index];
            append_type(output, "declared", facts->declared_contract);
            append_type(output, "inferred", facts->inferred_type);
            strbuf_append_format(output, " rep=%d flags=%u folded=%llu",
                (int)facts->representation, facts->flags,
                (unsigned long long)facts->folded_item);
        }
        strbuf_append_char(output, ')');
    }
}

}  // namespace

extern "C" StrBuf* js_ast_canonical_serialize(JsTranspiler* transpiler,
                                               JsAstNode* root) {
    if (!transpiler || !root) return NULL;
    StrBuf* output = strbuf_new();
    if (!output) return NULL;
    CanonicalContext context = {transpiler, output};
    append_canonical_node(&context, root);
    append_index(&context);
    strbuf_append_char(output, '\n');
    return output;
}

static void js_compare_seed_reference(JsTranspiler* reference,
        bool typescript, bool module) {
    if (!reference) return;
    reference->strict_js = !typescript;
    reference->is_module = module;
    reference->is_es_module = module;
    reference->strict_mode = module || typescript;
    if (typescript) {
        const TSLanguage* language = tree_sitter_typescript();
        js_transpiler_reference_set_language(reference, language);
        ts_type_registry_init(reference);
    }
    if (reference->global_scope) {
        reference->global_scope->kind = module ? SCOPE_KIND_MODULE
            : SCOPE_KIND_GLOBAL;
        reference->global_scope->strict = reference->strict_mode;
    }
}

static void js_compare_record_failure(JsTranspiler* tp, const char* message) {
    if (!tp) return;
    tp->has_errors = true;
    tp->parse_error_valid = true;
    tp->parse_error_row = 1;
    tp->parse_error_col = 0;
    strncpy(tp->parse_error_message, message,
        sizeof(tp->parse_error_message) - 1);
    tp->parse_error_message[sizeof(tp->parse_error_message) - 1] = '\0';
}

static bool js_transpiler_parse_compare_impl(JsTranspiler* tp,
        const char* source, size_t length, bool explicit_mode,
        JsParseMode mode) {
    if (!tp || !source) return false;

    bool direct_ok = explicit_mode
        ? js_transpiler_parse_c(tp, source, length, mode)
        : js_transpiler_parse_c_auto(tp, source, length);
    bool typescript = !tp->strict_js;
    bool module = explicit_mode ? (mode & JS_PARSE_MODULE) : tp->is_module;
    JsTranspiler* reference = js_transpiler_create(tp->runtime);
    if (!reference) {
        js_compare_record_failure(tp,
            "JavaScript compare backend could not create reference parser");
        return false;
    }
    js_compare_seed_reference(reference, typescript, module);
    bool reference_ok = js_transpiler_parse_reference(reference, source, length);
    if (!direct_ok || !reference_ok) {
        if (direct_ok != reference_ok) {
            log_error("js-parser-compare: acceptance mismatch direct=%d reference=%d",
                direct_ok ? 1 : 0, reference_ok ? 1 : 0);
            js_compare_record_failure(tp,
                "JavaScript parser backends disagree on source acceptance");
        }
        js_transpiler_destroy(reference);
        return false;
    }

    TSTree* reference_tree = js_transpiler_reference_tree(reference);
    JsAstNode* reference_ast = reference_tree
        ? build_js_ast_indexed(reference, ts_tree_root_node(reference_tree))
        : NULL;
    JsAstNode* direct_ast = tp->ast_root ? (JsAstNode*)tp->ast_root : NULL;
    if (!reference_ast || !direct_ast) {
        log_error("js-parser-compare: AST construction mismatch direct=%d reference=%d",
            direct_ast ? 1 : 0, reference_ast ? 1 : 0);
        js_compare_record_failure(tp,
            "JavaScript parser backends disagree on AST construction");
        js_transpiler_destroy(reference);
        return false;
    }
    if (typescript) {
        ts_resolve_all_types(reference, reference_ast);
        ts_resolve_all_types(tp, direct_ast);
    }

    StrBuf* reference_snapshot = js_ast_canonical_serialize(reference,
        reference_ast);
    StrBuf* direct_snapshot = js_ast_canonical_serialize(tp, direct_ast);
    bool equal = reference_snapshot && direct_snapshot &&
        reference_snapshot->length == direct_snapshot->length &&
        memcmp(reference_snapshot->str, direct_snapshot->str,
            reference_snapshot->length) == 0;
    if (!equal) {
        log_error("js-parser-compare: canonical AST/fact mismatch direct_bytes=%zu reference_bytes=%zu",
            direct_snapshot ? direct_snapshot->length : 0,
            reference_snapshot ? reference_snapshot->length : 0);
        js_compare_record_failure(tp,
            "JavaScript parser backends produced different AST/facts");
    }
    if (reference_snapshot) strbuf_free(reference_snapshot);
    if (direct_snapshot) strbuf_free(direct_snapshot);
    js_transpiler_destroy(reference);
    return equal;
}

bool js_transpiler_parse_compare(JsTranspiler* tp, const char* source,
        size_t length) {
    return js_transpiler_parse_compare_impl(tp, source, length, false,
        JS_PARSE_SCRIPT);
}

bool js_transpiler_parse_compare_mode(JsTranspiler* tp, const char* source,
        size_t length, JsParseMode mode) {
    return js_transpiler_parse_compare_impl(tp, source, length, true, mode);
}
#endif
