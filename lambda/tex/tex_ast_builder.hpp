// tex_ast_builder.hpp - Build TeX AST from Tree-sitter Parse
//
// Converts Tree-sitter concrete syntax tree (CST) into semantic TeX AST
// with proper mode tracking and macro expansion preparation.

#ifndef TEX_AST_BUILDER_HPP
#define TEX_AST_BUILDER_HPP

#include "tex_ast.hpp"
#include "tex_box.hpp"
#include "lambda/tree-sitter/lib/include/tree_sitter/api.h"
#include "lib/arena.h"
#include "lib/hashmap.h"

namespace tex {

// ============================================================================
// AST Builder Configuration
// ============================================================================

struct ASTBuilderConfig {
    bool expand_macros;       // Whether to expand macros during build
    bool track_locations;     // Whether to populate SourceLoc
    bool allow_errors;        // Whether to continue on parse errors
    Mode initial_mode;        // Starting mode (Horizontal or Math)
};

inline ASTBuilderConfig default_config() {
    return ASTBuilderConfig{
        .expand_macros = true,
        .track_locations = true,
        .allow_errors = true,
        .initial_mode = Mode::Horizontal
    };
}

// ============================================================================
// Macro Definition
// ============================================================================

struct MacroParameter {
    bool delimited;           // True if parameter has delimiter
    const char* delimiter;    // Delimiter text (if delimited)
};

struct MacroDef {
    const char* name;         // Macro name without backslash
    int param_count;          // Number of parameters (0-9)
    MacroParameter params[9]; // Parameter specifications
    const char* replacement;  // Replacement text with #1, #2, etc.
    bool is_outer;            // \outer macro
    bool is_long;             // \long macro (allows \par in args)
};

// ============================================================================
// AST Builder State
// ============================================================================

struct ASTBuilder {
    Arena* arena;
    const char* source;       // Source text
    size_t source_len;
    TSTree* tree;             // Tree-sitter parse tree

    ASTBuilderConfig config;

    // Current state
    Mode mode_stack[32];      // Mode stack for nested environments
    int mode_depth;

    // Math state
    bool in_display_math;
    int script_level;         // 0=normal, 1=script, 2=scriptscript

    // Environment stack
    struct EnvEntry {
        const char* name;
        TexNode* node;
    };
    EnvEntry env_stack[32];
    int env_depth;

    // Macro table
    Hashmap* macros;          // name -> MacroDef*

    // Error collection
    struct ParseError {
        SourceLoc loc;
        const char* message;
    };
    ParseError errors[64];
    int error_count;

    // Methods
    Mode current_mode() const {
        return mode_depth > 0 ? mode_stack[mode_depth - 1] : Mode::Horizontal;
    }

    void push_mode(Mode m) {
        if (mode_depth < 32) {
            mode_stack[mode_depth++] = m;
        }
    }

    void pop_mode() {
        if (mode_depth > 0) {
            mode_depth--;
        }
    }

    void add_error(SourceLoc loc, const char* msg);
};

// ============================================================================
// Main Builder Functions
// ============================================================================

// Initialize builder with tree-sitter parse result
ASTBuilder* create_ast_builder(
    Arena* arena,
    const char* source,
    size_t source_len,
    TSTree* tree,
    ASTBuilderConfig config = default_config()
);

// Build complete AST from tree-sitter tree
TexNode* build_ast(ASTBuilder* builder);

// Build AST for a specific node
TexNode* build_node(ASTBuilder* builder, TSNode ts_node);

// ============================================================================
// Node-Specific Builders
// ============================================================================

// Document level
TexNode* build_document(ASTBuilder* builder, TSNode node);
TexNode* build_preamble(ASTBuilder* builder, TSNode node);

// Text mode
TexNode* build_text(ASTBuilder* builder, TSNode node);
TexNode* build_paragraph(ASTBuilder* builder, TSNode node);
TexNode* build_word(ASTBuilder* builder, TSNode node);
TexNode* build_space(ASTBuilder* builder, TSNode node);

// Math mode
TexNode* build_math_inline(ASTBuilder* builder, TSNode node);
TexNode* build_math_display(ASTBuilder* builder, TSNode node);
TexNode* build_math_content(ASTBuilder* builder, TSNode node);
TexNode* build_math_group(ASTBuilder* builder, TSNode node);
TexNode* build_subscript(ASTBuilder* builder, TSNode node);
TexNode* build_superscript(ASTBuilder* builder, TSNode node);
TexNode* build_fraction(ASTBuilder* builder, TSNode node);
TexNode* build_sqrt(ASTBuilder* builder, TSNode node);
TexNode* build_root(ASTBuilder* builder, TSNode node);

// Commands
TexNode* build_command(ASTBuilder* builder, TSNode node);
TexNode* build_environment(ASTBuilder* builder, TSNode node);
TexNode* build_braced_group(ASTBuilder* builder, TSNode node);

// Special
TexNode* build_comment(ASTBuilder* builder, TSNode node);
TexNode* build_special_char(ASTBuilder* builder, TSNode node);

// ============================================================================
// Math Symbol Classification
// ============================================================================

// Classify a math symbol into its atom type
AtomType classify_math_symbol(uint32_t codepoint);
AtomType classify_math_command(const char* cmd);

// Check if command changes mode
bool is_mode_changing_command(const char* cmd, Mode* new_mode);

// Check if command is a large operator
bool is_large_operator(const char* cmd);

// Check if command is a binary operator
bool is_binary_operator(const char* cmd);

// Check if command is a relation
bool is_relation(const char* cmd);

// ============================================================================
// Macro Handling
// ============================================================================

// Register built-in macros
void register_builtin_macros(ASTBuilder* builder);

// Define a new macro
void define_macro(ASTBuilder* builder, const MacroDef& def);

// Look up a macro
const MacroDef* lookup_macro(ASTBuilder* builder, const char* name);

// Expand a macro invocation
TexNode* expand_macro(
    ASTBuilder* builder,
    const MacroDef* macro,
    TSNode args_node
);

// ============================================================================
// Environment Handling
// ============================================================================

// Information about a LaTeX environment
struct EnvironmentInfo {
    const char* name;
    Mode content_mode;        // Mode inside environment
    bool is_math;             // True for math environments
    bool is_display;          // True for display math environments
    bool is_tabular;          // True for array/tabular-like
    int num_columns;          // For tabular environments
};

// Get info about an environment
const EnvironmentInfo* get_environment_info(const char* name);

// ============================================================================
// Utility Functions
// ============================================================================

// Extract text from a tree-sitter node
const char* node_text(ASTBuilder* builder, TSNode node, size_t* len);

// Create SourceLoc from tree-sitter node
SourceLoc make_source_loc(TSNode node);

// Check if tree-sitter node is of a specific type
bool node_is_type(TSNode node, const char* type_name);

// Get child by field name
TSNode node_child_by_field(TSNode node, const char* field_name);

// Iterate children
int node_child_count(TSNode node);
TSNode node_child(TSNode node, int index);

// ============================================================================
// Unicode/Character Handling
// ============================================================================

// Convert TeX escape sequence to Unicode codepoint
uint32_t tex_escape_to_codepoint(const char* escape);

// Get Unicode codepoint for named math symbol
uint32_t math_symbol_codepoint(const char* name);

// Check if character is a TeX special character
bool is_tex_special_char(char c);

} // namespace tex

#endif // TEX_AST_BUILDER_HPP
