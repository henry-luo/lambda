#include "layout.hpp"

#include "../lib/tagged.hpp"
#include <cmath>

bool layout_view_is_abs_or_fixed(ViewBlock* block) {
    return block && block->position &&
        (block->positionp()->position == CSS_VALUE_ABSOLUTE ||
         block->positionp()->position == CSS_VALUE_FIXED);
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

    cb.has_definite_width = block->blk && block->block_mut()->given_width >= 0.0f;
    cb.has_definite_height = block->blk && block->block_mut()->given_height >= 0.0f;
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

static void layout_resolve_percent_size_axis(LayoutContext* lycon, ViewBlock* child,
                                             LayoutAxis axis, float base, bool definite) {
    LayoutAxisRefs refs(child->block_mut(), axis);
    float percent = *refs.given_percent;
    if (isnan(percent) || !definite) return;

    float value = base * percent / 100.0f;
    float& child_given = *refs.given;
    LayoutAxisRefs context_refs(&lycon->block, axis);
    if (context_refs.given) *context_refs.given = value;
    child_given = value;
}

static void layout_resolve_percent_offset_axis(PositionProp* position, LayoutAxis axis,
                                               bool start, float base) {
    LayoutAxisRefs refs(position, axis);
    RadiantInsetSide inset = start ? refs.insets.start : refs.insets.end;
    if (!*inset.has || isnan(*inset.percent)) return;

    *inset.value = *inset.percent * base / 100.0f;
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
    LayoutContainingBlock cb, bool use_content_box) {
    if (!lycon || !child || !child->blk) return;

    float width_base = use_content_box ? cb.content_width : cb.padding_width;
    float height_base = use_content_box ? cb.content_height : cb.padding_height;
    const float bases[2] = {width_base, height_base};
    const bool definite[2] = {cb.has_definite_width, cb.has_definite_height};
    const LayoutAxis axes[2] = {LAYOUT_AXIS_X, LAYOUT_AXIS_Y};
    for (int i = 0; i < 2; i++) {
        layout_resolve_percent_size_axis(
            lycon, child, axes[i], bases[i], definite[i]);
    }
}

void layout_resolve_percent_offsets_for_child(ViewBlock* child,
    LayoutContainingBlock cb) {
    if (!child || !child->position) return;

    PositionProp* pos = child->position;

    const float bases[2] = {cb.padding_width, cb.padding_height};
    const LayoutAxis axes[2] = {LAYOUT_AXIS_X, LAYOUT_AXIS_Y};
    for (int i = 0; i < 2; i++) {
        LayoutAxis axis = axes[i];
        layout_resolve_percent_offset_axis(pos, axis, true, bases[i]);
        layout_resolve_percent_offset_axis(pos, axis, false, bases[i]);
    }
}
