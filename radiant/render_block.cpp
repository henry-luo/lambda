#include "render.hpp"
#include "render_css3d.hpp"
#include "event.hpp"
#include "layout.hpp"
#include "layout_table.hpp"
#include "render_metrics.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/time_util.h"
#include "../lib/font/font.h"
#include "../lambda/input/css/dom_element.hpp"

#include <stdlib.h>

// CSS 2.2 §17.4: captions extend the wrapper outside the painted table grid.
Rect render_geometry_boundary_rect(const ViewBlock* view, Rect wrapper) {
    if (!view || view->view_type != RDT_VIEW_TABLE) return wrapper;
    RdtLogicalRect grid = layout_table_grid_rect(static_cast<const ViewTable*>(view));
    float scale = view->width > 0.0f ? wrapper.width / view->width : 1.0f;
    return {wrapper.x + grid.x * scale, wrapper.y + grid.y * scale,
        grid.width * scale, grid.height * scale};
}


#define DEBUG_RENDER_BLOCK 0

static bool render_block_trace_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        // Color inheritance traces fire for nearly every block on full pages;
        // keep them opt-in to avoid turning rendering into log I/O.
        enabled = getenv("RADIANT_TRACE_RENDER") ? 1 : 0;
    }
    return enabled != 0;
}

static bool render_block_clip_empty(const Bound* clip) {
    return !clip || clip->right <= clip->left || clip->bottom <= clip->top;
}

static bool render_block_fully_transparent(ViewBlock* block) {
    return block && block->in_line &&
        block->inl()->opacity >= 0.0f && block->inl()->opacity <= 0.0005f;
}

static bool render_block_has_visible_child_overflow(RasterRenderContext* rdcon, ViewBlock* block) {
    if (!block || !block->is_element() || !block->first_child) return false;
    if (block->blk && block->block()->contain_paint) return false;
    if (block->scroller &&
        (block->scroll()->overflow_x != CSS_VALUE_VISIBLE ||
         block->scroll()->overflow_y != CSS_VALUE_VISIBLE)) {
        return false;
    }

    float min_x = 0.0f, max_x = 0.0f;
    float min_y = 0.0f, max_y = 0.0f;
    layout_in_flow_content_bounds_cached(lam::view_require_element(block), LAYOUT_AXIS_X, true,
                                         rdcon ? rdcon->content_bounds_cache : NULL,
                                         &min_x, &max_x);
    layout_in_flow_content_bounds_cached(lam::view_require_element(block), LAYOUT_AXIS_Y, true,
                                         rdcon ? rdcon->content_bounds_cache : NULL,
                                         &min_y, &max_y);
    // Relative zero-height vlist rows paint their struts outside the border box.
    return min_x < 0.0f || max_x > block->width ||
           min_y < 0.0f || max_y > block->height ||
           render_text_subtree_overflows_block(rdcon, block);
}

bool render_block_dirty_misses(RasterRenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block || !rdcon->has_dirty_union) return false;

    float s = rdcon->raster_scale > 0 ? rdcon->raster_scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block, s) * s;
    Rect marker_rect = view_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    Bound dirty = {
        rdcon->dirty_union.left * s,
        rdcon->dirty_union.top * s,
        rdcon->dirty_union.right * s,
        rdcon->dirty_union.bottom * s
    };
    bool has_transform = rdcon->has_transform ||
        transform_has_functions(block->transform);
    return !has_transform &&
        !view_geometry_bounds_intersect(view_geometry_rect_to_bound(marker_rect), dirty) &&
        !render_block_has_visible_child_overflow(rdcon, block);
}

bool render_block_viewport_misses(RasterRenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block) return false;

    View* view = static_cast<View*>(block);
    if (!view->parent) return false;
    if (block->tag_id == MARKUP_NAME_HTML || block->tag_id == MARKUP_NAME_BODY) return false;

    bool has_transform = rdcon->has_transform ||
        transform_has_functions(block->transform);
    if (has_transform) return false;

    if (block->position && block->positionp()->first_abs_child) {
        return false;
    }
    float s = rdcon->raster_scale > 0 ? rdcon->raster_scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block, s) * s;
    Rect marker_rect = view_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    Bound marker_bound = view_geometry_rect_to_bound(marker_rect);
    return !view_geometry_bounds_intersect(marker_bound, rdcon->block.clip) &&
        !render_block_has_visible_child_overflow(rdcon, block);
}

struct RenderRetainedDirtySource {
    DomDocument* document;
    View* root;
};

static bool render_retained_dirty_source_inside(void* userdata, const DirtyRect* dirty) {
    auto* source = static_cast<RenderRetainedDirtySource*>(userdata);
    return dirty_source_in_view_subtree(source->document, source->root, dirty);
}

static bool render_block_solid_box(RasterRenderContext* rdcon, ViewBlock* block, Rect* rect, Color* color) {
    if (!rdcon || !block || !rdcon->html_box_cache_enabled || rdcon->paint_phase != RADIANT_PAINT_ALL ||
        rdcon->element_marker_suppression_depth || rdcon->has_transform || rdcon->css3d_context ||
        rdcon->block.has_clip_radius || rdcon->clip_shape_depth || rdcon->vector_clip_depth ||
        (block->view_type != RDT_VIEW_BLOCK && block->view_type != RDT_VIEW_INLINE_BLOCK) ||
        block->tag_id == MARKUP_NAME_HTML || block->tag_id == MARKUP_NAME_BODY || !block->parent ||
        block->first_child || block->transform || block->scroller || block->embed ||
        block->form_control() || block->vector_path() || block->custom_layout_paint_prop() ||
        block->filter_prop() || block->backdrop_filter_prop() || block->layout_fragment_list() ||
        view_visibility_is_hidden(block) ||
        (block->position && block->positionp()->first_abs_child) ||
        (block->blk && block->block()->list_style_type) ||
        (block->in_line && (block->inl()->opacity != 1.0f ||
            (block->inl()->mix_blend_mode && block->inl()->mix_blend_mode != CSS_VALUE_NORMAL)))) return false;
    if (css_display_element_is_replaced(block)) return false;
    // reuse is checked before the box opens its CSS clip scope; inspect the declaration as well.
    if (render_clip_css_value(block)) return false;
    const auto* bound = block->boundary();
    if (!bound || bound->border || bound->mask || bound->box_shadow || bound->outline ||
        !bound->background || !bound->background->color.a ||
        !render_paint_background_is_simple(bound->background)) return false;
    float scale = rdcon->raster_scale;
    if (!isfinite(scale) || scale <= 0) return false;
    *rect = render_geometry_block_border_rect(&rdcon->block, block, scale);
    RdtMatrix density = rdt_matrix_scale(scale, scale);
    float x = rect->x / scale, y = rect->y / scale;
    float left, top, right, bottom;
    // the ordinary background lowerer projects logical edges; admit only identical physical primitives.
    if (!rdt_matrix_project_rect_bounds(&density, x, y, x + rect->width / scale,
            y + rect->height / scale, &left, &top, &right, &bottom) ||
        left != rect->x || top != rect->y || right - left != rect->width || bottom - top != rect->height) return false;
    const Bound& clip = rdcon->block.clip;
    // omit the redundant clip only when every touched pixel lies in its fully covered interior.
    if (!isfinite(rect->x) || !isfinite(rect->y) || !isfinite(rect->width) || !isfinite(rect->height) ||
        rect->width <= 0 || rect->height <= 0 ||
        !(floorf(rect->x) >= ceilf(clip.left) && floorf(rect->y) >= ceilf(clip.top) &&
          ceilf(rect->x + rect->width) <= floorf(clip.right) &&
          ceilf(rect->y + rect->height) <= floorf(clip.bottom))) return false;
    *color = bound->background->color;
    return true;
}

bool render_block_try_retained_fragment(RasterRenderContext* rdcon, ViewBlock* block, bool allow_solid_box) {
    if (!rdcon || !block || !rdcon->dl || !rdcon->retained_dl_cache ||
        rdcon->paint_phase != RADIANT_PAINT_ALL ||
        rdcon->element_marker_suppression_depth > 0 ||
        rdcon->css3d_context) {
        return false;
    }
    Rect solid_rect = {}; Color solid_color = {};
    bool solid = allow_solid_box && render_block_solid_box(rdcon, block, &solid_rect, &solid_color);
    if ((!solid && !rdcon->has_dirty_union) || render_block_has_visible_child_overflow(rdcon, block)) return false;

    uint32_t view_id = static_cast<View*>(block)->id;
    const RetainedDisplayListFragment* fragment =
        retained_dl_cache_get(rdcon->retained_dl_cache, view_id);
    if (!fragment) {
        retained_dl_cache_note_reuse_miss(rdcon->retained_dl_cache);
        return false;
    }
    uint64_t current_video_generation = 0;
    uint64_t current_glyph_generation = 0;
    if (rdcon->ui_context && rdcon->ui_context->document &&
        rdcon->ui_context->document->state) {
        current_video_generation = rdcon->ui_context->document->state->video_frame_generation;
    }
    if (rdcon->ui_context) {
        current_glyph_generation =
            font_context_glyph_cache_generation(rdcon->ui_context->font_ctx);
    }
    if (!retained_dl_fragment_resources_valid(fragment, current_video_generation,
                                             current_glyph_generation)) {
        retained_dl_cache_note_reuse_rejected_resources(rdcon->retained_dl_cache);
        return false;
    }

    if (solid) {
        // a failed complete-paint check requires fresh recording, including during selective repaint.
        if (!retained_dl_append_solid_box(rdcon->dl, fragment, solid_rect, solid_color)) return false;
        RENDER_METRIC_ADD(html_box_reuses, 1);
        Bound canonical = retained_dl_fragment_marker_bounds(fragment);
        if (solid_rect.x != canonical.left || solid_rect.y != canonical.top)
            RENDER_METRIC_ADD(html_box_translations, 1);
        retained_dl_cache_note_reuse_hit(rdcon->retained_dl_cache);
        return true;
    }
    if (!rdcon->has_dirty_union) return false;

    float s = rdcon->raster_scale > 0 ? rdcon->raster_scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block, s) * s;
    Rect marker_rect = view_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    Bound marker_bound = {
        marker_rect.x,
        marker_rect.y,
        marker_rect.x + marker_rect.width,
        marker_rect.y + marker_rect.height
    };
    RenderRetainedDirtySource source = {
        rdcon->ui_context ? rdcon->ui_context->document.get() : nullptr,
        static_cast<View*>(block)
    };
    if (!retained_dl_append_fragment_for_dirty(
            rdcon->dl, fragment, marker_bound, rdcon->dirty_tracker, rdcon->raster_scale,
            render_retained_dirty_source_inside, &source)) {
        retained_dl_cache_note_reuse_rejected_dirty(rdcon->retained_dl_cache);
        return false;
    }
    retained_dl_cache_note_reuse_hit(rdcon->retained_dl_cache);
    return true;
}

RenderElementMarkerScope render_element_marker_begin(RasterRenderContext* rdcon, ViewBlock* block) {
    RenderElementMarkerScope scope = { -1 };
    // a partial phase cannot own or replay a complete contiguous subtree fragment.
    if (!rdcon || !rdcon->dl || !block || rdcon->paint_phase != RADIANT_PAINT_ALL ||
        rdcon->element_marker_suppression_depth > 0) {
        return scope;
    }

    float s = rdcon->raster_scale > 0 ? rdcon->raster_scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block, s) * s;
    Rect marker_rect = view_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    uint32_t view_id = static_cast<View*>(block)->id;
    if (rdcon->presentation_damage && rdcon->dl == rdcon->presentation_damage_list.get())
        presentation_damage_note_candidate(rdcon->presentation_damage.get(), block);
    scope.begin_index = dl_begin_element(rdcon->dl, view_id,
        marker_rect.x, marker_rect.y, marker_rect.width, marker_rect.height);
    return scope;
}

void render_element_marker_end(RasterRenderContext* rdcon, RenderElementMarkerScope* scope) {
    if (!rdcon || !rdcon->dl || !scope || scope->begin_index < 0) {
        return;
    }
    uint32_t view_id = rdcon->dl->data()[scope->begin_index].element_marker.view_id;
    dl_end_element(rdcon->dl, scope->begin_index);
    if (rdcon->presentation_damage && rdcon->dl == rdcon->presentation_damage_list.get()) {
        presentation_damage_note_range(rdcon->presentation_damage.get(), rdcon->dl, view_id,
            scope->begin_index, rdcon->dl->item_count() - 1);
    }
    scope->begin_index = -1;
}

void render_bound(RasterRenderContext* rdcon, ViewBlock* view) {
    if (!render_paint_phase_has_boundary(rdcon->paint_phase, view)) return;
    float s = rdcon->raster_scale;
    Rect rect = render_geometry_boundary_rect(view, render_geometry_block_border_rect(&rdcon->block, view, s));
    Rect solid_rect = {}; Color solid_color = {};
    if (render_block_solid_box(rdcon, view, &solid_rect, &solid_color)) {
        // this complete atomic box has no other paint; its canonical leaf needs no caller clip snapshot.
        render_background(rdcon, view, rect, false);
        RENDER_METRIC_ADD(html_box_recordings, 1);
        return;
    }
    if (!view->bound) { render_background(rdcon, view, rect); return; }
    if (render_paint_boundary_emit_fragmented_simple(rdcon->paint_list, view, rect)) {
        paint_ir_lower_raster_fragment(rdcon->paint_list, rdcon->dl);
        return;
    }

    // Resolve percentage border-radius values against element's own dimensions (in CSS px)
    if (view->boundary()->border) {
        resolve_border_radius_percentages(&view->boundary_mut()->border->radius, view->width, view->height);
    }

    // Render box-shadow BEFORE background (shadows go underneath the element)
    if (view->boundary()->box_shadow) {
        render_box_shadow(rdcon, view, rect);
    }

    // Render background (gradient, solid color, and background-image) using new rendering system
    if (view->boundary()->background || render_background_uses_table_layers(view)) {
        render_background(rdcon, view, rect);
    }

    // Render inset box-shadow AFTER background (inside the element)
    if (view->boundary()->box_shadow) {
        render_box_shadow_inset(rdcon, view, rect);
    }

    // Render borders using new rendering system
    if (view->boundary()->border) {

        // CSS 2.1 17.6.2: Use resolved borders for border-collapse cells
        bool use_resolved = false;
        CollapsedBorder* resolved_top = nullptr;
        CollapsedBorder* resolved_right = nullptr;
        CollapsedBorder* resolved_bottom = nullptr;
        CollapsedBorder* resolved_left = nullptr;

        if (view->view_type == RDT_VIEW_TABLE_CELL) {
            ViewTableCell* cell = lam::view_require_table_cell(view);
            if (cell->td && cell->td->top_resolved) {
                use_resolved = true;
                resolved_top = cell->td->top_resolved;
                resolved_right = cell->td->right_resolved;
                resolved_bottom = cell->td->bottom_resolved;
                resolved_left = cell->td->left_resolved;
            }
        }

        if (use_resolved) {
            // Render collapsed borders using resolved border data (table cells)
            // CSS 2.1 17.6.2: Collapsed borders are centered on the cell edges.
            if (resolved_left && resolved_left->style != CSS_VALUE_NONE && resolved_left->color.a) {
                float bw = resolved_left->width * s;
                Rect border_rect = rect;
                border_rect.x = rect.x - bw / 2.0f;
                border_rect.width = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect,
                                     resolved_left->color.c, &rdcon->block.clip,
                                     rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_right && resolved_right->style != CSS_VALUE_NONE && resolved_right->color.a) {
                float bw = resolved_right->width * s;
                Rect border_rect = rect;
                border_rect.x = rect.x + rect.width - bw / 2.0f;
                border_rect.width = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect,
                                     resolved_right->color.c, &rdcon->block.clip,
                                     rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_top && resolved_top->style != CSS_VALUE_NONE && resolved_top->color.a) {
                float bw = resolved_top->width * s;
                Rect border_rect = rect;
                border_rect.y = rect.y - bw / 2.0f;
                border_rect.height = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect,
                                     resolved_top->color.c, &rdcon->block.clip,
                                     rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_bottom && resolved_bottom->style != CSS_VALUE_NONE && resolved_bottom->color.a) {
                float bw = resolved_bottom->width * s;
                Rect border_rect = rect;
                border_rect.y = rect.y + rect.height - bw / 2.0f;
                border_rect.height = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect,
                                     resolved_bottom->color.c, &rdcon->block.clip,
                                     rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
        } else {
            render_border(rdcon, view, rect);
        }
    }


}

void render_outline_deferred(RasterRenderContext* rdcon, ViewBlock* view) {
    if (!view || view_visibility_is_hidden(view) || !view->bound || !view->boundary()->outline) return;
    float s = rdcon->raster_scale;
    BlockBlot saved = rdcon->block;
    RenderTransformScope transform_scope = render_state_push_transform(rdcon, view, &saved);
    rdcon->block.x = saved.x + view->x * s;
    rdcon->block.y = saved.y + view->y * s;
    Rect rect;
    rect.x = rdcon->block.x;  rect.y = rdcon->block.y;
    rect.width = view->width * s;  rect.height = view->height * s;
    if (view->boundary()->border) {
        resolve_border_radius_percentages(&view->boundary_mut()->border->radius, view->width, view->height);
    }
    render_outline(rdcon, view, rect);
    render_state_pop_transform(&transform_scope);
    rdcon->block = saved;
}

static void render_block_debug_rect(RasterRenderContext* rdcon, Rect rect, Bound* clip) {
    RdtPath* p = rdt_path_new();
    rdt_path_move_to(p, rect.x, rect.y);
    rdt_path_line_to(p, rect.x + rect.width, rect.y);
    rdt_path_line_to(p, rect.x + rect.width, rect.y + rect.height);
    rdt_path_line_to(p, rect.x, rect.y + rect.height);
    rdt_path_close(p);

    float dash_pattern[2] = {8.0f, 8.0f};
    Color debug_color;
    debug_color.r = 255; debug_color.g = 0; debug_color.b = 0; debug_color.a = 100;

    RdtPath* clip_p = rdt_path_new();
    rdt_path_add_rect(clip_p, clip->left, clip->top,
                      clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    rc_push_clip(rdcon, clip_p, NULL);

    rc_stroke_path(rdcon, p, debug_color, 2.0f, RDT_CAP_BUTT, RDT_JOIN_MITER,
                   dash_pattern, 2, NULL);

    rc_pop_clip(rdcon);
    rdt_path_free(clip_p);
    rdt_path_free(p);
}

typedef struct RenderBlockPhase {
    BlockBlot parent_block;
    FontBox parent_font;
    Color parent_color;
    RenderElementMarkerScope marker_scope;
    RenderTransformScope transform_scope;
    RenderClipScope css_clip_scope;
    RenderEffectGroup effect_group;
    bool self_hidden;
} RenderBlockPhase;

typedef struct RenderBlockChildrenPhase {
    RenderClipScope overflow_clip_scope;
    uint64_t start_time;
    bool has_children;
} RenderBlockChildrenPhase;

typedef struct RenderBlockPaintResult {
    double children_time;
    bool painted;
} RenderBlockPaintResult;

static bool render_block_skip_paint(RasterRenderContext* rdcon, ViewBlock* block, bool allow_solid_box) {
    if (!rdcon || !block) {
        return true;
    }
    if (render_text_paint_atomic_hidden(rdcon, block)) return true;
    if (view_backface_is_hidden(block, BACKFACE_SUBTREE)) return true;
    if (render_block_clip_empty(&rdcon->block.clip)) {
        return true;
    }
    if (render_block_fully_transparent(block)) {
        return true;
    }
    if (render_block_viewport_misses(rdcon, block)) {
        return true;
    }

    // Early exit if the block's own visual bounds are entirely outside the
    // dirty union. Transformed blocks stay conservative because their marker
    // bounds are finalized from recorded display-list item bounds.
    if (render_block_dirty_misses(rdcon, block)) {
        return true;
    }
    if (render_block_try_retained_fragment(rdcon, block, allow_solid_box)) {
        return true;
    }
    return false;
}

static void render_block_log_begin(RasterRenderContext* rdcon, ViewBlock* block) {
    log_enter();
}

static void render_block_log_end() {
    log_leave();
}

static void render_block_setup_font(RasterRenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block || !block->font) return;
    uint64_t t1 = time_now_ns();
    setup_font(rdcon->ui_context, &rdcon->font, block->font);
    uint64_t t2 = time_now_ns();
    render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_SETUP_FONT,
        time_elapsed_ms_f(t1, t2));
}

static RenderBlockPhase render_block_begin_phase(RasterRenderContext* rdcon, ViewBlock* block) {
    RenderBlockPhase phase = {};
    phase.parent_block = rdcon->block;
    phase.parent_font = rdcon->font;
    phase.parent_color = rdcon->color;
    phase.marker_scope = render_element_marker_begin(rdcon, block);

    // CSS 2.1 11.2: visibility:hidden suppresses own rendering but children
    // with visibility:visible should still appear.
    phase.self_hidden = view_visibility_is_hidden(block) ||
        view_backface_is_hidden(block);
    phase.transform_scope = render_state_push_transform(rdcon, block, &phase.parent_block);

    render_block_setup_font(rdcon, block);

    phase.css_clip_scope = render_clip_push_css_scope(rdcon, block,
        phase.parent_block.x, phase.parent_block.y, rdcon->raster_scale);
    phase.effect_group = render_effect_group_begin(rdcon, block, &phase.parent_block);
    return phase;
}

static void render_block_paint_self(RasterRenderContext* rdcon, ViewBlock* block,
                                    RenderBlockPhase* phase) {
    if (!rdcon || !block || !phase) return;

    if (!phase->self_hidden && (block->bound || render_background_uses_table_layers(block)) &&
        !render_paint_boundary_hidden(block)) {
        uint64_t tb1 = time_now_ns();
        render_bound(rdcon, block);
        uint64_t tb2 = time_now_ns();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_BOUND,
            time_elapsed_ms_f(tb1, tb2));
    }

    if (render_paint_phase_has_content(rdcon->paint_phase) &&
        !phase->self_hidden && !view_contents_are_skipped(block) &&
        block->vector_path() && block->vector_path()->segments) {
        render_vector_path(rdcon, block);
    }

    if (render_paint_phase_has_content(rdcon->paint_phase) &&
        !phase->self_hidden && !view_contents_are_skipped(block) &&
        block->form_control()) {
        render_form_control(rdcon, block);
    }

    float s = rdcon->raster_scale;
    rdcon->block.x = phase->parent_block.x + block->x * s;
    rdcon->block.y = phase->parent_block.y + block->y * s;

    if (DEBUG_RENDER_BLOCK) {
        Rect rc;
        rc.x = rdcon->block.x - (block->bound ? block->boundary()->margin.left * s : 0);
        rc.y = rdcon->block.y - (block->bound ? block->boundary()->margin.top * s : 0);
        rc.width = block->width * s +
            (block->bound ? (block->boundary()->margin.left + block->boundary()->margin.right) * s : 0);
        rc.height = block->height * s +
            (block->bound ? (block->boundary()->margin.top + block->boundary()->margin.bottom) * s : 0);
        render_block_debug_rect(rdcon, rc, &rdcon->block.clip);
    }
}

static bool block_should_paint_children(ViewBlock* block) {
    if (!block || view_contents_are_skipped(block)) return false;
    // SVG owns descendant transforms/clips, including foreign HTML in background-only visits.
    if (block->tag_id == MARKUP_NAME_SVG) return false;
    if (block->form_control() &&
        block->tag() != MARKUP_NAME_BUTTON) {
        return false;
    }
    return true;
}

static void render_block_apply_inherited_color(RasterRenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block) return;
    if (block->in_line && block->inl()->has_color) {
        if (render_block_trace_enabled()) {
        }
        rdcon->color = block->inl()->color;
    } else {
        if (render_block_trace_enabled()) {
        }
    }
}

static void render_block_deferred_child_outlines(RasterRenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block) return;
    View* outline_view = block->first_child;
    while (outline_view) {
        if (outline_view->view_type == RDT_VIEW_BLOCK ||
            outline_view->view_type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* outline_block = lam::view_require_block(outline_view);
            if (!view_backface_is_hidden(outline_block) && outline_block->bound && outline_block->boundary_mut()->outline) {
                render_outline_deferred(rdcon, outline_block);
            }
        }
        outline_view = static_cast<View*>(outline_view->next_sibling);
    }
}

static RenderBlockChildrenPhase render_block_begin_children_phase(RasterRenderContext* rdcon,
                                                                  ViewBlock* block) {
    RenderBlockChildrenPhase phase = {};
    phase.start_time = time_now_ns();
    View* view = block ? block->first_child : nullptr;
    phase.has_children = view != nullptr || (block && block->custom_layout_paint_prop());
    if (!phase.has_children) {
        return phase;
    }

    float origin_x = rdcon->block.x, origin_y = rdcon->block.y;
    if (block->scroller || (block->blk && block->block()->contain_paint)) {
        setup_scroller(rdcon, block);
    }

    phase.overflow_clip_scope = render_clip_push_overflow_scope(rdcon, block, origin_x, origin_y);
    return phase;
}

static void render_block_walk_children_phase(RasterRenderContext* rdcon, ViewBlock* block,
                                             RenderBlockChildrenPhase* phase) {
    if (!rdcon || !block || !phase || !phase->has_children) return;

    bool content_phase = render_paint_phase_has_content(rdcon->paint_phase);
    if (content_phase) render_raster_negative_z_descendants(rdcon, block);
    bool custom_painted = render_raster_custom_layout_children(rdcon, block);
    if (!custom_painted) {
        render_children(rdcon, block->first_child);
    }
    if (content_phase && block->position) {
        render_raster_positioned_children(rdcon, block);
    }
    if (content_phase && !custom_painted) {
        render_raster_positive_z_descendants(rdcon, block->first_child);
    }
}

// negative layers are lifted to their stacking owner, retaining intervening scroll/overflow scopes.
static void render_raster_negative_path(RasterRenderContext* rdcon, View* owner, View* target) {
    View* child = radiant_stack_path_child(owner, target);
    if (!child) return;
    if (child == target) {
        render_raster_positioned_view(rdcon, lam::view_require_block(child));
        return;
    }
    ViewBlock* block = lam::view_as_block(child);
    if (!block) {
        FontBox font = rdcon->font;
        Color color = rdcon->color;
        ViewElement* span = lam::view_as_element(child);
        if (span && span->font) setup_font(rdcon->ui_context, &rdcon->font, span->font);
        if (span && span->in_line && span->inl()->has_color) rdcon->color = span->inl()->color;
        render_raster_negative_path(rdcon, child, target);
        rdcon->font = font; rdcon->color = color;
        return;
    }
    BlockBlot saved = rdcon->block;
    FontBox font = rdcon->font;
    Color color = rdcon->color;
    RenderClipScope css_clip = render_clip_push_css_scope(rdcon, block, saved.x, saved.y, rdcon->raster_scale);
    rdcon->block.x += block->x * rdcon->raster_scale;
    rdcon->block.y += block->y * rdcon->raster_scale;
    render_block_setup_font(rdcon, block);
    render_block_apply_inherited_color(rdcon, block);
    float origin_x = rdcon->block.x, origin_y = rdcon->block.y;
    if (block->scroller) setup_scroller(rdcon, block);
    RenderClipScope overflow = render_clip_push_overflow_scope(rdcon, block, origin_x, origin_y);
    render_raster_negative_path(rdcon, child, target);
    if (overflow.active) render_clip_pop_scope(rdcon, &overflow);
    if (css_clip.active) render_clip_pop_scope(rdcon, &css_clip);
    rdcon->block = saved; rdcon->font = font; rdcon->color = color;
}

static bool render_raster_negative_layer(void* context, View* owner, View* layer) {
    render_raster_negative_path((RasterRenderContext*)context, owner, layer);
    return true;
}

void render_raster_negative_z_descendants(RasterRenderContext* rdcon, ViewBlock* block) {
    radiant_stack_visit_negative_z_descendants(block, false, rdcon, render_raster_negative_layer);
}

static double render_block_finish_children_phase(RasterRenderContext* rdcon, ViewBlock* block,
                                                 RenderBlockChildrenPhase* phase) {
    if (!phase) return 0;

    if (phase->has_children) {
        if (phase->overflow_clip_scope.active) {
            uint64_t toc1 = time_now_ns();
            render_clip_pop_scope(rdcon, &phase->overflow_clip_scope);
            uint64_t toc2 = time_now_ns();
            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_OVERFLOW_CLIP,
                time_elapsed_ms_f(toc1, toc2));
        }

        if (render_paint_phase_has_content(rdcon->paint_phase))
            render_block_deferred_child_outlines(rdcon, block);
    }

    return time_elapsed_ms_f(phase->start_time, time_now_ns());
}

double render_block_paint_children(RasterRenderContext* rdcon, ViewBlock* block) {
    RenderBlockChildrenPhase phase = render_block_begin_children_phase(rdcon, block);
    render_block_walk_children_phase(rdcon, block, &phase);
    return render_block_finish_children_phase(rdcon, block, &phase);
}

static void render_block_finish_phase(RasterRenderContext* rdcon, ViewBlock* block,
                                      RenderBlockPhase* phase) {
    if (!rdcon || !block || !phase) return;

    if (render_paint_phase_has_content(rdcon->paint_phase) && !phase->self_hidden && block->scroller) {
        render_scroller(rdcon, block, &phase->parent_block);
    }

    if (render_paint_phase_has_content(rdcon->paint_phase) && !phase->self_hidden &&
        block->multicol_prop() && block->multicol_prop()->computed_column_count > 1) {
        render_column_rules(rdcon, block);
    }

    render_effect_group_finish(&phase->effect_group, block, &rdcon->block.clip);

    if (phase->css_clip_scope.active) {
        uint64_t tc1 = time_now_ns();
        render_clip_pop_scope(rdcon, &phase->css_clip_scope);
        uint64_t tc2 = time_now_ns();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_CLIP,
            time_elapsed_ms_f(tc1, tc2));
    }

    render_state_pop_transform(&phase->transform_scope);

    rdcon->block = phase->parent_block;
    rdcon->font = phase->parent_font;
    rdcon->color = phase->parent_color;
    render_element_marker_end(rdcon, &phase->marker_scope);
}

typedef struct RasterBlockPaintDriver : RenderPaintBlockDriver {
    RasterRenderContext* rdcon;
    RenderBlockPhase phase;
    void (*content)(RasterRenderContext*, ViewBlock*);
} RasterBlockPaintDriver;

static bool raster_block_paint_begin(RenderPaintBlockDriver* ctx, ViewBlock* block, void** phase) {
    RasterBlockPaintDriver* driver = static_cast<RasterBlockPaintDriver*>(ctx);
    if (!driver || !driver->rdcon || !block || !phase) return false;
    render_block_log_begin(driver->rdcon, block);
    driver->phase = render_block_begin_phase(driver->rdcon, block);
    if (driver->rdcon->css3d_context) driver->rdcon->css3d_context->geometry(block);
    *phase = &driver->phase;
    return true;
}

static bool raster_block_paint_self(RenderPaintBlockDriver* ctx, ViewBlock* block, void* phase) {
    RasterBlockPaintDriver* driver = static_cast<RasterBlockPaintDriver*>(ctx);
    if (!driver || !driver->rdcon || !block || !phase) return false;
    render_block_paint_self(driver->rdcon, block, (RenderBlockPhase*)phase);
    render_block_apply_inherited_color(driver->rdcon, block);
    if (view_contents_are_skipped(block)) return false;
    if (driver->content) {
        RenderBlockPhase* current=(RenderBlockPhase*)phase;
        if (!current->self_hidden && render_paint_phase_has_content(driver->rdcon->paint_phase)) {
            // replaced pixels belong inside the same CSS transform, clip and opacity group as the box.
            BlockBlot saved=driver->rdcon->block;
            driver->rdcon->block.x=current->parent_block.x;driver->rdcon->block.y=current->parent_block.y;
            driver->content(driver->rdcon,block);driver->rdcon->block=saved;
        }
        return false;
    }
    return block_should_paint_children(block);
}

static double raster_block_paint_children(RenderPaintBlockDriver* ctx, ViewBlock* block, void* phase) {
    (void)phase;
    RasterBlockPaintDriver* driver = static_cast<RasterBlockPaintDriver*>(ctx);
    if (!driver || !driver->rdcon || !block) return 0.0;
    RenderTextPaintScope text_paint(driver->rdcon, block,
        &driver->rdcon->font, &driver->rdcon->color);
    return render_block_paint_children(driver->rdcon, block);
}

static void raster_block_paint_finish(RenderPaintBlockDriver* ctx, ViewBlock* block, void* phase) {
    RasterBlockPaintDriver* driver = static_cast<RasterBlockPaintDriver*>(ctx);
    if (!driver || !driver->rdcon || !block || !phase) return;
    render_block_finish_phase(driver->rdcon, block, (RenderBlockPhase*)phase);
    render_block_log_end();
}

static RenderBlockPaintResult render_block_run_paint_pipeline(RasterRenderContext* rdcon,
                                                              ViewBlock* block,
                                                              void (*content)(RasterRenderContext*, ViewBlock*)) {
    RenderBlockPaintResult result = {};
    RasterBlockPaintDriver driver = {};
    driver.rdcon = rdcon;driver.content=content;
    RenderPaintBlockOps ops = {};
    ops.ctx = lam::up(&driver);
    ops.begin = raster_block_paint_begin;
    ops.paint_self = raster_block_paint_self;
    ops.paint_children = raster_block_paint_children;
    ops.finish = raster_block_paint_finish;
    RenderPaintBlockResult shared_result = render_paint_block_run(&ops, block);
    result.children_time = shared_result.children_time;
    result.painted = shared_result.painted;
    return result;
}

static void render_block_finish_profile(RasterRenderContext* rdcon,
                                        RenderBlockPaintResult* result,
                                        uint64_t start_time) {
    if (!rdcon || !result || !result->painted) return;
    double this_total = time_elapsed_ms_f(start_time, time_now_ns());
    render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_BLOCK_SELF,
                             this_total - result->children_time);
}

// a paint layer's capture records the block offscreen through the same pipeline
static void render_block_record_live(RasterRenderContext* rdcon, ViewBlock* block,
                                     void (*content)(RasterRenderContext*, ViewBlock*)) {
    render_block_run_paint_pipeline(rdcon, block, content);
}

void render_block_view(RasterRenderContext* rdcon, ViewBlock* block) {
    render_block_view_content(rdcon,block,nullptr);
}

void render_block_view_content(RasterRenderContext* rdcon, ViewBlock* block,
    void (*content)(RasterRenderContext*, ViewBlock*)) {
    uint64_t rbv_start = time_now_ns();
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_BLOCK);

    // an external producer can start drawing after a formerly empty background-only recording.
    if (render_block_skip_paint(rdcon, block, !content)) return;

    RenderBlockPaintResult result = {};
    Css3dPaintContext* enclosing = rdcon->css3d_context;
    bool preserve = radiant::transform_preserves_3d(block);
    if (rdcon->dl && !enclosing && preserve) {
        Css3dPaintContext context(rdcon, block);
        rdcon->css3d_context = lam::up(&context);
        result = render_block_run_paint_pipeline(rdcon, block, content);
        rdcon->css3d_context = nullptr;
        if (!context.compose()) {
            // The original DOM-order stream is owned for cleanup, never a fallback.
            rdcon->paint_failed = true;
            log_error("[CSS3D_COMPOSE] failed to compose context view=%u", static_cast<View*>(block)->id);
        }
    } else if (enclosing) {
        Css3dPaintPlane parent = enclosing->enter(block);
        // A flat child paints as one plane. Its nested preserved contexts may
        // compose internally, without joining the enclosing context. A plane moved by a
        // presentation transform paints from its layer, before planes are ordered.
        if (!preserve) rdcon->css3d_context = nullptr;
        if (preserve || !render_paint_layer_try(rdcon, block, content, render_block_record_live, enclosing))
            result = render_block_run_paint_pipeline(rdcon, block, content);
        rdcon->css3d_context = lam::up(enclosing);
        enclosing->restore(parent);
    } else if (!render_paint_layer_try(rdcon, block, content, render_block_record_live, nullptr)) {
        result = render_block_run_paint_pipeline(rdcon, block, content);
    }
    render_block_finish_profile(rdcon, &result, rbv_start);
}
