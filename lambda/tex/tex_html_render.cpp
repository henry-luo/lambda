// tex_html_render.cpp - HTML output for TeX math formulas
//
// Converts TexNode trees to HTML+CSS markup compatible with MathLive styling.
// Uses MathLive's vlist-based layout with table positioning for exact match.
//
// Reference: MathLive v-box.ts makeRows() implementation
//
// VList Structure (MathLive-compatible):
//   ML__vlist-t [ML__vlist-t2]  (inline-table)
//   ├── ML__vlist-r             (table-row)
//   │   └── ML__vlist           (table-cell, height:Xem)
//   │       └── span            (top:-Yem, position:relative)
//   │           ├── ML__pstrut  (height strut for baseline)
//   │           └── content     (inline-block)
//   │       └── span...
//   │   └── ML__vlist-s         (Safari workaround, zero-width space)
//   └── ML__vlist-r             (second row for depth strut)
//       └── ML__vlist           (height:Dem for depth)

#include "tex_html_render.hpp"
#include "tex_node.hpp"
#include "tex_glue.hpp"
#include "lib/log.h"
#include "lib/strbuf.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace tex {

// forward declarations
static void render_node(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_hlist(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_vlist(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_char(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts);
static void render_rule(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts);
static void render_kern(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts);
static void render_glue(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts);
static void render_fraction(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_radical(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_scripts(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_delimiter(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_mathop(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_accent(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_mtable(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_mtable_column(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth, uint32_t hlines, bool trailing_hline);

// ============================================================================
// VList Helper Structures and Functions (MathLive-compatible)
// ============================================================================

// a single element in a vlist stack
struct VListElement {
    TexNode* node;          // the content node
    float shift;            // vertical shift (positive = down)
    float height;           // element height in em
    float depth;            // element depth in em
    const char* classes;    // additional CSS classes (e.g., "ML__center")
};

// calculate pstrut size from a list of elements
// pstrut must be taller than any element to ensure proper baseline alignment
static float calculate_pstrut_size(VListElement* elements, int count, float font_size_px) {
    float max_height = 0.0f;
    for (int i = 0; i < count; i++) {
        if (elements[i].node) {
            float h = elements[i].node->height / font_size_px;
            if (h > max_height) max_height = h;
        }
    }
    // add 2em buffer like MathLive does
    return max_height + 2.0f;
}

// render a MathLive-compatible vlist structure
// elements: array of VListElement, ordered bottom to top
// count: number of elements
// min_pos: minimum position (depth below baseline)
// max_pos: maximum position (height above baseline)
static void render_vlist_structure(StrBuf* out, const HtmlRenderOptions& opts,
                                   VListElement* elements, int count,
                                   float pstrut_size, float height_em, float depth_em,
                                   int render_depth,
                                   void (*render_content)(TexNode*, StrBuf*, const HtmlRenderOptions&, int)) {
    char buf[256];

    // determine if we need a two-row table (when there's depth below baseline)
    bool has_depth = depth_em > 0.01f;

    // outer wrapper: ML__vlist-t [ML__vlist-t2]
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-t");
    if (has_depth) {
        strbuf_append_str(out, " ");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-t2");
    }
    strbuf_append_str(out, "\">");

    // first row: ML__vlist-r
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-r\">");

    // vlist cell: ML__vlist with height
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist\" style=\"height:");
    snprintf(buf, sizeof(buf), "%.2fem\">", height_em);
    strbuf_append_str(out, buf);

    // render each element with positioning
    for (int i = 0; i < count; i++) {
        VListElement* elem = &elements[i];
        if (!elem->node) continue;

        // calculate top position: top = -pstrut_size - currPos - elem->depth
        float top_em = -pstrut_size + elem->shift;

        // element wrapper with position
        strbuf_append_str(out, "<span");
        if (elem->classes) {
            strbuf_append_str(out, " class=\"");
            strbuf_append_str(out, elem->classes);
            strbuf_append_str(out, "\"");
        }
        // Build style with position and optional hline
        strbuf_append_str(out, " style=\"top:");
        snprintf(buf, sizeof(buf), "%.2fem", top_em);
        strbuf_append_str(out, buf);
        if (elem->classes) {
            if (strcmp(elem->classes, "hline") == 0) {
                strbuf_append_str(out, ";border-top:0.5px solid currentColor");
            } else if (strcmp(elem->classes, "hline-after") == 0) {
                strbuf_append_str(out, ";border-bottom:0.5px solid currentColor");
            }
        }
        strbuf_append_str(out, "\">");

        // pstrut for baseline alignment
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        snprintf(buf, sizeof(buf), "__pstrut\" style=\"height:%.2fem\"></span>", pstrut_size);
        strbuf_append_str(out, buf);

        // content wrapper with height
        float content_height = elem->height + elem->depth;
        if (content_height > 0.01f) {
            snprintf(buf, sizeof(buf), "<span style=\"height:%.2fem;display:inline-block\">", content_height);
            strbuf_append_str(out, buf);
        }

        // render the actual content
        if (render_content) {
            render_content(elem->node, out, opts, render_depth + 1);
        } else {
            render_node(elem->node, out, opts, render_depth + 1);
        }

        if (content_height > 0.01f) {
            strbuf_append_str(out, "</span>");
        }

        strbuf_append_str(out, "</span>");  // close element wrapper
    }

    strbuf_append_str(out, "</span>");  // close ML__vlist

    // Safari workaround: ML__vlist-s with zero-width space
    if (has_depth) {
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-s\">\xe2\x80\x8b</span>");  // UTF-8 for U+200B (zero-width space)
    }

    strbuf_append_str(out, "</span>");  // close ML__vlist-r

    // second row for depth (if needed)
    if (has_depth) {
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-r\"><span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        snprintf(buf, sizeof(buf), "__vlist\" style=\"height:%.2fem\"></span></span>", depth_em);
        strbuf_append_str(out, buf);
    }

    strbuf_append_str(out, "</span>");  // close ML__vlist-t
}

// convert dimension from TeX points to em units (relative to font size)
static float pt_to_em(float pt, float font_size_px) {
    // TexNode dimensions are in CSS pixels
    // em is relative to font size (also in px)
    return pt / font_size_px;
}

// round to 3 decimal places for cleaner output
static float round3(float v) {
    return roundf(v * 1000.0f) / 1000.0f;
}

// escape HTML special characters
static void html_escape_char(StrBuf* out, int codepoint) {
    if (codepoint == '<') {
        strbuf_append_str(out, "&lt;");
    } else if (codepoint == '>') {
        strbuf_append_str(out, "&gt;");
    } else if (codepoint == '&') {
        strbuf_append_str(out, "&amp;");
    } else if (codepoint == '"') {
        strbuf_append_str(out, "&quot;");
    } else if (codepoint < 128) {
        strbuf_append_char(out, (char)codepoint);
    } else {
        // output as numeric character reference for non-ASCII
        char buf[16];
        snprintf(buf, sizeof(buf), "&#%d;", codepoint);
        strbuf_append_str(out, buf);
    }
}

// append a codepoint as UTF-8 or numeric reference
static void append_codepoint(StrBuf* out, int32_t cp) {
    if (cp < 0x80) {
        html_escape_char(out, cp);
    } else if (cp < 0x800) {
        char utf8[3];
        utf8[0] = (char)(0xC0 | (cp >> 6));
        utf8[1] = (char)(0x80 | (cp & 0x3F));
        utf8[2] = '\0';
        strbuf_append_str(out, utf8);
    } else if (cp < 0x10000) {
        char utf8[4];
        utf8[0] = (char)(0xE0 | (cp >> 12));
        utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (cp & 0x3F));
        utf8[3] = '\0';
        strbuf_append_str(out, utf8);
    } else {
        char utf8[5];
        utf8[0] = (char)(0xF0 | (cp >> 18));
        utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (cp & 0x3F));
        utf8[4] = '\0';
        strbuf_append_str(out, utf8);
    }
}

// get CSS class for atom type - MathLive compatible
static const char* atom_type_class(AtomType type) {
    switch (type) {
        // MathLive uses ML__mathit for ordinary math variables
        case AtomType::Ord: return "mathit";
        // operators use Computer Modern Roman
        case AtomType::Op: return "cmr";
        case AtomType::Bin: return "cmr";
        case AtomType::Rel: return "cmr";
        case AtomType::Open: return "cmr";
        case AtomType::Close: return "cmr";
        case AtomType::Punct: return "cmr";
        case AtomType::Inner: return "mathit";
        default: return "mathit";
    }
}

// get CSS class from font name - more accurate than atom_type_class
static const char* font_to_class(const char* font_name) {
    if (!font_name) return "mathit";

    // Map font name prefixes to CSS classes
    if (strncmp(font_name, "cmr", 3) == 0) return "cmr";      // roman
    if (strncmp(font_name, "cmmi", 4) == 0) return "mathit";  // math italic
    if (strncmp(font_name, "cmsy", 4) == 0) return "cmr";     // symbols (use roman class)
    if (strncmp(font_name, "cmex", 4) == 0) return "delim-size1";  // delimiters
    if (strncmp(font_name, "cmbx", 4) == 0) return "mathbf";  // bold
    if (strncmp(font_name, "cmss", 4) == 0) return "mathsf";  // sans-serif
    if (strncmp(font_name, "cmtt", 4) == 0) return "mathtt";  // typewriter
    if (strncmp(font_name, "cmsl", 4) == 0) return "mathit";  // slanted
    if (strncmp(font_name, "msbm", 4) == 0) return "mathbb";  // blackboard bold
    if (strncmp(font_name, "eufm", 4) == 0) return "mathfrak";// fraktur
    if (strncmp(font_name, "lasy", 4) == 0) return "cmr";     // LaTeX symbols

    return "mathit";  // default to italic
}

// Check if a node is a digit character (0-9)
// Returns the codepoint if it's a digit, 0 otherwise
static int32_t get_digit_codepoint(TexNode* node) {
    if (!node) return 0;

    int32_t codepoint = 0;
    const char* font_name = nullptr;

    if (node->node_class == NodeClass::Char) {
        codepoint = node->content.ch.codepoint;
        font_name = node->content.ch.font.name;
    } else if (node->node_class == NodeClass::MathChar) {
        codepoint = node->content.math_char.codepoint;
        font_name = node->content.math_char.font.name;
    } else {
        return 0;
    }

    // Convert font-specific encoding to Unicode if needed
    if (font_name) {
        if (strncmp(font_name, "cmr", 3) == 0 ||
            strncmp(font_name, "cmbx", 4) == 0) {
            // cmr10/cmbx digits are at positions 48-57 (same as ASCII)
            if (codepoint >= 48 && codepoint <= 57) {
                return codepoint;  // already ASCII/Unicode digit
            }
        }
    }

    // Check if it's a Unicode digit (0-9)
    if (codepoint >= '0' && codepoint <= '9') {
        return codepoint;
    }

    return 0;
}

// Get the CSS class for a digit character node
static const char* get_digit_class(TexNode* node, const HtmlRenderOptions& opts) {
    const char* font_name = nullptr;

    if (node->node_class == NodeClass::Char) {
        font_name = node->content.ch.font.name;
    } else if (node->node_class == NodeClass::MathChar) {
        font_name = node->content.math_char.font.name;
    }

    if (font_name) {
        return font_to_class(font_name);
    }

    return "cmr";  // digits use roman font by default
}

// Map cmsy10 (Computer Modern Symbol) character codes to Unicode for HTML output
// cmsy10 contains mathematical symbols: operators, relations, arrows, etc.
static int32_t cmsy10_to_unicode(int32_t code) {
    switch (code) {
        // Greek capitals (positions 0-11 are Greek in cmsy)
        // Not commonly used from cmsy10, usually from cmr10

        // Binary operators
        case 0:  return 0x2212; // minus −
        case 1:  return 0x22C5; // cdot ⋅
        case 2:  return 0x00D7; // times ×
        case 3:  return 0x2217; // ast ∗
        case 4:  return 0x00F7; // div ÷
        case 5:  return 0x22C6; // star ⋆
        case 6:  return 0x00B1; // pm ±
        case 7:  return 0x2213; // mp ∓
        case 8:  return 0x2295; // oplus ⊕
        case 9:  return 0x2296; // ominus ⊖
        case 10: return 0x2297; // otimes ⊗
        case 11: return 0x2298; // oslash ⊘
        case 12: return 0x2299; // odot ⊙
        case 13: return 0x25EF; // bigcirc ◯
        case 14: return 0x2218; // circ ∘
        case 15: return 0x2219; // bullet ∙

        // Relations
        case 16: return 0x224D; // asymp ≍
        case 17: return 0x2261; // equiv ≡
        case 18: return 0x2286; // subseteq ⊆
        case 19: return 0x2287; // supseteq ⊇
        case 20: return 0x2264; // leq ≤
        case 21: return 0x2265; // geq ≥
        case 22: return 0x227C; // preceq ≼
        case 23: return 0x227D; // succeq ≽
        case 24: return 0x223C; // sim ∼
        case 25: return 0x2248; // approx ≈
        case 26: return 0x2282; // subset ⊂
        case 27: return 0x2283; // supset ⊃
        case 28: return 0x226A; // ll ≪
        case 29: return 0x226B; // gg ≫
        case 30: return 0x227A; // prec ≺
        case 31: return 0x227B; // succ ≻

        // Arrows
        case 32: return 0x2190; // leftarrow ←
        case 33: return 0x2192; // rightarrow →
        case 34: return 0x2191; // uparrow ↑
        case 35: return 0x2193; // downarrow ↓
        case 36: return 0x2194; // leftrightarrow ↔
        case 37: return 0x2197; // nearrow ↗
        case 38: return 0x2198; // searrow ↘
        case 39: return 0x2243; // simeq ≃
        case 40: return 0x21D0; // Leftarrow ⇐
        case 41: return 0x21D2; // Rightarrow ⇒
        case 42: return 0x21D1; // Uparrow ⇑
        case 43: return 0x21D3; // Downarrow ⇓
        case 44: return 0x21D4; // Leftrightarrow ⇔
        case 45: return 0x2196; // nwarrow ↖
        case 46: return 0x2199; // swarrow ↙
        case 47: return 0x221D; // propto ∝

        // Miscellaneous symbols
        case 48: return 0x2032; // prime ′
        case 49: return 0x221E; // infty ∞
        case 50: return 0x2208; // in ∈
        case 51: return 0x220B; // ni ∋
        case 52: return 0x25B3; // triangle △
        case 53: return 0x25BD; // triangledown ▽
        case 54: return 0x0338; // negation slash (for \not) - combining character
        case 55: return 0x21A6; // mapsto ↦
        case 56: return 0x2200; // forall ∀
        case 57: return 0x2203; // exists ∃
        case 58: return 0x00AC; // neg ¬
        case 59: return 0x2205; // emptyset ∅
        case 60: return 0x211C; // Re ℜ
        case 61: return 0x2111; // Im ℑ
        case 62: return 0x22A4; // top ⊤
        case 63: return 0x22A5; // perp ⊥

        // More symbols
        case 64: return 0x2135; // aleph ℵ

        // Calligraphic letters (positions 65-90 are calligraphic A-Z)
        // Pass through as ASCII

        // More operators and symbols
        case 91: return 0x222A; // cup ∪
        case 92: return 0x2229; // cap ∩
        case 93: return 0x228E; // uplus ⊎
        case 94: return 0x2227; // wedge ∧
        case 95: return 0x2228; // vee ∨

        // Delimiters
        case 98:  return 0x230A; // lfloor ⌊
        case 99:  return 0x230B; // rfloor ⌋
        case 100: return 0x2308; // lceil ⌈
        case 101: return 0x2309; // rceil ⌉
        case 102: return '{';    // lbrace
        case 103: return '}';    // rbrace
        case 104: return 0x27E8; // langle ⟨
        case 105: return 0x27E9; // rangle ⟩
        case 106: return '|';    // vert
        case 107: return 0x2225; // Vert ‖
        case 108: return 0x2195; // updownarrow ↕
        case 109: return 0x21D5; // Updownarrow ⇕
        case 110: return '\\';   // backslash

        // More relations and operators
        case 114: return 0x2207; // nabla ∇
        case 116: return 0x2294; // sqcup ⊔
        case 117: return 0x2293; // sqcap ⊓
        case 118: return 0x2291; // sqsubseteq ⊑
        case 119: return 0x2292; // sqsupseteq ⊒

        // Card suits
        case 124: return 0x2663; // clubsuit ♣
        case 125: return 0x2662; // diamondsuit ♢
        case 126: return 0x2661; // heartsuit ♡
        case 127: return 0x2660; // spadesuit ♠

        default:
            // For calligraphic letters (65-90) and unmapped codes
            if (code >= 65 && code <= 90) {
                return code;  // A-Z calligraphic, render as letters
            }
            // Return as-is if printable ASCII, else 0
            return (code >= 32 && code < 127) ? code : 0;
    }
}

// Map cmmi10 (Computer Modern Math Italic) character codes to Unicode for HTML output
// cmmi10 contains italic Greek letters and some special symbols
static int32_t cmmi10_to_unicode(int32_t code) {
    switch (code) {
        // Lowercase Greek letters (0-25)
        case 11: return 0x03B1; // alpha α
        case 12: return 0x03B2; // beta β
        case 13: return 0x03B3; // gamma γ
        case 14: return 0x03B4; // delta δ
        case 15: return 0x03B5; // epsilon ε (varepsilon actually)
        case 16: return 0x03B6; // zeta ζ
        case 17: return 0x03B7; // eta η
        case 18: return 0x03B8; // theta θ
        case 19: return 0x03B9; // iota ι
        case 20: return 0x03BA; // kappa κ
        case 21: return 0x03BB; // lambda λ
        case 22: return 0x03BC; // mu μ
        case 23: return 0x03BD; // nu ν
        case 24: return 0x03BE; // xi ξ
        case 25: return 0x03C0; // pi π
        case 26: return 0x03C1; // rho ρ
        case 27: return 0x03C3; // sigma σ
        case 28: return 0x03C4; // tau τ
        case 29: return 0x03C5; // upsilon υ
        case 30: return 0x03C6; // phi φ
        case 31: return 0x03C7; // chi χ
        case 32: return 0x03C8; // psi ψ
        case 33: return 0x03C9; // omega ω

        // Variant Greek letters
        case 34: return 0x03B5; // varepsilon ε
        case 35: return 0x03D1; // vartheta ϑ
        case 36: return 0x03D6; // varpi ϖ
        case 37: return 0x03F1; // varrho ϱ
        case 38: return 0x03C2; // varsigma ς
        case 39: return 0x03D5; // varphi φ

        // Harpoons and other arrows
        case 40: return 0x21BC; // leftharpoonup ↼
        case 41: return 0x21BD; // leftharpoondown ↽
        case 42: return 0x21C0; // rightharpoonup ⇀
        case 43: return 0x21C1; // rightharpoondown ⇁

        // Special symbols
        case 60: return '.';    // period
        case 61: return ',';    // comma
        case 62: return '<';    // less
        case 63: return '>';    // greater
        case 64: return 0x2202; // partial ∂

        // Uppercase Greek (positions in cmmi10)
        case 0:  return 0x0393; // Gamma Γ
        case 1:  return 0x0394; // Delta Δ
        case 2:  return 0x0398; // Theta Θ
        case 3:  return 0x039B; // Lambda Λ
        case 4:  return 0x039E; // Xi Ξ
        case 5:  return 0x03A0; // Pi Π
        case 6:  return 0x03A3; // Sigma Σ
        case 7:  return 0x03A5; // Upsilon Υ
        case 8:  return 0x03A6; // Phi Φ
        case 9:  return 0x03A8; // Psi Ψ
        case 10: return 0x03A9; // Omega Ω

        // Miscellaneous
        case 96:  return 0x2113; // ell ℓ
        case 123: return 0x0131; // dotless i ı (imath)
        case 124: return 0x0237; // dotless j ȷ (jmath)
        case 125: return 0x210F; // hbar ℏ

        default:
            // Italic letters A-Z (65-90) and a-z (97-122)
            if ((code >= 65 && code <= 90) || (code >= 97 && code <= 122)) {
                return code;  // ASCII letters in italic
            }
            // Digits 0-9 (48-57) - though rarely in cmmi10
            if (code >= 48 && code <= 57) {
                return code;
            }
            return (code >= 32 && code < 127) ? code : 0;
    }
}

// Map cmr10 (Computer Modern Roman) character codes to Unicode for HTML output
// cmr10 contains roman text including uppercase Greek letters at positions 0-10
static int32_t cmr10_to_unicode(int32_t code) {
    switch (code) {
        // Uppercase Greek letters (positions 0-10 in OT1/cmr encoding)
        case 0: return 0x0393;   // Gamma Γ
        case 1: return 0x0394;   // Delta Δ
        case 2: return 0x0398;   // Theta Θ
        case 3: return 0x039B;   // Lambda Λ
        case 4: return 0x039E;   // Xi Ξ
        case 5: return 0x03A0;   // Pi Π
        case 6: return 0x03A3;   // Sigma Σ
        case 7: return 0x03A5;   // Upsilon Υ
        case 8: return 0x03A6;   // Phi Φ
        case 9: return 0x03A8;   // Psi Ψ
        case 10: return 0x03A9;  // Omega Ω

        // Ligatures and special characters
        case 11: return 0xFB00;  // ff ligature ﬀ
        case 12: return 0xFB01;  // fi ligature ﬁ
        case 13: return 0xFB02;  // fl ligature ﬂ
        case 14: return 0xFB03;  // ffi ligature ﬃ
        case 15: return 0xFB04;  // ffl ligature ﬄ
        case 16: return 0x0131;  // dotless i ı
        case 17: return 0x0237;  // dotless j ȷ

        // Accents
        case 18: return 0x0060;  // grave `
        case 19: return 0x00B4;  // acute ´
        case 20: return 0x02C7;  // caron ˇ
        case 21: return 0x02D8;  // breve ˘
        case 22: return 0x00AF;  // macron ¯
        case 23: return 0x02DA;  // ring above ˚
        case 24: return 0x00B8;  // cedilla ¸
        case 25: return 0x00DF;  // eszett ß
        case 26: return 0x00E6;  // ae æ
        case 27: return 0x0153;  // oe œ
        case 28: return 0x00F8;  // o-slash ø
        case 29: return 0x00C6;  // AE Æ
        case 30: return 0x0152;  // OE Œ
        case 31: return 0x00D8;  // O-slash Ø

        // Special quote characters
        case 34: return 0x201D;  // right double quote "
        case 39: return 0x2019;  // right single quote '
        case 60: return 0x00A1;  // inverted exclamation ¡
        case 62: return 0x00BF;  // inverted question ¿
        case 92: return 0x201C;  // left double quote "
        case 123: return 0x2013; // en dash –
        case 124: return 0x2014; // em dash —
        case 125: return 0x02DD; // double acute ˝
        case 126: return 0x0303; // tilde ~
        case 127: return 0x00A8; // diaeresis ¨

        default:
            // Standard ASCII range (32-126) maps directly
            if (code >= 32 && code < 127) return code;
            return code;
    }
}

// Map cmex10 character codes to Unicode for HTML output
// cmex10 contains extensible delimiters and large operators
static int32_t cmex10_to_unicode(int32_t code) {
    // Brackets and parentheses (small sizes)
    switch (code) {
        case 0:  return '(';   // left paren small
        case 1:  return ')';   // right paren small
        case 2:  return '[';   // left bracket small
        case 3:  return ']';   // right bracket small
        case 8:  return '{';   // left brace small
        case 9:  return '}';   // right brace small
        case 12: return '|';   // vertical bar
        case 13: return 0x2225; // double vertical bar ‖
        case 14: return '/';   // slash
        case 15: return '\\';  // backslash

        // Larger sizes (same Unicode, just larger rendition)
        case 16: return '(';   // left paren medium
        case 17: return ')';   // right paren medium
        case 18: return '(';   // left paren large
        case 19: return ')';   // right paren large
        case 20: return '[';   // left bracket medium
        case 21: return ']';   // right bracket medium
        case 22: return 0x230A; // left floor
        case 23: return 0x230B; // right floor
        case 24: return 0x2308; // left ceiling
        case 25: return 0x2309; // right ceiling
        case 26: return '{';   // left brace medium
        case 27: return '}';   // right brace medium

        // Big operators (small sizes)
        case 80: return 0x2211; // summation ∑
        case 81: return 0x220F; // product ∏
        case 82: return 0x222B; // integral ∫
        case 83: return 0x22C3; // big union ⋃
        case 84: return 0x22C2; // big intersection ⋂
        case 85: return 0x2A04; // big multiset union ⊎
        case 86: return 0x22C0; // big wedge ⋀
        case 87: return 0x22C1; // big vee ⋁
        // Big operators (large sizes, same symbols displayed larger)
        case 88: return 0x2211; // summation ∑ (large)
        case 89: return 0x220F; // product ∏ (large)
        case 90: return 0x222B; // integral ∫ (large)
        case 91: return 0x22C3; // big union ⋃ (large)
        case 92: return 0x22C2; // big intersection ⋂ (large)
        case 93: return 0x2A04; // big multiset union ⊎ (large)
        case 94: return 0x22C0; // big wedge ⋀ (large)
        case 95: return 0x22C1; // big vee ⋁ (large)
        // coproduct
        case 96: return 0x2210; // coproduct ∐ (small)
        case 97: return 0x2210; // coproduct ∐ (large)
        // oint (contour integral)
        case 72: return 0x222E; // contour integral ∮ (small)
        case 73: return 0x222E; // contour integral ∮ (large)
        // circled operators
        case 76: return 0x2A01; // bigoplus ⨁ (small)
        case 77: return 0x2A01; // bigoplus ⨁ (large)
        case 78: return 0x2A02; // bigotimes ⨂ (small)
        case 79: return 0x2A02; // bigotimes ⨂ (large)

        default:
            // for unmapped codes, just return as-is (may render as empty)
            return (code >= 32 && code < 127) ? code : 0;
    }
}

// Map msbm10 (AMS Blackboard Bold) character codes to Unicode for HTML output
// msbm10 contains blackboard bold letters and special symbols
static int32_t msbm10_to_unicode(int32_t code) {
    // Blackboard bold uppercase letters A-Z at positions 65-90
    if (code >= 65 && code <= 90) {
        // Map to Unicode Mathematical Double-Struck Capital letters
        // A=65 → 𝔸 U+1D538, but use simpler ℂ, ℕ, ℙ, ℚ, ℝ, ℤ when available
        switch (code) {
            case 67: return 0x2102;  // C → ℂ
            case 72: return 0x210D;  // H → ℍ
            case 78: return 0x2115;  // N → ℕ
            case 80: return 0x2119;  // P → ℙ
            case 81: return 0x211A;  // Q → ℚ
            case 82: return 0x211D;  // R → ℝ
            case 90: return 0x2124;  // Z → ℤ
            default:
                // Use Mathematical Double-Struck for others
                return 0x1D538 + (code - 65);  // A=𝔸, B=𝔹, etc.
        }
    }
    // Lowercase blackboard bold a-z at positions 97-122 (if present)
    if (code >= 97 && code <= 122) {
        return 0x1D552 + (code - 97);  // a=𝕒, b=𝕓, etc.
    }
    // Additional symbols
    switch (code) {
        case 107: return 0x2127;  // mho ℧
        default:
            return (code >= 32 && code < 127) ? code : 0;
    }
}

// Map eufm10 (Euler Fraktur) character codes to Unicode for HTML output
// eufm10 contains Fraktur/blackletter style letters
static int32_t eufm10_to_unicode(int32_t code) {
    // Fraktur uppercase A-Z at positions 65-90
    if (code >= 65 && code <= 90) {
        // Use Unicode Mathematical Fraktur Capital letters
        // Some have dedicated Unicode points, others use Plane 1
        switch (code) {
            case 67: return 0x212D;  // C → ℭ
            case 72: return 0x210C;  // H → ℌ
            case 73: return 0x2111;  // I → ℑ
            case 82: return 0x211C;  // R → ℜ
            case 90: return 0x2128;  // Z → ℨ
            default:
                // Mathematical Fraktur Capital: A=𝔄 at U+1D504
                return 0x1D504 + (code - 65);
        }
    }
    // Fraktur lowercase a-z at positions 97-122
    if (code >= 97 && code <= 122) {
        // Mathematical Fraktur Small: a=𝔞 at U+1D51E
        return 0x1D51E + (code - 97);
    }
    // Pass through other codes
    return (code >= 32 && code < 127) ? code : 0;
}

// render a single character node
static void render_char(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts) {
    if (!node) return;

    // get codepoint from content union
    int32_t codepoint = 0;
    const char* font_name = nullptr;
    if (node->node_class == NodeClass::Char) {
        codepoint = node->content.ch.codepoint;
        font_name = node->content.ch.font.name;
    } else if (node->node_class == NodeClass::MathChar) {
        codepoint = node->content.math_char.codepoint;
        font_name = node->content.math_char.font.name;
    } else if (node->node_class == NodeClass::Ligature) {
        codepoint = node->content.lig.codepoint;
        font_name = node->content.lig.font.name;
    }

    // Convert TFM character codes to Unicode based on font
    if (font_name) {
        if (strncmp(font_name, "cmr", 3) == 0) {
            codepoint = cmr10_to_unicode(codepoint);
        } else if (strncmp(font_name, "cmex", 4) == 0) {
            codepoint = cmex10_to_unicode(codepoint);
        } else if (strncmp(font_name, "cmsy", 4) == 0) {
            codepoint = cmsy10_to_unicode(codepoint);
        } else if (strncmp(font_name, "cmmi", 4) == 0) {
            codepoint = cmmi10_to_unicode(codepoint);
        } else if (strncmp(font_name, "msbm", 4) == 0) {
            codepoint = msbm10_to_unicode(codepoint);
        } else if (strncmp(font_name, "eufm", 4) == 0) {
            codepoint = eufm10_to_unicode(codepoint);
        }
        // cmbx, cmss, cmtt use same encoding as cmr
        else if (strncmp(font_name, "cmbx", 4) == 0 ||
                 strncmp(font_name, "cmss", 4) == 0 ||
                 strncmp(font_name, "cmtt", 4) == 0) {
            codepoint = cmr10_to_unicode(codepoint);
        }
    }

    // determine CSS class based on font name (more accurate than atom type)
    const char* atom_class = "mathit";  // default
    if (node->node_class == NodeClass::MathChar) {
        // prefer font name for class determination
        if (node->content.math_char.font.name) {
            atom_class = font_to_class(node->content.math_char.font.name);
        } else {
            atom_class = atom_type_class(node->content.math_char.atom_type);
        }
    } else if (node->node_class == NodeClass::Char) {
        if (node->content.ch.font.name) {
            atom_class = font_to_class(node->content.ch.font.name);
        }
    }

    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__%s", opts.class_prefix, atom_class);

    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\">");

    // output the character
    if (codepoint > 0) {
        append_codepoint(out, codepoint);
    }

    strbuf_append_str(out, "</span>");
}

// render horizontal spacing (kern)
static void render_kern(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts) {
    if (!node) return;

    // Check for null delimiter flag (from \bigl., \bigr., etc.)
    if (node->flags & TexNode::FLAG_NULLDELIM) {
        // Output MathLive-compatible null delimiter
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__nulldelimiter\" style=\"width:0.12em\"></span>");
        return;
    }

    if (node->width == 0.0f) return;

    float em = pt_to_em(node->width, opts.base_font_size_px);

    // use a span with inline-block width for spacing (MathLive compatible)
    strbuf_append_str(out, "<span style=\"display:inline-block;width:");
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2fem", round3(em));
    strbuf_append_str(out, buf);
    strbuf_append_str(out, "\"></span>");
}

// render stretchable space (glue)
static void render_glue(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts) {
    if (!node) return;

    float space_pt = node->content.glue.spec.space;

    // check for named spacing commands that map to CSS classes
    const char* name = node->content.glue.name;
    if (name && strcmp(name, "mathspace") == 0) {
        // Math spacing - the space value is in TeX points
        // For a 10pt math font:
        //   \quad  = 18 mu = 18 * 10pt / 18 = 10pt = 1em
        //   \qquad = 36 mu = 36 * 10pt / 18 = 20pt = 2em
        // We convert based on the TeX em size (10pt for most math fonts)
        float tex_em_pt = 10.0f;  // 1em in TeX points for a 10pt font
        float em = space_pt / tex_em_pt;

        // Use MathLive CSS classes for standard spacing
        if (fabsf(em - 1.0f) < 0.1f) {
            // approximately 1em = \quad
            strbuf_append_str(out, "<span class=\"ML__quad\"></span>");
            return;
        } else if (fabsf(em - 2.0f) < 0.1f) {
            // approximately 2em = \qquad
            strbuf_append_str(out, "<span class=\"ML__qquad\"></span>");
            return;
        } else if (fabsf(em - (3.0f / 18.0f)) < 0.02f) {
            // approximately 3/18 em = \,  (thinspace)
            strbuf_append_str(out, "<span class=\"ML__thinspace\"></span>");
            return;
        } else if (fabsf(em - (4.0f / 18.0f)) < 0.02f) {
            // approximately 4/18 em = \:  (mediumspace)
            strbuf_append_str(out, "<span class=\"ML__mediumspace\"></span>");
            return;
        } else if (fabsf(em - (5.0f / 18.0f)) < 0.02f) {
            // approximately 5/18 em = \;  (thickspace)
            strbuf_append_str(out, "<span class=\"ML__thickspace\"></span>");
            return;
        }

        // For other math spacing, use calculated em value
        if (em == 0.0f) return;

        strbuf_append_str(out, "<span style=\"display:inline-block;width:");
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2fem", round3(em));
        strbuf_append_str(out, buf);
        strbuf_append_str(out, "\"></span>");
        return;
    }

    // Non-math glue - use standard pt_to_em conversion
    float em = pt_to_em(space_pt, opts.base_font_size_px);
    if (em == 0.0f) return;

    // use a span with inline-block width for spacing
    strbuf_append_str(out, "<span style=\"display:inline-block;width:");
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2fem", round3(em));
    strbuf_append_str(out, buf);
    strbuf_append_str(out, "\"></span>");
}

// render a rule (horizontal or vertical line)
static void render_rule(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts) {
    if (!node) return;

    float width_em = pt_to_em(node->width, opts.base_font_size_px);
    float height_em = pt_to_em(node->height, opts.base_font_size_px);
    float depth_em = pt_to_em(node->depth, opts.base_font_size_px);

    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__rule", opts.class_prefix);

    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:inline-block;");

    if (opts.include_styles) {
        char style_buf[256];
        snprintf(style_buf, sizeof(style_buf),
            "width:%.3fem;height:%.3fem;background:currentColor;vertical-align:%.3fem;",
            round3(width_em), round3(height_em + depth_em), round3(-depth_em));
        strbuf_append_str(out, style_buf);
    }

    strbuf_append_str(out, "\"></span>");
}

// render horizontal list (row of items)
static void render_hlist(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    // At root level (depth=0, inside ML__base), we usually don't add extra wrapper.
    // However, if the hlist contains complex structures (mtable, delimiter with mtable),
    // we need the wrapper for proper layout like MathLive.
    bool needs_wrapper = (depth > 0);
    bool has_color = (node->color != nullptr);

    if (!needs_wrapper && !has_color) {
        // Check if this hlist contains complex structures that need wrapping
        // Complex structures: mtable (for bmatrix/pmatrix/etc), or delimiter + mtable combo
        bool has_mtable = false;
        bool has_delimiter = false;
        for (TexNode* child = node->first_child; child; child = child->next_sibling) {
            if (child->node_class == NodeClass::MTable) has_mtable = true;
            if (child->node_class == NodeClass::Char) {
                // Check for delimiter characters (brackets, parens, etc)
                int32_t cp = child->content.ch.codepoint;
                if (cp == '(' || cp == ')' || cp == '[' || cp == ']' ||
                    cp == '{' || cp == '}' || cp == '|' ||
                    cp == 0 || cp == 1 || cp == 2 || cp == 3) {  // cmex10 delimiters
                    has_delimiter = true;
                }
            }
        }
        // Wrap if we have both delimiter(s) and mtable - that's a delimited matrix
        if (has_mtable && has_delimiter) {
            needs_wrapper = true;
        }
    }

    if (needs_wrapper || has_color) {
        strbuf_append_str(out, "<span");
        if (opts.include_styles || has_color) {
            strbuf_append_str(out, " style=\"display:inline-block");
            if (has_color) {
                strbuf_append_str(out, ";color:");
                strbuf_append_str(out, node->color);
            }
            strbuf_append_str(out, "\"");
        }
        strbuf_append_str(out, ">");
    }

    // render children, merging consecutive digits into single spans
    // MathLive outputs numbers as single spans like <span>123</span>
    // instead of <span>1</span><span>2</span><span>3</span>
    TexNode* child = node->first_child;
    while (child) {
        // check if this is a digit that could be part of a number
        int32_t digit_cp = get_digit_codepoint(child);
        if (digit_cp) {
            // start collecting consecutive digits
            const char* digit_class = get_digit_class(child, opts);
            char class_buf[64];
            snprintf(class_buf, sizeof(class_buf), "%s__%s", opts.class_prefix, digit_class);

            strbuf_append_str(out, "<span class=\"");
            strbuf_append_str(out, class_buf);
            strbuf_append_str(out, "\">");

            // output first digit
            append_codepoint(out, digit_cp);

            // consume all consecutive digits with same class
            child = child->next_sibling;
            while (child) {
                int32_t next_digit_cp = get_digit_codepoint(child);
                if (!next_digit_cp) break;

                // check if same class (same font)
                const char* next_class = get_digit_class(child, opts);
                if (strcmp(digit_class, next_class) != 0) break;

                append_codepoint(out, next_digit_cp);
                child = child->next_sibling;
            }

            strbuf_append_str(out, "</span>");
            // child now points to next non-digit or null, continue loop
        } else {
            render_node(child, out, opts, depth + 1);
            child = child->next_sibling;
        }
    }

    if (needs_wrapper || has_color) {
        strbuf_append_str(out, "</span>");
    }
}

// render vertical list (stack of items) - MathLive vlist structure
static void render_vlist(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    // count children and collect dimensions
    int child_count = 0;
    float total_height = 0.0f;
    float max_depth = 0.0f;

    TexNode* child = node->first_child;
    while (child) {
        child_count++;
        total_height += child->height + child->depth;
        if (child->depth > max_depth) max_depth = child->depth;
        child = child->next_sibling;
    }

    if (child_count == 0) return;

    // build VListElement array
    VListElement* elements = (VListElement*)alloca(child_count * sizeof(VListElement));
    float curr_pos = 0.0f;
    int idx = 0;

    child = node->first_child;
    while (child) {
        elements[idx].node = child;
        elements[idx].shift = curr_pos;
        elements[idx].height = child->height / opts.base_font_size_px;
        elements[idx].depth = child->depth / opts.base_font_size_px;
        elements[idx].classes = nullptr;

        curr_pos += (child->height + child->depth) / opts.base_font_size_px;
        idx++;
        child = child->next_sibling;
    }

    float height_em = total_height / opts.base_font_size_px;
    float depth_em = node->depth / opts.base_font_size_px;
    float pstrut_size = calculate_pstrut_size(elements, child_count, opts.base_font_size_px);

    render_vlist_structure(out, opts, elements, child_count, pstrut_size, height_em, depth_em, depth, nullptr);
}

// render fraction - MathLive-compatible vlist structure
static void render_fraction(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    char buf[256];

    // get dimensions from the fraction node
    float numer_height = 0.0f, numer_depth = 0.0f;
    float denom_height = 0.0f, denom_depth = 0.0f;
    float rule_thickness = node->content.frac.rule_thickness / opts.base_font_size_px;

    if (node->content.frac.numerator) {
        numer_height = node->content.frac.numerator->height / opts.base_font_size_px;
        numer_depth = node->content.frac.numerator->depth / opts.base_font_size_px;
    }
    if (node->content.frac.denominator) {
        denom_height = node->content.frac.denominator->height / opts.base_font_size_px;
        denom_depth = node->content.frac.denominator->depth / opts.base_font_size_px;
    }

    // calculate positions (MathLive-style)
    // axis is at 0.25em above baseline typically
    float axis_height = 0.25f;

    // numerator sits above the axis + rule
    float numer_shift = -(axis_height + rule_thickness / 2.0f + numer_depth + 0.1f);
    // denominator sits below the axis - rule
    float denom_shift = axis_height + rule_thickness / 2.0f + denom_height + 0.1f;

    // pstrut size (must be taller than content)
    float pstrut_size = 3.0f;  // standard MathLive pstrut

    // total height and depth
    float total_height = -numer_shift + numer_height;
    float total_depth = denom_shift + denom_depth;

    // MathLive structure: ML__mfrac > (delim/nulldelim + vlist-t + delim/nulldelim)
    // fraction container
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__mfrac\">");

    // Left delimiter - real delimiter or null
    int32_t left_delim = node->content.frac.left_delim;
    if (left_delim != 0) {
        // Use real delimiter (e.g., for \binom)
        // Calculate size based on fraction height
        float delim_height = total_height + total_depth;
        const char* size_class;
        if (delim_height < 1.5f) {
            size_class = "delim-size1";
        } else if (delim_height < 2.4f) {
            size_class = "ML__delim-size2";
        } else if (delim_height < 3.0f) {
            size_class = "delim-size3";
        } else {
            size_class = "ML__delim-size4";
        }
        snprintf(buf, sizeof(buf), "<span class=\"%s__delim-%s\">", opts.class_prefix, size_class + 6); // skip "delim-" prefix
        strbuf_append_str(out, buf);
        append_codepoint(out, (uint32_t)left_delim);
        strbuf_append_str(out, "</span>");
    } else {
        // null delimiter (open) - inside mfrac
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__nulldelimiter ");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__open\" style=\"width:0.12em\"></span>");
    }

    // vlist-t vlist-t2 (two rows for above/below baseline)
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-t ");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-t2\">");

    // first row
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-r\">");

    // vlist cell
    snprintf(buf, sizeof(buf), "<span class=\"%s__vlist\" style=\"height:%.2fem\">",
             opts.class_prefix, total_height);
    strbuf_append_str(out, buf);

    // numerator (top position) - rendered first to match MathLive DOM order
    snprintf(buf, sizeof(buf), "<span class=\"%s__center\" style=\"top:%.2fem\">",
             opts.class_prefix, -pstrut_size + numer_shift);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
             opts.class_prefix, pstrut_size);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span style=\"height:%.2fem;display:inline-block\">",
             numer_height + numer_depth);
    strbuf_append_str(out, buf);
    if (node->content.frac.numerator) {
        render_node(node->content.frac.numerator, out, opts, depth + 1);
    }
    strbuf_append_str(out, "</span></span>");

    // fraction line (if visible)
    if (rule_thickness > 0.001f) {
        snprintf(buf, sizeof(buf), "<span style=\"top:%.2fem\">",
                 -pstrut_size - axis_height);
        strbuf_append_str(out, buf);
        snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
                 opts.class_prefix, pstrut_size);
        strbuf_append_str(out, buf);
        snprintf(buf, sizeof(buf), "<span class=\"%s__frac-line\" style=\"height:%.2fem;display:inline-block\"></span>",
                 opts.class_prefix, rule_thickness);
        strbuf_append_str(out, buf);
        strbuf_append_str(out, "</span>");
    }

    // denominator (bottom position) - rendered second to match MathLive DOM order
    snprintf(buf, sizeof(buf), "<span class=\"%s__center\" style=\"top:%.2fem\">",
             opts.class_prefix, -pstrut_size + denom_shift);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
             opts.class_prefix, pstrut_size);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span style=\"height:%.2fem;display:inline-block\">",
             denom_height + denom_depth);
    strbuf_append_str(out, buf);
    if (node->content.frac.denominator) {
        render_node(node->content.frac.denominator, out, opts, depth + 1);
    }
    strbuf_append_str(out, "</span></span>");

    strbuf_append_str(out, "</span>");  // close vlist

    // Safari workaround
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-s\">\xe2\x80\x8b</span>");

    strbuf_append_str(out, "</span>");  // close vlist-r

    // second row (depth strut)
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-r\"><span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    snprintf(buf, sizeof(buf), "__vlist\" style=\"height:%.2fem\"></span></span>", total_depth);
    strbuf_append_str(out, buf);

    strbuf_append_str(out, "</span>");  // close vlist-t

    // Right delimiter - real delimiter or null
    int32_t right_delim = node->content.frac.right_delim;
    if (right_delim != 0) {
        // Use real delimiter (e.g., for \binom)
        float delim_height = total_height + total_depth;
        const char* size_class;
        if (delim_height < 1.5f) {
            size_class = "delim-size1";
        } else if (delim_height < 2.4f) {
            size_class = "ML__delim-size2";
        } else if (delim_height < 3.0f) {
            size_class = "delim-size3";
        } else {
            size_class = "ML__delim-size4";
        }
        snprintf(buf, sizeof(buf), "<span class=\"%s__delim-%s\">", opts.class_prefix, size_class + 6);
        strbuf_append_str(out, buf);
        append_codepoint(out, (uint32_t)right_delim);
        strbuf_append_str(out, "</span>");
    } else {
        // null delimiter (close) - inside mfrac
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__nulldelimiter ");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__close\" style=\"width:0.12em\"></span>");
    }

    strbuf_append_str(out, "</span>");  // close mfrac
}

// render radical (square root) - MathLive-compatible structure
static void render_radical(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    char buf[256];

    // get dimensions
    float radicand_height = 0.0f, radicand_depth = 0.0f;
    if (node->content.radical.radicand) {
        radicand_height = node->content.radical.radicand->height / opts.base_font_size_px;
        radicand_depth = node->content.radical.radicand->depth / opts.base_font_size_px;
    }

    float total_height = radicand_height + 0.25f;  // add space for overline
    float total_depth = radicand_depth;
    float pstrut_size = 3.0f;

    // calculate sqrt-line position (higher than content)
    float sqrt_line_top = -pstrut_size - total_height + 0.1f;
    float content_top = -pstrut_size;

    // MathLive structure: inline-block wrapper containing sqrt-index?, sqrt-sign, vlist-t
    snprintf(buf, sizeof(buf), "<span style=\"display:inline-block;height:%.2fem\">",
             total_height + total_depth);
    strbuf_append_str(out, buf);

    // index (if present) - for \sqrt[n]{x}
    if (node->content.radical.degree) {
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__sqrt-index\">");

        // vlist-t only (no t2) for index positioning
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-t\">");
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-r\">");
        snprintf(buf, sizeof(buf), "<span class=\"%s__vlist\" style=\"height:%.2fem\">",
                 opts.class_prefix, 0.65f);
        strbuf_append_str(out, buf);

        // index content positioned
        snprintf(buf, sizeof(buf), "<span style=\"top:%.2fem\">", -pstrut_size + 0.68f);
        strbuf_append_str(out, buf);
        snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
                 opts.class_prefix, pstrut_size);
        strbuf_append_str(out, buf);
        strbuf_append_str(out, "<span style=\"height:0.33em;display:inline-block;font-size: 50%\">");
        render_node(node->content.radical.degree, out, opts, depth + 1);
        strbuf_append_str(out, "</span></span>");

        strbuf_append_str(out, "</span></span></span>");  // close vlist, vlist-r, vlist-t
        strbuf_append_str(out, "</span>");  // close sqrt-index
    }

    // sqrt-sign with delim-size class (matching MathLive)
    snprintf(buf, sizeof(buf), "<span class=\"%s__sqrt-sign\" style=\"top:%.2fem\">",
             opts.class_prefix, -0.01f);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span class=\"%s__delim-size1\">%s</span>",
             opts.class_prefix, "√");
    strbuf_append_str(out, buf);
    strbuf_append_str(out, "</span>");  // close sqrt-sign

    // vlist-t only (no t2) for sqrt body - MathLive style
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-t\">");

    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-r\">");

    snprintf(buf, sizeof(buf), "<span class=\"%s__vlist\" style=\"height:%.2fem\">",
             opts.class_prefix, total_height);
    strbuf_append_str(out, buf);

    // radicand content FIRST (MathLive puts content before line)
    snprintf(buf, sizeof(buf), "<span style=\"top:%.2fem\">", content_top);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
             opts.class_prefix, pstrut_size);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span style=\"height:%.2fem;display:inline-block\">",
             radicand_height + radicand_depth);
    strbuf_append_str(out, buf);
    if (node->content.radical.radicand) {
        render_node(node->content.radical.radicand, out, opts, depth + 1);
    }
    strbuf_append_str(out, "</span></span>");

    // sqrt-line (overline) SECOND
    snprintf(buf, sizeof(buf), "<span style=\"top:%.2fem\">", sqrt_line_top);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
             opts.class_prefix, pstrut_size);
    strbuf_append_str(out, buf);
    snprintf(buf, sizeof(buf), "<span class=\"%s__sqrt-line\" style=\"height:0.04em;display:inline-block\"></span>",
             opts.class_prefix);
    strbuf_append_str(out, buf);
    strbuf_append_str(out, "</span>");

    strbuf_append_str(out, "</span>");  // close vlist
    strbuf_append_str(out, "</span></span>");  // close vlist-r, vlist-t
    strbuf_append_str(out, "</span>");  // close inline-block wrapper
}

// render subscript/superscript - MathLive-compatible vlist structure
static void render_scripts(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    char buf[256];

    bool has_sub = node->content.scripts.subscript != nullptr;
    bool has_sup = node->content.scripts.superscript != nullptr;

    // nucleus (base) - render first
    if (node->content.scripts.nucleus) {
        render_node(node->content.scripts.nucleus, out, opts, depth + 1);
    }

    // if no scripts, we're done
    if (!has_sub && !has_sup) return;

    // calculate dimensions
    float sup_height = 0.0f, sup_depth = 0.0f;
    float sub_height = 0.0f, sub_depth = 0.0f;

    if (has_sup) {
        sup_height = node->content.scripts.superscript->height / opts.base_font_size_px;
        sup_depth = node->content.scripts.superscript->depth / opts.base_font_size_px;
    }
    if (has_sub) {
        sub_height = node->content.scripts.subscript->height / opts.base_font_size_px;
        sub_depth = node->content.scripts.subscript->depth / opts.base_font_size_px;
    }

    // MathLive-compatible positioning
    // In MathLive, subscript comes BEFORE superscript in DOM order
    // subscript: top ~= -2.75em (closer to baseline)
    // superscript: top ~= -3.41em (higher up)
    float pstrut_size = 3.0f;  // standard MathLive pstrut
    float sup_top = -3.41f;    // superscript position (further from baseline)
    float sub_top = -2.75f;    // subscript position (closer to baseline)

    // msubsup wrapper
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__msubsup\">");

    // vlist-t [vlist-t2 if subscript]
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-t");
    if (has_sub) {
        strbuf_append_str(out, " ");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-t2");
    }
    strbuf_append_str(out, "\">");

    // first row
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-r\">");

    // calculate total height
    float total_height = has_sup ? (sup_height + 0.4f) : 0.3f;

    // vlist cell
    snprintf(buf, sizeof(buf), "<span class=\"%s__vlist\" style=\"height:%.2fem\">",
             opts.class_prefix, total_height);
    strbuf_append_str(out, buf);

    // MathLive order: subscript FIRST, then superscript
    // subscript (if present)
    if (has_sub) {
        snprintf(buf, sizeof(buf), "<span style=\"top:%.2fem\">",
                 sub_top);
        strbuf_append_str(out, buf);
        snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
                 opts.class_prefix, pstrut_size);
        strbuf_append_str(out, buf);
        snprintf(buf, sizeof(buf), "<span style=\"height:%.2fem;display:inline-block;font-size: 70%%\">",
                 sub_height + sub_depth);
        strbuf_append_str(out, buf);
        render_node(node->content.scripts.subscript, out, opts, depth + 1);
        strbuf_append_str(out, "</span></span>");
    }

    // superscript (if present)
    if (has_sup) {
        snprintf(buf, sizeof(buf), "<span style=\"top:%.2fem;margin-right:0.05em\">",
                 sup_top);
        strbuf_append_str(out, buf);
        snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
                 opts.class_prefix, pstrut_size);
        strbuf_append_str(out, buf);
        snprintf(buf, sizeof(buf), "<span style=\"height:%.2fem;display:inline-block;font-size: 70%%\">",
                 sup_height + sup_depth);
        strbuf_append_str(out, buf);
        render_node(node->content.scripts.superscript, out, opts, depth + 1);
        strbuf_append_str(out, "</span></span>");
    }

    strbuf_append_str(out, "</span>");  // close vlist

    // Safari workaround for subscripts
    if (has_sub) {
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-s\">\xe2\x80\x8b</span>");
    }

    strbuf_append_str(out, "</span>");  // close vlist-r

    // second row (depth strut) for subscript
    if (has_sub) {
        float depth_em = sub_depth + 0.25f;  // depth based on subscript
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-r\"><span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        snprintf(buf, sizeof(buf), "__vlist\" style=\"height:%.2fem\"></span></span>", depth_em);
        strbuf_append_str(out, buf);
    }

    strbuf_append_str(out, "</span>");  // close vlist-t
    strbuf_append_str(out, "</span>");  // close msubsup
}

// check if delimiter should be stacked (extensible delimiters like |, \|)
static bool is_stackable_delimiter(int32_t cp) {
    switch (cp) {
        case '|':
        case 0x2016:  // \|
        case 0x2223:  // \mid, ∣
        case 0x2225:  // \parallel
            return true;
        default:
            return false;
    }
}

// render delimiter (parentheses, brackets, braces) - MathLive-compatible
// For delimiters that need to scale (from \left...\right), build appropriate structure:
// - Brackets, parens, braces: use delim-size class with single character
// - Vertical bars: stack multiple characters in vlist
static void render_delimiter(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    char buf[256];
    const char* delim_class = node->content.delim.is_left ? "open" : "close";
    int32_t cp = node->content.delim.codepoint;
    float target_size = node->content.delim.target_size / opts.base_font_size_px;  // convert to em

    // threshold for using scaled delimiter (in em)
    const float SCALE_THRESHOLD = 1.2f;

    // for small delimiters or size 0, use simple character
    if (target_size < SCALE_THRESHOLD) {
        snprintf(buf, sizeof(buf), "<span class=\"%s__%s\">", opts.class_prefix, delim_class);
        strbuf_append_str(out, buf);
        append_codepoint(out, cp);
        strbuf_append_str(out, "</span>");
        return;
    }

    // outer left-right wrapper for scaled delimiters
    float margin_top = -target_size / 2.0f + 0.25f;  // center vertically around axis
    snprintf(buf, sizeof(buf), "<span class=\"%s__left-right\" style=\"margin-top:%.3fem;height:%.4fem\">",
             opts.class_prefix, margin_top, target_size);
    strbuf_append_str(out, buf);

    // determine sizing class based on target size (matches MathLive thresholds)
    // size1: < 1.5em, size2: 1.5-2.4em, size3: 2.4-3.0em, size4: > 3.0em
    const char* size_class;
    if (target_size < 1.5f) {
        size_class = "delim-size1";
    } else if (target_size < 2.4f) {
        size_class = "ML__delim-size2";
    } else if (target_size < 3.0f) {
        size_class = "delim-size3";
    } else {
        size_class = "delim-size4";
    }

    // for stackable delimiters (vertical bars), use stacked vlist structure
    if (is_stackable_delimiter(cp)) {
        // calculate how many stacked characters we need
        float char_height = 0.61f;  // typical glyph height in em
        int stack_count = (int)ceil(target_size / char_height);
        if (stack_count < 2) stack_count = 2;
        if (stack_count > 5) stack_count = 5;  // reasonable limit

        // total vlist height and depth
        float vlist_height = stack_count * char_height - char_height / 2.0f;
        float vlist_depth = char_height / 2.0f;
        float pstrut_size = 2.61f;  // MathLive standard for delimiters

        // delim-mult wrapper for stacked delimiter
        snprintf(buf, sizeof(buf), "<span class=\"%s__%s %s__delim-mult\">", opts.class_prefix, delim_class, opts.class_prefix);
        strbuf_append_str(out, buf);

        // vlist structure
        snprintf(buf, sizeof(buf), "<span class=\"delim-size1 %s__vlist-t %s__vlist-t2\">",
                 opts.class_prefix, opts.class_prefix);
        strbuf_append_str(out, buf);

        // vlist-r
        snprintf(buf, sizeof(buf), "<span class=\"%s__vlist-r\">", opts.class_prefix);
        strbuf_append_str(out, buf);

        // vlist cell
        snprintf(buf, sizeof(buf), "<span class=\"%s__vlist\" style=\"height:%.2fem\">", opts.class_prefix, vlist_height);
        strbuf_append_str(out, buf);

        // use mathematical vertical bar character for stacking
        int32_t stack_char = 0x2223;  // ∣ (DIVIDES)

        // stack delimiter characters from bottom to top
        for (int i = 0; i < stack_count; i++) {
            float top = -pstrut_size + (stack_count - 1 - i) * char_height + 0.47f;
            snprintf(buf, sizeof(buf), "<span style=\"top:%.2fem\">", top);
            strbuf_append_str(out, buf);

            snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
                     opts.class_prefix, pstrut_size);
            strbuf_append_str(out, buf);

            snprintf(buf, sizeof(buf), "<span style=\"height:%.2fem;display:inline-block\">", char_height);
            strbuf_append_str(out, buf);
            append_codepoint(out, stack_char);
            strbuf_append_str(out, "</span></span>");
        }

        strbuf_append_str(out, "</span>");  // close vlist

        // Safari workaround
        snprintf(buf, sizeof(buf), "<span class=\"%s__vlist-s\">\xe2\x80\x8b</span>", opts.class_prefix);
        strbuf_append_str(out, buf);

        strbuf_append_str(out, "</span>");  // close vlist-r

        // depth row
        snprintf(buf, sizeof(buf), "<span class=\"%s__vlist-r\"><span class=\"%s__vlist\" style=\"height:%.2fem\"></span></span>",
                 opts.class_prefix, opts.class_prefix, vlist_depth);
        strbuf_append_str(out, buf);

        strbuf_append_str(out, "</span>");  // close vlist-t
        strbuf_append_str(out, "</span>");  // close delim-mult
    } else {
        // for brackets, parens, braces: use single scaled character with size class
        snprintf(buf, sizeof(buf), "<span class=\"%s__%s %s\">", opts.class_prefix, delim_class, size_class);
        strbuf_append_str(out, buf);
        append_codepoint(out, cp);
        strbuf_append_str(out, "</span>");
    }

    strbuf_append_str(out, "</span>");  // close left-right
}

// render large math operator (sum, product, integral, etc.) - MathLive-compatible
static void render_mathop(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node || node->node_class != NodeClass::MathOp) return;

    char buf[128];
    int32_t codepoint = node->content.math_op.codepoint;
    const char* font_name = node->content.math_op.font.name;

    // Convert TFM code to Unicode based on font
    if (font_name && strncmp(font_name, "cmex", 4) == 0) {
        codepoint = cmex10_to_unicode(codepoint);
    }

    // MathLive wraps large ops in ML__op-group > ML__op-symbol ML__large-op
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__op-group\"><span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__op-symbol ");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__large-op\">");

    // Output the operator character
    if (codepoint > 0) {
        append_codepoint(out, codepoint);
    }

    strbuf_append_str(out, "</span></span>");
}

// render accent (hat, bar, etc.) - MathLive vlist-compatible structure
static void render_accent(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    char buf[256];
    float font_size_px = opts.base_font_size_px;

    // calculate dimensions
    float base_height = 0.43f;  // approximate height of base character in em
    float base_depth = 0.0f;
    float accent_height = 0.72f;  // typical accent height in em

    if (node->content.accent.base) {
        base_height = pt_to_em(node->content.accent.base->height, font_size_px);
        base_depth = pt_to_em(node->content.accent.base->depth, font_size_px);
    }

    // MathLive uses vlist structure for accents
    // Height = accent_height, Depth = base_depth
    float total_height = base_height + accent_height;
    float pstrut_size = total_height + 2.0f;  // MathLive adds 2em buffer

    // determine if we need depth row
    bool has_depth = base_depth > 0.01f;

    // vlist wrapper
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-t");
    if (has_depth) {
        strbuf_append_str(out, " ");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-t2");
    }
    strbuf_append_str(out, "\">");

    // vlist-r
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__vlist-r\">");

    // vlist with height
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    snprintf(buf, sizeof(buf), "__vlist\" style=\"height:%.2fem\">", total_height);
    strbuf_append_str(out, buf);

    // base element (bottom position)
    float base_top = -pstrut_size + base_height;
    snprintf(buf, sizeof(buf), "<span style=\"top:%.2fem\">", base_top);
    strbuf_append_str(out, buf);

    // pstrut
    snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
             opts.class_prefix, pstrut_size);
    strbuf_append_str(out, buf);

    // content wrapper with height
    snprintf(buf, sizeof(buf), "<span style=\"height:%.2fem;display:inline-block\">", base_height + base_depth);
    strbuf_append_str(out, buf);

    // render base
    if (node->content.accent.base) {
        render_node(node->content.accent.base, out, opts, depth + 1);
    }

    strbuf_append_str(out, "</span></span>");

    // accent element (top position, with ML__center class)
    float accent_top = -pstrut_size + total_height - 0.27f;  // adjust for accent positioning
    snprintf(buf, sizeof(buf), "<span class=\"%s__center\" style=\"top:%.2fem;margin-left:0.16em\">",
             opts.class_prefix, accent_top);
    strbuf_append_str(out, buf);

    // pstrut
    snprintf(buf, sizeof(buf), "<span class=\"%s__pstrut\" style=\"height:%.2fem\"></span>",
             opts.class_prefix, pstrut_size);
    strbuf_append_str(out, buf);

    // accent body
    snprintf(buf, sizeof(buf), "<span class=\"%s__accent-body\" style=\"height:%.2fem;display:inline-block\">",
             opts.class_prefix, accent_height);
    strbuf_append_str(out, buf);
    append_codepoint(out, node->content.accent.accent_char);
    strbuf_append_str(out, "</span></span>");

    strbuf_append_str(out, "</span>");  // close vlist

    // Safari workaround
    if (has_depth) {
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-s\">\xe2\x80\x8b</span>");
    }

    strbuf_append_str(out, "</span>");  // close vlist-r

    // depth row if needed
    if (has_depth) {
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        strbuf_append_str(out, "__vlist-r\"><span class=\"");
        strbuf_append_str(out, opts.class_prefix);
        snprintf(buf, sizeof(buf), "__vlist\" style=\"height:%.2fem\"></span></span>", base_depth);
        strbuf_append_str(out, buf);
    }

    strbuf_append_str(out, "</span>");  // close vlist-t
}

// Custom content renderer for mtable cells - skips wrapper HBox to match MathLive
static void render_mtable_cell_content(TexNode* cell, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!cell) return;

    // Unwrap nested HBoxes to get to the actual content
    TexNode* content = cell;
    while (content &&
           (content->node_class == NodeClass::HBox ||
            content->node_class == NodeClass::HList ||
            content->node_class == NodeClass::MathList)) {
        // If this box has exactly one child that's also a box, unwrap it
        if (content->first_child && content->first_child == content->last_child &&
            (content->first_child->node_class == NodeClass::HBox ||
             content->first_child->node_class == NodeClass::HList ||
             content->first_child->node_class == NodeClass::MathList)) {
            content = content->first_child;
        } else {
            // Render children of this box directly (not wrapped in ML__base)
            TexNode* child = content->first_child;
            while (child) {
                render_node(child, out, opts, depth);
                child = child->next_sibling;
            }
            return;
        }
    }

    // Fallback: render the content as-is
    render_node(content, out, opts, depth);
}

// render math table/array column - outputs MathLive-compatible col-align-X structure
static void render_mtable_column(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth, uint32_t hlines = 0, bool trailing_hline = false) {
    if (!node) return;

    char buf[256];
    char col_align = node->content.mtable_col.col_align;
    if (col_align == 0) col_align = 'c';  // default to center

    // output column wrapper with alignment class
    strbuf_append_str(out, "<span class=\"col-align-");
    buf[0] = col_align;
    buf[1] = '\0';
    strbuf_append_str(out, buf);
    strbuf_append_str(out, "\">");

    // count children and build vlist elements
    int child_count = 0;
    float total_height = 0.0f;

    TexNode* child = node->first_child;
    while (child) {
        // skip kerns (glue/spacing nodes)
        if (child->node_class != NodeClass::Kern && child->node_class != NodeClass::Glue) {
            child_count++;
            total_height += child->height + child->depth;
        }
        child = child->next_sibling;
    }

    if (child_count == 0) {
        strbuf_append_str(out, "</span>");
        return;
    }

    // build vlist elements array
    VListElement* elements = (VListElement*)alloca(child_count * sizeof(VListElement));
    float curr_pos = 0.0f;
    int idx = 0;

    child = node->first_child;
    while (child) {
        if (child->node_class != NodeClass::Kern && child->node_class != NodeClass::Glue) {
            elements[idx].node = child;
            elements[idx].shift = curr_pos;
            elements[idx].height = child->height / opts.base_font_size_px;
            elements[idx].depth = child->depth / opts.base_font_size_px;
            // Check if this row has an hline before it
            if (hlines & (1u << idx)) {
                elements[idx].classes = "hline";
            } else if (trailing_hline && idx == child_count - 1) {
                // Last row with trailing hline gets "hline-after" class
                elements[idx].classes = "hline-after";
            } else {
                elements[idx].classes = nullptr;
            }

            curr_pos += (child->height + child->depth) / opts.base_font_size_px;
            idx++;
        }
        child = child->next_sibling;
    }

    float height_em = total_height / opts.base_font_size_px;
    float depth_em = node->depth / opts.base_font_size_px;
    float pstrut_size = calculate_pstrut_size(elements, child_count, opts.base_font_size_px);

    render_vlist_structure(out, opts, elements, child_count, pstrut_size, height_em, depth_em, depth, render_mtable_cell_content);

    strbuf_append_str(out, "</span>");
}

// render math table/array - outputs MathLive-compatible ML__mtable structure
static void render_mtable(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    char buf[256];
    float arraycolsep = node->content.mtable.arraycolsep;
    uint32_t hlines = node->content.mtable.hlines;
    bool trailing_hline = node->content.mtable.trailing_hline;

    // output table container with ML__mtable class
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__mtable\">");

    // MathLive adds leading arraycolsep (0.5em)
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__arraycolsep\" style=\"width:0.5em\"></span>");

    // render children (columns and column separators)
    int col_idx = 0;
    TexNode* child = node->first_child;
    while (child) {
        if (child->node_class == NodeClass::MTableColumn) {
            // render the column with hlines info
            render_mtable_column(child, out, opts, depth + 1, hlines, trailing_hline);
            col_idx++;
        } else if (child->node_class == NodeClass::Kern) {
            // column separator - output as ML__arraycolsep
            // MathLive uses fixed 1em for arraycolsep between columns
            strbuf_append_str(out, "<span class=\"");
            strbuf_append_str(out, opts.class_prefix);
            strbuf_append_str(out, "__arraycolsep\" style=\"width:1em\"></span>");
        } else {
            // render other nodes directly
            render_node(child, out, opts, depth + 1);
        }
        child = child->next_sibling;
    }

    // MathLive adds trailing arraycolsep (0.5em)
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__arraycolsep\" style=\"width:0.5em\"></span>");

    strbuf_append_str(out, "</span>");
}

// main node dispatcher
static void render_node(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    // prevent infinite recursion
    if (depth > 100) {
        log_error("tex_html_render: max depth exceeded");
        return;
    }

    switch (node->node_class) {
        case NodeClass::Char:
        case NodeClass::MathChar:
        case NodeClass::Ligature:
            render_char(node, out, opts);
            break;

        case NodeClass::HList:
        case NodeClass::HBox:
        case NodeClass::MathList:
            render_hlist(node, out, opts, depth);
            break;

        case NodeClass::VList:
        case NodeClass::VBox:
        case NodeClass::VTop:
            render_vlist(node, out, opts, depth);
            break;

        case NodeClass::Rule:
            render_rule(node, out, opts);
            break;

        case NodeClass::Kern:
            render_kern(node, out, opts);
            break;

        case NodeClass::Glue:
            render_glue(node, out, opts);
            break;

        case NodeClass::Fraction:
            render_fraction(node, out, opts, depth);
            break;

        case NodeClass::Radical:
            render_radical(node, out, opts, depth);
            break;

        case NodeClass::Scripts:
            render_scripts(node, out, opts, depth);
            break;

        case NodeClass::Delimiter:
            render_delimiter(node, out, opts, depth);
            break;

        case NodeClass::MathOp:
            render_mathop(node, out, opts, depth);
            break;

        case NodeClass::Accent:
            render_accent(node, out, opts, depth);
            break;

        case NodeClass::MTable:
            render_mtable(node, out, opts, depth);
            break;

        case NodeClass::MTableColumn:
            render_mtable_column(node, out, opts, depth);
            break;

        case NodeClass::Penalty:
        case NodeClass::Disc:
            // ignore non-visual nodes
            break;

        default:
            // for unknown types, try to render children
            if (node->first_child) {
                TexNode* child = node->first_child;
                while (child) {
                    render_node(child, out, opts, depth + 1);
                    child = child->next_sibling;
                }
            }
            break;
    }
}

// add struts for baseline handling (like MathLive's makeStruts)
// MathLive uses minimum strut heights to ensure consistent baseline positioning
static void add_struts(TexNode* root, StrBuf* out, const HtmlRenderOptions& opts) {
    if (!root) return;

    float height_em = pt_to_em(root->height, opts.base_font_size_px);
    float depth_em = pt_to_em(root->depth, opts.base_font_size_px);

    // MathLive uses minimum strut heights for consistent baseline
    // Minimum height is approximately 0.7em for typical math content
    const float MIN_STRUT_HEIGHT = 0.7f;
    const float MIN_STRUT_DEPTH = 0.2f;

    // Use at least minimum values
    if (height_em < MIN_STRUT_HEIGHT) height_em = MIN_STRUT_HEIGHT;
    if (depth_em < MIN_STRUT_DEPTH && depth_em > 0.01f) depth_em = MIN_STRUT_DEPTH;

    char class_buf[64];

    // top strut
    snprintf(class_buf, sizeof(class_buf), "%s__strut", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:inline-block;height:");

    char buf[64];
    snprintf(buf, sizeof(buf), "%.2fem", round3(height_em));
    strbuf_append_str(out, buf);
    strbuf_append_str(out, "\"></span>");

    // bottom strut - use MathLive-compatible class name
    snprintf(class_buf, sizeof(class_buf), "%s__strut--bottom", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:inline-block;height:");

    snprintf(buf, sizeof(buf), "%.2fem;vertical-align:%.2fem",
             round3(height_em + depth_em), round3(-depth_em));
    strbuf_append_str(out, buf);
    strbuf_append_str(out, "\"></span>");
}

// public API: render to string buffer
void render_texnode_to_html(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts) {
    if (!node || !out) return;

    log_debug("tex_html_render: rendering node class=%d (%s)",
              (int)node->node_class, node_class_name(node->node_class));

    // wrapper with latex class
    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__latex", opts.class_prefix);

    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\"");

    if (opts.include_styles) {
        strbuf_append_str(out, " style=\"display:inline-block;white-space:nowrap\"");
    }

    strbuf_append_str(out, ">");

    // add struts for baseline
    add_struts(node, out, opts);

    // ML__base wrapper for MathLive compatibility
    char base_class[64];
    snprintf(base_class, sizeof(base_class), "%s__base", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, base_class);
    strbuf_append_str(out, "\">");

    // render the content
    render_node(node, out, opts, 0);

    strbuf_append_str(out, "</span>");  // close ML__base
    strbuf_append_str(out, "</span>");  // close ML__latex
}

// public API: render to allocated string
char* render_texnode_to_html(TexNode* node, Arena* arena) {
    HtmlRenderOptions opts;
    return render_texnode_to_html(node, arena, opts);
}

char* render_texnode_to_html(TexNode* node, Arena* arena, const HtmlRenderOptions& opts) {
    if (!node || !arena) return nullptr;

    StrBuf* buf = strbuf_new_cap(1024);

    render_texnode_to_html(node, buf, opts);

    // copy to arena
    size_t len = buf->length;
    char* result = (char*)arena_alloc(arena, len + 1);
    memcpy(result, buf->str, len + 1);

    strbuf_free(buf);
    return result;
}

// default CSS stylesheet - MathLive-compatible vlist layout
const char* get_math_css_stylesheet() {
    return R"CSS(
/* Lambda Math CSS - MathLive vlist-compatible */
.ML__latex {
    display: inline-block;
    white-space: nowrap;
    font-family: "CMU Serif", "Latin Modern Math", "STIX Two Math", serif;
}

/* Struts for baseline alignment */
.ML__strut {
    display: inline-block;
    width: 0;
}
.ML__strut--bottom {
    display: inline-block;
}

/* VList table structure (MathLive-compatible) */
.ML__vlist-t {
    display: inline-table;
    table-layout: fixed;
    border-collapse: collapse;
}
.ML__vlist-r {
    display: table-row;
}
.ML__vlist {
    display: table-cell;
    vertical-align: bottom;
    position: relative;
}
.ML__vlist > span {
    display: block;
    height: 0;
    position: relative;
}
.ML__vlist > span > span {
    display: inline-block;
}
.ML__vlist > span > .ML__pstrut {
    overflow: hidden;
    width: 0;
}
.ML__vlist-t2 {
    margin-right: -2px;
}
.ML__vlist-s {
    display: table-cell;
    vertical-align: bottom;
    font-size: 1px;
    width: 2px;
    min-width: 2px;
}
.ML__pstrut {
    display: inline-block;
    overflow: hidden;
    width: 0;
}

/* Fractions */
.ML__mfrac {
    display: inline-block;
    vertical-align: middle;
}
.ML__frac-line {
    display: inline-block;
    width: 100%;
    border-bottom: 0.04em solid currentColor;
}
.ML__center {
    text-align: center;
}
.ML__nulldelimiter {
    display: inline-block;
}

/* Scripts (subscript/superscript) */
.ML__msubsup {
    text-align: left;
    display: inline-block;
}
.ML__sup {
    font-size: 70%;
    vertical-align: super;
}
.ML__sub {
    font-size: 70%;
    vertical-align: sub;
}

/* Radicals (square roots) */
.ML__sqrt {
    display: inline-flex;
    align-items: flex-end;
}
.ML__sqrt-sign {
    display: inline-block;
}
.ML__sqrt-symbol {
    display: inline-block;
}
.ML__sqrt-body {
    display: inline-block;
}
.ML__sqrt-line {
    display: inline-block;
    width: 100%;
    border-bottom: 0.04em solid currentColor;
}
.ML__root {
    display: inline-block;
    margin-right: -0.55em;
    vertical-align: top;
}

/* Base and horizontal lists */
.ML__base {
    display: inline-block;
}
.ML__hlist {
    display: inline-block;
}
.ML__mord {
    display: inline-block;
}

/* Delimiters */
.ML__open, .ML__close {
    display: inline-block;
}

/* Accents */
.ML__accent {
    display: inline-flex;
    flex-direction: column;
    align-items: center;
}
.ML__accent-char {
    line-height: 0.5;
}

/* Rules */
.ML__rule {
    display: inline-block;
    background: currentColor;
}

/* Spacing classes (MathLive-compatible) */
.ML__quad { display: inline-block; width: 1em; }
.ML__qquad { display: inline-block; width: 2em; }
.ML__thinspace { display: inline-block; width: 0.17em; }
.ML__mediumspace { display: inline-block; width: 0.22em; }
.ML__thickspace { display: inline-block; width: 0.28em; }
.ML__negativethinspace { display: inline-block; margin-right: -0.17em; }
.ML__mspace { display: inline-block; }

/* Font classes */
.ML__mathit { font-style: italic; }
.ML__cmr { font-style: normal; }
)CSS";
}

// generate standalone HTML document
char* render_texnode_to_html_document(TexNode* node, Arena* arena, const HtmlRenderOptions& opts) {
    if (!node || !arena) return nullptr;

    StrBuf* buf = strbuf_new_cap(4096);

    // HTML header
    strbuf_append_str(buf, "<!DOCTYPE html>\n<html>\n<head>\n");
    strbuf_append_str(buf, "<meta charset=\"UTF-8\">\n");
    strbuf_append_str(buf, "<title>Math Formula</title>\n");
    strbuf_append_str(buf, "<style>\n");
    strbuf_append_str(buf, get_math_css_stylesheet());
    strbuf_append_str(buf, "\nbody { font-size: ");

    char buf2[32];
    snprintf(buf2, sizeof(buf2), "%.0fpx", opts.base_font_size_px);
    strbuf_append_str(buf, buf2);
    strbuf_append_str(buf, "; padding: 2em; }\n");
    strbuf_append_str(buf, "</style>\n</head>\n<body>\n");

    // render the math
    render_texnode_to_html(node, buf, opts);

    // HTML footer
    strbuf_append_str(buf, "\n</body>\n</html>\n");

    // copy to arena
    size_t len = buf->length;
    char* result = (char*)arena_alloc(arena, len + 1);
    memcpy(result, buf->str, len + 1);

    strbuf_free(buf);
    return result;
}

} // namespace tex
