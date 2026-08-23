/**
 * @file re2_wrapper.cpp
 * @brief RE2 regex wrapper implementation for Lambda string pattern matching
 * @author Henry Luo
 * @license MIT
 */

#include "re2_wrapper.hpp"
#include "ast.hpp"
#ifndef SIMPLE_SCHEMA_PARSER
#include "../lambda.hpp"
#include "heap_api.h"
#endif
#include "../../lib/re2_glue.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"

#include <re2/re2.h>
#include <stdint.h>
#include <string>

// runtime functions needed for pattern_find_all, pattern_split
// Only available in main executable, not in shared library (lambda-input-full-cpp)
#ifndef SIMPLE_SCHEMA_PARSER
extern "C" {
    List* list();
    void list_push(List *list, Item item);
    void* heap_calloc(size_t size, TypeId type_id);
    void* heap_data_calloc(size_t size);
    String* heap_strcpy(const char* src, int64_t len);
}
extern __thread EvalContext* context;
#endif

// Convert Lambda occurrence syntax [n], [n, m], [n+] to regex {n}, {n,m}, {n,}
// Input: "[3]", "[2, 5]", "[3+]"
// Output to regex buffer: "{3}", "{2,5}", "{3,}"
static void convert_occurrence_to_regex(StrBuf* regex, StrView* op_str) {
    if (!op_str || !op_str->str || op_str->length < 3) {
        log_error("convert_occurrence_to_regex: invalid op_str");
        return;
    }

    const char* s = op_str->str;
    size_t len = op_str->length;

    // Skip leading '[' and trailing ']'
    if (s[0] != '[' || s[len-1] != ']') {
        // Fallback: might be old {n} syntax or something else, append as-is
        strbuf_append_str_n(regex, s, len);
        return;
    }

    // Parse content between [ and ]
    // Forms: "n", "n+", "n, m"
    strbuf_append_char(regex, '{');

    size_t i = 1;  // skip '['
    // Parse first number
    while (i < len - 1 && (s[i] >= '0' && s[i] <= '9')) {
        strbuf_append_char(regex, s[i]);
        i++;
    }

    // Skip whitespace
    while (i < len - 1 && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }

    if (i >= len - 1) {
        // Just [n] -> {n}
        strbuf_append_char(regex, '}');
    } else if (s[i] == '+') {
        // [n+] -> {n,}
        strbuf_append_str(regex, ",}");
    } else if (s[i] == ',') {
        // [n, m] -> {n,m}
        i++;  // skip ','
        // Skip whitespace after comma
        while (i < len - 1 && (s[i] == ' ' || s[i] == '\t')) {
            i++;
        }
        strbuf_append_char(regex, ',');
        // Parse second number
        while (i < len - 1 && (s[i] >= '0' && s[i] <= '9')) {
            strbuf_append_char(regex, s[i]);
            i++;
        }
        strbuf_append_char(regex, '}');
    } else {
        // Unknown format, close the brace
        strbuf_append_char(regex, '}');
    }
}

// Escape regex metacharacters in a literal string
void escape_regex_literal(StrBuf* regex, String* str) {
    if (!str) return;

    for (size_t i = 0; i < str->len; i++) {
        char c = str->chars[i];
        // RE2 metacharacters that need escaping
        switch (c) {
        case '\\': case '.': case '+': case '*': case '?':
        case '(': case ')': case '[': case ']': case '{': case '}':
        case '|': case '^': case '$':
            strbuf_append_char(regex, '\\');
            break;
        default:
            break;
        }
        strbuf_append_char(regex, c);
    }
}

// Convert character class to regex
static void compile_char_class(StrBuf* regex, PatternCharClass char_class) {
    switch (char_class) {
    case PATTERN_DIGIT:
        strbuf_append_str(regex, "[0-9]");
        break;
    case PATTERN_WORD:
        strbuf_append_str(regex, "[a-zA-Z0-9_]");
        break;
    case PATTERN_SPACE:
        strbuf_append_str(regex, "\\s");
        break;
    case PATTERN_ALPHA:
        strbuf_append_str(regex, "[a-zA-Z]");
        break;
    case PATTERN_ANY:
        strbuf_append_str(regex, ".");
        break;
    case PATTERN_ANY_STRING:
        strbuf_append_str(regex, ".*");
        break;
    }
}

static bool compile_negated_char_class(StrBuf* regex, AstNode* node) {
    if (!node || node->node_type != AST_NODE_PATTERN_CHAR_CLASS) return false;
    AstPatternCharClassNode* cc = (AstPatternCharClassNode*)node;
    switch (cc->char_class) {
    case PATTERN_DIGIT:
        strbuf_append_str(regex, "[^0-9]");
        return true;
    case PATTERN_WORD:
        strbuf_append_str(regex, "[^a-zA-Z0-9_]");
        return true;
    case PATTERN_SPACE:
        strbuf_append_str(regex, "[^\\s]");
        return true;
    case PATTERN_ALPHA:
        strbuf_append_str(regex, "[^a-zA-Z]");
        return true;
    default:
        return false;
    }
}

// Convert pattern AST to regex string
void compile_pattern_to_regex(StrBuf* regex, AstNode* node) {
    if (!node) {
        log_error("compile_pattern_to_regex: null node");
        return;
    }

    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        // String literal - escape and emit
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->type && pri->type->type_id == LMD_TYPE_STRING) {
            TypeString* str_type = (TypeString*)pri->type;
            if (str_type->string) {
                escape_regex_literal(regex, str_type->string);
            }
        } else if (pri->expr) {
            // Parenthesized expression
            compile_pattern_to_regex(regex, pri->expr);
        }
        break;
    }

    case AST_NODE_PATTERN_CHAR_CLASS: {
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)node;
        compile_char_class(regex, cc->char_class);
        break;
    }

    case AST_NODE_PATTERN_RANGE: {
        // "a" to "z" -> [a-z]
        AstPatternRangeNode* range = (AstPatternRangeNode*)node;
        strbuf_append_char(regex, '[');

        // Extract start character
        if (range->start && range->start->type && range->start->type->type_id == LMD_TYPE_STRING) {
            TypeString* start_type = (TypeString*)range->start->type;
            if (start_type->string && start_type->string->len > 0) {
                char c = start_type->string->chars[0];
                // Escape if needed in character class
                if (c == ']' || c == '\\' || c == '^' || c == '-') {
                    strbuf_append_char(regex, '\\');
                }
                strbuf_append_char(regex, c);
            }
        }

        strbuf_append_char(regex, '-');

        // Extract end character
        if (range->end && range->end->type && range->end->type->type_id == LMD_TYPE_STRING) {
            TypeString* end_type = (TypeString*)range->end->type;
            if (end_type->string && end_type->string->len > 0) {
                char c = end_type->string->chars[0];
                if (c == ']' || c == '\\' || c == '^' || c == '-') {
                    strbuf_append_char(regex, '\\');
                }
                strbuf_append_char(regex, c);
            }
        }

        strbuf_append_char(regex, ']');
        break;
    }

    case AST_NODE_BINARY:
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* bin = (AstBinaryNode*)node;
        if (bin->op == OPERATOR_UNION) {
            // a | b -> (?:a|b)
            strbuf_append_str(regex, "(?:");
            compile_pattern_to_regex(regex, bin->left);
            strbuf_append_char(regex, '|');
            compile_pattern_to_regex(regex, bin->right);
            strbuf_append_char(regex, ')');
        } else if (bin->op == OPERATOR_INTERSECT) {
            // a & b -> positive lookahead (?=a)b
            // Note: This is a limited intersection, may not work for all cases
            strbuf_append_str(regex, "(?=");
            compile_pattern_to_regex(regex, bin->left);
            strbuf_append_char(regex, ')');
            compile_pattern_to_regex(regex, bin->right);
        } else if (bin->op == OPERATOR_TO) {
            // Range operator (same as PATTERN_RANGE but from binary expression)
            strbuf_append_char(regex, '[');
            if (bin->left && bin->left->type && bin->left->type->type_id == LMD_TYPE_STRING) {
                TypeString* start_type = (TypeString*)bin->left->type;
                if (start_type->string && start_type->string->len > 0) {
                    strbuf_append_char(regex, start_type->string->chars[0]);
                }
            }
            strbuf_append_char(regex, '-');
            if (bin->right && bin->right->type && bin->right->type->type_id == LMD_TYPE_STRING) {
                TypeString* end_type = (TypeString*)bin->right->type;
                if (end_type->string && end_type->string->len > 0) {
                    strbuf_append_char(regex, end_type->string->chars[0]);
                }
            }
            strbuf_append_char(regex, ']');
        } else {
            log_error("compile_pattern_to_regex: unknown binary operator %d", bin->op);
        }
        break;
    }

    case AST_NODE_UNARY:
    case AST_NODE_UNARY_TYPE: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if (unary->op == OPERATOR_OPTIONAL) {
            // a? -> (?:a)?
            strbuf_append_str(regex, "(?:");
            compile_pattern_to_regex(regex, unary->operand);
            strbuf_append_str(regex, ")?");
        } else if (unary->op == OPERATOR_ONE_MORE) {
            // a+ -> (?:a)+
            strbuf_append_str(regex, "(?:");
            compile_pattern_to_regex(regex, unary->operand);
            strbuf_append_str(regex, ")+");
        } else if (unary->op == OPERATOR_ZERO_MORE) {
            // a* -> (?:a)*
            strbuf_append_str(regex, "(?:");
            compile_pattern_to_regex(regex, unary->operand);
            strbuf_append_str(regex, ")*");
        } else if (unary->op == OPERATOR_REPEAT) {
            // [n], [n+], [n, m] -> (?:a){n}, (?:a){n,}, (?:a){n,m}
            strbuf_append_str(regex, "(?:");
            compile_pattern_to_regex(regex, unary->operand);
            strbuf_append_str(regex, ")");
            // Convert Lambda occurrence syntax [n], [n, m], [n+] to regex {n}, {n,m}, {n,}
            if (unary->op_str.str && unary->op_str.length > 0) {
                convert_occurrence_to_regex(regex, &unary->op_str);
            }
        } else if (unary->op == OPERATOR_NOT) {
            // RE2 has no look-around; negate a character class directly so `!d` retains one-codepoint complement semantics.
            if (!compile_negated_char_class(regex, unary->operand)) {
                log_error("compile_pattern_to_regex: negation requires a character class");
            }
        } else {
            log_error("compile_pattern_to_regex: unknown unary operator %d", unary->op);
        }
        break;
    }

    case AST_NODE_PATTERN_SEQ: {
        // Pattern sequence - concatenate all patterns in sequence
        AstPatternSeqNode* seq = (AstPatternSeqNode*)node;
        AstNode* child = seq->first;
        while (child) {
            compile_pattern_to_regex(regex, child);
            child = child->next;
        }
        break;
    }

    case AST_NODE_LIST_TYPE: {
        // Parenthesized type expression in pattern context: (pattern)
        // list_type can contain one or more items separated by commas.
        // In pattern context, a single item is a group; multiple items form alternatives.
        AstListNode* list = (AstListNode*)node;
        AstNode* item = list->item;
        if (item && !item->next) {
            // Single item — just a grouping parenthesis
            strbuf_append_str(regex, "(?:");
            compile_pattern_to_regex(regex, item);
            strbuf_append_char(regex, ')');
        } else if (item) {
            // Multiple items — treat as alternatives (union)
            strbuf_append_str(regex, "(?:");
            compile_pattern_to_regex(regex, item);
            item = item->next;
            while (item) {
                strbuf_append_char(regex, '|');
                compile_pattern_to_regex(regex, item);
                item = item->next;
            }
            strbuf_append_char(regex, ')');
        }
        break;
    }

    case AST_NODE_ARRAY_TYPE: {
        // Array type in pattern context: [pattern] — treat as character class or group
        AstArrayNode* arr = (AstArrayNode*)node;
        if (arr->item) {
            strbuf_append_str(regex, "(?:");
            compile_pattern_to_regex(regex, arr->item);
            AstNode* item = arr->item->next;
            while (item) {
                strbuf_append_char(regex, '|');
                compile_pattern_to_regex(regex, item);
                item = item->next;
            }
            strbuf_append_char(regex, ')');
        }
        break;
    }

    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        if (ident->entry && ident->entry->node &&
                (ident->entry->node->node_type == AST_NODE_STRING_PATTERN ||
                 ident->entry->node->node_type == AST_NODE_SYMBOL_PATTERN)) {
            AstPatternDefNode* definition = (AstPatternDefNode*)ident->entry->node;
            compile_pattern_to_regex(regex, definition->as);
        } else {
            log_error("compile_pattern_to_regex: unresolved pattern reference '%.*s'",
                ident->name ? (int)ident->name->len : 0,
                ident->name ? ident->name->chars : "");
        }
        break;
    }

    default:
        log_error("compile_pattern_to_regex: unknown node type %d", node->node_type);
        break;
    }
}

static void render_pattern_literal(StrBuf* source, String* value) {
    strbuf_append_char(source, '"');
    if (value) {
        for (size_t i = 0; i < value->len; i++) {
            unsigned char c = (unsigned char)value->chars[i];
            switch (c) {
            case '\\': strbuf_append_str(source, "\\\\"); break;
            case '"': strbuf_append_str(source, "\\\""); break;
            case '\n': strbuf_append_str(source, "\\n"); break;
            case '\r': strbuf_append_str(source, "\\r"); break;
            case '\t': strbuf_append_str(source, "\\t"); break;
            default: strbuf_append_char(source, (char)c); break;
            }
        }
    }
    strbuf_append_char(source, '"');
}

static void render_pattern_surface(StrBuf* source, AstNode* node) {
    if (!node) return;
    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        if (primary->type && primary->type->type_id == LMD_TYPE_STRING) {
            render_pattern_literal(source, ((TypeString*)primary->type)->string);
        } else {
            render_pattern_surface(source, primary->expr);
        }
        break;
    }
    case AST_NODE_PATTERN_CHAR_CLASS: {
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)node;
        switch (cc->char_class) {
        case PATTERN_DIGIT: strbuf_append_char(source, 'd'); break;
        case PATTERN_WORD: strbuf_append_char(source, 'w'); break;
        case PATTERN_SPACE: strbuf_append_char(source, 's'); break;
        case PATTERN_ALPHA: strbuf_append_char(source, 'a'); break;
        case PATTERN_ANY: strbuf_append_char(source, '.'); break;
        case PATTERN_ANY_STRING: strbuf_append_str(source, "..."); break;
        }
        break;
    }
    case AST_NODE_PATTERN_RANGE: {
        AstPatternRangeNode* range = (AstPatternRangeNode*)node;
        render_pattern_surface(source, range->start);
        strbuf_append_str(source, " to ");
        render_pattern_surface(source, range->end);
        break;
    }
    case AST_NODE_BINARY:
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        strbuf_append_char(source, '(');
        render_pattern_surface(source, binary->left);
        if (binary->op == OPERATOR_UNION) strbuf_append_str(source, " | ");
        else if (binary->op == OPERATOR_INTERSECT) strbuf_append_str(source, " & ");
        else if (binary->op == OPERATOR_EXCLUDE) strbuf_append_str(source, " ! ");
        else if (binary->op == OPERATOR_TO) strbuf_append_str(source, " to ");
        render_pattern_surface(source, binary->right);
        strbuf_append_char(source, ')');
        break;
    }
    case AST_NODE_UNARY:
    case AST_NODE_UNARY_TYPE: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if (unary->op == OPERATOR_NOT) {
            strbuf_append_char(source, '!');
            strbuf_append_char(source, '(');
            render_pattern_surface(source, unary->operand);
            strbuf_append_char(source, ')');
        } else {
            strbuf_append_char(source, '(');
            render_pattern_surface(source, unary->operand);
            strbuf_append_char(source, ')');
            if (unary->op_str.str && unary->op_str.length > 0) {
                strbuf_append_str_n(source, unary->op_str.str, unary->op_str.length);
            }
        }
        break;
    }
    case AST_NODE_PATTERN_SEQ: {
        AstPatternSeqNode* sequence = (AstPatternSeqNode*)node;
        AstNode* child = sequence->first;
        bool first = true;
        while (child) {
            if (!first) strbuf_append_char(source, ' ');
            render_pattern_surface(source, child);
            first = false;
            child = child->next;
        }
        break;
    }
    case AST_NODE_LIST_TYPE: {
        AstListNode* list = (AstListNode*)node;
        strbuf_append_char(source, '(');
        AstNode* item = list->item;
        bool first = true;
        while (item) {
            if (!first) strbuf_append_str(source, " | ");
            render_pattern_surface(source, item);
            first = false;
            item = item->next;
        }
        strbuf_append_char(source, ')');
        break;
    }
    case AST_NODE_ARRAY_TYPE: {
        AstArrayNode* array = (AstArrayNode*)node;
        strbuf_append_char(source, '[');
        render_pattern_surface(source, array->item);
        strbuf_append_char(source, ']');
        break;
    }
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        if (ident->name) strbuf_append_str_n(source, ident->name->chars, ident->name->len);
        break;
    }
    case AST_NODE_PATTERN_ISLAND: {
        AstPatternIslandNode* island = (AstPatternIslandNode*)node;
        strbuf_append_str(source, island->is_symbol ? "\\symbol(" : "\\(");
        render_pattern_surface(source, island->pattern);
        strbuf_append_char(source, ')');
        break;
    }
    default:
        log_error("render_pattern_surface: unsupported AST node type %d", node->node_type);
        break;
    }
}

static String* pattern_pool_string(Pool* pool, const char* chars, size_t length) {
    String* value = (String*)pool_calloc(pool, sizeof(String) + length + 1);
    value->len = (uint32_t)length;
    value->is_ascii = 1;
    for (size_t i = 0; i < length; i++) {
        value->chars[i] = chars[i];
        if ((unsigned char)chars[i] >= 128) value->is_ascii = 0;
    }
    value->chars[length] = '\0';
    return value;
}

// Compile Lambda pattern AST to RE2 regex
TypePattern* compile_pattern_ast(Pool* pool, AstNode* pattern_ast, bool is_symbol, const char** error_msg) {
    if (!pattern_ast) {
        if (error_msg) *error_msg = "null pattern AST";
        return nullptr;
    }

    // Build regex string
    StrBuf* regex = strbuf_new_cap(256);
    strbuf_append_str(regex, "^");  // anchor start for full match
    compile_pattern_to_regex(regex, pattern_ast);
    strbuf_append_str(regex, "$");  // anchor end

    StrBuf* surface = strbuf_new_cap(256);
    strbuf_append_str(surface, is_symbol ? "\\symbol(" : "\\(");
    render_pattern_surface(surface, pattern_ast);
    strbuf_append_char(surface, ')');

    log_debug("Compiled pattern regex: %s", regex->str);

    re2::RE2::Options options = lam::re2_glue_default_options();
    static char error_buffer[256];
    re2::RE2* re2 = lam::re2_glue_compile(
        regex->str, regex->length, options, "compile_pattern_ast",
        error_msg ? error_buffer : nullptr, sizeof(error_buffer));
    if (!re2) {
        if (error_msg) *error_msg = error_buffer;
        strbuf_free(regex);
        strbuf_free(surface);
        return nullptr;
    }

    // Allocate TypePattern
    TypePattern* pattern = (TypePattern*)pool_calloc(pool, sizeof(TypePattern));
    pattern->type_id = LMD_TYPE_TYPE;
    pattern->kind = TYPE_KIND_PATTERN;
    pattern->is_symbol = is_symbol;
    pattern->re2 = re2;
    pattern->re2_unanchored = nullptr;
    pattern->pattern_index = -1;  // Will be set when registered

    // Keep the diagnostic surface separate from the anchored regex consumed by
    // partial matching; otherwise source rendering changes break find/replace.
    pattern->source = pattern_pool_string(pool, surface->str, surface->length);
    pattern->regex_source = pattern_pool_string(pool, regex->str, regex->length);

    strbuf_free(regex);
    strbuf_free(surface);
    return pattern;
}

bool compile_runtime_pattern(Pool* pool, ArrayList* type_list, TypePattern* pattern,
                             AstNode* pattern_ast, bool is_symbol) {
    if (!pool || !type_list || !pattern || !pattern_ast) return false;
    if (pattern->re2) {
        if (pattern->pattern_index >= 0) return true;
        // A direct parser may compile the regex before the module type-list
        // publication pass. Publish that existing compiled identity instead
        // of rejecting an otherwise valid named pattern at its first use.
        arraylist_append(type_list, pattern);
        pattern->pattern_index = type_list->length - 1;
        return true;
    }

    const char* error_msg = NULL;
    TypePattern* compiled = compile_pattern_ast(pool, pattern_ast, is_symbol, &error_msg);
    if (!compiled) {
        log_error("pattern compile: failed to build regex: %s",
            error_msg ? error_msg : "unknown error");
        return false;
    }
    pattern->re2 = compiled->re2;
    pattern->re2_unanchored = NULL;
    pattern->source = compiled->source;
    pattern->regex_source = compiled->regex_source;
    arraylist_append(type_list, pattern);
    pattern->pattern_index = type_list->length - 1;
    return true;
}

// Walk the declaration-bearing shapes that can contain a named pattern. This
// stays beside compile_pattern_ast so MIR lowering and the AST interpreter
// publish exactly one module-local TypePattern identity per definition.
bool compile_script_pattern_definitions(Pool* pool, ArrayList* type_list,
                                        AstNode* node) {
    if (!pool || !type_list) return false;
    bool ok = true;
    while (node) {
        switch (node->node_type) {
        case AST_NODE_STRING_PATTERN:
        case AST_NODE_SYMBOL_PATTERN: {
            AstPatternDefNode* definition = (AstPatternDefNode*)node;
            TypePattern* pattern = (TypePattern*)definition->type;
            if (!pattern || !definition->as) {
                log_error("pattern prepass: invalid definition '%.*s'",
                    definition->name ? (int)definition->name->len : 0,
                    definition->name ? definition->name->chars : "");
                ok = false;
                break;
            }
            bool needs_compile = !pattern->re2;
            if (!compile_runtime_pattern(pool, type_list, pattern, definition->as,
                    definition->is_symbol)) {
                log_error("pattern prepass: failed to compile '%.*s'",
                    (int)definition->name->len, definition->name->chars);
                ok = false;
                break;
            }
            if (needs_compile) {
                log_debug("pattern prepass: compiled '%.*s' index=%d",
                    (int)definition->name->len, definition->name->chars,
                    pattern->pattern_index);
            }
            break;
        }
        case AST_NODE_CONTENT:
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare &&
                    !compile_script_pattern_definitions(pool, type_list, list->declare)) {
                ok = false;
            }
            if (!compile_script_pattern_definitions(pool, type_list, list->item)) ok = false;
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM:
            if (!compile_script_pattern_definitions(pool, type_list,
                    ((AstLetNode*)node)->declare)) ok = false;
            break;
        case AST_NODE_OBJECT_TYPE: {
            AstObjectTypeNode* object = (AstObjectTypeNode*)node;
            if (!compile_script_pattern_definitions(pool, type_list, object->methods)) ok = false;
            break;
        }
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR:
            if (!compile_script_pattern_definitions(pool, type_list,
                    ((AstFuncNode*)node)->body)) ok = false;
            break;
        default:
            break;
        }
        node = node->next;
    }
    return ok;
}

static bool append_literal_type_regex(StrBuf* regex, Type* type) {
    if (!type) return false;
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_SIMPLE) {
        type = ((TypeType*)type)->type;
    }
    if (!type) return false;
    if (type->type_id == LMD_TYPE_STRING && type->is_literal) {
        String* literal = ((TypeString*)type)->string;
        if (!literal) return false;
        escape_regex_literal(regex, literal);
        return true;
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY) {
        TypeBinary* binary = (TypeBinary*)type;
        if (binary->op != OPERATOR_UNION) return false;
        strbuf_append_str(regex, "(?:");
        if (!append_literal_type_regex(regex, binary->left)) return false;
        strbuf_append_char(regex, '|');
        if (!append_literal_type_regex(regex, binary->right)) return false;
        strbuf_append_char(regex, ')');
        return true;
    }
    return false;
}

static bool append_literal_type_surface(StrBuf* surface, Type* type) {
    if (!type) return false;
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_SIMPLE) {
        type = ((TypeType*)type)->type;
    }
    if (!type) return false;
    if (type->type_id == LMD_TYPE_STRING && type->is_literal) {
        String* literal = ((TypeString*)type)->string;
        if (!literal) return false;
        render_pattern_literal(surface, literal);
        return true;
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY) {
        TypeBinary* binary = (TypeBinary*)type;
        if (binary->op != OPERATOR_UNION) return false;
        strbuf_append_char(surface, '(');
        if (!append_literal_type_surface(surface, binary->left)) return false;
        strbuf_append_str(surface, " | ");
        if (!append_literal_type_surface(surface, binary->right)) return false;
        strbuf_append_char(surface, ')');
        return true;
    }
    return false;
}

TypePattern* compile_literal_type_pattern(Pool* pool, Type* type, bool is_symbol,
                                          const char** error_msg) {
    if (!pool || !type) {
        if (error_msg) *error_msg = "null literal type";
        return nullptr;
    }

    StrBuf* regex = strbuf_new_cap(128);
    strbuf_append_char(regex, '^');
    if (!append_literal_type_regex(regex, type)) {
        if (error_msg) *error_msg = "type is not a literal string union";
        strbuf_free(regex);
        return nullptr;
    }
    strbuf_append_char(regex, '$');

    StrBuf* surface = strbuf_new_cap(128);
    strbuf_append_str(surface, is_symbol ? "\\symbol(" : "\\(");
    if (!append_literal_type_surface(surface, type)) {
        if (error_msg) *error_msg = "type is not a literal string union";
        strbuf_free(regex);
        strbuf_free(surface);
        return nullptr;
    }
    strbuf_append_char(surface, ')');

    re2::RE2::Options options = lam::re2_glue_default_options();
    static char error_buffer[256];
    re2::RE2* re2 = lam::re2_glue_compile(
        regex->str, regex->length, options, "compile_literal_type_pattern",
        error_msg ? error_buffer : nullptr, sizeof(error_buffer));
    if (!re2) {
        if (error_msg) *error_msg = error_buffer;
        strbuf_free(regex);
        strbuf_free(surface);
        return nullptr;
    }

    TypePattern* pattern = (TypePattern*)pool_calloc(pool, sizeof(TypePattern));
    pattern->type_id = LMD_TYPE_TYPE;
    pattern->kind = TYPE_KIND_PATTERN;
    pattern->is_symbol = is_symbol;
    pattern->re2 = re2;
    pattern->re2_unanchored = nullptr;
    pattern->pattern_index = -1;
    pattern->source = pattern_pool_string(pool, surface->str, surface->length);
    pattern->regex_source = pattern_pool_string(pool, regex->str, regex->length);

    strbuf_free(regex);
    strbuf_free(surface);
    return pattern;
}

// Match string against pattern (full match)
bool pattern_full_match(TypePattern* pattern, String* str) {
    if (!pattern || !pattern->re2 || !str) {
        return false;
    }

    re2::StringPiece input(str->chars, str->len);
    return re2::RE2::FullMatch(input, *pattern->re2);
}

bool pattern_full_match_chars(TypePattern* pattern, const char* chars, size_t len) {
    if (!pattern || !pattern->re2 || !chars) {
        return false;
    }

    re2::StringPiece input(chars, len);
    return re2::RE2::FullMatch(input, *pattern->re2);
}

// Match string against pattern (partial match)
bool pattern_partial_match(TypePattern* pattern, String* str) {
    if (!pattern || !pattern->re2 || !str) {
        return false;
    }

    re2::StringPiece input(str->chars, str->len);
    return re2::RE2::PartialMatch(input, *pattern->re2);
}

// Destroy a compiled pattern
void pattern_destroy(TypePattern* pattern) {
    if (pattern && pattern->re2) {
        lam::re2_glue_release(pattern->re2);
        pattern->re2 = nullptr;
    }
    if (pattern && pattern->re2_unanchored) {
        lam::re2_glue_release(pattern->re2_unanchored);
        pattern->re2_unanchored = nullptr;
    }
}

// One-shot RE2 helpers — see re2_wrapper.hpp. These are the C+-convention
// boundary: rb_runtime/py_stdlib/etc. call these instead of `new re2::RE2`
// so the new/delete stays inside the wrapper.
re2::RE2* re2_compile(const char* pattern, size_t pattern_len) {
    re2::RE2::Options opts = lam::re2_glue_default_options();
    return lam::re2_glue_compile(pattern, pattern_len, opts, nullptr);
}

void re2_release(re2::RE2* re) {
    lam::re2_glue_release(re);
}

static String* pattern_regex_source(TypePattern* pattern) {
    return pattern ? (pattern->regex_source ? pattern->regex_source : pattern->source) : nullptr;
}

#ifndef SIMPLE_SCHEMA_PARSER
static re2::RE2* pattern_get_unanchored_options(TypePattern* pattern, bool ignore_case, bool* must_release) {
    *must_release = false;
    if (!ignore_case) return pattern_get_unanchored(pattern);
    String* regex_source = pattern_regex_source(pattern);
    if (!regex_source) return nullptr;

    const char* src = regex_source->chars;
    size_t len = regex_source->len;
    if (len < 2 || src[0] != '^' || src[len - 1] != '$') {
        log_error("pattern_get_unanchored_options: unexpected source format: %s", src);
        return nullptr;
    }
    re2::RE2::Options opts = lam::re2_glue_default_options();
    opts.set_case_sensitive(false);
    re2::RE2* re = lam::re2_glue_compile(
        src + 1, len - 2, opts, "pattern_get_unanchored_options");
    if (!re) return nullptr;
    *must_release = true;
    return re;
}

static int64_t pattern_count_matches_with_re(re2::RE2* re, const char* str, size_t len) {
    re2::StringPiece input(str, len);
    re2::StringPiece match;
    size_t pos = 0;
    int64_t count = 0;
    while (pos <= len) {
        if (!re->Match(input, pos, len, re2::RE2::UNANCHORED, &match, 1)) break;
        int64_t match_start = (int64_t)(match.data() - str);
        size_t match_len_val = match.size();
        count++;
        pos = match_start + match_len_val;
        if (match_len_val == 0) pos++;
    }
    return count;
}
#endif

// Get or create unanchored RE2 for partial matching operations.
// The source string is stored as "^<regex>$"; we strip the anchors.
re2::RE2* pattern_get_unanchored(TypePattern* pattern) {
    if (!pattern) return nullptr;
    if (pattern->re2_unanchored) return pattern->re2_unanchored;

    // source is "^<regex>$", strip leading ^ and trailing $
    String* regex_source = pattern_regex_source(pattern);
    if (!regex_source) return nullptr;
    const char* src = regex_source->chars;
    size_t len = regex_source->len;
    if (len < 2 || src[0] != '^' || src[len - 1] != '$') {
        log_error("pattern_get_unanchored: unexpected source format: %s", src);
        return nullptr;
    }
    re2::RE2::Options opts = lam::re2_glue_default_options();
    pattern->re2_unanchored = lam::re2_glue_compile(
        src + 1, len - 2, opts, "pattern_get_unanchored");
    if (!pattern->re2_unanchored) {
        return nullptr;
    }

    log_debug("pattern_get_unanchored: compiled '%.*s'", (int)(len - 2), src + 1);
    return pattern->re2_unanchored;
}

// The following functions are only used in the main executable, not the shared library.
// They depend on runtime symbols (heap_alloc, list, list_push, etc.) not available in lambda-input-full-cpp.
#ifndef SIMPLE_SCHEMA_PARSER

// helper: create a heap-allocated String from a char* + len
static String* make_heap_string(const char* src, size_t len) {
    String* s = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    s->len = (uint32_t)len;
    s->flags = 0;
    s->is_ascii = 1;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)src[i] >= 128) { s->is_ascii = 0; break; }
    }
    memcpy(s->chars, src, len);
    s->chars[len] = '\0';
    return s;
}

// helper: create a match map {value: string, index: int}
// allocates TypeMap + ShapeEntry chain + data buffer on heap
Map* create_match_map(const char* match_str, size_t match_len, int64_t index) {
    Pool* pool = context->pool;
    ArrayList* tl = (ArrayList*)context->type_list;

    // create shape entries: value(string), index(int)
    // entry 1: "value" -> string
    ShapeEntry* e_value = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry) + sizeof(StrView));
    StrView* nv1 = (StrView*)((char*)e_value + sizeof(ShapeEntry));
    nv1->str = "value";
    nv1->length = 5;
    e_value->name = nv1;
    e_value->type = type_info[LMD_TYPE_STRING].type;
    e_value->byte_offset = 0;
    e_value->next = nullptr;

    int64_t offset2 = type_info[LMD_TYPE_STRING].byte_size;

    // entry 2: "index" -> int
    ShapeEntry* e_index = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry) + sizeof(StrView));
    StrView* nv2 = (StrView*)((char*)e_index + sizeof(ShapeEntry));
    nv2->str = "index";
    nv2->length = 5;
    e_index->name = nv2;
    e_index->type = type_info[LMD_TYPE_INT].type;
    e_index->byte_offset = offset2;
    e_index->next = nullptr;

    e_value->next = e_index;

    int64_t byte_size = offset2 + type_info[LMD_TYPE_INT].byte_size;

    // create TypeMap
    TypeMap* mt = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
    mt->shape = e_value;
    mt->last = e_index;
    mt->length = 2;
    mt->byte_size = byte_size;
    mt->type_index = tl->length;
    typemap_hash_build(mt, pool);
    arraylist_append(tl, mt);

    // create Map container
    Map* mp = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    RootFrame roots(1);
    Rooted<Map*> rooted_map(roots, mp);
    mp->type_id = LMD_TYPE_MAP;
    mp->type = mt;
    mp->data = heap_data_calloc(byte_size);

    // store value field (String* pointer)
    String* val_str = make_heap_string(match_str, match_len);
    // Both data-buffer and string allocation may collect; the map is the only
    // owner of its data and is not visible to the caller until this returns.
    mp = rooted_map.get();
    *(String**)((char*)mp->data + e_value->byte_offset) = val_str;

    // Shaped int fields are int64 lanes. Writing IEEE bits here makes the
    // map reader decode an ordinary match offset as the `+inf` sentinel.
    *(int64_t*)((char*)mp->data + e_index->byte_offset) = lambda_double_to_int_lane((double)index);

    return mp;
}

List* pattern_find_all_options(TypePattern* pattern, const char* str, size_t len,
                               int64_t limit, bool ignore_case) {
    List* result = list();
    result->is_content = 1;
    RootFrame roots(2);
    Rooted<List*> rooted_result(roots, result);
    Rooted<Map*> rooted_match(roots, (Map*)NULL);
    if (!pattern || !str || len == 0) return rooted_result.get();

    bool must_release = false;
    re2::RE2* re = pattern_get_unanchored_options(pattern, ignore_case, &must_release);
    if (!re) return rooted_result.get();

    int64_t total = pattern_count_matches_with_re(re, str, len);
    int64_t first = 0, selected_count = 0;
    lam::re2_glue_select_match_window(total, limit, &first, &selected_count);
    if (selected_count == 0) {
        if (must_release) lam::re2_glue_release(re);
        return rooted_result.get();
    }

    re2::StringPiece input(str, len);
    re2::StringPiece match;
    size_t pos = 0;
    int64_t ordinal = 0;
    int64_t pushed = 0;

    while (pos <= len) {
        if (!re->Match(input, pos, len, re2::RE2::UNANCHORED, &match, 1)) {
            break;
        }

        int64_t match_start = (int64_t)(match.data() - str);
        size_t match_len_val = match.size();

        if (ordinal >= first && pushed < selected_count) {
            Map* m = create_match_map(match.data(), match_len_val, match_start);
            // Keep a new match live until the rooted result list owns it.
            rooted_match.set(m);
            list_push(rooted_result.get(), {.map = rooted_match.get()});
            rooted_match.set((Map*)NULL);
            pushed++;
        }
        ordinal++;

        // advance past match; if zero-length match, advance by 1 char
        pos = match_start + match_len_val;
        if (match_len_val == 0) pos++;
        if (pushed >= selected_count) break;
    }

    if (must_release) lam::re2_glue_release(re);
    return rooted_result.get();
}

// Find all non-overlapping matches of pattern in string
List* pattern_find_all(TypePattern* pattern, const char* str, size_t len) {
    return pattern_find_all_options(pattern, str, len, 0, false);
}

String* pattern_replace_all_options(TypePattern* pattern, const char* str, size_t str_len,
                                    const char* repl, size_t repl_len,
                                    int64_t limit, bool ignore_case) {
    if (!pattern || !str) return nullptr;

    bool must_release = false;
    re2::RE2* re = pattern_get_unanchored_options(pattern, ignore_case, &must_release);
    if (!re) return nullptr;

    int64_t total = pattern_count_matches_with_re(re, str, str_len);
    int64_t first = 0, selected_count = 0;
    lam::re2_glue_select_match_window(total, limit, &first, &selected_count);
    if (selected_count == 0) {
        if (must_release) lam::re2_glue_release(re);
        return make_heap_string(str, str_len);
    }

    StrBuf* out = strbuf_new_cap(str_len + 1);
    re2::StringPiece input(str, str_len);
    re2::StringPiece match;
    size_t pos = 0;
    size_t copy_pos = 0;
    int64_t ordinal = 0;
    int64_t replaced = 0;

    while (pos <= str_len) {
        if (!re->Match(input, pos, str_len, re2::RE2::UNANCHORED, &match, 1)) break;
        size_t match_start = (size_t)(match.data() - str);
        size_t match_len_val = match.size();
        bool selected = ordinal >= first && replaced < selected_count;

        if (selected) {
            if (match_start > copy_pos) strbuf_append_str_n(out, str + copy_pos, match_start - copy_pos);
            if (repl && repl_len > 0) strbuf_append_str_n(out, repl, repl_len);
            copy_pos = match_start + match_len_val;
            replaced++;
        }

        ordinal++;
        pos = match_start + match_len_val;
        if (match_len_val == 0) pos++;
        if (replaced >= selected_count) break;
    }
    if (copy_pos < str_len) strbuf_append_str_n(out, str + copy_pos, str_len - copy_pos);

    String* result = make_heap_string(out->str, out->length);
    strbuf_free(out);
    if (must_release) lam::re2_glue_release(re);
    return result;
}

// Replace all non-overlapping matches of pattern in string
String* pattern_replace_all(TypePattern* pattern, const char* str, size_t str_len,
                            const char* repl, size_t repl_len) {
    if (!pattern || !str) return nullptr;

    re2::RE2* re = pattern_get_unanchored(pattern);
    if (!re) return nullptr;

    std::string input(str, str_len); // STD_CONTAINER_OK: RE2::GlobalReplace requires std::string* in/out buffer.
    re2::StringPiece replacement(repl, repl_len);
    RE2::GlobalReplace(&input, *re, replacement);

    return make_heap_string(input.c_str(), input.size());
}

static String* make_heap_rooted_slice(Rooted<Item>& rooted_source, size_t offset, size_t len) {
    String* value = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    // heap_alloc may compact the source; derive its character pointer only
    // after the safepoint instead of retaining a raw nursery interior pointer.
    const char* src = rooted_source.get().get_chars() + offset;
    value->len = (uint32_t)len;
    value->flags = 0;
    value->is_ascii = 1;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)src[i] >= 128) { value->is_ascii = 0; break; }
    }
    memcpy(value->chars, src, len);
    value->chars[len] = '\0';
    return value;
}

// Split string by pattern matches
List* pattern_split(TypePattern* pattern, Item source, bool keep_delim) {
    RootFrame roots(2);
    Rooted<Item> rooted_source(roots, source);
    Rooted<List*> rooted_result(roots, (List*)NULL);
    List* result = list();
    rooted_result.set(result);
    if (!pattern || !source.get_chars()) return rooted_result.get();
    size_t len = source.get_len();
    if (len == 0) return rooted_result.get();

    re2::RE2* re = pattern_get_unanchored(pattern);
    if (!re) return rooted_result.get();

    size_t pos = 0;

    while (pos <= len) {
        // Each preceding result allocation can move the source string.
        const char* str = rooted_source.get().get_chars();
        re2::StringPiece input(str, len);
        re2::StringPiece match;
        if (!re->Match(input, pos, len, re2::RE2::UNANCHORED, &match, 1)) {
            break;
        }

        size_t match_start = match.data() - str;
        size_t match_len_val = match.size();

        // push the part before the match
        size_t part_len = match_start - pos;
        String* part = make_heap_rooted_slice(rooted_source, pos, part_len);
        list_push(rooted_result.get(), {.item = s2it(part)});

        // optionally push the delimiter
        if (keep_delim && match_len_val > 0) {
            String* delim = make_heap_rooted_slice(rooted_source, match_start, match_len_val);
            list_push(rooted_result.get(), {.item = s2it(delim)});
        }

        // advance past match; handle zero-length matches
        pos = match_start + match_len_val;
        if (match_len_val == 0) pos++;
    }

    // push remaining part after last match
    if (pos <= len) {
        size_t part_len = len - pos;
        String* part = make_heap_rooted_slice(rooted_source, pos, part_len);
        list_push(rooted_result.get(), {.item = s2it(part)});
    }

    return rooted_result.get();
}

// C-linkage wrapper for create_match_map, callable from lambda-eval.cpp
extern "C" Map* create_match_map_ext(const char* match_str, size_t match_len, int64_t index) {
    return create_match_map(match_str, match_len, index);
}

#endif // SIMPLE_SCHEMA_PARSER
