// latex_parser.hpp - LaTeX parser class declaration
// Modular parser combining PEG.js whitespace/mode handling with tree-sitter structure

#pragma once
#ifndef LAMBDA_INPUT_LATEX_PARSER_HPP
#define LAMBDA_INPUT_LATEX_PARSER_HPP

#include "../input-context.hpp"
#include "../../mark_builder.hpp"
#include "../../lambda-data.hpp"
#include "../../../lib/stringbuf.h"
#include <string>
#include <vector>
#include <stack>
#include <unordered_map>

namespace lambda {
namespace latex {

// Forward declarations
struct CommandSpec;
struct EnvironmentSpec;
class MacroRegistry;

// =============================================================================
// Mode Tracking (from PEG.js)
// =============================================================================

enum class LatexMode {
    Vertical,       // V - between paragraphs
    Horizontal,     // H - within paragraph
    Both,           // HV - works in both modes
    Paragraph,      // P - paragraph-level only
    Preamble,       // Before \begin{document}
    Math,           // Inside math mode
    RestrictedH,    // Restricted horizontal (in \hbox, etc.)
};

// =============================================================================
// Source Position Tracking
// =============================================================================

struct SourceSpan {
    size_t start_offset;
    size_t end_offset;
    uint32_t start_line;
    uint32_t start_col;
    uint32_t end_line;
    uint32_t end_col;
};

// =============================================================================
// Argument Specification (PEG.js style)
// =============================================================================

// Argument types for command parsing
// s = star (*), g = required group {}, o = optional group []
// i = identifier, n = number, l = length
// h = horizontal content, v = vertical content
// X = expandable content
enum class ArgType : char {
    Star = 's',           // Optional star (*)
    Group = 'g',          // Required group {...}
    OptGroup = 'o',       // Optional group [...]
    Identifier = 'i',     // Identifier {name}
    Number = 'n',         // Numeric expression {num}
    Length = 'l',         // Length {12pt}
    HContent = 'h',       // Horizontal content
    VContent = 'v',       // Vertical content
    Expandable = 'X',     // Expandable content
    Verbatim = 'V',       // Verbatim (doesn't expand)
};

struct ArgSpec {
    ArgType type;
    bool optional;  // ends with ?
};

// =============================================================================
// Length Value
// =============================================================================

struct Length {
    double value;
    std::string unit;  // sp, pt, px, dd, mm, pc, cc, cm, in, ex, em

    Length operator*(double factor) const { return {value * factor, unit}; }
};

// =============================================================================
// Main Parser Class
// =============================================================================

class LatexParser {
public:
    // Constructor takes Input context, source text, and length
    LatexParser(Input* input, const char* source, size_t len);

    // Main entry point - parse entire document
    Item parse();

    // Destructor
    ~LatexParser();

    // Non-copyable
    LatexParser(const LatexParser&) = delete;
    LatexParser& operator=(const LatexParser&) = delete;

private:
    // ==========================================================================
    // State
    // ==========================================================================

    Input* input_;
    InputContext ctx_;
    MarkBuilder& builder_;

    const char* source_;      // start of source
    const char* pos_;         // current position
    const char* end_;         // end of source

    LatexMode mode_;          // current parsing mode
    std::stack<LatexMode> mode_stack_;

    int depth_;               // recursion depth for safety
    static constexpr int MAX_DEPTH = 50;

    // Group balancing (PEG.js feature)
    std::stack<int> balance_stack_;

    // ==========================================================================
    // Mode Management
    // ==========================================================================

    void enter_mode(LatexMode m);
    void exit_mode();
    bool is_vmode() const { return mode_ == LatexMode::Vertical; }
    bool is_hmode() const { return mode_ == LatexMode::Horizontal; }
    bool is_math_mode() const { return mode_ == LatexMode::Math; }

    // ==========================================================================
    // Position Tracking
    // ==========================================================================

    size_t offset() const { return pos_ - source_; }
    bool at_end() const { return pos_ >= end_ || *pos_ == '\0'; }
    size_t remaining() const { return at_end() ? 0 : end_ - pos_; }

    SourceSpan make_span(size_t start) const;

    // ==========================================================================
    // Character Access
    // ==========================================================================

    char peek(int offset = 0) const;
    char advance();
    bool match(const char* str);
    bool match(char c);
    bool match_word(const char* word);  // match word followed by non-alpha
    bool lookahead(const char* str) const;

    // ==========================================================================
    // Whitespace & Comment Handling
    // ==========================================================================

    void skip_spaces();           // horizontal whitespace only (space, tab)
    void skip_whitespace();       // all whitespace including newlines
    void skip_comment();          // skip % comment to end of line
    bool is_comment() const { return peek() == '%'; }
    bool is_paragraph_break();    // blank line detection

    // ==========================================================================
    // Group Balancing
    // ==========================================================================

    void start_balanced();
    bool is_balanced();
    void end_balanced();

    // ==========================================================================
    // High-Level Parse Rules
    // ==========================================================================

    Item parse_document();
    Item parse_preamble();
    Item parse_body();
    Item parse_content();         // general content

    // ==========================================================================
    // Primitive Rules (PEG.js inspired)
    // ==========================================================================

    Item parse_primitive();
    Item parse_char();            // [a-zA-Z]
    Item parse_digit();           // [0-9]
    Item parse_punctuation();     // .,;:*/()!?=+<>
    Item parse_space();           // significant space
    Item parse_ligature();        // ff, fi, fl, ---, --, ``, ''
    Item parse_hyphen();          // -, --, ---
    Item parse_quotes();          // ``, '', `, '
    Item parse_ctrl_sym();        // \$, \%, \#, etc.
    Item parse_ctrl_space();      // \<space>, \<newline>
    Item parse_charsym();         // \char, ^^XX, ^^^^XXXX
    Item parse_diacritic();       // \', \`, \^, etc.
    Item parse_nbsp();            // ~ (non-breaking space)
    Item parse_utf8_char();       // multi-byte UTF-8

    // ==========================================================================
    // Text and Paragraphs
    // ==========================================================================

    Item parse_text();            // text until special char
    Item parse_paragraph();       // paragraph content
    Item parse_text_block();      // text block with paragraph break

    // Helper methods for primitives
    void append_utf8_char(StringBuf* sb);  // append UTF-8 char to buffer
    void normalize_whitespace(StringBuf* sb);  // normalize whitespace in buffer

    // ==========================================================================
    // Groups and Arguments
    // ==========================================================================

    Item parse_group();           // {...}
    Item parse_opt_group();       // [...]
    Item parse_balanced_content(char end_char);  // content until end_char
    std::string parse_balanced_braces();         // content inside matched {}

    // Argument parsing based on spec
    std::vector<ArgSpec> parse_arg_spec(const char* spec);
    std::vector<Item> parse_command_args(const char* spec);
    Item parse_single_arg(ArgType type, bool optional);

    // ==========================================================================
    // Commands
    // ==========================================================================

    Item parse_command();
    std::string parse_command_name();
    Item dispatch_command(const std::string& name);

    // Control symbols and special chars (after backslash consumed)
    Item parse_ctrl_sym_after_backslash();
    Item parse_diacritic_after_backslash();
    Item parse_charsym_after_backslash();

    // Generic command parsing
    Item parse_generic_command(const std::string& name, const CommandSpec* spec);
    Item parse_linebreak_args();

    // Symbol commands (no arguments)
    Item parse_symbol_command(const std::string& name);

    // Font commands
    Item parse_font_command(const std::string& name);

    // Spacing commands
    Item parse_spacing_command(const std::string& name);

    // Counter commands (already implemented)
    Item parse_counter_command(const std::string& name);

    // Reference commands
    Item parse_ref_command(const std::string& name);

    // Special commands
    Item parse_verb_command();
    Item parse_item_command();

    // ==========================================================================
    // Environments
    // ==========================================================================

    Item parse_environment();
    Item parse_begin_env();
    Item parse_end_env(const std::string& expected_name);

    Item parse_generic_environment(const std::string& name);
    Item parse_list_environment(const std::string& name);
    Item parse_tabular_environment(const std::string& name);
    Item parse_verbatim_environment(const std::string& name);
    Item parse_math_environment_content(const std::string& name);

    bool is_math_environment(const std::string& name) const;
    bool is_verbatim_environment(const std::string& name) const;
    bool is_list_environment(const std::string& name) const;

    // ==========================================================================
    // Sections (tree-sitter style hierarchy)
    // ==========================================================================

    Item parse_section_command(const std::string& name, int level);
    bool is_section_command() const;
    int get_section_level(const std::string& name) const;

    // ==========================================================================
    // Math Mode (kept minimal - delegates to existing math parser)
    // ==========================================================================

    Item parse_inline_math();     // $...$
    Item parse_display_math();    // $$...$$ or \[...\]
    Item parse_math_content(const std::string& delimiter);
    Item parse_display_math_content();
    Item parse_math_content_impl(const std::string& delimiter, bool display);

    // ==========================================================================
    // Numbers and Lengths
    // ==========================================================================

    int parse_integer();
    double parse_float();
    int parse_hex(int digits);
    int parse_octal();
    Length parse_length();
    std::string parse_length_unit();

    // Numeric expressions (for counters)
    int parse_num_expr();
    int parse_num_term();
    int parse_num_factor();
    int parse_value_command();    // \value{counter}

    // ==========================================================================
    // Identifier Parsing
    // ==========================================================================

    std::string parse_identifier();

    // ==========================================================================
    // Element Creation Helpers
    // ==========================================================================

    Item create_element(const char* tag);
    Item create_text(const char* text, size_t len);
    Item create_text(const std::string& text);
    Item create_space();
    Item create_nbsp();
    Item create_parbreak();

    // Create char from Unicode code point
    Item char_from_code(int code);

    // ==========================================================================
    // Error Handling
    // ==========================================================================

    void error(const char* fmt, ...);
    void warning(const char* fmt, ...);
    void expect(char c);
    void expect(const char* str);
};

// =============================================================================
// Command Specification
// =============================================================================

struct CommandSpec {
    const char* name;
    const char* arg_spec;     // PEG.js style argument specification
    bool is_symbol;           // No arguments, produces symbol
    bool gobbles_space;       // Consumes trailing whitespace
    LatexMode mode;           // Which mode this command works in

    // Handler type - function pointer or enum for special handling
    enum class Handler {
        Default,              // Use generic argument parsing
        Symbol,               // Symbol command (no args)
        Font,                 // Font command
        Spacing,              // Spacing command
        Section,              // Section command
        Counter,              // Counter command
        Ref,                  // Reference command
        Environment,          // Environment command (begin/end)
        Verb,                 // Verbatim command
        Item,                 // List item command
        Special,              // Requires special handling
    };
    Handler handler;
};

// =============================================================================
// Environment Specification
// =============================================================================

enum class EnvType {
    Generic,        // Standard environment
    Math,           // Math environments (equation, align, etc.)
    Verbatim,       // Verbatim content (verbatim, lstlisting, etc.)
    List,           // List environments (itemize, enumerate, description)
    Tabular,        // Table environments
    Figure,         // Float environments
    Theorem,        // Theorem-like environments
};

struct EnvironmentSpec {
    const char* name;
    EnvType type;
    const char* arg_spec;     // Arguments after \begin{name}
    bool takes_options;       // Has [options] after name
};

// =============================================================================
// Registry Access Functions
// =============================================================================

const CommandSpec* find_command(const char* name);
const EnvironmentSpec* find_environment(const char* name);

// Initialize registries (called once)
void init_registries();

} // namespace latex
} // namespace lambda

#endif // LAMBDA_INPUT_LATEX_PARSER_HPP
