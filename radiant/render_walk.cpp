#include "render_backend.h"
#include "render_paint_block.hpp"
#include "view.hpp"
#include "webview.h"
#include "../lib/tagged.hpp"
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

typedef struct RenderWalkBlockPhase {
    float pa_x;
    float pa_y;
    FontBox pa_font;
    Color pa_color;
    bool opened_transform;
    bool opened_opacity;
    bool stop_after_self;
} RenderWalkBlockPhase;

typedef struct RenderWalkBlockDriver {
    RenderBackend* backend;
    RenderWalkState* state;
    RenderWalkBlockPhase phase;
} RenderWalkBlockDriver;

static bool render_walk_block_begin(void* ctx, ViewBlock* block, void** phase) {
    RenderWalkBlockDriver* driver = (RenderWalkBlockDriver*)ctx;
    if (!driver || !driver->backend || !driver->state || !block || !phase) return false;

    RenderBackend* backend = driver->backend;
    RenderWalkState* state = driver->state;
    RenderWalkBlockPhase* p = &driver->phase;
    *p = {};
    p->pa_x = state->x;
    p->pa_y = state->y;
    p->pa_font = state->font;
    p->pa_color = state->color;

    if (block->font) {
        setup_font(state->ui_context, &state->font, block->font);
        if (backend->on_font_change) {
            backend->on_font_change(backend->ctx, block->font);
        }
    }

    state->x = p->pa_x + block->x;
    state->y = p->pa_y + block->y;

    bool has_transform = block->transform && block->transform->functions;
    p->opened_transform = has_transform && backend->begin_transform && backend->end_transform;
    if (p->opened_transform) {
        backend->begin_transform(backend->ctx, block, state->x, state->y);
    }

    float elem_opacity = (block->in_line && block->in_line->opacity > 0)
                          ? block->in_line->opacity : 1.0f;
    p->opened_opacity = elem_opacity < 0.9995f &&
                        backend->begin_opacity && backend->end_opacity;
    if (p->opened_opacity) {
        backend->begin_opacity(backend->ctx, elem_opacity);
    }

    *phase = p;
    return true;
}

static bool render_walk_block_paint_self(void* ctx, ViewBlock* block, void* phase) {
    RenderWalkBlockDriver* driver = (RenderWalkBlockDriver*)ctx;
    RenderWalkBlockPhase* p = (RenderWalkBlockPhase*)phase;
    if (!driver || !driver->backend || !driver->state || !block || !p) return false;

    RenderBackend* backend = driver->backend;
    RenderWalkState* state = driver->state;

    if (block->bound && backend->render_bound) {
        backend->render_bound(backend->ctx, block, state->x, state->y);
    }

    if (block->in_line && block->in_line->has_color) {
        state->color = block->in_line->color;
    }

    if (block->tag_id == HTM_TAG_SVG) {
        if (backend->render_inline_svg) {
            backend->render_inline_svg(backend->ctx, block, state->x, state->y,
                                       &state->font, state->color);
        }
        p->stop_after_self = true;
        return false;
    }

    if (block->embed && block->embed->img && backend->render_image) {
        backend->render_image(backend->ctx, block, state->x, state->y);
    }

    if (block->embed && block->embed->webview &&
        block->embed->webview->mode == WEBVIEW_MODE_LAYER &&
        block->embed->webview->surface && block->embed->webview->surface->pixels &&
        backend->render_image) {
        backend->render_image(backend->ctx, block, state->x, state->y);
    }

    return true;
}

static double render_walk_block_paint_children(void* ctx, ViewBlock* block, void* phase) {
    (void)phase;
    RenderWalkBlockDriver* driver = (RenderWalkBlockDriver*)ctx;
    if (!driver || !driver->backend || !driver->state || !block) return 0.0;

    RenderBackend* backend = driver->backend;
    RenderWalkState* state = driver->state;
    if (block->first_child) {
        if (backend->begin_block_children) {
            backend->begin_block_children(backend->ctx, block);
        }

        render_walk_children(backend, state, block->first_child);

        if (backend->end_block_children) {
            backend->end_block_children(backend->ctx, block);
        }
    }
    return 0.0;
}

static void render_walk_block_finish(void* ctx, ViewBlock* block, void* phase) {
    RenderWalkBlockDriver* driver = (RenderWalkBlockDriver*)ctx;
    RenderWalkBlockPhase* p = (RenderWalkBlockPhase*)phase;
    if (!driver || !driver->backend || !driver->state || !block || !p) return;

    RenderBackend* backend = driver->backend;
    RenderWalkState* state = driver->state;

    if (!p->stop_after_self &&
        block->multicol && block->multicol->computed_column_count > 1) {
        if (backend->render_column_rules) {
            backend->render_column_rules(backend->ctx, block, state->x, state->y);
        }
    }

    if (p->opened_opacity) {
        backend->end_opacity(backend->ctx);
    }

    if (p->opened_transform) {
        backend->end_transform(backend->ctx);
    }

    state->x = p->pa_x;
    state->y = p->pa_y;
    state->font = p->pa_font;
    state->color = p->pa_color;
}

void render_walk_block(RenderBackend* backend, RenderWalkState* state, ViewBlock* block) {
    if (!backend || !state || !block) return;

    RenderWalkBlockDriver driver = {};
    driver.backend = backend;
    driver.state = state;

    RenderPaintBlockOps ops = {};
    ops.ctx = &driver;
    ops.begin = render_walk_block_begin;
    ops.paint_self = render_walk_block_paint_self;
    ops.paint_children = render_walk_block_paint_children;
    ops.finish = render_walk_block_finish;
    render_paint_block_run(&ops, block);
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

    if (span->in_line && span->in_line->has_color) {
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
                if (backend->render_block) {
                    backend->render_block(backend->ctx, lam::view_require_block(view),
                                          state->x, state->y, &state->font, state->color);
                } else {
                    render_walk_block(backend, state, lam::view_require_block(view));
                }
                break;

            case RDT_VIEW_INLINE:
                if (backend->render_inline) {
                    backend->render_inline(backend->ctx, lam::view_require_element(view),
                                           state->x, state->y, &state->font, state->color);
                } else {
                    render_walk_inline(backend, state, lam::view_require_element(view));
                }
                break;

            case RDT_VIEW_TEXT:
                if (backend->render_text) {
                    backend->render_text(backend->ctx, lam::view_require_text(view),
                                         state->x, state->y,
                                         &state->font, state->color);
                }
                break;

            case RDT_VIEW_MARKER:
                if (backend->render_marker) {
                    backend->render_marker(backend->ctx, lam::view_require_element(view),
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

void render_walk_positioned_children(RenderBackend* backend, RenderWalkState* state, ViewBlock* block) {
    if (!backend || !state || !block || !block->position) return;

    ViewBlock* abs_children[256];
    int abs_count = 0;
    ViewBlock* child_block = block->position->first_abs_child;
    while (child_block && abs_count < 256) {
        abs_children[abs_count++] = child_block;
        child_block = child_block->position->next_abs_sibling;
    }

    // stable insertion sort by z-index, matching the legacy raster order.
    for (int i = 1; i < abs_count; i++) {
        ViewBlock* key = abs_children[i];
        int key_z = key->position ? key->position->z_index : 0;
        int j = i - 1;
        while (j >= 0) {
            int j_z = abs_children[j]->position ? abs_children[j]->position->z_index : 0;
            if (j_z > key_z) {
                abs_children[j + 1] = abs_children[j];
                j--;
            } else {
                break;
            }
        }
        abs_children[j + 1] = key;
    }

    for (int i = 0; i < abs_count; i++) {
        if (backend->render_block) {
            backend->render_block(backend->ctx, abs_children[i],
                                  state->x, state->y, &state->font, state->color);
        } else {
            render_walk_block(backend, state, abs_children[i]);
        }
    }
}
