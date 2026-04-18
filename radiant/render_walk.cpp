#include "render_backend.h"
#include "view.hpp"
#include "webview.h"
#include "../lambda/input/css/dom_element.hpp"
extern "C" {
#include "../lib/log.h"
}

// ============================================================================
// Shared render-tree walker
//
// Traverses the View tree in document order, dispatching drawing operations
// through a RenderBackend vtable. Used by SVG and PDF output backends to
// avoid duplicating the walk/dispatch/context-propagation logic.
// ============================================================================

void render_walk_block(RenderBackend* backend, RenderWalkState* state, ViewBlock* block) {
    if (!block) return;

    // save parent state
    float pa_x = state->x;
    float pa_y = state->y;
    FontBox pa_font = state->font;
    Color pa_color = state->color;

    // update font
    if (block->font) {
        setup_font(state->ui_context, &state->font, block->font);
        if (backend->on_font_change) {
            backend->on_font_change(backend->ctx, block->font);
        }
    }

    // render boundary (background, borders, shadow, outline)
    if (block->bound && backend->render_bound) {
        backend->render_bound(backend->ctx, block, pa_x + block->x, pa_y + block->y);
    }

    // propagate position
    state->x = pa_x + block->x;
    state->y = pa_y + block->y;

    // update inherited color
    if (block->in_line && block->in_line->color.c) {
        state->color = block->in_line->color;
    }

    // inline SVG passthrough (HTM_TAG_SVG)
    if (block->tag_id == HTM_TAG_SVG) {
        if (backend->render_inline_svg) {
            backend->render_inline_svg(backend->ctx, block, state->x, state->y);
        }
        // restore and return — SVG children are not HTML views
        state->x = pa_x; state->y = pa_y;
        state->font = pa_font; state->color = pa_color;
        return;
    }

    // render embedded image
    if (block->embed && block->embed->img && backend->render_image) {
        backend->render_image(backend->ctx, block, state->x, state->y);
    }

    // render webview layer mode snapshot (composite as image)
    if (block->embed && block->embed->webview &&
        block->embed->webview->mode == WEBVIEW_MODE_LAYER &&
        block->embed->webview->surface && block->embed->webview->surface->pixels &&
        backend->render_image) {
        backend->render_image(backend->ctx, block, state->x, state->y);
    }

    // render children
    if (block->first_child) {
        float elem_opacity = (block->in_line && block->in_line->opacity > 0)
                              ? block->in_line->opacity : 1.0f;
        bool has_opacity = elem_opacity < 0.9995f;

        bool has_transform = block->transform && block->transform->functions;

        if (has_opacity && backend->begin_opacity) {
            backend->begin_opacity(backend->ctx, elem_opacity);
        }
        if (has_transform && backend->begin_transform) {
            backend->begin_transform(backend->ctx, block, state->x, state->y);
        }

        if (backend->begin_block_children) {
            backend->begin_block_children(backend->ctx, block);
        }

        render_walk_children(backend, state, block->first_child);

        if (backend->end_block_children) {
            backend->end_block_children(backend->ctx, block);
        }

        if (has_transform && backend->end_transform) {
            backend->end_transform(backend->ctx);
        }
        if (has_opacity && backend->end_opacity) {
            backend->end_opacity(backend->ctx);
        }
    }

    // column rules
    if (block->multicol && block->multicol->computed_column_count > 1) {
        if (backend->render_column_rules) {
            backend->render_column_rules(backend->ctx, block, state->x, state->y);
        }
    }

    // restore
    state->x = pa_x; state->y = pa_y;
    state->font = pa_font; state->color = pa_color;
}

void render_walk_inline(RenderBackend* backend, RenderWalkState* state, ViewSpan* span) {
    if (!span) return;

    FontBox pa_font = state->font;
    Color pa_color = state->color;

    if (span->font) {
        setup_font(state->ui_context, &state->font, span->font);
        if (backend->on_font_change) {
            backend->on_font_change(backend->ctx, span->font);
        }
    }

    if (span->in_line && span->in_line->color.c) {
        state->color = span->in_line->color;
    }

    if (span->first_child) {
        float elem_opacity = (span->in_line && span->in_line->opacity > 0)
                              ? span->in_line->opacity : 1.0f;
        bool has_opacity = elem_opacity < 0.9995f;

        if (has_opacity && backend->begin_opacity) {
            backend->begin_opacity(backend->ctx, elem_opacity);
        }

        if (backend->begin_inline_children) {
            backend->begin_inline_children(backend->ctx, span);
        }

        render_walk_children(backend, state, span->first_child);

        if (backend->end_inline_children) {
            backend->end_inline_children(backend->ctx, span);
        }

        if (has_opacity && backend->end_opacity) {
            backend->end_opacity(backend->ctx);
        }
    }

    state->font = pa_font;
    state->color = pa_color;
}

void render_walk_children(RenderBackend* backend, RenderWalkState* state, View* view) {
    while (view) {
        switch (view->view_type) {
            case RDT_VIEW_BLOCK:
            case RDT_VIEW_INLINE_BLOCK:
            case RDT_VIEW_TABLE:
            case RDT_VIEW_TABLE_ROW_GROUP:
            case RDT_VIEW_TABLE_ROW:
            case RDT_VIEW_TABLE_CELL:
            case RDT_VIEW_LIST_ITEM:
                render_walk_block(backend, state, (ViewBlock*)view);
                break;

            case RDT_VIEW_INLINE:
                render_walk_inline(backend, state, (ViewSpan*)view);
                break;

            case RDT_VIEW_TEXT:
                if (backend->render_text) {
                    backend->render_text(backend->ctx, (ViewText*)view,
                                         state->x, state->y,
                                         &state->font, state->color);
                }
                break;

            case RDT_VIEW_MARKER:
                if (backend->render_marker) {
                    backend->render_marker(backend->ctx, (ViewSpan*)view,
                                           state->x, state->y,
                                           &state->font, state->color);
                }
                break;

            default:
                break;
        }
        view = view->next();
    }
}
