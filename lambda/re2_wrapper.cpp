/**
 * @file re2_wrapper.cpp
 * @brief RE2 regex wrapper implementation for Lambda string pattern matching
 * @author Henry Luo
 * @license MIT
 */

#include "re2_wrapper.hpp"
#include "ast.hpp"
#include "../lib/log.h"
#include "../lib/mempool.h"

#include <re2/re2.h>

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
    if (!str || !str->chars) return;

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

    case AST_NODE_BINARY: {
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

    case AST_NODE_UNARY: {
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
            // !a -> negative lookahead (?!a)
            // Note: This matches position, not characters. For proper negation of char class
            // we'd need [^...] but that's context-dependent
            strbuf_append_str(regex, "(?!");
            compile_pattern_to_regex(regex, unary->operand);
            strbuf_append_str(regex, ").");
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

    case AST_NODE_IDENT: {
        // Pattern reference - this should have been resolved during type checking
        // For now, log an error
        AstIdentNode* ident = (AstIdentNode*)node;
        log_error("compile_pattern_to_regex: unresolved pattern reference '%.*s'",
            (int)ident->name->len, ident->name->chars);
        break;
    }

    default:
        log_error("compile_pattern_to_regex: unknown node type %d", node->node_type);
        break;
    }
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

    log_debug("Compiled pattern regex: %s", regex->str);

    // Compile RE2
    re2::RE2::Options options;
    options.set_log_errors(false);
    // UTF8 is the default encoding

    re2::RE2* re2 = new re2::RE2(regex->str, options);

    if (!re2->ok()) {
        if (error_msg) {
            // Note: error() returns std::string, we need to copy it
            static char error_buffer[256];
            snprintf(error_buffer, sizeof(error_buffer), "%s", re2->error().c_str());
            *error_msg = error_buffer;
        }
        delete re2;
        strbuf_free(regex);
        return nullptr;
    }

    // Allocate TypePattern
    TypePattern* pattern = (TypePattern*)pool_calloc(pool, sizeof(TypePattern));
    pattern->type_id = LMD_TYPE_PATTERN;
    pattern->is_symbol = is_symbol;
    pattern->re2 = re2;
    pattern->pattern_index = -1;  // Will be set when registered

    // Store source pattern for debugging
    pattern->source = (String*)pool_calloc(pool, sizeof(String) + regex->length + 1);
    pattern->source->len = regex->length;
    memcpy(pattern->source->chars, regex->str, regex->length + 1);

    strbuf_free(regex);
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
        delete pattern->re2;
        pattern->re2 = nullptr;
    }
}
