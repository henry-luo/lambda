#include "layout.hpp"

// Direct declaration of the actual C symbol (compiler will add underscore)
extern "C" int strview_to_int(StrView* s);

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
        // 1px top border
        block->bound->border->width.top = 1 * lycon->ui_context->pixel_ratio;
        block->bound->border->width.left = block->bound->border->width.right = block->bound->border->width.bottom = 0;
        block->bound->border->width.top_specificity = block->bound->border->width.left_specificity =
            block->bound->border->width.right_specificity = block->bound->border->width.bottom_specificity = -1;
        // 0.5em margin
        block->bound->margin.top = block->bound->margin.bottom =
            block->bound->margin.left = block->bound->margin.right = 0.5 * lycon->font.style->font_size;
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
            log_debug("font color: %s", color_attr);
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
        if (valign_attr) {
            if (strcasecmp(valign_attr, "top") == 0) {
                block->in_line->vertical_align = CSS_VALUE_TOP;
            } else if (strcasecmp(valign_attr, "middle") == 0) {
                block->in_line->vertical_align = CSS_VALUE_MIDDLE;
            } else if (strcasecmp(valign_attr, "bottom") == 0) {
                block->in_line->vertical_align = CSS_VALUE_BOTTOM;
            }
        }
        break;
    }
    case HTM_TAG_TD: {
        // table data: default padding 1px (browsers vary), vertical-align: middle;
        // TD defaults to text-align: start (left in LTR) - does not inherit from outside table
        if (!block->bound) { block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp)); }
        block->bound->padding.top = block->bound->padding.right =
            block->bound->padding.bottom = block->bound->padding.left = 1 * lycon->ui_context->pixel_ratio;
        block->bound->padding.top_specificity = block->bound->padding.right_specificity =
            block->bound->padding.bottom_specificity = block->bound->padding.left_specificity = -1;
        if (!block->in_line) { block->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
        block->in_line->vertical_align = CSS_VALUE_MIDDLE;

        // Set default text-align to left (start) - table cells don't inherit text-align from outside
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_LEFT;  // Default for TD

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
        if (valign_attr) {
            if (strcasecmp(valign_attr, "top") == 0) {
                block->in_line->vertical_align = CSS_VALUE_TOP;
            } else if (strcasecmp(valign_attr, "middle") == 0) {
                block->in_line->vertical_align = CSS_VALUE_MIDDLE;
            } else if (strcasecmp(valign_attr, "bottom") == 0) {
                block->in_line->vertical_align = CSS_VALUE_BOTTOM;
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
