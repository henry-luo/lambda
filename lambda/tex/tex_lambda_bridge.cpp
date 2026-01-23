// tex_lambda_bridge.cpp - Implementation of Lambda-to-TeX document conversion
//
// This file implements the bridge between Lambda's document representation
// and the TeX typesetting pipeline.

#include "tex_lambda_bridge.hpp"
#include "tex_hlist.hpp"
#include "tex_linebreak.hpp"
#include "tex_vlist.hpp"
#include "tex_pagebreak.hpp"
#include "tex_math_bridge.hpp"
#include "tex_hyphen.hpp"
#include "../../lib/log.h"
#include "../../lib/stringbuf.h"
#include "../../lib/mempool.h"
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace tex {

// ============================================================================
// DocumentContext Implementation
// ============================================================================

DocumentContext DocumentContext::create(Arena* arena, TFMFontManager* fonts) {
    // Default: US Letter (8.5 x 11 inches), 1 inch margins
    return create(arena, fonts, 612.0f, 792.0f, 72.0f, 72.0f);
}

DocumentContext DocumentContext::create(Arena* arena, TFMFontManager* fonts,
                                          float page_w, float page_h,
                                          float margin_lr, float margin_tb) {
    DocumentContext ctx = {};
    ctx.arena = arena;
    ctx.fonts = fonts;

    // Page dimensions
    ctx.page_width = page_w;
    ctx.page_height = page_h;
    ctx.margin_left = margin_lr;
    ctx.margin_right = margin_lr;
    ctx.margin_top = margin_tb;
    ctx.margin_bottom = margin_tb;

    // Text area
    ctx.text_width = page_w - margin_lr * 2;
    ctx.text_height = page_h - margin_tb * 2;

    // Typography defaults
    ctx.base_size_pt = 10.0f;
    ctx.leading = 1.2f;
    ctx.parindent = 20.0f;     // About 2em at 10pt
    ctx.parskip = 0.0f;        // Traditional TeX: no space between paragraphs

    // Set up fonts
    ctx.roman_font = FontSpec("cmr10", ctx.base_size_pt, nullptr, 0);
    ctx.italic_font = FontSpec("cmti10", ctx.base_size_pt, nullptr, 0);
    ctx.bold_font = FontSpec("cmbx10", ctx.base_size_pt, nullptr, 0);
    ctx.mono_font = FontSpec("cmtt10", ctx.base_size_pt, nullptr, 0);

    // Get TFM fonts (only if font manager is available)
    // fonts may be null for HTML-only document model generation
    if (fonts) {
        ctx.roman_tfm = fonts->get_font("cmr10");
        ctx.italic_tfm = fonts->get_font("cmti10");
        ctx.bold_tfm = fonts->get_font("cmbx10");
        ctx.mono_tfm = fonts->get_font("cmtt10");

        // Use roman as fallback
        if (!ctx.italic_tfm) ctx.italic_tfm = ctx.roman_tfm;
        if (!ctx.bold_tfm) ctx.bold_tfm = ctx.roman_tfm;
        if (!ctx.mono_tfm) ctx.mono_tfm = ctx.roman_tfm;
    } else {
        ctx.roman_tfm = nullptr;
        ctx.italic_tfm = nullptr;
        ctx.bold_tfm = nullptr;
        ctx.mono_tfm = nullptr;
    }

    // Initialize hyphenation
    ctx.hyphenator = get_us_english_hyphenator(arena);

    return ctx;
}

FontSpec DocumentContext::current_font() const {
    switch (format.style) {
        case TextStyle::Italic:
            return italic_font;
        case TextStyle::Bold:
            return bold_font;
        case TextStyle::BoldItalic:
            return bold_font;  // TODO: cmbi font
        case TextStyle::Monospace:
            return mono_font;
        case TextStyle::SmallCaps:
            return roman_font;  // TODO: cmcsc font
        case TextStyle::Roman:
        default:
            return roman_font;
    }
}

TFMFont* DocumentContext::current_tfm() const {
    switch (format.style) {
        case TextStyle::Italic:
            return italic_tfm;
        case TextStyle::Bold:
            return bold_tfm;
        case TextStyle::BoldItalic:
            return bold_tfm;
        case TextStyle::Monospace:
            return mono_tfm;
        case TextStyle::SmallCaps:
        case TextStyle::Roman:
        default:
            return roman_tfm;
    }
}

LineBreakParams DocumentContext::line_break_params() const {
    LineBreakParams params = LineBreakParams::defaults();
    params.hsize = text_width;
    params.tolerance = 10000.0f;  // High tolerance to accept looser lines
    params.pretolerance = 1000.0f;  // Also increase pretolerance
    params.line_penalty = 10.0f;
    params.hyphen_penalty = 50.0f;
    params.emergency_stretch = 50.0f;  // Allow emergency stretch
    return params;
}

float DocumentContext::baseline_skip() const {
    return base_size_pt * leading;
}

MathContext DocumentContext::math_context() const {
    return MathContext::create(arena, fonts, base_size_pt);
}

// ============================================================================
// Helper Functions
// ============================================================================

// Check if tag matches (case-insensitive)
static bool tag_matches(const char* tag, const char* expected) {
    if (!tag || !expected) return false;
    while (*tag && *expected) {
        if (tolower(*tag) != tolower(*expected)) return false;
        tag++;
        expected++;
    }
    return *tag == '\0' && *expected == '\0';
}

// Get heading level from tag (h1 -> 1, h2 -> 2, etc.)
static int get_heading_level(const char* tag) {
    if (!tag) return 0;
    if (tag[0] == 'h' || tag[0] == 'H') {
        if (tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0') {
            return tag[1] - '0';
        }
    }
    return 0;
}

// Check if element is a block element
static bool is_block_element(const char* tag) {
    if (!tag) return false;
    return tag_matches(tag, "p") ||
           tag_matches(tag, "div") ||
           tag_matches(tag, "section") ||
           tag_matches(tag, "article") ||
           get_heading_level(tag) > 0 ||
           tag_matches(tag, "ul") ||
           tag_matches(tag, "ol") ||
           tag_matches(tag, "li") ||
           tag_matches(tag, "blockquote") ||
           tag_matches(tag, "pre") ||
           tag_matches(tag, "code") ||
           tag_matches(tag, "table") ||
           tag_matches(tag, "hr") ||
           tag_matches(tag, "math");
}

// Transfer nodes from source list to target (clears source)
static void transfer_nodes(TexNode* target, TexNode* source) {
    if (!target || !source || !source->first_child) return;

    for (TexNode* n = source->first_child; n; ) {
        TexNode* next = n->next_sibling;
        n->prev_sibling = nullptr;
        n->next_sibling = nullptr;
        n->parent = nullptr;
        target->append_child(n);
        n = next;
    }
    source->first_child = nullptr;
    source->last_child = nullptr;
}

// ============================================================================
// HListContext Helper for current style
// ============================================================================

static HListContext make_hlist_ctx(DocumentContext& ctx) {
    HListContext hctx(ctx.arena, ctx.fonts);
    hctx.current_tfm = ctx.current_tfm();
    hctx.current_font = ctx.current_font();
    hctx.apply_ligatures = true;
    hctx.apply_kerning = true;
    return hctx;
}

// ============================================================================
// Text Processing
// ============================================================================

// Convert text string to HList using current font
static TexNode* build_text_hlist(const char* text, size_t len, DocumentContext& ctx) {
    if (!text || len == 0) {
        return make_hlist(ctx.arena);
    }

    TFMFont* tfm = ctx.current_tfm();
    if (!tfm) {
        log_error("lambda_bridge: no TFM font available");
        return make_hlist(ctx.arena);
    }

    HListContext hctx = make_hlist_ctx(ctx);
    return text_to_hlist(text, len, hctx);
}

// Process text with inline math detection ($...$)
static TexNode* build_text_with_math(const char* text, size_t len, DocumentContext& ctx) {
    if (!text || len == 0) {
        return make_hlist(ctx.arena);
    }

    // Check for $ signs
    bool has_math = false;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '$') {
            has_math = true;
            break;
        }
    }

    if (!has_math) {
        return build_text_hlist(text, len, ctx);
    }

    // Process with math detection
    MathContext math_ctx = ctx.math_context();
    return process_text_with_math(text, len, math_ctx, ctx.fonts);
}

// ============================================================================
// Inline Content Conversion
// ============================================================================

// Forward declaration
static TexNode* convert_inline_content(const ItemReader& content, DocumentContext& ctx, Pool* pool);

// Append text to an existing HList
static void append_text_to_hlist(TexNode* hlist, const char* text, size_t len, DocumentContext& ctx) {
    if (!hlist || !text || len == 0) return;

    TexNode* text_nodes = build_text_hlist(text, len, ctx);
    if (text_nodes) {
        transfer_nodes(hlist, text_nodes);
    }
}

// Process em/i element - switch to italic
static void append_emphasis(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx, Pool* pool) {
    TextStyle saved = ctx.format.style;

    if (ctx.format.style == TextStyle::Bold) {
        ctx.format.style = TextStyle::BoldItalic;
    } else {
        ctx.format.style = TextStyle::Italic;
    }

    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        TexNode* child_nodes = convert_inline_content(child, ctx, pool);
        if (child_nodes) {
            transfer_nodes(hlist, child_nodes);
        }
    }

    ctx.format.style = saved;
}

// Process strong/b element - switch to bold
static void append_strong(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx, Pool* pool) {
    TextStyle saved = ctx.format.style;

    if (ctx.format.style == TextStyle::Italic) {
        ctx.format.style = TextStyle::BoldItalic;
    } else {
        ctx.format.style = TextStyle::Bold;
    }

    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        TexNode* child_nodes = convert_inline_content(child, ctx, pool);
        if (child_nodes) {
            transfer_nodes(hlist, child_nodes);
        }
    }

    ctx.format.style = saved;
}

// Process code element - switch to monospace
static void append_code(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx, Pool* pool) {
    TextStyle saved = ctx.format.style;
    ctx.format.style = TextStyle::Monospace;

    // Get text content from element
    StringBuf* sb = stringbuf_new(pool);
    elem.textContent(sb);

    if (sb->length > 0 && sb->str) {
        append_text_to_hlist(hlist, sb->str->chars, sb->length, ctx);
    }

    stringbuf_free(sb);
    ctx.format.style = saved;
}

// Process link element - just render the text for now
static void append_link(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx, Pool* pool) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        TexNode* child_nodes = convert_inline_content(child, ctx, pool);
        if (child_nodes) {
            transfer_nodes(hlist, child_nodes);
        }
    }
}

// Process inline math element
static void append_inline_math(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx, Pool* pool) {
    StringBuf* sb = stringbuf_new(pool);
    elem.textContent(sb);

    if (sb->length > 0 && sb->str) {
        MathContext math_ctx = ctx.math_context();
        math_ctx.style = MathStyle::Text;  // Inline math

        TexNode* math_hbox = typeset_latex_math(sb->str->chars, sb->length, math_ctx);
        if (math_hbox) {
            hlist->append_child(math_hbox);
        }
    }

    stringbuf_free(sb);
}

// Convert inline content (text or inline element) to HList nodes
static TexNode* convert_inline_content(const ItemReader& content, DocumentContext& ctx, Pool* pool) {
    TexNode* hlist = make_hlist(ctx.arena);

    if (content.isString()) {
        const char* str = content.cstring();
        if (str) {
            size_t len = strlen(str);
            TexNode* text_nodes = build_text_with_math(str, len, ctx);
            if (text_nodes) {
                transfer_nodes(hlist, text_nodes);
            }
        }
    } else if (content.isElement()) {
        ElementReader elem = content.asElement();
        const char* tag = elem.tagName();

        if (tag_matches(tag, "em") || tag_matches(tag, "i")) {
            append_emphasis(hlist, elem, ctx, pool);
        } else if (tag_matches(tag, "strong") || tag_matches(tag, "b")) {
            append_strong(hlist, elem, ctx, pool);
        } else if (tag_matches(tag, "code")) {
            append_code(hlist, elem, ctx, pool);
        } else if (tag_matches(tag, "a")) {
            append_link(hlist, elem, ctx, pool);
        } else if (tag_matches(tag, "math") || tag_matches(tag, "span")) {
            // Check if this is inline math
            const char* cls = elem.get_attr_string("class");
            if (cls && (strstr(cls, "math") || strstr(cls, "katex"))) {
                append_inline_math(hlist, elem, ctx, pool);
            } else {
                // Generic inline element - recurse on children
                auto iter = elem.children();
                ItemReader child;
                while (iter.next(&child)) {
                    TexNode* child_nodes = convert_inline_content(child, ctx, pool);
                    if (child_nodes) {
                        transfer_nodes(hlist, child_nodes);
                    }
                }
            }
        } else {
            // Unknown inline element - recurse on children
            auto iter = elem.children();
            ItemReader child;
            while (iter.next(&child)) {
                TexNode* child_nodes = convert_inline_content(child, ctx, pool);
                if (child_nodes) {
                    transfer_nodes(hlist, child_nodes);
                }
            }
        }
    }

    return hlist;
}

// Build HList from all children of an element
static TexNode* build_inline_hlist(const ElementReader& elem, DocumentContext& ctx, Pool* pool) {
    TexNode* hlist = make_hlist(ctx.arena);

    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        TexNode* child_nodes = convert_inline_content(child, ctx, pool);
        if (child_nodes) {
            transfer_nodes(hlist, child_nodes);
        }
    }

    return hlist;
}

// ============================================================================
// Block Element Conversion
// ============================================================================

TexNode* convert_paragraph(const ElementReader& elem, DocumentContext& ctx) {
    Pool* pool = pool_create();

    // Build HList from paragraph content
    TexNode* hlist = build_inline_hlist(elem, ctx, pool);

    pool_destroy(pool);

    if (!hlist || !hlist->first_child) {
        return nullptr;  // Empty paragraph
    }

    // Apply hyphenation if available
    if (ctx.hyphenator) {
        FontSpec font = ctx.current_font();
        hlist = insert_discretionary_hyphens(hlist, ctx.hyphenator, font, ctx.arena);
    }

    // Apply line breaking to create VList of lines
    LineBreakParams params = ctx.line_break_params();
    TexNode* paragraph = typeset_paragraph(hlist, params, ctx.baseline_skip(), ctx.arena);

    return paragraph;
}

TexNode* convert_heading(const ElementReader& elem, int level, DocumentContext& ctx) {
    // Increment section counter
    ctx.sections.increment(level);

    Pool* pool = pool_create();

    // Build HList from heading content
    TexNode* hlist = build_inline_hlist(elem, ctx, pool);

    pool_destroy(pool);

    if (!hlist || !hlist->first_child) {
        return nullptr;
    }

    // Size factors for different heading levels
    static const float SIZE_FACTORS[] = {1.0f, 1.728f, 1.44f, 1.2f, 1.0f, 0.9f, 0.8f};
    float factor = (level >= 1 && level <= 6) ? SIZE_FACTORS[level] : 1.0f;

    // Scale the font size
    float original_size = ctx.base_size_pt;
    ctx.base_size_pt *= factor;

    // Create a VList with just the heading
    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width);
    vctx.body_font = ctx.bold_font;
    vctx.body_font.size_pt *= factor;

    begin_vlist(vctx);

    // Add space above heading
    if (level <= 2) {
        add_vspace(vctx, Glue::flexible(18.0f, 4.0f, 2.0f));
    } else {
        add_vspace(vctx, Glue::flexible(12.0f, 3.0f, 1.0f));
    }

    // Create the heading line (centered for h1, left-aligned for others)
    TexNode* heading_line;
    if (level == 1) {
        heading_line = center_line(hlist, ctx.text_width, ctx.arena);
    } else {
        // Convert to hbox
        HListDimensions dims = measure_hlist(hlist);
        heading_line = hlist_to_hbox(hlist, dims.width, ctx.arena);
    }

    if (heading_line) {
        add_raw(vctx, heading_line);
    }

    // Add space below heading
    if (level <= 2) {
        add_vspace(vctx, Glue::flexible(12.0f, 2.0f, 1.0f));
    } else {
        add_vspace(vctx, Glue::flexible(6.0f, 1.0f, 0.5f));
    }

    TexNode* result = end_vlist(vctx);

    // Restore original size
    ctx.base_size_pt = original_size;

    return result;
}

TexNode* convert_list(const ElementReader& elem, bool ordered, DocumentContext& ctx) {
    // Increase list depth
    int depth = ctx.format.list_depth;
    ctx.format.list_depth++;
    if (depth < 8) {
        ctx.format.list_counter[depth] = 0;
    }

    // Create VList for list items
    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width);
    begin_vlist(vctx);

    // Process list items
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (!child.isElement()) continue;

        ElementReader item = child.asElement();
        const char* tag = item.tagName();

        if (tag_matches(tag, "li")) {
            // Increment counter for ordered lists
            if (ordered && depth < 8) {
                ctx.format.list_counter[depth]++;
            }

            TexNode* li_vlist = convert_list_item(item, ctx);
            if (li_vlist) {
                add_raw(vctx, li_vlist);
            }
        }
    }

    // Restore list depth
    ctx.format.list_depth = depth;

    return end_vlist(vctx);
}

TexNode* convert_list_item(const ElementReader& elem, DocumentContext& ctx) {
    int depth = ctx.format.list_depth;

    // Determine bullet/number marker
    const char* marker;
    char num_buf[16];

    bool ordered = (depth > 0 && ctx.format.list_counter[depth - 1] > 0);
    if (ordered && depth > 0 && depth <= 8) {
        snprintf(num_buf, sizeof(num_buf), "%d.", ctx.format.list_counter[depth - 1]);
        marker = num_buf;
    } else {
        // Bullet markers by depth
        static const char* BULLETS[] = {"*", "o", "-", "+"};
        marker = BULLETS[(depth - 1) % 4];
    }

    // Calculate indent
    float indent = ctx.parindent + (depth - 1) * 15.0f;  // 15pt per level

    Pool* pool = pool_create();

    // Build the content HList
    TexNode* content = build_inline_hlist(elem, ctx, pool);

    pool_destroy(pool);

    if (!content || !content->first_child) {
        return nullptr;
    }

    // Create marker text
    HListContext hctx = make_hlist_ctx(ctx);
    TexNode* marker_hlist = text_to_hlist(marker, strlen(marker), hctx);
    (void)marker_hlist;  // TODO: prepend marker to content

    // Create VList for this item
    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width - indent);
    begin_vlist(vctx);

    // Apply hyphenation if available
    if (ctx.hyphenator) {
        FontSpec font = ctx.current_font();
        content = insert_discretionary_hyphens(content, ctx.hyphenator, font, ctx.arena);
    }

    // Break content into lines
    LineBreakParams params = ctx.line_break_params();
    params.hsize = ctx.text_width - indent;
    TexNode* lines = typeset_paragraph(content, params, ctx.baseline_skip(), ctx.arena);

    if (lines) {
        add_raw(vctx, lines);
    }

    return end_vlist(vctx);
}

TexNode* convert_blockquote(const ElementReader& elem, DocumentContext& ctx) {
    // Increase margins for blockquote
    float saved_left = ctx.margin_left;
    float saved_width = ctx.text_width;

    float indent = 20.0f;  // Indent blockquotes by 20pt
    ctx.margin_left += indent;
    ctx.text_width -= indent * 2;

    // Create VList for blockquote content
    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width);
    begin_vlist(vctx);

    // Add space above
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    // Process children as block elements
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            TexNode* block = convert_block_element(child_elem, ctx);
            if (block) {
                add_raw(vctx, block);
            }
        } else if (child.isString()) {
            // Text directly in blockquote - treat as paragraph
            const char* str = child.cstring();
            if (str && strlen(str) > 0) {
                TexNode* hlist = build_text_with_math(str, strlen(str), ctx);
                if (hlist && hlist->first_child) {
                    if (ctx.hyphenator) {
                        hlist = insert_discretionary_hyphens(hlist, ctx.hyphenator,
                                                             ctx.current_font(), ctx.arena);
                    }
                    LineBreakParams params = ctx.line_break_params();
                    TexNode* para = typeset_paragraph(hlist, params, ctx.baseline_skip(), ctx.arena);
                    if (para) {
                        add_raw(vctx, para);
                    }
                }
            }
        }
    }

    // Add space below
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    // Restore margins
    ctx.margin_left = saved_left;
    ctx.text_width = saved_width;

    return end_vlist(vctx);
}

TexNode* convert_code_block(const ElementReader& elem, DocumentContext& ctx) {
    // Switch to monospace font
    TextStyle saved = ctx.format.style;
    ctx.format.style = TextStyle::Monospace;

    // Get text content
    Pool* pool = pool_create();
    StringBuf* sb = stringbuf_new(pool);
    elem.textContent(sb);

    if (!sb->str || sb->length == 0) {
        stringbuf_free(sb);
        pool_destroy(pool);
        ctx.format.style = saved;
        return nullptr;
    }

    // Create VList for code block
    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width);
    vctx.body_font = ctx.mono_font;
    begin_vlist(vctx);

    // Add space above
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    // Split by lines and add each line
    const char* text = sb->str->chars;
    const char* line_start = text;
    const char* end = text + sb->length;

    while (line_start < end) {
        // Find end of line
        const char* line_end = line_start;
        while (line_end < end && *line_end != '\n') {
            line_end++;
        }

        size_t line_len = line_end - line_start;
        if (line_len > 0) {
            // Create HList for this line
            HListContext hctx = make_hlist_ctx(ctx);
            TexNode* line_hlist = text_to_hlist(line_start, line_len, hctx);

            if (line_hlist) {
                // Convert to hbox
                HListDimensions dims = measure_hlist(line_hlist);
                TexNode* line_hbox = hlist_to_hbox(line_hlist, dims.width, ctx.arena);
                if (line_hbox) {
                    add_line(vctx, line_hbox);
                }
            }
        } else {
            // Empty line - add some vertical space
            add_vspace(vctx, Glue::fixed(ctx.baseline_skip() * 0.5f));
        }

        // Move to next line
        line_start = line_end;
        if (line_start < end && *line_start == '\n') {
            line_start++;
        }
    }

    // Add space below
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    stringbuf_free(sb);
    pool_destroy(pool);
    ctx.format.style = saved;

    return end_vlist(vctx);
}

TexNode* convert_math_block(const ElementReader& elem, DocumentContext& ctx) {
    Pool* pool = pool_create();
    StringBuf* sb = stringbuf_new(pool);
    elem.textContent(sb);

    if (!sb->str || sb->length == 0) {
        stringbuf_free(sb);
        pool_destroy(pool);
        return nullptr;
    }

    // Typeset display math
    MathContext math_ctx = ctx.math_context();
    math_ctx.style = MathStyle::Display;

    TexNode* math_hbox = typeset_latex_math(sb->str->chars, sb->length, math_ctx);
    if (!math_hbox) {
        stringbuf_free(sb);
        pool_destroy(pool);
        return nullptr;
    }

    // Create centered display with spacing
    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width);
    begin_vlist(vctx);

    // Space above
    add_vspace(vctx, Glue::flexible(12.0f, 3.0f, 2.0f));

    // Center the math
    TexNode* centered = center_line(math_hbox, ctx.text_width, ctx.arena);
    if (centered) {
        add_raw(vctx, centered);
    }

    // Space below
    add_vspace(vctx, Glue::flexible(12.0f, 3.0f, 2.0f));

    stringbuf_free(sb);
    pool_destroy(pool);

    return end_vlist(vctx);
}

TexNode* convert_table(const ElementReader& elem, DocumentContext& ctx) {
    // Basic table support - just process as paragraphs for now
    // TODO: Implement proper table layout

    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width);
    begin_vlist(vctx);

    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    // Process table cells as text
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            TexNode* block = convert_block_element(child_elem, ctx);
            if (block) {
                add_raw(vctx, block);
            }
        }
    }

    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    return end_vlist(vctx);
}

TexNode* convert_horizontal_rule(DocumentContext& ctx) {
    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width);
    begin_vlist(vctx);

    // Space above
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    // Add horizontal rule
    add_hrule(vctx, 0.4f, ctx.text_width);

    // Space below
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    return end_vlist(vctx);
}

// ============================================================================
// Block Element Dispatcher
// ============================================================================

TexNode* convert_block_element(const ElementReader& elem, DocumentContext& ctx) {
    const char* tag = elem.tagName();
    if (!tag) return nullptr;

    // Check for heading
    int heading_level = get_heading_level(tag);
    if (heading_level > 0) {
        return convert_heading(elem, heading_level, ctx);
    }

    // Other block elements
    if (tag_matches(tag, "p")) {
        return convert_paragraph(elem, ctx);
    } else if (tag_matches(tag, "ul")) {
        return convert_list(elem, false, ctx);
    } else if (tag_matches(tag, "ol")) {
        return convert_list(elem, true, ctx);
    } else if (tag_matches(tag, "li")) {
        return convert_list_item(elem, ctx);
    } else if (tag_matches(tag, "blockquote")) {
        return convert_blockquote(elem, ctx);
    } else if (tag_matches(tag, "pre") ||
               (tag_matches(tag, "code") && !elem.isEmpty())) {
        return convert_code_block(elem, ctx);
    } else if (tag_matches(tag, "math")) {
        // Check if display math
        const char* display = elem.get_attr_string("display");
        if (display && strcmp(display, "block") == 0) {
            return convert_math_block(elem, ctx);
        }
        // Inline math treated as inline content
        return nullptr;
    } else if (tag_matches(tag, "table")) {
        return convert_table(elem, ctx);
    } else if (tag_matches(tag, "hr")) {
        return convert_horizontal_rule(ctx);
    } else if (tag_matches(tag, "div") || tag_matches(tag, "section") ||
               tag_matches(tag, "article") || tag_matches(tag, "main") ||
               tag_matches(tag, "header") || tag_matches(tag, "footer")) {
        // Container elements - process children
        VListContext vctx(ctx.arena, ctx.fonts);
        init_vlist_context(vctx, ctx.text_width);
        begin_vlist(vctx);

        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                TexNode* block = convert_block_element(child_elem, ctx);
                if (block) {
                    add_raw(vctx, block);
                }
            } else if (child.isString()) {
                // Text in container - treat as paragraph
                const char* str = child.cstring();
                if (str) {
                    // Skip whitespace-only text
                    bool has_content = false;
                    for (const char* p = str; *p; p++) {
                        if (!isspace(*p)) {
                            has_content = true;
                            break;
                        }
                    }
                    if (has_content) {
                        TexNode* hlist = build_text_with_math(str, strlen(str), ctx);
                        if (hlist && hlist->first_child) {
                            if (ctx.hyphenator) {
                                hlist = insert_discretionary_hyphens(hlist, ctx.hyphenator,
                                                                     ctx.current_font(), ctx.arena);
                            }
                            LineBreakParams params = ctx.line_break_params();
                            TexNode* para = typeset_paragraph(hlist, params, ctx.baseline_skip(), ctx.arena);
                            if (para) {
                                add_raw(vctx, para);
                            }
                        }
                    }
                }
            }
        }

        return end_vlist(vctx);
    }

    // Unknown block element - try to process children
    return nullptr;
}

// ============================================================================
// Document Conversion API
// ============================================================================

TexNode* convert_document(Item document, DocumentContext& ctx) {
    if (document.item == ItemNull.item) {
        return make_vlist(ctx.arena);
    }

    TypeId type = get_type_id(document);
    if (type != LMD_TYPE_ELEMENT) {
        log_error("lambda_bridge: document must be an Element");
        return make_vlist(ctx.arena);
    }

    ElementReader root(document.element);
    return convert_document(root, ctx);
}

TexNode* convert_document(const ElementReader& root, DocumentContext& ctx) {
    // Create main VList for document
    VListContext vctx(ctx.arena, ctx.fonts);
    init_vlist_context(vctx, ctx.text_width);
    begin_vlist(vctx);

    // Process all children of root element
    auto iter = root.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            TexNode* block = convert_block_element(child_elem, ctx);
            if (block) {
                add_raw(vctx, block);
                // Add parskip between blocks
                if (ctx.parskip > 0) {
                    add_vspace(vctx, Glue::fixed(ctx.parskip));
                }
            }
        } else if (child.isString()) {
            // Direct text in root - treat as paragraph
            const char* str = child.cstring();
            if (str) {
                // Skip whitespace-only text
                bool has_content = false;
                for (const char* p = str; *p; p++) {
                    if (!isspace(*p)) {
                        has_content = true;
                        break;
                    }
                }
                if (has_content) {
                    TexNode* hlist = build_text_with_math(str, strlen(str), ctx);
                    if (hlist && hlist->first_child) {
                        if (ctx.hyphenator) {
                            hlist = insert_discretionary_hyphens(hlist, ctx.hyphenator,
                                                                 ctx.current_font(), ctx.arena);
                        }
                        LineBreakParams params = ctx.line_break_params();
                        TexNode* para = typeset_paragraph(hlist, params, ctx.baseline_skip(), ctx.arena);
                        if (para) {
                            add_raw(vctx, para);
                            if (ctx.parskip > 0) {
                                add_vspace(vctx, Glue::fixed(ctx.parskip));
                            }
                        }
                    }
                }
            }
        }
    }

    return end_vlist(vctx);
}

// ============================================================================
// Page Breaking API
// ============================================================================

PageList break_into_pages(TexNode* document, DocumentContext& ctx) {
    PageList pages = {};

    if (!document) {
        return pages;
    }

    // Set up page break parameters
    PageBreakParams params = PageBreakParams::defaults();
    params.page_height = ctx.text_height;
    params.top_skip = ctx.base_size_pt;

    // Break document into pages using the core page breaking API
    PageBreakResult result = tex::break_into_pages(document, params, ctx.arena);

    if (!result.success) {
        log_error("lambda_bridge: page breaking failed");
        return pages;
    }

    // Build actual pages from break points
    PageContent* page_contents = build_pages(document, result, params, ctx.arena);

    // Extract VLists from PageContent array
    pages.page_count = result.page_count;
    pages.pages = (TexNode**)arena_alloc(ctx.arena, sizeof(TexNode*) * result.page_count);
    pages.total_badness = 0;

    for (int i = 0; i < result.page_count; i++) {
        pages.pages[i] = page_contents[i].vlist;
        pages.total_badness += result.page_penalties ? result.page_penalties[i] : 0;
    }

    return pages;
}

TexNode* typeset_document(Item document, DocumentContext& ctx) {
    TexNode* content = convert_document(document, ctx);
    return content;
}

TexNode* typeset_document_vlist(Item document, DocumentContext& ctx) {
    return typeset_document(document, ctx);
}

} // namespace tex
