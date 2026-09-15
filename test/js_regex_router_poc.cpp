// POC structural RegExp router. It intentionally owns one small AST parser,
// then derives routing features from that AST instead of lexical characters.
#include "js_regex_router_poc.h"

#include <string.h>

enum RegexAstKind {
    REGEX_AST_ATOM,
    REGEX_AST_SEQUENCE,
    REGEX_AST_ALTERNATION,
    REGEX_AST_GROUP,
    REGEX_AST_LOOKAROUND,
    REGEX_AST_BACKREFERENCE,
    REGEX_AST_ANCHOR,
};

struct RegexAstNode {
    RegexAstKind kind;
    int first_child;
    int next_sibling;
    int min_count;
    int max_count; // -1 denotes an unbounded maximum
};

struct RegexAstParser {
    const char* pattern;
    int pattern_len;
    int pos;
    bool failed;
    RegexAstNode nodes[512];
    int node_count;
};

struct RegexAstFacts {
    bool may_be_empty;
    bool has_bounded_zero_min;
    bool has_unbounded_quantifier;
    unsigned features;
};

static int regex_ast_new(RegexAstParser* parser, RegexAstKind kind) {
    if (parser->node_count >= (int)(sizeof(parser->nodes) / sizeof(parser->nodes[0]))) {
        parser->failed = true;
        return -1;
    }
    int index = parser->node_count++;
    RegexAstNode* node = &parser->nodes[index];
    node->kind = kind;
    node->first_child = -1;
    node->next_sibling = -1;
    node->min_count = 1;
    node->max_count = 1;
    return index;
}

static void regex_ast_append_child(RegexAstParser* parser, int parent, int child) {
    if (parent < 0 || child < 0) {
        parser->failed = true;
        return;
    }
    RegexAstNode* parent_node = &parser->nodes[parent];
    if (parent_node->first_child < 0) {
        parent_node->first_child = child;
        return;
    }
    int sibling = parent_node->first_child;
    while (parser->nodes[sibling].next_sibling >= 0) sibling = parser->nodes[sibling].next_sibling;
    parser->nodes[sibling].next_sibling = child;
}

static bool regex_ast_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool regex_ast_skip_hex(RegexAstParser* parser, int count) {
    if (parser->pos + count > parser->pattern_len) return false;
    parser->pos += count;
    return true;
}

static bool regex_ast_skip_braced(RegexAstParser* parser) {
    if (parser->pos >= parser->pattern_len || parser->pattern[parser->pos] != '{') return false;
    parser->pos++;
    int content_start = parser->pos;
    while (parser->pos < parser->pattern_len && parser->pattern[parser->pos] != '}') parser->pos++;
    if (parser->pos == content_start || parser->pos >= parser->pattern_len) return false;
    parser->pos++;
    return true;
}

static int regex_ast_parse_disjunction(RegexAstParser* parser);

static int regex_ast_parse_atom(RegexAstParser* parser) {
    if (parser->pos >= parser->pattern_len) {
        parser->failed = true;
        return -1;
    }
    char c = parser->pattern[parser->pos++];
    if (c == '(') {
        RegexAstKind kind = REGEX_AST_GROUP;
        if (parser->pos < parser->pattern_len && parser->pattern[parser->pos] == '?') {
            parser->pos++;
            if (parser->pos >= parser->pattern_len) {
                parser->failed = true;
                return -1;
            }
            char modifier = parser->pattern[parser->pos++];
            if (modifier == ':') {
                // non-capturing group
            } else if (modifier == '=' || modifier == '!') {
                kind = REGEX_AST_LOOKAROUND;
            } else if (modifier == '<') {
                if (parser->pos < parser->pattern_len &&
                    (parser->pattern[parser->pos] == '=' || parser->pattern[parser->pos] == '!')) {
                    parser->pos++;
                    kind = REGEX_AST_LOOKAROUND;
                } else {
                    int name_start = parser->pos;
                    while (parser->pos < parser->pattern_len && parser->pattern[parser->pos] != '>') parser->pos++;
                    if (parser->pos == name_start || parser->pos >= parser->pattern_len) {
                        parser->failed = true;
                        return -1;
                    }
                    parser->pos++;
                }
            } else {
                parser->failed = true;
                return -1;
            }
        }
        int group = regex_ast_new(parser, kind);
        int body = regex_ast_parse_disjunction(parser);
        if (parser->pos >= parser->pattern_len || parser->pattern[parser->pos] != ')') {
            parser->failed = true;
            return -1;
        }
        parser->pos++;
        regex_ast_append_child(parser, group, body);
        return group;
    }
    if (c == '[') {
        bool closed = false;
        while (parser->pos < parser->pattern_len) {
            char class_char = parser->pattern[parser->pos++];
            if (class_char == '\\' && parser->pos < parser->pattern_len) {
                parser->pos++;
                continue;
            }
            if (class_char == ']') {
                closed = true;
                break;
            }
        }
        if (!closed) {
            parser->failed = true;
            return -1;
        }
        return regex_ast_new(parser, REGEX_AST_ATOM);
    }
    if (c == '\\') {
        if (parser->pos >= parser->pattern_len) {
            parser->failed = true;
            return -1;
        }
        char escape = parser->pattern[parser->pos++];
        if (escape >= '1' && escape <= '9') return regex_ast_new(parser, REGEX_AST_BACKREFERENCE);
        if (escape == 'k' && parser->pos < parser->pattern_len && parser->pattern[parser->pos] == '<') {
            parser->pos++;
            int name_start = parser->pos;
            while (parser->pos < parser->pattern_len && parser->pattern[parser->pos] != '>') parser->pos++;
            if (parser->pos == name_start || parser->pos >= parser->pattern_len) {
                parser->failed = true;
                return -1;
            }
            parser->pos++;
            return regex_ast_new(parser, REGEX_AST_BACKREFERENCE);
        }
        if (escape == 'p' || escape == 'P') {
            if (!regex_ast_skip_braced(parser)) {
                parser->failed = true;
                return -1;
            }
        } else if (escape == 'u') {
            if (parser->pos < parser->pattern_len && parser->pattern[parser->pos] == '{') {
                if (!regex_ast_skip_braced(parser)) {
                    parser->failed = true;
                    return -1;
                }
            } else if (!regex_ast_skip_hex(parser, 4)) {
                parser->failed = true;
                return -1;
            }
        } else if (escape == 'x' && !regex_ast_skip_hex(parser, 2)) {
            parser->failed = true;
            return -1;
        } else if (escape == 'c' && !regex_ast_skip_hex(parser, 1)) {
            parser->failed = true;
            return -1;
        }
        return regex_ast_new(parser, REGEX_AST_ATOM);
    }
    if (c == '^' || c == '$') return regex_ast_new(parser, REGEX_AST_ANCHOR);
    if (c == ')' || c == '|' || c == '?' || c == '*' || c == '+') {
        parser->failed = true;
        return -1;
    }
    return regex_ast_new(parser, REGEX_AST_ATOM);
}

// Returns 0 for no quantifier, 1 for a parsed quantifier, and -1 for invalid syntax.
static int regex_ast_parse_quantifier(RegexAstParser* parser, RegexAstNode* node) {
    if (parser->pos >= parser->pattern_len) return 0;
    char c = parser->pattern[parser->pos];
    if (c == '?') {
        node->min_count = 0;
        node->max_count = 1;
        parser->pos++;
    } else if (c == '*') {
        node->min_count = 0;
        node->max_count = -1;
        parser->pos++;
    } else if (c == '+') {
        node->min_count = 1;
        node->max_count = -1;
        parser->pos++;
    } else if (c == '{' && parser->pos + 1 < parser->pattern_len &&
               regex_ast_is_digit(parser->pattern[parser->pos + 1])) {
        parser->pos++;
        int min_count = 0;
        while (parser->pos < parser->pattern_len && regex_ast_is_digit(parser->pattern[parser->pos])) {
            min_count = min_count * 10 + (parser->pattern[parser->pos++] - '0');
        }
        int max_count = min_count;
        if (parser->pos < parser->pattern_len && parser->pattern[parser->pos] == ',') {
            parser->pos++;
            if (parser->pos < parser->pattern_len && regex_ast_is_digit(parser->pattern[parser->pos])) {
                max_count = 0;
                while (parser->pos < parser->pattern_len && regex_ast_is_digit(parser->pattern[parser->pos])) {
                    max_count = max_count * 10 + (parser->pattern[parser->pos++] - '0');
                }
            } else {
                max_count = -1;
            }
        }
        if (parser->pos >= parser->pattern_len || parser->pattern[parser->pos] != '}' ||
            (max_count >= 0 && max_count < min_count)) return -1;
        parser->pos++;
        node->min_count = min_count;
        node->max_count = max_count;
    } else {
        return 0;
    }
    // A trailing '?' marks a lazy quantifier; it is not a second optionality.
    if (parser->pos < parser->pattern_len && parser->pattern[parser->pos] == '?') parser->pos++;
    return 1;
}

static int regex_ast_parse_sequence(RegexAstParser* parser) {
    int sequence = regex_ast_new(parser, REGEX_AST_SEQUENCE);
    while (!parser->failed && parser->pos < parser->pattern_len) {
        char c = parser->pattern[parser->pos];
        if (c == ')' || c == '|') break;
        int atom = regex_ast_parse_atom(parser);
        if (atom < 0) return -1;
        int quantifier = regex_ast_parse_quantifier(parser, &parser->nodes[atom]);
        if (quantifier < 0) {
            parser->failed = true;
            return -1;
        }
        regex_ast_append_child(parser, sequence, atom);
    }
    return sequence;
}

static int regex_ast_parse_disjunction(RegexAstParser* parser) {
    int first = regex_ast_parse_sequence(parser);
    if (parser->failed || parser->pos >= parser->pattern_len || parser->pattern[parser->pos] != '|') return first;
    int alternation = regex_ast_new(parser, REGEX_AST_ALTERNATION);
    regex_ast_append_child(parser, alternation, first);
    while (!parser->failed && parser->pos < parser->pattern_len && parser->pattern[parser->pos] == '|') {
        parser->pos++;
        regex_ast_append_child(parser, alternation, regex_ast_parse_sequence(parser));
    }
    return alternation;
}

static RegexAstFacts regex_ast_analyze(RegexAstParser* parser, int node_index, bool multiline) {
    RegexAstFacts facts = {};
    facts.may_be_empty = true;
    if (node_index < 0) return facts;
    RegexAstNode* node = &parser->nodes[node_index];

    if (node->kind == REGEX_AST_SEQUENCE) {
        for (int child = node->first_child; child >= 0; child = parser->nodes[child].next_sibling) {
            RegexAstFacts child_facts = regex_ast_analyze(parser, child, multiline);
            facts.may_be_empty = facts.may_be_empty && child_facts.may_be_empty;
            facts.has_bounded_zero_min = facts.has_bounded_zero_min || child_facts.has_bounded_zero_min;
            facts.has_unbounded_quantifier = facts.has_unbounded_quantifier || child_facts.has_unbounded_quantifier;
            facts.features |= child_facts.features;
        }
    } else if (node->kind == REGEX_AST_ALTERNATION) {
        facts.may_be_empty = false;
        for (int child = node->first_child; child >= 0; child = parser->nodes[child].next_sibling) {
            RegexAstFacts child_facts = regex_ast_analyze(parser, child, multiline);
            facts.may_be_empty = facts.may_be_empty || child_facts.may_be_empty;
            facts.has_bounded_zero_min = facts.has_bounded_zero_min || child_facts.has_bounded_zero_min;
            facts.has_unbounded_quantifier = facts.has_unbounded_quantifier || child_facts.has_unbounded_quantifier;
            facts.features |= child_facts.features;
        }
    } else if (node->kind == REGEX_AST_GROUP || node->kind == REGEX_AST_LOOKAROUND) {
        RegexAstFacts body = regex_ast_analyze(parser, node->first_child, multiline);
        facts = body;
        if (node->kind == REGEX_AST_LOOKAROUND) {
            facts.may_be_empty = true;
            facts.features |= JS_REGEX_POC_FEATURE_LOOKAROUND;
        }
        bool repeats = node->max_count < 0 || node->max_count > 1;
        if (repeats && body.has_bounded_zero_min && !body.has_unbounded_quantifier) {
            facts.features |= JS_REGEX_POC_FEATURE_NULLABLE_DISCARD;
        }
    } else {
        facts.may_be_empty = node->kind == REGEX_AST_ANCHOR || node->kind == REGEX_AST_LOOKAROUND;
        if (node->kind == REGEX_AST_BACKREFERENCE) facts.features |= JS_REGEX_POC_FEATURE_BACKREFERENCE;
        if (node->kind == REGEX_AST_ANCHOR && multiline) {
            facts.features |= JS_REGEX_POC_FEATURE_MULTILINE_ANCHOR;
        }
    }

    if (node->min_count == 0 && node->max_count >= 0) facts.has_bounded_zero_min = true;
    if (node->max_count < 0) facts.has_unbounded_quantifier = true;
    if (node->min_count == 0) facts.may_be_empty = true;
    return facts;
}

JsRegexPocResult js_regex_poc_route(const char* pattern, int pattern_len, bool multiline) {
    JsRegexPocResult result = {};
    result.route = JS_REGEX_POC_SYNTAX_ERROR;
    if (!pattern || pattern_len < 0) return result;

    RegexAstParser parser = {};
    parser.pattern = pattern;
    parser.pattern_len = pattern_len;
    int root = regex_ast_parse_disjunction(&parser);
    if (parser.failed || parser.pos != parser.pattern_len || root < 0) return result;

    RegexAstFacts facts = regex_ast_analyze(&parser, root, multiline);
    result.node_count = parser.node_count;
    result.features = facts.features;
    result.route = facts.features == 0 ? JS_REGEX_POC_RE2_ELIGIBLE : JS_REGEX_POC_BACKTRACK_REQUIRED;
    return result;
}
