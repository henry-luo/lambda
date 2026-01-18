// tex_catcode.hpp - TeX Category Code System
//
// Implements TeX's category code system for proper tokenization.
// Each character is assigned a category code that determines how it's handled.
//
// Reference: TeXBook Chapter 7-8

#ifndef TEX_CATCODE_HPP
#define TEX_CATCODE_HPP

#include <cstdint>
#include <cstddef>

namespace tex {

// ============================================================================
// Category Codes
// ============================================================================

enum class CatCode : uint8_t {
    ESCAPE      = 0,    // \ - starts control sequence
    BEGIN_GROUP = 1,    // { - begins group
    END_GROUP   = 2,    // } - ends group
    MATH_SHIFT  = 3,    // $ - math mode toggle
    ALIGN_TAB   = 4,    // & - alignment tab
    END_LINE    = 5,    // \r, \n - end of line
    PARAM       = 6,    // # - macro parameter
    SUPERSCRIPT = 7,    // ^ - superscript
    SUBSCRIPT   = 8,    // _ - subscript
    IGNORED     = 9,    // null - ignored character
    SPACE       = 10,   // space, tab - space
    LETTER      = 11,   // a-z, A-Z - letters (part of control sequence names)
    OTHER       = 12,   // other characters (digits, punctuation)
    ACTIVE      = 13,   // ~ - active character (acts like a macro)
    COMMENT     = 14,   // % - comment (to end of line)
    INVALID     = 15,   // delete (0x7F) - invalid character
};

// Convert CatCode to string for debugging
const char* catcode_name(CatCode cat);

// ============================================================================
// CatCode Table
// ============================================================================

class CatCodeTable {
public:
    // Create with all characters set to OTHER
    CatCodeTable();
    
    // Copy constructor
    CatCodeTable(const CatCodeTable& other);
    CatCodeTable& operator=(const CatCodeTable& other);
    
    // Get/set category code for a character
    void set(unsigned char c, CatCode cat);
    CatCode get(unsigned char c) const;
    
    // Convenience for signed char
    CatCode get(char c) const { return get(static_cast<unsigned char>(c)); }
    void set(char c, CatCode cat) { set(static_cast<unsigned char>(c), cat); }
    
    // ========================================================================
    // Standard Configurations
    // ========================================================================
    
    // Initialize with plain TeX defaults (IniTeX)
    static CatCodeTable plain_tex();
    
    // Initialize with LaTeX defaults (after format loaded)
    static CatCodeTable latex_default();
    
    // ========================================================================
    // Mode Changes
    // ========================================================================
    
    // Set all catcodes for verbatim mode (everything is OTHER except end)
    void set_verbatim_mode(char end_char = '}');
    
    // Restore normal mode
    void restore_from(const CatCodeTable& saved);
    
    // Make a character active
    void make_active(char c);
    
    // Make a character a letter (for \makeatletter, etc.)
    void make_letter(char c);
    
    // Make a character other (for \makeatother, etc.)
    void make_other(char c);
    
private:
    CatCode table[256];
};

// ============================================================================
// Catcode State Machine
// ============================================================================

// State of the input processor (TeXBook p. 46)
enum class InputState : uint8_t {
    NEW_LINE,       // N - beginning of line (space ignored)
    SKIP_BLANKS,    // S - skipping blanks (space ignored)
    MID_LINE,       // M - middle of line (normal processing)
};

} // namespace tex

#endif // TEX_CATCODE_HPP
