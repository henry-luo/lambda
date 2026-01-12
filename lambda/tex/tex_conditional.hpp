// tex_conditional.hpp - TeX Conditional Processing
//
// Implements conditionals following TeXBook Chapter 20.
// Supports \if, \ifx, \ifnum, \ifdim, \ifcat, etc.
//
// Reference: TeXBook Chapter 20

#ifndef TEX_CONDITIONAL_HPP
#define TEX_CONDITIONAL_HPP

#include "tex_macro.hpp"
#include "../../lib/arena.h"
#include <cstddef>

namespace tex {

// ============================================================================
// Conditional Types
// ============================================================================

enum class ConditionalType : uint8_t {
    If,         // \if - compare character codes
    Ifx,        // \ifx - compare meanings
    Ifcat,      // \ifcat - compare category codes
    Ifnum,      // \ifnum - compare numbers
    Ifdim,      // \ifdim - compare dimensions
    Ifodd,      // \ifodd - test if odd
    Ifvmode,    // \ifvmode - in vertical mode?
    Ifhmode,    // \ifhmode - in horizontal mode?
    Ifmmode,    // \ifmmode - in math mode?
    Ifinner,    // \ifinner - in inner mode?
    Ifvoid,     // \ifvoid - box register empty?
    Ifhbox,     // \ifhbox - is hbox?
    Ifvbox,     // \ifvbox - is vbox?
    Ifeof,      // \ifeof - end of file?
    Iftrue,     // \iftrue - always true
    Iffalse,    // \iffalse - always false
    Ifcase,     // \ifcase - multi-way branch
    Ifdefined,  // \ifdefined - is macro defined? (e-TeX)
};

// ============================================================================
// Conditional State
// ============================================================================

struct ConditionalState {
    ConditionalType type;
    bool result;            // True or false branch
    int nesting_level;      // For tracking nested conditionals
    bool skip_else;         // Currently skipping \else branch
};

// Stack of conditional states (for nesting)
struct ConditionalStack {
    ConditionalState* states;
    int count;
    int capacity;

    void push(ConditionalState state);
    ConditionalState pop();
    ConditionalState* top();
    bool empty() const { return count == 0; }
};

// ============================================================================
// Conditional Processor
// ============================================================================

class ConditionalProcessor {
public:
    ConditionalProcessor(Arena* arena, MacroProcessor* macros);
    ~ConditionalProcessor();

    // ========================================================================
    // Processing
    // ========================================================================

    // Process conditionals in input, returning expanded result
    char* process(const char* input, size_t len, size_t* out_len);

    // Evaluate a single conditional at position
    // Returns the number of characters consumed
    size_t evaluate_conditional(const char* input, size_t pos, size_t len,
                                bool* result);

    // ========================================================================
    // Specific Conditionals
    // ========================================================================

    // \if - compare character codes of next two tokens
    bool eval_if(const char* input, size_t* pos, size_t len);

    // \ifx - compare meanings of next two tokens
    bool eval_ifx(const char* input, size_t* pos, size_t len);

    // \ifnum - compare two numbers with relation
    bool eval_ifnum(const char* input, size_t* pos, size_t len);

    // \ifdim - compare two dimensions with relation
    bool eval_ifdim(const char* input, size_t* pos, size_t len);

    // \ifodd - test if number is odd
    bool eval_ifodd(const char* input, size_t* pos, size_t len);

    // \ifdefined - test if token is defined (e-TeX)
    bool eval_ifdefined(const char* input, size_t* pos, size_t len);

    // ========================================================================
    // Mode Testing
    // ========================================================================

    void set_vertical_mode(bool v) { in_vmode = v; }
    void set_horizontal_mode(bool h) { in_hmode = h; }
    void set_math_mode(bool m) { in_mmode = m; }
    void set_inner_mode(bool i) { in_inner = i; }

private:
    Arena* arena;
    MacroProcessor* macros;
    ConditionalStack stack;

    // Mode flags
    bool in_vmode;
    bool in_hmode;
    bool in_mmode;
    bool in_inner;

    // Helpers
    size_t skip_whitespace(const char* input, size_t pos, size_t len);
    size_t parse_token(const char* input, size_t pos, size_t len,
                       const char** token, size_t* token_len);
    int parse_number(const char* input, size_t* pos, size_t len);
    float parse_dimension(const char* input, size_t* pos, size_t len);
    int parse_relation(const char* input, size_t* pos, size_t len);

    // Find matching \fi, accounting for nesting
    size_t find_fi(const char* input, size_t pos, size_t len);

    // Find \else or \fi
    size_t find_else_or_fi(const char* input, size_t pos, size_t len, bool* found_else);
};

// ============================================================================
// Utility Functions
// ============================================================================

// Check if a string starts with a conditional command
bool is_conditional_command(const char* str, size_t len);

// Get the type of conditional from command name
ConditionalType get_conditional_type(const char* cmd, size_t len);

} // namespace tex

#endif // TEX_CONDITIONAL_HPP
