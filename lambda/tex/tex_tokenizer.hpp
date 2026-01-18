// tex_tokenizer.hpp - TeX Tokenizer (Mouth)
//
// The tokenizer converts characters to tokens following TeX's rules.
// This is the "Mouth" in TeX terminology (TeXBook Chapter 8).
//
// Features:
// - Category code based tokenization
// - Control sequence recognition
// - ^^notation handling
// - State machine (N/S/M states)
//
// Reference: TeXBook Chapter 7-8

#ifndef TEX_TOKENIZER_HPP
#define TEX_TOKENIZER_HPP

#include "tex_catcode.hpp"
#include "tex_token.hpp"
#include "../../lib/arena.h"
#include <cstddef>

namespace tex {

// ============================================================================
// Input Source
// ============================================================================

// An input source (file, string, or token list being inserted)
struct InputSource {
    enum class Type : uint8_t {
        STRING,         // String input
        FILE,           // File input (not implemented yet)
        TOKEN_LIST,     // Token list being inserted
    };
    
    Type type;
    
    union {
        struct {
            const char* data;
            size_t len;
            size_t pos;
        } string;
        
        struct {
            TokenList* list;
            TokenNode* current;
        } tokens;
    };
    
    // Source tracking
    const char* filename;
    SourceLoc loc;
    InputState state;
    
    // Create from string
    static InputSource from_string(const char* data, size_t len, const char* filename = nullptr);
    
    // Create from token list
    static InputSource from_tokens(TokenList* list);
    
    // Check if at end
    bool at_end() const;
    
    // Peek next character (returns -1 at end)
    int peek_char() const;
    
    // Get next character (advances position)
    int get_char();
    
    // Get next token (for TOKEN_LIST type)
    Token get_token();
};

// ============================================================================
// Tokenizer
// ============================================================================

class Tokenizer {
public:
    Tokenizer(Arena* arena);
    ~Tokenizer();
    
    // ========================================================================
    // Input Management
    // ========================================================================
    
    // Push a string input source
    void push_input(const char* data, size_t len, const char* filename = nullptr);
    
    // Push a token list to be read (for \expandafter, etc.)
    void push_tokens(TokenList* list);
    
    // Check if at end of all input
    bool at_end() const;
    
    // ========================================================================
    // Catcode Access
    // ========================================================================
    
    // Get the current catcode table
    CatCodeTable& catcodes() { return catcode_table; }
    const CatCodeTable& catcodes() const { return catcode_table; }
    
    // Set catcode for a character
    void set_catcode(char c, CatCode cat);
    CatCode get_catcode(char c) const;
    
    // ========================================================================
    // Tokenization
    // ========================================================================
    
    // Get the next token (main tokenization routine)
    Token get_token();
    
    // Peek at the next token without consuming it
    Token peek_token();
    
    // Push a token back to be read again
    void push_back(const Token& t);
    
    // ========================================================================
    // State
    // ========================================================================
    
    // Get current input state
    InputState get_state() const;
    
    // Get current source location
    SourceLoc get_loc() const;
    
    // Get end character for \endlinechar
    int get_endline_char() const { return endline_char; }
    void set_endline_char(int c) { endline_char = c; }
    
    // Debug: get input depth
    int get_input_depth() const { return input_depth; }
    
private:
    Arena* arena;
    
    // Input stack
    static constexpr int MAX_INPUT_STACK = 256;
    InputSource input_stack[MAX_INPUT_STACK];
    int input_depth;
    
    // Catcode table
    CatCodeTable catcode_table;
    
    // Pushed-back tokens
    TokenList pushed_tokens;
    
    // Configuration
    int endline_char;       // Character appended at end of line (default '\r')
    
    // ========================================================================
    // Internal Tokenization
    // ========================================================================
    
    // Get next raw character from input (handles ^^notation)
    int get_next_char();
    
    // Process ^^notation
    int process_superscript_notation();
    
    // Read a control sequence name
    Token read_control_sequence();
    
    // Skip to end of line (for comments)
    void skip_to_eol();
    
    // Current input source
    InputSource* current_input();
    const InputSource* current_input() const;
    
    // Pop exhausted input source
    void pop_input();
};

} // namespace tex

#endif // TEX_TOKENIZER_HPP
