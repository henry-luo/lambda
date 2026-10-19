#include "layout.hpp"
#include "../lib/utf.h"

#include <utf8proc.h>

bool layout_utf8_next_codepoint(const char** cursor, const char* end,
                                uint32_t* codepoint) {
    if (!cursor || !*cursor || !end || *cursor >= end || !codepoint) return false;
    int bytes = str_utf8_decode(*cursor, (size_t)(end - *cursor), codepoint);
    if (bytes <= 0 || bytes > end - *cursor) {
        (*cursor)++;
        return false;
    }
    *cursor += bytes;
    return true;
}

bool layout_utf8_next_codepoint(const unsigned char** cursor,
                                const unsigned char* end,
                                uint32_t* codepoint) {
    const char* text_cursor = cursor ? (const char*)*cursor : nullptr;
    bool decoded = layout_utf8_next_codepoint(
        &text_cursor, (const char*)end, codepoint);
    if (cursor) *cursor = (const unsigned char*)text_cursor;
    return decoded;
}

bool layout_text_native_break_allowed(const LayoutTextBreaks& breaks, size_t offset,
    uint32_t current, const unsigned char* next, const unsigned char* end,
    bool keep_all) {
    if (!breaks.allows_after(offset)) return false;
    uint32_t following = 0;
    layout_utf8_next_codepoint(&next, end, &following);
    // the resource supplements Unicode boundaries; its ASCII punctuation
    // tailoring must not depend on unrelated Unicode elsewhere in the node.
    if (current < 0x80 && following < 0x80) return false;
    // keep-all suppresses letter boundaries involving CJK, including Hangul
    // jamo and mixed Latin/CJK runs; other scripts retain dictionary breaks.
    bool cjk_edge = has_id_line_break_class(current) || utf_is_hangul(current) ||
        has_id_line_break_class(following) || utf_is_hangul(following);
    return !keep_all || !cjk_edge || !is_typographic_letter_unit(current) ||
        !is_typographic_letter_unit(following);
}


bool layout_text_allows_ascii_opening_break(uint32_t current, uint32_t next) {
    if (next != '(' && next != '<' && next != '[' && next != '{') return false;
    // CSS Text 3 §5.5 permits punctuation tailoring: these ASCII separators precede an opening unit.
    // letters, numbers, word prefixes and opening units retain their adjoining word.
    return current >= 0x21 && current <= 0x7E &&
        strchr("!\"#%&)*+,-.:;=>?\\]|}~", current) != nullptr;
}

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

bool text_codepoint_is_extended_pictographic(uint32_t codepoint) {
    return utf8proc_get_property((utf8proc_int32_t)codepoint)->boundclass ==
        UTF8PROC_BOUNDCLASS_EXTENDED_PICTOGRAPHIC;
}

/**
 * Check if a codepoint has the UAX #14 BA (Break After) class.
 * BA creates an ordinary opportunity after the character; unlike break-all,
 * it remains active for punctuation and non-letter word separators.
 */
bool is_line_break_ba(uint32_t cp) {
    static const uint32_t ranges[][2] = {
        {0x0009, 0x0009}, {0x007C, 0x007C}, {0x00AD, 0x00AD},
        {0x058A, 0x058A}, {0x05BE, 0x05BE}, {0x0964, 0x0965},
        {0x0E5A, 0x0E5B}, {0x0F0B, 0x0F0B}, {0x0F34, 0x0F34},
        {0x0F7F, 0x0F7F}, {0x0F85, 0x0F85}, {0x0FBE, 0x0FBF},
        {0x0FD2, 0x0FD2}, {0x104A, 0x104B}, {0x1361, 0x1361},
        {0x1400, 0x1400}, {0x1680, 0x1680}, {0x16EB, 0x16ED},
        {0x1735, 0x1736}, {0x17D4, 0x17D5}, {0x17D8, 0x17D8},
        {0x17DA, 0x17DA}, {0x1804, 0x1805}, {0x1B5A, 0x1B5B},
        {0x1B5D, 0x1B60}, {0x1C3B, 0x1C3F}, {0x1C7E, 0x1C7F},
        {0x2000, 0x2006}, {0x2008, 0x200A}, {0x2010, 0x2010},
        {0x2012, 0x2013}, {0x2027, 0x2027}, {0x2056, 0x2056},
        {0x2058, 0x205B}, {0x205D, 0x205F}, {0x2CFA, 0x2CFC},
        {0x2CFF, 0x2CFF}, {0x2D70, 0x2D70}, {0x2E0E, 0x2E15},
        {0x2E17, 0x2E17}, {0x2E19, 0x2E19}, {0x2E2A, 0x2E2D},
        {0x2E30, 0x2E31}, {0x2E33, 0x2E34}, {0x2E3C, 0x2E3E},
        {0x2E40, 0x2E41}, {0x2E43, 0x2E4A}, {0x2E4C, 0x2E4C},
        {0x2E4E, 0x2E4F}, {0x3000, 0x3000}, {0xA4FE, 0xA4FF},
        {0xA60D, 0xA60D}, {0xA60F, 0xA60F}, {0xA6F3, 0xA6F7},
        {0xA8CE, 0xA8CF}, {0xA92E, 0xA92F}, {0xA9C7, 0xA9C9},
        {0xAA5D, 0xAA5F}, {0xAAF0, 0xAAF1}, {0xABEB, 0xABEB},
        {0x10100, 0x10102}, {0x1039F, 0x1039F}, {0x103D0, 0x103D0},
        {0x10857, 0x10857}, {0x1091F, 0x1091F}, {0x10A50, 0x10A57},
        {0x10AF0, 0x10AF5}, {0x10B39, 0x10B3F}, {0x10EAD, 0x10EAD},
        {0x11047, 0x11048}, {0x110BE, 0x110C1}, {0x11140, 0x11143},
        {0x111C5, 0x111C6}, {0x111C8, 0x111C8}, {0x111DD, 0x111DF},
        {0x11238, 0x11239}, {0x1123B, 0x1123C}, {0x112A9, 0x112A9},
        {0x1144B, 0x1144E}, {0x1145A, 0x1145B}, {0x115C2, 0x115C3},
        {0x115C9, 0x115D7}, {0x11641, 0x11642}, {0x1173C, 0x1173E},
        {0x11944, 0x11946}, {0x11A41, 0x11A44}, {0x11A9A, 0x11A9C},
        {0x11AA1, 0x11AA2}, {0x11C41, 0x11C45}, {0x11FFF, 0x11FFF},
        {0x12470, 0x12474}, {0x16A6E, 0x16A6F}, {0x16AF5, 0x16AF5},
        {0x16B37, 0x16B39}, {0x16B44, 0x16B44}, {0x16E97, 0x16E98},
        {0x1BC9F, 0x1BC9F}, {0x1DA87, 0x1DA8A}
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        if (cp >= ranges[i][0] && cp <= ranges[i][1]) return true;
    }
    return false;
}


// shared segmentation keeps paint independent of the streaming line breaker.
bool layout_get_grapheme_cluster_bytes(const unsigned char* cluster_start,
                                       const unsigned char* text_end,
                                       size_t* cluster_bytes) {
    if (!cluster_start || !text_end || cluster_start >= text_end ||
        !cluster_bytes) {
        return false;
    }
    uint32_t previous = 0;
    int first_bytes = str_utf8_decode(
        (const char*)cluster_start, (size_t)(text_end - cluster_start), &previous);
    if (first_bytes <= 0) return false;

    const unsigned char* cursor = cluster_start + first_bytes;
    bool has_extension = false;
    utf8proc_int32_t grapheme_state = 0;
    while (cursor < text_end) {
        const unsigned char* current_start = cursor;
        uint32_t current = 0;
        if (!layout_utf8_next_codepoint(&cursor, text_end, &current) ||
            utf8proc_grapheme_break_stateful(
                (utf8proc_int32_t)previous, (utf8proc_int32_t)current, &grapheme_state) ||
            is_line_break_ba(previous)) {
            // The next layout unit begins here. Keeping it in this cluster
            // hides a prior UAX #14 break opportunity from the line breaker.
            cursor = current_start;
            break;
        }
        has_extension = true;
        previous = current;
    }
    if (!has_extension) return false;

    *cluster_bytes = (size_t)(cursor - cluster_start);
    return true;
}

bool text_get_grapheme_cluster(const unsigned char* start, const unsigned char* end,
        TextGraphemeCluster* cluster) {
    if (!cluster) return false;
    size_t bytes = 0;
    if (!layout_get_grapheme_cluster_bytes(start, end, &bytes)) return false;
    const unsigned char* cursor = start;
    uint32_t first = 0;
    if (!layout_utf8_next_codepoint(&cursor, start + bytes, &first)) return false;
    bool emoji = text_codepoint_is_extended_pictographic(first) ||
        utf_is_emoji_presentation_default(first);
    bool color = utf_is_emoji_presentation_default(first), text = false;
    while (cursor < start + bytes) {
        uint32_t cp = 0;
        if (!layout_utf8_next_codepoint(&cursor, start + bytes, &cp)) return false;
        if (cp == 0xFE0E) text = true;
        if (cp == 0xFE0F || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF)) color = true;
        // keycap bases are Emoji characters outside Extended_Pictographic (UTS #51).
        if (cp == 0x20E3 && (first == '#' || first == '*' || (first >= '0' && first <= '9'))) {
            emoji = color = true;
        }
    }
    *cluster = {bytes, first, emoji, color && !text};
    return true;
}

bool text_get_emoji_cluster(const unsigned char* start, const unsigned char* end,
        TextGraphemeCluster* cluster) {
    return text_get_grapheme_cluster(start, end, cluster) && cluster->emoji;
}

bool text_justify_cluster_gap(TextJustifyContext* context, const unsigned char* start,
        const unsigned char* end, CssEnum mode) {
    bool first = true, gap = false;
    uint32_t cp = 0;
    // retain RI parity and ZWJ state for the next unit even when this cluster paints once.
    while (layout_utf8_next_codepoint(&start, end, &cp)) {
        bool boundary = text_justify_character_gap(context, cp, mode);
        if (first) gap = boundary;
        first = false;
    }
    return gap;
}

bool text_emphasis_marks_codepoint(uint32_t codepoint) {
    if (!codepoint || text_codepoint_has_zero_advance(codepoint)) return false;
    utf8proc_category_t cat = utf8proc_category((utf8proc_int32_t)codepoint);
    if (cat == UTF8PROC_CATEGORY_ZS || cat == UTF8PROC_CATEGORY_ZL ||
        cat == UTF8PROC_CATEGORY_ZP || cat == UTF8PROC_CATEGORY_CC ||
        cat == UTF8PROC_CATEGORY_CF || cat == UTF8PROC_CATEGORY_CN) return false;
    if (cat >= UTF8PROC_CATEGORY_PC && cat <= UTF8PROC_CATEGORY_PO) {
        // CSS Text Decoration 3 retains marks on these punctuation symbols.
        switch (codepoint) {
            case '#': case '%': case '&': case '@':
            case 0x0609: case 0x060A: case 0x066A:
            case 0x00A7: case 0x00B6: case 0x2030: case 0x2031:
            case 0x204A: case 0x204B: case 0x2053: case 0x303D:
                return true;
            default: return false;
        }
    }
    return true;
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

bool text_unicode_space_has_explicit_advance(uint32_t codepoint) {
    return text_unicode_space_width_em(codepoint) > 0.0f ||
        codepoint == 0x202F || codepoint == 0x2008;
}
