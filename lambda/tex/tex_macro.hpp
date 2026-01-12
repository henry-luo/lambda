// tex_macro.hpp - TeX Macro Processor
//
// Implements macro expansion following TeXBook Chapter 20.
// Supports \def, \edef, \gdef, \xdef, \newcommand, etc.
//
// Reference: TeXBook Chapter 20

#ifndef TEX_MACRO_HPP
#define TEX_MACRO_HPP

#include "../../lib/arena.h"
#include "../../lib/hashmap.h"
#include <cstddef>

namespace tex {

// ============================================================================
// Macro Definition
// ============================================================================

// Parameter type for macro arguments
enum class MacroParamType : uint8_t {
    Undelimited,    // Normal #1 parameter
    Delimited,      // #1. (delimited by following text)
    Optional,       // [default] - LaTeX-style optional argument
};

struct MacroParam {
    MacroParamType type;
    const char* delimiter;      // Text that delimits this parameter (null for undelimited)
    size_t delimiter_len;
    const char* default_value;  // Default value for optional parameters
    size_t default_len;
};

struct MacroDef {
    const char* name;           // Macro name (without backslash)
    size_t name_len;
    int param_count;            // Number of parameters (0-9)
    MacroParam* params;         // Parameter specifications
    const char* replacement;    // Replacement text with #1, #2, etc.
    size_t replacement_len;
    bool is_long;               // \long\def - can span paragraphs
    bool is_outer;              // \outer\def - can't be in arguments
    bool is_protected;          // \protected\def - robust command
    bool is_global;             // \gdef or global scope

    MacroDef()
        : name(nullptr), name_len(0)
        , param_count(0), params(nullptr)
        , replacement(nullptr), replacement_len(0)
        , is_long(false), is_outer(false)
        , is_protected(false), is_global(false) {}
};

// ============================================================================
// Macro Processor State
// ============================================================================

class MacroProcessor {
public:
    MacroProcessor(Arena* arena);
    ~MacroProcessor();

    // ========================================================================
    // Macro Definition
    // ========================================================================

    // Define a macro (\def\name#1#2{replacement})
    bool define(const char* name, size_t name_len,
                const char* param_text, size_t param_len,
                const char* replacement, size_t repl_len);

    // Define with full specification
    bool define_full(const MacroDef& def);

    // LaTeX-style \newcommand{\name}[nargs][default]{def}
    bool newcommand(const char* name, size_t name_len,
                    int nargs,
                    const char* default_arg, size_t default_len,
                    const char* definition, size_t def_len);

    // \renewcommand - redefine existing macro
    bool renewcommand(const char* name, size_t name_len,
                      int nargs,
                      const char* default_arg, size_t default_len,
                      const char* definition, size_t def_len);

    // \providecommand - define only if not already defined
    bool providecommand(const char* name, size_t name_len,
                        int nargs,
                        const char* default_arg, size_t default_len,
                        const char* definition, size_t def_len);

    // ========================================================================
    // Macro Lookup
    // ========================================================================

    // Check if a macro is defined
    bool is_defined(const char* name, size_t len) const;

    // Get macro definition (returns null if not defined)
    const MacroDef* get_macro(const char* name, size_t len) const;

    // ========================================================================
    // Macro Expansion
    // ========================================================================

    // Expand macros in input string
    // Returns allocated string (caller must manage memory)
    char* expand(const char* input, size_t len, size_t* out_len);

    // Expand a single macro call at position
    // Returns number of characters consumed, fills out_result
    size_t expand_one(const char* input, size_t pos, size_t len,
                      char** out_result, size_t* out_result_len);

    // Get current expansion depth
    int get_expansion_depth() const { return expansion_depth; }

    // ========================================================================
    // Control
    // ========================================================================

    // Enter/leave local scope (for grouping { })
    void begin_group();
    void end_group();

    // Set expansion limit (prevent infinite loops)
    void set_expansion_limit(int limit) { expansion_limit = limit; }

    // ========================================================================
    // Debugging
    // ========================================================================

    void dump_macros() const;

private:
    Arena* arena;
    HashMap* macros;            // name -> MacroDef*
    HashMap* saved_macros;      // For group scoping

    int expansion_depth;
    int expansion_limit;

    // Helper functions
    MacroParam* parse_param_text(const char* param_text, size_t len, int* param_count);
    char* substitute_params(const MacroDef* macro,
                            const char** args, size_t* arg_lens,
                            size_t* out_len);
    size_t match_argument(const char* input, size_t pos, size_t len,
                          const MacroParam* param,
                          const char** arg, size_t* arg_len);
    char* expand_recursive(const char* input, size_t len, size_t* out_len);
};

// ============================================================================
// Utility Functions
// ============================================================================

// Parse \def or \newcommand from input
// Returns position after the definition
size_t parse_macro_definition(
    const char* input, size_t pos, size_t len,
    MacroProcessor* processor
);

// Parse a single braced argument {content}
// Returns position after closing brace
size_t parse_braced_argument(
    const char* input, size_t pos, size_t len,
    const char** content, size_t* content_len
);

// Parse optional argument [content]
// Returns position after closing bracket (or pos if no optional)
size_t parse_optional_argument(
    const char* input, size_t pos, size_t len,
    const char** content, size_t* content_len
);

} // namespace tex

#endif // TEX_MACRO_HPP
