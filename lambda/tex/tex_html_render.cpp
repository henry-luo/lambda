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
static void render_accent(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_mtable(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);
static void render_mtable_column(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth);

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
        snprintf(buf, sizeof(buf), " style=\"top:%.2fem\">", top_em);
        strbuf_append_str(out, buf);

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
    if (strncmp(font_name, "cmtt", 4) == 0) return "mathtt";  // typewriter
    if (strncmp(font_name, "cmsl", 4) == 0) return "mathit";  // slanted
    if (strncmp(font_name, "msbm", 4) == 0) return "ams";     // AMS symbols
    if (strncmp(font_name, "lasy", 4) == 0) return "cmr";     // LaTeX symbols

    return "mathit";  // default to italic
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

        // Big operators
        case 80: return 0x2211; // summation ∑
        case 81: return 0x220F; // product ∏
        case 82: return 0x222B; // integral ∫
        case 83: return 0x22C3; // big union ⋃
        case 84: return 0x22C2; // big intersection ⋂
        case 86: return 0x2A00; // big circled operator

        default:
            // for unmapped codes, just return as-is (may render as empty)
            return (code >= 32 && code < 127) ? code : 0;
    }
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

    // For cmex10, convert TFM character codes to Unicode
    if (font_name && strncmp(font_name, "cmex", 4) == 0) {
        codepoint = cmex10_to_unicode(codepoint);
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
    if (!node || node->width == 0.0f) return;

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

    float em = pt_to_em(node->content.glue.spec.space, opts.base_font_size_px);
    if (em == 0.0f) return;

    // use a span with inline-block width for spacing (MathLive compatible)
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

    char class_buf[64];
    // use ML__base for MathLive compatibility
    snprintf(class_buf, sizeof(class_buf), "%s__base", opts.class_prefix);

    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\"");

    if (opts.include_styles) {
        strbuf_append_str(out, " style=\"display:inline-block\"");
    }

    strbuf_append_str(out, ">");

    // render children
    TexNode* child = node->first_child;
    while (child) {
        render_node(child, out, opts, depth + 1);
        child = child->next_sibling;
    }

    strbuf_append_str(out, "</span>");
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

    // MathLive structure: ML__mfrac > (nulldelim + vlist-t + nulldelim)
    // fraction container
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__mfrac\">");

    // null delimiter (open) - inside mfrac
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__nulldelimiter ");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__open\" style=\"width:0.12em\"></span>");

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

    // denominator (bottom position)
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

    // numerator (top position)
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

    // null delimiter (close) - inside mfrac
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__nulldelimiter ");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__close\" style=\"width:0.12em\"></span>");

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

// render delimiter (parentheses, brackets, braces)
static void render_delimiter(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    char class_buf[64];
    const char* delim_class = node->content.delim.is_left ? "open" : "close";
    snprintf(class_buf, sizeof(class_buf), "%s__%s", opts.class_prefix, delim_class);

    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\">");

    // output delimiter character
    append_codepoint(out, node->content.delim.codepoint);

    strbuf_append_str(out, "</span>");
}

// render accent (hat, bar, etc.)
static void render_accent(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;

    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__accent", opts.class_prefix);

    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:inline-flex;flex-direction:column;align-items:center\">");

    // accent character on top
    snprintf(class_buf, sizeof(class_buf), "%s__accent-char", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"line-height:0.5\">");
    append_codepoint(out, node->content.accent.accent_char);
    strbuf_append_str(out, "</span>");

    // base
    if (node->content.accent.base) {
        render_node(node->content.accent.base, out, opts, depth + 1);
    }

    strbuf_append_str(out, "</span>");
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
static void render_mtable_column(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
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
            elements[idx].classes = nullptr;

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

    // output table container with ML__mtable class
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, opts.class_prefix);
    strbuf_append_str(out, "__mtable\">");

    // render children (columns and column separators)
    int col_idx = 0;
    TexNode* child = node->first_child;
    while (child) {
        if (child->node_class == NodeClass::MTableColumn) {
            // render the column
            render_mtable_column(child, out, opts, depth + 1);
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
static void add_struts(TexNode* root, StrBuf* out, const HtmlRenderOptions& opts) {
    if (!root) return;

    float height_em = pt_to_em(root->height, opts.base_font_size_px);
    float depth_em = pt_to_em(root->depth, opts.base_font_size_px);

    char class_buf[64];

    // top strut
    snprintf(class_buf, sizeof(class_buf), "%s__strut", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:inline-block;height:");

    char buf[64];
    snprintf(buf, sizeof(buf), "%.3fem", round3(height_em));
    strbuf_append_str(out, buf);
    strbuf_append_str(out, "\"></span>");

    // bottom strut - use MathLive-compatible class name
    snprintf(class_buf, sizeof(class_buf), "%s__strut--bottom", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:inline-block;height:");

    snprintf(buf, sizeof(buf), "%.3fem;vertical-align:%.3fem",
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

    // render the content
    render_node(node, out, opts, 0);

    strbuf_append_str(out, "</span>");
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
