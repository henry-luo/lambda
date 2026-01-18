// tex_catcode.cpp - TeX Category Code System Implementation
//
// Reference: TeXBook Chapter 7-8

#include "tex_catcode.hpp"
#include "../../lib/log.h"
#include <cstring>

namespace tex {

// ============================================================================
// CatCode Names
// ============================================================================

const char* catcode_name(CatCode cat) {
    switch (cat) {
        case CatCode::ESCAPE:      return "ESCAPE";
        case CatCode::BEGIN_GROUP: return "BEGIN_GROUP";
        case CatCode::END_GROUP:   return "END_GROUP";
        case CatCode::MATH_SHIFT:  return "MATH_SHIFT";
        case CatCode::ALIGN_TAB:   return "ALIGN_TAB";
        case CatCode::END_LINE:    return "END_LINE";
        case CatCode::PARAM:       return "PARAM";
        case CatCode::SUPERSCRIPT: return "SUPERSCRIPT";
        case CatCode::SUBSCRIPT:   return "SUBSCRIPT";
        case CatCode::IGNORED:     return "IGNORED";
        case CatCode::SPACE:       return "SPACE";
        case CatCode::LETTER:      return "LETTER";
        case CatCode::OTHER:       return "OTHER";
        case CatCode::ACTIVE:      return "ACTIVE";
        case CatCode::COMMENT:     return "COMMENT";
        case CatCode::INVALID:     return "INVALID";
        default:                   return "UNKNOWN";
    }
}

// ============================================================================
// CatCodeTable Implementation
// ============================================================================

CatCodeTable::CatCodeTable() {
    // default: all characters are OTHER
    for (int i = 0; i < 256; i++) {
        table[i] = CatCode::OTHER;
    }
}

CatCodeTable::CatCodeTable(const CatCodeTable& other) {
    memcpy(table, other.table, 256);
}

CatCodeTable& CatCodeTable::operator=(const CatCodeTable& other) {
    if (this != &other) {
        memcpy(table, other.table, 256);
    }
    return *this;
}

void CatCodeTable::set(unsigned char c, CatCode cat) {
    table[c] = cat;
}

CatCode CatCodeTable::get(unsigned char c) const {
    return table[c];
}

CatCodeTable CatCodeTable::plain_tex() {
    CatCodeTable cat;
    
    // escape character
    cat.set('\\', CatCode::ESCAPE);
    
    // grouping
    cat.set('{', CatCode::BEGIN_GROUP);
    cat.set('}', CatCode::END_GROUP);
    
    // math mode
    cat.set('$', CatCode::MATH_SHIFT);
    
    // alignment
    cat.set('&', CatCode::ALIGN_TAB);
    
    // end of line
    cat.set('\r', CatCode::END_LINE);
    cat.set('\n', CatCode::END_LINE);
    
    // parameter
    cat.set('#', CatCode::PARAM);
    
    // super/subscript
    cat.set('^', CatCode::SUPERSCRIPT);
    cat.set('_', CatCode::SUBSCRIPT);
    
    // null is ignored
    cat.set('\0', CatCode::IGNORED);
    
    // space and tab
    cat.set(' ', CatCode::SPACE);
    cat.set('\t', CatCode::SPACE);
    
    // letters: A-Z, a-z
    for (char c = 'A'; c <= 'Z'; c++) {
        cat.set(c, CatCode::LETTER);
    }
    for (char c = 'a'; c <= 'z'; c++) {
        cat.set(c, CatCode::LETTER);
    }
    
    // comment
    cat.set('%', CatCode::COMMENT);
    
    // delete is invalid
    cat.set((unsigned char)0x7F, CatCode::INVALID);
    
    // tilde is active
    cat.set('~', CatCode::ACTIVE);
    
    return cat;
}

CatCodeTable CatCodeTable::latex_default() {
    // LaTeX uses the same as plain TeX
    // Packages may modify this (e.g., babel, inputenc)
    return plain_tex();
}

void CatCodeTable::set_verbatim_mode(char end_char) {
    // In verbatim mode, almost everything is OTHER
    // except the end character which keeps its catcode
    CatCode saved_end = get(static_cast<unsigned char>(end_char));
    
    for (int i = 0; i < 256; i++) {
        table[i] = CatCode::OTHER;
    }
    
    // restore end character
    set(static_cast<unsigned char>(end_char), saved_end);
    
    // keep end of line as END_LINE
    set('\r', CatCode::END_LINE);
    set('\n', CatCode::END_LINE);
    
    // keep space as SPACE for proper handling
    set(' ', CatCode::SPACE);
    set('\t', CatCode::SPACE);
}

void CatCodeTable::restore_from(const CatCodeTable& saved) {
    memcpy(table, saved.table, 256);
}

void CatCodeTable::make_active(char c) {
    set(c, CatCode::ACTIVE);
}

void CatCodeTable::make_letter(char c) {
    set(c, CatCode::LETTER);
}

void CatCodeTable::make_other(char c) {
    set(c, CatCode::OTHER);
}

} // namespace tex
