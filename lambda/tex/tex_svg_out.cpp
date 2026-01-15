// tex_svg_out.cpp - SVG Output Generation Implementation
//
// Converts TeX node trees to SVG format.

#include "tex_svg_out.hpp"
#include "lib/log.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// Helper Functions
// ============================================================================

static void write_indent(SVGWriter& writer) {
    if (!writer.params.indent) return;
    for (int i = 0; i < writer.indent_level; i++) {
        strbuf_append_str(writer.output, "  ");
    }
}

static void write_newline(SVGWriter& writer) {
    if (writer.params.indent) {
        strbuf_append_char(writer.output, '\n');
    }
}

// Escape XML special characters
static void write_xml_escaped(StrBuf* buf, const char* str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        switch (c) {
            case '&':  strbuf_append_str(buf, "&amp;"); break;
            case '<':  strbuf_append_str(buf, "&lt;"); break;
            case '>':  strbuf_append_str(buf, "&gt;"); break;
            case '"':  strbuf_append_str(buf, "&quot;"); break;
            case '\'': strbuf_append_str(buf, "&apos;"); break;
            default:   strbuf_append_char(buf, c); break;
        }
    }
}

// Escape XML string
static void write_xml_string(StrBuf* buf, const char* str) {
    if (!str) return;
    write_xml_escaped(buf, str, strlen(str));
}

// ============================================================================
// Font Mapping
// ============================================================================

const char* svg_font_family(const char* tex_font_name) {
    if (!tex_font_name) return "serif";

    // Map TeX font names to CSS font families
    if (strncmp(tex_font_name, "cmr", 3) == 0 ||
        strncmp(tex_font_name, "cmbx", 4) == 0) {
        return "'CMU Serif', 'Computer Modern', 'Latin Modern Roman', Georgia, serif";
    }
    if (strncmp(tex_font_name, "cmmi", 4) == 0 ||
        strncmp(tex_font_name, "cmti", 4) == 0) {
        return "'CMU Serif Italic', 'Computer Modern', 'Latin Modern Roman', Georgia, serif";
    }
    if (strncmp(tex_font_name, "cmsy", 4) == 0 ||
        strncmp(tex_font_name, "cmex", 4) == 0) {
        return "'CMU Serif', 'STIX Two Math', 'Computer Modern', serif";
    }
    if (strncmp(tex_font_name, "cmss", 4) == 0) {
        return "'CMU Sans Serif', 'Computer Modern Sans', 'Latin Modern Sans', Arial, sans-serif";
    }
    if (strncmp(tex_font_name, "cmtt", 4) == 0) {
        return "'CMU Typewriter Text', 'Computer Modern Typewriter', 'Latin Modern Mono', monospace";
    }

    return "serif";
}

void svg_color_string(uint32_t color, char* out, size_t out_len) {
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = color & 0xFF;

    if (a == 0) {
        snprintf(out, out_len, "transparent");
    } else if (a == 255) {
        snprintf(out, out_len, "#%02X%02X%02X", r, g, b);
    } else {
        snprintf(out, out_len, "rgba(%d,%d,%d,%.3f)", r, g, b, a / 255.0f);
    }
}

// ============================================================================
// Bounds Computation
// ============================================================================

void svg_compute_bounds(TexNode* root, float& min_x, float& min_y, float& max_x, float& max_y) {
    if (!root) return;

    // Update bounds based on this node
    float node_left = root->x;
    float node_right = root->x + root->width;
    float node_top = root->y - root->height;
    float node_bottom = root->y + root->depth;

    if (node_left < min_x) min_x = node_left;
    if (node_right > max_x) max_x = node_right;
    if (node_top < min_y) min_y = node_top;
    if (node_bottom > max_y) max_y = node_bottom;

    // Recurse to children
    for (TexNode* child = root->first_child; child; child = child->next_sibling) {
        // Compute child absolute position
        float child_abs_x = root->x + child->x;
        float child_abs_y = root->y + child->y;

        // Temporarily set absolute position for bounds computation
        float saved_x = child->x;
        float saved_y = child->y;
        child->x = child_abs_x;
        child->y = child_abs_y;

        svg_compute_bounds(child, min_x, min_y, max_x, max_y);

        // Restore relative position
        child->x = saved_x;
        child->y = saved_y;
    }
}

// ============================================================================
// SVG Writer Initialization
// ============================================================================

bool svg_init(SVGWriter& writer, Arena* arena, const SVGParams& params) {
    writer.arena = arena;
    writer.output = strbuf_new();
    if (!writer.output) {
        log_error("tex_svg_out: failed to create output buffer");
        return false;
    }

    writer.params = params;
    writer.indent_level = 0;
    writer.current_font = nullptr;
    writer.current_size = 0;
    writer.current_color = params.text_color;

    writer.content_min_x = 1e9f;
    writer.content_min_y = 1e9f;
    writer.content_max_x = -1e9f;
    writer.content_max_y = -1e9f;

    return true;
}

// ============================================================================
// SVG Document Structure
// ============================================================================

void svg_write_header(SVGWriter& writer, float width, float height) {
    strbuf_append_str(writer.output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    write_newline(writer);

    // SVG root element
    char buf[256];
    snprintf(buf, sizeof(buf),
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "viewBox=\"0 0 %.2f %.2f\" "
        "width=\"%.2f\" height=\"%.2f\">",
        width, height, width, height);
    strbuf_append_str(writer.output, buf);
    write_newline(writer);

    writer.indent_level++;

    // Title and description
    if (writer.params.include_metadata) {
        if (writer.params.title) {
            write_indent(writer);
            strbuf_append_str(writer.output, "<title>");
            write_xml_string(writer.output, writer.params.title);
            strbuf_append_str(writer.output, "</title>");
            write_newline(writer);
        }
        if (writer.params.description) {
            write_indent(writer);
            strbuf_append_str(writer.output, "<desc>");
            write_xml_string(writer.output, writer.params.description);
            strbuf_append_str(writer.output, "</desc>");
            write_newline(writer);
        }
    }

    // Background
    if (writer.params.background != 0) {
        char color[32];
        svg_color_string(writer.params.background, color, sizeof(color));
        write_indent(writer);
        snprintf(buf, sizeof(buf),
            "<rect width=\"100%%\" height=\"100%%\" fill=\"%s\"/>", color);
        strbuf_append_str(writer.output, buf);
        write_newline(writer);
    }
}

void svg_write_footer(SVGWriter& writer) {
    writer.indent_level--;
    strbuf_append_str(writer.output, "</svg>");
    write_newline(writer);
}

void svg_write_font_styles(SVGWriter& writer) {
    write_indent(writer);
    strbuf_append_str(writer.output, "<defs>");
    write_newline(writer);
    writer.indent_level++;

    write_indent(writer);
    strbuf_append_str(writer.output, "<style type=\"text/css\">");
    write_newline(writer);

    // Font definitions
    strbuf_append_str(writer.output,
        "    .tex-text { font-family: 'CMU Serif', 'Computer Modern', Georgia, serif; }\n"
        "    .tex-math { font-family: 'CMU Serif', 'STIX Two Math', serif; font-style: italic; }\n"
        "    .tex-symbol { font-family: 'CMU Serif', 'STIX Two Math', serif; }\n"
        "    .tex-mono { font-family: 'CMU Typewriter Text', monospace; }\n"
    );

    write_indent(writer);
    strbuf_append_str(writer.output, "</style>");
    write_newline(writer);

    writer.indent_level--;
    write_indent(writer);
    strbuf_append_str(writer.output, "</defs>");
    write_newline(writer);
}

// ============================================================================
// Node Rendering
// ============================================================================

void svg_render_char(SVGWriter& writer, TexNode* node, float x, float y) {
    if (node->node_class != NodeClass::Char && node->node_class != NodeClass::MathChar) {
        return;
    }

    // Get character info
    int32_t codepoint;
    const char* font_name;
    float font_size;

    if (node->node_class == NodeClass::Char) {
        codepoint = node->content.ch.codepoint;
        font_name = node->content.ch.font.name;
        font_size = node->content.ch.font.size_pt;
    } else {
        codepoint = node->content.math_char.codepoint;
        font_name = node->content.math_char.font.name;
        font_size = node->content.math_char.font.size_pt;
    }

    // Map CM character to Unicode if needed
    int32_t unicode = CMToUnicodeMap::map(codepoint, font_name);

    // Get font family
    const char* font_family = writer.params.font_family;
    if (!font_family) {
        font_family = svg_font_family(font_name);
    }

    // Get color
    char color[32];
    svg_color_string(writer.current_color, color, sizeof(color));

    // Build text element
    write_indent(writer);

    char buf[512];
    snprintf(buf, sizeof(buf),
        "<text x=\"%.2f\" y=\"%.2f\" font-family=\"%s\" font-size=\"%.2f\" fill=\"%s\">",
        x * writer.params.scale,
        y * writer.params.scale,
        font_family,
        font_size * writer.params.scale,
        color);
    strbuf_append_str(writer.output, buf);

    // Write character
    if (unicode < 128 && unicode >= 32) {
        // ASCII printable - escape if needed
        char ch[2] = { (char)unicode, '\0' };
        write_xml_string(writer.output, ch);
    } else {
        // Unicode entity
        snprintf(buf, sizeof(buf), "&#x%04X;", unicode);
        strbuf_append_str(writer.output, buf);
    }

    strbuf_append_str(writer.output, "</text>");
    write_newline(writer);

    // Update bounds
    float right = x + node->width;
    float top = y - node->height;
    float bottom = y + node->depth;

    if (x < writer.content_min_x) writer.content_min_x = x;
    if (right > writer.content_max_x) writer.content_max_x = right;
    if (top < writer.content_min_y) writer.content_min_y = top;
    if (bottom > writer.content_max_y) writer.content_max_y = bottom;
}

void svg_render_rule(SVGWriter& writer, TexNode* node, float x, float y) {
    if (node->node_class != NodeClass::Rule) return;

    float width = node->width;
    float height = node->height + node->depth;
    float top = y - node->height;

    // Get color
    char color[32];
    svg_color_string(writer.current_color, color, sizeof(color));

    write_indent(writer);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"%s\"/>",
        x * writer.params.scale,
        top * writer.params.scale,
        width * writer.params.scale,
        height * writer.params.scale,
        color);
    strbuf_append_str(writer.output, buf);
    write_newline(writer);

    // Update bounds
    if (x < writer.content_min_x) writer.content_min_x = x;
    if (x + width > writer.content_max_x) writer.content_max_x = x + width;
    if (top < writer.content_min_y) writer.content_min_y = top;
    if (top + height > writer.content_max_y) writer.content_max_y = top + height;
}

void svg_render_hlist(SVGWriter& writer, TexNode* node, float x, float y) {
    // Render all children at their relative positions
    for (TexNode* child = node->first_child; child; child = child->next_sibling) {
        float child_x = x + child->x;
        float child_y = y + child->y;
        svg_render_node(writer, child, child_x, child_y);
    }
}

void svg_render_vlist(SVGWriter& writer, TexNode* node, float x, float y) {
    // Render all children at their relative positions
    for (TexNode* child = node->first_child; child; child = child->next_sibling) {
        float child_x = x + child->x;
        float child_y = y + child->y;
        svg_render_node(writer, child, child_x, child_y);
    }
}

void svg_render_node(SVGWriter& writer, TexNode* node, float x, float y) {
    if (!node) return;

    switch (node->node_class) {
        case NodeClass::Char:
        case NodeClass::MathChar:
            svg_render_char(writer, node, x, y);
            break;

        case NodeClass::Rule:
            svg_render_rule(writer, node, x, y);
            break;

        case NodeClass::HList:
        case NodeClass::HBox:
            svg_render_hlist(writer, node, x, y);
            break;

        case NodeClass::VList:
        case NodeClass::VBox:
        case NodeClass::VTop:
        case NodeClass::Page:
        case NodeClass::Paragraph:
            svg_render_vlist(writer, node, x, y);
            break;

        case NodeClass::MathList:
            svg_render_hlist(writer, node, x, y);
            break;

        case NodeClass::Fraction:
            // Render numerator, denominator, and rule
            if (node->content.frac.numerator) {
                svg_render_node(writer, node->content.frac.numerator,
                    x + node->content.frac.numerator->x,
                    y + node->content.frac.numerator->y);
            }
            if (node->content.frac.denominator) {
                svg_render_node(writer, node->content.frac.denominator,
                    x + node->content.frac.denominator->x,
                    y + node->content.frac.denominator->y);
            }
            // Fraction bar would be drawn as a rule by the layout
            svg_render_hlist(writer, node, x, y);
            break;

        case NodeClass::Radical:
            if (node->content.radical.radicand) {
                svg_render_node(writer, node->content.radical.radicand,
                    x + node->content.radical.radicand->x,
                    y + node->content.radical.radicand->y);
            }
            if (node->content.radical.degree) {
                svg_render_node(writer, node->content.radical.degree,
                    x + node->content.radical.degree->x,
                    y + node->content.radical.degree->y);
            }
            svg_render_hlist(writer, node, x, y);
            break;

        case NodeClass::Scripts:
            if (node->content.scripts.nucleus) {
                svg_render_node(writer, node->content.scripts.nucleus,
                    x + node->content.scripts.nucleus->x,
                    y + node->content.scripts.nucleus->y);
            }
            if (node->content.scripts.subscript) {
                svg_render_node(writer, node->content.scripts.subscript,
                    x + node->content.scripts.subscript->x,
                    y + node->content.scripts.subscript->y);
            }
            if (node->content.scripts.superscript) {
                svg_render_node(writer, node->content.scripts.superscript,
                    x + node->content.scripts.superscript->x,
                    y + node->content.scripts.superscript->y);
            }
            break;

        case NodeClass::Glue:
        case NodeClass::Kern:
        case NodeClass::Penalty:
            // Invisible nodes - nothing to render
            break;

        case NodeClass::Ligature:
            // Treat like a char node
            svg_render_char(writer, node, x, y);
            break;

        default:
            // For unknown nodes, try rendering children
            svg_render_hlist(writer, node, x, y);
            break;
    }
}

// ============================================================================
// Document Rendering
// ============================================================================

bool svg_write_document(SVGWriter& writer, TexNode* root) {
    if (!root) {
        log_error("tex_svg_out: null root node");
        return false;
    }

    // Compute content bounds
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;

    if (root->width > 0 && root->height > 0) {
        // Use explicit dimensions
        max_x = root->width;
        max_y = root->height + root->depth;
    } else {
        // Compute from children
        svg_compute_bounds(root, min_x, min_y, max_x, max_y);
    }

    // Determine viewport size
    float width = writer.params.viewport_width;
    float height = writer.params.viewport_height;

    if (width <= 0) width = (max_x - min_x) + 20;  // Add margin
    if (height <= 0) height = (max_y - min_y) + 20;

    // Apply scale
    width *= writer.params.scale;
    height *= writer.params.scale;

    // Write header
    svg_write_header(writer, width, height);

    // Write font styles
    svg_write_font_styles(writer);

    // Start content group
    write_indent(writer);

    // Apply offset to center content
    float offset_x = 10 - min_x;
    float offset_y = 10 - min_y + root->height;  // Baseline offset

    char buf[128];
    snprintf(buf, sizeof(buf), "<g transform=\"translate(%.2f, %.2f)\">",
        offset_x * writer.params.scale,
        offset_y * writer.params.scale);
    strbuf_append_str(writer.output, buf);
    write_newline(writer);

    writer.indent_level++;

    // Render content
    svg_render_node(writer, root, 0, 0);

    writer.indent_level--;

    // Close content group
    write_indent(writer);
    strbuf_append_str(writer.output, "</g>");
    write_newline(writer);

    // Write footer
    svg_write_footer(writer);

    log_debug("tex_svg_out: rendered document %.0fx%.0f px", width, height);
    return true;
}

// ============================================================================
// Output Functions
// ============================================================================

const char* svg_get_output(SVGWriter& writer) {
    if (!writer.output) return nullptr;
    return writer.output->str;
}

bool svg_write_to_file(SVGWriter& writer, const char* filename) {
    if (!writer.output || !filename) return false;

    FILE* f = fopen(filename, "w");
    if (!f) {
        log_error("tex_svg_out: failed to open %s for writing", filename);
        return false;
    }

    const char* content = writer.output->str;
    size_t len = writer.output->length;

    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        log_error("tex_svg_out: write error to %s", filename);
        return false;
    }

    log_info("tex_svg_out: wrote %zu bytes to %s", len, filename);
    return true;
}

// ============================================================================
// Convenience Functions
// ============================================================================

bool svg_render_to_file(
    TexNode* root,
    const char* filename,
    const SVGParams* params,
    Arena* arena
) {
    SVGParams p = params ? *params : SVGParams::defaults();

    SVGWriter writer;
    if (!svg_init(writer, arena, p)) {
        return false;
    }

    if (!svg_write_document(writer, root)) {
        return false;
    }

    return svg_write_to_file(writer, filename);
}

const char* svg_render_to_string(
    TexNode* root,
    const SVGParams* params,
    Arena* arena
) {
    SVGParams p = params ? *params : SVGParams::defaults();

    SVGWriter writer;
    if (!svg_init(writer, arena, p)) {
        return nullptr;
    }

    if (!svg_write_document(writer, root)) {
        return nullptr;
    }

    return svg_get_output(writer);
}

// ============================================================================
// Math-Specific SVG Functions (for HTML embedding)
// ============================================================================

void svg_compute_math_bounds(TexNode* math, float* width, float* height, float* depth) {
    if (!math) {
        if (width) *width = 0;
        if (height) *height = 0;
        if (depth) *depth = 0;
        return;
    }

    // Use the node's own dimensions
    if (width) *width = math->width;
    if (height) *height = math->height;
    if (depth) *depth = math->depth;
}

/**
 * Write SVG header for inline math (no XML declaration, compact format).
 */
static void svg_write_inline_header(SVGWriter& writer, float width, float height, float depth) {
    // Total height is height + depth
    float total_height = height + depth;
    
    // ViewBox starts at (0, -height) so baseline is at y=0
    char buf[256];
    snprintf(buf, sizeof(buf),
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "viewBox=\"0 %.2f %.2f %.2f\" "
        "width=\"%.2fpt\" height=\"%.2fpt\" "
        "style=\"vertical-align: %.2fpt;\">",
        -height,                          // viewBox y starts at -height (baseline at 0)
        width, total_height,              // viewBox dimensions
        width, total_height,              // actual dimensions in points
        -depth);                          // CSS vertical-align to align baseline
    strbuf_append_str(writer.output, buf);
    
    if (writer.params.indent) {
        strbuf_append_char(writer.output, '\n');
    }
    
    writer.indent_level++;
}

/**
 * Write compact font styles for inline SVG.
 */
static void svg_write_inline_styles(SVGWriter& writer) {
    write_indent(writer);
    strbuf_append_str(writer.output, "<style>");
    strbuf_append_str(writer.output, 
        ".m{font-family:'CMU Serif','STIX Two Math',serif;font-style:italic}"
        ".s{font-family:'CMU Serif','STIX Two Math',serif}"
    );
    strbuf_append_str(writer.output, "</style>");
    write_newline(writer);
}

const char* svg_render_math_inline(TexNode* math, Arena* arena, const SVGParams* opts) {
    if (!math) {
        log_error("tex_svg_out: null math node for inline render");
        return nullptr;
    }

    // Get dimensions
    float width = math->width;
    float height = math->height;
    float depth = math->depth;

    // Ensure minimum dimensions
    if (width < 1.0f) width = 1.0f;
    if (height + depth < 1.0f) height = 1.0f;

    // Setup writer with compact params
    SVGParams p = opts ? *opts : SVGParams::defaults();
    p.indent = false;           // Compact output
    p.include_metadata = false; // No title/desc for inline
    p.viewport_width = width;
    p.viewport_height = height + depth;

    SVGWriter writer;
    if (!svg_init(writer, arena, p)) {
        return nullptr;
    }

    // Write inline SVG header (no XML declaration)
    svg_write_inline_header(writer, width, height, depth);

    // Write compact styles
    svg_write_inline_styles(writer);

    // Render math content at baseline y=0
    // The viewBox is set up so y=0 is the baseline
    svg_render_node(writer, math, 0, 0);

    // Close SVG
    writer.indent_level--;
    strbuf_append_str(writer.output, "</svg>");

    return svg_get_output(writer);
}

} // namespace tex
