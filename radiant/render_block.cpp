#include "render.hpp"
#include "scroller.hpp"
#include "layout.hpp"
#include "render.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lambda/input/css/dom_element.hpp"

#include <chrono>
#include <stdlib.h>

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
        block->in_line->opacity >= 0.0f && block->in_line->opacity <= 0.0005f;
}

bool render_block_dirty_misses(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block || !rdcon->has_dirty_union) return false;

    float s = rdcon->scale > 0 ? rdcon->scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block) * s;
    Rect marker_rect = render_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    Bound dirty = {
        rdcon->dirty_union.left * s,
        rdcon->dirty_union.top * s,
        rdcon->dirty_union.right * s,
        rdcon->dirty_union.bottom * s
    };
    bool has_transform = rdcon->has_transform ||
        (block->transform && block->transform->functions);
    return !has_transform &&
        !render_geometry_bounds_intersect(render_geometry_rect_to_bound(marker_rect), dirty);
}

bool render_block_viewport_misses(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block) return false;

    View* view = static_cast<View*>(block);
    if (!view->parent) return false;
    if (block->tag_id == HTM_TAG_HTML || block->tag_id == HTM_TAG_BODY) return false;

    bool has_transform = rdcon->has_transform ||
        (block->transform && block->transform->functions);
    if (has_transform) return false;

    if (block->position && block->position->first_abs_child) {
        return false;
    }

    float s = rdcon->scale > 0 ? rdcon->scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block) * s;
    Rect marker_rect = render_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    Bound marker_bound = render_geometry_rect_to_bound(marker_rect);
    return !render_geometry_bounds_intersect(marker_bound, rdcon->block.clip);
}

static bool render_view_subtree_contains_id(View* view, uint32_t id) {
    if (!view || id == 0) return false;
    if (view->id == id) return true;
    if (!view->is_element()) return false;

    DomElement* elem = lam::dom_require_element(view);
    View* child = static_cast<View*>(elem->first_child);
    while (child) {
        if (render_view_subtree_contains_id(child, id)) return true;
        child = static_cast<View*>(child->next_sibling);
    }
    return false;
}

static bool render_retained_dirty_source_inside(void* userdata, uint32_t source_view_id) {
    return render_view_subtree_contains_id((View*)userdata, source_view_id);
}

bool render_block_try_retained_fragment(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block || !rdcon->dl || !rdcon->retained_dl_cache ||
        !rdcon->has_dirty_union || rdcon->element_marker_suppression_depth > 0) {
        return false;
    }

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

    float s = rdcon->scale > 0 ? rdcon->scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block) * s;
    Rect marker_rect = render_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    Bound marker_bound = {
        marker_rect.x,
        marker_rect.y,
        marker_rect.x + marker_rect.width,
        marker_rect.y + marker_rect.height
    };
    if (!retained_dl_append_fragment_for_dirty(
            rdcon->dl, fragment, marker_bound, rdcon->dirty_tracker, rdcon->scale,
            render_retained_dirty_source_inside, static_cast<View*>(block))) {
        retained_dl_cache_note_reuse_rejected_dirty(rdcon->retained_dl_cache);
        return false;
    }
    retained_dl_cache_note_reuse_hit(rdcon->retained_dl_cache);
    log_debug("[RETAINED_DL] reused view %u (%d items)",
              view_id, retained_dl_fragment_item_count(fragment));
    return true;
}

RenderElementMarkerScope render_element_marker_begin(RenderContext* rdcon, ViewBlock* block) {
    RenderElementMarkerScope scope = { -1 };
    if (!rdcon || !rdcon->dl || !block || rdcon->element_marker_suppression_depth > 0) {
        return scope;
    }

    float s = rdcon->scale > 0 ? rdcon->scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block) * s;
    Rect marker_rect = render_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    uint32_t view_id = static_cast<View*>(block)->id;
    scope.begin_index = dl_begin_element(rdcon->dl, view_id,
        marker_rect.x, marker_rect.y, marker_rect.width, marker_rect.height);
    return scope;
}

void render_element_marker_end(RenderContext* rdcon, RenderElementMarkerScope* scope) {
    if (!rdcon || !rdcon->dl || !scope || scope->begin_index < 0) {
        return;
    }
    dl_end_element(rdcon->dl, scope->begin_index);
    scope->begin_index = -1;
}

void render_bound(RenderContext* rdcon, ViewBlock* view) {
    float s = rdcon->scale;
    Rect rect;
    rect.x = rdcon->block.x + view->x * s;  rect.y = rdcon->block.y + view->y * s;
    rect.width = view->width * s;  rect.height = view->height * s;

    // Resolve percentage border-radius values against element's own dimensions (in CSS px)
    if (view->bound->border) {
        resolve_border_radius_percentages(&view->bound->border->radius, view->width, view->height);
    }

    // Render box-shadow BEFORE background (shadows go underneath the element)
    if (view->bound->box_shadow) {
        render_box_shadow(rdcon, view, rect);
    }

    RdtPath* mask_clip_path = nullptr;
    bool mask_clip_active = false;
    if (view->bound->mask && view->bound->mask->has_radial_gradient) {
        MaskProp* mask = view->bound->mask;
        float radius = mask->radius_is_percent
            ? mask->radius * (rect.width < rect.height ? rect.width : rect.height)
            : mask->radius * s;
        if (radius > 0.0f) {
            float cx = rect.x + mask->cx * rect.width;
            float cy = rect.y + mask->cy * rect.height;
            mask_clip_path = rdt_path_new();
            rdt_path_add_circle(mask_clip_path, cx, cy, radius, radius);
            rc_push_clip(rdcon, mask_clip_path, nullptr);
            mask_clip_active = true;
        }
    }

    // Render background (gradient, solid color, and background-image) using new rendering system
    if (view->bound->background) {
        render_background(rdcon, view, rect);
    }

    // Render inset box-shadow AFTER background (inside the element)
    if (view->bound->box_shadow) {
        render_box_shadow_inset(rdcon, view, rect);
    }

    // Render borders using new rendering system
    if (view->bound->border) {
        log_debug("render border");

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

    if (mask_clip_active) {
        rc_pop_clip(rdcon);
    }
    if (mask_clip_path) {
        rdt_path_free(mask_clip_path);
    }
}

void render_outline_deferred(RenderContext* rdcon, ViewBlock* view) {
    if (!view->bound || !view->bound->outline) return;
    float s = rdcon->scale;
    BlockBlot saved = rdcon->block;
    RenderTransformScope transform_scope = render_state_push_transform(rdcon, view, &saved);
    rdcon->block.x = saved.x + view->x * s;
    rdcon->block.y = saved.y + view->y * s;
    Rect rect;
    rect.x = rdcon->block.x;  rect.y = rdcon->block.y;
    rect.width = view->width * s;  rect.height = view->height * s;
    if (view->bound->border) {
        resolve_border_radius_percentages(&view->bound->border->radius, view->width, view->height);
    }
    render_outline(rdcon, view, rect);
    render_state_pop_transform(&transform_scope);
    rdcon->block = saved;
}

static void render_block_debug_rect(RenderContext* rdcon, Rect rect, Bound* clip) {
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
    std::chrono::high_resolution_clock::time_point start_time;
    bool has_children;
} RenderBlockChildrenPhase;

typedef struct RenderBlockPaintResult {
    double children_time;
    bool painted;
} RenderBlockPaintResult;

static bool render_block_skip_paint(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block) {
        return true;
    }
    if (render_block_clip_empty(&rdcon->block.clip)) {
        log_debug("render_block_skip_paint: empty inherited clip for %s",
                  block->node_name());
        return true;
    }
    if (render_block_fully_transparent(block)) {
        log_debug("render_block_skip_paint: opacity zero for %s",
                  block->node_name());
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
    if (render_block_try_retained_fragment(rdcon, block)) {
        return true;
    }
    return false;
}

static void render_block_log_begin(RenderContext* rdcon, ViewBlock* block) {
    log_debug("render block view:%s, clip:[%.0f,%.0f,%.0f,%.0f]", block->node_name(),
        rdcon->block.clip.left, rdcon->block.clip.top,
        rdcon->block.clip.right, rdcon->block.clip.bottom);
    log_enter();
}

static void render_block_log_end() {
    log_leave();
}

static bool render_block_empty_cell_hides_bound(ViewBlock* block) {
    if (!block || block->view_type != RDT_VIEW_TABLE_CELL) return false;
    ViewTableCell* cell = lam::view_require_table_cell(block);
    if (cell->td && cell->td->hide_empty) {
        log_debug("Skipping bound for empty cell (empty-cells: hide)");
        return true;
    }
    return false;
}

static void render_block_setup_font(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block || !block->font) return;
    auto t1 = std::chrono::high_resolution_clock::now();
    setup_font(rdcon->ui_context, &rdcon->font, block->font);
    auto t2 = std::chrono::high_resolution_clock::now();
    render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_SETUP_FONT,
        std::chrono::duration<double, std::milli>(t2 - t1).count());
}

static void render_block_default_list_marker(RenderContext* rdcon, ViewBlock* block,
                                             bool self_hidden) {
    if (!rdcon || !block || self_hidden || block->view_type != RDT_VIEW_LIST_ITEM) return;
    DomElement* li_elem = lam::dom_require_element(lam::view_dom_node(block));
    if (!li_elem->pseudo || !li_elem->pseudo->marker) {
        render_list_bullet(rdcon, block);
    }
}

static RenderBlockPhase render_block_begin_phase(RenderContext* rdcon, ViewBlock* block) {
    RenderBlockPhase phase = {};
    phase.parent_block = rdcon->block;
    phase.parent_font = rdcon->font;
    phase.parent_color = rdcon->color;
    phase.marker_scope = render_element_marker_begin(rdcon, block);

    // CSS 2.1 11.2: visibility:hidden suppresses own rendering but children
    // with visibility:visible should still appear.
    phase.self_hidden = block->in_line && block->in_line->visibility == VIS_HIDDEN;
    phase.transform_scope = render_state_push_transform(rdcon, block, &phase.parent_block);

    render_block_setup_font(rdcon, block);
    render_block_default_list_marker(rdcon, block, phase.self_hidden);

    phase.css_clip_scope = render_clip_push_css_scope(rdcon, block,
        phase.parent_block.x, phase.parent_block.y, rdcon->scale);
    phase.effect_group = render_effect_group_begin(rdcon, block, &phase.parent_block);
    return phase;
}

static void render_block_paint_self(RenderContext* rdcon, ViewBlock* block,
                                    RenderBlockPhase* phase) {
    if (!rdcon || !block || !phase) return;

    if (!phase->self_hidden && block->bound &&
        !render_block_empty_cell_hides_bound(block)) {
        auto tb1 = std::chrono::high_resolution_clock::now();
        render_bound(rdcon, block);
        auto tb2 = std::chrono::high_resolution_clock::now();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_BOUND,
            std::chrono::duration<double, std::milli>(tb2 - tb1).count());
    }

    if (block->vpath && block->vpath->segments) {
        render_vector_path(rdcon, block);
    }

    if (!phase->self_hidden &&
        block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form) {
        render_form_control(rdcon, block);
    }

    float s = rdcon->scale;
    rdcon->block.x = phase->parent_block.x + block->x * s;
    rdcon->block.y = phase->parent_block.y + block->y * s;

    if (DEBUG_RENDER_BLOCK) {
        Rect rc;
        rc.x = rdcon->block.x - (block->bound ? block->bound->margin.left * s : 0);
        rc.y = rdcon->block.y - (block->bound ? block->bound->margin.top * s : 0);
        rc.width = block->width * s +
            (block->bound ? (block->bound->margin.left + block->bound->margin.right) * s : 0);
        rc.height = block->height * s +
            (block->bound ? (block->bound->margin.top + block->bound->margin.bottom) * s : 0);
        render_block_debug_rect(rdcon, rc, &rdcon->block.clip);
    }
}

static bool block_should_paint_children(ViewBlock* block) {
    if (!block) return false;
    if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form &&
        block->tag() != HTM_TAG_BUTTON) {
        return false;
    }
    return true;
}

static void render_block_apply_inherited_color(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block) return;
    if (block->in_line && block->in_line->has_color) {
        if (render_block_trace_enabled()) {
            log_debug("[RENDER COLOR] element=%s setting color: #%02x%02x%02x (was #%02x%02x%02x) color.c=0x%08x",
                      block->node_name(),
                      block->in_line->color.r, block->in_line->color.g, block->in_line->color.b,
                      rdcon->color.r, rdcon->color.g, rdcon->color.b,
                      block->in_line->color.c);
        }
        rdcon->color = block->in_line->color;
    } else {
        if (render_block_trace_enabled()) {
            log_debug("[RENDER COLOR] element=%s inheriting color #%02x%02x%02x (in_line=%p, color.c=%u)",
                      block->node_name(), rdcon->color.r, rdcon->color.g, rdcon->color.b,
                      block->in_line, block->in_line ? block->in_line->color.c : 0);
        }
    }
}

static void render_block_deferred_child_outlines(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block) return;
    View* outline_view = block->first_child;
    while (outline_view) {
        if (outline_view->view_type == RDT_VIEW_BLOCK ||
            outline_view->view_type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* outline_block = lam::view_require_block(outline_view);
            if (outline_block->bound && outline_block->bound->outline) {
                render_outline_deferred(rdcon, outline_block);
            }
        }
        outline_view = static_cast<View*>(outline_view->next_sibling);
    }
}

static RenderBlockChildrenPhase render_block_begin_children_phase(RenderContext* rdcon,
                                                                  ViewBlock* block) {
    RenderBlockChildrenPhase phase = {};
    phase.start_time = std::chrono::high_resolution_clock::now();
    View* view = block ? block->first_child : nullptr;
    phase.has_children = view != nullptr || (block && block->custom_layout_paint);
    if (!phase.has_children) {
        log_debug("view has no child");
        return phase;
    }

    render_block_apply_inherited_color(rdcon, block);

    if (block->scroller) {
        setup_scroller(rdcon, block);
    }

    phase.overflow_clip_scope = render_clip_push_overflow_scope(rdcon);
    return phase;
}

static void render_block_walk_children_phase(RenderContext* rdcon, ViewBlock* block,
                                             RenderBlockChildrenPhase* phase) {
    if (!rdcon || !block || !phase || !phase->has_children) return;

    bool custom_painted = render_raster_custom_layout_children(rdcon, block);
    if (!custom_painted) {
        render_children(rdcon, block->first_child);
    }
    if (block->position) {
        log_debug("render absolute/fixed positioned children");
        render_raster_positioned_children(rdcon, block);
    }
    if (!custom_painted) {
        render_raster_positive_z_descendants(rdcon, block->first_child);
    }
}

static double render_block_finish_children_phase(RenderContext* rdcon, ViewBlock* block,
                                                 RenderBlockChildrenPhase* phase) {
    if (!phase) return 0;

    if (phase->has_children) {
        if (phase->overflow_clip_scope.active) {
            auto toc1 = std::chrono::high_resolution_clock::now();
            render_clip_pop_scope(rdcon, &phase->overflow_clip_scope);
            auto toc2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_OVERFLOW_CLIP,
                std::chrono::duration<double, std::milli>(toc2 - toc1).count());
        }

        render_block_deferred_child_outlines(rdcon, block);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end_time - phase->start_time).count();
}

static double render_block_paint_children_phase(RenderContext* rdcon, ViewBlock* block) {
    RenderBlockChildrenPhase phase = render_block_begin_children_phase(rdcon, block);
    render_block_walk_children_phase(rdcon, block, &phase);
    return render_block_finish_children_phase(rdcon, block, &phase);
}

static void render_block_finish_phase(RenderContext* rdcon, ViewBlock* block,
                                      RenderBlockPhase* phase) {
    if (!rdcon || !block || !phase) return;

    if (block->scroller) {
        render_scroller(rdcon, block, &phase->parent_block);
    }

    if (block->multicol && block->multicol->computed_column_count > 1) {
        render_column_rules(rdcon, block);
    }

    render_effect_group_finish(&phase->effect_group, block, &rdcon->block.clip);

    if (phase->css_clip_scope.active) {
        auto tc1 = std::chrono::high_resolution_clock::now();
        render_clip_pop_scope(rdcon, &phase->css_clip_scope);
        auto tc2 = std::chrono::high_resolution_clock::now();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_CLIP,
            std::chrono::duration<double, std::milli>(tc2 - tc1).count());
    }

    render_state_pop_transform(&phase->transform_scope);

    rdcon->block = phase->parent_block;
    rdcon->font = phase->parent_font;
    rdcon->color = phase->parent_color;
    render_element_marker_end(rdcon, &phase->marker_scope);
}

typedef struct RasterBlockPaintDriver {
    RenderContext* rdcon;
    RenderBlockPhase phase;
} RasterBlockPaintDriver;

static bool raster_block_paint_begin(void* ctx, ViewBlock* block, void** phase) {
    RasterBlockPaintDriver* driver = (RasterBlockPaintDriver*)ctx;
    if (!driver || !driver->rdcon || !block || !phase) return false;
    render_block_log_begin(driver->rdcon, block);
    driver->phase = render_block_begin_phase(driver->rdcon, block);
    *phase = &driver->phase;
    return true;
}

static bool raster_block_paint_self(void* ctx, ViewBlock* block, void* phase) {
    RasterBlockPaintDriver* driver = (RasterBlockPaintDriver*)ctx;
    if (!driver || !driver->rdcon || !block || !phase) return false;
    render_block_paint_self(driver->rdcon, block, (RenderBlockPhase*)phase);
    return block_should_paint_children(block);
}

static double raster_block_paint_children(void* ctx, ViewBlock* block, void* phase) {
    (void)phase;
    RasterBlockPaintDriver* driver = (RasterBlockPaintDriver*)ctx;
    if (!driver || !driver->rdcon || !block) return 0.0;
    return render_block_paint_children_phase(driver->rdcon, block);
}

static void raster_block_paint_finish(void* ctx, ViewBlock* block, void* phase) {
    RasterBlockPaintDriver* driver = (RasterBlockPaintDriver*)ctx;
    if (!driver || !driver->rdcon || !block || !phase) return;
    render_block_finish_phase(driver->rdcon, block, (RenderBlockPhase*)phase);
    render_block_log_end();
}

static RenderBlockPaintResult render_block_run_paint_pipeline(RenderContext* rdcon,
                                                              ViewBlock* block) {
    RenderBlockPaintResult result = {};
    RasterBlockPaintDriver driver = {};
    driver.rdcon = rdcon;
    RenderPaintBlockOps ops = {};
    ops.ctx = &driver;
    ops.begin = raster_block_paint_begin;
    ops.paint_self = raster_block_paint_self;
    ops.paint_children = raster_block_paint_children;
    ops.finish = raster_block_paint_finish;
    RenderPaintBlockResult shared_result = render_paint_block_run(&ops, block);
    result.children_time = shared_result.children_time;
    result.painted = shared_result.painted;
    return result;
}

static void render_block_finish_profile(RenderContext* rdcon,
                                        RenderBlockPaintResult* result,
                                        std::chrono::high_resolution_clock::time_point start_time) {
    if (!rdcon || !result || !result->painted) return;
    auto rbv_end = std::chrono::high_resolution_clock::now();
    double this_total = std::chrono::duration<double, std::milli>(rbv_end - start_time).count();
    render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_BLOCK_SELF,
                             this_total - result->children_time);
}

void render_block_view(RenderContext* rdcon, ViewBlock* block) {
    auto rbv_start = std::chrono::high_resolution_clock::now();
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_BLOCK);

    if (render_block_skip_paint(rdcon, block)) return;

    RenderBlockPaintResult result = render_block_run_paint_pipeline(rdcon, block);
    render_block_finish_profile(rdcon, &result, rbv_start);
}
