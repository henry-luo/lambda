// math_symbols.hpp - Symbol lookup declarations for LaTeX math
//
// This header provides lookup functions for converting LaTeX commands
// to Unicode codepoints and determining atom types.

#ifndef LAMBDA_MATH_SYMBOLS_HPP
#define LAMBDA_MATH_SYMBOLS_HPP

namespace lambda {

// Math atom types for inter-atomic spacing (TeX's atom classification)
// See: The TeXBook, Chapter 17
enum class MathAtomType {
    Ord,    // Ordinary symbol (variables, numbers)
    Op,     // Large operator (sum, integral)
    Bin,    // Binary operator (+, -)
    Rel,    // Relation (=, <)
    Open,   // Opening delimiter (
    Close,  // Closing delimiter )
    Punct,  // Punctuation (comma)
    Inner,  // Inner (fractions)
    Acc,    // Accent (hat, tilde)
    Rad,    // Radical (sqrt)
    Vcent,  // Vertically centered (rule)
    Over,   // Over delimiter
    Under,  // Under delimiter
    Alpha   // Alphabetic character
};

// Look up a LaTeX command in the symbol tables
// Returns true if found, and sets codepoint and atom_type
// command can include leading backslash (e.g., "\\alpha" or "alpha")
bool lookup_math_symbol(const char* command, int* codepoint, MathAtomType* atom_type);

// Check if a command is an operator name (sin, cos, lim, etc.)
bool is_operator_name(const char* command);

// Check if a command is a large operator (sum, int, prod, etc.)
bool is_large_operator(const char* command);

// Get the atom type for a single character (+, -, =, etc.)
MathAtomType get_single_char_atom_type(char c);

} // namespace lambda

#endif // LAMBDA_MATH_SYMBOLS_HPP
