#include "layout_containing_block.hpp"
#include "layout.hpp"

#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include <cmath>

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
    cb.border_width = max(block->width, 0.0f);
    cb.border_height = max(block->height, 0.0f);

    cb.padding_x = box.border.left;
    cb.padding_y = box.border.top;
    cb.padding_width = max(cb.border_width - box.border_h, 0.0f);
    cb.padding_height = max(cb.border_height - box.border_v, 0.0f);

    cb.content_x = box.border.left + box.padding.left;
    cb.content_y = box.border.top + box.padding.top;
    cb.content_width = max(cb.border_width - box.pad_border_h, 0.0f);
    cb.content_height = max(cb.border_height - box.pad_border_v, 0.0f);

    cb.has_definite_width = block->blk && block->blk->given_width >= 0.0f;
    cb.has_definite_height = block->blk && block->blk->given_height >= 0.0f;
    return cb;
}

LayoutContainingBlock layout_initial_containing_block(LayoutContext* lycon) {
    LayoutContainingBlock cb = {};
    if (!lycon) return cb;

    if (lycon->ui_context) {
        cb.border_width = lycon->ui_context->viewport_width > 0
            ? lycon->ui_context->viewport_width * lycon->ui_context->pixel_ratio
            : 0.0f;
        cb.border_height = lycon->ui_context->viewport_height > 0
            ? lycon->ui_context->viewport_height * lycon->ui_context->pixel_ratio
            : 0.0f;
    }
    if (cb.border_width <= 0.0f) cb.border_width = lycon->width;
    if (cb.border_height <= 0.0f) cb.border_height = lycon->height;

    cb.padding_width = cb.border_width;
    cb.padding_height = cb.border_height;
    cb.content_width = cb.border_width;
    cb.content_height = cb.border_height;
    cb.has_definite_width = cb.border_width > 0.0f;
    cb.has_definite_height = cb.border_height > 0.0f;
    return cb;
}

bool layout_is_initial_containing_block(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return false;
    if (lycon && lycon->doc && lycon->doc->view_tree &&
        lycon->doc->view_tree->root == (View*)block) {
        return true;
    }
    return block->parent_view() == nullptr;
}

LayoutContainingBlock layout_absolute_containing_block(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return layout_initial_containing_block(lycon);

    bool is_icb = layout_is_initial_containing_block(lycon, block);
    if (is_icb) {
        return layout_initial_containing_block(lycon);
    }

    LayoutContainingBlock cb = layout_containing_block_for_view(block);
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
