#include "layout_containing_block.hpp"
#include "layout.hpp"

#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include <cmath>

static float clamp_non_negative(float value) {
    return value > 0.0f ? value : 0.0f;
}

bool layout_view_is_abs_or_fixed(ViewBlock* block) {
    return block && block->position &&
        (block->position->position == CSS_VALUE_ABSOLUTE ||
         block->position->position == CSS_VALUE_FIXED);
}

ViewBlock* layout_nearest_block_ancestor(ViewElement* view) {
    ViewElement* current = view;
    while (current && !current->is_block()) {
        current = current->parent_view();
    }
    return (current && current->is_block()) ? lam::view_require_block(current) : nullptr;
}

LayoutContainingBlock layout_containing_block_for_view(ViewBlock* block) {
    LayoutContainingBlock cb = {};
    cb.view = block;
    if (!block) return cb;

    BoxMetrics box = layout_box_metrics(block);

    cb.border_x = 0.0f;
    cb.border_y = 0.0f;
    cb.border_width = clamp_non_negative(block->width);
    cb.border_height = clamp_non_negative(block->height);

    cb.padding_x = box.border.left;
    cb.padding_y = box.border.top;
    cb.padding_width = clamp_non_negative(cb.border_width - box.border_h);
    cb.padding_height = clamp_non_negative(cb.border_height - box.border_v);

    cb.content_x = box.border.left + box.padding.left;
    cb.content_y = box.border.top + box.padding.top;
    cb.content_width = clamp_non_negative(cb.border_width - box.pad_border_h);
    cb.content_height = clamp_non_negative(cb.border_height - box.pad_border_v);

    cb.has_definite_width = block->blk && block->blk->given_width >= 0.0f;
    cb.has_definite_height = block->blk && block->blk->given_height >= 0.0f;
    return cb;
}

LayoutContainingBlock layout_initial_containing_block(LayoutContext* lycon) {
    LayoutContainingBlock cb = {};
    if (!lycon || !lycon->ui_context) return cb;

    cb.border_width = lycon->ui_context->viewport_width > 0
        ? lycon->ui_context->viewport_width * lycon->ui_context->pixel_ratio
        : 0.0f;
    cb.border_height = lycon->ui_context->viewport_height > 0
        ? lycon->ui_context->viewport_height * lycon->ui_context->pixel_ratio
        : 0.0f;

    cb.padding_width = cb.border_width;
    cb.padding_height = cb.border_height;
    cb.content_width = cb.border_width;
    cb.content_height = cb.border_height;
    cb.has_definite_width = cb.border_width > 0.0f;
    cb.has_definite_height = cb.border_height > 0.0f;
    return cb;
}

LayoutContainingBlock layout_absolute_containing_block(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return layout_initial_containing_block(lycon);

    bool is_icb = (block->parent_view() == nullptr);
    if (is_icb && lycon && lycon->ui_context) {
        return layout_initial_containing_block(lycon);
    }

    LayoutContainingBlock cb = layout_containing_block_for_view(block);
    if (is_icb) {
        cb.padding_x = 0.0f;
        cb.padding_y = 0.0f;
        cb.padding_width = cb.border_width;
        cb.padding_height = cb.border_height;
    }
    return cb;
}

void layout_resolve_percent_size_for_child(LayoutContext* lycon, ViewBlock* child,
    LayoutContainingBlock cb, bool use_content_box, const char* log_context) {
    if (!lycon || !child || !child->blk) return;

    float width_base = use_content_box ? cb.content_width : cb.padding_width;
    float height_base = use_content_box ? cb.content_height : cb.padding_height;
    const char* context = log_context ? log_context : "child";

    if (!isnan(child->blk->given_width_percent) && width_base > 0.0f) {
        float width = width_base * child->blk->given_width_percent / 100.0f;
        log_debug("[LAYOUT_CB] %s width %.1f%% of %.1f = %.1f (was %.1f)",
                  context, child->blk->given_width_percent, width_base, width, lycon->block.given_width);
        lycon->block.given_width = width;
        child->blk->given_width = width;
    }
    if (!isnan(child->blk->given_height_percent) && height_base > 0.0f) {
        float height = height_base * child->blk->given_height_percent / 100.0f;
        log_debug("[LAYOUT_CB] %s height %.1f%% of %.1f = %.1f (was %.1f)",
                  context, child->blk->given_height_percent, height_base, height, lycon->block.given_height);
        lycon->block.given_height = height;
        child->blk->given_height = height;
    }
}

void layout_resolve_percent_offsets_for_child(ViewBlock* child,
    LayoutContainingBlock cb, const char* log_context) {
    if (!child || !child->position) return;

    PositionProp* pos = child->position;
    const char* context = log_context ? log_context : "positioned child";

    if (pos->has_left && !isnan(pos->left_percent)) {
        pos->left = pos->left_percent * cb.padding_width / 100.0f;
        log_debug("[LAYOUT_CB] %s left %.1f%% of %.1f = %.1f",
                  context, pos->left_percent, cb.padding_width, pos->left);
    }
    if (pos->has_right && !isnan(pos->right_percent)) {
        pos->right = pos->right_percent * cb.padding_width / 100.0f;
        log_debug("[LAYOUT_CB] %s right %.1f%% of %.1f = %.1f",
                  context, pos->right_percent, cb.padding_width, pos->right);
    }
    if (pos->has_top && !isnan(pos->top_percent)) {
        pos->top = pos->top_percent * cb.padding_height / 100.0f;
        log_debug("[LAYOUT_CB] %s top %.1f%% of %.1f = %.1f",
                  context, pos->top_percent, cb.padding_height, pos->top);
    }
    if (pos->has_bottom && !isnan(pos->bottom_percent)) {
        pos->bottom = pos->bottom_percent * cb.padding_height / 100.0f;
        log_debug("[LAYOUT_CB] %s bottom %.1f%% of %.1f = %.1f",
                  context, pos->bottom_percent, cb.padding_height, pos->bottom);
    }
}
