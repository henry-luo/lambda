// input-math2.cpp - LaTeX math parser using tree-sitter-latex-math
//
// This parser converts LaTeX math strings to a MathNode tree (Lambda elements)
// using the tree-sitter-latex-math grammar.
//
// Usage:
//   Item math_tree = parse_math("x^2 + \\frac{1}{2}", input);

#include "input.hpp"
#include "../lambda-data.hpp"
#include "../math_node.hpp"
#include "../math_symbols.hpp"
#include "../mark_builder.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <tree_sitter/api.h>
#include <string.h>
#include <stdlib.h>

// Tree-sitter latex_math language
extern "C" {
    const TSLanguage* tree_sitter_latex_math(void);
}

namespace lambda {

// ============================================================================
// Parser context
// ============================================================================

struct MathParseContext {
    Input* input;
    MathNodeBuilder* builder;
    const char* source;
    size_t source_len;
    TSTree* tree;
    
    MathParseContext(Input* input, const char* src)
        : input(input), source(src), source_len(strlen(src)), tree(nullptr) {
        builder = new MathNodeBuilder(input);
    }
    
    ~MathParseContext() {
        delete builder;
        if (tree) ts_tree_delete(tree);
    }
    
    // Get node text
    const char* node_text(TSNode node, int* out_len) {
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        if (out_len) *out_len = end - start;
        return source + start;
    }
    
    // Get node text as null-terminated string (caller must free)
    char* node_text_dup(TSNode node) {
        int len;
        const char* text = node_text(node, &len);
        char* result = (char*)malloc(len + 1);
        memcpy(result, text, len);
        result[len] = '\0';
        return result;
    }
};

// ============================================================================
// Forward declarations
// ============================================================================

static Item build_node(MathParseContext& ctx, TSNode node);
static Item build_math(MathParseContext& ctx, TSNode node);
static Item build_expression(MathParseContext& ctx, TSNode node);
static Item build_atom(MathParseContext& ctx, TSNode node);

// ============================================================================
// Node type dispatch
// ============================================================================

static Item build_node(MathParseContext& ctx, TSNode node) {
    if (ts_node_is_null(node)) return ItemNull;
    
    const char* type = ts_node_type(node);
    
    // dispatch based on node type
    if (strcmp(type, "math") == 0) {
        return build_math(ctx, node);
    }
    if (strcmp(type, "group") == 0) {
        return build_expression(ctx, node);  // unwrap group for content
    }
    if (strcmp(type, "subsup") == 0) {
        return build_atom(ctx, node);
    }
    if (strcmp(type, "symbol") == 0 ||
        strcmp(type, "number") == 0 ||
        strcmp(type, "operator") == 0 ||
        strcmp(type, "relation") == 0 ||
        strcmp(type, "punctuation") == 0 ||
        strcmp(type, "fraction") == 0 ||
        strcmp(type, "binomial") == 0 ||
        strcmp(type, "radical") == 0 ||
        strcmp(type, "delimiter_group") == 0 ||
        strcmp(type, "accent") == 0 ||
        strcmp(type, "big_operator") == 0 ||
        strcmp(type, "text_command") == 0 ||
        strcmp(type, "style_command") == 0 ||
        strcmp(type, "space_command") == 0 ||
        strcmp(type, "command") == 0) {
        return build_atom(ctx, node);
    }
    
    // unknown node type - try to recurse into children
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count == 1) {
        return build_node(ctx, ts_node_named_child(node, 0));
    }
    if (child_count > 1) {
        return build_math(ctx, node);  // treat as sequence
    }
    
    log_debug("math parser: unknown node type '%s'", type);
    return ItemNull;
}

// ============================================================================
// Build math (sequence of expressions)
// ============================================================================

static Item build_math(MathParseContext& ctx, TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);
    
    if (child_count == 0) {
        return ItemNull;
    }
    
    if (child_count == 1) {
        return build_node(ctx, ts_node_named_child(node, 0));
    }
    
    // multiple children - build a row
    Item* items = (Item*)malloc(child_count * sizeof(Item));
    int count = 0;
    
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        Item item = build_node(ctx, child);
        if (item.item != ItemNull.item) {
            items[count++] = item;
        }
    }
    
    Item result;
    if (count == 0) {
        result = ItemNull;
    } else if (count == 1) {
        result = items[0];
    } else {
        result = ctx.builder->row(items, count);
    }
    
    free(items);
    return result;
}

// ============================================================================
// Build expression (group content)
// ============================================================================

static Item build_expression(MathParseContext& ctx, TSNode node) {
    const char* type = ts_node_type(node);
    
    if (strcmp(type, "group") == 0) {
        // group: { ... }
        // just build the contents as a row/single item
        return build_math(ctx, node);
    }
    
    if (strcmp(type, "brack_group") == 0) {
        // bracket group: [ ... ]
        return build_math(ctx, node);
    }
    
    return build_node(ctx, node);
}

// ============================================================================
// Build atoms
// ============================================================================

static Item build_symbol(MathParseContext& ctx, TSNode node) {
    char* text = ctx.node_text_dup(node);
    Item result = ctx.builder->symbol(text);
    free(text);
    return result;
}

static Item build_number(MathParseContext& ctx, TSNode node) {
    char* text = ctx.node_text_dup(node);
    Item result = ctx.builder->number(text);
    free(text);
    return result;
}

static Item build_operator(MathParseContext& ctx, TSNode node) {
    char* text = ctx.node_text_dup(node);
    
    // check if it's a command
    if (text[0] == '\\') {
        int codepoint;
        MathAtomType atom_type;
        if (lookup_math_symbol(text, &codepoint, &atom_type)) {
            Item result = ctx.builder->command(text, codepoint, atom_type);
            free(text);
            return result;
        }
    }
    
    // single character operator
    MathAtomType atom_type = MathAtomType::Bin;
    if (text[0] && text[1] == '\0') {
        atom_type = get_single_char_atom_type(text[0]);
    }
    Item result = ctx.builder->op(text, atom_type);
    free(text);
    return result;
}

static Item build_relation(MathParseContext& ctx, TSNode node) {
    char* text = ctx.node_text_dup(node);
    
    // check if it's a command
    if (text[0] == '\\') {
        int codepoint;
        MathAtomType atom_type;
        if (lookup_math_symbol(text, &codepoint, &atom_type)) {
            Item result = ctx.builder->command(text, codepoint, atom_type);
            free(text);
            return result;
        }
    }
    
    Item result = ctx.builder->rel(text);
    free(text);
    return result;
}

static Item build_punctuation(MathParseContext& ctx, TSNode node) {
    char* text = ctx.node_text_dup(node);
    Item result = ctx.builder->punct(text);
    free(text);
    return result;
}

static Item build_fraction(MathParseContext& ctx, TSNode node) {
    // get command
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    char* cmd = ts_node_is_null(cmd_node) ? strdup("\\frac") : ctx.node_text_dup(cmd_node);
    
    // get numerator and denominator
    TSNode numer_node = ts_node_child_by_field_name(node, "numer", 5);
    TSNode denom_node = ts_node_child_by_field_name(node, "denom", 5);
    
    Item numer = build_expression(ctx, numer_node);
    Item denom = build_expression(ctx, denom_node);
    
    Item result = ctx.builder->fraction(numer, denom, cmd);
    free(cmd);
    return result;
}

static Item build_binomial(MathParseContext& ctx, TSNode node) {
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    char* cmd = ts_node_is_null(cmd_node) ? strdup("\\binom") : ctx.node_text_dup(cmd_node);
    
    TSNode top_node = ts_node_child_by_field_name(node, "top", 3);
    TSNode bottom_node = ts_node_child_by_field_name(node, "bottom", 6);
    
    Item top = build_expression(ctx, top_node);
    Item bottom = build_expression(ctx, bottom_node);
    
    Item result = ctx.builder->binomial(top, bottom, cmd);
    free(cmd);
    return result;
}

static Item build_radical(MathParseContext& ctx, TSNode node) {
    TSNode index_node = ts_node_child_by_field_name(node, "index", 5);
    TSNode radicand_node = ts_node_child_by_field_name(node, "radicand", 8);
    
    Item index = ts_node_is_null(index_node) ? ItemNull : build_expression(ctx, index_node);
    Item radicand = build_expression(ctx, radicand_node);
    
    return ctx.builder->radical(radicand, index);
}

static Item build_delimiter_group(MathParseContext& ctx, TSNode node) {
    TSNode left_node = ts_node_child_by_field_name(node, "left_delim", 10);
    TSNode right_node = ts_node_child_by_field_name(node, "right_delim", 11);
    
    char* left = ts_node_is_null(left_node) ? strdup("(") : ctx.node_text_dup(left_node);
    char* right = ts_node_is_null(right_node) ? strdup(")") : ctx.node_text_dup(right_node);
    
    // content is everything between delimiters
    Item content = build_math(ctx, node);
    
    Item result = ctx.builder->delimiter(left, right, content);
    free(left);
    free(right);
    return result;
}

static Item build_accent(MathParseContext& ctx, TSNode node) {
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    TSNode base_node = ts_node_child_by_field_name(node, "base", 4);
    
    char* cmd = ts_node_is_null(cmd_node) ? strdup("\\hat") : ctx.node_text_dup(cmd_node);
    Item base = build_expression(ctx, base_node);
    
    Item result = ctx.builder->accent(cmd, base);
    free(cmd);
    return result;
}

static Item build_big_operator(MathParseContext& ctx, TSNode node) {
    TSNode op_node = ts_node_child_by_field_name(node, "op", 2);
    TSNode lower_node = ts_node_child_by_field_name(node, "lower", 5);
    TSNode upper_node = ts_node_child_by_field_name(node, "upper", 5);
    
    char* op = ts_node_is_null(op_node) ? strdup("\\sum") : ctx.node_text_dup(op_node);
    Item lower = ts_node_is_null(lower_node) ? ItemNull : build_expression(ctx, lower_node);
    Item upper = ts_node_is_null(upper_node) ? ItemNull : build_expression(ctx, upper_node);
    
    Item result = ctx.builder->bigOperator(op, lower, upper);
    free(op);
    return result;
}

static Item build_text_command(MathParseContext& ctx, TSNode node) {
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    TSNode content_node = ts_node_child_by_field_name(node, "content", 7);
    
    char* cmd = ts_node_is_null(cmd_node) ? strdup("\\text") : ctx.node_text_dup(cmd_node);
    
    // extract text content (inside braces)
    const char* content = "";
    if (!ts_node_is_null(content_node)) {
        TSNode text_node = ts_node_named_child(content_node, 0);  // text_content
        if (!ts_node_is_null(text_node)) {
            char* text = ctx.node_text_dup(text_node);
            Item result = ctx.builder->text(text, cmd);
            free(text);
            free(cmd);
            return result;
        }
    }
    
    Item result = ctx.builder->text(content, cmd);
    free(cmd);
    return result;
}

static Item build_style_command(MathParseContext& ctx, TSNode node) {
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    TSNode arg_node = ts_node_child_by_field_name(node, "arg", 3);
    
    char* cmd = ctx.node_text_dup(cmd_node);
    Item content = ts_node_is_null(arg_node) ? ItemNull : build_expression(ctx, arg_node);
    
    Item result = ctx.builder->style(cmd, content);
    free(cmd);
    return result;
}

static Item build_space_command(MathParseContext& ctx, TSNode node) {
    char* text = ctx.node_text_dup(node);
    Item result = ctx.builder->space(text);
    free(text);
    return result;
}

static Item build_command(MathParseContext& ctx, TSNode node) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) {
        return ctx.builder->error("missing command name");
    }
    
    char* cmd = ctx.node_text_dup(name_node);
    
    // look up in symbol tables
    int codepoint;
    MathAtomType atom_type;
    if (lookup_math_symbol(cmd, &codepoint, &atom_type)) {
        Item result = ctx.builder->command(cmd, codepoint, atom_type);
        free(cmd);
        return result;
    }
    
    // unknown command - create as generic command node
    Item result = ctx.builder->command(cmd, 0, MathAtomType::Ord);
    free(cmd);
    return result;
}

static Item build_subsup(MathParseContext& ctx, TSNode node) {
    TSNode base_node = ts_node_child_by_field_name(node, "base", 4);
    TSNode sub_node = ts_node_child_by_field_name(node, "sub", 3);
    TSNode sup_node = ts_node_child_by_field_name(node, "sup", 3);
    
    Item base = build_node(ctx, base_node);
    Item sub = ts_node_is_null(sub_node) ? ItemNull : build_expression(ctx, sub_node);
    Item sup = ts_node_is_null(sup_node) ? ItemNull : build_expression(ctx, sup_node);
    
    return ctx.builder->subsup(base, sub, sup);
}

static Item build_atom(MathParseContext& ctx, TSNode node) {
    const char* type = ts_node_type(node);
    
    if (strcmp(type, "symbol") == 0) return build_symbol(ctx, node);
    if (strcmp(type, "number") == 0) return build_number(ctx, node);
    if (strcmp(type, "operator") == 0) return build_operator(ctx, node);
    if (strcmp(type, "relation") == 0) return build_relation(ctx, node);
    if (strcmp(type, "punctuation") == 0) return build_punctuation(ctx, node);
    if (strcmp(type, "fraction") == 0) return build_fraction(ctx, node);
    if (strcmp(type, "binomial") == 0) return build_binomial(ctx, node);
    if (strcmp(type, "radical") == 0) return build_radical(ctx, node);
    if (strcmp(type, "delimiter_group") == 0) return build_delimiter_group(ctx, node);
    if (strcmp(type, "accent") == 0) return build_accent(ctx, node);
    if (strcmp(type, "big_operator") == 0) return build_big_operator(ctx, node);
    if (strcmp(type, "text_command") == 0) return build_text_command(ctx, node);
    if (strcmp(type, "style_command") == 0) return build_style_command(ctx, node);
    if (strcmp(type, "space_command") == 0) return build_space_command(ctx, node);
    if (strcmp(type, "command") == 0) return build_command(ctx, node);
    if (strcmp(type, "subsup") == 0) return build_subsup(ctx, node);
    if (strcmp(type, "group") == 0) return build_expression(ctx, node);
    
    log_debug("math parser: unhandled atom type '%s'", type);
    return ItemNull;
}

// ============================================================================
// Public API
// ============================================================================

Item parse_math(const char* source, Input* input) {
    if (!source || !*source) return ItemNull;
    
    log_debug("math parser: parsing '%s'", source);
    
    // create parser
    TSParser* parser = ts_parser_new();
    if (!ts_parser_set_language(parser, tree_sitter_latex_math())) {
        log_error("math parser: failed to set language");
        ts_parser_delete(parser);
        return ItemNull;
    }
    
    // parse source
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source, strlen(source));
    if (!tree) {
        log_error("math parser: failed to parse");
        ts_parser_delete(parser);
        return ItemNull;
    }
    
    TSNode root = ts_tree_root_node(tree);
    
    // check for errors
    if (ts_node_has_error(root)) {
        log_debug("math parser: parse tree has errors");
    }
    
    // build math node tree
    MathParseContext ctx(input, source);
    ctx.tree = tree;
    
    Item result = build_node(ctx, root);
    
    ts_parser_delete(parser);
    // tree is deleted by ctx destructor
    ctx.tree = nullptr;  // prevent double-delete
    ts_tree_delete(tree);
    
    return result;
}

// Debug: print the parse tree
void debug_print_math_tree(const char* source) {
    TSParser* parser = ts_parser_new();
    if (!ts_parser_set_language(parser, tree_sitter_latex_math())) {
        log_error("math parser: failed to set language");
        ts_parser_delete(parser);
        return;
    }
    
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source, strlen(source));
    if (!tree) {
        log_error("math parser: failed to parse");
        ts_parser_delete(parser);
        return;
    }
    
    TSNode root = ts_tree_root_node(tree);
    char* sexp = ts_node_string(root);
    log_debug("math parse tree: %s", sexp);
    free(sexp);
    
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

// ============================================================================
// Utility function implementations (declared in math_node.hpp)
// ============================================================================

MathNodeType get_math_node_type(Item node) {
    if (node.item == ItemNull.item) return MathNodeType::Error;
    TypeId type = get_type_id(node);
    if (type != LMD_TYPE_MAP) return MathNodeType::Error;
    
    Map* map = node.map;
    ConstItem node_type = map->get("node");
    if (node_type.item == ItemNull.item) return MathNodeType::Error;
    
    const char* name = nullptr;
    TypeId nt_type = node_type.type_id();
    Item node_type_item = *(Item*)&node_type;  // cast to Item to use get_string
    if (nt_type == LMD_TYPE_SYMBOL) {
        String* str = node_type_item.get_symbol();
        if (str) name = str->chars;
    } else if (nt_type == LMD_TYPE_STRING) {
        String* str = node_type_item.get_string();
        if (str) name = str->chars;
    }
    if (!name) return MathNodeType::Error;
    
    if (strcmp(name, "symbol") == 0) return MathNodeType::Symbol;
    if (strcmp(name, "number") == 0) return MathNodeType::Number;
    if (strcmp(name, "command") == 0) return MathNodeType::Command;
    if (strcmp(name, "group") == 0) return MathNodeType::Group;
    if (strcmp(name, "row") == 0) return MathNodeType::Row;
    if (strcmp(name, "subsup") == 0) return MathNodeType::Subsup;
    if (strcmp(name, "frac") == 0) return MathNodeType::Fraction;
    if (strcmp(name, "binom") == 0) return MathNodeType::Binomial;
    if (strcmp(name, "radical") == 0) return MathNodeType::Radical;
    if (strcmp(name, "delimiter") == 0) return MathNodeType::Delimiter;
    if (strcmp(name, "accent") == 0) return MathNodeType::Accent;
    if (strcmp(name, "bigop") == 0) return MathNodeType::BigOperator;
    if (strcmp(name, "array") == 0) return MathNodeType::Array;
    if (strcmp(name, "text") == 0) return MathNodeType::Text;
    if (strcmp(name, "style") == 0) return MathNodeType::Style;
    if (strcmp(name, "space") == 0) return MathNodeType::Space;
    if (strcmp(name, "error") == 0) return MathNodeType::Error;
    
    return MathNodeType::Error;
}

MathAtomType get_math_atom_type(Item node) {
    if (node.item == ItemNull.item) return MathAtomType::Ord;
    TypeId type = get_type_id(node);
    if (type != LMD_TYPE_MAP) return MathAtomType::Ord;
    
    Map* map = node.map;
    ConstItem atom_type = map->get("atom");
    if (atom_type.item == ItemNull.item) return MathAtomType::Ord;
    
    const char* name = nullptr;
    TypeId at_type = atom_type.type_id();
    Item atom_type_item = *(Item*)&atom_type;  // cast to Item to use get_string
    if (at_type == LMD_TYPE_SYMBOL) {
        String* str = atom_type_item.get_symbol();
        if (str) name = str->chars;
    } else if (at_type == LMD_TYPE_STRING) {
        String* str = atom_type_item.get_string();
        if (str) name = str->chars;
    }
    if (!name) return MathAtomType::Ord;
    
    if (strcmp(name, "ord") == 0) return MathAtomType::Ord;
    if (strcmp(name, "op") == 0) return MathAtomType::Op;
    if (strcmp(name, "bin") == 0) return MathAtomType::Bin;
    if (strcmp(name, "rel") == 0) return MathAtomType::Rel;
    if (strcmp(name, "open") == 0) return MathAtomType::Open;
    if (strcmp(name, "close") == 0) return MathAtomType::Close;
    if (strcmp(name, "punct") == 0) return MathAtomType::Punct;
    if (strcmp(name, "inner") == 0) return MathAtomType::Inner;
    
    return MathAtomType::Ord;
}

} // namespace lambda