// tex_html_render.cpp - HTML output for TeX math formulas
//
// Converts TexNode trees to HTML+CSS markup compatible with MathLive styling.
// Uses inline styles for positioning and MathLive class names for semantics.
//
// Reference: MathLive box.ts toMarkup() implementation

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

// render a single character node
static void render_char(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts) {
    if (!node) return;
    
    // get codepoint from content union
    int32_t codepoint = 0;
    if (node->node_class == NodeClass::Char) {
        codepoint = node->content.ch.codepoint;
    } else if (node->node_class == NodeClass::MathChar) {
        codepoint = node->content.math_char.codepoint;
    } else if (node->node_class == NodeClass::Ligature) {
        codepoint = node->content.lig.codepoint;
    }
    
    // determine CSS class based on atom type
    const char* atom_class = "ord";
    if (node->node_class == NodeClass::MathChar) {
        atom_class = atom_type_class(node->content.math_char.atom_type);
    }
    
    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__%s", opts.class_prefix, atom_class);
    
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\">");
    
    // output the character
    append_codepoint(out, codepoint);
    
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

// render vertical list (stack of items)
static void render_vlist(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;
    
    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__vlist", opts.class_prefix);
    
    // calculate shift for vertical alignment
    float shift_em = pt_to_em(node->shift, opts.base_font_size_px);
    
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\"");
    
    if (opts.include_styles) {
        char style_buf[128];
        snprintf(style_buf, sizeof(style_buf),
            " style=\"display:inline-flex;flex-direction:column;vertical-align:%.3fem\"",
            round3(shift_em));
        strbuf_append_str(out, style_buf);
    }
    
    strbuf_append_str(out, ">");
    
    // render children in vertical stack
    TexNode* child = node->first_child;
    while (child) {
        strbuf_append_str(out, "<span style=\"display:block\">");
        render_node(child, out, opts, depth + 1);
        strbuf_append_str(out, "</span>");
        child = child->next_sibling;
    }
    
    strbuf_append_str(out, "</span>");
}

// render fraction
static void render_fraction(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;
    
    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__mfrac", opts.class_prefix);
    
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\"");
    
    if (opts.include_styles) {
        strbuf_append_str(out, " style=\"display:inline-block;vertical-align:middle\"");
    }
    
    strbuf_append_str(out, ">");
    
    // vlist container
    snprintf(class_buf, sizeof(class_buf), "%s__vlist", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:inline-flex;flex-direction:column;align-items:center\">");
    
    // numerator
    snprintf(class_buf, sizeof(class_buf), "%s__numer", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:block;text-align:center\">");
    
    if (node->content.frac.numerator) {
        render_node(node->content.frac.numerator, out, opts, depth + 1);
    }
    
    strbuf_append_str(out, "</span>");
    
    // fraction line (only if rule_thickness > 0)
    if (node->content.frac.rule_thickness > 0.0f) {
        snprintf(class_buf, sizeof(class_buf), "%s__frac-line", opts.class_prefix);
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, class_buf);
        strbuf_append_str(out, "\" style=\"display:block;width:100%;border-bottom:1px solid currentColor;margin:0.1em 0\"></span>");
    }
    
    // denominator
    snprintf(class_buf, sizeof(class_buf), "%s__denom", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:block;text-align:center\">");
    
    if (node->content.frac.denominator) {
        render_node(node->content.frac.denominator, out, opts, depth + 1);
    }
    
    strbuf_append_str(out, "</span>");
    
    strbuf_append_str(out, "</span>");  // close vlist
    strbuf_append_str(out, "</span>");  // close mfrac
}

// render radical (square root)
static void render_radical(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;
    
    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__sqrt", opts.class_prefix);
    
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\"");
    
    if (opts.include_styles) {
        strbuf_append_str(out, " style=\"display:inline-flex;align-items:stretch\"");
    }
    
    strbuf_append_str(out, ">");
    
    // index (if present) - for \sqrt[n]{x}
    if (node->content.radical.degree) {
        snprintf(class_buf, sizeof(class_buf), "%s__sqrt-index", opts.class_prefix);
        strbuf_append_str(out, "<span class=\"");
        strbuf_append_str(out, class_buf);
        strbuf_append_str(out, "\" style=\"font-size:70%;align-self:flex-start;margin-right:-0.3em\">");
        render_node(node->content.radical.degree, out, opts, depth + 1);
        strbuf_append_str(out, "</span>");
    }
    
    // radical sign - use unicode √
    snprintf(class_buf, sizeof(class_buf), "%s__sqrt-sign", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"font-size:120%\">&#x221A;</span>");
    
    // radicand with overline
    snprintf(class_buf, sizeof(class_buf), "%s__sqrt-body", opts.class_prefix);
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"border-top:1px solid currentColor;padding:0 0.1em\">");
    
    if (node->content.radical.radicand) {
        render_node(node->content.radical.radicand, out, opts, depth + 1);
    }
    
    strbuf_append_str(out, "</span>");
    strbuf_append_str(out, "</span>");
}

// render subscript/superscript
static void render_scripts(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts, int depth) {
    if (!node) return;
    
    char class_buf[64];
    snprintf(class_buf, sizeof(class_buf), "%s__supsub", opts.class_prefix);
    
    strbuf_append_str(out, "<span class=\"");
    strbuf_append_str(out, class_buf);
    strbuf_append_str(out, "\" style=\"display:inline-block\">");
    
    // nucleus (base)
    if (node->content.scripts.nucleus) {
        render_node(node->content.scripts.nucleus, out, opts, depth + 1);
    }
    
    // subscript and superscript container
    bool has_sub = node->content.scripts.subscript != nullptr;
    bool has_sup = node->content.scripts.superscript != nullptr;
    
    if (has_sub || has_sup) {
        strbuf_append_str(out, "<span style=\"display:inline-flex;flex-direction:column;vertical-align:middle;font-size:70%\">");
        
        // superscript first (top)
        if (has_sup) {
            snprintf(class_buf, sizeof(class_buf), "%s__sup", opts.class_prefix);
            strbuf_append_str(out, "<span class=\"");
            strbuf_append_str(out, class_buf);
            strbuf_append_str(out, "\" style=\"line-height:1\">");
            render_node(node->content.scripts.superscript, out, opts, depth + 1);
            strbuf_append_str(out, "</span>");
        }
        
        // subscript (bottom)
        if (has_sub) {
            snprintf(class_buf, sizeof(class_buf), "%s__sub", opts.class_prefix);
            strbuf_append_str(out, "<span class=\"");
            strbuf_append_str(out, class_buf);
            strbuf_append_str(out, "\" style=\"line-height:1\">");
            render_node(node->content.scripts.subscript, out, opts, depth + 1);
            strbuf_append_str(out, "</span>");
        }
        
        strbuf_append_str(out, "</span>");
    }
    
    strbuf_append_str(out, "</span>");
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

// default CSS stylesheet
const char* get_math_css_stylesheet() {
    return R"CSS(
/* Lambda Math CSS - MathLive compatible */
.ML__latex {
    display: inline-block;
    white-space: nowrap;
    font-family: "CMU Serif", "Latin Modern Math", "STIX Two Math", serif;
}
.ML__strut {
    display: inline-block;
    width: 0;
}
.ML__hlist {
    display: inline-block;
}
.ML__vlist {
    display: inline-block;
}
.ML__mfrac {
    display: inline-block;
    vertical-align: middle;
}
.ML__numer, .ML__denom {
    display: block;
    text-align: center;
}
.ML__frac-line {
    display: block;
    width: 100%;
    border-bottom: 1px solid currentColor;
    margin: 0.1em 0;
}
.ML__sqrt {
    display: inline-flex;
    align-items: stretch;
}
.ML__sqrt-sign {
    font-size: 120%;
}
.ML__sqrt-body {
    border-top: 1px solid currentColor;
    padding: 0 0.1em;
}
.ML__sqrt-index {
    font-size: 70%;
    align-self: flex-start;
    margin-right: -0.3em;
}
.ML__supsub {
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
.ML__delim {
    display: inline-flex;
    align-items: center;
}
.ML__accent {
    display: inline-flex;
    flex-direction: column;
    align-items: center;
}
.ML__accent-char {
    line-height: 0.5;
}
.ML__rule {
    display: inline-block;
    background: currentColor;
}
/* MathLive compatible class names */
.ML__mathit { font-style: italic; }
.ML__cmr { font-style: normal; }
.ML__base { display: inline-block; }
.ML__strut--bottom { display: inline-block; }
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
