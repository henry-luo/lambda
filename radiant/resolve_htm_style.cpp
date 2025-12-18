#include "layout.hpp"
#include <cstdlib>  // for strtol

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

// Get the cellpadding attribute value from the parent TABLE element
// Returns -1 if no cellpadding attribute is found, otherwise returns the pixel value
// The HTML spec says: cellpadding on TABLE maps to padding on TD/TH cells
static float get_parent_table_cellpadding(DomNode* elmt, float pixel_ratio) {
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
                        return cellpadding * pixel_ratio;
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

void apply_element_default_style(LayoutContext* lycon, DomNode* elmt) {
    ViewSpan* span = (ViewSpan*)elmt;  ViewBlock* block = (ViewBlock*)elmt;
    float em_size = 0;  uintptr_t elmt_name = elmt->tag();
    switch (elmt_name) {
    case HTM_TAG_BODY: {
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // margin: 8px
        block->bound->margin.top = block->bound->margin.right =
            block->bound->margin.bottom = block->bound->margin.left = 8 * lycon->ui_context->pixel_ratio;
        block->bound->margin.top_specificity = block->bound->margin.right_specificity =
            block->bound->margin.bottom_specificity = block->bound->margin.left_specificity = -1;
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
        block->font->font_weight = CSS_VALUE_BOLD;
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
        break;
    }
    case HTM_TAG_P:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        // margin: 1em 0;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        break;
    case HTM_TAG_UL:  case HTM_TAG_OL:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->list_style_type = elmt_name == HTM_TAG_UL ? CSS_VALUE_DISC : CSS_VALUE_DECIMAL;
        if (!block->bound) {
            block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        // margin: 1em 0; padding: 0 0 0 40px;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        block->bound->padding.left = 40 * lycon->ui_context->pixel_ratio;
        block->bound->padding.left_specificity = -1;
        break;
    case HTM_TAG_CENTER:
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        break;
    case HTM_TAG_IMG:  { // get html width and height (before the css styles)
        size_t value_len;  const char *value;
        value = elmt->get_attribute("width");
        if (value) {
            value_len = strlen(value);
            StrView width_view = strview_init(value, value_len);
            float width = strview_to_int(&width_view);
            if (width >= 0) lycon->block.given_width = width * lycon->ui_context->pixel_ratio;
            // else width attr ignored
        }
        value = elmt->get_attribute("height");
        if (value) {
            value_len = strlen(value);
            StrView height_view = strview_init(value, value_len);
            float height = strview_to_int(&height_view);
            if (height >= 0) lycon->block.given_height = height * lycon->ui_context->pixel_ratio;
            // else height attr ignored
        }
        break;
    }
    case HTM_TAG_IFRAME:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // todo: inset border style
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left = 1 * lycon->ui_context->pixel_ratio;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        if (!block->scroller) { block->scroller = alloc_scroll_prop(lycon); }
        block->scroller->overflow_x = CSS_VALUE_AUTO;
        block->scroller->overflow_y = CSS_VALUE_AUTO;
        // default iframe size to 300 x 200
        lycon->block.given_width = 300 * lycon->ui_context->pixel_ratio;
        lycon->block.given_height = 200 * lycon->ui_context->pixel_ratio;
        break;
    case HTM_TAG_HR:
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        // hr default: 1px border on all sides (creates 2px height from border-top + border-bottom)
        // This matches browser UA stylesheet behavior
        block->bound->border->width.top = block->bound->border->width.bottom = 1 * lycon->ui_context->pixel_ratio;
        block->bound->border->width.left = block->bound->border->width.right = 1 * lycon->ui_context->pixel_ratio;
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
        block->bound->margin.top = block->bound->margin.bottom = 8 * lycon->ui_context->pixel_ratio;
        block->bound->margin.left = block->bound->margin.right = 0;
        block->bound->margin.left_type = CSS_VALUE_AUTO;
        block->bound->margin.right_type = CSS_VALUE_AUTO;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity =
            block->bound->margin.left_specificity = block->bound->margin.right_specificity = -1;
        break;
    case HTM_TAG_B:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_weight = CSS_VALUE_BOLD;
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
            if (!span->in_line) { span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
            span->in_line->color = parse_html_color(color_attr);
            log_debug("HTM_TAG_FONT color: %s -> rgb(%d,%d,%d)", color_attr,
                      span->in_line->color.r, span->in_line->color.g, span->in_line->color.b);
        }
        // Handle font size attribute (deprecated HTML but still supported)
        // size="1" = x-small (10px), size="2" = small (13px), size="3" = medium (16px, default)
        // size="4" = large (18px), size="5" = x-large (24px), size="6" = xx-large (32px), size="7" = 48px
        const char* size_attr = span->get_attribute("size");
        if (size_attr) {
            int size_value = atoi(size_attr);
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
                        int delta = atoi(size_attr);
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
            span->font->font_size = font_size * lycon->ui_context->pixel_ratio;
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
        if (!span->in_line) { span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
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
        break;
    case HTM_TAG_EM:  case HTM_TAG_CITE:  case HTM_TAG_DFN:  case HTM_TAG_VAR:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_style = CSS_VALUE_ITALIC;
        break;
    case HTM_TAG_CODE:  case HTM_TAG_KBD:  case HTM_TAG_SAMP:  case HTM_TAG_TT:
        // monospace font family
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->family = (char*)"monospace";
        break;
    case HTM_TAG_MARK:
        // yellow background highlight - handled via background property on block
        // Note: InlineProp doesn't have bg_color; would need BackgroundProp
        break;
    case HTM_TAG_SMALL:
        // font-size: smaller (0.83em)
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.style->font_size * 0.83;
        break;
    case HTM_TAG_BIG:
        // font-size: larger (1.17em) - deprecated but still supported
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.style->font_size * 1.17;
        break;
    case HTM_TAG_SUB:
        // subscript: smaller font, lowered baseline
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.style->font_size * 0.83;
        if (!span->in_line) { span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
        span->in_line->vertical_align = CSS_VALUE_SUB;
        break;
    case HTM_TAG_SUP:
        // superscript: smaller font, raised baseline
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.style->font_size * 0.83;
        if (!span->in_line) { span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
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
    case HTM_TAG_PRE:  case HTM_TAG_LISTING:  case HTM_TAG_XMP:
        // preformatted: monospace, preserve whitespace, margin 1em 0
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->family = (char*)"monospace";
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->white_space = CSS_VALUE_PRE;
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        break;
    case HTM_TAG_BLOCKQUOTE:
        // margin: 1em 40px
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size;
        block->bound->margin.left = block->bound->margin.right = 40 * lycon->ui_context->pixel_ratio;
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
        block->bound->margin.left = block->bound->margin.right = 40 * lycon->ui_context->pixel_ratio;
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
        block->bound->margin.left = 40 * lycon->ui_context->pixel_ratio;
        block->bound->margin.left_specificity = -1;
        break;
    case HTM_TAG_DT:
        // definition term: bold (common style, not strictly default)
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->font_weight = CSS_VALUE_BOLD;
        break;
    case HTM_TAG_LI:
        // list item: display list-item handled elsewhere
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
                        lycon->block.given_width = width * lycon->ui_context->pixel_ratio;
                        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                        block->blk->given_width = lycon->block.given_width;
                        log_debug("[HTML] TABLE width attribute: %.0fpx", width);
                    }
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
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;  // TH defaults to center
        if (!block->in_line) { block->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
        block->in_line->vertical_align = CSS_VALUE_MIDDLE;

        // Per HTML spec (WHATWG 15.3.8): td, th { padding: 1px; }
        // However, the cellpadding attribute on the parent TABLE overrides this default
        float cellpadding = get_parent_table_cellpadding(elmt, lycon->ui_context->pixel_ratio);
        if (cellpadding >= 0) {
            // Use cellpadding from parent table (can be 0)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.right =
                block->bound->padding.bottom = block->bound->padding.left = cellpadding;
            block->bound->padding.top_specificity = block->bound->padding.right_specificity =
                block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        } else {
            // No cellpadding attribute - apply UA default of 1px
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.right =
                block->bound->padding.bottom = block->bound->padding.left = 1 * lycon->ui_context->pixel_ratio;
            block->bound->padding.top_specificity = block->bound->padding.right_specificity =
                block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        }

        // Handle HTML align attribute (e.g., align="left", align="right", align="center")
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            if (strcasecmp(align_attr, "left") == 0) {
                block->blk->text_align = CSS_VALUE_LEFT;
            } else if (strcasecmp(align_attr, "right") == 0) {
                block->blk->text_align = CSS_VALUE_RIGHT;
            } else if (strcasecmp(align_attr, "center") == 0) {
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
            if (strcasecmp(valign_attr, "top") == 0) {
                block->in_line->vertical_align = CSS_VALUE_TOP;
            } else if (strcasecmp(valign_attr, "middle") == 0) {
                block->in_line->vertical_align = CSS_VALUE_MIDDLE;
            } else if (strcasecmp(valign_attr, "bottom") == 0) {
                block->in_line->vertical_align = CSS_VALUE_BOTTOM;
            }
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
        break;
    }
    case HTM_TAG_TD: {
        // TD defaults to vertical-align: middle (CSS 2.1), text-align: start
        if (!block->in_line) { block->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
        block->in_line->vertical_align = CSS_VALUE_MIDDLE;

        // Set default text-align to left (start) - table cells don't inherit text-align from outside
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_LEFT;  // Default for TD

        // Per HTML spec (WHATWG 15.3.8): td, th { padding: 1px; }
        // However, the cellpadding attribute on the parent TABLE overrides this default
        // Per HTML spec: cellpadding maps to padding-top/right/bottom/left on TD/TH elements
        float cellpadding = get_parent_table_cellpadding(elmt, lycon->ui_context->pixel_ratio);
        if (cellpadding >= 0) {
            // Use cellpadding from parent table (can be 0)
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.right =
                block->bound->padding.bottom = block->bound->padding.left = cellpadding;
            block->bound->padding.top_specificity = block->bound->padding.right_specificity =
                block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        } else {
            // No cellpadding attribute - apply UA default of 1px
            if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
            block->bound->padding.top = block->bound->padding.right =
                block->bound->padding.bottom = block->bound->padding.left = 1 * lycon->ui_context->pixel_ratio;
            block->bound->padding.top_specificity = block->bound->padding.right_specificity =
                block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        }

        // Handle HTML align attribute (e.g., align="left", align="right", align="center")
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            if (strcasecmp(align_attr, "left") == 0) {
                block->blk->text_align = CSS_VALUE_LEFT;
            } else if (strcasecmp(align_attr, "right") == 0) {
                block->blk->text_align = CSS_VALUE_RIGHT;
            } else if (strcasecmp(align_attr, "center") == 0) {
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
            if (strcasecmp(valign_attr, "top") == 0) {
                block->in_line->vertical_align = CSS_VALUE_TOP;
            } else if (strcasecmp(valign_attr, "middle") == 0) {
                block->in_line->vertical_align = CSS_VALUE_MIDDLE;
            } else if (strcasecmp(valign_attr, "bottom") == 0) {
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
        break;
    }
    case HTM_TAG_CAPTION:
        // table caption: text-align center
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        break;
    // ========== Form elements ==========
    case HTM_TAG_FIELDSET:
        // fieldset: border and padding
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        if (!block->bound->border) { block->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp)); }
        block->bound->border->width.top = block->bound->border->width.right =
            block->bound->border->width.bottom = block->bound->border->width.left = 2 * lycon->ui_context->pixel_ratio;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        block->bound->padding.top = block->bound->padding.bottom = 0.35 * lycon->font.style->font_size;
        block->bound->padding.left = block->bound->padding.right = 0.75 * lycon->font.style->font_size;
        block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
            block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style->font_size * 0.5;
        block->bound->margin.top_specificity = block->bound->margin.bottom_specificity = -1;
        break;
    case HTM_TAG_LEGEND:
        // legend: padding
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->padding.left = block->bound->padding.right = 2 * lycon->ui_context->pixel_ratio;
        block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        break;
    case HTM_TAG_BUTTON:
        // button: centered text, some padding
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->padding.top = block->bound->padding.bottom = 1 * lycon->ui_context->pixel_ratio;
        block->bound->padding.left = block->bound->padding.right = 6 * lycon->ui_context->pixel_ratio;
        block->bound->padding.top_specificity = block->bound->padding.bottom_specificity =
            block->bound->padding.left_specificity = block->bound->padding.right_specificity = -1;
        break;
    // ========== Semantic/sectioning elements with no visual default ==========
    case HTM_TAG_ARTICLE:  case HTM_TAG_SECTION:  case HTM_TAG_NAV:
    case HTM_TAG_ASIDE:  case HTM_TAG_HEADER:  case HTM_TAG_FOOTER:
    case HTM_TAG_MAIN:  case HTM_TAG_HGROUP:  case HTM_TAG_DETAILS:
    case HTM_TAG_SUMMARY:
        // these are block-level but have no special default styling
        break;
    }
}
