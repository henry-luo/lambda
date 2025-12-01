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
        break;
    }
    case HTM_TAG_H1:
        em_size = 2;  // 2em
        goto HEADING_PROP;
    case HTM_TAG_H2:
        em_size = 1.5;  // 1.5em
        goto HEADING_PROP;
    case HTM_TAG_H3:
        em_size = 1.17;  // 1.17em
        goto HEADING_PROP;
    case HTM_TAG_H4:
        em_size = 1;  // 1em
        goto HEADING_PROP;
    case HTM_TAG_H5:
        em_size = 0.83;  // 0.83em
        goto HEADING_PROP;
    case HTM_TAG_H6:
        em_size = 0.67;  // 0.67em
        HEADING_PROP:
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->font_size = lycon->font.style->font_size * em_size;
        block->font->font_weight = CSS_VALUE_BOLD;
        break;
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
    case HTM_TAG_TH:
        // font-weight: bold;  text-align: center;
        log_debug("apply default TH styles");
        if (!block->font) { block->font = alloc_font_prop(lycon); }
        block->font->font_weight = CSS_VALUE_BOLD;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = CSS_VALUE_CENTER;
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
    }
}
