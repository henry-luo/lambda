// tex_font_adapter.cpp - Font Adapter Implementations
//
// Implements the FontProvider adapters for TFM and FreeType fonts.

#include "tex_font_adapter.hpp"
#include "lib/log.h"
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// CM to Unicode Mapping Tables
// ============================================================================

// CMMI (Math Italic) character mapping
// Greek letters start at position 11 in cmmi
static const int32_t CMMI_TO_UNICODE[] = {
    // 0-10: Special symbols
    0x0393,  // 0: Gamma
    0x0394,  // 1: Delta
    0x0398,  // 2: Theta
    0x039B,  // 3: Lambda
    0x039E,  // 4: Xi
    0x03A0,  // 5: Pi
    0x03A3,  // 6: Sigma
    0x03A5,  // 7: Upsilon
    0x03A6,  // 8: Phi
    0x03A8,  // 9: Psi
    0x03A9,  // 10: Omega
    // 11-33: Lowercase Greek
    0x03B1,  // 11: alpha
    0x03B2,  // 12: beta
    0x03B3,  // 13: gamma
    0x03B4,  // 14: delta
    0x03B5,  // 15: epsilon (varepsilon)
    0x03B6,  // 16: zeta
    0x03B7,  // 17: eta
    0x03B8,  // 18: theta
    0x03B9,  // 19: iota
    0x03BA,  // 20: kappa
    0x03BB,  // 21: lambda
    0x03BC,  // 22: mu
    0x03BD,  // 23: nu
    0x03BE,  // 24: xi
    0x03C0,  // 25: pi
    0x03C1,  // 26: rho
    0x03C3,  // 27: sigma
    0x03C4,  // 28: tau
    0x03C5,  // 29: upsilon
    0x03C6,  // 30: phi (varphi)
    0x03C7,  // 31: chi
    0x03C8,  // 32: psi
    0x03C9,  // 33: omega
    // 34-37: variant Greek
    0x03F5,  // 34: lunate epsilon
    0x03D1,  // 35: vartheta
    0x03D6,  // 36: varpi
    0x03F1,  // 37: varrho
    0x03C2,  // 38: varsigma
    0x03D5,  // 39: straightphi
    // 40-47: additional symbols
    0x21BC,  // 40: leftharpoonup
    0x21BD,  // 41: leftharpoondown
    0x21C0,  // 42: rightharpoonup
    0x21C1,  // 43: rightharpoondown
    0x0060,  // 44: grave
    0x00B4,  // 45: acute
    0x02C7,  // 46: caron
    0x02D8,  // 47: breve
    // 48-57: digits in italic
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    // 58-63: punctuation
    '.', ',', '<', '/', '>', '*',
    // 64: partial derivative
    0x2202,
    // 65-90: uppercase letters (italic)
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    // 91-96: various
    0x266D,  // 91: flat
    0x266E,  // 92: natural
    0x266F,  // 93: sharp
    0x2323,  // 94: smile
    0x2322,  // 95: frown
    0x2113,  // 96: ell
    // 97-122: lowercase letters (italic)
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    // 123-127: additional
    0x0131,  // 123: dotless i
    0x0237,  // 124: dotless j
    0x2118,  // 125: weierstrass p
    0x20D7,  // 126: vector arrow
    0x0302,  // 127: combining circumflex
};

// CMSY (Math Symbols) character mapping
static const int32_t CMSY_TO_UNICODE[] = {
    0x2212,  // 0: minus
    0x22C5,  // 1: cdot
    0x00D7,  // 2: times
    0x2217,  // 3: asterisk
    0x00F7,  // 4: div
    0x22C4,  // 5: diamond
    0x00B1,  // 6: pm
    0x2213,  // 7: mp
    0x2295,  // 8: oplus
    0x2296,  // 9: ominus
    0x2297,  // 10: otimes
    0x2298,  // 11: oslash
    0x2299,  // 12: odot
    0x25CB,  // 13: bigcirc
    0x2218,  // 14: circ
    0x2219,  // 15: bullet
    0x224D,  // 16: asymp
    0x2261,  // 17: equiv
    0x2286,  // 18: subseteq
    0x2287,  // 19: supseteq
    0x2264,  // 20: leq
    0x2265,  // 21: geq
    0x227C,  // 22: preceq
    0x227D,  // 23: succeq
    0x223C,  // 24: sim
    0x2248,  // 25: approx
    0x2282,  // 26: subset
    0x2283,  // 27: supset
    0x226A,  // 28: ll
    0x226B,  // 29: gg
    0x227A,  // 30: prec
    0x227B,  // 31: succ
    0x2190,  // 32: leftarrow
    0x2192,  // 33: rightarrow
    0x2191,  // 34: uparrow
    0x2193,  // 35: downarrow
    0x2194,  // 36: leftrightarrow
    0x2197,  // 37: nearrow
    0x2198,  // 38: searrow
    0x2243,  // 39: simeq
    0x21D0,  // 40: Leftarrow
    0x21D2,  // 41: Rightarrow
    0x21D1,  // 42: Uparrow
    0x21D3,  // 43: Downarrow
    0x21D4,  // 44: Leftrightarrow
    0x2196,  // 45: nwarrow
    0x2199,  // 46: swarrow
    0x221D,  // 47: propto
    0x2032,  // 48: prime
    0x221E,  // 49: infty
    0x2208,  // 50: in
    0x220B,  // 51: ni
    0x25B3,  // 52: bigtriangleup
    0x25BD,  // 53: bigtriangledown
    0x0338,  // 54: not (combining)
    0x2021,  // 55: dagger (double)
    0x21A6,  // 56: mapsto
    0x2020,  // 57: dagger
    0x2022,  // 58: bullet
    0x2026,  // 59: ldots
    0x22EF,  // 60: cdots
    0x22EE,  // 61: vdots
    0x22F1,  // 62: ddots
    0x266D,  // 63: flat (alt)
    // 64-79: various operators
    0x2135,  // 64: aleph
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    // 80-95: calligraphic letters
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    0x222A,  // 91: cup
    0x2229,  // 92: cap
    0x228E,  // 93: uplus
    0x2227,  // 94: wedge (land)
    0x2228,  // 95: vee (lor)
    // 96-127: more symbols
    0x22A2,  // 96: vdash
    0x22A3,  // 97: dashv
    0x230A,  // 98: lfloor
    0x230B,  // 99: rfloor
    0x2308,  // 100: lceil
    0x2309,  // 101: rceil
    '{',     // 102: lbrace
    '}',     // 103: rbrace
    0x27E8,  // 104: langle
    0x27E9,  // 105: rangle
    '|',     // 106: vert
    0x2016,  // 107: Vert (double)
    0x2195,  // 108: updownarrow
    0x21D5,  // 109: Updownarrow
    '\\',    // 110: backslash
    0x2240,  // 111: wr (wreath)
    0x221A,  // 112: surd
    0x2A3F,  // 113: amalg
    0x2207,  // 114: nabla
    0x222B,  // 115: int
    0x2294,  // 116: sqcup
    0x2293,  // 117: sqcap
    0x2291,  // 118: sqsubseteq
    0x2292,  // 119: sqsupseteq
    0x00A7,  // 120: S (section)
    0x2020,  // 121: dagger
    0x2021,  // 122: ddagger
    0x00B6,  // 123: P (paragraph)
    0x2663,  // 124: clubsuit
    0x2662,  // 125: diamondsuit
    0x2661,  // 126: heartsuit
    0x2660,  // 127: spadesuit
};

// CMEX (Math Extensions) character mapping
// Large delimiters and operators
static const int32_t CMEX_TO_UNICODE[] = {
    '(',     // 0: left paren (small)
    ')',     // 1: right paren (small)
    '[',     // 2: left bracket (small)
    ']',     // 3: right bracket (small)
    0x230A,  // 4: lfloor (small)
    0x230B,  // 5: rfloor (small)
    0x2308,  // 6: lceil (small)
    0x2309,  // 7: rceil (small)
    '{',     // 8: lbrace (small)
    '}',     // 9: rbrace (small)
    0x27E8,  // 10: langle (small)
    0x27E9,  // 11: rangle (small)
    '|',     // 12: vert (small)
    0x2016,  // 13: Vert (small)
    '/',     // 14: slash
    '\\',    // 15: backslash
    // 16-47: larger versions
    '(',     // 16
    ')',     // 17
    '(',     // 18
    ')',     // 19
    '(',     // 20
    ')',     // 21
    '(',     // 22
    ')',     // 23
    '[',     // 24
    ']',     // 25
    '[',     // 26
    ']',     // 27
    '[',     // 28
    ']',     // 29
    '[',     // 30
    ']',     // 31
    '{',     // 32
    '}',     // 33
    '{',     // 34
    '}',     // 35
    '{',     // 36
    '}',     // 37
    '{',     // 38
    '}',     // 39
    0x27E8,  // 40: langle
    0x27E9,  // 41: rangle
    0x27E8,  // 42
    0x27E9,  // 43
    0x27E8,  // 44
    0x27E9,  // 45
    0x27E8,  // 46
    0x27E9,  // 47
    // 48-79: extensible pieces
    '(',     // 48: paren top
    ')',     // 49
    0x239B,  // 50: paren extension
    0x239E,  // 51
    0x239D,  // 52: paren bottom
    0x23A0,  // 53
    0x23A1,  // 54: bracket top
    0x23A4,  // 55
    0x23A2,  // 56: bracket extension
    0x23A5,  // 57
    0x23A3,  // 58: bracket bottom
    0x23A6,  // 59
    0x23A7,  // 60: brace top
    0x23AB,  // 61
    0x23A8,  // 62: brace middle
    0x23AC,  // 63
    0x23A9,  // 64: brace bottom
    0x23AD,  // 65
    0x23AA,  // 66: brace extension
    0x23AA,  // 67
    // 68-79: vertical bars
    '|',     // 68
    '|',     // 69
    0x2016,  // 70
    0x2016,  // 71
    '/',     // 72
    '\\',    // 73
    '/',     // 74
    '\\',    // 75
    '/',     // 76
    '\\',    // 77
    '/',     // 78
    '\\',    // 79
    // 80-95: big operators
    0x2211,  // 80: sum (small)
    0x220F,  // 81: prod
    0x222B,  // 82: int (small)
    0x22C3,  // 83: bigcup
    0x22C2,  // 84: bigcap
    0x2A04,  // 85: biguplus
    0x2227,  // 86: bigwedge
    0x2228,  // 87: bigvee
    0x2211,  // 88: sum (large)
    0x220F,  // 89: prod (large)
    0x222B,  // 90: int (large)
    0x22C3,  // 91: bigcup (large)
    0x22C2,  // 92: bigcap (large)
    0x2A04,  // 93: biguplus (large)
    0x2A01,  // 94: bigoplus
    0x2A02,  // 95: bigotimes
    // 96-127: more
    0x2A00,  // 96: bigodot
    0x222E,  // 97: oint
    0x2A06,  // 98: bigsqcup
    0x222B,  // 99: int
    0x222B,  // 100
    0x222B,  // 101
    0x222B,  // 102
    0x222B,  // 103
    0x2210,  // 104: coprod
    0x2210,  // 105
    0x0302,  // 106: hat
    0x0302,  // 107
    0x0302,  // 108
    0x0303,  // 109: tilde
    0x0303,  // 110
    0x0303,  // 111
    '[',     // 112
    ']',     // 113
    0x230A,  // 114
    0x230B,  // 115
    0x2308,  // 116
    0x2309,  // 117
    '{',     // 118
    '}',     // 119
    0x221A,  // 120: sqrt
    0x221A,  // 121
    0x221A,  // 122
    0x221A,  // 123
    0x221A,  // 124
    0x221A,  // 125
    0x23B7,  // 126: radical bottom
    0x2502,  // 127: radical vertical
};

// ============================================================================
// CMToUnicodeMap Implementation
// ============================================================================

int32_t CMToUnicodeMap::from_cmmi(int cm_char) {
    if (cm_char < 0) return cm_char;
    if (cm_char < 128 && cm_char < (int)(sizeof(CMMI_TO_UNICODE)/sizeof(CMMI_TO_UNICODE[0]))) {
        return CMMI_TO_UNICODE[cm_char];
    }
    return cm_char;  // Pass through for ASCII range
}

int32_t CMToUnicodeMap::from_cmsy(int cm_char) {
    if (cm_char < 0) return cm_char;
    if (cm_char < 128 && cm_char < (int)(sizeof(CMSY_TO_UNICODE)/sizeof(CMSY_TO_UNICODE[0]))) {
        return CMSY_TO_UNICODE[cm_char];
    }
    return cm_char;
}

int32_t CMToUnicodeMap::from_cmex(int cm_char) {
    if (cm_char < 0) return cm_char;
    if (cm_char < 128 && cm_char < (int)(sizeof(CMEX_TO_UNICODE)/sizeof(CMEX_TO_UNICODE[0]))) {
        return CMEX_TO_UNICODE[cm_char];
    }
    return cm_char;
}

int32_t CMToUnicodeMap::from_cmr(int cm_char) {
    // CMR is mostly ASCII-compatible
    // Handle special TeX characters
    switch (cm_char) {
        case 0: return 0x0393;   // Gamma (in some encodings)
        case 1: return 0x0394;   // Delta
        case 2: return 0x0398;   // Theta
        case 3: return 0x039B;   // Lambda
        case 4: return 0x039E;   // Xi
        case 5: return 0x03A0;   // Pi
        case 6: return 0x03A3;   // Sigma
        case 7: return 0x03A5;   // Upsilon
        case 8: return 0x03A6;   // Phi
        case 9: return 0x03A8;   // Psi
        case 10: return 0x03A9;  // Omega
        case 11: return 0xFB00;  // ff ligature
        case 12: return 0xFB01;  // fi ligature
        case 13: return 0xFB02;  // fl ligature
        case 14: return 0xFB03;  // ffi ligature
        case 15: return 0xFB04;  // ffl ligature
        case 16: return 0x0131;  // dotless i
        case 17: return 0x0237;  // dotless j
        case 18: return 0x0060;  // grave
        case 19: return 0x00B4;  // acute
        case 20: return 0x02C7;  // caron
        case 21: return 0x02D8;  // breve
        case 22: return 0x00AF;  // macron
        case 23: return 0x02DA;  // ring above
        case 24: return 0x00B8;  // cedilla
        case 25: return 0x00DF;  // eszett
        case 26: return 0x00E6;  // ae
        case 27: return 0x0153;  // oe
        case 28: return 0x00F8;  // o-slash
        case 29: return 0x00C6;  // AE
        case 30: return 0x0152;  // OE
        case 31: return 0x00D8;  // O-slash
        // 32-127: standard ASCII (mostly)
        case 34: return 0x201D;  // right double quote
        case 39: return 0x2019;  // right single quote
        case 60: return 0x00A1;  // inverted exclamation
        case 62: return 0x00BF;  // inverted question
        case 92: return 0x201C;  // left double quote
        case 123: return 0x2013; // en dash
        case 124: return 0x2014; // em dash
        case 125: return 0x02DD; // double acute
        case 126: return 0x0303; // tilde
        case 127: return 0x00A8; // diaeresis
        default:
            if (cm_char >= 32 && cm_char < 127) return cm_char;
            return cm_char;
    }
}

int32_t CMToUnicodeMap::map(int cm_char, const char* font_name) {
    if (!font_name) return cm_char;

    // Check font name prefix
    if (strncmp(font_name, "cmmi", 4) == 0) {
        return from_cmmi(cm_char);
    }
    if (strncmp(font_name, "cmsy", 4) == 0) {
        return from_cmsy(cm_char);
    }
    if (strncmp(font_name, "cmex", 4) == 0) {
        return from_cmex(cm_char);
    }
    if (strncmp(font_name, "cmr", 3) == 0 ||
        strncmp(font_name, "cmti", 4) == 0 ||
        strncmp(font_name, "cmbx", 4) == 0 ||
        strncmp(font_name, "cmtt", 4) == 0) {
        return from_cmr(cm_char);
    }

    // Default: pass through
    return cm_char;
}

// ============================================================================
// TFMFontProvider Implementation
// ============================================================================

TFMFontProvider::TFMFontProvider(TFMFontManager* manager, Arena* arena)
    : manager_(manager)
    , arena_(arena)
    , cache_(nullptr)
    , cache_count_(0)
    , cache_capacity_(0)
{
}

const char* TFMFontProvider::select_font_name(FontFamily family, bool bold, bool italic) {
    switch (family) {
        case FontFamily::Roman:
            if (bold && italic) return "cmbxti10";
            if (bold) return "cmbx10";
            if (italic) return "cmti10";
            return "cmr10";

        case FontFamily::Italic:
            return italic ? "cmmi10" : "cmmi10";

        case FontFamily::Symbol:
            return "cmsy10";

        case FontFamily::Extension:
            return "cmex10";
    }
    return "cmr10";
}

FontMetrics* TFMFontProvider::wrap_tfm_font(TFMFont* tfm, float size_pt) {
    if (!tfm) return nullptr;

    FontMetrics* fm = (FontMetrics*)arena_alloc(arena_, sizeof(FontMetrics));
    if (!fm) return nullptr;

    memset(fm, 0, sizeof(FontMetrics));
    fm->font_name = tfm->name;
    fm->design_size = tfm->design_size;
    fm->scale = size_pt / tfm->design_size;

    // Set font type based on parameters
    if (tfm->np >= 22) {
        fm->type = FontMetrics::Type::MathSymbol;
        // Copy math symbol parameters
        fm->params.symbol.slant = tfm->get_param(1);
        fm->params.symbol.x_height = tfm->get_param(5);
        fm->params.symbol.quad = tfm->get_param(6);
        fm->params.symbol.num1 = tfm->get_param(8);
        fm->params.symbol.num2 = tfm->get_param(9);
        fm->params.symbol.num3 = tfm->get_param(10);
        fm->params.symbol.denom1 = tfm->get_param(11);
        fm->params.symbol.denom2 = tfm->get_param(12);
        fm->params.symbol.sup1 = tfm->get_param(13);
        fm->params.symbol.sup2 = tfm->get_param(14);
        fm->params.symbol.sup3 = tfm->get_param(15);
        fm->params.symbol.sub1 = tfm->get_param(16);
        fm->params.symbol.sub2 = tfm->get_param(17);
        fm->params.symbol.sup_drop = tfm->get_param(18);
        fm->params.symbol.sub_drop = tfm->get_param(19);
        fm->params.symbol.delim1 = tfm->get_param(20);
        fm->params.symbol.delim2 = tfm->get_param(21);
        fm->params.symbol.axis_height = tfm->get_param(22);
    } else if (tfm->np >= 13 && strncmp(tfm->name, "cmex", 4) == 0) {
        fm->type = FontMetrics::Type::MathExtension;
        fm->params.extension.default_rule_thickness = tfm->get_param(8);
        fm->params.extension.big_op_spacing1 = tfm->get_param(9);
        fm->params.extension.big_op_spacing2 = tfm->get_param(10);
        fm->params.extension.big_op_spacing3 = tfm->get_param(11);
        fm->params.extension.big_op_spacing4 = tfm->get_param(12);
        fm->params.extension.big_op_spacing5 = tfm->get_param(13);
    } else {
        fm->type = FontMetrics::Type::Text;
        fm->params.text.slant = tfm->get_param(1);
        fm->params.text.interword_space = tfm->get_param(2);
        fm->params.text.interword_stretch = tfm->get_param(3);
        fm->params.text.interword_shrink = tfm->get_param(4);
        fm->params.text.x_height = tfm->get_param(5);
        fm->params.text.quad = tfm->get_param(6);
        fm->params.text.extra_space = tfm->get_param(7);
    }

    // Allocate and populate glyph metrics
    int glyph_count = tfm->last_char - tfm->first_char + 1;
    if (glyph_count > 0) {
        GlyphMetrics* glyphs = (GlyphMetrics*)arena_alloc(arena_, glyph_count * sizeof(GlyphMetrics));
        if (glyphs) {
            for (int i = 0; i < glyph_count; i++) {
                int c = tfm->first_char + i;
                glyphs[i].codepoint = c;
                glyphs[i].width = tfm->char_width(c);
                glyphs[i].height = tfm->char_height(c);
                glyphs[i].depth = tfm->char_depth(c);
                glyphs[i].italic_correction = tfm->char_italic(c);
            }
            fm->glyphs = glyphs;
            fm->glyph_count = glyph_count;
        }
    }

    return fm;
}

const FontMetrics* TFMFontProvider::get_font(
    FontFamily family,
    bool bold,
    bool italic,
    float size_pt
) {
    const char* name = select_font_name(family, bold, italic);
    TFMFont* tfm = manager_->get_font(name);
    return wrap_tfm_font(tfm, size_pt);
}

const FontMetrics* TFMFontProvider::get_math_symbol_font(float size_pt) {
    TFMFont* tfm = manager_->get_font("cmsy10");
    return wrap_tfm_font(tfm, size_pt);
}

const FontMetrics* TFMFontProvider::get_math_extension_font(float size_pt) {
    TFMFont* tfm = manager_->get_font("cmex10");
    return wrap_tfm_font(tfm, size_pt);
}

const FontMetrics* TFMFontProvider::get_math_text_font(float size_pt, bool italic) {
    TFMFont* tfm = manager_->get_font(italic ? "cmmi10" : "cmr10");
    return wrap_tfm_font(tfm, size_pt);
}

// ============================================================================
// FreeTypeFontProvider Implementation
// ============================================================================

FreeTypeFontProvider::FreeTypeFontProvider(FT_Library ft_lib, Arena* arena)
    : ft_lib_(ft_lib)
    , arena_(arena)
    , faces_(nullptr)
    , face_count_(0)
    , face_capacity_(0)
{
}

FreeTypeFontProvider::~FreeTypeFontProvider() {
    // Clean up loaded faces
    for (int i = 0; i < face_count_; i++) {
        if (faces_[i].face) {
            FT_Done_Face(faces_[i].face);
        }
    }
}

const char* FreeTypeFontProvider::map_family_to_font(FontFamily family, bool bold, bool italic) {
    // Map TeX font families to system fonts (CMU family)
    switch (family) {
        case FontFamily::Roman:
            if (bold && italic) return "CMU Serif BoldItalic";
            if (bold) return "CMU Serif Bold";
            if (italic) return "CMU Serif Italic";
            return "CMU Serif";

        case FontFamily::Italic:
            return "CMU Serif Italic";

        case FontFamily::Symbol:
            return "CMU Serif";  // Symbols are in the same font

        case FontFamily::Extension:
            return "CMU Serif";  // Extensions need special handling
    }
    return "CMU Serif";
}

FT_Face FreeTypeFontProvider::load_face(const char* font_name, float size_pt) {
    // Check cache first
    for (int i = 0; i < face_count_; i++) {
        if (strcmp(faces_[i].font_name, font_name) == 0 &&
            fabs(faces_[i].size_pt - size_pt) < 0.1f) {
            return faces_[i].face;
        }
    }

    // Load new face
    // Note: In practice, this would use FontConfig or platform font APIs
    // For now, return nullptr and rely on fallback
    log_debug("FreeTypeFontProvider: would load font '%s' at %.1fpt", font_name, size_pt);
    return nullptr;
}

FT_Face FreeTypeFontProvider::get_face(const char* font_name, float size_pt) {
    return load_face(font_name, size_pt);
}

FontMetrics* FreeTypeFontProvider::create_metrics_from_face(FT_Face face, const char* name, float size_pt) {
    if (!face) return nullptr;

    FontMetrics* fm = (FontMetrics*)arena_alloc(arena_, sizeof(FontMetrics));
    if (!fm) return nullptr;

    memset(fm, 0, sizeof(FontMetrics));
    fm->font_name = name;
    fm->design_size = size_pt;
    fm->scale = 1.0f;
    fm->type = FontMetrics::Type::Text;

    // Extract metrics from FreeType face
    float units_per_em = (float)face->units_per_EM;
    float scale_factor = size_pt / units_per_em;

    fm->params.text.x_height = face->height * scale_factor * 0.5f;  // Approximate
    fm->params.text.quad = size_pt;

    return fm;
}

const FontMetrics* FreeTypeFontProvider::get_font(
    FontFamily family,
    bool bold,
    bool italic,
    float size_pt
) {
    const char* name = map_family_to_font(family, bold, italic);
    FT_Face face = load_face(name, size_pt);
    return create_metrics_from_face(face, name, size_pt);
}

const FontMetrics* FreeTypeFontProvider::get_math_symbol_font(float size_pt) {
    return get_font(FontFamily::Symbol, false, false, size_pt);
}

const FontMetrics* FreeTypeFontProvider::get_math_extension_font(float size_pt) {
    return get_font(FontFamily::Extension, false, false, size_pt);
}

const FontMetrics* FreeTypeFontProvider::get_math_text_font(float size_pt, bool italic) {
    return get_font(FontFamily::Roman, false, italic, size_pt);
}

// ============================================================================
// DualFontProvider Implementation
// ============================================================================

DualFontProvider::DualFontProvider(
    TFMFontProvider* tfm_provider,
    FreeTypeFontProvider* ft_provider
)
    : tfm_(tfm_provider)
    , ft_(ft_provider)
{
}

const FontMetrics* DualFontProvider::get_font(
    FontFamily family,
    bool bold,
    bool italic,
    float size_pt
) {
    // Use TFM for metrics (accurate typesetting)
    return tfm_->get_font(family, bold, italic, size_pt);
}

const FontMetrics* DualFontProvider::get_math_symbol_font(float size_pt) {
    return tfm_->get_math_symbol_font(size_pt);
}

const FontMetrics* DualFontProvider::get_math_extension_font(float size_pt) {
    return tfm_->get_math_extension_font(size_pt);
}

const FontMetrics* DualFontProvider::get_math_text_font(float size_pt, bool italic) {
    return tfm_->get_math_text_font(size_pt, italic);
}

FT_Face DualFontProvider::get_render_face(const char* tfm_name, float size_pt) {
    // Map TFM name to system font
    const char* system_font = "CMU Serif";
    if (strncmp(tfm_name, "cmmi", 4) == 0) system_font = "CMU Serif Italic";
    else if (strncmp(tfm_name, "cmss", 4) == 0) system_font = "CMU Sans Serif";
    else if (strncmp(tfm_name, "cmtt", 4) == 0) system_font = "CMU Typewriter Text";
    else if (strncmp(tfm_name, "cmbx", 4) == 0) system_font = "CMU Serif Bold";

    return ft_->get_face(system_font, size_pt);
}

// ============================================================================
// Factory Functions
// ============================================================================

TFMFontProvider* create_tfm_provider(Arena* arena) {
    TFMFontManager* manager = create_font_manager(arena);
    if (!manager) return nullptr;

    TFMFontProvider* provider = (TFMFontProvider*)arena_alloc(arena, sizeof(TFMFontProvider));
    if (!provider) return nullptr;

    new (provider) TFMFontProvider(manager, arena);
    return provider;
}

FreeTypeFontProvider* create_freetype_provider(FT_Library ft_lib, Arena* arena) {
    FreeTypeFontProvider* provider = (FreeTypeFontProvider*)arena_alloc(arena, sizeof(FreeTypeFontProvider));
    if (!provider) return nullptr;

    new (provider) FreeTypeFontProvider(ft_lib, arena);
    return provider;
}

DualFontProvider* create_dual_provider(
    TFMFontProvider* tfm,
    FreeTypeFontProvider* ft
) {
    // Note: Caller must ensure the arena used for TFM provider is used here too
    // For simplicity, allocate on heap
    return new DualFontProvider(tfm, ft);
}

// ============================================================================
// Glyph Fallback Implementation
// ============================================================================

// Common fallback fonts in priority order
// These are typically available on most systems
const char* FALLBACK_FONT_NAMES[] = {
    "CMU Serif",           // Computer Modern Unicode (preferred for math)
    "STIX Two Math",       // STIX fonts for math
    "DejaVu Serif",        // DejaVu has good Unicode coverage
    "DejaVu Sans",
    "Noto Serif",          // Google Noto fonts
    "Noto Sans",
    "Noto Sans Symbols2",  // Mathematical symbols
    "Liberation Serif",    // Free alternative to Times
    "FreeSerif",           // GNU FreeFont
    "Symbola",             // Extensive Unicode symbol coverage
    "serif",               // System default serif
};

const int FALLBACK_FONT_COUNT = sizeof(FALLBACK_FONT_NAMES) / sizeof(FALLBACK_FONT_NAMES[0]);

GlyphFallback* GlyphFallback::create(FT_Library ft_lib, Arena* arena) {
    GlyphFallback* fb = (GlyphFallback*)arena_alloc(arena, sizeof(GlyphFallback));
    if (!fb) return nullptr;

    fb->ft_lib = ft_lib;
    fb->fallback_faces = nullptr;
    fb->fallback_count = 0;

    // Note: In a production implementation, we would pre-load fallback fonts
    // here. For now, we rely on the font subsystem to do lazy loading.
    // This avoids FontConfig dependencies in this adapter.

    log_debug("GlyphFallback: created with %d potential fallback fonts", FALLBACK_FONT_COUNT);
    return fb;
}

GlyphFallback::Result GlyphFallback::find_glyph(FT_Face primary, int32_t codepoint) {
    Result result = { nullptr, 0, false };

    // First try the primary font
    if (primary) {
        FT_UInt glyph_idx = FT_Get_Char_Index(primary, codepoint);
        if (glyph_idx != 0) {
            result.face = primary;
            result.glyph_index = glyph_idx;
            result.found = true;
            return result;
        }
    }

    // Try pre-loaded fallback faces
    for (int i = 0; i < fallback_count; i++) {
        if (fallback_faces[i]) {
            FT_UInt glyph_idx = FT_Get_Char_Index(fallback_faces[i], codepoint);
            if (glyph_idx != 0) {
                result.face = fallback_faces[i];
                result.glyph_index = glyph_idx;
                result.found = true;
                log_debug("GlyphFallback: found U+%04X in fallback font %d", codepoint, i);
                return result;
            }
        }
    }

    // Not found in any font
    log_debug("GlyphFallback: missing glyph U+%04X (no fallback found)", codepoint);
    return result;
}

GlyphFallback::Result GlyphFallback::find_cm_glyph(FT_Face primary, int cm_char, const char* font_name) {
    // First map CM character to Unicode
    int32_t unicode = CMToUnicodeMap::map(cm_char, font_name);

    // Then do fallback search
    return find_glyph(primary, unicode);
}

} // namespace tex
