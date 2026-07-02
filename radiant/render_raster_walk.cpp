#include "render.hpp"
#include "render_profiler.hpp"
#include "render_text.hpp"
#include "render_backend.h"
#include "render_list.hpp"
#include "render_media.hpp"
#include "render_svg_inline.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lambda/input/css/dom_element.hpp"

#include <chrono>
#include <stdlib.h>

static bool render_trace_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        // Per-box render diagnostics scale with page size; make them opt-in so
        // online-page rendering is measured by rendering work, not log volume.
        enabled = getenv("RADIANT_TRACE_RENDER") ? 1 : 0;
    }
    return enabled != 0;
}

static void render_raster_dispatch_block(RenderContext* rdcon, ViewBlock* block,
                                         bool skip_positioned_in_normal_flow) {
    if (!rdcon || !block) return;
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_DISPATCH);

    if (render_block_viewport_misses(rdcon, block)) {
        return;
    }

    if (block->view_type == RDT_VIEW_LIST_ITEM) {
        render_litem_view(rdcon, block);
        return;
    }

    if (render_trace_enabled()) {
        log_debug("[RENDER DISPATCH] view_type=%d, embed=%p, img=%p, width=%.0f, height=%.0f",
                  block->view_type, block->embed,
                  block->embed ? block->embed->img : NULL, block->width, block->height);
    }
    if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form) {
        if (render_trace_enabled()) log_debug("[RENDER DISPATCH] calling render_block_view for form control");
        render_block_view(rdcon, block);
    }
    else if (block->tag_id == HTM_TAG_SVG) {
        if (block->bound) { render_bound(rdcon, block); }
        if (render_trace_enabled()) log_debug("[RENDER DISPATCH] calling render_inline_svg for inline SVG");
        auto ts1 = std::chrono::high_resolution_clock::now();
        render_inline_svg(rdcon, block);
        auto ts2 = std::chrono::high_resolution_clock::now();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_SVG,
            std::chrono::duration<double, std::milli>(ts2 - ts1).count());
    }
    else if (block->embed && block->embed->img) {
        if (render_trace_enabled()) log_debug("[RENDER DISPATCH] calling render_image_view");
        auto ti1 = std::chrono::high_resolution_clock::now();
        render_image_view(rdcon, block);
        auto ti2 = std::chrono::high_resolution_clock::now();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_IMAGE,
            std::chrono::duration<double, std::milli>(ti2 - ti1).count());
    }
    else if (block->embed && block->embed->video) {
        if (render_trace_enabled()) log_debug("[RENDER DISPATCH] calling render_video_content for <video>");
        if (!render_block_dirty_misses(rdcon, block)) {
            if (!render_block_try_retained_fragment(rdcon, block)) {
                RenderElementMarkerScope marker_scope = render_element_marker_begin(rdcon, block);
                rdcon->element_marker_suppression_depth++;
                render_block_view(rdcon, block);
                rdcon->element_marker_suppression_depth--;
                render_video_content(rdcon, block);
                render_element_marker_end(rdcon, &marker_scope);
            }
        }
    }
    else if (render_media_is_webview_layer(block)) {
        if (render_trace_enabled()) log_debug("[RENDER DISPATCH] calling render_webview_layer_content");
        if (!render_block_dirty_misses(rdcon, block)) {
            if (!render_block_try_retained_fragment(rdcon, block)) {
                RenderElementMarkerScope marker_scope = render_element_marker_begin(rdcon, block);
                rdcon->element_marker_suppression_depth++;
                render_block_view(rdcon, block);
                rdcon->element_marker_suppression_depth--;
                render_webview_layer_content(rdcon, block);
                render_element_marker_end(rdcon, &marker_scope);
            }
        }
    }
    else if (block->embed && block->embed->doc) {
        render_embed_doc(rdcon, block);
    }
    else if (block->blk && block->blk->list_style_type) {
        render_list_view(rdcon, block);
    }
    else {
        // Skip only absolute/fixed positioned elements - they are rendered separately.
        // Floats also have a position struct and remain in normal flow.
        if (skip_positioned_in_normal_flow && block->position &&
            (block->position->position == CSS_VALUE_ABSOLUTE ||
             block->position->position == CSS_VALUE_FIXED)) {
            log_debug("absolute/fixed positioned block, skip in normal rendering");
        } else {
            render_block_view(rdcon, block);
        }
    }
}

static void render_raster_walk_block(void* vctx, ViewBlock* block, float abs_x, float abs_y,
                                     FontBox* font, Color color) {
    (void)abs_x; (void)abs_y; (void)font; (void)color;
    RenderContext* rdcon = (RenderContext*)vctx;
    render_raster_dispatch_block(rdcon, block, true);
}

static void render_raster_walk_inline(void* vctx, ViewSpan* span, float abs_x, float abs_y,
                                      FontBox* font, Color color) {
    (void)abs_x; (void)abs_y; (void)font; (void)color;
    RenderContext* rdcon = (RenderContext*)vctx;
    if (!rdcon || !span) return;
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_DISPATCH);
    auto tiv1 = std::chrono::high_resolution_clock::now();
    render_inline_view(rdcon, span);
    auto tiv2 = std::chrono::high_resolution_clock::now();
    render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_INLINE,
        std::chrono::duration<double, std::milli>(tiv2 - tiv1).count());
}

static void render_raster_walk_text(void* vctx, ViewText* text, float abs_x, float abs_y,
                                    FontBox* font, Color color) {
    (void)abs_x; (void)abs_y; (void)font; (void)color;
    RenderContext* rdcon = (RenderContext*)vctx;
    if (!rdcon || !text) return;
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_DISPATCH);
    auto tt1 = std::chrono::high_resolution_clock::now();
    render_text_view(rdcon, text);
    auto tt2 = std::chrono::high_resolution_clock::now();
    render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_TEXT,
        std::chrono::duration<double, std::milli>(tt2 - tt1).count());
}

static void render_raster_walk_marker(void* vctx, ViewSpan* marker, float abs_x, float abs_y,
                                      FontBox* font, Color color) {
    (void)abs_x; (void)abs_y; (void)font; (void)color;
    RenderContext* rdcon = (RenderContext*)vctx;
    if (!rdcon || !marker) return;
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_DISPATCH);
    render_marker_view(rdcon, marker);
}

static void render_raster_walk_positioned_block(void* vctx, ViewBlock* block, float abs_x, float abs_y,
                                                FontBox* font, Color color) {
    (void)abs_x; (void)abs_y; (void)font; (void)color;
    RenderContext* rdcon = (RenderContext*)vctx;
    render_raster_dispatch_block(rdcon, block, false);
}

static void render_raster_backend_init(RenderBackend* backend, RenderContext* rdcon) {
    if (!backend) return;
    *backend = {};
    backend->ctx = rdcon;
    backend->render_block = render_raster_walk_block;
    backend->render_inline = render_raster_walk_inline;
    backend->render_text = render_raster_walk_text;
    backend->render_marker = render_raster_walk_marker;
}

static void render_raster_walk_state_init(RenderWalkState* walk_state, RenderContext* rdcon) {
    if (!walk_state || !rdcon) return;
    *walk_state = {};
    walk_state->x = rdcon->block.x;
    walk_state->y = rdcon->block.y;
    walk_state->font = rdcon->font;
    walk_state->color = rdcon->color;
    walk_state->ui_context = rdcon->ui_context;
}

void render_raster_positioned_children(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block || !block->position) return;
    RenderBackend backend;
    render_raster_backend_init(&backend, rdcon);
    backend.render_block = render_raster_walk_positioned_block;
    RenderWalkState walk_state;
    render_raster_walk_state_init(&walk_state, rdcon);
    render_walk_positioned_children(&backend, &walk_state, block);
}

void render_raster_positive_z_descendants(RenderContext* rdcon, View* view) {
    if (!rdcon || !view) return;
    RenderBackend backend;
    render_raster_backend_init(&backend, rdcon);
    RenderWalkState walk_state;
    render_raster_walk_state_init(&walk_state, rdcon);
    render_walk_positive_z_descendants(&backend, &walk_state, view);
}

void render_children(RenderContext* rdcon, View* view) {
    if (!rdcon || !view) return;
    auto trc_start = std::chrono::high_resolution_clock::now();

    RenderBackend backend;
    render_raster_backend_init(&backend, rdcon);

    RenderWalkState walk_state;
    render_raster_walk_state_init(&walk_state, rdcon);

    render_walk_children(&backend, &walk_state, view);

    auto trc_end = std::chrono::high_resolution_clock::now();
    render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_CHILDREN,
        std::chrono::duration<double, std::milli>(trc_end - trc_start).count());
}

void render_raster_view_tree(RenderContext* rdcon, ViewTree* view_tree) {
    if (!rdcon || !view_tree) return;

    View* root_view = view_tree->root;
    if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
        log_debug("Render root view");
        render_children(rdcon, root_view);
    } else {
        log_error("Invalid root view");
    }
}
