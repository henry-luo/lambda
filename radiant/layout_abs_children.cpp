#include "layout.hpp"

#include "../lambda/input/css/css_style_node.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include <cmath>

void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block,
    BlockContext *pa_block, Linebox *pa_line);

static float layout_abs_child_aspect_ratio(ViewBlock* block) {
    if (!block || !block->specified_style) return 0.0f;

    CssDeclaration* ar_decl = style_tree_get_declaration(
        block->specified_style, CSS_PROPERTY_ASPECT_RATIO);
    if (!ar_decl || !ar_decl->value) return 0.0f;

    return layout_aspect_ratio_value(ar_decl->value);
}

static void layout_apply_abs_child_aspect_ratio(AbsChildLayoutState* state) {
    if (!state || !state->child_block) return;

    ViewBlock* child = state->child_block;
    float aspect_ratio = layout_abs_child_aspect_ratio(child);
    if (aspect_ratio <= 0.0f) return;

    bool has_explicit_height = state->original_given_height > 0.0f;
    bool has_explicit_width = state->original_given_width > 0.0f;
    if (child->width > 0.0f && !has_explicit_height) {
        child->height = child->width / aspect_ratio;
        log_debug("[LAYOUT_ABS] aspect-ratio height: width=%.1f ratio=%.3f height=%.1f",
                  child->width, aspect_ratio, child->height);
    } else if (child->height > 0.0f && !has_explicit_width && child->width <= 0.0f) {
        child->width = child->height * aspect_ratio;
        log_debug("[LAYOUT_ABS] aspect-ratio width: height=%.1f ratio=%.3f width=%.1f",
                  child->height, aspect_ratio, child->width);
    }
}

void layout_absolute_children_in_context(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx) {
    if (!lycon || !container || !ctx) return;

    log_enter();
    log_debug("[LAYOUT_ABS] children start: container=%s context=%s",
              container->node_name(), ctx->log_context ? ctx->log_context : "abs");

    DomNode* child = container->first_child;
    while (child) {
        if (!child->is_element()) {
            child = child->next_sibling;
            continue;
        }

        ViewBlock* child_block = lam::view_as_block(child->as_element());
        if (!layout_view_is_abs_or_fixed(child_block)) {
            child = child->next_sibling;
            continue;
        }

        AbsChildLayoutState state = {};
        state.child = child;
        state.child_block = child_block;
        state.containing_block = ctx->containing_block.view
            ? ctx->containing_block
            : layout_containing_block_for_view(container);
        state.parent_block = lycon->block;
        state.parent_line = lycon->line;
        LayoutContextScope child_scope(lycon);

        if (child_block->blk) {
            lycon->block.given_width = child_block->block()->given_width;
            lycon->block.given_height = child_block->block()->given_height;
            if (ctx->resolve_percent_against_content_box) {
                layout_resolve_percent_size_for_child(lycon, child_block,
                    state.containing_block, true, ctx->log_context);
            }
        } else {
            lycon->block.given_width = -1.0f;
            lycon->block.given_height = -1.0f;
        }

        state.original_given_width = lycon->block.given_width;
        state.original_given_height = lycon->block.given_height;

        if (ctx->prepare_child) {
            ctx->prepare_child(lycon, container, ctx, &state);
        }

        layout_abs_block(lycon, child, child_block, &state.parent_block, &state.parent_line);

        if (ctx->after_child) {
            ctx->after_child(lycon, container, ctx, &state);
        }

        layout_apply_abs_child_aspect_ratio(&state);

        log_debug("[LAYOUT_ABS] child laid out: %s at (%.1f, %.1f) size %.1fx%.1f",
                  child->node_name(), child_block->x, child_block->y,
                  child_block->width, child_block->height);

        child = child->next_sibling;
    }

    log_debug("[LAYOUT_ABS] children complete: container=%s", container->node_name());
    log_leave();
}
