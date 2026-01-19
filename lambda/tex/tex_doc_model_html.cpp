// tex_doc_model_html.cpp - HTML Output for Document Model
//
// HTML rendering functions for the document model, extracted from
// tex_document_model.cpp for maintainability.

#include "tex_document_model.hpp"
#include "tex_doc_model_internal.hpp"
#include "lib/log.h"
#include <cstring>
#include <cstdio>

// Conditionally include SVG support
#ifndef DOC_MODEL_NO_SVG
#include "tex_svg_out.hpp"
#endif

namespace tex {

// ============================================================================
// SVG Stub (when SVG support is disabled)
// ============================================================================

#ifdef DOC_MODEL_NO_SVG
// Stub function when SVG library is not linked
static const char* svg_render_math_inline(TexNode*, Arena*, void*) {
    return nullptr;
}
struct SVGParams {
    bool indent;
    static SVGParams defaults() { return {false}; }
};
#endif

// ============================================================================
// HTML Utilities
// ============================================================================

void html_escape_append(StrBuf* out, const char* text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        // Check for UTF-8 non-breaking space (U+00A0 = 0xC2 0xA0)
        if (c == 0xC2 && i + 1 < len && (unsigned char)text[i + 1] == 0xA0) {
            strbuf_append_str(out, "&nbsp;");
            i++;  // Skip the second byte
            continue;
        }
        switch (c) {
        case '&':  strbuf_append_str(out, "&amp;"); break;
        case '<':  strbuf_append_str(out, "&lt;"); break;
        case '>':  strbuf_append_str(out, "&gt;"); break;
        case '"':  strbuf_append_str(out, "&quot;"); break;
        case '\'': strbuf_append_str(out, "&#39;"); break;
        default:   strbuf_append_char(out, (char)c); break;
        }
    }
}

void html_indent(StrBuf* out, int depth) {
    for (int i = 0; i < depth; i++) {
        strbuf_append_str(out, "  ");
    }
}

void html_write_default_css(StrBuf* out, const char* prefix) {
    strbuf_append_str(out, "<style>\n");
    
    // Document container
    strbuf_append_format(out, ".%sdocument {\n", prefix);
    strbuf_append_str(out, "  max-width: 800px;\n");
    strbuf_append_str(out, "  margin: 0 auto;\n");
    strbuf_append_str(out, "  padding: 2em;\n");
    strbuf_append_str(out, "  font-family: 'Computer Modern Serif', 'Latin Modern Roman', Georgia, serif;\n");
    strbuf_append_str(out, "  font-size: 12pt;\n");
    strbuf_append_str(out, "  line-height: 1.5;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Headings
    for (int level = 0; level < 6; level++) {
        float sizes[] = {2.0f, 1.7f, 1.4f, 1.2f, 1.1f, 1.0f};
        strbuf_append_format(out, ".%sheading-%d {\n", prefix, level);
        strbuf_append_format(out, "  font-size: %.1fem;\n", sizes[level]);
        strbuf_append_str(out, "  font-weight: bold;\n");
        strbuf_append_format(out, "  margin-top: %.1fem;\n", level == 0 ? 1.5f : 1.2f);
        strbuf_append_format(out, "  margin-bottom: %.1fem;\n", 0.5f);
        strbuf_append_str(out, "}\n\n");
    }
    
    // Paragraph
    strbuf_append_format(out, ".%sparagraph {\n", prefix);
    strbuf_append_str(out, "  text-indent: 1.5em;\n");
    strbuf_append_str(out, "  margin: 0.5em 0;\n");
    strbuf_append_str(out, "}\n\n");
    
    // First paragraph after heading - no indent
    for (int level = 0; level < 6; level++) {
        strbuf_append_format(out, ".%sheading-%d + .%sparagraph {\n", prefix, level, prefix);
        strbuf_append_str(out, "  text-indent: 0;\n");
        strbuf_append_str(out, "}\n\n");
    }
    
    // Lists
    strbuf_append_format(out, ".%slist {\n", prefix);
    strbuf_append_str(out, "  margin: 0.5em 0;\n");
    strbuf_append_str(out, "  padding-left: 2em;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Blockquote / quote environments
    strbuf_append_format(out, ".%sblockquote {\n", prefix);
    strbuf_append_str(out, "  margin: 1em 2em;\n");
    strbuf_append_str(out, "  font-style: italic;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Code blocks
    strbuf_append_format(out, ".%scode-block {\n", prefix);
    strbuf_append_str(out, "  background: #f5f5f5;\n");
    strbuf_append_str(out, "  border: 1px solid #ddd;\n");
    strbuf_append_str(out, "  border-radius: 3px;\n");
    strbuf_append_str(out, "  padding: 1em;\n");
    strbuf_append_str(out, "  overflow-x: auto;\n");
    strbuf_append_str(out, "  font-family: 'Courier New', monospace;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Tables
    strbuf_append_format(out, ".%stable {\n", prefix);
    strbuf_append_str(out, "  border-collapse: collapse;\n");
    strbuf_append_str(out, "  margin: 1em auto;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%stable td, .%stable th {\n", prefix, prefix);
    strbuf_append_str(out, "  border: 1px solid #ddd;\n");
    strbuf_append_str(out, "  padding: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Math
    strbuf_append_format(out, ".%smath-inline {\n", prefix);
    strbuf_append_str(out, "  font-style: italic;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%smath-display {\n", prefix);
    strbuf_append_str(out, "  display: block;\n");
    strbuf_append_str(out, "  text-align: center;\n");
    strbuf_append_str(out, "  margin: 1em 0;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Text styles
    strbuf_append_format(out, ".%ssmallcaps {\n", prefix);
    strbuf_append_str(out, "  font-variant: small-caps;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%ssl {\n", prefix);  // slanted
    strbuf_append_str(out, "  font-style: oblique;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sup {\n", prefix);  // upright
    strbuf_append_str(out, "  font-style: normal;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Font sizes
    strbuf_append_format(out, ".%stiny { font-size: 0.5em; }\n", prefix);
    strbuf_append_format(out, ".%sscriptsize { font-size: 0.7em; }\n", prefix);
    strbuf_append_format(out, ".%sfootnotesize { font-size: 0.8em; }\n", prefix);
    strbuf_append_format(out, ".%ssmall { font-size: 0.9em; }\n", prefix);
    strbuf_append_format(out, ".%snormalsize { font-size: 1em; }\n", prefix);
    strbuf_append_format(out, ".%slarge { font-size: 1.2em; }\n", prefix);
    strbuf_append_format(out, ".%sLarge { font-size: 1.44em; }\n", prefix);
    strbuf_append_format(out, ".%sLARGE { font-size: 1.728em; }\n", prefix);
    strbuf_append_format(out, ".%shuge { font-size: 2.074em; }\n", prefix);
    strbuf_append_format(out, ".%sHuge { font-size: 2.488em; }\n", prefix);
    strbuf_append_str(out, "\n");
    
    // Abstract
    strbuf_append_format(out, ".%sabstract {\n", prefix);
    strbuf_append_str(out, "  margin: 2em auto;\n");
    strbuf_append_str(out, "  max-width: 600px;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sabstract-title {\n", prefix);
    strbuf_append_str(out, "  font-weight: bold;\n");
    strbuf_append_str(out, "  text-align: center;\n");
    strbuf_append_str(out, "  margin-bottom: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Title block
    strbuf_append_format(out, ".%stitle-block {\n", prefix);
    strbuf_append_str(out, "  text-align: center;\n");
    strbuf_append_str(out, "  margin-bottom: 2em;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sdoc-title {\n", prefix);
    strbuf_append_str(out, "  font-size: 1.5em;\n");
    strbuf_append_str(out, "  font-weight: bold;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sdoc-author {\n", prefix);
    strbuf_append_str(out, "  font-size: 1.2em;\n");
    strbuf_append_str(out, "  margin-top: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sdoc-date {\n", prefix);
    strbuf_append_str(out, "  margin-top: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_str(out, "</style>\n");
}

// ============================================================================
// HTML Element Rendering
// ============================================================================

// Helper to check if any ancestor TEXT_SPAN has ITALIC flag
static bool has_italic_ancestor(DocElement* elem) {
    DocElement* parent = elem->parent;
    while (parent) {
        if (parent->type == DocElemType::TEXT_SPAN) {
            if (parent->text.style.has(DocTextStyle::ITALIC)) {
                return true;
            }
        }
        parent = parent->parent;
    }
    return false;
}

// Forward declarations for mutual recursion
static void render_children_html_with_context(DocElement* parent, StrBuf* out, 
                                  const HtmlOutputOptions& opts, int depth, uint16_t inherited_flags);
static void render_children_html(DocElement* parent, StrBuf* out, 
                                  const HtmlOutputOptions& opts, int depth);

// Render a TEXT_SPAN element with inherited style context for emphasis toggling
static void render_text_span_html_with_context(DocElement* elem, StrBuf* out, 
                                   const HtmlOutputOptions& opts, uint16_t inherited_flags) {
    DocTextStyle& style = elem->text.style;
    
    // Resolve EMPHASIS flag: toggle between italic and upright based on context
    // \emph inside italic context -> upright; \emph in upright context -> italic
    uint16_t resolved_flags = style.flags;
    if (style.has(DocTextStyle::EMPHASIS)) {
        // Clear the EMPHASIS flag from resolved
        resolved_flags &= ~DocTextStyle::EMPHASIS;
        // Check if we're already in an italic context (from parent, inherited, or ancestors)
        bool in_italic_context = (inherited_flags & DocTextStyle::ITALIC) != 0 || has_italic_ancestor(elem);
        if (in_italic_context) {
            // In italic context: emphasis means upright
            resolved_flags |= DocTextStyle::UPRIGHT;
        } else {
            // In upright context: emphasis means italic
            resolved_flags |= DocTextStyle::ITALIC;
        }
    }
    
    // Create a temporary style for rendering with resolved flags
    DocTextStyle resolved_style = style;
    resolved_style.flags = resolved_flags;
    
    // Opening tags - use semantic HTML tags
    if (resolved_style.has(DocTextStyle::BOLD))
        strbuf_append_str(out, "<strong>");
    if (resolved_style.has(DocTextStyle::ITALIC))
        strbuf_append_str(out, "<em>");
    if (resolved_style.has(DocTextStyle::MONOSPACE))
        strbuf_append_str(out, "<code>");
    if (resolved_style.has(DocTextStyle::SLANTED))
        strbuf_append_format(out, "<span class=\"%ssl\">", opts.css_class_prefix);
    if (resolved_style.has(DocTextStyle::UPRIGHT))
        strbuf_append_format(out, "<span class=\"%sup\">", opts.css_class_prefix);
    if (resolved_style.has(DocTextStyle::UNDERLINE))
        strbuf_append_str(out, "<u>");
    if (resolved_style.has(DocTextStyle::STRIKEOUT))
        strbuf_append_str(out, "<s>");
    if (resolved_style.has(DocTextStyle::SMALLCAPS))
        strbuf_append_format(out, "<span class=\"%ssmallcaps\">", opts.css_class_prefix);
    if (resolved_style.has(DocTextStyle::SUPERSCRIPT))
        strbuf_append_str(out, "<sup>");
    if (resolved_style.has(DocTextStyle::SUBSCRIPT))
        strbuf_append_str(out, "<sub>");
    // Font size - use class
    const char* size_class = font_size_name_class(resolved_style.font_size_name);
    if (size_class) {
        strbuf_append_format(out, "<span class=\"%s%s\">", opts.css_class_prefix, size_class);
    }
    
    // Content
    if (elem->text.text && elem->text.text_len > 0) {
        html_escape_append(out, elem->text.text, elem->text.text_len);
    }
    
    // Recurse to children with combined flags
    uint16_t child_inherited = inherited_flags | resolved_flags;
    render_children_html_with_context(elem, out, opts, 0, child_inherited);
    
    // Closing tags (reverse order)
    // Close font size first (innermost)
    if (resolved_style.font_size_name != FontSizeName::INHERIT)
        strbuf_append_str(out, "</span>");
    if (resolved_style.has(DocTextStyle::SUBSCRIPT))
        strbuf_append_str(out, "</sub>");
    if (resolved_style.has(DocTextStyle::SUPERSCRIPT))
        strbuf_append_str(out, "</sup>");
    if (resolved_style.has(DocTextStyle::SMALLCAPS))
        strbuf_append_str(out, "</span>");
    if (resolved_style.has(DocTextStyle::STRIKEOUT))
        strbuf_append_str(out, "</s>");
    if (resolved_style.has(DocTextStyle::UNDERLINE))
        strbuf_append_str(out, "</u>");
    if (resolved_style.has(DocTextStyle::UPRIGHT))
        strbuf_append_str(out, "</span>");
    if (resolved_style.has(DocTextStyle::SLANTED))
        strbuf_append_str(out, "</span>");
    if (resolved_style.has(DocTextStyle::MONOSPACE))
        strbuf_append_str(out, "</code>");
    if (resolved_style.has(DocTextStyle::ITALIC))
        strbuf_append_str(out, "</em>");
    if (resolved_style.has(DocTextStyle::BOLD))
        strbuf_append_str(out, "</strong>");
}

// Legacy wrapper that doesn't pass context
static void render_text_span_html(DocElement* elem, StrBuf* out, 
                                   const HtmlOutputOptions& opts) {
    render_text_span_html_with_context(elem, out, opts, 0);
}

static void render_heading_html(DocElement* elem, StrBuf* out,
                                 const HtmlOutputOptions& opts, int depth) {
    // Map level to HTML heading: part(0)->h1, chapter(1)->h2, section(2)->h3, etc.
    int h_level = elem->heading.level + 1;
    if (h_level > 6) h_level = 6;
    
    if (opts.pretty_print) html_indent(out, depth);
    
    // Build heading tag with optional id and class
    if (elem->heading.label) {
        strbuf_append_format(out, "<h%d id=\"%s\" class=\"%sheading-%d\">", 
            h_level, elem->heading.label, opts.css_class_prefix, elem->heading.level);
    } else {
        strbuf_append_format(out, "<h%d class=\"%sheading-%d\">", 
            h_level, opts.css_class_prefix, elem->heading.level);
    }
    
    // Number if present
    if (elem->heading.number && !(elem->flags & DocElement::FLAG_STARRED)) {
        strbuf_append_format(out, "<span class=\"%ssection-number\">%s</span>",
            opts.css_class_prefix, elem->heading.number);
    }
    
    // Title
    if (elem->heading.title) {
        html_escape_append(out, elem->heading.title, strlen(elem->heading.title));
    }
    
    strbuf_append_format(out, "</h%d>", h_level);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_paragraph_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts, int depth) {
    // Skip empty paragraphs (no children or only empty text runs/whitespace)
    if (!elem->first_child) return;
    
    // Check if paragraph has any visible content
    bool has_content = false;
    for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
        if (child->type == DocElemType::TEXT_RUN) {
            if (child->text.text && child->text.text_len > 0) {
                has_content = true;
                break;
            }
        } else if (child->type == DocElemType::TEXT_SPAN) {
            if ((child->text.text && child->text.text_len > 0) || child->first_child) {
                has_content = true;
                break;
            }
        } else if (child->type != DocElemType::SPACE || child->space.is_linebreak) {
            has_content = true;
            break;
        }
    }
    if (!has_content) return;
    
    if (opts.pretty_print) html_indent(out, depth);
    
    // Build up the class string based on flags
    bool has_continue = (elem->flags & DocElement::FLAG_CONTINUE) != 0;
    bool has_noindent = (elem->flags & DocElement::FLAG_NOINDENT) != 0;
    bool has_centered = (elem->flags & DocElement::FLAG_CENTERED) != 0;
    bool has_raggedright = (elem->flags & DocElement::FLAG_FLUSH_LEFT) != 0;
    bool has_raggedleft = (elem->flags & DocElement::FLAG_FLUSH_RIGHT) != 0;
    
    bool has_any_class = has_continue || has_noindent || has_centered || has_raggedright || has_raggedleft;
    
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        // Mode with prefix: always add class="latex-paragraph"
        if (has_continue && has_noindent) {
            strbuf_append_format(out, "<p class=\"%sparagraph continue noindent\">", opts.css_class_prefix);
        } else if (has_continue) {
            strbuf_append_format(out, "<p class=\"%sparagraph continue\">", opts.css_class_prefix);
        } else if (has_noindent) {
            strbuf_append_format(out, "<p class=\"%sparagraph noindent\">", opts.css_class_prefix);
        } else {
            strbuf_append_format(out, "<p class=\"%sparagraph\">", opts.css_class_prefix);
        }
    } else {
        // Hybrid mode (no prefix): only add class when needed
        if (has_any_class) {
            strbuf_append_str(out, "<p class=\"");
            bool first = true;
            if (has_raggedright) {
                strbuf_append_str(out, "raggedright");
                first = false;
            }
            if (has_raggedleft) {
                if (!first) strbuf_append_str(out, " ");
                strbuf_append_str(out, "raggedleft");
                first = false;
            }
            if (has_centered) {
                if (!first) strbuf_append_str(out, " ");
                strbuf_append_str(out, "centering");
                first = false;
            }
            if (has_continue) {
                if (!first) strbuf_append_str(out, " ");
                strbuf_append_str(out, "continue");
                first = false;
            }
            if (has_noindent) {
                if (!first) strbuf_append_str(out, " ");
                strbuf_append_str(out, "noindent");
                first = false;
            }
            strbuf_append_str(out, "\">");
        } else {
            strbuf_append_str(out, "<p>");
        }
    }
    
    render_children_html(elem, out, opts, depth + 1);
    
    strbuf_append_str(out, "</p>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_list_html(DocElement* elem, StrBuf* out,
                              const HtmlOutputOptions& opts, int depth) {
    const char* tag;
    const char* list_class;
    switch (elem->list.list_type) {
    case ListType::ITEMIZE:     tag = "ul"; list_class = "itemize"; break;
    case ListType::ENUMERATE:   tag = "ol"; list_class = "enumerate"; break;
    case ListType::DESCRIPTION: tag = "dl"; list_class = "description"; break;
    default:                    tag = "ul"; list_class = "itemize"; break;
    }
    
    if (opts.pretty_print) html_indent(out, depth);
    
    // Build class list - add "centering" if centered
    const char* centering = (elem->flags & DocElement::FLAG_CENTERED) ? " centering" : "";
    
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        // Mode with prefix: class="latex-list"
        strbuf_append_format(out, "<%s class=\"%slist%s\">", tag, opts.css_class_prefix, centering);
    } else {
        // Hybrid mode (no prefix): class="itemize" / "enumerate" / "description"
        strbuf_append_format(out, "<%s class=\"%s%s\">", tag, list_class, centering);
    }
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "</%s>", tag);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

// Helper to calculate list nesting level by walking up parent chain
// Returns 0 for top-level list, 1 for nested, etc.
static int get_list_nesting_level(DocElement* elem) {
    int level = 0;
    // Start from the item's parent (which is the list it belongs to)
    // and count how many LIST elements are above that
    DocElement* list = elem->parent;
    if (!list || list->type != DocElemType::LIST) return 0;
    
    for (DocElement* p = list->parent; p; p = p->parent) {
        if (p->type == DocElemType::LIST) {
            level++;
        }
    }
    return level;
}

static void render_list_item_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts, int depth,
                                   ListType parent_type) {
    if (opts.pretty_print) html_indent(out, depth);
    
    // Check if item is centered
    const char* centering_class = (elem->flags & DocElement::FLAG_CENTERED) ? " class=\"centering\"" : "";
    
    if (parent_type == ListType::DESCRIPTION) {
        // Description list: <dt>term</dt><dd>content</dd>
        if (elem->list_item.label) {
            strbuf_append_format(out, "<dt%s>", centering_class);
            html_escape_append(out, elem->list_item.label, strlen(elem->list_item.label));
            strbuf_append_str(out, "</dt>");
            if (opts.pretty_print) strbuf_append_str(out, "\n");
            if (opts.pretty_print) html_indent(out, depth);
        }
        strbuf_append_format(out, "<dd%s>", centering_class);
    } else {
        strbuf_append_format(out, "<li%s>", centering_class);
        // Semantic HTML: no bullet/number markup - let CSS handle list styling
    }
    
    // Render children directly
    render_children_html(elem, out, opts, depth + 1);
    
    if (parent_type == ListType::DESCRIPTION) {
        strbuf_append_str(out, "</dd>");
    } else {
        strbuf_append_str(out, "</li>");
    }
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_table_html(DocElement* elem, StrBuf* out,
                               const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<table class=\"%stable\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</table>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_table_row_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "<tr>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</tr>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_table_cell_html(DocElement* elem, StrBuf* out,
                                    const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    
    const char* align_style = "";
    switch (elem->cell.alignment) {
    case 'c': align_style = " style=\"text-align: center;\""; break;
    case 'r': align_style = " style=\"text-align: right;\""; break;
    case 'l': 
    default:  align_style = " style=\"text-align: left;\""; break;
    }
    
    strbuf_append_format(out, "<td%s", align_style);
    if (elem->cell.colspan > 1) {
        strbuf_append_format(out, " colspan=\"%d\"", elem->cell.colspan);
    }
    if (elem->cell.rowspan > 1) {
        strbuf_append_format(out, " rowspan=\"%d\"", elem->cell.rowspan);
    }
    strbuf_append_str(out, ">");
    
    render_children_html(elem, out, opts, depth + 1);
    
    strbuf_append_str(out, "</td>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_math_html(DocElement* elem, StrBuf* out,
                              const HtmlOutputOptions& opts, int depth) {
    bool is_display = (elem->type == DocElemType::MATH_DISPLAY ||
                       elem->type == DocElemType::MATH_EQUATION ||
                       elem->type == DocElemType::MATH_ALIGN);
    
    const char* css_class = is_display ? "math-display" : "math-inline";
    
    // Check if we have a typeset TexNode tree
    bool has_svg = (opts.math_as_svg && elem->math.node != nullptr);
    
    if (is_display) {
        if (opts.pretty_print) html_indent(out, depth);
        strbuf_append_format(out, "<div class=\"%s%s\">", opts.css_class_prefix, css_class);
        if (opts.pretty_print) strbuf_append_str(out, "\n");
        
        if (has_svg) {
            // Render math as inline SVG
            if (opts.pretty_print) html_indent(out, depth + 1);
            
            // Create temporary arena for SVG rendering
            Pool* temp_pool = pool_create();
            Arena* temp_arena = arena_create_default(temp_pool);
            
            SVGParams svg_params = SVGParams::defaults();
            svg_params.indent = false;  // Compact for inline
            
            const char* svg = svg_render_math_inline(elem->math.node, temp_arena, &svg_params);
            if (svg) {
                strbuf_append_str(out, svg);
            }
            
            arena_destroy(temp_arena);
            pool_destroy(temp_pool);
            
            if (opts.pretty_print) strbuf_append_str(out, "\n");
        } else if (elem->math.latex_src) {
            // Fallback: output LaTeX source in a comment
            if (opts.pretty_print) html_indent(out, depth + 1);
            strbuf_append_str(out, "<span class=\"");
            strbuf_append_str(out, opts.css_class_prefix);
            strbuf_append_str(out, "math-fallback\">");
            html_escape_append(out, elem->math.latex_src, strlen(elem->math.latex_src));
            strbuf_append_str(out, "</span>");
            if (opts.pretty_print) strbuf_append_str(out, "\n");
        }
        
        // Equation number
        if (elem->math.number) {
            if (opts.pretty_print) html_indent(out, depth + 1);
            strbuf_append_format(out, "<span class=\"%seq-number\">(%s)</span>",
                opts.css_class_prefix, elem->math.number);
            if (opts.pretty_print) strbuf_append_str(out, "\n");
        }
        
        if (opts.pretty_print) html_indent(out, depth);
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
    } else {
        // Inline math
        strbuf_append_format(out, "<span class=\"%s%s\">", opts.css_class_prefix, css_class);
        
        if (has_svg) {
            // Render math as inline SVG
            Pool* temp_pool = pool_create();
            Arena* temp_arena = arena_create_default(temp_pool);
            
            SVGParams svg_params = SVGParams::defaults();
            svg_params.indent = false;
            
            const char* svg = svg_render_math_inline(elem->math.node, temp_arena, &svg_params);
            if (svg) {
                strbuf_append_str(out, svg);
            }
            
            arena_destroy(temp_arena);
            pool_destroy(temp_pool);
        } else if (elem->math.latex_src) {
            // Fallback: output escaped LaTeX
            html_escape_append(out, elem->math.latex_src, strlen(elem->math.latex_src));
        }
        
        strbuf_append_str(out, "</span>");
    }
}

static void render_link_html(DocElement* elem, StrBuf* out,
                              const HtmlOutputOptions& opts) {
    strbuf_append_str(out, "<a href=\"");
    if (elem->link.href) {
        html_escape_append(out, elem->link.href, strlen(elem->link.href));
    }
    strbuf_append_str(out, "\">");
    
    if (elem->link.link_text) {
        html_escape_append(out, elem->link.link_text, strlen(elem->link.link_text));
    }
    
    render_children_html(elem, out, opts, 0);
    
    strbuf_append_str(out, "</a>");
}

static void render_image_html(DocElement* elem, StrBuf* out,
                               const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    
    strbuf_append_str(out, "<img src=\"");
    if (elem->image.src) {
        html_escape_append(out, elem->image.src, strlen(elem->image.src));
    }
    strbuf_append_str(out, "\"");
    
    if (elem->image.width > 0) {
        strbuf_append_format(out, " width=\"%.0f\"", elem->image.width);
    }
    if (elem->image.height > 0) {
        strbuf_append_format(out, " height=\"%.0f\"", elem->image.height);
    }
    if (elem->image.alt) {
        strbuf_append_str(out, " alt=\"");
        html_escape_append(out, elem->image.alt, strlen(elem->image.alt));
        strbuf_append_str(out, "\"");
    }
    
    strbuf_append_str(out, " />");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_figure_html(DocElement* elem, StrBuf* out,
                                const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<figure class=\"%sfigure\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</figure>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_blockquote_html(DocElement* elem, StrBuf* out,
                                    const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    
    // Get the environment name for the class (quote, quotation, verse)
    const char* env_name = elem->alignment.env_name;
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        // Mode with prefix: class="latex-blockquote" or specific name
        strbuf_append_format(out, "<blockquote class=\"%sblockquote\">", opts.css_class_prefix);
    } else {
        // Hybrid mode: use specific class name (quote, quotation, verse)
        if (env_name && (strcmp(env_name, "quote") == 0 || 
                         strcmp(env_name, "quotation") == 0 ||
                         strcmp(env_name, "verse") == 0)) {
            strbuf_append_format(out, "<blockquote class=\"%s\">", env_name);
        } else {
            strbuf_append_str(out, "<blockquote class=\"quote\">");
        }
    }
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</blockquote>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_code_block_html(DocElement* elem, StrBuf* out,
                                    const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<pre class=\"%scode-block\"><code>", opts.css_class_prefix);
    
    // For code blocks, render text directly without escaping newlines
    if (elem->text.text && elem->text.text_len > 0) {
        html_escape_append(out, elem->text.text, elem->text.text_len);
    }
    render_children_html(elem, out, opts, depth + 1);
    
    strbuf_append_str(out, "</code></pre>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_cross_ref_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts) {
    strbuf_append_str(out, "<a href=\"#");
    if (elem->ref.ref_label) {
        html_escape_append(out, elem->ref.ref_label, strlen(elem->ref.ref_label));
    }
    strbuf_append_str(out, "\">");
    
    if (elem->ref.ref_text) {
        html_escape_append(out, elem->ref.ref_text, strlen(elem->ref.ref_text));
    }
    
    strbuf_append_str(out, "</a>");
}

static void render_citation_html(DocElement* elem, StrBuf* out,
                                  const HtmlOutputOptions& opts) {
    strbuf_append_str(out, "<cite>");
    if (elem->citation.cite_text) {
        html_escape_append(out, elem->citation.cite_text, strlen(elem->citation.cite_text));
    }
    strbuf_append_str(out, "</cite>");
}

static void render_footnote_html(DocElement* elem, StrBuf* out,
                                  const HtmlOutputOptions& opts) {
    strbuf_append_format(out, "<sup class=\"%sfootnote\"><a href=\"#fn%d\">[%d]</a></sup>",
        opts.css_class_prefix, elem->footnote.footnote_number, elem->footnote.footnote_number);
}

static void render_abstract_html(DocElement* elem, StrBuf* out,
                                  const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<div class=\"%sabstract\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    if (opts.pretty_print) html_indent(out, depth + 1);
    strbuf_append_format(out, "<div class=\"%sabstract-title\">Abstract</div>", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</div>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_title_block_html(DocElement* elem, StrBuf* out,
                                     const HtmlOutputOptions& opts, int depth,
                                     TexDocumentModel* doc) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<div class=\"%stitle-block\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    if (doc && doc->title) {
        if (opts.pretty_print) html_indent(out, depth + 1);
        strbuf_append_format(out, "<div class=\"%sdoc-title\">", opts.css_class_prefix);
        html_escape_append(out, doc->title, strlen(doc->title));
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
    }
    
    if (doc && doc->author) {
        if (opts.pretty_print) html_indent(out, depth + 1);
        strbuf_append_format(out, "<div class=\"%sdoc-author\">", opts.css_class_prefix);
        html_escape_append(out, doc->author, strlen(doc->author));
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
    }
    
    if (doc && doc->date) {
        if (opts.pretty_print) html_indent(out, depth + 1);
        strbuf_append_format(out, "<div class=\"%sdoc-date\">", opts.css_class_prefix);
        html_escape_append(out, doc->date, strlen(doc->date));
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
    }
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</div>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_children_html(DocElement* parent, StrBuf* out,
                                  const HtmlOutputOptions& opts, int depth) {
    for (DocElement* child = parent->first_child; child; child = child->next_sibling) {
        doc_element_to_html(child, out, opts, depth);
    }
}

// Forward declaration for context-aware element rendering
static void doc_element_to_html_with_context(DocElement* elem, StrBuf* out,
                          const HtmlOutputOptions& opts, int depth, uint16_t inherited_flags);

// Context-aware version that propagates inherited style flags for emphasis toggling
static void render_children_html_with_context(DocElement* parent, StrBuf* out,
                                  const HtmlOutputOptions& opts, int depth, uint16_t inherited_flags) {
    for (DocElement* child = parent->first_child; child; child = child->next_sibling) {
        doc_element_to_html_with_context(child, out, opts, depth, inherited_flags);
    }
}

// Helper to check if an element is inline content
// Exported for use by model building code
bool is_inline_element(DocElement* elem) {
    if (!elem) return false;
    switch (elem->type) {
    case DocElemType::TEXT_RUN:
    case DocElemType::TEXT_SPAN:
    case DocElemType::SPACE:
    case DocElemType::RAW_HTML:  // inline HTML like logos
    case DocElemType::CROSS_REF: // cross-references are inline
        return true;
    default:
        return false;
    }
}

// Context-aware element rendering that handles emphasis toggling
static void doc_element_to_html_with_context(DocElement* elem, StrBuf* out,
                          const HtmlOutputOptions& opts, int depth, uint16_t inherited_flags) {
    if (!elem) return;
    
    switch (elem->type) {
    case DocElemType::TEXT_SPAN:
        render_text_span_html_with_context(elem, out, opts, inherited_flags);
        break;
        
    case DocElemType::TEXT_RUN:
        if (elem->text.text && elem->text.text_len > 0) {
            // VERBATIM flag skips typographic transformations (for macro output, counter output, etc.)
            if (elem->text.style.has(DocTextStyle::VERBATIM)) {
                html_escape_append(out, elem->text.text, elem->text.text_len);
            } else {
                bool in_monospace = elem->text.style.has(DocTextStyle::MONOSPACE);
                html_escape_append_transformed(out, elem->text.text, elem->text.text_len, in_monospace);
            }
        }
        break;
        
    default:
        // For all other elements, fall back to the regular renderer
        doc_element_to_html(elem, out, opts, depth);
        break;
    }
}

void doc_element_to_html(DocElement* elem, StrBuf* out,
                          const HtmlOutputOptions& opts, int depth) {
    if (!elem) return;
    
    switch (elem->type) {
    case DocElemType::DOCUMENT:
        render_children_html(elem, out, opts, depth);
        break;
        
    case DocElemType::TEXT_SPAN:
        render_text_span_html(elem, out, opts);
        break;
        
    case DocElemType::TEXT_RUN:
        if (elem->text.text && elem->text.text_len > 0) {
            // VERBATIM flag skips typographic transformations (for macro output, counter output, etc.)
            if (elem->text.style.has(DocTextStyle::VERBATIM)) {
                html_escape_append(out, elem->text.text, elem->text.text_len);
            } else {
                // Check if we're in monospace context (texttt style)
                bool in_monospace = elem->text.style.has(DocTextStyle::MONOSPACE);
                html_escape_append_transformed(out, elem->text.text, elem->text.text_len, in_monospace);
            }
        }
        break;
        
    case DocElemType::HEADING:
        render_heading_html(elem, out, opts, depth);
        break;
        
    case DocElemType::PARAGRAPH:
        render_paragraph_html(elem, out, opts, depth);
        break;
        
    case DocElemType::LIST:
        render_list_html(elem, out, opts, depth);
        break;
        
    case DocElemType::LIST_ITEM: {
        // Determine parent list type
        ListType parent_type = ListType::ITEMIZE;
        if (elem->parent && elem->parent->type == DocElemType::LIST) {
            parent_type = elem->parent->list.list_type;
        }
        render_list_item_html(elem, out, opts, depth, parent_type);
        break;
    }
        
    case DocElemType::TABLE:
        render_table_html(elem, out, opts, depth);
        break;
        
    case DocElemType::TABLE_ROW:
        render_table_row_html(elem, out, opts, depth);
        break;
        
    case DocElemType::TABLE_CELL:
        render_table_cell_html(elem, out, opts, depth);
        break;
        
    case DocElemType::MATH_INLINE:
    case DocElemType::MATH_DISPLAY:
    case DocElemType::MATH_EQUATION:
    case DocElemType::MATH_ALIGN:
        render_math_html(elem, out, opts, depth);
        break;
        
    case DocElemType::LINK:
        render_link_html(elem, out, opts);
        break;
        
    case DocElemType::IMAGE:
        render_image_html(elem, out, opts, depth);
        break;
        
    case DocElemType::FIGURE:
        render_figure_html(elem, out, opts, depth);
        break;
        
    case DocElemType::BLOCKQUOTE:
        render_blockquote_html(elem, out, opts, depth);
        break;
        
    case DocElemType::CODE_BLOCK:
        render_code_block_html(elem, out, opts, depth);
        break;
        
    case DocElemType::ALIGNMENT: {
        // Determine alignment class from env_name or flags
        const char* align_class = "list";
        bool use_list_prefix = (opts.css_class_prefix && opts.css_class_prefix[0]);
        bool is_quote_env = false;
        if (elem->alignment.env_name) {
            // Check if this is a quote-like environment
            is_quote_env = (strcmp(elem->alignment.env_name, "quote") == 0 ||
                           strcmp(elem->alignment.env_name, "quotation") == 0 ||
                           strcmp(elem->alignment.env_name, "verse") == 0);
            // Use stored environment name: "list quote", "list quotation", "list verse", etc.
            static char class_buf[64];
            if (use_list_prefix) {
                snprintf(class_buf, sizeof(class_buf), "list %s", elem->alignment.env_name);
            } else {
                snprintf(class_buf, sizeof(class_buf), "%s", elem->alignment.env_name);
            }
            align_class = class_buf;
        } else if (elem->flags & DocElement::FLAG_CENTERED) {
            align_class = use_list_prefix ? "list center" : "center";
        } else if (elem->flags & DocElement::FLAG_FLUSH_LEFT) {
            align_class = use_list_prefix ? "list flushleft" : "flushleft";
        } else if (elem->flags & DocElement::FLAG_FLUSH_RIGHT) {
            align_class = use_list_prefix ? "list flushright" : "flushright";
        }
        // Use <blockquote> for quote environments
        if (is_quote_env) {
            strbuf_append_format(out, "<blockquote class=\"%s\">", align_class);
            if (opts.pretty_print) strbuf_append_str(out, "\n");
            render_children_html(elem, out, opts, depth + 1);
            strbuf_append_str(out, "</blockquote>");
        } else {
            strbuf_append_format(out, "<div class=\"%s\">", align_class);
            if (opts.pretty_print) strbuf_append_str(out, "\n");
            render_children_html(elem, out, opts, depth + 1);
            strbuf_append_str(out, "</div>");
        }
        if (opts.pretty_print) strbuf_append_str(out, "\n");
        break;
    }
        
    case DocElemType::CROSS_REF:
        render_cross_ref_html(elem, out, opts);
        break;
        
    case DocElemType::CITATION:
        render_citation_html(elem, out, opts);
        break;
        
    case DocElemType::FOOTNOTE:
        render_footnote_html(elem, out, opts);
        break;
        
    case DocElemType::ABSTRACT:
        render_abstract_html(elem, out, opts, depth);
        break;
        
    case DocElemType::TITLE_BLOCK:
        render_title_block_html(elem, out, opts, depth, nullptr);
        break;
        
    case DocElemType::SECTION:
        render_children_html(elem, out, opts, depth);
        break;
        
    case DocElemType::SPACE:
        if (elem->space.is_linebreak) {
            strbuf_append_str(out, "<br>");
            if (opts.pretty_print) strbuf_append_str(out, "\n");
        } else {
            strbuf_append_str(out, " ");
        }
        break;
        
    case DocElemType::RAW_HTML:
        if (elem->raw.raw_content && elem->raw.raw_len > 0) {
            strbuf_append_str_n(out, elem->raw.raw_content, elem->raw.raw_len);
        }
        break;
        
    case DocElemType::RAW_LATEX:
        // Skip raw LaTeX in HTML output
        strbuf_append_str(out, "<!-- LaTeX: ");
        if (elem->raw.raw_content && elem->raw.raw_len > 0) {
            html_escape_append(out, elem->raw.raw_content, elem->raw.raw_len);
        }
        strbuf_append_str(out, " -->");
        break;
        
    case DocElemType::ERROR:
        strbuf_append_str(out, "<span class=\"error\">[ERROR]</span>");
        break;
        
    default:
        log_debug("doc_element_to_html: unhandled type %s", doc_elem_type_name(elem->type));
        break;
    }
}

// ============================================================================
// Document to HTML
// ============================================================================

bool doc_model_to_html(TexDocumentModel* doc, StrBuf* output, const HtmlOutputOptions& opts) {
    if (!doc || !output) return false;
    
    // HTML header
    if (opts.standalone) {
        strbuf_append_str(output, "<!DOCTYPE html>\n");
        strbuf_append_format(output, "<html lang=\"%s\">\n", opts.lang);
        strbuf_append_str(output, "<head>\n");
        strbuf_append_str(output, "  <meta charset=\"UTF-8\">\n");
        strbuf_append_str(output, "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
        
        // Title
        if (doc->title) {
            strbuf_append_str(output, "  <title>");
            html_escape_append(output, doc->title, strlen(doc->title));
            strbuf_append_str(output, "</title>\n");
        } else {
            strbuf_append_str(output, "  <title>Document</title>\n");
        }
        
        // Web fonts
        if (opts.font_mode == HtmlOutputOptions::FONT_WEBFONT) {
            strbuf_append_str(output, "  <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/computer-modern@0.1.2/cmsans.min.css\">\n");
            strbuf_append_str(output, "  <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/computer-modern@0.1.2/cmserif.min.css\">\n");
        }
        
        // CSS
        if (opts.include_css) {
            html_write_default_css(output, opts.css_class_prefix);
        }
        
        strbuf_append_str(output, "</head>\n");
        strbuf_append_str(output, "<body>\n");
    }
    
    // Document container
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        // Mode with prefix: <article class="latex-document latex-article">
        strbuf_append_format(output, "<article class=\"%sdocument %s%s\">\n", 
            opts.css_class_prefix, opts.css_class_prefix, doc->document_class);
    } else {
        // Hybrid mode: <article class="latex-document">
        strbuf_append_str(output, "<article class=\"latex-document\">");
    }
    
    // Title block
    if (doc->title || doc->author || doc->date) {
        strbuf_append_format(output, "  <header class=\"%stitle-block\">\n", opts.css_class_prefix);
        if (doc->title) {
            strbuf_append_format(output, "    <h1 class=\"%sdoc-title\">", opts.css_class_prefix);
            html_escape_append(output, doc->title, strlen(doc->title));
            strbuf_append_str(output, "</h1>\n");
        }
        if (doc->author) {
            strbuf_append_format(output, "    <div class=\"%sdoc-author\">", opts.css_class_prefix);
            html_escape_append(output, doc->author, strlen(doc->author));
            strbuf_append_str(output, "</div>\n");
        }
        if (doc->date) {
            strbuf_append_format(output, "    <div class=\"%sdoc-date\">", opts.css_class_prefix);
            html_escape_append(output, doc->date, strlen(doc->date));
            strbuf_append_str(output, "</div>\n");
        }
        strbuf_append_str(output, "  </header>\n");
    }
    
    // Document content
    if (doc->root) {
        doc_element_to_html(doc->root, output, opts, 1);
    }
    
    // Close document container
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        strbuf_append_str(output, "</article>\n");
    } else {
        // Hybrid mode: no trailing newline for compact output
        strbuf_append_str(output, "</article>");
    }
    
    // HTML footer
    if (opts.standalone) {
        strbuf_append_str(output, "</body>\n");
        strbuf_append_str(output, "</html>\n");
    }
    
    return true;
}

} // namespace tex
