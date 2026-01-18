// tex_token.cpp - TeX Token Implementation
//
// Reference: TeXBook Chapter 7

#include "tex_token.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cstdio>

namespace tex {

// ============================================================================
// Token Constructors
// ============================================================================

Token Token::make_char(char c, CatCode cat, SourceLoc loc) {
    Token t;
    t.type = TokenType::CHAR;
    t.catcode = cat;
    t.noexpand = false;
    t.chr.ch = c;
    t.loc = loc;
    return t;
}

Token Token::make_cs(const char* name, size_t len, Arena* arena, SourceLoc loc) {
    Token t;
    t.type = TokenType::CS;
    t.catcode = CatCode::ESCAPE;  // nominal
    t.noexpand = false;
    
    // Copy name to arena
    char* name_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(name_copy, name, len);
    name_copy[len] = '\0';
    
    t.cs.name = name_copy;
    t.cs.len = (uint16_t)len;
    t.loc = loc;
    return t;
}

Token Token::make_param(int num, SourceLoc loc) {
    Token t;
    t.type = TokenType::PARAM;
    t.catcode = CatCode::PARAM;
    t.noexpand = false;
    t.param.num = (int8_t)num;
    t.loc = loc;
    return t;
}

Token Token::make_active(char c, SourceLoc loc) {
    Token t;
    t.type = TokenType::CS_ACTIVE;
    t.catcode = CatCode::ACTIVE;
    t.noexpand = false;
    t.chr.ch = c;
    t.loc = loc;
    return t;
}

Token Token::make_end() {
    Token t;
    t.type = TokenType::END_OF_INPUT;
    t.catcode = CatCode::INVALID;
    t.noexpand = false;
    t.chr.ch = '\0';
    t.loc = SourceLoc();
    return t;
}

// ============================================================================
// Token Predicates
// ============================================================================

bool Token::is_cs(const char* name) const {
    if (type != TokenType::CS) return false;
    return cs.len == strlen(name) && memcmp(cs.name, name, cs.len) == 0;
}

bool Token::char_code_equal(const Token& other) const {
    // For \if: compare character codes
    // Control sequences compare as their string codes
    if (type == TokenType::CHAR && other.type == TokenType::CHAR) {
        return chr.ch == other.chr.ch;
    }
    // Different types can't be equal for \if purposes
    return false;
}

bool Token::catcode_equal(const Token& other) const {
    // For \ifcat: compare category codes
    if (type == TokenType::CHAR && other.type == TokenType::CHAR) {
        return catcode == other.catcode;
    }
    if (type == TokenType::CS && other.type == TokenType::CS) {
        return true;  // All CS have same "catcode" (ESCAPE)
    }
    return false;
}

const char* Token::to_string(Arena* arena) const {
    static char buf[256];
    
    switch (type) {
        case TokenType::CHAR:
            if (chr.ch >= 32 && chr.ch < 127) {
                snprintf(buf, sizeof(buf), "'%c'[%s]", chr.ch, catcode_name(catcode));
            } else {
                snprintf(buf, sizeof(buf), "'\\x%02x'[%s]", (unsigned char)chr.ch, catcode_name(catcode));
            }
            break;
            
        case TokenType::CS:
            snprintf(buf, sizeof(buf), "\\%.*s", (int)cs.len, cs.name);
            break;
            
        case TokenType::PARAM:
            if (param.num == -1) {
                snprintf(buf, sizeof(buf), "##");
            } else {
                snprintf(buf, sizeof(buf), "#%d", param.num);
            }
            break;
            
        case TokenType::CS_ACTIVE:
            snprintf(buf, sizeof(buf), "~'%c'", chr.ch);
            break;
            
        case TokenType::END_OF_INPUT:
            snprintf(buf, sizeof(buf), "<END>");
            break;
            
        default:
            snprintf(buf, sizeof(buf), "<UNKNOWN>");
            break;
    }
    
    return buf;
}

// ============================================================================
// TokenList Implementation
// ============================================================================

TokenList::TokenList()
    : arena(nullptr)
    , head(nullptr)
    , tail(nullptr)
    , count(0)
{
}

TokenList::TokenList(Arena* arena)
    : arena(arena)
    , head(nullptr)
    , tail(nullptr)
    , count(0)
{
}

TokenNode* TokenList::alloc_node() {
    if (arena) {
        return (TokenNode*)arena_alloc(arena, sizeof(TokenNode));
    }
    // fallback to malloc (caller must free)
    return (TokenNode*)malloc(sizeof(TokenNode));
}

void TokenList::push_back(const Token& t) {
    TokenNode* node = alloc_node();
    node->token = t;
    node->next = nullptr;
    
    if (tail) {
        tail->next = node;
        tail = node;
    } else {
        head = tail = node;
    }
    count++;
}

void TokenList::push_front(const Token& t) {
    TokenNode* node = alloc_node();
    node->token = t;
    node->next = head;
    
    head = node;
    if (!tail) {
        tail = node;
    }
    count++;
}

Token TokenList::pop_front() {
    if (!head) {
        return Token::make_end();
    }
    
    TokenNode* node = head;
    Token t = node->token;
    
    head = node->next;
    if (!head) {
        tail = nullptr;
    }
    count--;
    
    // Note: don't free node if arena-allocated
    return t;
}

void TokenList::append(TokenList& other) {
    if (other.empty()) return;
    
    if (tail) {
        tail->next = other.head;
    } else {
        head = other.head;
    }
    tail = other.tail;
    count += other.count;
    
    // Clear other
    other.head = other.tail = nullptr;
    other.count = 0;
}

void TokenList::clear() {
    head = tail = nullptr;
    count = 0;
}

TokenList TokenList::substitute(TokenList* args, int arg_count, Arena* target) const {
    TokenList result(target);
    
    for (const TokenNode* node = head; node; node = node->next) {
        const Token& t = node->token;
        
        if (t.type == TokenType::PARAM) {
            int num = t.param.num;
            
            if (num == -1) {
                // ## → #
                result.push_back(Token::make_char('#', CatCode::OTHER, t.loc));
            } else if (num >= 1 && num <= arg_count && args) {
                // #n → substitute argument n
                TokenList& arg = args[num - 1];
                for (const TokenNode* an = arg.head; an; an = an->next) {
                    result.push_back(an->token);
                }
            }
        } else {
            result.push_back(t);
        }
    }
    
    return result;
}

TokenList TokenList::copy(Arena* target_arena) const {
    TokenList result(target_arena);
    
    for (const TokenNode* node = head; node; node = node->next) {
        const Token& t = node->token;
        
        // For CS tokens, need to copy the name
        if (t.type == TokenType::CS) {
            result.push_back(Token::make_cs(t.cs.name, t.cs.len, target_arena, t.loc));
        } else {
            result.push_back(t);
        }
    }
    
    return result;
}

void TokenList::dump() const {
    log_debug("TokenList[%zu]: ", count);
    for (const TokenNode* node = head; node; node = node->next) {
        log_debug("  %s", node->token.to_string());
    }
}

// ============================================================================
// Token Meaning Comparison
// ============================================================================

bool meanings_equal(const TokenMeaning& a, const TokenMeaning& b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case TokenMeaning::Type::UNDEFINED:
            return true;  // Both undefined = equal
            
        case TokenMeaning::Type::PRIMITIVE:
            // Would need to compare primitive codes
            return false;
            
        case TokenMeaning::Type::MACRO:
            // Compare macro definitions
            // For now, just pointer equality (same definition object)
            return a.macro_def == b.macro_def;
            
        case TokenMeaning::Type::CHAR_DEF:
        case TokenMeaning::Type::LET_CHAR:
            return a.char_code == b.char_code && a.char_catcode == b.char_catcode;
            
        default:
            return false;
    }
}

} // namespace tex
