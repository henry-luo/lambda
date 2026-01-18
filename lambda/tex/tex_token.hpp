// tex_token.hpp - TeX Token Representation
//
// Tokens are the fundamental units of TeX processing.
// They are created by the tokenizer (Mouth) and consumed by the expander (Gullet).
//
// Reference: TeXBook Chapter 7

#ifndef TEX_TOKEN_HPP
#define TEX_TOKEN_HPP

#include "tex_catcode.hpp"
#include "../../lib/arena.h"
#include <cstdint>
#include <cstddef>

namespace tex {

// ============================================================================
// Source Location
// ============================================================================

struct SourceLoc {
    uint32_t line;          // 1-based line number
    uint32_t column;        // 1-based column number
    uint32_t offset;        // byte offset from start
    
    SourceLoc() : line(1), column(1), offset(0) {}
    SourceLoc(uint32_t l, uint32_t c, uint32_t o) : line(l), column(c), offset(o) {}
};

// ============================================================================
// Token Types
// ============================================================================

enum class TokenType : uint8_t {
    CHAR,           // Character with catcode
    CS,             // Control sequence (\name)
    PARAM,          // Parameter token (#1, #2, etc.)
    CS_ACTIVE,      // Active character treated as control sequence
    END_OF_INPUT,   // End of input stream
};

// ============================================================================
// Token Structure
// ============================================================================

struct Token {
    TokenType type;
    CatCode catcode;        // For CHAR tokens, the category code
    bool noexpand;          // If true, don't expand even if expandable (from \noexpand/\unexpanded)
    
    union {
        struct {
            char ch;        // The character
        } chr;
        
        struct {
            const char* name;   // Control sequence name (arena-allocated)
            uint16_t len;       // Length of name
        } cs;
        
        struct {
            int8_t num;         // Parameter number (1-9, or -1 for ##)
        } param;
    };
    
    SourceLoc loc;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    static Token make_char(char c, CatCode cat, SourceLoc loc = SourceLoc());
    static Token make_cs(const char* name, size_t len, Arena* arena, SourceLoc loc = SourceLoc());
    static Token make_param(int num, SourceLoc loc = SourceLoc());
    static Token make_active(char c, SourceLoc loc = SourceLoc());
    static Token make_end();
    
    // ========================================================================
    // Predicates
    // ========================================================================
    
    bool is_char() const { return type == TokenType::CHAR; }
    bool is_cs() const { return type == TokenType::CS; }
    bool is_param() const { return type == TokenType::PARAM; }
    bool is_active() const { return type == TokenType::CS_ACTIVE; }
    bool is_end() const { return type == TokenType::END_OF_INPUT; }
    
    // Check if this is a specific control sequence
    bool is_cs(const char* name) const;
    
    // Check catcode
    bool has_catcode(CatCode cat) const { return type == TokenType::CHAR && catcode == cat; }
    
    // Is this an expandable token? (might be, depends on definition)
    bool might_be_expandable() const { 
        return type == TokenType::CS || type == TokenType::CS_ACTIVE;
    }
    
    // ========================================================================
    // Comparison
    // ========================================================================
    
    // Compare character codes (\if)
    bool char_code_equal(const Token& other) const;
    
    // Compare category codes (\ifcat)
    bool catcode_equal(const Token& other) const;
    
    // ========================================================================
    // String Representation
    // ========================================================================
    
    // Convert to string for debugging/error messages
    // Returns pointer to static buffer or arena-allocated string
    const char* to_string(Arena* arena = nullptr) const;
    
    // Get the character code (for CHAR tokens)
    char get_char() const { return type == TokenType::CHAR ? chr.ch : '\0'; }
    
    // Get the control sequence name (for CS tokens)
    const char* get_cs_name() const { 
        return (type == TokenType::CS) ? cs.name : nullptr;
    }
    size_t get_cs_len() const {
        return (type == TokenType::CS) ? cs.len : 0;
    }
};

// ============================================================================
// Token List
// ============================================================================

// A linked list of tokens (for macro replacement, etc.)
struct TokenNode {
    Token token;
    TokenNode* next;
};

class TokenList {
public:
    TokenList();
    explicit TokenList(Arena* arena);
    
    // ========================================================================
    // Modification
    // ========================================================================
    
    void push_back(const Token& t);
    void push_front(const Token& t);
    Token pop_front();
    
    bool empty() const { return head == nullptr; }
    size_t size() const { return count; }
    
    // Append another list (moves tokens from other)
    void append(TokenList& other);
    
    // Clear all tokens
    void clear();
    
    // ========================================================================
    // Iteration
    // ========================================================================
    
    TokenNode* begin() { return head; }
    const TokenNode* begin() const { return head; }
    TokenNode* end() { return nullptr; }
    const TokenNode* end() const { return nullptr; }
    
    // ========================================================================
    // Macro Substitution
    // ========================================================================
    
    // Create a new list with parameter substitution
    // args[0] = #1, args[1] = #2, etc.
    TokenList substitute(TokenList* args, int arg_count, Arena* arena) const;
    
    // ========================================================================
    // Copying
    // ========================================================================
    
    // Deep copy to new arena
    TokenList copy(Arena* target_arena) const;
    
    // ========================================================================
    // Debug
    // ========================================================================
    
    void dump() const;
    
private:
    Arena* arena;
    TokenNode* head;
    TokenNode* tail;
    size_t count;
    
    TokenNode* alloc_node();
};

// ============================================================================
// Token Equivalence (for \ifx)
// ============================================================================

// Meaning of a token for comparison
struct TokenMeaning {
    enum class Type : uint8_t {
        UNDEFINED,      // Not defined
        PRIMITIVE,      // Built-in primitive
        MACRO,          // User-defined macro
        CHAR_DEF,       // \chardef'd token
        COUNT_DEF,      // \countdef'd token
        LET_CHAR,       // \let to a character
    };
    
    Type type;
    
    // For MACRO type
    const struct MacroDef* macro_def;
    
    // For CHAR_DEF, LET_CHAR
    char char_code;
    CatCode char_catcode;
};

// Compare two token meanings (\ifx)
bool meanings_equal(const TokenMeaning& a, const TokenMeaning& b);

} // namespace tex

#endif // TEX_TOKEN_HPP
