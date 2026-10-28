#include "layout.hpp"

#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_element.hpp"

void layout_table_caption_content_setup(LayoutContext* lycon, ViewBlock* caption,
                                        float content_width, BlockContext* parent,
                                        float used_inline_size) {
    // caption BFCs own their containing sizes; an infinite stand-in must not stretch descendants.
    block_context_init(&lycon->block, caption, lycon->pool);
    lycon->block.parent = lam::up(parent);
    lycon->block.given_width = content_width;
    lycon->block.content_width = content_width;
    caption->width = layout_border_size_from_content_box(caption, content_width, true);
    float given_height = caption->blk ? caption->block()->given_height : -1.0f;
    lycon->block.given_height = given_height;
    if (given_height >= 0.0f) {
        lycon->block.content_height = layout_content_size_if_border_box(caption, given_height, false);
        caption->height = layout_css_size_to_border_box(
            caption->bound, layout_box_sizing(caption), given_height, false);
    }
    if (used_inline_size >= 0.0f && layout_block_inline_axis_is_vertical(caption)) {
        // the wrapper supplies a used inline measure without changing the caption's authored auto size.
        caption->height = used_inline_size;
        lycon->block.content_height = layout_content_size_from_border_box(caption, used_inline_size, false);
        lycon->block.given_height = lycon->block.content_height;
        lycon->block.used_size_owner = lam::up(caption);
    }
    if (caption->blk) {
        caption->block_mut()->vertical_geometry_published = false;
    }
    lycon->elmt = lam::up(static_cast<DomNode*>(caption));
    lycon->view = lam::up(static_cast<View*>(caption));
    setup_inline(lycon, caption);
}

float layout_table_caption_content_finish(LayoutContext* lycon, ViewBlock* caption) {
    float content_height = lycon->block.advance_y;
    bool wrapper_inline_measure = lycon->block.used_size_owner.get() == caption;
    bool own_vertical_flow = layout_block_inline_axis_is_vertical(caption) &&
        (wrapper_inline_measure || layout_inline_box_is_orthogonal_to_parent(caption));
    if (wrapper_inline_measure) {
        BoxMetrics box = layout_box_metrics(caption);
        float block_extent = max(lycon->block.advance_y - box.border.top - box.padding.top, 0.0f) + box.pad_border_h;
        if (layout_vertical_parent_has_block_flow_child(caption))
            block_extent = layout_compute_in_flow_child_width_extent(caption);
        caption->width = caption->blk && caption->block()->given_width >= 0.0f
            ? layout_css_size_to_border_box(caption->bound, layout_box_sizing(caption), caption->block()->given_width, true)
            : layout_apply_min_max_axis(caption, block_extent, true, true);
    }
    if (own_vertical_flow) {
        content_height = layout_vertical_content_inline_extent(lycon, caption) +
            layout_axis_decoration_start(caption->bound, LAYOUT_AXIS_Y);
    }
    float given_height = caption->blk && caption->block_mut()->given_height >= 0
        ? caption->block()->given_height : -1.0f;
    if (given_height >= 0.0f) {
        caption->height = layout_css_size_to_border_box(
            caption->bound, layout_box_sizing(caption), given_height, false);
    } else {
        caption->height = content_height + layout_axis_decoration_end(
            caption->bound ? caption->boundary() : nullptr, LAYOUT_AXIS_Y);
    }
    if (caption->blk) {
        caption->height = layout_apply_min_max_axis(caption, caption->height, false, true);
    }
    if (own_vertical_flow) layout_publish_vertical_flow_contents(lycon, caption);
    return caption->height;
}

float relayout_table_caption(LayoutContext* lycon, ViewBlock* cap, float table_width,
                              bool vertical_inline_axis) {
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
            } else if (child->is_element()) view_pool_reset_retained_subtree(lycon->doc->view_tree, child->as_element());
        }
    }

    LayoutContextScope lscope(lycon);
    LayoutViewScope view_scope(lycon);
    // width refinement reuses the generated text resolved during the source-order pass.
    CounterContext* saved_counters = lycon->counter_context;
    lycon->counter_context = nullptr;

    float content_width = cap->width;
    BoxMetrics cap_box = layout_box_metrics(cap);
    content_width -= cap_box.pad_border_h;
    content_width = max(content_width, 0.0f);

    lycon->view = lam::up(static_cast<View*>(cap));
    dom_node_resolve_style(static_cast<DomNode*>(cap), lycon);

    layout_table_caption_content_setup(lycon, cap, content_width, &lscope.saved_block,
        vertical_inline_axis ? cap->height : -1.0f);

    if (dom_elem) {
        layout_flow_children(lycon, dom_elem->first_child, true);
    }

    layout_table_caption_content_finish(lycon, cap);
    lycon->counter_context = lam::up(saved_counters);

    log_debug("Caption re-layout complete: width=%.1f, height=%.1f", cap->width, cap->height);

    float margin_v = 0;
    if (cap->bound) {
        float mt = cap->boundary()->margin.top > 0 ? cap->boundary()->margin.top : 0;
        float mb = cap->boundary()->margin.bottom > 0 ? cap->boundary()->margin.bottom : 0;
        margin_v = mt + mb;
    }
    return cap->height + margin_v;
}

float adjust_table_caption_width(ViewBlock* cap, float wrapper_content_width,
                                   bool vertical_inline_axis) {
    if (vertical_inline_axis) {
        LayoutAxisRefs axis(cap, LAYOUT_AXIS_Y);
        float margin_v = axis.non_auto_margin_start() + axis.non_auto_margin_end();
        float height = max(wrapper_content_width - margin_v, 0.0f);
        if (cap->blk && cap->block()->given_height >= 0.0f) {
            height = layout_css_size_to_border_box(cap->bound, layout_box_sizing(cap), cap->block()->given_height, false);
        }
        cap->height = layout_apply_min_max_axis(cap, height, false, true);
        return cap->height;
    }
    // the grid has settled since the source-order pass resolved caption percentages against its parent.
    if (cap->blk && !isnan(cap->block()->given_width_percent))
        cap->block_mut()->given_width = wrapper_content_width * cap->block()->given_width_percent * 0.01f;
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
