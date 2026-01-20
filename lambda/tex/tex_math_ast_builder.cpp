// tex_math_ast_builder.cpp - Parse LaTeX Math to MathAST
//
// Phase A of the two-phase math pipeline:
//   LaTeX Math String → Tree-sitter → Lambda Element → MathASTNode tree
//
// This module builds a semantic AST from parsed LaTeX math, deferring
// typesetting decisions to Phase B (tex_math_ast_typeset.cpp).
//
// Reference: vibe/Latex_Typeset_Math.md

#include "tex_math_ast.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include <tree_sitter/api.h>
#include <cstring>
#include <cstdlib>

// Use global scope strbuf functions (from lib/strbuf.h)
extern "C" {
#include "../../lib/strbuf.h"
}

// Tree-sitter latex_math language
extern "C" {
    const TSLanguage* tree_sitter_latex_math(void);
}

namespace tex {

// ============================================================================
// Type Name for Debugging
// ============================================================================

const char* math_node_type_name(MathNodeType type) {
    switch (type) {
        case MathNodeType::ORD:       return "ORD";
        case MathNodeType::OP:        return "OP";
        case MathNodeType::BIN:       return "BIN";
        case MathNodeType::REL:       return "REL";
        case MathNodeType::OPEN:      return "OPEN";
        case MathNodeType::CLOSE:     return "CLOSE";
        case MathNodeType::PUNCT:     return "PUNCT";
        case MathNodeType::INNER:     return "INNER";
        case MathNodeType::ROW:       return "ROW";
        case MathNodeType::FRAC:      return "FRAC";
        case MathNodeType::SQRT:      return "SQRT";
        case MathNodeType::SCRIPTS:   return "SCRIPTS";
        case MathNodeType::DELIMITED: return "DELIMITED";
        case MathNodeType::ACCENT:    return "ACCENT";
        case MathNodeType::OVERUNDER: return "OVERUNDER";
        case MathNodeType::TEXT:      return "TEXT";
        case MathNodeType::ARRAY:     return "ARRAY";
        case MathNodeType::ARRAY_ROW: return "ARRAY_ROW";
        case MathNodeType::ARRAY_CELL: return "ARRAY_CELL";
        case MathNodeType::SPACE:     return "SPACE";
        case MathNodeType::ERROR:     return "ERROR";
        default:                      return "UNKNOWN";
    }
}

// ============================================================================
// Node Allocation
// ============================================================================

MathASTNode* alloc_math_node(Arena* arena, MathNodeType type) {
    MathASTNode* node = (MathASTNode*)arena_alloc(arena, sizeof(MathASTNode));
    memset(node, 0, sizeof(MathASTNode));
    node->type = type;
    return node;
}

// ============================================================================
// Atom Node Constructors
// ============================================================================

MathASTNode* make_math_ord(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ORD);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Ord;
    return node;
}

MathASTNode* make_math_op(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::OP);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Op;
    return node;
}

MathASTNode* make_math_bin(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::BIN);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Bin;
    return node;
}

MathASTNode* make_math_rel(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::REL);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Rel;
    return node;
}

MathASTNode* make_math_open(Arena* arena, int32_t codepoint) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::OPEN);
    node->atom.codepoint = codepoint;
    node->atom.command = nullptr;
    node->atom.atom_class = (uint8_t)AtomType::Open;
    return node;
}

MathASTNode* make_math_close(Arena* arena, int32_t codepoint) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::CLOSE);
    node->atom.codepoint = codepoint;
    node->atom.command = nullptr;
    node->atom.atom_class = (uint8_t)AtomType::Close;
    return node;
}

MathASTNode* make_math_punct(Arena* arena, int32_t codepoint) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::PUNCT);
    node->atom.codepoint = codepoint;
    node->atom.command = nullptr;
    node->atom.atom_class = (uint8_t)AtomType::Punct;
    return node;
}

// ============================================================================
// Structural Node Constructors
// ============================================================================

MathASTNode* make_math_row(Arena* arena) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ROW);
    node->row.child_count = 0;
    return node;
}

MathASTNode* make_math_frac(Arena* arena, MathASTNode* numer, MathASTNode* denom, float rule_thickness) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::FRAC);
    node->frac.rule_thickness = rule_thickness;
    node->frac.left_delim = 0;
    node->frac.right_delim = 0;
    node->above = numer;    // numerator in above branch
    node->below = denom;    // denominator in below branch
    return node;
}

MathASTNode* make_math_sqrt(Arena* arena, MathASTNode* radicand, MathASTNode* index) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::SQRT);
    node->sqrt.has_index = (index != nullptr);
    node->body = radicand;   // radicand in body branch
    node->above = index;     // index (n-th root) in above branch
    return node;
}

MathASTNode* make_math_scripts(Arena* arena, MathASTNode* nucleus, MathASTNode* super, MathASTNode* sub) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::SCRIPTS);
    node->scripts.nucleus_type = (uint8_t)AtomType::Ord;  // will be updated based on nucleus
    node->body = nucleus;
    node->superscript = super;
    node->subscript = sub;
    
    // Determine nucleus type
    if (nucleus) {
        switch (nucleus->type) {
            case MathNodeType::ORD:
            case MathNodeType::OP:
            case MathNodeType::BIN:
            case MathNodeType::REL:
            case MathNodeType::OPEN:
            case MathNodeType::CLOSE:
            case MathNodeType::PUNCT:
                node->scripts.nucleus_type = nucleus->atom.atom_class;
                break;
            default:
                node->scripts.nucleus_type = (uint8_t)AtomType::Ord;
                break;
        }
    }
    return node;
}

MathASTNode* make_math_delimited(Arena* arena, int32_t left, MathASTNode* body, int32_t right) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::DELIMITED);
    node->delimited.left_delim = left;
    node->delimited.right_delim = right;
    node->body = body;
    return node;
}

MathASTNode* make_math_accent(Arena* arena, int32_t accent_char, const char* command, MathASTNode* base) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ACCENT);
    node->accent.accent_char = accent_char;
    node->accent.command = command;
    node->body = base;
    return node;
}

MathASTNode* make_math_overunder(Arena* arena, MathASTNode* nucleus, MathASTNode* over, MathASTNode* under, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::OVERUNDER);
    node->overunder.over_char = 0;
    node->overunder.under_char = 0;
    node->overunder.command = command;
    node->body = nucleus;
    node->above = over;
    node->below = under;
    return node;
}

// ============================================================================
// Text/Space Node Constructors
// ============================================================================

MathASTNode* make_math_text(Arena* arena, const char* text, size_t len, bool is_roman) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::TEXT);
    node->text.text = text;
    node->text.len = len;
    node->text.is_roman = is_roman;
    return node;
}

MathASTNode* make_math_space(Arena* arena, float width_mu) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::SPACE);
    node->space.width_mu = width_mu;
    return node;
}

MathASTNode* make_math_error(Arena* arena, const char* message) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ERROR);
    node->text.text = message;
    node->text.len = message ? strlen(message) : 0;
    return node;
}

// ============================================================================
// Tree Manipulation
// ============================================================================

void math_row_append(MathASTNode* row, MathASTNode* child) {
    if (!row || !child || row->type != MathNodeType::ROW) return;

    if (!row->body) {
        // First child
        row->body = child;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
    } else {
        // Find last child
        MathASTNode* last = row->body;
        while (last->next_sibling) {
            last = last->next_sibling;
        }
        last->next_sibling = child;
        child->prev_sibling = last;
        child->next_sibling = nullptr;
    }
    row->row.child_count++;
}

int math_row_count(MathASTNode* row) {
    if (!row || row->type != MathNodeType::ROW) return 0;
    return row->row.child_count;
}

// ============================================================================
// Greek Letter Lookup
// ============================================================================

struct GreekEntry {
    const char* name;
    int code;
    bool uppercase;
};

static const GreekEntry GREEK_TABLE[] = {
    // Uppercase
    {"Gamma", 0, true}, {"Delta", 1, true}, {"Theta", 2, true},
    {"Lambda", 3, true}, {"Xi", 4, true}, {"Pi", 5, true},
    {"Sigma", 6, true}, {"Upsilon", 7, true}, {"Phi", 8, true},
    {"Psi", 9, true}, {"Omega", 10, true},
    // Lowercase
    {"alpha", 11, false}, {"beta", 12, false}, {"gamma", 13, false},
    {"delta", 14, false}, {"epsilon", 15, false}, {"zeta", 16, false},
    {"eta", 17, false}, {"theta", 18, false}, {"iota", 19, false},
    {"kappa", 20, false}, {"lambda", 21, false}, {"mu", 22, false},
    {"nu", 23, false}, {"xi", 24, false}, {"pi", 25, false},
    {"rho", 26, false}, {"sigma", 27, false}, {"tau", 28, false},
    {"upsilon", 29, false}, {"phi", 30, false}, {"chi", 31, false},
    {"psi", 32, false}, {"omega", 33, false},
    // Variants
    {"varepsilon", 34, false}, {"vartheta", 35, false}, {"varpi", 36, false},
    {"varrho", 37, false}, {"varsigma", 38, false}, {"varphi", 39, false},
    {"varkappa", 123, false},
    {nullptr, 0, false}
};

static const GreekEntry* lookup_greek(const char* name, size_t len) {
    for (const GreekEntry* g = GREEK_TABLE; g->name; g++) {
        if (strlen(g->name) == len && strncmp(g->name, name, len) == 0) {
            return g;
        }
    }
    return nullptr;
}

// ============================================================================
// Symbol Lookup (for binary operators, relations)
// ============================================================================

struct SymbolEntry {
    const char* name;
    int code;
    AtomType atom;
};

static const SymbolEntry SYMBOL_TABLE[] = {
    // Relations
    {"leq", 20, AtomType::Rel}, {"le", 20, AtomType::Rel},
    {"geq", 21, AtomType::Rel}, {"ge", 21, AtomType::Rel},
    {"equiv", 17, AtomType::Rel}, {"sim", 24, AtomType::Rel},
    {"approx", 25, AtomType::Rel}, {"subset", 26, AtomType::Rel},
    {"supset", 27, AtomType::Rel}, {"subseteq", 18, AtomType::Rel},
    {"supseteq", 19, AtomType::Rel}, {"in", 50, AtomType::Rel},
    {"ni", 51, AtomType::Rel}, {"notin", 54, AtomType::Rel},
    {"neq", 54, AtomType::Rel}, {"ne", 54, AtomType::Rel},
    {"prec", 28, AtomType::Rel}, {"succ", 29, AtomType::Rel},
    {"ll", 30, AtomType::Rel}, {"gg", 31, AtomType::Rel},
    {"perp", 63, AtomType::Rel}, {"mid", 106, AtomType::Rel},
    {"parallel", 107, AtomType::Rel},
    // Binary operators
    {"pm", 6, AtomType::Bin}, {"mp", 7, AtomType::Bin},
    {"times", 2, AtomType::Bin}, {"div", 4, AtomType::Bin},
    {"cdot", 1, AtomType::Bin}, {"ast", 3, AtomType::Bin},
    {"star", 5, AtomType::Bin}, {"circ", 14, AtomType::Bin},
    {"bullet", 15, AtomType::Bin}, {"cap", 92, AtomType::Bin},
    {"cup", 91, AtomType::Bin}, {"vee", 95, AtomType::Bin},
    {"lor", 95, AtomType::Bin}, {"wedge", 94, AtomType::Bin},
    {"land", 94, AtomType::Bin}, {"setminus", 110, AtomType::Bin},
    {"oplus", 8, AtomType::Bin}, {"ominus", 9, AtomType::Bin},
    {"otimes", 10, AtomType::Bin}, {"oslash", 11, AtomType::Bin},
    {nullptr, 0, AtomType::Ord}
};

static const SymbolEntry* lookup_symbol(const char* name, size_t len) {
    for (const SymbolEntry* s = SYMBOL_TABLE; s->name; s++) {
        if (strlen(s->name) == len && strncmp(s->name, name, len) == 0) {
            return s;
        }
    }
    return nullptr;
}

// ============================================================================
// Big Operator Lookup
// ============================================================================

struct BigOpEntry {
    const char* name;
    int small_code;
    int large_code;
    bool uses_limits;
};

static const BigOpEntry BIG_OP_TABLE[] = {
    {"sum", 80, 88, true},
    {"prod", 81, 89, true},
    {"coprod", 96, 97, true},
    {"int", 82, 90, false},
    {"oint", 72, 73, false},
    {"iint", 82, 90, false},
    {"iiint", 82, 90, false},
    {"bigcap", 84, 92, true},
    {"bigcup", 83, 91, true},
    {"bigvee", 87, 95, true},
    {"bigwedge", 86, 94, true},
    {"bigoplus", 76, 77, true},
    {"bigotimes", 78, 79, true},
    {"bigodot", 74, 75, true},
    {"biguplus", 85, 93, true},
    {"bigsqcup", 70, 71, true},
    {"lim", 0, 0, true},
    {"liminf", 0, 0, true},
    {"limsup", 0, 0, true},
    {"max", 0, 0, true},
    {"min", 0, 0, true},
    {"sup", 0, 0, true},
    {"inf", 0, 0, true},
    {nullptr, 0, 0, false}
};

static const BigOpEntry* lookup_big_op(const char* name, size_t len) {
    for (const BigOpEntry* op = BIG_OP_TABLE; op->name; op++) {
        if (strlen(op->name) == len && strncmp(op->name, name, len) == 0) {
            return op;
        }
    }
    return nullptr;
}

// ============================================================================
// AST Builder Class
// ============================================================================

class MathASTBuilder {
public:
    MathASTBuilder(Arena* arena, const char* source, size_t len)
        : arena(arena), source(source), source_len(len) {}

    MathASTNode* build();

private:
    Arena* arena;
    const char* source;
    size_t source_len;

    // Tree-sitter node processing
    MathASTNode* build_ts_node(TSNode node);
    MathASTNode* build_math(TSNode node);
    MathASTNode* build_group(TSNode node);
    MathASTNode* build_symbol(TSNode node);
    MathASTNode* build_number(TSNode node);
    MathASTNode* build_operator(TSNode node);
    MathASTNode* build_relation(TSNode node);
    MathASTNode* build_punctuation(TSNode node);
    MathASTNode* build_command(TSNode node);
    MathASTNode* build_subsup(TSNode node);
    MathASTNode* build_fraction(TSNode node);
    MathASTNode* build_radical(TSNode node);
    MathASTNode* build_delimiter_group(TSNode node);
    MathASTNode* build_accent(TSNode node);
    MathASTNode* build_big_operator(TSNode node);
    MathASTNode* build_environment(TSNode node);
    MathASTNode* build_text_command(TSNode node);
    MathASTNode* build_space_command(TSNode node);

    // Helpers
    const char* node_text(TSNode node, int* out_len);
    const char* arena_copy_str(const char* str, size_t len);
};

const char* MathASTBuilder::node_text(TSNode node, int* out_len) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (out_len) *out_len = (int)(end - start);
    return source + start;
}

const char* MathASTBuilder::arena_copy_str(const char* str, size_t len) {
    char* copy = (char*)arena_alloc(arena, len + 1);
    memcpy(copy, str, len);
    copy[len] = '\0';
    return copy;
}

MathASTNode* MathASTBuilder::build() {
    if (!source || source_len == 0) {
        return make_math_row(arena);
    }

    // Create tree-sitter parser
    TSParser* parser = ts_parser_new();
    if (!ts_parser_set_language(parser, tree_sitter_latex_math())) {
        log_error("tex_math_ast: failed to set tree-sitter language");
        ts_parser_delete(parser);
        return make_math_error(arena, "parser init failed");
    }

    // Parse source
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source, (uint32_t)source_len);
    if (!tree) {
        log_error("tex_math_ast: failed to parse math");
        ts_parser_delete(parser);
        return make_math_error(arena, "parse failed");
    }

    TSNode root = ts_tree_root_node(tree);

    if (ts_node_has_error(root)) {
        log_debug("tex_math_ast: parse tree has errors, continuing anyway");
    }

    // Build AST
    MathASTNode* result = build_ts_node(root);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    if (!result) {
        result = make_math_row(arena);
    }

    return result;
}

MathASTNode* MathASTBuilder::build_ts_node(TSNode node) {
    if (ts_node_is_null(node)) return nullptr;

    const char* type = ts_node_type(node);

    if (strcmp(type, "math") == 0) return build_math(node);
    if (strcmp(type, "group") == 0) return build_group(node);
    if (strcmp(type, "symbol") == 0) return build_symbol(node);
    if (strcmp(type, "number") == 0) return build_number(node);
    if (strcmp(type, "operator") == 0) return build_operator(node);
    if (strcmp(type, "relation") == 0) return build_relation(node);
    if (strcmp(type, "punctuation") == 0) return build_punctuation(node);
    if (strcmp(type, "command") == 0) return build_command(node);
    if (strcmp(type, "subsup") == 0) return build_subsup(node);
    if (strcmp(type, "fraction") == 0) return build_fraction(node);
    if (strcmp(type, "radical") == 0) return build_radical(node);
    if (strcmp(type, "delimiter_group") == 0) return build_delimiter_group(node);
    if (strcmp(type, "accent") == 0) return build_accent(node);
    if (strcmp(type, "big_operator") == 0) return build_big_operator(node);
    if (strcmp(type, "environment") == 0) return build_environment(node);
    if (strcmp(type, "text_command") == 0) return build_text_command(node);
    if (strcmp(type, "space_command") == 0) return build_space_command(node);

    // Unknown - try children
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count == 1) {
        return build_ts_node(ts_node_named_child(node, 0));
    }
    if (child_count > 1) {
        return build_math(node);
    }

    log_debug("tex_math_ast: unknown node type '%s'", type);
    return nullptr;
}

MathASTNode* MathASTBuilder::build_math(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);

    if (child_count == 0) return nullptr;
    if (child_count == 1) return build_ts_node(ts_node_named_child(node, 0));

    // Build ROW node
    MathASTNode* row = make_math_row(arena);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        MathASTNode* child_node = build_ts_node(child);
        if (child_node) {
            math_row_append(row, child_node);
        }
    }

    return row;
}

MathASTNode* MathASTBuilder::build_group(TSNode node) {
    return build_math(node);
}

MathASTNode* MathASTBuilder::build_symbol(TSNode node) {
    int len;
    const char* text = node_text(node, &len);
    if (len != 1) return nullptr;

    return make_math_ord(arena, text[0], nullptr);
}

MathASTNode* MathASTBuilder::build_number(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    if (len == 1) {
        return make_math_ord(arena, text[0], nullptr);
    }

    // Multiple digits - create a ROW
    MathASTNode* row = make_math_row(arena);
    for (int i = 0; i < len; i++) {
        math_row_append(row, make_math_ord(arena, text[i], nullptr));
    }
    return row;
}

MathASTNode* MathASTBuilder::build_operator(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    // Check for command
    if (text[0] == '\\' && len > 1) {
        const char* cmd = text + 1;
        size_t cmd_len = len - 1;
        const char* cmd_copy = arena_copy_str(cmd, cmd_len);

        const SymbolEntry* sym = lookup_symbol(cmd, cmd_len);
        if (sym) {
            return make_math_bin(arena, sym->code, cmd_copy);
        }
    }

    // Single character operator
    return make_math_bin(arena, text[0], nullptr);
}

MathASTNode* MathASTBuilder::build_relation(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    if (text[0] == '\\' && len > 1) {
        const char* cmd = text + 1;
        size_t cmd_len = len - 1;
        const char* cmd_copy = arena_copy_str(cmd, cmd_len);

        const SymbolEntry* sym = lookup_symbol(cmd, cmd_len);
        if (sym) {
            return make_math_rel(arena, sym->code, cmd_copy);
        }
    }

    return make_math_rel(arena, text[0], nullptr);
}

MathASTNode* MathASTBuilder::build_punctuation(TSNode node) {
    int len;
    const char* text = node_text(node, &len);
    return make_math_punct(arena, text[0]);
}

MathASTNode* MathASTBuilder::build_command(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    // Strip backslash
    if (text[0] == '\\' && len > 1) {
        const char* cmd = text + 1;
        size_t cmd_len = len - 1;

        // Greek letters
        const GreekEntry* greek = lookup_greek(cmd, cmd_len);
        if (greek) {
            return make_math_ord(arena, greek->code, arena_copy_str(cmd, cmd_len));
        }

        // Symbols (binary/relation operators)
        const SymbolEntry* sym = lookup_symbol(cmd, cmd_len);
        if (sym) {
            if (sym->atom == AtomType::Bin) {
                return make_math_bin(arena, sym->code, arena_copy_str(cmd, cmd_len));
            } else if (sym->atom == AtomType::Rel) {
                return make_math_rel(arena, sym->code, arena_copy_str(cmd, cmd_len));
            }
        }

        // Big operators
        const BigOpEntry* bigop = lookup_big_op(cmd, cmd_len);
        if (bigop) {
            MathASTNode* op = make_math_op(arena, bigop->large_code, arena_copy_str(cmd, cmd_len));
            if (bigop->uses_limits) {
                op->flags |= MathASTNode::FLAG_LIMITS;
            }
            return op;
        }

        // Common commands with no arguments
        if (cmd_len == 5 && strncmp(cmd, "infty", 5) == 0) {
            return make_math_ord(arena, 49, arena_copy_str(cmd, cmd_len));  // cmsy10 infinity
        }
        if (cmd_len == 6 && strncmp(cmd, "partial", 7) == 0) {
            return make_math_ord(arena, 64, arena_copy_str(cmd, cmd_len));  // cmmi10 partial
        }
        if (cmd_len == 5 && strncmp(cmd, "nabla", 5) == 0) {
            return make_math_ord(arena, 114, arena_copy_str(cmd, cmd_len)); // cmsy10 nabla
        }

        // Unknown command - return as ordinary with command name
        return make_math_ord(arena, 0, arena_copy_str(cmd, cmd_len));
    }

    return nullptr;
}

MathASTNode* MathASTBuilder::build_subsup(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count < 2) return nullptr;

    // First child is the base
    MathASTNode* base = build_ts_node(ts_node_named_child(node, 0));

    MathASTNode* super = nullptr;
    MathASTNode* sub = nullptr;

    // Look for superscript and subscript children
    for (uint32_t i = 1; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);

        if (strcmp(type, "superscript") == 0) {
            // Superscript has its content as first named child
            uint32_t sup_children = ts_node_named_child_count(child);
            if (sup_children > 0) {
                super = build_ts_node(ts_node_named_child(child, 0));
            }
        } else if (strcmp(type, "subscript") == 0) {
            uint32_t sub_children = ts_node_named_child_count(child);
            if (sub_children > 0) {
                sub = build_ts_node(ts_node_named_child(child, 0));
            }
        }
    }

    if (!super && !sub) {
        return base;
    }

    return make_math_scripts(arena, base, super, sub);
}

MathASTNode* MathASTBuilder::build_fraction(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count < 2) return nullptr;

    // First two children are numerator and denominator
    MathASTNode* numer = build_ts_node(ts_node_named_child(node, 0));
    MathASTNode* denom = build_ts_node(ts_node_named_child(node, 1));

    return make_math_frac(arena, numer, denom);
}

MathASTNode* MathASTBuilder::build_radical(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count < 1) return nullptr;

    MathASTNode* radicand = nullptr;
    MathASTNode* index = nullptr;

    // Look for radicand and optional index
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);

        if (strcmp(type, "index") == 0) {
            uint32_t idx_children = ts_node_named_child_count(child);
            if (idx_children > 0) {
                index = build_ts_node(ts_node_named_child(child, 0));
            }
        } else if (!radicand) {
            radicand = build_ts_node(child);
        }
    }

    if (!radicand) {
        radicand = make_math_row(arena);
    }

    return make_math_sqrt(arena, radicand, index);
}

MathASTNode* MathASTBuilder::build_delimiter_group(TSNode node) {
    int32_t left_delim = '(';
    int32_t right_delim = ')';

    // Get delimiters from child nodes
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char* type = ts_node_type(child);

        if (strcmp(type, "left_delim") == 0) {
            int len;
            const char* text = node_text(child, &len);
            if (len > 0) left_delim = text[len - 1];  // Get last char (after \left)
        } else if (strcmp(type, "right_delim") == 0) {
            int len;
            const char* text = node_text(child, &len);
            if (len > 0) right_delim = text[len - 1];
        }
    }

    // Build content
    MathASTNode* content = nullptr;
    uint32_t named_count = ts_node_named_child_count(node);
    if (named_count > 0) {
        if (named_count == 1) {
            content = build_ts_node(ts_node_named_child(node, 0));
        } else {
            content = build_math(node);
        }
    }

    return make_math_delimited(arena, left_delim, content, right_delim);
}

MathASTNode* MathASTBuilder::build_accent(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    // Find accent command name
    const char* cmd = nullptr;
    size_t cmd_len = 0;
    if (text[0] == '\\') {
        cmd = text + 1;
        // Find end of command name
        const char* end = cmd;
        while (*end && *end != '{' && *end != ' ') end++;
        cmd_len = end - cmd;
    }

    // Build base content
    MathASTNode* base = nullptr;
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count > 0) {
        base = build_ts_node(ts_node_named_child(node, 0));
    }

    // Determine accent character
    int32_t accent_char = '^';  // default
    if (cmd_len == 3 && strncmp(cmd, "hat", 3) == 0) accent_char = '^';
    else if (cmd_len == 3 && strncmp(cmd, "bar", 3) == 0) accent_char = '-';
    else if (cmd_len == 5 && strncmp(cmd, "tilde", 5) == 0) accent_char = '~';
    else if (cmd_len == 3 && strncmp(cmd, "vec", 3) == 0) accent_char = 0x2192;  // rightarrow
    else if (cmd_len == 3 && strncmp(cmd, "dot", 3) == 0) accent_char = '.';

    return make_math_accent(arena, accent_char, 
                            cmd ? arena_copy_str(cmd, cmd_len) : nullptr, 
                            base);
}

MathASTNode* MathASTBuilder::build_big_operator(TSNode node) {
    // Similar to build_subsup but for operators with limits
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count < 1) return nullptr;

    MathASTNode* op = build_ts_node(ts_node_named_child(node, 0));

    MathASTNode* super = nullptr;
    MathASTNode* sub = nullptr;

    for (uint32_t i = 1; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);

        if (strcmp(type, "superscript") == 0) {
            uint32_t sup_children = ts_node_named_child_count(child);
            if (sup_children > 0) {
                super = build_ts_node(ts_node_named_child(child, 0));
            }
        } else if (strcmp(type, "subscript") == 0) {
            uint32_t sub_children = ts_node_named_child_count(child);
            if (sub_children > 0) {
                sub = build_ts_node(ts_node_named_child(child, 0));
            }
        }
    }

    if (!super && !sub) {
        return op;
    }

    // For big operators, use OVERUNDER with limits flag
    if (op && op->type == MathNodeType::OP && (op->flags & MathASTNode::FLAG_LIMITS)) {
        return make_math_overunder(arena, op, super, sub, op->atom.command);
    }

    return make_math_scripts(arena, op, super, sub);
}

MathASTNode* MathASTBuilder::build_environment(TSNode node) {
    // Handle matrix, pmatrix, bmatrix, etc.
    // TODO: implement array parsing
    return make_math_row(arena);
}

MathASTNode* MathASTBuilder::build_text_command(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count < 1) return nullptr;

    TSNode content = ts_node_named_child(node, 0);
    int len;
    const char* text = node_text(content, &len);

    return make_math_text(arena, arena_copy_str(text, len), len, true);
}

MathASTNode* MathASTBuilder::build_space_command(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    float width_mu = 3.0f;  // default thin space

    if (text[0] == '\\' && len >= 2) {
        char cmd = text[1];
        switch (cmd) {
            case ',': width_mu = 3.0f; break;   // thinmuskip
            case ':': width_mu = 4.0f; break;   // medmuskip
            case ';': width_mu = 5.0f; break;   // thickmuskip
            case '!': width_mu = -3.0f; break;  // negative thin space
            default:
                // Check for \quad, \qquad
                if (len >= 5 && strncmp(text + 1, "quad", 4) == 0) {
                    width_mu = 18.0f;  // 1em
                    if (len >= 6 && text[5] == 'q') {
                        width_mu = 36.0f;  // 2em
                    }
                }
        }
    }

    return make_math_space(arena, width_mu);
}

// ============================================================================
// Public Entry Points
// ============================================================================

MathASTNode* parse_math_string_to_ast(const char* latex_src, size_t len, Arena* arena) {
    MathASTBuilder builder(arena, latex_src, len);
    return builder.build();
}

MathASTNode* parse_math_to_ast(const ::ItemReader& math_elem, Arena* arena) {
    // Get the source string from the Lambda Element
    // Math elements have a "source" attribute containing the LaTeX source
    if (math_elem.isNull()) {
        log_debug("tex_math_ast: null math element");
        return make_math_row(arena);
    }
    
    // Create ElementReader from Item
    ::ElementReader elem(math_elem.item());
    
    // Try to get source from attribute
    const char* src = elem.get_attr_string("source");
    if (src) {
        return parse_math_string_to_ast(src, strlen(src), arena);
    }

    // Try text content
    const char* text = elem.get_attr_string("text");
    if (text) {
        return parse_math_string_to_ast(text, strlen(text), arena);
    }

    // No source found
    log_debug("tex_math_ast: no source found in math element");
    return make_math_row(arena);
}

// ============================================================================
// Debug Dump
// ============================================================================

void math_ast_dump(MathASTNode* node, ::StrBuf* out, int depth) {
    if (!node) {
        ::strbuf_append_str(out, "(null)\n");
        return;
    }

    // Indentation
    for (int i = 0; i < depth; i++) {
        ::strbuf_append_str(out, "  ");
    }

    // Node type
    ::strbuf_append_format(out, "%s", math_node_type_name(node->type));

    // Type-specific info
    switch (node->type) {
        case MathNodeType::ORD:
        case MathNodeType::OP:
        case MathNodeType::BIN:
        case MathNodeType::REL:
        case MathNodeType::OPEN:
        case MathNodeType::CLOSE:
        case MathNodeType::PUNCT:
            if (node->atom.command) {
                ::strbuf_append_format(out, " cmd='%s'", node->atom.command);
            } else if (node->atom.codepoint > 0 && node->atom.codepoint < 256) {
                ::strbuf_append_format(out, " cp='%c'", (char)node->atom.codepoint);
            } else {
                ::strbuf_append_format(out, " cp=%d", node->atom.codepoint);
            }
            break;

        case MathNodeType::ROW:
            ::strbuf_append_format(out, " count=%d", node->row.child_count);
            break;

        case MathNodeType::FRAC:
            ::strbuf_append_format(out, " thickness=%.1f", node->frac.rule_thickness);
            break;

        case MathNodeType::TEXT:
            ::strbuf_append_format(out, " text='%.*s'", (int)node->text.len, node->text.text);
            break;

        case MathNodeType::SPACE:
            ::strbuf_append_format(out, " width=%.1fmu", node->space.width_mu);
            break;

        default:
            break;
    }

    ::strbuf_append_str(out, "\n");

    // Dump branches
    if (node->body) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "body:\n");
        
        if (node->type == MathNodeType::ROW) {
            // For ROW, iterate through siblings
            for (MathASTNode* child = node->body; child; child = child->next_sibling) {
                math_ast_dump(child, out, depth + 2);
            }
        } else {
            math_ast_dump(node->body, out, depth + 2);
        }
    }

    if (node->above) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "above:\n");
        math_ast_dump(node->above, out, depth + 2);
    }

    if (node->below) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "below:\n");
        math_ast_dump(node->below, out, depth + 2);
    }

    if (node->superscript) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "superscript:\n");
        math_ast_dump(node->superscript, out, depth + 2);
    }

    if (node->subscript) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "subscript:\n");
        math_ast_dump(node->subscript, out, depth + 2);
    }
}

} // namespace tex
