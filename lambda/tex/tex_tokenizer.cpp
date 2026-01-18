// tex_tokenizer.cpp - TeX Tokenizer (Mouth) Implementation
//
// Reference: TeXBook Chapter 7-8

#include "tex_tokenizer.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cctype>

namespace tex {

// ============================================================================
// InputSource Implementation
// ============================================================================

InputSource InputSource::from_string(const char* data, size_t len, const char* filename) {
    InputSource src;
    src.type = Type::STRING;
    src.string.data = data;
    src.string.len = len;
    src.string.pos = 0;
    src.filename = filename;
    src.loc = SourceLoc(1, 1, 0);
    src.state = InputState::NEW_LINE;
    return src;
}

InputSource InputSource::from_tokens(TokenList* list) {
    InputSource src;
    src.type = Type::TOKEN_LIST;
    src.tokens.list = list;
    src.tokens.current = list ? list->begin() : nullptr;
    src.filename = nullptr;
    src.loc = SourceLoc();
    src.state = InputState::MID_LINE;
    return src;
}

bool InputSource::at_end() const {
    switch (type) {
        case Type::STRING:
            return string.pos >= string.len;
        case Type::TOKEN_LIST:
            return tokens.current == nullptr;
        default:
            return true;
    }
}

int InputSource::peek_char() const {
    if (type != Type::STRING) return -1;
    if (string.pos >= string.len) return -1;
    return (unsigned char)string.data[string.pos];
}

int InputSource::get_char() {
    if (type != Type::STRING) return -1;
    if (string.pos >= string.len) return -1;
    
    char c = string.data[string.pos++];
    loc.offset++;
    
    if (c == '\n') {
        loc.line++;
        loc.column = 1;
    } else {
        loc.column++;
    }
    
    return (unsigned char)c;
}

Token InputSource::get_token() {
    if (type != Type::TOKEN_LIST) return Token::make_end();
    if (!tokens.current) return Token::make_end();
    
    Token t = tokens.current->token;
    tokens.current = tokens.current->next;
    return t;
}

// ============================================================================
// Tokenizer Implementation
// ============================================================================

Tokenizer::Tokenizer(Arena* arena)
    : arena(arena)
    , input_depth(0)
    , catcode_table(CatCodeTable::latex_default())
    , pushed_tokens(arena)
    , endline_char('\r')
{
}

Tokenizer::~Tokenizer() {
    // Arena handles all allocations
}

void Tokenizer::push_input(const char* data, size_t len, const char* filename) {
    if (input_depth >= MAX_INPUT_STACK) {
        log_error("tokenizer: input stack overflow");
        return;
    }
    input_stack[input_depth++] = InputSource::from_string(data, len, filename);
}

void Tokenizer::push_tokens(TokenList* list) {
    if (input_depth >= MAX_INPUT_STACK) {
        log_error("tokenizer: input stack overflow");
        return;
    }
    input_stack[input_depth++] = InputSource::from_tokens(list);
}

bool Tokenizer::at_end() const {
    return input_depth == 0 && pushed_tokens.empty();
}

void Tokenizer::set_catcode(char c, CatCode cat) {
    catcode_table.set(c, cat);
}

CatCode Tokenizer::get_catcode(char c) const {
    return catcode_table.get(c);
}

InputSource* Tokenizer::current_input() {
    return input_depth > 0 ? &input_stack[input_depth - 1] : nullptr;
}

const InputSource* Tokenizer::current_input() const {
    return input_depth > 0 ? &input_stack[input_depth - 1] : nullptr;
}

void Tokenizer::pop_input() {
    if (input_depth > 0) {
        input_depth--;
    }
}

InputState Tokenizer::get_state() const {
    const InputSource* src = current_input();
    return src ? src->state : InputState::NEW_LINE;
}

SourceLoc Tokenizer::get_loc() const {
    const InputSource* src = current_input();
    return src ? src->loc : SourceLoc();
}

void Tokenizer::push_back(const Token& t) {
    pushed_tokens.push_front(t);
}

Token Tokenizer::peek_token() {
    Token t = get_token();
    if (!t.is_end()) {
        push_back(t);
    }
    return t;
}

int Tokenizer::get_next_char() {
    InputSource* src = current_input();
    if (!src || src->type != InputSource::Type::STRING) {
        return -1;
    }
    
    int c = src->get_char();
    if (c == -1) return -1;
    
    // Check for ^^notation
    if (c == '^' && catcode_table.get('^') == CatCode::SUPERSCRIPT) {
        int c2 = src->peek_char();
        if (c2 == '^') {
            src->get_char();  // consume second ^
            return process_superscript_notation();
        }
    }
    
    return c;
}

int Tokenizer::process_superscript_notation() {
    // After seeing ^^, read the encoded character
    InputSource* src = current_input();
    if (!src) return -1;
    
    int c = src->peek_char();
    if (c == -1) return '^';  // Just return ^ if at end
    
    // Check for ^^XY (hex notation)
    if (isxdigit(c)) {
        int c1 = src->get_char();
        int c2 = src->peek_char();
        if (c2 != -1 && isxdigit(c2)) {
            src->get_char();
            // Parse as hex
            auto hex_val = [](int ch) {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return 0;
            };
            return (hex_val(c1) << 4) | hex_val(c2);
        }
        // Not hex pair, use single char transformation
        // ^^X where X is 64..127 → X-64, X is 0..63 → X+64
        if (c1 >= 64) return c1 - 64;
        return c1 + 64;
    }
    
    // Single character transformation
    c = src->get_char();
    if (c >= 64) return c - 64;
    return c + 64;
}

void Tokenizer::skip_to_eol() {
    InputSource* src = current_input();
    if (!src || src->type != InputSource::Type::STRING) return;
    
    while (!src->at_end()) {
        int c = src->get_char();
        if (c == '\n' || c == '\r') {
            src->state = InputState::NEW_LINE;
            break;
        }
    }
}

Token Tokenizer::read_control_sequence() {
    InputSource* src = current_input();
    if (!src) return Token::make_end();
    
    SourceLoc start_loc = src->loc;
    
    // collect the control sequence name
    char name_buf[256];
    size_t name_len = 0;
    
    int c = src->peek_char();
    if (c == -1) {
        // \<eof> → control sequence with empty name (treated as \csname\endcsname)
        return Token::make_cs("", 0, arena, start_loc);
    }
    
    CatCode cat = catcode_table.get((char)c);
    
    if (cat == CatCode::LETTER) {
        // Multi-letter control sequence: \abc
        while (!src->at_end()) {
            c = src->peek_char();
            if (c == -1) break;
            cat = catcode_table.get((char)c);
            if (cat != CatCode::LETTER) break;
            
            if (name_len < sizeof(name_buf) - 1) {
                name_buf[name_len++] = (char)src->get_char();
            } else {
                src->get_char();  // consume but don't store
            }
        }
        
        // After multi-letter CS, enter SKIP_BLANKS state
        src->state = InputState::SKIP_BLANKS;
    } else {
        // Single-character control sequence: \# \$ etc.
        name_buf[name_len++] = (char)src->get_char();
        
        // After single-char CS, enter MID_LINE state (unless it was a space)
        if (cat == CatCode::SPACE) {
            src->state = InputState::SKIP_BLANKS;
        } else {
            src->state = InputState::MID_LINE;
        }
    }
    
    return Token::make_cs(name_buf, name_len, arena, start_loc);
}

Token Tokenizer::get_token() {
    // first check pushed-back tokens
    if (!pushed_tokens.empty()) {
        return pushed_tokens.pop_front();
    }
    
    // Check for token list input
    while (input_depth > 0) {
        InputSource* src = current_input();
        
        if (src->type == InputSource::Type::TOKEN_LIST) {
            if (!src->at_end()) {
                return src->get_token();
            }
            pop_input();
            continue;
        }
        
        if (src->type != InputSource::Type::STRING) {
            pop_input();
            continue;
        }
        
        // String input - tokenize
        if (src->at_end()) {
            pop_input();
            continue;
        }
        
        SourceLoc loc = src->loc;
        int c = get_next_char();
        if (c == -1) {
            pop_input();
            continue;
        }
        
        CatCode cat = catcode_table.get((char)c);
        
        switch (cat) {
            case CatCode::ESCAPE: {
                // control sequence
                Token cs = read_control_sequence();
                return cs;
            }
            
            case CatCode::END_LINE: {
                // End of line handling depends on state
                InputState state = src->state;
                src->state = InputState::NEW_LINE;
                
                if (state == InputState::NEW_LINE) {
                    // Empty line → \par
                    return Token::make_cs("par", 3, arena, loc);
                } else if (state == InputState::MID_LINE) {
                    // End of line → space
                    return Token::make_char(' ', CatCode::SPACE, loc);
                }
                // SKIP_BLANKS state: ignore
                continue;
            }
            
            case CatCode::SPACE: {
                if (src->state == InputState::NEW_LINE || src->state == InputState::SKIP_BLANKS) {
                    // ignore space at start of line or after CS
                    continue;
                }
                // Compress multiple spaces to one
                while (!src->at_end()) {
                    int c2 = src->peek_char();
                    if (c2 == -1) break;
                    if (catcode_table.get((char)c2) != CatCode::SPACE) break;
                    src->get_char();
                }
                src->state = InputState::SKIP_BLANKS;
                return Token::make_char(' ', CatCode::SPACE, loc);
            }
            
            case CatCode::COMMENT:
                // skip to end of line
                skip_to_eol();
                continue;
            
            case CatCode::IGNORED:
                // skip ignored characters
                continue;
            
            case CatCode::INVALID:
                log_error("tokenizer: invalid character 0x%02x at %d:%d", 
                          c, loc.line, loc.column);
                continue;
            
            case CatCode::ACTIVE:
                // Active character → treat like control sequence
                src->state = InputState::MID_LINE;
                return Token::make_active((char)c, loc);
            
            case CatCode::PARAM: {
                // Parameter token
                src->state = InputState::MID_LINE;
                int c2 = src->peek_char();
                if (c2 == -1) {
                    log_error("tokenizer: unexpected end after #");
                    return Token::make_char('#', CatCode::OTHER, loc);
                }
                
                if (c2 >= '1' && c2 <= '9') {
                    src->get_char();
                    return Token::make_param(c2 - '0', loc);
                } else if (c2 == '#') {
                    // ## → # (in replacement text only, but we tokenize it)
                    src->get_char();
                    return Token::make_param(-1, loc);  // Special: means ##
                }
                // Standalone # - error in some contexts
                return Token::make_char('#', CatCode::PARAM, loc);
            }
            
            case CatCode::SUPERSCRIPT:
            case CatCode::SUBSCRIPT:
            case CatCode::MATH_SHIFT:
            case CatCode::ALIGN_TAB:
            case CatCode::BEGIN_GROUP:
            case CatCode::END_GROUP:
            case CatCode::LETTER:
            case CatCode::OTHER:
                // normal character token
                src->state = InputState::MID_LINE;
                return Token::make_char((char)c, cat, loc);
            
            default:
                // Unknown catcode - treat as OTHER
                src->state = InputState::MID_LINE;
                return Token::make_char((char)c, CatCode::OTHER, loc);
        }
    }
    
    return Token::make_end();
}

} // namespace tex
