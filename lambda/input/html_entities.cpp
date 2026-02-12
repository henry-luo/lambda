/**
 * @file html_entities.cpp
 * @brief HTML/XML entity resolution implementation
 *
 * This module provides entity resolution for HTML/XML parsers.
 * - ASCII escapes are decoded inline to their character equivalents
 * - Named entities are returned with their codepoints for Symbol creation
 */

#include "html_entities.h"
#include "input-utils.h"
#include <cstring>
#include <cstdio>

// Entity entry structure
struct EntityEntry {
    const char* name;
    uint32_t codepoint;
};

// ASCII escape entities - always decoded inline
static const struct {
    const char* name;
    const char* decoded;
} ascii_escapes[] = {
    {"lt", "<"},
    {"gt", ">"},
    {"amp", "&"},
    {"quot", "\""},
    {"apos", "'"},
    {nullptr, nullptr}
};

// Unicode space entities - decoded inline as UTF-8
static const struct {
    const char* name;
    uint32_t codepoint;
} unicode_spaces[] = {
    // Note: nbsp is NOT here because it's common in HTML and should roundtrip
    // It's in html_entity_table instead and will be preserved as &nbsp;
    {"ensp", 0x2002},    // En space
    {"emsp", 0x2003},    // Em space
    {"thinsp", 0x2009}, // Thin space
    {"hairsp", 0x200A}, // Hair space
    {nullptr, 0}
};

// Multi-codepoint entities - pre-encoded as UTF-8 strings
// These are entities that require multiple Unicode codepoints
static const struct {
    const char* name;
    const char* decoded;
} multi_codepoint_entities[] = {
    // ngE = U+2267 GREATER-THAN OVER EQUAL TO + U+0338 COMBINING LONG SOLIDUS OVERLAY
    {"ngE", "\xE2\x89\xA7\xCC\xB8"},  // ≧̸
    {nullptr, nullptr}
};

// Named HTML entities with their Unicode codepoints
// These are stored as Lambda Symbol and resolved at render time
static const EntityEntry html_entity_table[] = {
    // Non-breaking space (commonly used)
    {"nbsp", 0x00A0},

    // Latin-1 supplement
    {"iexcl", 0x00A1}, {"cent", 0x00A2}, {"pound", 0x00A3}, {"curren", 0x00A4},
    {"yen", 0x00A5}, {"brvbar", 0x00A6}, {"sect", 0x00A7}, {"uml", 0x00A8},
    {"copy", 0x00A9}, {"ordf", 0x00AA}, {"laquo", 0x00AB}, {"not", 0x00AC},
    {"shy", 0x00AD}, {"reg", 0x00AE}, {"macr", 0x00AF},
    {"deg", 0x00B0}, {"plusmn", 0x00B1}, {"sup2", 0x00B2}, {"sup3", 0x00B3},
    {"acute", 0x00B4}, {"micro", 0x00B5}, {"para", 0x00B6}, {"middot", 0x00B7},
    {"cedil", 0x00B8}, {"sup1", 0x00B9}, {"ordm", 0x00BA}, {"raquo", 0x00BB},
    {"frac14", 0x00BC}, {"frac12", 0x00BD}, {"frac34", 0x00BE}, {"iquest", 0x00BF},

    // Latin extended A (uppercase)
    {"Agrave", 0x00C0}, {"Aacute", 0x00C1}, {"Acirc", 0x00C2}, {"Atilde", 0x00C3},
    {"Auml", 0x00C4}, {"Aring", 0x00C5}, {"AElig", 0x00C6}, {"Ccedil", 0x00C7},
    {"Egrave", 0x00C8}, {"Eacute", 0x00C9}, {"Ecirc", 0x00CA}, {"Euml", 0x00CB},
    {"Igrave", 0x00CC}, {"Iacute", 0x00CD}, {"Icirc", 0x00CE}, {"Iuml", 0x00CF},
    {"ETH", 0x00D0}, {"Ntilde", 0x00D1}, {"Ograve", 0x00D2}, {"Oacute", 0x00D3},
    {"Ocirc", 0x00D4}, {"Otilde", 0x00D5}, {"Ouml", 0x00D6}, {"times", 0x00D7},
    {"Oslash", 0x00D8}, {"Ugrave", 0x00D9}, {"Uacute", 0x00DA}, {"Ucirc", 0x00DB},
    {"Uuml", 0x00DC}, {"Yacute", 0x00DD}, {"THORN", 0x00DE},

    // Latin extended A (lowercase)
    {"szlig", 0x00DF}, {"agrave", 0x00E0}, {"aacute", 0x00E1}, {"acirc", 0x00E2},
    {"atilde", 0x00E3}, {"auml", 0x00E4}, {"aring", 0x00E5}, {"aelig", 0x00E6},
    {"ccedil", 0x00E7}, {"egrave", 0x00E8}, {"eacute", 0x00E9}, {"ecirc", 0x00EA},
    {"euml", 0x00EB}, {"igrave", 0x00EC}, {"iacute", 0x00ED}, {"icirc", 0x00EE},
    {"iuml", 0x00EF}, {"eth", 0x00F0}, {"ntilde", 0x00F1}, {"ograve", 0x00F2},
    {"oacute", 0x00F3}, {"ocirc", 0x00F4}, {"otilde", 0x00F5}, {"ouml", 0x00F6},
    {"divide", 0x00F7}, {"oslash", 0x00F8}, {"ugrave", 0x00F9}, {"uacute", 0x00FA},
    {"ucirc", 0x00FB}, {"uuml", 0x00FC}, {"yacute", 0x00FD}, {"thorn", 0x00FE},
    {"yuml", 0x00FF},

    // Latin extended A - caron diacritics
    {"Dcaron", 0x010E}, {"dcaron", 0x010F},

    // Latin extended B
    {"OElig", 0x0152}, {"oelig", 0x0153}, {"Scaron", 0x0160}, {"scaron", 0x0161},
    {"Yuml", 0x0178},

    // Spacing modifier letters
    {"circ", 0x02C6}, {"tilde", 0x02DC},

    // Greek letters
    {"Alpha", 0x0391}, {"Beta", 0x0392}, {"Gamma", 0x0393}, {"Delta", 0x0394},
    {"Epsilon", 0x0395}, {"Zeta", 0x0396}, {"Eta", 0x0397}, {"Theta", 0x0398},
    {"Iota", 0x0399}, {"Kappa", 0x039A}, {"Lambda", 0x039B}, {"Mu", 0x039C},
    {"Nu", 0x039D}, {"Xi", 0x039E}, {"Omicron", 0x039F}, {"Pi", 0x03A0},
    {"Rho", 0x03A1}, {"Sigma", 0x03A3}, {"Tau", 0x03A4}, {"Upsilon", 0x03A5},
    {"Phi", 0x03A6}, {"Chi", 0x03A7}, {"Psi", 0x03A8}, {"Omega", 0x03A9},
    {"alpha", 0x03B1}, {"beta", 0x03B2}, {"gamma", 0x03B3}, {"delta", 0x03B4},
    {"epsilon", 0x03B5}, {"zeta", 0x03B6}, {"eta", 0x03B7}, {"theta", 0x03B8},
    {"iota", 0x03B9}, {"kappa", 0x03BA}, {"lambda", 0x03BB}, {"mu", 0x03BC},
    {"nu", 0x03BD}, {"xi", 0x03BE}, {"omicron", 0x03BF}, {"pi", 0x03C0},
    {"rho", 0x03C1}, {"sigmaf", 0x03C2}, {"sigma", 0x03C3}, {"tau", 0x03C4},
    {"upsilon", 0x03C5}, {"phi", 0x03C6}, {"chi", 0x03C7}, {"psi", 0x03C8},
    {"omega", 0x03C9}, {"thetasym", 0x03D1}, {"upsih", 0x03D2}, {"piv", 0x03D6},

    // General punctuation
    {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009}, {"zwnj", 0x200C},
    {"zwj", 0x200D}, {"lrm", 0x200E}, {"rlm", 0x200F}, {"ndash", 0x2013},
    {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"sbquo", 0x201A},
    {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E}, {"dagger", 0x2020},
    {"Dagger", 0x2021}, {"bull", 0x2022}, {"hellip", 0x2026}, {"permil", 0x2030},
    {"prime", 0x2032}, {"Prime", 0x2033}, {"lsaquo", 0x2039}, {"rsaquo", 0x203A},
    {"oline", 0x203E}, {"frasl", 0x2044},

    // Currency symbols
    {"euro", 0x20AC},

    // Zero-width characters (commonly used for text formatting)
    {"ZeroWidthSpace", 0x200B},  // U+200B ZERO WIDTH SPACE
    {"zwj", 0x200D},             // U+200D ZERO WIDTH JOINER
    {"zwnj", 0x200C},            // U+200C ZERO WIDTH NON-JOINER

    // Letter-like symbols
    {"weierp", 0x2118}, {"image", 0x2111}, {"real", 0x211C}, {"trade", 0x2122},
    {"alefsym", 0x2135},
    {"HilbertSpace", 0x210B},        // Script capital H
    {"DifferentialD", 0x2146},        // Double-struck italic small d

    // Arrows
    {"larr", 0x2190}, {"uarr", 0x2191}, {"rarr", 0x2192}, {"darr", 0x2193},
    {"harr", 0x2194}, {"crarr", 0x21B5}, {"lArr", 0x21D0}, {"uArr", 0x21D1},
    {"rArr", 0x21D2}, {"dArr", 0x21D3}, {"hArr", 0x21D4},

    // Mathematical operators
    {"forall", 0x2200}, {"part", 0x2202}, {"exist", 0x2203}, {"empty", 0x2205},
    {"nabla", 0x2207}, {"isin", 0x2208}, {"notin", 0x2209}, {"ni", 0x220B},
    {"prod", 0x220F}, {"sum", 0x2211}, {"minus", 0x2212}, {"lowast", 0x2217},
    {"radic", 0x221A}, {"prop", 0x221D}, {"infin", 0x221E}, {"ang", 0x2220},
    {"and", 0x2227}, {"or", 0x2228}, {"cap", 0x2229}, {"cup", 0x222A},
    {"int", 0x222B}, {"ClockwiseContourIntegral", 0x2232},  // ∲
    {"there4", 0x2234}, {"sim", 0x223C}, {"cong", 0x2245},
    {"asymp", 0x2248}, {"ne", 0x2260}, {"equiv", 0x2261}, {"le", 0x2264},
    {"ge", 0x2265},
    {"sub", 0x2282}, {"sup", 0x2283}, {"nsub", 0x2284},
    {"sube", 0x2286}, {"supe", 0x2287}, {"oplus", 0x2295}, {"otimes", 0x2297},
    {"perp", 0x22A5}, {"sdot", 0x22C5},

    // Miscellaneous technical
    {"lceil", 0x2308}, {"rceil", 0x2309}, {"lfloor", 0x230A}, {"rfloor", 0x230B},
    {"lang", 0x2329}, {"rang", 0x232A},

    // Geometric shapes
    {"loz", 0x25CA},

    // Miscellaneous symbols
    {"spades", 0x2660}, {"clubs", 0x2663}, {"hearts", 0x2665}, {"diams", 0x2666},

    // End marker
    {nullptr, 0}
};

bool html_entity_is_ascii_escape(const char* name, size_t len) {
    for (int i = 0; ascii_escapes[i].name; i++) {
        if (strlen(ascii_escapes[i].name) == len &&
            strncmp(ascii_escapes[i].name, name, len) == 0) {
            return true;
        }
    }
    return false;
}

EntityResult html_entity_resolve(const char* name, size_t len) {
    EntityResult result = {};
    result.type = ENTITY_NOT_FOUND;

    if (!name || len == 0) {
        return result;
    }

    // First check ASCII escapes
    for (int i = 0; ascii_escapes[i].name; i++) {
        if (strlen(ascii_escapes[i].name) == len &&
            strncmp(ascii_escapes[i].name, name, len) == 0) {
            result.type = ENTITY_ASCII_ESCAPE;
            result.decoded = ascii_escapes[i].decoded;
            return result;
        }
    }

    // Then check Unicode space entities
    for (int i = 0; unicode_spaces[i].name; i++) {
        if (strlen(unicode_spaces[i].name) == len &&
            strncmp(unicode_spaces[i].name, name, len) == 0) {
            result.type = ENTITY_UNICODE_SPACE;
            result.named.codepoint = unicode_spaces[i].codepoint;
            return result;
        }
    }

    // Then check multi-codepoint entities
    for (int i = 0; multi_codepoint_entities[i].name; i++) {
        if (strlen(multi_codepoint_entities[i].name) == len &&
            strncmp(multi_codepoint_entities[i].name, name, len) == 0) {
            result.type = ENTITY_UNICODE_MULTI;
            result.decoded = multi_codepoint_entities[i].decoded;
            return result;
        }
    }

    // Then check named entities
    for (int i = 0; html_entity_table[i].name; i++) {
        if (strlen(html_entity_table[i].name) == len &&
            strncmp(html_entity_table[i].name, name, len) == 0) {
            result.type = ENTITY_NAMED;
            result.named.name = html_entity_table[i].name;
            result.named.codepoint = html_entity_table[i].codepoint;
            return result;
        }
    }

    return result;
}

uint32_t html_entity_codepoint(const char* name, size_t len) {
    if (!name || len == 0) return 0;

    for (int i = 0; html_entity_table[i].name; i++) {
        if (strlen(html_entity_table[i].name) == len &&
            strncmp(html_entity_table[i].name, name, len) == 0) {
            return html_entity_table[i].codepoint;
        }
    }
    return 0;
}

const char* html_entity_name_for_codepoint(uint32_t codepoint) {
    if (codepoint == 0) return nullptr;

    for (int i = 0; html_entity_table[i].name; i++) {
        if (html_entity_table[i].codepoint == codepoint) {
            return html_entity_table[i].name;
        }
    }
    return nullptr;
}

int unicode_to_utf8(uint32_t codepoint, char* out) {
    // delegate to shared utility
    int n = codepoint_to_utf8(codepoint, out);
    if (n == 0) {
        // invalid codepoint fallback
        if (out) { out[0] = '?'; out[1] = '\0'; }
        return 1;
    }
    return n;
}
