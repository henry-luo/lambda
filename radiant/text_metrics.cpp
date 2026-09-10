#include "layout.hpp"

#include <utf8proc.h>

bool text_codepoint_has_zero_advance(uint32_t codepoint) {
    if (codepoint >= 0x1F3FB && codepoint <= 0x1F3FF) return true;  // emoji modifiers
    if (codepoint >= 0xFE00 && codepoint <= 0xFE0F) return true;    // variation selectors
    if (codepoint >= 0xE0100 && codepoint <= 0xE01EF) return true;  // variation selectors

    // CSS Text treats these controls as zero-advance even when the font
    // category lookup would not classify them as combining marks.
    if (codepoint == 0x00AD || codepoint == 0x034F || codepoint == 0x061C ||
        codepoint == 0x180E || codepoint == 0xFEFF ||
        (codepoint >= 0x200B && codepoint <= 0x200F) ||
        (codepoint >= 0x202A && codepoint <= 0x202E) ||
        (codepoint >= 0x2060 && codepoint <= 0x2064) ||
        (codepoint >= 0x2066 && codepoint <= 0x206F)) {
        return true;
    }

    utf8proc_category_t cat = utf8proc_category((utf8proc_int32_t)codepoint);
    return cat == UTF8PROC_CATEGORY_MN || cat == UTF8PROC_CATEGORY_ME;
}

float text_unicode_space_width_em(uint32_t codepoint) {
    if (text_codepoint_has_zero_advance(codepoint)) return -1.0f;

    switch (codepoint) {
        case 0x2000: return 0.5f;   // EN QUAD - width of 'n' (nominally 1/2 em)
        case 0x2001: return 1.0f;   // EM QUAD - width of 'm' (nominally 1 em)
        case 0x2002: return 0.5f;   // EN SPACE - 1/2 em
        case 0x2003: return 1.0f;   // EM SPACE - 1 em
        case 0x2004: return 1.0f/3; // THREE-PER-EM SPACE - 1/3 em
        case 0x2005: return 0.25f;  // FOUR-PER-EM SPACE - 1/4 em
        case 0x2006: return 1.0f/6; // SIX-PER-EM SPACE - 1/6 em
        case 0x2007: return 0.0f;   // FIGURE SPACE - use the selected font metric
        case 0x2009: return 1.0f/5; // THIN SPACE - ~1/5 em (or 1/6 em)
        case 0x200A: return 1.0f/10; // HAIR SPACE - very thin (~1/10 to 1/16 em)
        default: return 0.0f;
    }
}
