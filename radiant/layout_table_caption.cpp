#include "layout_table_caption.hpp"

#include "layout.hpp"
#include "layout_box.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_element.hpp"

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

    lycon->block.content_width = content_width;
    lycon->block.content_height = 10000;
    lycon->block.advance_y = 0;

    float inner_left = 0;
    if (cap->bound) {
        if (cap->bound->border) {
            inner_left += cap->bound->border->width.left;
            lycon->block.advance_y += cap->bound->border->width.top;
        }
        inner_left += cap->bound->padding.left;
        lycon->block.advance_y += cap->bound->padding.top;
    }
    lycon->line.left = inner_left;
    lycon->line.right = inner_left + content_width;

    if (cap->font) {
        setup_font(lycon->ui_context, &lycon->font, cap->font);
    }
    setup_line_height(lycon, cap);
    if (lycon->font.font_handle) {
        if (lycon->block.line_height_is_normal) {
            font_get_normal_lh_split(lycon->font.font_handle, &lycon->block.init_ascender, &lycon->block.init_descender);
        } else {
            TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
            if (typo.valid && typo.use_typo_metrics) {
                lycon->block.init_ascender = typo.ascender;
                lycon->block.init_descender = typo.descender;
            } else {
                const FontMetrics* fm = font_get_metrics(lycon->font.font_handle);
                if (fm) {
                    lycon->block.init_ascender = fm->hhea_ascender;
                    lycon->block.init_descender = -(fm->hhea_descender);
                }
            }
        }
    }
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height - (lycon->block.init_ascender + lycon->block.init_descender)) / 2);

    if (cap->blk) {
        if (!isnan(cap->blk->text_indent_percent)) {
            lycon->block.text_indent = content_width * cap->blk->text_indent_percent / 100.0f;
        } else {
            lycon->block.text_indent = cap->blk->text_indent;
        }
        if (lycon->block.text_indent != 0.0f) {
            lycon->block.is_first_line = true;
        }
    }

    line_reset(lycon);

    if (cap->blk && cap->blk->text_align) {
        lycon->block.text_align = cap->blk->text_align;
    }
    if (cap->blk && cap->blk->direction) {
        lycon->block.direction = cap->blk->direction;
    }

    if (dom_elem) {
        for (DomNode* child = dom_elem->first_child; child; child = child->next_sibling) {
            layout_flow_node(lycon, child);
        }
        if (!lycon->line.is_line_start) { line_break(lycon); }
    }

    float caption_content_height = lycon->block.advance_y;
    float caption_given_height = (cap->blk && cap->blk->given_height >= 0)
        ? cap->blk->given_height : -1;
    if (caption_given_height >= 0) {
        cap->height = caption_given_height;
        cap->height += cap_box.pad_border_v;
    } else {
        cap->height = caption_content_height;
        if (cap->bound) {
            cap->height += cap->bound->padding.bottom;
            if (cap->bound->border) {
                cap->height += cap->bound->border->width.bottom;
            }
        }
    }
    if (cap->blk) {
        bool is_border_box = layout_uses_border_box(cap);
        if (is_border_box) {
            cap->height = adjust_min_max_height(cap, cap->height);
        } else {
            BoxMetrics box = layout_box_metrics(cap);
            float content_h = cap->height - box.pad_border_v;
            float clamped_h = adjust_min_max_height(cap, content_h);
            cap->height = clamped_h + box.pad_border_v;
        }
    }

    log_debug("Caption re-layout complete: width=%.1f, height=%.1f", cap->width, cap->height);

    lycon->view = saved_view;

    float margin_v = 0;
    if (cap->bound) {
        float mt = cap->bound->margin.top > 0 ? cap->bound->margin.top : 0;
        float mb = cap->bound->margin.bottom > 0 ? cap->bound->margin.bottom : 0;
        margin_v = mt + mb;
    }
    return cap->height + margin_v;
}

float adjust_table_caption_width(ViewBlock* cap, float wrapper_content_width) {
    if (cap->blk && cap->blk->given_width > 0) {
        cap->width = layout_css_size_to_border_box(
            cap->bound, layout_box_sizing(cap), cap->blk->given_width, true);
    } else {
        // caption is a block child of the table wrapper.
        float cap_margin_h = 0;
        if (cap->bound) {
            if (cap->bound->margin.left_type != CSS_VALUE_AUTO && cap->bound->margin.left > 0)
                cap_margin_h += cap->bound->margin.left;
            if (cap->bound->margin.right_type != CSS_VALUE_AUTO && cap->bound->margin.right > 0)
                cap_margin_h += cap->bound->margin.right;
        }
        cap->width = wrapper_content_width - cap_margin_h;
    }
    if (cap->blk) {
        bool is_border_box = layout_uses_border_box(cap);
        if (is_border_box) {
            cap->width = adjust_min_max_width(cap, cap->width);
        } else {
            BoxMetrics box = layout_box_metrics(cap);
            float content_w = cap->width - box.pad_border_h;
            float clamped_w = adjust_min_max_width(cap, content_w);
            cap->width = clamped_w + box.pad_border_h;
        }
    }
    return cap->width;
}
