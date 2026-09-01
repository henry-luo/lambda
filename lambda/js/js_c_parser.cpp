#include "js_transpiler.hpp"
#include "js_c_ast_helpers.hpp"
#include "../ts/ts_ast.hpp"
#include "../ts/ts_type_parser.hpp"

#include "../../lib/mempool.h"
#include "../../lib/mem.h"
#include "../../lib/log.h"
#include <string.h>

// This sink is intentionally small until every reduction carries the child
// facts required by its retained AST constructor. It proves direct ownership
// for expression statements without creating a replacement syntax tree.
typedef struct JsCParseValue {
    JsAstNode* node;
    SourceSpan span;
    struct JsCParseValue* below;
} JsCParseValue;

typedef struct JsCAstSink {
    JsTranspiler* transpiler;
    const char* source;
    size_t source_length;
    JsCParseValue* values;
    JsAstNode* small_values[2];
    JsAstNode* root;
    bool unsupported;
    JsReductionKind rejected_kind;
    JsReductionForm rejected_form;
    uint32_t rejected_start;
    uint32_t rejected_end;
} JsCAstSink;

static bool js_c_span_contains(SourceSpan outer, SourceSpan inner) {
    return outer.start_byte <= inner.start_byte &&
        outer.end_byte >= inner.end_byte;
}

static bool js_c_source_slice(JsCAstSink* sink, SourceSpan span, StrView* out) {
    if (!sink || !out || span.end_byte < span.start_byte ||
            span.end_byte > sink->source_length) return false;
    out->str = sink->source + span.start_byte;
    out->length = span.end_byte - span.start_byte;
    return true;
}

static bool js_c_unsupported(JsCAstSink* sink) {
    if (sink) sink->unsupported = true;
    return false;
}

static bool js_c_push(JsCAstSink* sink, JsAstNode* node, SourceSpan span);

// every direct-parser reduction publishes one constructed node or one failure.
static bool js_c_push_result(JsCAstSink* sink, JsAstNode* node, SourceSpan span) {
    return node ? js_c_push(sink, node, span) : js_c_unsupported(sink);
}

static bool js_c_is_ts_type_node(const JsAstNode* node) {
    return node && (int)node->node_type == TS_AST_NODE_TYPE_FACT;
}

static bool js_c_is_type_parameters(const JsAstNode* node) {
    // Generic parameter lists are consumed by their direct parent and use the
    // ordinary null sentinel so no TS wrapper reaches the executable tree.
    return node && node->node_type == JS_AST_NODE_NULL;
}

static Type* js_c_type_fact(JsAstNode* type_node) {
    return js_c_is_ts_type_node(type_node)
        ? ((TsTypeFactNode*)type_node)->resolved_type : NULL;
}

static JsAstNode* js_c_discard_type_fact(JsAstNode* type_fact,
        SourceSpan declaration_span) {
    if (!type_fact) return NULL;
    type_fact->node_type = JS_AST_NODE_NULL;
    type_fact->source_span = declaration_span;
    type_fact->next = NULL;
    return type_fact;
}

static JsAstNode* js_c_make_enum_number(JsCAstSink* sink, SourceSpan span,
        int value) {
    JsLiteralNode* literal = (JsLiteralNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_LITERAL, span, sizeof(JsLiteralNode));
    if (!literal) return NULL;
    literal->literal_type = JS_LITERAL_NUMBER;
    literal->value.number_value = (double)value;
    literal->type = &TYPE_INT;
    return (JsAstNode*)literal;
}

static bool js_c_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool js_c_source_is_module(const char* source, size_t length) {
    if (!source) return false;
    JsLexer lexer;
    js_lexer_init(&lexer, source, length);
    bool statement_start = true;
    uint32_t paren_depth = 0;
    uint32_t bracket_depth = 0;
    uint32_t brace_depth = 0;
    for (;;) {
        JsToken token = js_lexer_next(&lexer);
        if (token.kind == JS_TOK_ERROR || token.kind == JS_TOK_EOF) return false;
        bool top_level = paren_depth == 0 && bracket_depth == 0 &&
            brace_depth == 0;
        bool declaration_start = statement_start || token.line_terminator_before;
        if (top_level && declaration_start && token.kind == JS_TOK_EXPORT) {
            return true;
        }
        if (top_level && declaration_start && token.kind == JS_TOK_IMPORT) {
            JsLexer lookahead = lexer;
            JsToken next = js_lexer_next(&lookahead);
            if (next.kind != JS_TOK_LPAREN && next.kind != JS_TOK_DOT) {
                return true;
            }
            if (next.kind == JS_TOK_DOT) return true; // import.meta
        }

        switch (token.kind) {
        case JS_TOK_LPAREN: paren_depth++; break;
        case JS_TOK_RPAREN: if (paren_depth) paren_depth--; break;
        case JS_TOK_LBRACKET: bracket_depth++; break;
        case JS_TOK_RBRACKET: if (bracket_depth) bracket_depth--; break;
        case JS_TOK_LBRACE: brace_depth++; break;
        case JS_TOK_RBRACE: if (brace_depth) brace_depth--; break;
        default: break;
        }

        bool now_top_level = paren_depth == 0 && bracket_depth == 0 &&
            brace_depth == 0;
        statement_start = token.kind == JS_TOK_SEMICOLON ||
            (token.kind == JS_TOK_RBRACE && now_top_level);
        if (token.kind == JS_TOK_HASHBANG) statement_start = true;
    }
}

static size_t js_c_skip_space(const char* source, size_t offset, size_t end) {
    while (offset < end && js_c_is_space(source[offset])) offset++;
    return offset;
}

static size_t js_c_program_start(const char* source, size_t body_start,
        size_t end) {
    size_t offset = 0;
    while (offset < body_start) {
        offset = js_c_skip_space(source, offset, body_start);
        if (offset >= body_start) break;
        if (offset == 0 && source[offset] == '#' && offset + 1 < end &&
                source[offset + 1] == '!') return offset;
        if (source[offset] == '/' && offset + 1 < end &&
                source[offset + 1] == '/') return offset;
        if (source[offset] == '/' && offset + 1 < end &&
                source[offset + 1] == '*') return offset;
        if (source[offset] == '<' && offset + 3 < end &&
                source[offset + 1] == '!' && source[offset + 2] == '-' &&
                source[offset + 3] == '-') return offset;
        break;
    }
    return body_start;
}

static bool js_c_read_name(const char* source, size_t end, size_t* offset,
        size_t* name_start, size_t* name_end) {
    size_t pos = js_c_skip_space(source, *offset, end);
    if (pos >= end || !((source[pos] >= 'A' && source[pos] <= 'Z') ||
            (source[pos] >= 'a' && source[pos] <= 'z') || source[pos] == '_' ||
            source[pos] == '$')) return false;
    *name_start = pos++;
    while (pos < end && ((source[pos] >= 'A' && source[pos] <= 'Z') ||
            (source[pos] >= 'a' && source[pos] <= 'z') ||
            (source[pos] >= '0' && source[pos] <= '9') ||
            source[pos] == '_' || source[pos] == '$')) pos++;
    *name_end = pos;
    *offset = pos;
    return true;
}

// A TS enum has a JavaScript runtime value. Construct that value directly so
// the published tree never needs a second TS-to-JS rewrite or fact rebuild.
static JsAstNode* js_c_lower_ts_enum(JsCAstSink* sink,
        SourceSpan span, String* name, JsAstNode** members, int member_count) {
    if (!sink || !name) return NULL;
    JsObjectNode* object = (JsObjectNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_OBJECT_EXPRESSION, span,
        sizeof(JsObjectNode));
    if (!object) return NULL;
    object->type = &TYPE_MAP;

    JsAstNode* last_property = NULL;
    for (int i = 0; i < member_count; i++) {
        JsPropertyNode* property = (JsPropertyNode*)members[i];
        if (!property || property->node_type != JS_AST_NODE_PROPERTY ||
                !property->key || property->key->node_type != JS_AST_NODE_IDENTIFIER) {
            return NULL;
        }
        JsIdentifierNode* key = (JsIdentifierNode*)property->key;
        key->type = &TYPE_STRING;
        property->computed = false;
        property->method = false;
        property->type = property->value ? property->value->type : &TYPE_ANY;
        property->next = NULL;
        if (last_property) last_property->next = (JsAstNode*)property;
        else object->properties = (JsAstNode*)property;
        last_property = (JsAstNode*)property;
    }

    for (int i = 0; i < member_count; i++) {
        JsPropertyNode* member = (JsPropertyNode*)members[i];
        if (!member || !member->key ||
                member->key->node_type != JS_AST_NODE_IDENTIFIER ||
                !member->value || member->value->node_type != JS_AST_NODE_LITERAL) {
            continue;
        }
        JsLiteralNode* member_value = (JsLiteralNode*)member->value;
        if (member_value->literal_type != JS_LITERAL_NUMBER ||
                member_value->value.number_value < 0) continue;
        String* member_name = ((JsIdentifierNode*)member->key)->name;
        if (!member_name) continue;
        JsPropertyNode* property = (JsPropertyNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_PROPERTY, span,
            sizeof(JsPropertyNode));
        JsLiteralNode* key = (JsLiteralNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_LITERAL, span,
            sizeof(JsLiteralNode));
        JsLiteralNode* value = (JsLiteralNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_LITERAL, span,
            sizeof(JsLiteralNode));
        if (!property || !key || !value) return NULL;
        key->literal_type = JS_LITERAL_NUMBER;
        key->value.number_value = member_value->value.number_value;
        key->type = &TYPE_INT;
        value->literal_type = JS_LITERAL_STRING;
        value->value.string_value = member_name;
        value->type = &TYPE_STRING;
        property->key = (JsAstNode*)key;
        property->value = (JsAstNode*)value;
        property->type = &TYPE_STRING;
        if (last_property) last_property->next = (JsAstNode*)property;
        else object->properties = (JsAstNode*)property;
        last_property = (JsAstNode*)property;
    }

    JsVariableDeclaratorNode* declarator =
        (JsVariableDeclaratorNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_VARIABLE_DECLARATOR, span,
            sizeof(JsVariableDeclaratorNode));
    JsIdentifierNode* identifier = (JsIdentifierNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_IDENTIFIER, span, sizeof(JsIdentifierNode));
    JsVariableDeclarationNode* declaration =
        (JsVariableDeclarationNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_VARIABLE_DECLARATION, span,
            sizeof(JsVariableDeclarationNode));
    if (!declarator || !identifier || !declaration) return NULL;
    identifier->name = name;
    identifier->type = &TYPE_MAP;
    declarator->id = (JsAstNode*)identifier;
    declarator->init = (JsAstNode*)object;
    declarator->type = &TYPE_MAP;
    declaration->kind = JS_VAR_CONST;
    declaration->declarations = (JsAstNode*)declarator;
    declaration->type = &TYPE_NULL;
    return (JsAstNode*)declaration;
}

// Decorators affect a class value, so emit the ordinary JavaScript binding and
// assignments before the tree is indexed instead of preserving decorator nodes
// for a later AST rewrite.
static JsAstNode* js_c_lower_decorated_class(JsCAstSink* sink,
        JsAstNode** decorators, uint32_t decorator_count,
        JsAstNode* class_node) {
    if (!sink || !decorators || !class_node ||
            (class_node->node_type != JS_AST_NODE_CLASS_DECLARATION &&
             class_node->node_type != JS_AST_NODE_CLASS_EXPRESSION)) return NULL;
    JsClassNode* class_value = (JsClassNode*)class_node;
    if (!class_value->name) return NULL;
    SourceSpan span = class_node->source_span;
    class_value->node_type = JS_AST_NODE_CLASS_EXPRESSION;

    JsVariableDeclaratorNode* declarator =
        (JsVariableDeclaratorNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_VARIABLE_DECLARATOR, span,
            sizeof(JsVariableDeclaratorNode));
    JsIdentifierNode* identifier = (JsIdentifierNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_IDENTIFIER, span, sizeof(JsIdentifierNode));
    JsVariableDeclarationNode* declaration =
        (JsVariableDeclarationNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_VARIABLE_DECLARATION, span,
            sizeof(JsVariableDeclarationNode));
    if (!declarator || !identifier || !declaration) return NULL;
    identifier->name = class_value->name;
    declarator->id = (JsAstNode*)identifier;
    declarator->init = class_node;
    declaration->kind = JS_VAR_LET;
    declaration->declarations = (JsAstNode*)declarator;

    JsAstNode* tail = (JsAstNode*)declaration;
    for (uint32_t i = decorator_count; i > 0; i--) {
        JsAstNode* decorator = decorators[i - 1];
        if (!decorator) continue;
        JsIdentifierNode* argument = (JsIdentifierNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_IDENTIFIER, span,
            sizeof(JsIdentifierNode));
        JsCallNode* call = (JsCallNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_CALL_EXPRESSION, span, sizeof(JsCallNode));
        JsIdentifierNode* fallback = (JsIdentifierNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_IDENTIFIER, span,
            sizeof(JsIdentifierNode));
        JsBinaryNode* coalesce = (JsBinaryNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_BINARY_EXPRESSION, span,
            sizeof(JsBinaryNode));
        JsIdentifierNode* left = (JsIdentifierNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_IDENTIFIER, span,
            sizeof(JsIdentifierNode));
        JsAssignmentNode* assignment =
            (JsAssignmentNode*)alloc_js_ast_node_span(sink->transpiler,
                JS_AST_NODE_ASSIGNMENT_EXPRESSION, span,
                sizeof(JsAssignmentNode));
        JsExpressionStatementNode* statement =
            (JsExpressionStatementNode*)alloc_js_ast_node_span(sink->transpiler,
                JS_AST_NODE_EXPRESSION_STATEMENT, span,
                sizeof(JsExpressionStatementNode));
        if (!argument || !call || !fallback || !coalesce || !left ||
                !assignment || !statement) return NULL;
        argument->name = class_value->name;
        call->callee = decorator;
        call->arguments = (JsAstNode*)argument;
        fallback->name = class_value->name;
        coalesce->op = JS_OP_NULLISH_COALESCE;
        coalesce->left = (JsAstNode*)call;
        coalesce->right = (JsAstNode*)fallback;
        left->name = class_value->name;
        assignment->op = JS_OP_ASSIGN;
        assignment->left = (JsAstNode*)left;
        assignment->right = (JsAstNode*)coalesce;
        statement->expression = (JsAstNode*)assignment;
        tail->next = (JsAstNode*)statement;
        tail = (JsAstNode*)statement;
    }
    return (JsAstNode*)declaration;
}

static JsAstNode* js_c_make_namespace_identifier(JsCAstSink* sink,
        SourceSpan span, String* name) {
    JsIdentifierNode* identifier = (JsIdentifierNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_IDENTIFIER, span, sizeof(JsIdentifierNode));
    if (identifier) identifier->name = name;
    return (JsAstNode*)identifier;
}

static JsAstNode* js_c_make_namespace_member(JsCAstSink* sink, SourceSpan span,
        String* namespace_name, JsAstNode* property) {
    JsMemberNode* member = (JsMemberNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_MEMBER_EXPRESSION, span,
        sizeof(JsMemberNode));
    if (!member) return NULL;
    member->object = js_c_make_namespace_identifier(sink, span, namespace_name);
    member->property = property;
    member->computed = false;
    return member->object && member->property ? (JsAstNode*)member : NULL;
}

static JsAstNode* js_c_make_namespace_assignment(JsCAstSink* sink,
        SourceSpan span, String* namespace_name, JsAstNode* property,
        JsAstNode* value) {
    JsAstNode* member = js_c_make_namespace_member(sink, span, namespace_name,
        property);
    JsAssignmentNode* assignment = (JsAssignmentNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_ASSIGNMENT_EXPRESSION, span,
        sizeof(JsAssignmentNode));
    JsExpressionStatementNode* statement =
        (JsExpressionStatementNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_EXPRESSION_STATEMENT, span,
            sizeof(JsExpressionStatementNode));
    if (!member || !assignment || !statement) return NULL;
    assignment->op = JS_OP_ASSIGN;
    assignment->left = member;
    assignment->right = value;
    statement->expression = (JsAstNode*)assignment;
    return (JsAstNode*)statement;
}

static void js_c_append_namespace_statement(JsAstNode** first, JsAstNode** last,
        JsAstNode* statement) {
    if (!statement) return;
    statement->next = NULL;
    if (*last) (*last)->next = statement;
    else *first = statement;
    *last = statement;
}

// A TS namespace is emitted as the standard namespace IIFE before binding and
// indexing. This keeps its synthetic function in the same one-pass AST facts.
static JsAstNode* js_c_lower_ts_namespace(JsCAstSink* sink,
        SourceSpan span, String* namespace_name, JsAstNode* statements) {
    if (!sink || !namespace_name) return NULL;
    JsVariableDeclaratorNode* namespace_declarator =
        (JsVariableDeclaratorNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_VARIABLE_DECLARATOR, span,
            sizeof(JsVariableDeclaratorNode));
    JsVariableDeclarationNode* namespace_declaration =
        (JsVariableDeclarationNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_VARIABLE_DECLARATION, span,
            sizeof(JsVariableDeclarationNode));
    if (!namespace_declarator || !namespace_declaration) return NULL;
    namespace_declarator->id = js_c_make_namespace_identifier(sink, span,
        namespace_name);
    if (!namespace_declarator->id) return NULL;
    namespace_declaration->kind = JS_VAR_VAR;
    namespace_declaration->declarations = (JsAstNode*)namespace_declarator;

    JsAstNode* body_first = NULL;
    JsAstNode* body_last = NULL;
    for (JsAstNode* raw_statement = statements; raw_statement;) {
        JsAstNode* next = raw_statement->next;
        raw_statement->next = NULL;
        JsAstNode* statement = raw_statement;
        if (!statement) continue;
        bool exported = statement->node_type == JS_AST_NODE_EXPORT_DECLARATION;
        if (exported) {
            statement = ((JsExportNode*)statement)->declaration;
            if (!statement) continue;
        }
        if (exported && statement->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* function = (JsFunctionNode*)statement;
            if (!function->name) return NULL;
            function->node_type = JS_AST_NODE_FUNCTION_EXPRESSION;
            JsAstNode* property = js_c_make_namespace_identifier(sink, span,
                function->name);
            statement = js_c_make_namespace_assignment(sink, span,
                namespace_name, property, (JsAstNode*)function);
        } else if (exported &&
                statement->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* variables =
                (JsVariableDeclarationNode*)statement;
            for (JsAstNode* item = variables->declarations; item;
                    item = item->next) {
                JsVariableDeclaratorNode* declarator =
                    (JsVariableDeclaratorNode*)item;
                if (!declarator->id || !declarator->init) continue;
                JsAstNode* assignment = js_c_make_namespace_assignment(sink,
                    span, namespace_name, declarator->id,
                    declarator->init);
                if (!assignment) return NULL;
                js_c_append_namespace_statement(&body_first, &body_last,
                    assignment);
            }
            raw_statement = next;
            continue;
        }
        if (!statement) return NULL;
        js_c_append_namespace_statement(&body_first, &body_last, statement);
        raw_statement = next;
    }

    JsBlockNode* body = (JsBlockNode*)alloc_js_ast_node_span(sink->transpiler,
        JS_AST_NODE_BLOCK_STATEMENT, span, sizeof(JsBlockNode));
    JsFunctionNode* function = (JsFunctionNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_FUNCTION_EXPRESSION, span,
        sizeof(JsFunctionNode));
    JsAstNode* parameter = js_c_make_namespace_identifier(sink, span,
        namespace_name);
    JsAstNode* argument = js_c_make_namespace_identifier(sink, span,
        namespace_name);
    JsAstNode* fallback_lhs = js_c_make_namespace_identifier(sink, span,
        namespace_name);
    JsObjectNode* fallback_object = (JsObjectNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_OBJECT_EXPRESSION, span,
        sizeof(JsObjectNode));
    JsAssignmentNode* fallback_assignment =
        (JsAssignmentNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_ASSIGNMENT_EXPRESSION, span,
            sizeof(JsAssignmentNode));
    JsBinaryNode* fallback = (JsBinaryNode*)alloc_js_ast_node_span(
        sink->transpiler, JS_AST_NODE_BINARY_EXPRESSION, span,
        sizeof(JsBinaryNode));
    JsCallNode* call = (JsCallNode*)alloc_js_ast_node_span(sink->transpiler,
        JS_AST_NODE_CALL_EXPRESSION, span, sizeof(JsCallNode));
    JsExpressionStatementNode* invocation =
        (JsExpressionStatementNode*)alloc_js_ast_node_span(sink->transpiler,
            JS_AST_NODE_EXPRESSION_STATEMENT, span,
            sizeof(JsExpressionStatementNode));
    if (!body || !function || !parameter || !argument || !fallback_lhs ||
            !fallback_object || !fallback_assignment || !fallback || !call ||
            !invocation) return NULL;
    body->statements = body_first;
    function->params = parameter;
    function->body = (JsAstNode*)body;
    fallback_assignment->op = JS_OP_ASSIGN;
    fallback_assignment->left = fallback_lhs;
    fallback_assignment->right = (JsAstNode*)fallback_object;
    fallback->op = JS_OP_OR;
    fallback->left = argument;
    fallback->right = (JsAstNode*)fallback_assignment;
    call->callee = (JsAstNode*)function;
    call->arguments = (JsAstNode*)fallback;
    invocation->expression = (JsAstNode*)call;
    namespace_declaration->next = (JsAstNode*)invocation;
    return (JsAstNode*)namespace_declaration;
}

static JsAstNode* js_c_build_ts_declaration(JsCAstSink* sink,
        const JsParseReduction* reduction, JsAstNode** children);

static JsAstNode* js_c_build_ts_declaration(JsCAstSink* sink,
        const JsParseReduction* reduction, JsAstNode** children) {
    StrView source;
    if (!js_c_source_slice(sink, reduction->span, &source)) return NULL;
    size_t pos = reduction->introducer.span.end_byte;
    size_t end = reduction->span.end_byte;
    size_t name_start = 0;
    size_t name_end = 0;
    if (!js_c_read_name(sink->source, end, &pos, &name_start, &name_end)) {
        return NULL;
    }
    String* name = name_pool_create_len(sink->transpiler->name_pool,
        sink->source + name_start, (int)(name_end - name_start));
    if (!name) return NULL;

    if (reduction->introducer.kind == JS_TOK_TYPE) {
        if (!children || reduction->child_count < 1) return NULL;
        JsAstNode* type_fact = NULL;
        for (uint32_t i = 0; i < reduction->child_count; i++) {
            if (!js_c_is_type_parameters(children[i]) && !type_fact)
                type_fact = children[i];
        }
        if (!type_fact || !js_c_is_ts_type_node(type_fact)) return NULL;
        Type* alias_type = js_c_type_fact(type_fact);
        ts_type_registry_add(sink->transpiler, name->chars,
            alias_type);
        return js_c_discard_type_fact(type_fact, reduction->span);
    }

    if (reduction->introducer.kind == JS_TOK_INTERFACE) {
        if (reduction->child_count < 1 || !children) return NULL;
        JsAstNode* body = children[reduction->child_count - 1];
        if (!body || !js_c_is_ts_type_node(body)) {
            return NULL;
        }
        for (uint32_t i = 0; i + 1 < reduction->child_count; i++) {
            if (js_c_is_type_parameters(children[i])) continue;
            if (!children[i] || !js_c_is_ts_type_node(children[i])) return NULL;
        }
        Type* interface_type = js_c_type_fact(body);
        if (interface_type && interface_type->type_id == LMD_TYPE_MAP) {
            ((TypeMap*)interface_type)->struct_name = name->chars;
        }
        ts_type_registry_add(sink->transpiler, name->chars,
            interface_type);
        return js_c_discard_type_fact(body, reduction->span);
    }

    if (reduction->introducer.kind == JS_TOK_ENUM) {
        int member_count = (int)reduction->child_count;
        if (member_count > 0) {
            int next_value = 0;
            bool next_value_valid = true;
            for (uint32_t i = 0; i < reduction->child_count; i++) {
                JsPropertyNode* member = (JsPropertyNode*)children[i];
                if (!member || member->node_type != JS_AST_NODE_PROPERTY) {
                    return NULL;
                }
                if (member->value &&
                        member->value->node_type == JS_AST_NODE_LITERAL) {
                    JsLiteralNode* literal = (JsLiteralNode*)member->value;
                    if (literal->literal_type == JS_LITERAL_NUMBER) {
                        int numeric_value = (int)literal->value.number_value;
                        member->value = js_c_make_enum_number(sink,
                            reduction->span, numeric_value);
                        if (!member->value) return NULL;
                        next_value = numeric_value + 1;
                        next_value_valid = true;
                    } else {
                        next_value_valid = false;
                    }
                } else if (!member->value && next_value_valid) {
                    member->value = js_c_make_enum_number(sink,
                        reduction->span, next_value++);
                    if (!member->value) return NULL;
                } else {
                    next_value_valid = false;
                }
            }
        }
        return js_c_lower_ts_enum(sink, reduction->span, name, children,
            member_count);
    }

    if (reduction->introducer.kind == JS_TOK_NAMESPACE ||
            reduction->introducer.kind == JS_TOK_MODULE) {
        if (reduction->child_count != 1 || !children || !children[0] ||
                children[0]->node_type != JS_AST_NODE_BLOCK_STATEMENT) return NULL;
        JsBlockNode* block = (JsBlockNode*)children[0];
        return js_c_lower_ts_namespace(sink, reduction->span, name,
            block->statements);
    }
    (void)source;
    return NULL;
}

static const char* js_c_reduction_form_name(JsReductionForm form) {
    switch (form) {
    case JS_REDUCTION_NONE: return "none";
    case JS_REDUCTION_TOKEN: return "token";
    case JS_REDUCTION_PREFIX: return "prefix";
    case JS_REDUCTION_POSTFIX: return "postfix";
    case JS_REDUCTION_BINARY: return "binary";
    case JS_REDUCTION_HOLE: return "hole";
    case JS_REDUCTION_ARRAY: return "array";
    case JS_REDUCTION_OBJECT: return "object";
    case JS_REDUCTION_PROPERTY: return "property";
    case JS_REDUCTION_SPREAD: return "spread";
    case JS_REDUCTION_ASSIGNMENT: return "assignment";
    case JS_REDUCTION_CALL: return "call";
    case JS_REDUCTION_NEW: return "new";
    case JS_REDUCTION_MEMBER: return "member";
    case JS_REDUCTION_SUBSCRIPT: return "subscript";
    case JS_REDUCTION_SEQUENCE: return "sequence";
    case JS_REDUCTION_CONDITIONAL: return "conditional";
    case JS_REDUCTION_DECLARATOR: return "declarator";
    case JS_REDUCTION_VARIABLE_DECLARATION: return "variable_declaration";
    case JS_REDUCTION_EXPRESSION_STATEMENT: return "expression_statement";
    case JS_REDUCTION_STATEMENT_WRAPPER: return "statement_wrapper";
    case JS_REDUCTION_IF: return "if";
    case JS_REDUCTION_WHILE: return "while";
    case JS_REDUCTION_DO_WHILE: return "do_while";
    case JS_REDUCTION_RETURN: return "return";
    case JS_REDUCTION_THROW: return "throw";
    case JS_REDUCTION_BREAK: return "break";
    case JS_REDUCTION_CONTINUE: return "continue";
    case JS_REDUCTION_PARAMETER: return "parameter";
    case JS_REDUCTION_CLASS_BODY: return "class_body";
    case JS_REDUCTION_METHOD: return "method";
    case JS_REDUCTION_FIELD: return "field";
    case JS_REDUCTION_STATIC_BLOCK: return "static_block";
    case JS_REDUCTION_FUNCTION: return "function";
    case JS_REDUCTION_ARROW: return "arrow";
    case JS_REDUCTION_CLASS: return "class";
    case JS_REDUCTION_TEMPLATE: return "template";
    case JS_REDUCTION_IMPORT: return "import";
    case JS_REDUCTION_EXPORT: return "export";
    case JS_REDUCTION_TYPE: return "type";
    case JS_REDUCTION_FOR: return "for";
    case JS_REDUCTION_FOR_IN: return "for_in";
    case JS_REDUCTION_FOR_OF: return "for_of";
    case JS_REDUCTION_SWITCH: return "switch";
    case JS_REDUCTION_CASE: return "case";
    case JS_REDUCTION_TRY: return "try";
    case JS_REDUCTION_CATCH: return "catch";
    case JS_REDUCTION_IMPORT_SPECIFIER: return "import_specifier";
    case JS_REDUCTION_EXPORT_SPECIFIER: return "export_specifier";
    case JS_REDUCTION_OBJECT_METHOD: return "object_method";
    case JS_REDUCTION_TEMPLATE_PART: return "template_part";
    case JS_REDUCTION_TAGGED_TEMPLATE: return "tagged_template";
    case JS_REDUCTION_NON_NULL: return "non_null";
    case JS_REDUCTION_TYPE_ASSERTION: return "type_assertion";
    case JS_REDUCTION_ENUM_MEMBER: return "enum_member";
    case JS_REDUCTION_LABELED: return "labeled";
    case JS_REDUCTION_WITH: return "with";
    case JS_REDUCTION_TYPE_PARAMETER: return "type_parameter";
    case JS_REDUCTION_TYPE_PARAMETERS: return "type_parameters";
    case JS_REDUCTION_DECORATOR: return "decorator";
    case JS_REDUCTION_DECORATED_DECLARATION: return "decorated_declaration";
    default: return "other";
    }
}

static bool js_c_push(JsCAstSink* sink, JsAstNode* node, SourceSpan span) {
    if (!sink || !node) return js_c_unsupported(sink);
    JsCParseValue* value = (JsCParseValue*)pool_alloc(sink->transpiler->pool,
        sizeof(JsCParseValue));
    if (!value) return js_c_unsupported(sink);
    value->node = node;
    value->span = span;
    value->below = sink->values;
    sink->values = value;
    return true;
}

static bool js_c_take_values(JsCAstSink* sink, uint32_t count,
        SourceSpan span, JsAstNode*** out_nodes) {
    if (!sink || !out_nodes) return js_c_unsupported(sink);
    *out_nodes = NULL;
    if (count == 0) return true;
    if ((size_t)count > SIZE_MAX / sizeof(JsAstNode*)) {
        return js_c_unsupported(sink);
    }
    JsCParseValue* value = sink->values;
    size_t previous_start = SIZE_MAX;
    for (uint32_t i = 0; i < count; i++) {
        if (!value || !js_c_span_contains(span, value->span)) {
            return js_c_unsupported(sink);
        }
        if (i > 0 && value->span.start_byte > previous_start) {
            return js_c_unsupported(sink);
        }
        previous_start = value->span.start_byte;
        value = value->below;
    }
    // Unary and binary reductions are the common case; reuse sink-owned slots
    // so direct AST construction does not allocate a pointer array per node.
    if (count <= 2) {
        value = sink->values;
        for (uint32_t i = count; i > 0; i--) {
            sink->small_values[i - 1] = value->node;
            value = value->below;
        }
        sink->values = value;
        *out_nodes = sink->small_values;
        return true;
    }
    JsAstNode** nodes = (JsAstNode**)pool_alloc(sink->transpiler->pool,
        sizeof(JsAstNode*) * count);
    if (!nodes) return js_c_unsupported(sink);
    value = sink->values;
    for (uint32_t i = count; i > 0; i--) {
        nodes[i - 1] = value->node;
        value = value->below;
    }
    sink->values = value;
    *out_nodes = nodes;
    return true;
}

static bool js_c_pop_children(JsCAstSink* sink, uint32_t count,
        JsAstNode** out_left, JsAstNode** out_right, SourceSpan span) {
    JsAstNode** nodes = NULL;
    if (count != 2 || !js_c_take_values(sink, count, span, &nodes)) return false;
    if (out_left) *out_left = nodes[0];
    if (out_right) *out_right = nodes[1];
    return true;
}

// Reducers transfer child ownership through one list builder before invoking
// their form-specific AST constructor.
static JsAstNode* js_c_link_children(JsAstNode** nodes, uint32_t begin,
        uint32_t end) {
    JsAstNode* first = NULL;
    JsAstNode* previous = NULL;
    for (uint32_t i = begin; i < end; i++) {
        if (!first) first = nodes[i];
        else previous->next = nodes[i];
        previous = nodes[i];
    }
    if (previous) previous->next = NULL;
    return first;
}

static JsAstNode* js_c_link_non_type_parameter_children(JsAstNode** nodes,
        uint32_t begin, uint32_t end) {
    JsAstNode* first = NULL;
    JsAstNode* previous = NULL;
    for (uint32_t i = begin; i < end; i++) {
        if (js_c_is_type_parameters(nodes[i])) continue;
        if (!first) first = nodes[i];
        else previous->next = nodes[i];
        previous = nodes[i];
    }
    if (previous) previous->next = NULL;
    return first;
}

static bool js_c_pop_value(JsCAstSink* sink, uint32_t count,
        JsAstNode** out_node, SourceSpan span) {
    JsAstNode** nodes = NULL;
    if (count != 1 || !js_c_take_values(sink, count, span, &nodes)) return false;
    if (out_node) *out_node = nodes[0];
    return true;
}

static bool js_c_binary_operator_supported(JsTokenKind kind) {
    switch (kind) {
    case JS_TOK_PLUS: case JS_TOK_MINUS: case JS_TOK_STAR:
    case JS_TOK_SLASH: case JS_TOK_PERCENT: case JS_TOK_EXP:
    case JS_TOK_AMP: case JS_TOK_PIPE: case JS_TOK_CARET:
    case JS_TOK_LSHIFT: case JS_TOK_RSHIFT: case JS_TOK_URSHIFT:
    case JS_TOK_AMP_AMP: case JS_TOK_PIPE_PIPE: case JS_TOK_NULLISH:
    case JS_TOK_EQUAL_EQUAL: case JS_TOK_STRICT_EQUAL:
    case JS_TOK_BANG_EQUAL: case JS_TOK_STRICT_BANG_EQUAL:
    case JS_TOK_LT: case JS_TOK_LTE: case JS_TOK_GT: case JS_TOK_GTE:
    case JS_TOK_IN: case JS_TOK_INSTANCEOF:
        return true;
    default:
        return false;
    }
}

static bool js_c_assignment_operator_supported(JsTokenKind kind) {
    switch (kind) {
    case JS_TOK_EQUAL: case JS_TOK_PLUS_EQUAL: case JS_TOK_MINUS_EQUAL:
    case JS_TOK_STAR_EQUAL: case JS_TOK_SLASH_EQUAL:
    case JS_TOK_PERCENT_EQUAL: case JS_TOK_EXP_EQUAL:
    case JS_TOK_AMP_EQUAL: case JS_TOK_PIPE_EQUAL: case JS_TOK_CARET_EQUAL:
    case JS_TOK_LSHIFT_EQUAL: case JS_TOK_RSHIFT_EQUAL:
    case JS_TOK_URSHIFT_EQUAL: case JS_TOK_AMP_AMP_EQUAL:
    case JS_TOK_PIPE_PIPE_EQUAL: case JS_TOK_NULLISH_EQUAL:
        return true;
    default:
        return false;
    }
}

static int js_c_variable_kind(JsTokenKind kind) {
    switch (kind) {
    case JS_TOK_LET: return JS_VAR_LET;
    case JS_TOK_CONST: return JS_VAR_CONST;
    case JS_TOK_VAR: return JS_VAR_VAR;
    default: return -1;
    }
}

static bool js_c_unary_operator_supported(JsTokenKind kind) {
    switch (kind) {
    case JS_TOK_PLUS: case JS_TOK_MINUS: case JS_TOK_BANG:
    case JS_TOK_TILDE: case JS_TOK_TYPEOF: case JS_TOK_VOID:
    case JS_TOK_DELETE: case JS_TOK_PLUS_PLUS: case JS_TOK_MINUS_MINUS:
        return true;
    default:
        return false;
    }
}

static const char* js_c_leaf_node_type(JsTokenKind kind) {
    switch (kind) {
    case JS_TOK_IDENTIFIER: case JS_TOK_PRIVATE_IDENTIFIER: return "identifier";
    case JS_TOK_BREAK: case JS_TOK_CASE: case JS_TOK_CATCH:
    case JS_TOK_CLASS: case JS_TOK_CONST: case JS_TOK_CONTINUE:
    case JS_TOK_DEBUGGER: case JS_TOK_DEFAULT: case JS_TOK_DELETE:
    case JS_TOK_DO: case JS_TOK_ELSE: case JS_TOK_EXPORT:
    case JS_TOK_EXTENDS: case JS_TOK_FINALLY: case JS_TOK_FOR:
    case JS_TOK_FUNCTION: case JS_TOK_IF: case JS_TOK_IMPORT:
    case JS_TOK_IN: case JS_TOK_INSTANCEOF: case JS_TOK_LET:
    case JS_TOK_NEW: case JS_TOK_RETURN: case JS_TOK_SUPER:
    case JS_TOK_SWITCH: case JS_TOK_THIS: case JS_TOK_THROW:
    case JS_TOK_TRY: case JS_TOK_TYPEOF: case JS_TOK_VAR:
    case JS_TOK_VOID: case JS_TOK_WHILE: case JS_TOK_WITH:
    case JS_TOK_YIELD: case JS_TOK_AWAIT: case JS_TOK_OF:
    case JS_TOK_ASYNC: case JS_TOK_AS: case JS_TOK_ASSERTS:
    case JS_TOK_ABSTRACT: case JS_TOK_ANY: case JS_TOK_BOOLEAN:
    case JS_TOK_DECLARE: case JS_TOK_ENUM: case JS_TOK_FROM:
    case JS_TOK_IMPLEMENTS: case JS_TOK_INFER: case JS_TOK_INTERFACE:
    case JS_TOK_IS:
    case JS_TOK_KEYOF: case JS_TOK_MODULE: case JS_TOK_NAMESPACE:
    case JS_TOK_NEVER: case JS_TOK_NUMBER_TYPE: case JS_TOK_OBJECT:
    case JS_TOK_PACKAGE: case JS_TOK_PRIVATE: case JS_TOK_PROTECTED:
    case JS_TOK_PUBLIC: case JS_TOK_READONLY: case JS_TOK_REQUIRE:
    case JS_TOK_SATISFIES: case JS_TOK_STATIC: case JS_TOK_STRING_TYPE:
    case JS_TOK_SYMBOL: case JS_TOK_TYPE: case JS_TOK_UNKNOWN:
    case JS_TOK_GET: case JS_TOK_SET: return "identifier";
        return "identifier";
    case JS_TOK_NUMBER: case JS_TOK_BIGINT: return "number";
    case JS_TOK_STRING: return "string";
    case JS_TOK_TRUE: return "true";
    case JS_TOK_FALSE: return "false";
    case JS_TOK_NULL: return "null";
    default: return NULL;
    }
}

static JsAstNode* js_c_leaf(JsCAstSink* sink, const JsParseReduction* reduction) {
    if (!sink || !reduction) return NULL;
    StrView source;
    if (!js_c_source_slice(sink, reduction->span, &source)) return NULL;

    if (reduction->introducer.kind == JS_TOK_REGEXP) {
        return build_js_regex_from_source(sink->transpiler, source,
            reduction->span);
    }
    if (reduction->introducer.kind == JS_TOK_TEMPLATE) {
        return build_js_template_from_source(sink->transpiler, source,
            reduction->span);
    }

    if ((reduction->flags & JS_REDUCTION_FLAG_BINDING) &&
            reduction->kind != JS_REDUCE_PATTERN &&
            reduction->introducer.kind != JS_TOK_LBRACKET &&
            reduction->introducer.kind != JS_TOK_LBRACE) {
        return build_js_identifier_from_source(sink->transpiler, source,
            reduction->span);
    }
    if ((reduction->flags & JS_REDUCTION_FLAG_PROPERTY) &&
            reduction->introducer.kind != JS_TOK_NUMBER &&
            reduction->introducer.kind != JS_TOK_BIGINT &&
            reduction->introducer.kind != JS_TOK_STRING) {
        return build_js_identifier_from_source(sink->transpiler, source,
            reduction->span);
    }
    const char* node_type = js_c_leaf_node_type(reduction->introducer.kind);
    if (!node_type) return NULL;
    if (strcmp(node_type, "identifier") == 0) {
        // Contextual keyword leaves such as `this`, `super`, and `async` use
        // the same runtime reference shape as ordinary identifiers.
        return build_js_identifier_from_source(sink->transpiler, source,
            reduction->span);
    }
    switch (reduction->introducer.kind) {
    case JS_TOK_IDENTIFIER:
        return build_js_identifier_from_source(sink->transpiler, source,
            reduction->span);
    default:
        return build_js_literal_from_source(sink->transpiler, node_type, source,
            reduction->span);
    }
}

static bool js_c_reduce(void* context, const JsParseReduction* reduction) {
    JsCAstSink* sink = (JsCAstSink*)context;
    if (!sink || !reduction) return false;
    sink->rejected_kind = reduction->kind;
    sink->rejected_form = reduction->form;
    sink->rejected_start = reduction->span.start_byte;
    sink->rejected_end = reduction->span.end_byte;

    if (reduction->kind == JS_REDUCE_TYPE &&
            reduction->form == JS_REDUCTION_TYPE_PARAMETER) {
        if (!reduction->child_count || reduction->child_count > 2) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count,
                reduction->span, &nodes)) return false;
        for (uint32_t i = 0; i < reduction->child_count; i++) {
            if (!js_c_is_ts_type_node(nodes[i])) return js_c_unsupported(sink);
        }
        return js_c_push_result(sink, nodes[0], reduction->span);
    }

    if (reduction->kind == JS_REDUCE_TYPE &&
            reduction->form == JS_REDUCTION_TYPE_PARAMETERS) {
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count,
                reduction->span, &nodes)) return false;
        if (!reduction->child_count) return js_c_unsupported(sink);
        for (uint32_t i = 0; i < reduction->child_count; i++) {
            if (!js_c_is_ts_type_node(nodes[i])) return js_c_unsupported(sink);
        }
        return js_c_push_result(sink,
            js_c_discard_type_fact(nodes[0], reduction->span), reduction->span);
    }

    if (reduction->kind == JS_REDUCE_TYPE &&
            reduction->form == JS_REDUCTION_TYPE) {
        StrView source;
        if (!js_c_source_slice(sink, reduction->span, &source)) {
            return js_c_unsupported(sink);
        }
        TsTypeFactNode* type = ts_parse_type_text(sink->transpiler, source.str,
            (int)source.length);
        if (type) type->source_span = reduction->span;
        return js_c_push_result(sink, (JsAstNode*)type, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_ENUM_MEMBER) {
        JsAstNode** nodes = NULL;
        if (reduction->child_count != 1 && reduction->child_count != 2) {
            return js_c_unsupported(sink);
        }
        if (!js_c_take_values(sink, reduction->child_count,
                reduction->span, &nodes)) return false;
        if (!nodes[0] || nodes[0]->node_type != JS_AST_NODE_IDENTIFIER) {
            return js_c_unsupported(sink);
        }
        JsPropertyNode* member = (JsPropertyNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_PROPERTY, reduction->span,
            sizeof(JsPropertyNode));
        if (!member) return js_c_unsupported(sink);
        member->key = nodes[0];
        member->value = reduction->child_count == 2 ? nodes[1] : NULL;
        member->computed = false;
        member->method = false;
        member->shorthand = false;
        return js_c_push(sink, (JsAstNode*)member, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_DECORATOR) {
        JsAstNode* expression = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &expression,
                reduction->span)) return false;
        return js_c_push_result(sink, expression, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_DECORATED_DECLARATION) {
        if (reduction->child_count < 2) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count,
                reduction->span, &nodes)) return false;
        JsAstNode* declaration = nodes[reduction->child_count - 1];
        if (!declaration) return js_c_unsupported(sink);
        declaration->source_span.start_byte = reduction->span.start_byte;
        JsAstNode* decorators[16];
        uint32_t decorator_count = reduction->child_count - 1;
        if (decorator_count > 16) return js_c_unsupported(sink);
        for (uint32_t i = 0; i + 1 < reduction->child_count; i++) {
            if (!nodes[i]) {
                return js_c_unsupported(sink);
            }
            decorators[i] = nodes[i];
        }
        JsAstNode* lowered = js_c_lower_decorated_class(sink, decorators,
            decorator_count, declaration);
        return js_c_push_result(sink, lowered, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_TYPE) {
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* declaration = js_c_build_ts_declaration(sink, reduction,
            nodes);
        return js_c_push_result(sink, declaration, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_TOKEN) {
        if (reduction->introducer.kind == JS_TOK_NEW &&
                reduction->operator_token.kind != JS_TOK_EOF) {
            JsAstNode* target = build_js_new_target_from_span(
                sink->transpiler, reduction->span);
            return js_c_push_result(sink, target, reduction->span);
        }
        JsAstNode* leaf = js_c_leaf(sink, reduction);
        return js_c_push_result(sink, leaf, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_PATTERN &&
            reduction->form == JS_REDUCTION_TOKEN) {
        JsAstNode* binding = js_c_leaf(sink, reduction);
        return js_c_push_result(sink, binding, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_PATTERN &&
            reduction->form == JS_REDUCTION_HOLE) {
        JsAstNode* hole = build_js_pattern_hole(sink->transpiler,
            reduction->span);
        return js_c_push_result(sink, hole, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_HOLE) {
        JsAstNode* hole = build_js_array_hole(sink->transpiler,
            reduction->span);
        return js_c_push_result(sink, hole, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_PATTERN &&
            reduction->form == JS_REDUCTION_ASSIGNMENT) {
        JsAstNode* left = NULL;
        JsAstNode* right = NULL;
        if (!js_c_pop_children(sink, reduction->child_count, &left, &right,
                reduction->span)) return false;
        JsAstNode* assignment = build_js_assignment_pattern_from_children(
            sink->transpiler, reduction->span, left, right);
        return js_c_push_result(sink, assignment, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_PATTERN &&
            reduction->form == JS_REDUCTION_SPREAD) {
        JsAstNode* argument = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &argument,
                reduction->span)) return false;
        JsAstNode* rest = build_js_rest_pattern_from_child(sink->transpiler,
            reduction->span, argument,
            (reduction->flags & JS_REDUCTION_FLAG_PROPERTY) != 0);
        return js_c_push_result(sink, rest, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_PATTERN &&
            reduction->form == JS_REDUCTION_PROPERTY) {
        JsAstNode** nodes = NULL;
        if (reduction->child_count != 2 ||
                !js_c_take_values(sink, reduction->child_count,
                    reduction->span, &nodes)) return false;
        JsAstNode* property = build_js_pattern_property_from_children(
            sink->transpiler, reduction->span, nodes[0], nodes[1],
            (reduction->flags & JS_REDUCTION_FLAG_COMPUTED) != 0,
            (reduction->flags & JS_REDUCTION_FLAG_SHORTHAND) != 0);
        return js_c_push_result(sink, property, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_PATTERN &&
            (reduction->form == JS_REDUCTION_ARRAY ||
             reduction->form == JS_REDUCTION_OBJECT)) {
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count,
                reduction->span, &nodes)) return false;
        JsAstNode* list = js_c_link_children(nodes, 0, reduction->child_count);
        JsAstNode* pattern = reduction->form == JS_REDUCTION_ARRAY
            ? build_js_pattern_array_from_list(sink->transpiler,
                reduction->span, list, reduction->child_count)
            : build_js_pattern_object_from_list(sink->transpiler,
                reduction->span, list, reduction->child_count);
        return js_c_push_result(sink, pattern, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_TEMPLATE_PART) {
        StrView source;
        if (!js_c_source_slice(sink, reduction->span, &source)) {
            return js_c_unsupported(sink);
        }
        JsAstNode* element = build_js_template_element_from_source(
            sink->transpiler, source, reduction->span,
            (reduction->flags & JS_REDUCTION_FLAG_TEMPLATE_TAIL) != 0);
        return js_c_push_result(sink, element, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_TEMPLATE) {
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count,
                reduction->span, &nodes)) return false;
        JsAstNode* parts = js_c_link_children(nodes, 0, reduction->child_count);
        JsAstNode* literal = build_js_template_from_parts(sink->transpiler,
            reduction->span, parts, reduction->child_count);
        return js_c_push_result(sink, literal, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_TAGGED_TEMPLATE) {
        JsAstNode* tag = NULL;
        JsAstNode* quasi = NULL;
        if (!js_c_pop_children(sink, reduction->child_count, &tag, &quasi,
                reduction->span)) return false;
        JsAstNode* tagged = build_js_tagged_template_from_children(
            sink->transpiler, reduction->span, tag, quasi);
        return js_c_push_result(sink, tagged, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_DECLARATOR) {
        if (reduction->child_count < 1 || reduction->child_count > 3) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* type_node = NULL;
        JsAstNode* init = NULL;
        for (uint32_t i = 1; i < reduction->child_count; i++) {
            if (js_c_is_ts_type_node(nodes[i])) type_node = nodes[i];
            else init = nodes[i];
        }
        JsAstNode* declarator = type_node
            ? build_js_declarator_with_type_from_children(sink->transpiler,
                reduction->span, nodes[0], js_c_type_fact(type_node), init)
            : build_js_declarator_from_children(sink->transpiler,
                reduction->span, nodes[0], init);
        return js_c_push_result(sink, declarator, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_VARIABLE_DECLARATION) {
        int kind = js_c_variable_kind(reduction->introducer.kind);
        if (kind < 0) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* declarations = js_c_link_children(nodes, 0,
            reduction->child_count);
        JsAstNode* declaration = build_js_variable_declaration_from_list(
            sink->transpiler, reduction->span, declarations,
            reduction->child_count, kind);
        return js_c_push_result(sink, declaration, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_PARAMETER) {
        if (reduction->child_count < 1 || reduction->child_count > 3) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* type_node = NULL;
        JsAstNode* default_value = NULL;
        for (uint32_t i = 1; i < reduction->child_count; i++) {
            if (js_c_is_ts_type_node(nodes[i])) type_node = nodes[i];
            else default_value = nodes[i];
        }
        JsAstNode* parameter = type_node
            ? build_js_parameter_with_type_from_children(sink->transpiler,
                reduction->span, nodes[0], js_c_type_fact(type_node), default_value,
                (reduction->flags & JS_REDUCTION_FLAG_OPTIONAL) != 0,
                (reduction->flags & JS_REDUCTION_FLAG_SPREAD) != 0)
            : build_js_parameter_from_children(sink->transpiler,
                reduction->span, nodes[0], default_value,
                (reduction->flags & JS_REDUCTION_FLAG_OPTIONAL) != 0,
                (reduction->flags & JS_REDUCTION_FLAG_SPREAD) != 0);
        if (parameter && parameter->node_type ==
                (JsAstNodeType)TS_AST_NODE_PARAMETER) {
            TsParameterNode* ts_parameter = (TsParameterNode*)parameter;
            ts_parameter->accessibility = reduction->parameter_accessibility;
            ts_parameter->readonly = reduction->parameter_readonly;
        }
        return js_c_push_result(sink, parameter, reduction->span);
    }

    if ((reduction->kind == JS_REDUCE_DECLARATION ||
            reduction->kind == JS_REDUCE_EXPRESSION) &&
            (reduction->form == JS_REDUCTION_FUNCTION ||
             reduction->form == JS_REDUCTION_ARROW)) {
        bool arrow = reduction->form == JS_REDUCTION_ARROW;
        uint32_t minimum = arrow ? 1 :
            ((reduction->flags & JS_REDUCTION_FLAG_NAMED) ? 2 : 1);
        if (reduction->child_count < minimum) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        uint32_t body_index = reduction->child_count - 1;
        if (!nodes[body_index] || (!arrow &&
                nodes[body_index]->node_type != JS_AST_NODE_BLOCK_STATEMENT)) {
            return js_c_unsupported(sink);
        }
        JsAstNode* return_type = NULL;
        if (body_index > 0 && js_c_is_ts_type_node(nodes[body_index - 1]) &&
                !js_c_is_type_parameters(nodes[body_index - 1])) {
            return_type = nodes[--body_index];
        }
        JsAstNode* name = NULL;
        uint32_t parameter_start = 0;
        if (!arrow && (reduction->flags & JS_REDUCTION_FLAG_NAMED)) {
            name = nodes[0];
            parameter_start = 1;
        }
        JsAstNode* params = js_c_link_non_type_parameter_children(nodes,
            parameter_start, body_index);
        bool declaration =
            (reduction->flags & JS_REDUCTION_FLAG_DECLARATION) != 0 &&
            (reduction->flags & JS_REDUCTION_FLAG_NAMED) != 0;
        JsAstNode* function = return_type
            ? build_js_function_with_return_type_from_children(
                sink->transpiler, reduction->span, name, params,
                nodes[reduction->child_count - 1],
                js_c_type_fact(return_type),
                (reduction->flags & 1u) != 0,
                (reduction->flags & 2u) != 0,
                declaration,
                arrow)
            : build_js_function_from_children(
                sink->transpiler, reduction->span, name, params,
                nodes[reduction->child_count - 1],
                (reduction->flags & 1u) != 0,
                (reduction->flags & 2u) != 0,
                declaration,
                arrow);
        return js_c_push_result(sink, function, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_CLASS_MEMBER &&
            reduction->form == JS_REDUCTION_CLASS_BODY) {
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* members = js_c_link_children(nodes, 0,
            reduction->child_count);
        JsAstNode* body = build_js_class_body_from_list(sink->transpiler,
            reduction->span, members, reduction->child_count);
        return js_c_push_result(sink, body, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_CLASS_MEMBER &&
            reduction->form == JS_REDUCTION_METHOD) {
        if (reduction->child_count < 2) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        uint32_t body_index = reduction->child_count - 1;
        JsAstNode* return_type = NULL;
        if (body_index > 1 && js_c_is_ts_type_node(nodes[body_index - 1]) &&
                !js_c_is_type_parameters(nodes[body_index - 1])) {
            return_type = nodes[--body_index];
        }
        JsAstNode* params = js_c_link_non_type_parameter_children(nodes, 1,
            body_index);
        JsAstNode* method = build_js_method_from_children(sink->transpiler,
            reduction->span, nodes[0], params,
            nodes[reduction->child_count - 1], reduction->flags);
        if (method && !sink->transpiler->strict_js) {
            // TypeScript method bodies include the same trailing trivia as the
            // enclosing method in the reference AST.
            JsAstNode* body = ((JsMethodDefinitionNode*)method)->body;
            if (body) body->source_span.end_byte = reduction->span.end_byte;
        }
        if (method && return_type) {
            ((JsMethodDefinitionNode*)method)->declared_return_type =
                js_c_type_fact(return_type);
        }
        return js_c_push_result(sink, method, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_CLASS_MEMBER &&
            reduction->form == JS_REDUCTION_FIELD) {
        if (reduction->child_count < 1 || reduction->child_count > 3) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* value = NULL;
        for (uint32_t i = 1; i < reduction->child_count; i++) {
            if (!js_c_is_ts_type_node(nodes[i])) value = nodes[i];
        }
        JsAstNode* field = build_js_field_from_children(sink->transpiler,
            reduction->span, nodes[0], value, reduction->flags);
        return js_c_push_result(sink, field, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_CLASS_MEMBER &&
            reduction->form == JS_REDUCTION_STATIC_BLOCK) {
        JsAstNode* body = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &body,
                reduction->span)) return false;
        JsAstNode* block = build_js_static_block_from_child(sink->transpiler,
            reduction->span, body);
        return js_c_push_result(sink, block, reduction->span);
    }

    if ((reduction->kind == JS_REDUCE_DECLARATION ||
            reduction->kind == JS_REDUCE_EXPRESSION) &&
            reduction->form == JS_REDUCTION_CLASS) {
        uint32_t expected = 1 +
            ((reduction->flags & JS_REDUCTION_FLAG_NAMED) ? 1u : 0u) +
            ((reduction->flags & JS_REDUCTION_FLAG_SUPER) ? 1u : 0u);
        if (reduction->child_count != expected &&
                reduction->child_count != expected + 1u) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        uint32_t index = 0;
        JsAstNode* name = NULL;
        JsAstNode* superclass = NULL;
        if (reduction->flags & JS_REDUCTION_FLAG_NAMED) name = nodes[index++];
        if (index < reduction->child_count &&
                js_c_is_type_parameters(nodes[index])) index++;
        if (reduction->flags & JS_REDUCTION_FLAG_SUPER) superclass = nodes[index++];
        JsAstNode* class_node = build_js_class_from_children(
            sink->transpiler, reduction->span, name, superclass, nodes[index],
            (reduction->flags & JS_REDUCTION_FLAG_DECLARATION) != 0);
        return js_c_push_result(sink, class_node, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_IMPORT_SPECIFIER) {
        if (reduction->child_count != 1 && reduction->child_count != 2) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* specifier = build_js_import_specifier_from_children(
            sink->transpiler, reduction->span, nodes[0],
            reduction->child_count == 2 ? nodes[1] : NULL);
        return js_c_push_result(sink, specifier, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_IMPORT) {
        if (reduction->child_count < 1) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        uint32_t source_index = reduction->child_count - 1;
        uint32_t index = 0;
        JsAstNode* default_name = NULL;
        JsAstNode* namespace_name = NULL;
        if (reduction->flags & JS_REDUCTION_FLAG_IMPORT_DEFAULT) {
            default_name = nodes[index++];
        }
        if (reduction->flags & JS_REDUCTION_FLAG_IMPORT_NAMESPACE) {
            namespace_name = nodes[index++];
        }
        JsAstNode* specifiers = NULL;
        JsAstNode* previous = NULL;
        while (index < source_index) {
            JsAstNode* item = nodes[index++];
            if (!item || item->node_type != JS_AST_NODE_IMPORT_SPECIFIER) {
                return js_c_unsupported(sink);
            }
            if (!previous) specifiers = item;
            else previous->next = item;
            previous = item;
        }
        if (previous) previous->next = NULL;
        JsAstNode* import_node = build_js_import_from_children(
            sink->transpiler, reduction->span, nodes[source_index],
            default_name, namespace_name, specifiers);
        return js_c_push_result(sink, import_node, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_EXPORT_SPECIFIER) {
        if (reduction->child_count != 1 && reduction->child_count != 2) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* specifier = build_js_export_specifier_from_children(
            sink->transpiler, reduction->span, nodes[0],
            reduction->child_count == 2 ? nodes[1] : NULL);
        return js_c_push_result(sink, specifier, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_DECLARATION &&
            reduction->form == JS_REDUCTION_EXPORT) {
        uint32_t minimum = (reduction->flags & JS_REDUCTION_FLAG_EXPORT_SOURCE) ? 1u : 0u;
        if (reduction->child_count < minimum) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        uint32_t source_index = (reduction->flags & JS_REDUCTION_FLAG_EXPORT_SOURCE)
            ? reduction->child_count - 1 : reduction->child_count;
        JsAstNode* declaration = NULL;
        JsAstNode* specifiers = NULL;
        JsAstNode* previous = NULL;
        for (uint32_t i = 0; i < source_index; i++) {
            JsAstNode* item = nodes[i];
            if (!item) return js_c_unsupported(sink);
            if (item->node_type == JS_AST_NODE_EXPORT_SPECIFIER) {
                if (!previous) specifiers = item;
                else previous->next = item;
                previous = item;
            } else if (!declaration) {
                declaration = item;
            } else {
                return js_c_unsupported(sink);
            }
        }
        if (previous) previous->next = NULL;
        JsAstNode* source = source_index < reduction->child_count
            ? nodes[source_index] : NULL;
        JsAstNode* export_node = build_js_export_from_children(
            sink->transpiler, reduction->span, declaration, specifiers,
            source, reduction->flags);
        return js_c_push_result(sink, export_node, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_OBJECT_METHOD) {
        if (reduction->child_count < 2) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* params = js_c_link_children(nodes, 1,
            reduction->child_count - 1);
        JsAstNode* property = build_js_object_method_from_children(
            sink->transpiler, reduction->span, nodes[0], params,
            nodes[reduction->child_count - 1], reduction->flags);
        return js_c_push_result(sink, property, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_FOR) {
        JsAstNode** nodes = NULL;
        uint32_t clause_count =
            ((reduction->flags & JS_REDUCTION_FLAG_FOR_INIT) ? 1u : 0u) +
            ((reduction->flags & JS_REDUCTION_FLAG_FOR_TEST) ? 1u : 0u) +
            ((reduction->flags & JS_REDUCTION_FLAG_FOR_UPDATE) ? 1u : 0u);
        if (reduction->child_count != clause_count &&
                reduction->child_count != clause_count + 1u) {
            return js_c_unsupported(sink);
        }
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                    &nodes)) return false;
        uint32_t index = 0;
        JsAstNode* init = NULL;
        JsAstNode* test = NULL;
        JsAstNode* update = NULL;
        if (reduction->flags & JS_REDUCTION_FLAG_FOR_INIT) init = nodes[index++];
        if (reduction->flags & JS_REDUCTION_FLAG_FOR_TEST) test = nodes[index++];
        if (reduction->flags & JS_REDUCTION_FLAG_FOR_UPDATE) update = nodes[index++];
        JsAstNode* body = index < reduction->child_count ? nodes[index] : NULL;
        JsAstNode* loop = build_js_for_from_children(sink->transpiler,
            reduction->span, init, test, update, body);
        return js_c_push_result(sink, loop, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            (reduction->form == JS_REDUCTION_FOR_IN ||
             reduction->form == JS_REDUCTION_FOR_OF)) {
        JsAstNode** nodes = NULL;
        if ((reduction->child_count != 2 && reduction->child_count != 3) ||
                !js_c_take_values(sink, reduction->child_count, reduction->span,
                    &nodes)) return false;
        int kind = JS_VAR_VAR;
        bool declares_binding =
            (reduction->flags & JS_REDUCTION_FLAG_FOR_DECLARATION) != 0;
        if (nodes[0]->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            kind = ((JsVariableDeclarationNode*)nodes[0])->kind;
            declares_binding = true;
        }
        JsAstNode* loop = build_js_for_of_from_children(sink->transpiler,
            reduction->span, nodes[0], nodes[1],
            reduction->child_count == 3 ? nodes[2] : NULL, kind,
            declares_binding,
            (reduction->flags & JS_REDUCTION_FLAG_FOR_AWAIT) != 0,
            reduction->form == JS_REDUCTION_FOR_IN);
        return js_c_push_result(sink, loop, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_CASE) {
        bool is_default = (reduction->flags & JS_REDUCTION_FLAG_DEFAULT) != 0;
        uint32_t expected = reduction->child_count - (is_default ? 0u : 1u);
        if (reduction->child_count < (is_default ? 0u : 1u)) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* test = is_default ? NULL : nodes[0];
        uint32_t statement_start = is_default ? 0u : 1u;
        JsAstNode* consequent = js_c_link_children(nodes, statement_start,
            reduction->child_count);
        (void)expected;
        JsAstNode* case_node = build_js_switch_case_from_children(
            sink->transpiler, reduction->span, test, consequent, is_default);
        return js_c_push_result(sink, case_node, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_SWITCH) {
        if (reduction->child_count < 1) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* cases = js_c_link_children(nodes, 1, reduction->child_count);
        JsAstNode* switched = build_js_switch_from_children(sink->transpiler,
            reduction->span, nodes[0], cases, reduction->child_count - 1);
        return js_c_push_result(sink, switched, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_CATCH) {
        if (reduction->child_count != 1 && reduction->child_count != 2) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* parameter = (reduction->flags & JS_REDUCTION_FLAG_CATCH_PARAM)
            ? nodes[0] : NULL;
        JsAstNode* body = nodes[reduction->child_count - 1];
        JsAstNode* handler = build_js_catch_from_children(sink->transpiler,
            reduction->span, parameter, body);
        return js_c_push_result(sink, handler, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_TRY) {
        uint32_t expected = 1 +
            ((reduction->flags & JS_REDUCTION_FLAG_TRY_HANDLER) ? 1u : 0u) +
            ((reduction->flags & JS_REDUCTION_FLAG_TRY_FINALIZER) ? 1u : 0u);
        if (reduction->child_count != expected) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        uint32_t index = 0;
        JsAstNode* block = nodes[index++];
        JsAstNode* handler = (reduction->flags & JS_REDUCTION_FLAG_TRY_HANDLER)
            ? nodes[index++] : NULL;
        JsAstNode* finalizer = (reduction->flags & JS_REDUCTION_FLAG_TRY_FINALIZER)
            ? nodes[index++] : NULL;
        JsAstNode* tried = build_js_try_from_children(sink->transpiler,
            reduction->span, block, handler, finalizer);
        return js_c_push_result(sink, tried, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_BLOCK &&
            reduction->form == JS_REDUCTION_NONE) {
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* statements = js_c_link_children(nodes, 0,
            reduction->child_count);
        JsAstNode* block = build_js_block_from_list(sink->transpiler,
            reduction->span, statements, reduction->child_count);
        return js_c_push_result(sink, block, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_BINARY) {
        if (reduction->operator_token.kind == JS_TOK_AS ||
                reduction->operator_token.kind == JS_TOK_SATISFIES) {
            JsAstNode* inner = NULL;
            JsAstNode* target_type = NULL;
            if (reduction->child_count != 2) return js_c_unsupported(sink);
            JsAstNode** nodes = NULL;
            if (!js_c_take_values(sink, reduction->child_count,
                    reduction->span, &nodes)) return false;
            inner = nodes[0];
            target_type = nodes[1];
            JsAstNode* expression = build_js_type_expression_from_children(
                sink->transpiler, reduction->span, inner, target_type,
                reduction->operator_token.kind == JS_TOK_SATISFIES, false);
            return js_c_push_result(sink, expression, reduction->span);
        }
        if (!js_c_binary_operator_supported(
                reduction->operator_token.kind)) {
            return js_c_unsupported(sink);
        }
        JsAstNode* left = NULL;
        JsAstNode* right = NULL;
        if (!js_c_pop_children(sink, reduction->child_count, &left, &right,
                reduction->span)) return false;
        StrView op;
        if (!js_c_source_slice(sink, reduction->operator_token.span, &op)) {
            return js_c_unsupported(sink);
        }
        JsAstNode* binary = build_js_binary_from_children(sink->transpiler,
            reduction->span,
            js_operator_from_string(op.str, op.length), left, right);
        return js_c_push_result(sink, binary, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            (reduction->form == JS_REDUCTION_PREFIX ||
             reduction->form == JS_REDUCTION_POSTFIX)) {
        if (reduction->operator_token.kind == JS_TOK_AWAIT ||
                reduction->operator_token.kind == JS_TOK_YIELD) {
            JsAstNode* operand = NULL;
            if (reduction->child_count > 1) return js_c_unsupported(sink);
            if (reduction->child_count == 1 &&
                    !js_c_pop_value(sink, reduction->child_count, &operand,
                        reduction->span)) return false;
            JsAstNode* value = reduction->operator_token.kind == JS_TOK_AWAIT
                ? build_js_await_from_child(sink->transpiler,
                    reduction->span, operand)
                : build_js_yield_from_child(sink->transpiler,
                    reduction->span, operand,
                    (reduction->flags & JS_REDUCTION_FLAG_YIELD_DELEGATE) != 0);
            return js_c_push_result(sink, value, reduction->span);
        }
        if (!js_c_unary_operator_supported(reduction->operator_token.kind)) {
            return js_c_unsupported(sink);
        }
        JsAstNode* operand = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &operand,
                reduction->span)) return false;
        StrView op;
        if (!js_c_source_slice(sink, reduction->operator_token.span, &op)) {
            return js_c_unsupported(sink);
        }
        JsAstNode* unary = build_js_unary_from_child(sink->transpiler,
            reduction->span,
            js_unary_operator_from_string(op.str, op.length), operand,
            reduction->form == JS_REDUCTION_PREFIX);
        return js_c_push_result(sink, unary, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_NON_NULL) {
        JsAstNode* inner = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &inner,
                reduction->span)) return false;
        JsAstNode* expression = build_js_non_null_from_child(
            sink->transpiler, reduction->span, inner);
        return js_c_push_result(sink, expression, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_TYPE_ASSERTION) {
        JsAstNode** nodes = NULL;
        if (reduction->child_count != 2 ||
                !js_c_take_values(sink, reduction->child_count,
                    reduction->span, &nodes)) return false;
        JsAstNode* expression = build_js_type_expression_from_children(
            sink->transpiler, reduction->span, nodes[1], nodes[0], false, true);
        return js_c_push_result(sink, expression, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_CALL) {
        if (reduction->child_count < 1) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* arguments = js_c_link_children(nodes, 1,
            reduction->child_count);
        JsAstNode* call = build_js_call_from_children(sink->transpiler,
            reduction->span, nodes[0], arguments,
            (reduction->flags & JS_REDUCTION_FLAG_OPTIONAL) != 0);
        return js_c_push_result(sink, call, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_NEW) {
        if (reduction->child_count < 1) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* arguments = js_c_link_children(nodes, 1,
            reduction->child_count);
        JsAstNode* expression = build_js_new_from_children(sink->transpiler,
            reduction->span, nodes[0], arguments);
        return js_c_push_result(sink, expression, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_ASSIGNMENT) {
        if (!js_c_assignment_operator_supported(
                reduction->operator_token.kind)) return js_c_unsupported(sink);
        JsAstNode* left = NULL;
        JsAstNode* right = NULL;
        if (!js_c_pop_children(sink, reduction->child_count, &left, &right,
                reduction->span)) return false;
        StrView op;
        if (!js_c_source_slice(sink, reduction->operator_token.span, &op)) {
            return js_c_unsupported(sink);
        }
        JsAstNode* assignment = build_js_assignment_from_children(
            sink->transpiler, reduction->span,
            js_operator_from_string(op.str, op.length), left, right);
        return js_c_push_result(sink, assignment, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_SEQUENCE) {
        JsAstNode* left = NULL;
        JsAstNode* right = NULL;
        if (!js_c_pop_children(sink, reduction->child_count, &left, &right,
                reduction->span)) return false;
        JsSequenceNode* sequence = NULL;
        if (left && left->node_type == JS_AST_NODE_SEQUENCE_EXPRESSION) {
            sequence = (JsSequenceNode*)left;
            JsAstNode* tail = sequence->expressions;
            if (!tail) return js_c_unsupported(sink);
            while (tail->next) tail = tail->next;
            tail->next = right;
            sequence->length++;
            sequence->source_span = reduction->span;
        } else {
            left->next = right;
            JsAstNode* list = build_js_sequence_from_list(sink->transpiler,
                reduction->span, left, 2);
            if (!list) return js_c_unsupported(sink);
            sequence = (JsSequenceNode*)list;
        }
        return js_c_push(sink, (JsAstNode*)sequence, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_CONDITIONAL) {
        JsAstNode** nodes = NULL;
        if (reduction->child_count != 3 ||
                !js_c_take_values(sink, reduction->child_count,
                    reduction->span, &nodes)) return false;
        JsAstNode* conditional = build_js_conditional_from_children(
            sink->transpiler, reduction->span, nodes[0], nodes[1], nodes[2]);
        return js_c_push_result(sink, conditional, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_PROPERTY) {
        if (reduction->child_count != 1 && reduction->child_count != 2) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* value = reduction->child_count == 2 ? nodes[1] : nodes[0];
        JsAstNode* property = build_js_property_from_children(
            sink->transpiler, reduction->span, nodes[0], value,
            (reduction->flags & JS_REDUCTION_FLAG_COMPUTED) != 0,
            (reduction->flags & JS_REDUCTION_FLAG_SHORTHAND) != 0);
        return js_c_push_result(sink, property, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_SPREAD) {
        JsAstNode* argument = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &argument,
                reduction->span)) return false;
        JsAstNode* spread = build_js_spread_from_child(sink->transpiler,
            reduction->span, argument);
        return js_c_push_result(sink, spread, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_OBJECT) {
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        for (uint32_t i = 0; i < reduction->child_count; i++) {
            if (nodes[i] && nodes[i]->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                mark_js_object_spread(sink->transpiler, nodes[i]);
            }
        }
        JsAstNode* properties = js_c_link_children(nodes, 0,
            reduction->child_count);
        JsAstNode* object = build_js_object_from_list(sink->transpiler,
            reduction->span, properties, reduction->child_count);
        return js_c_push_result(sink, object, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            (reduction->form == JS_REDUCTION_MEMBER ||
             reduction->form == JS_REDUCTION_SUBSCRIPT)) {
        if (reduction->child_count != 2) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* member = build_js_member_from_children(sink->transpiler,
            reduction->span, nodes[0], nodes[1],
            reduction->form == JS_REDUCTION_SUBSCRIPT ||
                (reduction->flags & JS_REDUCTION_FLAG_COMPUTED) != 0,
            (reduction->flags & JS_REDUCTION_FLAG_OPTIONAL) != 0);
        return js_c_push_result(sink, member, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_EXPRESSION &&
            reduction->form == JS_REDUCTION_ARRAY) {
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* elements = js_c_link_children(nodes, 0,
            reduction->child_count);
        JsAstNode* array = build_js_array_from_list(sink->transpiler,
            reduction->span, elements, reduction->child_count);
        return js_c_push_result(sink, array, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_STATEMENT_WRAPPER) {
        JsAstNode* statement = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &statement,
                reduction->span)) return false;
        return js_c_push(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_EXPRESSION_STATEMENT) {
        JsAstNode* expression = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &expression,
                reduction->span)) return false;
        if (expression && expression->node_type == JS_AST_NODE_OBJECT_EXPRESSION &&
                expression->source_span.start_byte == reduction->span.start_byte) {
            JsAstNode* block = build_js_statement_block_from_object(
                sink->transpiler, reduction->span, expression);
            if (block) return js_c_push(sink, block, reduction->span);
        }
        JsAstNode* statement = build_js_expression_statement_from_child(
            sink->transpiler, reduction->span, expression);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_IF) {
        if (reduction->child_count < 1 || reduction->child_count > 3) {
            return js_c_unsupported(sink);
        }
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) {
            return false;
        }
        bool empty_consequent =
            (reduction->child_flags & JS_REDUCTION_CHILD_EMPTY_CONSEQUENT) != 0;
        JsAstNode* consequent = empty_consequent ? NULL
            : (reduction->child_count >= 2 ? nodes[1] : NULL);
        JsAstNode* alternate = empty_consequent
            ? (reduction->child_count == 2 ? nodes[1] : NULL)
            : (reduction->child_count == 3 ? nodes[2] : NULL);
        JsAstNode* statement = build_js_if_from_children(sink->transpiler,
            reduction->span, nodes[0], consequent, alternate);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_WHILE) {
        JsAstNode** nodes = NULL;
        if ((reduction->child_count != 1 && reduction->child_count != 2) ||
                !js_c_take_values(sink, reduction->child_count,
                    reduction->span, &nodes)) return false;
        JsAstNode* statement = build_js_while_from_children(
            sink->transpiler, reduction->span, nodes[0],
            reduction->child_count == 2 ? nodes[1] : NULL);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_DO_WHILE) {
        JsAstNode** nodes = NULL;
        if ((reduction->child_count != 2 && reduction->child_count != 1) ||
                !js_c_take_values(sink, reduction->child_count,
                    reduction->span, &nodes)) return false;
        bool missing_body =
            (reduction->child_flags & JS_REDUCTION_CHILD_MISSING_BODY) != 0;
        if (reduction->child_count == 1 && !missing_body) {
            return js_c_unsupported(sink);
        }
        JsAstNode* statement = build_js_do_while_from_children(
            sink->transpiler, reduction->span,
            missing_body ? NULL : nodes[0],
            missing_body ? nodes[0] : nodes[1]);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_RETURN) {
        if (reduction->child_count > 1) return js_c_unsupported(sink);
        JsAstNode** nodes = NULL;
        if (!js_c_take_values(sink, reduction->child_count, reduction->span,
                &nodes)) return false;
        JsAstNode* statement = build_js_return_from_child(sink->transpiler,
            reduction->span, reduction->child_count ? nodes[0] : NULL);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_THROW) {
        JsAstNode* argument = NULL;
        if (!js_c_pop_value(sink, reduction->child_count, &argument,
                reduction->span)) return false;
        JsAstNode* statement = build_js_throw_from_child(sink->transpiler,
            reduction->span, argument);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            (reduction->form == JS_REDUCTION_BREAK ||
             reduction->form == JS_REDUCTION_CONTINUE)) {
        if (reduction->child_count != 0) return js_c_unsupported(sink);
        StrView label = {NULL, 0};
        if (reduction->operator_token.kind != JS_TOK_EOF &&
                !js_c_source_slice(sink, reduction->operator_token.span,
                    &label)) return js_c_unsupported(sink);
        JsAstNode* statement = build_js_break_continue(sink->transpiler,
            reduction->span, reduction->form == JS_REDUCTION_CONTINUE, label);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_LABELED) {
        JsAstNode* body = NULL;
        JsAstNode** nodes = NULL;
        if (reduction->child_count > 1 ||
                !js_c_take_values(sink, reduction->child_count, reduction->span,
                    &nodes)) return false;
        if (reduction->child_count) body = nodes[0];
        StrView label;
        if (!js_c_source_slice(sink, reduction->operator_token.span, &label)) {
            return js_c_unsupported(sink);
        }
        JsAstNode* statement = build_js_labeled_from_child(
            sink->transpiler, reduction->span, label, body);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_STATEMENT &&
            reduction->form == JS_REDUCTION_WITH) {
        JsAstNode* object = NULL;
        JsAstNode* body = NULL;
        JsAstNode** nodes = NULL;
        if ((reduction->child_count != 1 && reduction->child_count != 2) ||
                !js_c_take_values(sink, reduction->child_count, reduction->span,
                    &nodes)) return false;
        object = nodes[0];
        if (reduction->child_count == 2) body = nodes[1];
        JsAstNode* statement = build_js_with_from_children(
            sink->transpiler, reduction->span, object, body);
        return js_c_push_result(sink, statement, reduction->span);
    }

    if (reduction->kind == JS_REDUCE_PROGRAM) {
        SourceSpan program_span = reduction->span;
        JsProgramNode* program = (JsProgramNode*)alloc_js_ast_node_span(
            sink->transpiler, JS_AST_NODE_PROGRAM, program_span,
            sizeof(JsProgramNode));
        if (!program) return js_c_unsupported(sink);
        JsAstNode* body = NULL;
        while (sink->values) {
            JsCParseValue* value = sink->values;
            sink->values = value->below;
            if (!value->node) return js_c_unsupported(sink);
            // Decorated declarations are represented as a decorator chain
            // ending in the declaration; append the preceding program body
            // after that chain so the class/function is not lost.
            JsAstNode* tail = value->node;
            while (tail->next) tail = tail->next;
            tail->next = body;
            body = value->node;
        }
        program->body = body;
        if (body) program->source_span.start_byte = (uint32_t)
            js_c_program_start(sink->source, body->source_span.start_byte,
                sink->source_length);
        program->has_use_strict_directive =
            js_ast_statement_list_has_use_strict_directive(body);
        program->type = &TYPE_ANY;
        sink->root = (JsAstNode*)program;
        return true;
    }

    // The parser may recognize more syntax than this first AST slice can
    // preserve. Reject it before a partial root can become executable.
    return js_c_unsupported(sink);
}

static void js_c_set_parse_error(JsTranspiler* tp, const char* source,
        size_t length, const JsParseError* error) {
    if (!tp) return;
    tp->parse_error_valid = true;
    size_t offset = error && error->span.start_byte <= length
        ? error->span.start_byte : length;
    int64_t row = 1;
    int64_t col = 0;
    for (size_t i = 0; i < offset; i++) {
        if (source[i] == '\n') {
            row++;
            col = 0;
        } else {
            col++;
        }
    }
    tp->parse_error_row = row;
    tp->parse_error_col = col;
    const char* message = error && error->message ? error->message :
        "JavaScript C parser rejected the source";
    strncpy(tp->parse_error_message, message,
        sizeof(tp->parse_error_message) - 1);
    tp->parse_error_message[sizeof(tp->parse_error_message) - 1] = '\0';
}

typedef struct JsCCompilePassContext {
    JsTranspiler* transpiler;
    const char* source;
    size_t source_length;
    JsParseMode mode;
    JsAstNode* root;
    int validation_errors;
} JsCCompilePassContext;

static int js_parse_build_compiler_pass(void* opaque) {
    JsCCompilePassContext* pass = (JsCCompilePassContext*)opaque;
    JsTranspiler* tp = pass ? pass->transpiler : NULL;
    const char* source = pass ? pass->source : NULL;
    size_t length = pass ? pass->source_length : 0;
    JsParseMode mode = pass ? pass->mode : JS_PARSE_SCRIPT;
    if (!tp || !source || length > UINT32_MAX) return 0;
    tp->source = source;
    tp->source_length = length;
    tp->parse_error_valid = false;
    tp->parse_error_message[0] = '\0';
    tp->has_errors = false;
    tp->binding_error_count = 0;
    tp->ast_root = NULL;
    bool caller_strict = tp->strict_mode;
    tp->strict_js = !(mode & JS_PARSE_TYPESCRIPT);
    tp->is_module = (mode & JS_PARSE_MODULE) != 0;
    tp->is_es_module = tp->is_module;
    tp->strict_mode = caller_strict || tp->is_module;
    if ((mode & JS_PARSE_TYPESCRIPT) && !tp->type_registry) {
        ts_type_registry_init(tp);
    }

    JsCAstSink sink_context = {tp, source, length, NULL, {NULL, NULL}, NULL,
        false, JS_REDUCE_PROGRAM, JS_REDUCTION_NONE, 0, 0};
    JsParseSink sink = {js_c_reduce};
    JsParseError error = {};
    JsParseMetrics metrics = {};
    JsParseStatus status = js_parser_parse_source(source, length, mode, &sink,
        &sink_context, &metrics, &error);
    if (status != JS_PARSE_OK || sink_context.unsupported || !sink_context.root) {
        tp->has_errors = true;
        if (sink_context.unsupported) {
            JsParseError rejected_error = {};
            rejected_error.span.start_byte = sink_context.rejected_start;
            rejected_error.message = "JavaScript C AST reduction rejected";
            js_c_set_parse_error(tp, source, length, &rejected_error);
            log_error("js-c: reduction rejected kind=%d form=%d span=%u..%u",
                (int)sink_context.rejected_kind,
                (int)sink_context.rejected_form,
                sink_context.rejected_start, sink_context.rejected_end);
            strncpy(tp->parse_error_message,
                "JavaScript C AST reduction rejected: ",
                sizeof(tp->parse_error_message) - 1);
            strncat(tp->parse_error_message,
                js_c_reduction_form_name(sink_context.rejected_form),
                sizeof(tp->parse_error_message) -
                    strlen(tp->parse_error_message) - 1);
            tp->parse_error_message[sizeof(tp->parse_error_message) - 1] = '\0';
        } else if (status != JS_PARSE_OK) {
            js_c_set_parse_error(tp, source, length, &error);
        } else {
            tp->parse_error_valid = true;
            strncpy(tp->parse_error_message,
                "JavaScript C AST root was not published",
                sizeof(tp->parse_error_message) - 1);
            tp->parse_error_message[sizeof(tp->parse_error_message) - 1] = '\0';
        }
        return 0;
    }
    pass->root = sink_context.root;
    return 1;
}

static int js_bind_compiler_pass(void* opaque) {
    JsCCompilePassContext* pass = (JsCCompilePassContext*)opaque;
    JsTranspiler* tp = pass ? pass->transpiler : NULL;
    if (!tp || !pass->root || pass->root->node_type != JS_AST_NODE_PROGRAM) return 0;
    // Directive prologues determine strictness before declaration binding
    // instantiation; the rebuilt scope graph must see that mode when deciding
    // whether Annex-B block functions receive an outer var companion.
    JsProgramNode* program = (JsProgramNode*)pass->root;
    if (program->has_use_strict_directive) {
        tp->strict_mode = true;
    }
    if (!js_rebuild_direct_scope_graph(tp, pass->root)) {
        tp->has_errors = true;
        tp->parse_error_valid = true;
        strncpy(tp->parse_error_message,
            "JavaScript C scope graph construction failed",
            sizeof(tp->parse_error_message) - 1);
        tp->parse_error_message[sizeof(tp->parse_error_message) - 1] = '\0';
        return 0;
    }
    // The shared Script owner is the AST lifetime authority after adoption.
    tp->ast_root = (AstNode*)pass->root;
    js_report_any_census(tp);
    return 1;
}

static int js_validate_compiler_pass(void* opaque) {
    JsCCompilePassContext* pass = (JsCCompilePassContext*)opaque;
    if (!pass || !pass->transpiler || !pass->root) return 0;
    pass->validation_errors = js_check_early_errors(pass->transpiler, pass->root);
    return pass->validation_errors == 0;
}

static int js_index_compiler_pass(void* opaque) {
    JsCCompilePassContext* pass = (JsCCompilePassContext*)opaque;
    if (!pass || !pass->transpiler || !pass->root) return 0;
    AstIndexPassContext index_context = {
        &pass->transpiler->ast_index, (AstNode*)pass->root,
        pass->transpiler->profile};
    return ast_index_compiler_pass(&index_context);
}

bool js_transpiler_parse_c(JsTranspiler* tp, const char* source, size_t length,
        JsParseMode mode) {
    if (!tp || !source || length > UINT32_MAX) return false;
    if (mode == JS_PARSE_AUTO) {
        mode = tp->strict_js ? JS_PARSE_SCRIPT : JS_PARSE_TYPESCRIPT;
        if (js_c_source_is_module(source, length)) mode = (JsParseMode)
            (mode | JS_PARSE_MODULE);
    }
    JsCCompilePassContext pass_context = {tp, source, length, mode, NULL, -1};
    CompilerPassManager* pass_manager = &tp->pass_manager;
    compiler_pass_manager_init(pass_manager, COMPILER_FACT_NONE);
    CompilerPassSpec passes[] = {
        {"parse-build", COMPILER_FACT_NONE, COMPILER_FACT_AST, js_parse_build_compiler_pass, &pass_context},
        {"bind", COMPILER_FACT_AST, COMPILER_FACT_BOUND, js_bind_compiler_pass, &pass_context},
        {"validate", COMPILER_FACT_AST | COMPILER_FACT_BOUND, COMPILER_FACT_VALIDATED, js_validate_compiler_pass, &pass_context},
        {"index", COMPILER_FACT_FRONTEND, COMPILER_FACT_INDEXED, js_index_compiler_pass, &pass_context},
    };
    for (uint32_t i = 0; i < 4; i++) {
        if (!compiler_pass_manager_add(pass_manager, &passes[i])) break;
    }
    if (pass_manager->pass_count == 4 && compiler_pass_manager_run(pass_manager, NULL)) return true;
    if (pass_context.validation_errors > 0) return true;
    if (!tp->has_errors) {
        tp->parse_error_valid = tp->has_errors = true;
        strncpy(tp->parse_error_message, "JavaScript C AST indexing failed",
            sizeof(tp->parse_error_message) - 1);
        tp->parse_error_message[sizeof(tp->parse_error_message) - 1] = '\0';
    }
    tp->ast_root = NULL;
    return false;
}
