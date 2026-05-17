#pragma once

#include "layout.hpp"
#include "layout_containing_block.hpp"

typedef enum AbsStaticContextKind {
    ABS_STATIC_BLOCK,
    ABS_STATIC_FLEX,
    ABS_STATIC_GRID,
} AbsStaticContextKind;

struct AbsStaticContext;

typedef struct AbsChildLayoutState {
    DomNode* child;
    ViewBlock* child_block;
    LayoutContainingBlock containing_block;
    BlockContext parent_block;
    Linebox parent_line;
    float original_given_width;
    float original_given_height;
    bool has_grid_area;
    float grid_area_x;
    float grid_area_y;
    float grid_area_width;
    float grid_area_height;
} AbsChildLayoutState;

typedef void (*AbsPrepareChildFn)(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state);
typedef void (*AbsAfterChildFn)(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state);

typedef struct AbsStaticContext {
    AbsStaticContextKind kind;
    LayoutContainingBlock containing_block;
    FlexContainerLayout* flex;
    GridContainerLayout* grid;
    bool resolve_percent_against_content_box;
    const char* log_context;
    AbsPrepareChildFn prepare_child;
    AbsAfterChildFn after_child;
} AbsStaticContext;

void layout_absolute_children_in_context(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx);
