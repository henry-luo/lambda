// tex_html_render.hpp - HTML output for TeX math formulas
//
// Renders TexNode trees to HTML+CSS for web display.
// Uses MathLive-compatible CSS class names for styling.
//
// Reference: MathLive box.ts toMarkup() implementation

#ifndef TEX_HTML_RENDER_HPP
#define TEX_HTML_RENDER_HPP

#include "tex_node.hpp"
#include "lib/arena.h"
#include "lib/strbuf.h"

namespace tex {

// options for HTML rendering
struct HtmlRenderOptions {
    float base_font_size_px = 16.0f;
    const char* class_prefix = "ML";  // MathLive-compatible class prefix
    bool include_styles = true;       // include inline styles
    bool standalone = false;          // wrap in full HTML document with CSS
};

// render TexNode tree to HTML string
// returns HTML markup for the math formula
char* render_texnode_to_html(TexNode* node, Arena* arena);

// render with options
char* render_texnode_to_html(TexNode* node, Arena* arena, const HtmlRenderOptions& opts);

// render to string buffer (for incremental building)
void render_texnode_to_html(TexNode* node, StrBuf* out, const HtmlRenderOptions& opts);

// get the default CSS stylesheet for math rendering
const char* get_math_css_stylesheet();

// generate a standalone HTML document with embedded CSS
char* render_texnode_to_html_document(TexNode* node, Arena* arena, const HtmlRenderOptions& opts);

} // namespace tex

#endif // TEX_HTML_RENDER_HPP
