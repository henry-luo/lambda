#include "render.hpp"
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
    bool opened_effect_group;
    bool stop_after_self;
} RenderWalkBlockPhase;

typedef struct RenderWalkBlockDriver {
    RenderBackend* backend;
    RenderWalkState* state;
    RenderWalkBlockPhase phase;
} RenderWalkBlockDriver;

static float render_walk_css_opacity(const InlineProp* in_line) {
    if (!in_line) return 1.0f;
    float opacity = in_line->opacity;
    if (opacity < 0.0f) return 1.0f;
    if (opacity > 1.0f) return 1.0f;
    return opacity;
}

static bool render_walk_blend_mode_effective(CssEnum mode) {
    return mode && mode != CSS_VALUE_NORMAL;
}

static bool render_walk_filter_effective(const FilterProp* filter) {
    return filter && filter->functions;
}

static bool render_walk_block_effect_group(ViewBlock* block, float abs_x, float abs_y,
                                           PaintEffectGroup* group) {
    if (!block || !group) return false;
    *group = {};
    float opacity = render_walk_css_opacity(block->in_line);
    CssEnum blend = (block->in_line &&
                     render_walk_blend_mode_effective(block->in_line->mix_blend_mode))
                    ? block->in_line->mix_blend_mode : (CssEnum)0;
    bool has_filter = render_walk_filter_effective(block->filter);
    bool has_backdrop_filter = render_walk_filter_effective(block->backdrop_filter);
    bool has_shadow = block->bound && block->bound->box_shadow;
    if (opacity >= 0.9995f && !blend && !has_filter &&
        !has_backdrop_filter && !has_shadow) {
        return false;
    }

    float visual_overflow = render_geometry_block_visual_overflow(block);
    group->bounds.left = abs_x - visual_overflow;
    group->bounds.top = abs_y - visual_overflow;
    group->bounds.right = abs_x + block->width + visual_overflow;
    group->bounds.bottom = abs_y + block->height + visual_overflow;
    group->opacity = opacity;
    group->blend_mode = (int)blend; // INT_CAST_OK: CssEnum is serialized through PaintIR as an integer enum value.
    group->filter = has_filter ? block->filter : NULL;
    group->backdrop = has_backdrop_filter;
    group->backdrop_filter = has_backdrop_filter ? block->backdrop_filter : NULL;
    group->shadow = has_shadow;
    return true;
}

static bool render_walk_inline_effect_group(ViewSpan* span, PaintEffectGroup* group) {
    if (!span || !group) return false;
    *group = {};
    float opacity = render_walk_css_opacity(span->in_line);
    CssEnum blend = (span->in_line &&
                     render_walk_blend_mode_effective(span->in_line->mix_blend_mode))
                    ? span->in_line->mix_blend_mode : (CssEnum)0;
    bool has_filter = render_walk_filter_effective(span->filter);
    bool has_backdrop_filter = render_walk_filter_effective(span->backdrop_filter);
    if (opacity >= 0.9995f && !blend && !has_filter && !has_backdrop_filter) {
        return false;
    }

    group->opacity = opacity;
    group->blend_mode = (int)blend; // INT_CAST_OK: CssEnum is serialized through PaintIR as an integer enum value.
    group->filter = has_filter ? span->filter : NULL;
    group->backdrop = has_backdrop_filter;
    group->backdrop_filter = has_backdrop_filter ? span->backdrop_filter : NULL;
    return true;
}

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

    PaintEffectGroup group = {};
    bool has_effect_group = render_walk_block_effect_group(block, state->x, state->y, &group);
    p->opened_effect_group = has_effect_group &&
                             backend->begin_effect_group && backend->end_effect_group;
    if (p->opened_effect_group) {
        backend->begin_effect_group(backend->ctx, &group);
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

        if (block->position) {
            render_walk_positioned_children(backend, state, block);
        }
        render_walk_positive_z_descendants(backend, state, block->first_child);

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

    if (p->opened_effect_group) {
        backend->end_effect_group(backend->ctx);
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
        PaintEffectGroup group = {};
        bool has_effect_group = render_walk_inline_effect_group(span, &group);
        bool opened_effect_group = has_effect_group &&
                                   backend->begin_effect_group && backend->end_effect_group;

        if (opened_effect_group) {
            backend->begin_effect_group(backend->ctx, &group);
        }

        if (backend->begin_inline_children) {
            backend->begin_inline_children(backend->ctx, span);
        }

        render_walk_children(backend, state, span->first_child);

        if (backend->end_inline_children) {
            backend->end_inline_children(backend->ctx, span);
        }

        if (opened_effect_group) {
            backend->end_effect_group(backend->ctx);
        }
    }

    state->font = pa_font;
    state->color = pa_color;
}

static void render_walk_view(RenderBackend* backend, RenderWalkState* state, View* view) {
    if (!backend || !state || !view) return;

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
}

void render_walk_children(RenderBackend* backend, RenderWalkState* state, View* view) {
    while (view) {
        if (!radiant_stack_is_deferred_from_normal_flow(view)) {
            render_walk_view(backend, state, view);
        }
        view = view->next();
    }
}

void render_walk_positive_z_descendants(RenderBackend* backend, RenderWalkState* state, View* view) {
    if (!backend || !state || !view) return;

    ArrayList* positive_views = radiant_stack_collect_positive_z_descendants(
        view, "[RAD_CAP_RENDER_Z]");
    if (!positive_views) return;
    radiant_stack_sort_in_paint_order(positive_views);

    for (int i = 0; i < positive_views->length; i++) {
        render_walk_view(backend, state, (View*)positive_views->data[i]);
    }
    arraylist_free(positive_views);
}

void render_walk_positioned_children(RenderBackend* backend, RenderWalkState* state, ViewBlock* block) {
    if (!backend || !state || !block || !block->position) return;

    ArrayList* abs_children = radiant_stack_collect_positioned_children(
        block, "[RAD_CAP_RENDER_ABS]");
    if (!abs_children) return;
    radiant_stack_sort_in_paint_order(abs_children);

    for (int i = 0; i < abs_children->length; i++) {
        ViewBlock* abs_child = (ViewBlock*)abs_children->data[i];
        if (backend->render_block) {
            backend->render_block(backend->ctx, abs_child,
                                  state->x, state->y, &state->font, state->color);
        } else {
            render_walk_block(backend, state, abs_child);
        }
    }
    arraylist_free(abs_children);
}
