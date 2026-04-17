#include "layout.hpp"
#include "form_control.hpp"
#include "rdt_video.h"
#include "state_store.hpp"
#include "../lib/str.h"
#include "../lib/memtrack.h"
#include <cstdlib>  // for strtol
#include <new>      // for placement new

// Direct declaration of the actual C symbol (compiler will add underscore)
extern "C" int strview_to_int(StrView* s);

// Parse HTML color attribute (e.g., "#ff6600" or "ff6600" or named colors like "red")
static Color parse_html_color(const char* color_str) {
    Color result;
    result.r = 0; result.g = 0; result.b = 0; result.a = 255;  // default black, opaque
    if (!color_str || !*color_str) return result;

    // Skip leading # if present
    if (*color_str == '#') color_str++;

    size_t len = strlen(color_str);
    if (len == 6) {
        // Parse #rrggbb format
        char hex[3] = {0};
        hex[0] = color_str[0]; hex[1] = color_str[1];
        result.r = (uint8_t)strtol(hex, NULL, 16);
        hex[0] = color_str[2]; hex[1] = color_str[3];
        result.g = (uint8_t)strtol(hex, NULL, 16);
        hex[0] = color_str[4]; hex[1] = color_str[5];
        result.b = (uint8_t)strtol(hex, NULL, 16);
    } else if (len == 3) {
        // Parse #rgb shorthand (e.g., #f60 -> #ff6600)
        char hex[3] = {0};
        hex[0] = color_str[0]; hex[1] = color_str[0];
        result.r = (uint8_t)strtol(hex, NULL, 16);
        hex[0] = color_str[1]; hex[1] = color_str[1];
        result.g = (uint8_t)strtol(hex, NULL, 16);
        hex[0] = color_str[2]; hex[1] = color_str[2];
        result.b = (uint8_t)strtol(hex, NULL, 16);
    }
    // TODO: add named color support (red, blue, green, etc.)
    return result;
}

// Get the border attribute value from the parent TABLE element
// Returns -1 if no border attribute is found, otherwise returns the pixel value
// Per WHATWG 15.3.10: table[border] td, table[border] th { border-width: 1px; border-style: inset; border-color: grey; }
static float get_parent_table_border(DomNode* elmt) {
    // Traverse up to find the TABLE element (TD -> TR -> TBODY/THEAD/TFOOT -> TABLE, or TD -> TR -> TABLE)
    DomNode* node = elmt->parent;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->tag_id == HTM_TAG_TABLE) {
                const char* border_attr = elem->get_attribute("border");
                if (border_attr) {
                    StrView b_view = strview_init(border_attr, strlen(border_attr));
                    float border_val = strview_to_int(&b_view);
                    if (border_val >= 0) {
                        return border_val;
                    }
                }
                return -1;
            }
        }
        node = node->parent;
    }
    return -1;
}

// Get the cellpadding attribute value from the parent TABLE element
// Returns -1 if no cellpadding attribute is found, otherwise returns the pixel value (CSS logical pixels)
// The HTML spec says: cellpadding on TABLE maps to padding on TD/TH cells
static float get_parent_table_cellpadding(DomNode* elmt) {
    // Traverse up to find the TABLE element (TD -> TR -> TBODY/THEAD/TFOOT -> TABLE, or TD -> TR -> TABLE)
    DomNode* node = elmt->parent;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->tag_id == HTM_TAG_TABLE) {
                const char* cellpadding_attr = elem->get_attribute("cellpadding");
                if (cellpadding_attr) {
                    StrView cp_view = strview_init(cellpadding_attr, strlen(cellpadding_attr));
                    float cellpadding = strview_to_int(&cp_view);
                    if (cellpadding >= 0) {
                        log_debug("[HTML] TABLE cellpadding attribute: %.0fpx", cellpadding);
                        return cellpadding;  // CSS logical pixels
                    }
                }
                // Found parent table but no cellpadding attribute
                return -1;
            }
        }
        node = node->parent;
    }
    // No parent table found
    return -1;
}

// get parent TR's valign attribute (for TD/TH cells)
static const char* get_parent_tr_valign(DomNode* elmt) {
    // TD/TH -> TR, check TR's valign attribute
    DomNode* node = elmt->parent;
    if (node && node->is_element()) {
        DomElement* elem = node->as_element();
        if (elem->tag_id == HTM_TAG_TR) {
            return elem->get_attribute("valign");
        }
    }
    return nullptr;
}

// HTML5 §14.3.4 / Unicode UAX #9: Detect first strong directional character.
// Returns 1 for RTL (R/AL), -1 for LTR (L), 0 for neutral/not found.
static int bidi_strong_class(uint32_t cp) {
    // L (Left-to-Right): Latin, CJK, LRM
    if (cp == 0x200E) return -1; // LRM
    // R (Right-to-Left): Hebrew, RLM
    if (cp == 0x200F) return 1;  // RLM
    // AL (Arabic Letter): ALM
    if (cp == 0x061C) return 1;  // ALM
    // Hebrew: U+0590-U+05FF (R)
    if (cp >= 0x0590 && cp <= 0x05FF) return 1;
    // Arabic: U+0600-U+07BF (AL) — Arabic, Arabic Supplement, Arabic Extended-A
    if (cp >= 0x0600 && cp <= 0x07BF) return 1;
    // Arabic Extended-B, Arabic Presentation Forms
    if (cp >= 0x0860 && cp <= 0x089F) return 1;
    if (cp >= 0xFB50 && cp <= 0xFDFF) return 1;  // Arabic Presentation Forms-A
    if (cp >= 0xFE70 && cp <= 0xFEFF) return 1;  // Arabic Presentation Forms-B
    // NKo: U+07C0-U+07FF (R)
    if (cp >= 0x07C0 && cp <= 0x07FF) return 1;
    // Syriac: U+0700-U+074F (AL)
    if (cp >= 0x0700 && cp <= 0x074F) return 1;
    // Thaana: U+0780-U+07BF (AL) — already covered above
    // Samaritan, Mandaic
    if (cp >= 0x0800 && cp <= 0x085F) return 1;
    // Common strong LTR ranges (L class)
    // Basic Latin A-Z, a-z
    if ((cp >= 0x0041 && cp <= 0x005A) || (cp >= 0x0061 && cp <= 0x007A)) return -1;
    // Latin Extended
    if (cp >= 0x00C0 && cp <= 0x02AF) return -1;
    // Greek
    if (cp >= 0x0370 && cp <= 0x03FF) return -1;
    // Cyrillic
    if (cp >= 0x0400 && cp <= 0x052F) return -1;
    // CJK Unified Ideographs
    if (cp >= 0x4E00 && cp <= 0x9FFF) return -1;
    // Hangul
    if (cp >= 0xAC00 && cp <= 0xD7AF) return -1;
    // Hiragana, Katakana
    if (cp >= 0x3040 && cp <= 0x30FF) return -1;
    // Thai, Lao, Tibetan, Myanmar, Georgian, Ethiopic, Cherokee, etc.
    if (cp >= 0x0E01 && cp <= 0x0E5B) return -1;  // Thai
    if (cp >= 0x0E81 && cp <= 0x0EDF) return -1;  // Lao
    if (cp >= 0x10A0 && cp <= 0x10FF) return -1;  // Georgian
    if (cp >= 0x1100 && cp <= 0x11FF) return -1;  // Hangul Jamo
    // Devanagari and other Indic scripts
    if (cp >= 0x0900 && cp <= 0x0DFF) return -1;
    // Latin Extended Additional, General Punctuation etc. are neutral
    return 0;
}

// HTML5 §14.3.4: Walk the element's descendants to find the first strong character.
// Skip <script>, <style>, and elements with their own dir attribute.
static int find_first_strong_in_node(DomNode* node) {
    if (!node) return 0;
    if (node->is_text()) {
        DomText* text = node->as_text();
        if (!text->text || text->length == 0) return 0;
        const char* p = text->text;
        const char* end = p + text->length;
        while (p < end) {
            uint32_t cp;
            int bytes = str_utf8_decode(p, (size_t)(end - p), &cp);
            if (bytes <= 0) { p++; continue; }
            int cls = bidi_strong_class(cp);
            if (cls != 0) return cls;
            p += bytes;
        }
        return 0;
    }
    if (node->is_element()) {
        DomElement* elem = node->as_element();
        // Skip <script>, <style> — they don't contribute to dir="auto" detection
        uintptr_t tag = elem->tag_id;
        if (tag == HTM_TAG_SCRIPT || tag == HTM_TAG_STYLE) return 0;
        // Skip elements that have their own dir attribute — per HTML spec,
        // they establish their own directionality and don't contribute to parent's auto
        const char* child_dir = elem->get_attribute("dir");
        if (child_dir) return 0;
        // Recurse into children
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            int result = find_first_strong_in_node(child);
            if (result != 0) return result;
        }
    }
    return 0;
}

// Resolve dir="auto" by finding the first strong directional character.
// Returns CSS_VALUE_RTL or CSS_VALUE_LTR.
static CssEnum resolve_dir_auto(DomElement* elmt) {
    for (DomNode* child = elmt->first_child; child; child = child->next_sibling) {
        int result = find_first_strong_in_node(child);
        if (result > 0) return CSS_VALUE_RTL;
        if (result < 0) return CSS_VALUE_LTR;
    }
    return CSS_VALUE_LTR;  // default to LTR if no strong character found
}

void apply_element_default_style(LayoutContext* lycon, DomNode* elmt) {
    ViewSpan* span = (ViewSpan*)elmt;  ViewBlock* block = (ViewBlock*)elmt;
    float em_size = 0;  uintptr_t elmt_name = elmt->tag();
    switch (elmt_name) {
    case HTM_TAG_BODY: {
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // margin: 8px (CSS logical pixels)
        block->bound->margin.top = block->bound->margin.right =
            block->bound->margin.bottom = block->bound->margin.left = 8;
        block->bound->margin.top_specificity = block->bound->margin.right_specificity =
            block->bound->margin.bottom_specificity = block->bound->margin.left_specificity = -1;
        // Handle HTML bgcolor attribute (e.g., <body bgcolor="#fff">)
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            if (!block->bound->background) { block->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp)); }
            block->bound->background->color = bg_color;
            log_debug("[HTML] BODY bgcolor attribute: #%02x%02x%02x", bg_color.r, bg_color.g, bg_color.b);
        }
        // overflow: visible (CSS default - no special overflow handling for body)
        break;
    }
    case HTM_TAG_H1:
        em_size = 2;  // 2em font-size
        goto HEADING_PROP;
    case HTM_TAG_H2:
        em_size = 1.5;  // 1.5em font-size
        goto HEADING_PROP;
    case HTM_TAG_H3:
        em_size = 1.17;  // 1.17em font-size
        goto HEADING_PROP;
    case HTM_TAG_H4:
        em_size = 1;  // 1em font-size
        goto HEADING_PROP;
    case HTM_TAG_H5:
        em_size = 0.83;  // 0.83em font-size
        goto HEADING_PROP;
    case HTM_TAG_H6:
        em_size = 0.67;  // 0.67em font-size
        HEADING_PROP: {
        // Font styles
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        float heading_font_size = lycon->font.style->font_size * em_size;
        block->font->font_size = heading_font_size;
        block->font->font_size_from_medium = false;
        block->font->font_weight = CSS_VALUE_BOLD;
        block->font->font_weight_numeric = 700;
        // Default margins for headings (browser UA stylesheet)
        // margin: 0.67em 0 for h1, varying for other levels
        // The margin is relative to the heading's computed font-size
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        float margin_em;
        switch (elmt_name) {
            case HTM_TAG_H1: margin_em = 0.67; break;
            case HTM_TAG_H2: margin_em = 0.83; break;
            case HTM_TAG_H3: margin_em = 1.00; break;
            case HTM_TAG_H4: margin_em = 1.33; break;
            case HTM_TAG_H5: margin_em = 1.67; break;
            case HTM_TAG_H6: margin_em = 2.33; break;
            default: margin_em = 0.67; break;
        }
        block->bound->margin.top = block->bound->margin.bottom = heading_font_size * margin_em;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        // Handle HTML align attribute (e.g., align="left", align="right", align="center")
        {
            const char* align_attr = elmt->get_attribute("align");
            if (align_attr) {
                if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                if (str_ieq_const(align_attr, strlen(align_attr), "left")) {
                    block->blk->text_align = CSS_VALUE_LEFT;
                } else if (str_ieq_const(align_attr, strlen(align_attr), "right")) {
                    block->blk->text_align = CSS_VALUE_RIGHT;
                } else if (str_ieq_const(align_attr, strlen(align_attr), "center")) {
                    block->blk->text_align = CSS_VALUE_CENTER;
                } else if (str_ieq_const(align_attr, strlen(align_attr), "justify")) {
                    block->blk->text_align = CSS_VALUE_JUSTIFY;
                }
            }
        }
        break;
    }
    case HTM_TAG_P: {
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // margin: 1em 0;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        // Handle HTML align attribute (e.g., align="left", align="right", align="center")
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            if (str_ieq_const(align_attr, strlen(align_attr), "left")) {
                block->blk->text_align = CSS_VALUE_LEFT;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "right")) {
                block->blk->text_align = CSS_VALUE_RIGHT;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "center")) {
                block->blk->text_align = CSS_VALUE_CENTER;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "justify")) {
                block->blk->text_align = CSS_VALUE_JUSTIFY;
            }
        }
        break;
    }
    case HTM_TAG_UL:  case HTM_TAG_OL:  case HTM_TAG_MENU:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->list_style_type = elmt_name == HTM_TAG_UL ? CSS_VALUE_DISC : CSS_VALUE_DECIMAL;
        if (!block->bound) {
            block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        // margin: 1em 0; padding: 0 0 0 40px;
        // UA stylesheet: nested lists (ul ul, ol ol, ul ol, ol ul) have margin: 0
        {
            bool is_nested = false;
            DomNode* ancestor = elmt->parent;
            while (ancestor) {
                if (ancestor->is_element()) {
                    uintptr_t atag = ancestor->tag();
                    if (atag == HTM_TAG_UL || atag == HTM_TAG_OL ||
                        atag == HTM_TAG_MENU || atag == HTM_TAG_DIR) {
                        is_nested = true;
                        break;
                    }
                }
                ancestor = ancestor->parent;
            }
            if (!is_nested) {
                block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
                block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
            }
        }
        block->bound->padding.left = 40;  // CSS logical pixels
        block->bound->padding.left_specificity = -1;
        break;
    case HTM_TAG_CENTER:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        break;
    case HTM_TAG_DIV: {
        // HTML spec §14.3.3: <div align> maps to text-align
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            size_t alen = strlen(align_attr);
            if (str_ieq_const(align_attr, alen, "left")) {
                block->blk->text_align = CSS_VALUE_LEFT;
            } else if (str_ieq_const(align_attr, alen, "right")) {
                block->blk->text_align = CSS_VALUE_RIGHT;
            } else if (str_ieq_const(align_attr, alen, "center")) {
                block->blk->text_align = CSS_VALUE_CENTER;
            } else if (str_ieq_const(align_attr, alen, "justify")) {
                block->blk->text_align = CSS_VALUE_JUSTIFY;
            }
        }
        break;
    }
    case HTM_TAG_IMG:  { // get html width and height (before the css styles)
        size_t value_len;  const char *value;
        value = elmt->get_attribute("width");
        if (value) {
            value_len = strlen(value);
            if (value_len > 0 && value[value_len - 1] == '%') {
                // HTML attribute width="50%" — percentage of containing block
                StrView width_view = strview_init(value, value_len - 1);
                float percent = strview_to_int(&width_view);
                if (percent > 0) {
                    if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                    block->blk->given_width_percent = percent;
                    // resolve against container width at layout time
                    float container_width = lycon->block.content_width > 0
                        ? lycon->block.content_width : 0;
                    if (container_width > 0) {
                        lycon->block.given_width = container_width * percent / 100.0f;
                    }
                    log_debug("[HTML] IMG width attribute: %.0f%% -> %.1fpx", percent, lycon->block.given_width);
                }
            } else if (value_len > 0 && value[0] >= '0' && value[0] <= '9') {
                // HTML spec: non-negative integer must start with ASCII digit; skip "auto" etc.
                StrView width_view = strview_init(value, value_len);
                float width = strview_to_int(&width_view);
                if (width >= 0) lycon->block.given_width = width;  // CSS logical pixels
            }
        }
        value = elmt->get_attribute("height");
        if (value) {
            value_len = strlen(value);
            if (value_len > 0 && value[value_len - 1] == '%') {
                // HTML attribute height="50%" — percentage of containing block
                StrView height_view = strview_init(value, value_len - 1);
                float percent = strview_to_int(&height_view);
                if (percent > 0) {
                    if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                    block->blk->given_height_percent = percent;
                    float container_height = lycon->block.content_height > 0
                        ? lycon->block.content_height : 0;
                    if (container_height > 0) {
                        lycon->block.given_height = container_height * percent / 100.0f;
                    }
                    log_debug("[HTML] IMG height attribute: %.0f%% -> %.1fpx", percent, lycon->block.given_height);
                }
            } else if (value_len > 0 && value[0] >= '0' && value[0] <= '9') {
                // HTML spec: non-negative integer must start with ASCII digit; skip "auto" etc.
                StrView height_view = strview_init(value, value_len);
                float height = strview_to_int(&height_view);
                if (height >= 0) lycon->block.given_height = height;  // CSS logical pixels
            }
        }
        // HTML spec §14.3.3: <img align="left|right"> maps to float: left|right
        {
            const char* align_attr = elmt->get_attribute("align");
            if (align_attr) {
                size_t align_len = strlen(align_attr);
                if (str_ieq_const(align_attr, align_len, "left")) {
                    if (!block->position) { block->position = alloc_position_prop(lycon); }
                    block->position->float_prop = CSS_VALUE_LEFT;
                } else if (str_ieq_const(align_attr, align_len, "right")) {
                    if (!block->position) { block->position = alloc_position_prop(lycon); }
                    block->position->float_prop = CSS_VALUE_RIGHT;
                }
            }
        }
        break;
    }
    case HTM_TAG_IFRAME: {
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // HTML spec §15.5.14: iframe { border: 2px inset; }
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left = 2;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        block->bound->border->top_style = block->bound->border->bottom_style = CSS_VALUE_INSET;
        block->bound->border->left_style = block->bound->border->right_style = CSS_VALUE_INSET;
        block->bound->border->top_color.r = block->bound->border->top_color.g =
            block->bound->border->top_color.b = 128; block->bound->border->top_color.a = 255;
        block->bound->border->left_color = block->bound->border->top_color;
        block->bound->border->bottom_color.r = block->bound->border->bottom_color.g =
            block->bound->border->bottom_color.b = 192; block->bound->border->bottom_color.a = 255;
        block->bound->border->right_color = block->bound->border->bottom_color;
        if (!block->scroller) { block->scroller = alloc_scroll_prop(lycon); }
        block->scroller->overflow_x = CSS_VALUE_AUTO;
        block->scroller->overflow_y = CSS_VALUE_AUTO;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        // Parse HTML width/height attributes; default 300x150 per HTML spec
        size_t value_len;  const char *value;
        value = elmt->get_attribute("width");
        if (value) {
            value_len = strlen(value);
            if (value_len > 0 && value[value_len - 1] == '%') {
                StrView width_view = strview_init(value, value_len - 1);
                float percent = strview_to_int(&width_view);
                if (percent > 0) {
                    block->blk->given_width_percent = percent;
                    lycon->block.given_width = -1;  // resolve at layout time
                    log_debug("[HTML] IFRAME width attribute: %.0f%%", percent);
                }
            } else {
                StrView width_view = strview_init(value, value_len);
                float width = strview_to_int(&width_view);
                if (width >= 0) {
                    lycon->block.given_width = width;
                    block->blk->given_width = width;
                }
            }
        } else {
            lycon->block.given_width = 300;  // default intrinsic width
            block->blk->given_width = 300;
        }
        value = elmt->get_attribute("height");
        if (value) {
            value_len = strlen(value);
            if (value_len > 0 && value[value_len - 1] == '%') {
                StrView height_view = strview_init(value, value_len - 1);
                float percent = strview_to_int(&height_view);
                if (percent > 0) {
                    block->blk->given_height_percent = percent;
                    lycon->block.given_height = -1;  // resolve at layout time
                    log_debug("[HTML] IFRAME height attribute: %.0f%%", percent);
                }
            } else {
                StrView height_view = strview_init(value, value_len);
                float height = strview_to_int(&height_view);
                if (height >= 0) {
                    lycon->block.given_height = height;
                    block->blk->given_height = height;
                }
            }
        } else {
            lycon->block.given_height = 150;  // default intrinsic height
            block->blk->given_height = 150;
        }
        break;
    }
    case HTM_TAG_EMBED:
        // replaced element with default 300x150 per HTML spec
        block->display.inner = RDT_DISPLAY_REPLACED;
        lycon->block.given_width = 300;
        lycon->block.given_height = 150;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->given_width = 300;
        block->blk->given_height = 150;
        break;
    case HTM_TAG_AUDIO: {
        // HTML §4.8.9: <audio> without controls is not rendered
        // audio-only playback: create RdtVideo (AVPlayer handles audio files natively)
        const char* src = elmt->get_attribute("src");
        if (src && *src && lycon->ui_context && lycon->ui_context->document && lycon->ui_context->document->url) {
            if (!block->embed) { block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp)); }
            if (!block->embed->video) {
                Url* abs_url = parse_url(lycon->ui_context->document->url, src);
                char* file_path = abs_url ? url_to_local_path(abs_url) : NULL;
                if (abs_url) url_destroy(abs_url);

                if (file_path) {
                    RdtVideo* video = rdt_video_create(NULL, NULL);
                    if (video) {
                        log_debug("audio: opening file: %s", file_path);
                        rdt_video_open_file(video, file_path);
                        if (elmt->has_attribute("loop")) rdt_video_set_loop(video, true);
                        if (elmt->has_attribute("muted")) rdt_video_set_muted(video, true);
                        block->embed->video = video;
                        if (elmt->has_attribute("autoplay")) {
                            rdt_video_play(video);
                            DomDocument* doc = lycon->ui_context->document;
                            if (doc->state) doc->state->has_active_video = true;
                        }
                    }
                    mem_free(file_path);
                } else {
                    log_error("audio: failed to resolve src path: %s", src);
                }
            }
        }
        break;
    }
    case HTM_TAG_VIDEO: {
        // replaced element with default 300x150 per HTML spec
        block->display.inner = RDT_DISPLAY_REPLACED;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        // Parse HTML width/height attributes; default 300x150
        if (const char* w_attr = elmt->get_attribute("width")) {
            StrView w_view = strview_init(w_attr, strlen(w_attr));
            float w = strview_to_int(&w_view);
            if (w >= 0) { lycon->block.given_width = w; block->blk->given_width = w; }
            else { lycon->block.given_width = 300; block->blk->given_width = 300; }
        } else { lycon->block.given_width = 300; block->blk->given_width = 300; }
        if (const char* h_attr = elmt->get_attribute("height")) {
            StrView h_view = strview_init(h_attr, strlen(h_attr));
            float h = strview_to_int(&h_view);
            if (h >= 0) { lycon->block.given_height = h; block->blk->given_height = h; }
            else { lycon->block.given_height = 150; block->blk->given_height = 150; }
        } else { lycon->block.given_height = 150; block->blk->given_height = 150; }

        // initialize video playback if src attribute is present
        const char* src = elmt->get_attribute("src");
        if (src && *src && lycon->ui_context && lycon->ui_context->document && lycon->ui_context->document->url) {
            if (!block->embed) { block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp)); }
            if (!block->embed->video) {
                // resolve relative src path against document URL (same as load_image)
                Url* abs_url = parse_url(lycon->ui_context->document->url, src);
                char* file_path = abs_url ? url_to_local_path(abs_url) : NULL;
                if (abs_url) url_destroy(abs_url);

                if (file_path) {
                    // preload attribute: "none" defers open until play
                    const char* preload = elmt->get_attribute("preload");
                    bool preload_none = preload && strcmp(preload, "none") == 0;

                    RdtVideo* video = rdt_video_create(NULL, NULL);
                    if (video) {
                        if (!preload_none) {
                            log_debug("video: opening file: %s", file_path);
                            rdt_video_open_file(video, file_path);
                        } else {
                            log_debug("video: preload=none, deferring open: %s", file_path);
                        }
                        if (elmt->has_attribute("loop")) rdt_video_set_loop(video, true);
                        if (elmt->has_attribute("muted")) rdt_video_set_muted(video, true);
                        block->embed->video = video;
                        // controls attribute
                        if (elmt->has_attribute("controls")) {
                            block->embed->has_controls = true;
                        }
                        // poster attribute: load poster image
                        const char* poster_src = elmt->get_attribute("poster");
                        if (poster_src && *poster_src) {
                            block->embed->poster = load_image(lycon->ui_context, poster_src);
                            if (block->embed->poster) {
                                log_debug("video: loaded poster image: %s", poster_src);
                            }
                        }
                        // autoplay: start playback immediately
                        if (elmt->has_attribute("autoplay")) {
                            if (preload_none) {
                                // need to open first when preload=none + autoplay
                                rdt_video_open_file(video, file_path);
                            }
                            rdt_video_play(video);
                            // enable continuous redraw for video playback
                            DomDocument* doc = lycon->ui_context->document;
                            if (doc->state) doc->state->has_active_video = true;
                        }
                    }
                    mem_free(file_path);
                } else {
                    log_error("video: failed to resolve src path: %s", src);
                }
            }
        }
        break;
    }
    case HTM_TAG_CANVAS:
        // HTML §4.8.9: <audio> without controls is not rendered (display: none)
        // With controls, it's a replaced element with browser-specific dimensions
        if (elmt->has_attribute("controls")) {
            block->display.inner = RDT_DISPLAY_REPLACED;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            lycon->block.given_width = 300;
            lycon->block.given_height = 54;
            block->blk->given_width = 300;
            block->blk->given_height = 54;
        }
        break;
    case HTM_TAG_OBJECT:
        // HTML §4.8.7: <object> is replaced only when it has a data attribute.
        // Without data, it renders its fallback content (children) as normal flow.
        if (elmt->get_attribute("data")) {
            block->display.inner = RDT_DISPLAY_REPLACED;
            lycon->block.given_width = 300;
            lycon->block.given_height = 150;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            block->blk->given_width = 300;
            block->blk->given_height = 150;
        }
        break;
    case HTM_TAG_HR:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // hr default: 1px border on all sides (creates 2px height from border-top + border-bottom)
        // This matches browser UA stylesheet behavior (CSS logical pixels)
        block->bound->border->width.top = block->bound->border->width.bottom = 1;
        block->bound->border->width.left = block->bound->border->width.right = 1;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        // Default border style: inset (typical browser default for hr)
        block->bound->border->top_style = block->bound->border->bottom_style = CSS_VALUE_INSET;
        block->bound->border->left_style = block->bound->border->right_style = CSS_VALUE_INSET;
        // Default border colors for inset style: darker gray on top/left, lighter on bottom/right
        // Top/left: dark gray for 3D inset effect
        block->bound->border->top_color.r = 128; block->bound->border->top_color.g = 128;
        block->bound->border->top_color.b = 128; block->bound->border->top_color.a = 255;
        block->bound->border->left_color.r = 128; block->bound->border->left_color.g = 128;
        block->bound->border->left_color.b = 128; block->bound->border->left_color.a = 255;
        // Bottom/right: lighter for 3D inset effect
        block->bound->border->bottom_color.r = 192; block->bound->border->bottom_color.g = 192;
        block->bound->border->bottom_color.b = 192; block->bound->border->bottom_color.a = 255;
        block->bound->border->right_color.r = 192; block->bound->border->right_color.g = 192;
        block->bound->border->right_color.b = 192; block->bound->border->right_color.a = 255;
        // 8px margin top/bottom, auto left/right for horizontal centering (browser default)
        block->bound->margin.top = block->bound->margin.bottom = 8;  // CSS logical pixels
        block->bound->margin.left = block->bound->margin.right = 0;
        block->bound->margin.left_type = CSS_VALUE_AUTO;
        block->bound->margin.right_type = CSS_VALUE_AUTO;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    case HTM_TAG_B:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_weight = CSS_VALUE_BOLD;
        span->font->font_weight_numeric = 700;
        break;
    case HTM_TAG_I:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_style = CSS_VALUE_ITALIC;
        break;
    case HTM_TAG_U:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->text_deco = CSS_VALUE_UNDERLINE;
        break;
    case HTM_TAG_S:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->text_deco = CSS_VALUE_LINE_THROUGH;
        break;
    case HTM_TAG_FONT: {
        // parse font style
        // Get color attribute using DomNode interface
        const char* color_attr = span->get_attribute("color");
        if (color_attr) {
            if (!span->in_line) { span->in_line = alloc_inline_prop(lycon); }
            span->in_line->color = parse_html_color(color_attr);
            log_debug("HTM_TAG_FONT color: %s -> rgb(%d,%d,%d)", color_attr,
                      span->in_line->color.r, span->in_line->color.g, span->in_line->color.b);
        }
        // Handle font size attribute (deprecated HTML but still supported)
        // size="1" = x-small (10px), size="2" = small (13px), size="3" = medium (16px, default)
        // size="4" = large (18px), size="5" = x-large (24px), size="6" = xx-large (32px), size="7" = 48px
        const char* size_attr = span->get_attribute("size");
        if (size_attr) {
            int size_value = (int)str_to_int64_default(size_attr, strlen(size_attr), 0);
            float font_size = 16;  // default medium
            // CSS absolute-size keywords mapped to pixels (based on 16px base)
            switch (size_value) {
                case 1: font_size = 10; break;  // x-small
                case 2: font_size = 13; break;  // small
                case 3: font_size = 16; break;  // medium (default)
                case 4: font_size = 18; break;  // large
                case 5: font_size = 24; break;  // x-large
                case 6: font_size = 32; break;  // xx-large
                case 7: font_size = 48; break;  // xxx-large
                default:
                    // Handle relative sizes: +1, -1, etc.
                    if (size_attr[0] == '+' || size_attr[0] == '-') {
                        int delta = (int)str_to_int64_default(size_attr, strlen(size_attr), 0);
                        // Map current font size to approximate level and apply delta
                        float current = lycon->font.style->font_size;
                        int level = 3;  // assume medium
                        if (current <= 10) level = 1;
                        else if (current <= 13) level = 2;
                        else if (current <= 16) level = 3;
                        else if (current <= 18) level = 4;
                        else if (current <= 24) level = 5;
                        else if (current <= 32) level = 6;
                        else level = 7;
                        level += delta;
                        if (level < 1) level = 1;
                        if (level > 7) level = 7;
                        switch (level) {
                            case 1: font_size = 10; break;
                            case 2: font_size = 13; break;
                            case 3: font_size = 16; break;
                            case 4: font_size = 18; break;
                            case 5: font_size = 24; break;
                            case 6: font_size = 32; break;
                            case 7: font_size = 48; break;
                        }
                    }
                    break;
            }
            if (!span->font) { span->font = alloc_font_prop(lycon); }
            span->font->font_size = font_size;  // CSS logical pixels
            log_debug("HTM_TAG_FONT size='%s' -> %.1fpx", size_attr, span->font->font_size);
        }
        // Handle font face attribute
        const char* face_attr = span->get_attribute("face");
        if (face_attr) {
            if (!span->font) { span->font = alloc_font_prop(lycon); }
            span->font->family = (char*)face_attr;  // store font family name
            log_debug("HTM_TAG_FONT face: %s", face_attr);
        }
        break;
    }
    case HTM_TAG_A: {
        // anchor style
        if (!span->in_line) { span->in_line = alloc_inline_prop(lycon); }
        span->in_line->cursor = CSS_VALUE_POINTER;
        span->in_line->color = color_name_to_rgb(CSS_VALUE_BLUE);
        span->font = alloc_font_prop(lycon);
        span->font->text_deco = CSS_VALUE_UNDERLINE;
        break;
    }
    // ========== Additional text formatting elements ==========
    case HTM_TAG_STRONG:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_weight = CSS_VALUE_BOLD;
        span->font->font_weight_numeric = 700;
        break;
    case HTM_TAG_EM:  case HTM_TAG_CITE:  case HTM_TAG_DFN:  case HTM_TAG_VAR:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_style = CSS_VALUE_ITALIC;
        break;
    case HTM_TAG_CODE:  case HTM_TAG_KBD:  case HTM_TAG_SAMP:  case HTM_TAG_TT: {
        // monospace font family
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->family = (char*)"monospace";
        // Browser quirk (Chromium CheckForGenericFamilyChange): when font-family
        // transitions to monospace and no explicit font-size on this element,
        // scale inherited size by 13/16. Only applies when the inherited font-size
        // originates from the CSS 'medium' keyword (initial value), not from an
        // explicit font-size declaration like '12px'.
        bool parent_is_mono = lycon->font.style && lycon->font.style->family &&
            str_ieq_const(lycon->font.style->family, strlen(lycon->font.style->family), "monospace");
        if (!parent_is_mono && span->font->font_size > 0 && span->font->font_size_from_medium) {
            span->font->font_size = span->font->font_size * 13.0f / 16.0f;
        }
        break;
    }
    case HTM_TAG_MARK:
        // yellow background highlight - handled via background property on block
        // Note: InlineProp doesn't have bg_color; would need BackgroundProp
        break;
    case HTM_TAG_SMALL:
        // font-size: smaller (0.83em)
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.style->font_size * 0.83;
        span->font->font_size_from_medium = false;
        break;
    case HTM_TAG_BIG:
        // font-size: larger (1.17em) - deprecated but still supported
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.style->font_size * 1.17;
        span->font->font_size_from_medium = false;
        break;
    case HTM_TAG_SUB:
        // subscript: smaller font, lowered baseline
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.style->font_size * 0.83;
        span->font->font_size_from_medium = false;
        if (!span->in_line) { span->in_line = alloc_inline_prop(lycon); }
        span->in_line->vertical_align = CSS_VALUE_SUB;
        break;
    case HTM_TAG_SUP:
        // superscript: smaller font, raised baseline
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.style->font_size * 0.83;
        span->font->font_size_from_medium = false;
        if (!span->in_line) { span->in_line = alloc_inline_prop(lycon); }
        span->in_line->vertical_align = CSS_VALUE_SUPER;
        break;
    case HTM_TAG_DEL:  case HTM_TAG_STRIKE:
        // strikethrough
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->text_deco = CSS_VALUE_LINE_THROUGH;
        break;
    case HTM_TAG_INS:
        // underline for inserted text
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->text_deco = CSS_VALUE_UNDERLINE;
        break;
    case HTM_TAG_Q:
        // inline quotation - browser adds quotes via CSS content, we just style italic
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_style = CSS_VALUE_ITALIC;
        break;
    case HTM_TAG_ABBR:  case HTM_TAG_ACRONYM:
        // abbreviation/acronym - dotted underline in some browsers
        // we'll use standard underline for simplicity
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->text_deco = CSS_VALUE_UNDERLINE;
        break;
    // ========== Block elements ==========
    case HTM_TAG_PRE:  case HTM_TAG_LISTING:  case HTM_TAG_XMP: {
        // preformatted: monospace, preserve whitespace, margin 1em 0
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->family = (char*)"monospace";
        // Browser quirk (Chromium CheckForGenericFamilyChange): when font-family
        // transitions to monospace and no explicit font-size on this element,
        // scale inherited size by 13/16. Only applies when the inherited font-size
        // originates from the CSS 'medium' keyword (initial value).
        float pre_font_size = lycon->font.style->font_size;
        {
            bool parent_is_mono = lycon->font.style && lycon->font.style->family &&
                str_ieq_const(lycon->font.style->family, strlen(lycon->font.style->family), "monospace");
            if (!parent_is_mono && block->font->font_size > 0 && block->font->font_size_from_medium) {
                block->font->font_size = block->font->font_size * 13.0f / 16.0f;
            }
            if (!parent_is_mono && pre_font_size > 0 && block->font->font_size_from_medium) {
                pre_font_size = pre_font_size * 13.0f / 16.0f;
            }
        }
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->white_space = CSS_VALUE_PRE;
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = pre_font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        break;
    }
    case HTM_TAG_BLOCKQUOTE:
        // margin: 1em 40px
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.left = block->bound->margin.right = 40;  // CSS logical pixels
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    case HTM_TAG_ADDRESS:
        // italic, block display
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->font_style = CSS_VALUE_ITALIC;
        break;
    case HTM_TAG_FIGURE:
        // margin: 1em 40px
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.left = block->bound->margin.right = 40;  // CSS logical pixels
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    case HTM_TAG_FIGCAPTION:
        // text-align: center (common default)
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        break;
    case HTM_TAG_DL:
        // definition list: margin 1em 0
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        break;
    case HTM_TAG_DD:
        // definition description: margin-left 40px
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.left = 40;  // CSS logical pixels
        block->bound->margin.left_specificity = -1;
        break;
    case HTM_TAG_DT:
        // definition term: bold (common style, not strictly default)
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->font_weight = CSS_VALUE_BOLD;
        block->font->font_weight_numeric = 700;
        break;
    case HTM_TAG_LI:
        // list item: display list-item handled elsewhere
        break;
    case HTM_TAG_SUMMARY:
        // UA default: list-style: inside disclosure-closed
        // summary elements use inside marker position (disclosure triangle before text)
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->list_style_position = (CssEnum)1;  // 1 = inside
        block->blk->list_style_type = CSS_VALUE_DISCLOSURE_CLOSED;
        break;
    // ========== Table elements ==========
    case HTM_TAG_TABLE: {
        // HTML UA default: border-spacing: 2px (CSS spec default is 0, but HTML tables use 2px)
        // This is applied at the TableProp level in layout_table.cpp, not here in block props

        // Handle HTML width attribute (e.g., width="85%" or width="600")
        const char* width_attr = elmt->get_attribute("width");
        if (width_attr) {
            size_t value_len = strlen(width_attr);
            if (value_len > 0) {
                // Check if it's a percentage value (ends with %)
                if (width_attr[value_len - 1] == '%') {
                    // Parse percentage value
                    StrView width_view = strview_init(width_attr, value_len - 1);
                    float percent = strview_to_int(&width_view);
                    if (percent > 0 && percent <= 100) {
                        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                        block->blk->given_width_percent = percent;
                        // Calculate resolved width from container
                        float container_width = lycon->block.content_width > 0
                            ? lycon->block.content_width
                            : (lycon->line.right - lycon->line.left);
                        if (container_width > 0) {
                            lycon->block.given_width = container_width * percent / 100.0f;
                            block->blk->given_width = lycon->block.given_width;
                            log_debug("[HTML] TABLE width attribute: %.0f%% -> %.1fpx", percent, lycon->block.given_width);
                        }
                    }
                } else {
                    // Parse pixel value
                    StrView width_view = strview_init(width_attr, value_len);
                    float width = strview_to_int(&width_view);
                    if (width > 0) {
                        lycon->block.given_width = width;  // CSS logical pixels
                        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                        block->blk->given_width = lycon->block.given_width;
                        log_debug("[HTML] TABLE width attribute: %.0fpx", width);
                    }
                }
            }
        }
        // Handle HTML height attribute (e.g., height="200")
        const char* height_attr = elmt->get_attribute("height");
        if (height_attr) {
            size_t value_len = strlen(height_attr);
            if (value_len > 0) {
                // Parse pixel value (percentages for table height are less common)
                StrView height_view = strview_init(height_attr, value_len);
                float height = strview_to_int(&height_view);
                if (height > 0) {
                    lycon->block.given_height = height;  // CSS logical pixels
                    if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                    block->blk->given_height = lycon->block.given_height;
                    log_debug("[HTML] TABLE height attribute: %.0fpx", height);
                }
            }
        }
        // Handle HTML bgcolor attribute (e.g., bgcolor="#f6f6ef")
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            if (!block->bound->background) { block->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp)); }
            block->bound->background->color = bg_color;
            log_debug("[HTML] TABLE bgcolor attribute: #%02x%02x%02x", bg_color.r, bg_color.g, bg_color.b);
        }
        // Handle HTML border attribute (e.g., border="5")
        // Per WHATWG 15.3.10: table[border] { border-style: outset; border-color: grey; }
        // border-width is the attribute value in pixels
        const char* border_attr = elmt->get_attribute("border");
        if (border_attr) {
            StrView bv = strview_init(border_attr, strlen(border_attr));
            float border_width = strview_to_int(&bv);
            if (border_width >= 0) {
                if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
                if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
                block->bound->border->width.top = block->bound->border->width.right =
                    block->bound->border->width.bottom = block->bound->border->width.left = border_width;
                block->bound->border->width.top_specificity = block->bound->border->width.right_specificity =
                    block->bound->border->width.bottom_specificity = block->bound->border->width.left_specificity = -1;
                block->bound->border->top_style = block->bound->border->right_style =
                    block->bound->border->bottom_style = block->bound->border->left_style = CSS_VALUE_OUTSET;
                // border-color: grey (128, 128, 128)
                block->bound->border->top_color.r = block->bound->border->right_color.r =
                    block->bound->border->bottom_color.r = block->bound->border->left_color.r = 128;
                block->bound->border->top_color.g = block->bound->border->right_color.g =
                    block->bound->border->bottom_color.g = block->bound->border->left_color.g = 128;
                block->bound->border->top_color.b = block->bound->border->right_color.b =
                    block->bound->border->bottom_color.b = block->bound->border->left_color.b = 128;
                block->bound->border->top_color.a = block->bound->border->right_color.a =
                    block->bound->border->bottom_color.a = block->bound->border->left_color.a = 255;
                log_debug("[HTML] TABLE border attribute: %.0fpx outset grey", border_width);
            }
        }
        // HTML spec §14.3.3: <table align="center"> maps to margin-left: auto; margin-right: auto
        // <table align="left|right"> maps to float: left|right
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            size_t alen = strlen(align_attr);
            if (str_ieq_const(align_attr, alen, "center")) {
                if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
                block->bound->margin.left_type = CSS_VALUE_AUTO;
                block->bound->margin.right_type = CSS_VALUE_AUTO;
                block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
                log_debug("[HTML] TABLE align=center: margin-left/right auto");
            } else if (str_ieq_const(align_attr, alen, "left")) {
                if (!block->position) { block->position = alloc_position_prop(lycon); }
                block->position->float_prop = CSS_VALUE_LEFT;
                log_debug("[HTML] TABLE align=left: float left");
            } else if (str_ieq_const(align_attr, alen, "right")) {
                if (!block->position) { block->position = alloc_position_prop(lycon); }
                block->position->float_prop = CSS_VALUE_RIGHT;
                log_debug("[HTML] TABLE align=right: float right");
            }
        }
        break;
    }
    case HTM_TAG_TR: {
        // Handle HTML bgcolor attribute for table rows
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            if (!block->bound->background) { block->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp)); }
            block->bound->background->color = bg_color;
            log_debug("[HTML] TR bgcolor attribute: #%02x%02x%02x", bg_color.r, bg_color.g, bg_color.b);
        }
        break;
    }
    case HTM_TAG_TH: {
        // font-weight: bold;  text-align: center;  vertical-align: middle;
        log_debug("apply default TH styles");
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->font_weight = CSS_VALUE_BOLD;
        block->font->font_weight_numeric = 700;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;  // TH defaults to center
        if (!block->in_line) { block->in_line = alloc_inline_prop(lycon); }
        block->in_line->vertical_align = CSS_VALUE_MIDDLE;

        // Per HTML spec (WHATWG 15.3.8): td, th { padding: 1px; }
        // However, the cellpadding attribute on the parent TABLE overrides this default
        float cellpadding = get_parent_table_cellpadding(elmt);
        if (cellpadding >= 0) {
            // Use cellpadding from parent table (can be 0)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.right =
                block->bound->padding.bottom = block->bound->padding.left = cellpadding;
            block->bound->padding.top_specificity = block->bound->padding.right_specificity =
                block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        } else {
            // No cellpadding attribute - apply UA default of 1px (CSS logical pixels)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.right =
                block->bound->padding.bottom = block->bound->padding.left = 1;
            block->bound->padding.top_specificity = block->bound->padding.right_specificity =
                block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        }

        // Handle HTML align attribute (e.g., align="left", align="right", align="center")
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            if (str_ieq_const(align_attr, strlen(align_attr), "left")) {
                block->blk->text_align = CSS_VALUE_LEFT;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "right")) {
                block->blk->text_align = CSS_VALUE_RIGHT;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "center")) {
                block->blk->text_align = CSS_VALUE_CENTER;
            }
        }
        // Handle HTML valign attribute (e.g., valign="top", valign="middle", valign="bottom")
        const char* valign_attr = elmt->get_attribute("valign");
        // if TH doesn't have valign, inherit from parent TR
        if (!valign_attr) {
            valign_attr = get_parent_tr_valign(elmt);
        }
        if (valign_attr) {
            if (str_ieq_const(valign_attr, strlen(valign_attr), "top")) {
                block->in_line->vertical_align = CSS_VALUE_TOP;
            } else if (str_ieq_const(valign_attr, strlen(valign_attr), "middle")) {
                block->in_line->vertical_align = CSS_VALUE_MIDDLE;
            } else if (str_ieq_const(valign_attr, strlen(valign_attr), "bottom")) {
                block->in_line->vertical_align = CSS_VALUE_BOTTOM;
            }
        }
        // WHATWG 15.3.7: td[nowrap], th[nowrap] → white-space: nowrap
        if (elmt->get_attribute("nowrap")) {
            block->blk->white_space = CSS_VALUE_NOWRAP;
            log_debug("[HTML] TH nowrap attribute -> white-space: nowrap");
        }
        // Handle HTML bgcolor attribute for TH
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            if (!block->bound->background) { block->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp)); }
            block->bound->background->color = bg_color;
            log_debug("[HTML] TH bgcolor attribute: #%02x%02x%02x", bg_color.r, bg_color.g, bg_color.b);
        }
        // Per WHATWG 15.3.10: table[border] td, table[border] th { border: 1px inset grey; }
        {
            float parent_border = get_parent_table_border(elmt);
            if (parent_border > 0) {
                if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
                if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
                block->bound->border->width.top = block->bound->border->width.right =
                    block->bound->border->width.bottom = block->bound->border->width.left = 1;
                block->bound->border->width.top_specificity = block->bound->border->width.right_specificity =
                    block->bound->border->width.bottom_specificity = block->bound->border->width.left_specificity = -1;
                block->bound->border->top_style = block->bound->border->right_style =
                    block->bound->border->bottom_style = block->bound->border->left_style = CSS_VALUE_INSET;
                block->bound->border->top_color.r = block->bound->border->right_color.r =
                    block->bound->border->bottom_color.r = block->bound->border->left_color.r = 128;
                block->bound->border->top_color.g = block->bound->border->right_color.g =
                    block->bound->border->bottom_color.g = block->bound->border->left_color.g = 128;
                block->bound->border->top_color.b = block->bound->border->right_color.b =
                    block->bound->border->bottom_color.b = block->bound->border->left_color.b = 128;
                block->bound->border->top_color.a = block->bound->border->right_color.a =
                    block->bound->border->bottom_color.a = block->bound->border->left_color.a = 255;
                log_debug("[HTML] TH border from parent TABLE: 1px inset grey");
            }
        }
        break;
    }
    case HTM_TAG_TD: {
        // TD defaults to vertical-align: middle (CSS 2.1), text-align: start
        if (!block->in_line) { block->in_line = alloc_inline_prop(lycon); }
        block->in_line->vertical_align = CSS_VALUE_MIDDLE;

        // Set default text-align to left (start) - table cells don't inherit text-align from outside
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_LEFT;  // Default for TD

        // Per HTML spec (WHATWG 15.3.8): td, th { padding: 1px; }
        // However, the cellpadding attribute on the parent TABLE overrides this default
        // Per HTML spec: cellpadding maps to padding-top/right/bottom/left on TD/TH elements
        float cellpadding = get_parent_table_cellpadding(elmt);
        if (cellpadding >= 0) {
            // Use cellpadding from parent table (can be 0)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.right =
                block->bound->padding.bottom = block->bound->padding.left = cellpadding;
            block->bound->padding.top_specificity = block->bound->padding.right_specificity =
                block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        } else {
            // No cellpadding attribute - apply UA default of 1px (CSS logical pixels)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.right =
                block->bound->padding.bottom = block->bound->padding.left = 1;
            block->bound->padding.top_specificity = block->bound->padding.right_specificity =
                block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        }

        // Handle HTML align attribute (e.g., align="left", align="right", align="center")
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            if (str_ieq_const(align_attr, strlen(align_attr), "left")) {
                block->blk->text_align = CSS_VALUE_LEFT;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "right")) {
                block->blk->text_align = CSS_VALUE_RIGHT;
            } else if (str_ieq_const(align_attr, strlen(align_attr), "center")) {
                block->blk->text_align = CSS_VALUE_CENTER;
            }
        }
        // Handle HTML valign attribute (e.g., valign="top", valign="middle", valign="bottom")
        const char* valign_attr = elmt->get_attribute("valign");
        // if TD doesn't have valign, inherit from parent TR
        if (!valign_attr) {
            valign_attr = get_parent_tr_valign(elmt);
        }
        if (valign_attr) {
            if (str_ieq_const(valign_attr, strlen(valign_attr), "top")) {
                block->in_line->vertical_align = CSS_VALUE_TOP;
            } else if (str_ieq_const(valign_attr, strlen(valign_attr), "middle")) {
                block->in_line->vertical_align = CSS_VALUE_MIDDLE;
            } else if (str_ieq_const(valign_attr, strlen(valign_attr), "bottom")) {
                block->in_line->vertical_align = CSS_VALUE_BOTTOM;
            }
        }
        // Handle HTML bgcolor attribute for TD
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            if (!block->bound->background) { block->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp)); }
            block->bound->background->color = bg_color;
            log_debug("[HTML] TD bgcolor attribute: #%02x%02x%02x", bg_color.r, bg_color.g, bg_color.b);
        }
        // WHATWG 15.3.7: td[nowrap], th[nowrap] → white-space: nowrap
        if (elmt->get_attribute("nowrap")) {
            block->blk->white_space = CSS_VALUE_NOWRAP;
            log_debug("[HTML] TD nowrap attribute -> white-space: nowrap");
        }
        // Per WHATWG 15.3.10: table[border] td, table[border] th { border: 1px inset grey; }
        {
            float parent_border = get_parent_table_border(elmt);
            if (parent_border > 0) {
                if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
                if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
                block->bound->border->width.top = block->bound->border->width.right =
                    block->bound->border->width.bottom = block->bound->border->width.left = 1;
                block->bound->border->width.top_specificity = block->bound->border->width.right_specificity =
                    block->bound->border->width.bottom_specificity = block->bound->border->width.left_specificity = -1;
                block->bound->border->top_style = block->bound->border->right_style =
                    block->bound->border->bottom_style = block->bound->border->left_style = CSS_VALUE_INSET;
                block->bound->border->top_color.r = block->bound->border->right_color.r =
                    block->bound->border->bottom_color.r = block->bound->border->left_color.r = 128;
                block->bound->border->top_color.g = block->bound->border->right_color.g =
                    block->bound->border->bottom_color.g = block->bound->border->left_color.g = 128;
                block->bound->border->top_color.b = block->bound->border->right_color.b =
                    block->bound->border->bottom_color.b = block->bound->border->left_color.b = 128;
                block->bound->border->top_color.a = block->bound->border->right_color.a =
                    block->bound->border->bottom_color.a = block->bound->border->left_color.a = 255;
                log_debug("[HTML] TD border from parent TABLE: 1px inset grey");
            }
        }
        break;
    }
    case HTM_TAG_CAPTION:
        // table caption: text-align center
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        break;
    // ========== Form elements ==========
    case HTM_TAG_FIELDSET:
        // fieldset: border and padding (CSS logical pixels)
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left = 2;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        // Chrome uses groove style with gray color for fieldset borders
        block->bound->border->top_style = block->bound->border->right_style =
            block->bound->border->bottom_style = block->bound->border->left_style = CSS_VALUE_GROOVE;
        block->bound->border->top_color.r = block->bound->border->right_color.r =
            block->bound->border->bottom_color.r = block->bound->border->left_color.r = 192;
        block->bound->border->top_color.g = block->bound->border->right_color.g =
            block->bound->border->bottom_color.g = block->bound->border->left_color.g = 192;
        block->bound->border->top_color.b = block->bound->border->right_color.b =
            block->bound->border->bottom_color.b = block->bound->border->left_color.b = 192;
        block->bound->border->top_color.a = block->bound->border->right_color.a =
            block->bound->border->bottom_color.a = block->bound->border->left_color.a = 255;
        block->bound->padding.top = 0.35 * lycon->font.style->font_size;
        block->bound->padding.bottom = 0.625 * lycon->font.style->font_size;
        block->bound->padding.left = block->bound->padding.right = 0.75 * lycon->font.style->font_size;
        block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
            block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        block->bound->margin.left = block->bound->margin.right = 2;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    case HTM_TAG_LEGEND:
        // legend: padding (CSS logical pixels)
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->padding.left = block->bound->padding.right = 2;
        block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        break;
    case HTM_TAG_BUTTON: {
        // button: centered text, some padding, inline-block display with flow inner
        // All values in CSS logical pixels
        // Guard: only allocate if not already allocated (avoid re-allocating on repeated style resolution)
        if (!block->form) {
            block->item_prop_type = DomElement::ITEM_PROP_FORM;
            block->form = (FormControlProp*)alloc_prop(lycon, sizeof(FormControlProp));
            new (block->form) FormControlProp();
            block->form->control_type = FORM_CONTROL_BUTTON;
            if (block->has_attribute("disabled")) block->form->disabled = 1;
        }

        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = CSS_VALUE_FLOW;  // button has flow children
        // Button sizing is determined by content - will be shrink-to-fit

        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        // Chrome UA: font-size 13.3333px, font-family Arial for form controls
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->font_size = 13.3333f;
        block->font->family = (char*)"Arial";
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->padding.top = block->bound->padding.bottom = FormDefaults::BUTTON_PADDING_V;
        block->bound->padding.left = block->bound->padding.right = FormDefaults::BUTTON_PADDING_H;
        block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
            block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        // Default border: 2px outset (Chrome UA stylesheet) — same as <input type=button/submit>
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        block->bound->border->width.top = block->bound->border->width.bottom =
            block->bound->border->width.left = block->bound->border->width.right = FormDefaults::BUTTON_BORDER;
        block->bound->border->width.top_specificity = block->bound->border->width.bottom_specificity =
            block->bound->border->width.left_specificity = block->bound->border->width.right_specificity = -1;
        block->bound->border->top_style = block->bound->border->bottom_style =
            block->bound->border->left_style = block->bound->border->right_style = CSS_VALUE_OUTSET;
        break;
    }
    case HTM_TAG_INPUT: {
        // Allocate form control prop - all values in CSS logical pixels
        // Guard: only allocate if not already allocated (avoid re-allocating on repeated style resolution)
        if (!block->form) {
            block->item_prop_type = DomElement::ITEM_PROP_FORM;
            block->form = (FormControlProp*)alloc_prop(lycon, sizeof(FormControlProp));
            new (block->form) FormControlProp();  // placement new for constructor

            // Parse type attribute
            const char* type = block->get_attribute("type");
            block->form->input_type = type;
            block->form->control_type = get_input_control_type(type);

            // Parse common attributes
            block->form->value = block->get_attribute("value");
            block->form->placeholder = block->get_attribute("placeholder");
            block->form->name = block->get_attribute("name");

            // Parse size attribute for text inputs
            const char* size_attr = block->get_attribute("size");
            if (size_attr) {
                block->form->size = (int)str_to_int64_default(size_attr, strlen(size_attr), 0);
                if (block->form->size <= 0) block->form->size = FormDefaults::TEXT_SIZE_CHARS;
            }

            // Parse state attributes - check both attribute and pseudo_state
            // The pseudo_state may have been set during DOM tree building
            if (block->has_attribute("disabled") ||
                (block->pseudo_state & PSEUDO_STATE_DISABLED)) {
                block->form->disabled = 1;
            }
            if (block->has_attribute("readonly")) block->form->readonly = 1;
            if (block->has_attribute("checked") ||
                (block->pseudo_state & PSEUDO_STATE_CHECKED)) {
                block->form->checked = 1;
            }
            if (block->has_attribute("required")) block->form->required = 1;
        }

        // Set display and intrinsic size based on control type
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }

        switch (block->form->control_type) {
        case FORM_CONTROL_HIDDEN:
            block->display.outer = CSS_VALUE_NONE;
            break;
        case FORM_CONTROL_CHECKBOX:
        case FORM_CONTROL_RADIO:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            block->form->intrinsic_width = FormDefaults::CHECK_SIZE;
            block->form->intrinsic_height = FormDefaults::CHECK_SIZE;
            // Set given_width/height so layout algorithm uses intrinsic size
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
            // Default margin: Chrome UA stylesheet
            // checkbox: 3px 3px 3px 4px, radio: 3px 3px 0px 5px
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->margin.top = 3; block->bound->margin.right = FormDefaults::CHECK_MARGIN;
            block->bound->margin.bottom = (block->form->control_type == FORM_CONTROL_RADIO) ? 0 : FormDefaults::CHECK_MARGIN;
            block->bound->margin.left = (block->form->control_type == FORM_CONTROL_RADIO) ? FormDefaults::RADIO_MARGIN_LEFT : FormDefaults::CHECKBOX_MARGIN_LEFT;
            block->bound->margin.top_specificity = block->bound->margin.right_specificity =
                block->bound->margin.bottom_specificity = block->bound->margin.left_specificity = -1;
            break;
        case FORM_CONTROL_BUTTON:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            // Button intrinsic size depends on value text - computed in layout
            // Default padding: 1px 6px (Chrome UA stylesheet)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.bottom = FormDefaults::BUTTON_PADDING_V;
            block->bound->padding.left = block->bound->padding.right = FormDefaults::BUTTON_PADDING_H;
            block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
                block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
            // Default border: 2px outset (Chrome UA stylesheet)
            if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
            block->bound->border->width.top = block->bound->border->width.bottom =
                block->bound->border->width.left = block->bound->border->width.right = FormDefaults::BUTTON_BORDER;
            block->bound->border->width.top_specificity = block->bound->border->width.bottom_specificity =
                block->bound->border->width.left_specificity = block->bound->border->width.right_specificity = -1;
            block->bound->border->top_style = block->bound->border->bottom_style =
                block->bound->border->left_style = block->bound->border->right_style = CSS_VALUE_OUTSET;
            break;
        case FORM_CONTROL_IMAGE:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            // Image button is a replaced element; use broken-image fallback dimensions
            block->form->intrinsic_width = FormDefaults::IMAGE_INPUT_WIDTH;
            block->form->intrinsic_height = FormDefaults::IMAGE_INPUT_HEIGHT;
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
            break;
        case FORM_CONTROL_RANGE: {
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            block->form->intrinsic_width = FormDefaults::RANGE_WIDTH;
            // On macOS Chrome, a range with a list attribute (tick marks) renders taller (22px) than one without (16px)
            const char* list_attr = block->get_attribute("list");
            block->form->intrinsic_height = list_attr ? FormDefaults::RANGE_HEIGHT_WITH_LIST : FormDefaults::RANGE_HEIGHT;
            // Set given_width/height so layout algorithm uses intrinsic size (border-box)
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
            // Chrome default margin: 2px all sides
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->margin.top = block->bound->margin.right =
                block->bound->margin.bottom = block->bound->margin.left = 2;
            block->bound->margin.top_specificity = block->bound->margin.right_specificity =
                block->bound->margin.bottom_specificity = block->bound->margin.left_specificity = -1;
            // Parse range attributes
            const char* min_attr = block->get_attribute("min");
            const char* max_attr = block->get_attribute("max");
            const char* step_attr = block->get_attribute("step");
            if (min_attr) block->form->range_min = str_to_double_default(min_attr, strlen(min_attr), 0.0);
            if (max_attr) block->form->range_max = str_to_double_default(max_attr, strlen(max_attr), 0.0);
            if (step_attr) block->form->range_step = str_to_double_default(step_attr, strlen(step_attr), 0.0);
            if (block->form->value) {
                float val = (float)str_to_double_default(block->form->value, strlen(block->form->value), 0.0);
                block->form->range_value = (val - block->form->range_min) /
                    (block->form->range_max - block->form->range_min);
            }
            break;
        }
        default:  // FORM_CONTROL_TEXT
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            // File inputs: Chrome renders as 253×21 border-box with no external border/padding
            // (the internal "Choose File" button + label text are shadow DOM)
            if (block->form->input_type && strcmp(block->form->input_type, "file") == 0) {
                block->form->intrinsic_width = 253.0f;
                block->form->intrinsic_height = FormDefaults::TEXT_HEIGHT;
                if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                block->blk->given_width = 253.0f;
                block->blk->given_height = FormDefaults::TEXT_HEIGHT;
                block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
                break;
            }
            block->form->intrinsic_width = FormDefaults::TEXT_WIDTH;
            block->form->intrinsic_height = FormDefaults::TEXT_HEIGHT;
            // Don't set given_width/given_height — layout_form_control computes
            // intrinsic size dynamically from size attribute and font metrics
            // Default border for text inputs (CSS logical pixels)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
            block->bound->border->width.top = block->bound->border->width.right =
                block->bound->border->width.bottom = block->bound->border->width.left = FormDefaults::TEXT_BORDER;
            block->bound->border->top_style = block->bound->border->right_style =
                block->bound->border->bottom_style = block->bound->border->left_style = CSS_VALUE_SOLID;
            block->bound->border->top_color.r = block->bound->border->right_color.r =
                block->bound->border->bottom_color.r = block->bound->border->left_color.r = 118;
            block->bound->border->top_color.g = block->bound->border->right_color.g =
                block->bound->border->bottom_color.g = block->bound->border->left_color.g = 118;
            block->bound->border->top_color.b = block->bound->border->right_color.b =
                block->bound->border->bottom_color.b = block->bound->border->left_color.b = 118;
            block->bound->border->top_color.a = block->bound->border->right_color.a =
                block->bound->border->bottom_color.a = block->bound->border->left_color.a = 255;
            block->bound->padding.top = block->bound->padding.bottom = FormDefaults::TEXT_PADDING_V;
            block->bound->padding.left = block->bound->padding.right = FormDefaults::TEXT_PADDING_H;
            break;
        }
        break;
    }
    case HTM_TAG_SELECT: {
        // All values in CSS logical pixels
        // Guard: only allocate if not already allocated (avoid re-allocating on repeated style resolution)
        if (!block->form) {
            block->item_prop_type = DomElement::ITEM_PROP_FORM;
            block->form = (FormControlProp*)alloc_prop(lycon, sizeof(FormControlProp));
            new (block->form) FormControlProp();
            block->form->control_type = FORM_CONTROL_SELECT;
            block->form->name = block->get_attribute("name");
            if (block->has_attribute("disabled")) block->form->disabled = 1;
            if (block->has_attribute("multiple")) block->form->multiple = 1;
            // HTML §4.10.7: size attr specifies visible rows in listbox mode
            const char* size_attr = block->get_attribute("size");
            if (size_attr) {
                int size_val = (int)str_to_int64_default(size_attr, strlen(size_attr), 0);
                if (size_val > 0) block->form->select_size = size_val;
            }

            // Count options and find selected index
            int option_count = 0;
            int selected_idx = -1;
            DomNode* child = block->first_child;
            while (child) {
                if (child->is_element()) {
                    DomElement* child_elem = (DomElement*)child;
                    if (child_elem->tag() == HTM_TAG_OPTION) {
                        if (child_elem->has_attribute("selected") && selected_idx < 0) {
                            selected_idx = option_count;
                        }
                        option_count++;
                    } else if (child_elem->tag() == HTM_TAG_OPTGROUP) {
                        // Count options inside optgroup
                        DomNode* opt_child = child_elem->first_child;
                        while (opt_child) {
                            if (opt_child->is_element()) {
                                DomElement* opt_elem = (DomElement*)opt_child;
                                if (opt_elem->tag() == HTM_TAG_OPTION) {
                                    if (opt_elem->has_attribute("selected") && selected_idx < 0) {
                                        selected_idx = option_count;
                                    }
                                    option_count++;
                                }
                            }
                            opt_child = opt_child->next_sibling;
                        }
                    }
                }
                child = child->next_sibling;
            }
            block->form->option_count = option_count;
            block->form->selected_index = (selected_idx >= 0) ? selected_idx : (option_count > 0 ? 0 : -1);
        }
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        block->form->intrinsic_width = FormDefaults::SELECT_WIDTH;
        block->form->intrinsic_height = FormDefaults::SELECT_HEIGHT;
        // Don't set given_width — let CSS cascade or calc_select_size set it
        // Set given_height so layout uses intrinsic height (border-box); will be updated by calc_select_size for listbox
        lycon->block.given_height = block->form->intrinsic_height;
        block->blk->given_height = block->form->intrinsic_height;
        // Default border
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left = 1;
        break;
    }
    case HTM_TAG_TEXTAREA: {
        // All values in CSS logical pixels
        // Guard: only allocate if not already allocated (avoid re-allocating on repeated style resolution)
        if (!block->form) {
            block->item_prop_type = DomElement::ITEM_PROP_FORM;
            block->form = (FormControlProp*)alloc_prop(lycon, sizeof(FormControlProp));
            new (block->form) FormControlProp();
            block->form->control_type = FORM_CONTROL_TEXTAREA;
            block->form->name = block->get_attribute("name");
            block->form->placeholder = block->get_attribute("placeholder");
            if (block->has_attribute("disabled")) block->form->disabled = 1;
            if (block->has_attribute("readonly")) block->form->readonly = 1;
            // Parse cols/rows
            const char* cols_attr = block->get_attribute("cols");
            const char* rows_attr = block->get_attribute("rows");
            if (cols_attr) block->form->cols = (int)str_to_int64_default(cols_attr, strlen(cols_attr), 0);
            if (rows_attr) block->form->rows = (int)str_to_int64_default(rows_attr, strlen(rows_attr), 0);
        }
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        // Note: textarea uses content-box (CSS default), same as Chrome UA
        // Intrinsic size: Chrome default 182x36 border-box (20 cols, 2 rows)
        block->form->intrinsic_width = 182.0f;
        block->form->intrinsic_height = 36.0f;
        // Don't set given_width/given_height — layout_form_control computes
        // intrinsic size dynamically from cols/rows and font metrics
        // Default border and padding
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left = FormDefaults::TEXTAREA_BORDER;
        block->bound->border->top_style = block->bound->border->right_style =
            block->bound->border->bottom_style = block->bound->border->left_style = CSS_VALUE_SOLID;
        block->bound->border->top_color.r = block->bound->border->right_color.r =
            block->bound->border->bottom_color.r = block->bound->border->left_color.r = 118;
        block->bound->border->top_color.g = block->bound->border->right_color.g =
            block->bound->border->bottom_color.g = block->bound->border->left_color.g = 118;
        block->bound->border->top_color.b = block->bound->border->right_color.b =
            block->bound->border->bottom_color.b = block->bound->border->left_color.b = 118;
        block->bound->border->top_color.a = block->bound->border->right_color.a =
            block->bound->border->bottom_color.a = block->bound->border->left_color.a = 255;
        block->bound->padding.top = block->bound->padding.bottom =
            block->bound->padding.left = block->bound->padding.right = FormDefaults::TEXTAREA_PADDING;
        break;
    }
    case HTM_TAG_METER: {
        // Meter: inline-block replaced element, Chrome default 80x16
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = RDT_DISPLAY_REPLACED;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        block->blk->given_width = FormDefaults::METER_WIDTH;
        block->blk->given_height = FormDefaults::METER_HEIGHT;
        lycon->block.given_width = FormDefaults::METER_WIDTH;
        lycon->block.given_height = FormDefaults::METER_HEIGHT;
        break;
    }
    case HTM_TAG_PROGRESS: {
        // Progress: inline-block replaced element, Chrome default 160x16
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = RDT_DISPLAY_REPLACED;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        block->blk->given_width = FormDefaults::PROGRESS_WIDTH;
        block->blk->given_height = FormDefaults::PROGRESS_HEIGHT;
        lycon->block.given_width = FormDefaults::PROGRESS_WIDTH;
        lycon->block.given_height = FormDefaults::PROGRESS_HEIGHT;
        break;
    }
    case HTM_TAG_LABEL:
        // label is inline by default, no special styling
        break;
    case HTM_TAG_OPTION:
    case HTM_TAG_OPTGROUP: {
        // HTML spec: option/optgroup inside select/datalist are 0×0 (UA rendered).
        // Outside select/datalist, they render as normal block flow content.
        uintptr_t parent_tag = elmt->parent ? elmt->parent->tag() : 0;
        if (parent_tag == HTM_TAG_SELECT || parent_tag == HTM_TAG_DATALIST ||
            parent_tag == HTM_TAG_OPTGROUP) {
            block->display.outer = CSS_VALUE_BLOCK;
            block->display.inner = CSS_VALUE_FLOW;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            block->blk->given_width = 0;
            block->blk->given_height = 0;
        }
        break;
    }
    case HTM_TAG_DATALIST:
        // Datalist should be completely hidden (display:none)
        block->display.outer = CSS_VALUE_NONE;
        block->display.inner = CSS_VALUE_NONE;
        break;
    // ========== Semantic/sectioning elements with no visual default ==========
    case HTM_TAG_ARTICLE:  case HTM_TAG_SECTION:  case HTM_TAG_NAV:
    case HTM_TAG_ASIDE:  case HTM_TAG_HEADER:  case HTM_TAG_FOOTER:
    case HTM_TAG_MAIN:  case HTM_TAG_HGROUP:  case HTM_TAG_DETAILS:
        // these are block-level but have no special default styling
        break;
    }

    // Handle HTML 'dir' attribute (global, applies to all elements)
    // CSS 2.1 §9.10: The 'dir' attribute maps to the CSS 'direction' property
    const char* dir_attr = elmt->get_attribute("dir");
    if (dir_attr) {
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        if (str_ieq_const(dir_attr, strlen(dir_attr), "rtl")) {
            block->blk->direction = CSS_VALUE_RTL;
            log_debug("[HTML] dir attribute: rtl");
        } else if (str_ieq_const(dir_attr, strlen(dir_attr), "ltr")) {
            block->blk->direction = CSS_VALUE_LTR;
            log_debug("[HTML] dir attribute: ltr");
        } else if (str_ieq_const(dir_attr, strlen(dir_attr), "auto")) {
            // HTML5 §14.3.4: dir="auto" — resolve direction from first strong character
            CssEnum resolved = resolve_dir_auto(static_cast<DomElement*>(elmt));
            block->blk->direction = resolved;
            log_debug("[HTML] dir attribute: auto -> %s",
                      resolved == CSS_VALUE_RTL ? "rtl" : "ltr");
        }
    }
}
