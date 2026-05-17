#pragma once

#include "view.hpp"
#include "layout_box.hpp"

struct LayoutContext;

typedef struct LayoutContainingBlock {
    ViewBlock* view;

    float border_x;
    float border_y;
    float border_width;
    float border_height;

    float padding_x;
    float padding_y;
    float padding_width;
    float padding_height;

    float content_x;
    float content_y;
    float content_width;
    float content_height;

    bool has_definite_width;
    bool has_definite_height;
} LayoutContainingBlock;

bool layout_view_is_abs_or_fixed(ViewBlock* block);
ViewBlock* layout_nearest_block_ancestor(ViewElement* view);

LayoutContainingBlock layout_containing_block_for_view(ViewBlock* block);
LayoutContainingBlock layout_initial_containing_block(LayoutContext* lycon);
LayoutContainingBlock layout_absolute_containing_block(LayoutContext* lycon, ViewBlock* block);

void layout_resolve_percent_size_for_child(LayoutContext* lycon, ViewBlock* child,
    LayoutContainingBlock cb, bool use_content_box, const char* log_context);
void layout_resolve_percent_offsets_for_child(ViewBlock* child,
    LayoutContainingBlock cb, const char* log_context);
