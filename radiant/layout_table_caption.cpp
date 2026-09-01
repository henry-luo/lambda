#include "layout.hpp"

#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_element.hpp"

void layout_table_caption_content_setup(LayoutContext* lycon, ViewBlock* caption,
                                        float content_width) {
    lycon->block.content_width = content_width;
    lycon->block.content_height = 10000;
    lycon->block.advance_y = 0;

    float inner_left = layout_axis_decoration_start(
        caption->bound ? caption->boundary() : nullptr, LAYOUT_AXIS_X);
    lycon->block.advance_y += layout_axis_decoration_start(
        caption->bound ? caption->boundary() : nullptr, LAYOUT_AXIS_Y);
    lycon->line.left = inner_left;
    lycon->line.right = inner_left + content_width;

    if (caption->font) {
        setup_font(lycon->ui_context, &lycon->font, caption->font);
    }
    setup_line_height(lycon, caption);
    layout_setup_block_font_metrics(lycon);
    if (caption->blk) {
        lycon->block.text_indent = isnan(caption->block()->text_indent_percent)
            ? caption->block()->text_indent
            : content_width * caption->block()->text_indent_percent / 100.0f;
        if (lycon->block.text_indent != 0.0f) lycon->block.is_first_line = true;
    }
    line_reset(lycon);
    if (caption->blk && caption->block_mut()->text_align) {
        lycon->block.text_align = caption->block()->text_align;
    }
    if (caption->blk && caption->block_mut()->direction) {
        lycon->block.direction = caption->block()->direction;
    }
}

float layout_table_caption_content_finish(LayoutContext* lycon, ViewBlock* caption,
                                          float given_height_padding) {
    float content_height = lycon->block.advance_y;
    float given_height = caption->blk && caption->block_mut()->given_height >= 0
        ? caption->block()->given_height : -1.0f;
    if (given_height >= 0.0f) {
        caption->height = given_height + given_height_padding;
    } else {
        caption->height = content_height + layout_axis_decoration_end(
            caption->bound ? caption->boundary() : nullptr, LAYOUT_AXIS_Y);
    }
    if (caption->blk) {
        caption->height = layout_apply_min_max_axis(caption, caption->height, false, true);
    }
    return caption->height;
}

float relayout_table_caption(LayoutContext* lycon, ViewBlock* cap, float table_width) {
    (void)table_width;
    DomElement* dom_elem = lam::dom_require_element(cap);
    // reset child text views before re-layout
    if (dom_elem) {
        for (DomNode* child = dom_elem->first_child; child; child = child->next_sibling) {
            if (child->is_text()) {
                child->view_type = RDT_VIEW_NONE;
                ViewText* text_view = lam::unsafe_view_text_storage(lam::dom_require<DOM_NODE_TEXT>(child));
                text_view->rect = nullptr;
                text_view->width = 0;
                text_view->height = 0;
            }
        }
    }

    LayoutContextScope lscope(lycon);
    View* saved_view = lycon->view;

    float content_width = cap->width;
    BoxMetrics cap_box = layout_box_metrics(cap);
    content_width -= cap_box.pad_border_h;
    content_width = max(content_width, 0.0f);

    lycon->view = static_cast<View*>(cap);
    dom_node_resolve_style(static_cast<DomNode*>(cap), lycon);

    layout_table_caption_content_setup(lycon, cap, content_width);

    if (dom_elem) {
        layout_flow_children(lycon, dom_elem->first_child, true);
    }

    layout_table_caption_content_finish(lycon, cap, cap_box.pad_border_v);

    log_debug("Caption re-layout complete: width=%.1f, height=%.1f", cap->width, cap->height);

    lycon->view = saved_view;

    float margin_v = 0;
    if (cap->bound) {
        float mt = cap->boundary()->margin.top > 0 ? cap->boundary()->margin.top : 0;
        float mb = cap->boundary()->margin.bottom > 0 ? cap->boundary()->margin.bottom : 0;
        margin_v = mt + mb;
    }
    return cap->height + margin_v;
}

float adjust_table_caption_width(ViewBlock* cap, float wrapper_content_width) {
    if (cap->blk && cap->block_mut()->given_width > 0) {
        cap->width = layout_css_size_to_border_box(
            cap->bound, layout_box_sizing(cap), cap->block()->given_width, true);
    } else {
        // caption is a block child of the table wrapper.
        float cap_margin_h = 0;
        if (cap->bound) {
            if (cap->boundary_mut()->margin.left_type != CSS_VALUE_AUTO && cap->boundary_mut()->margin.left > 0)
                cap_margin_h += cap->boundary()->margin.left;
            if (cap->boundary_mut()->margin.right_type != CSS_VALUE_AUTO && cap->boundary_mut()->margin.right > 0)
                cap_margin_h += cap->boundary()->margin.right;
        }
        cap->width = wrapper_content_width - cap_margin_h;
    }
    if (cap->blk) {
        cap->width = layout_apply_min_max_axis(cap, cap->width, true, true);
    }
    return cap->width;
}
