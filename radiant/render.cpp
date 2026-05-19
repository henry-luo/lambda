#include "render.hpp"
#include "render_border.hpp"
#include "render_background.hpp"
#include "render_effects.hpp"
#include "render_clip.hpp"
#include "render_state.hpp"
#include "render_profiler.hpp"
#include "render_text.hpp"
#include "render_output.hpp"
#include "render_selection.hpp"
#include "render_columns.hpp"
#include "render_list.hpp"
#include "render_vector_path.hpp"
#include "render_media.hpp"
#include "render_geometry.hpp"
#include "render_svg_inline.hpp"
#include "retained_display_list.hpp"
#include "scroller.hpp"
#include "layout.hpp"
#include "form_control.hpp"
#include "state_store.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/str.h"
#include "../lambda/input/css/dom_element.hpp"
#include <string.h>
#include <math.h>
#include <chrono>
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "lib/stb_image_write.h"

// Forward declaration for inline SVG rendering (defined in render_svg_inline.cpp)
void render_inline_svg(RenderContext* rdcon, ViewBlock* view);

#define DEBUG_RENDER 0

/**
 * Reset canvas target and draw shapes to buffer.
 * This resets ThorVG's dirty region tracking to prevent black backgrounds
 * when rendering multiple shapes to the same frame buffer.
 *
 * ThorVG's smart rendering tracks "dirty regions" and clears them before
 * each draw. When we render multiple shapes to the same buffer within one
 * frame, this causes previously drawn content to be cleared to black.
 * Resetting the target sets fulldraw=true, which bypasses dirty region clearing.
 */
// CollapsedBorder struct is now defined in view.hpp

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
// load_html_doc is declared in view.hpp (via layout.hpp)

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);
void render_children(RenderContext* rdcon, View* view);
void render_form_control(RenderContext* rdcon, ViewBlock* block);  // form controls
void render_select_dropdown(RenderContext* rdcon, ViewBlock* select, DocState* state);  // select dropdown popup

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

static bool render_dirty_rect_intersects_marker(RenderContext* rdcon,
                                                const DirtyRect* dirty,
                                                Rect marker_rect) {
    if (!rdcon || !dirty) return false;
    float s = rdcon->scale > 0 ? rdcon->scale : 1.0f;
    Bound dirty_bound = {
        dirty->x * s,
        dirty->y * s,
        (dirty->x + dirty->width) * s,
        (dirty->y + dirty->height) * s
    };
    return render_geometry_bounds_intersect(
        render_geometry_rect_to_bound(marker_rect), dirty_bound);
}

static bool render_retained_fragment_bounds_match(Bound cached, Rect current) {
    float left = current.x;
    float top = current.y;
    float right = current.x + current.width;
    float bottom = current.y + current.height;
    float tolerance = 0.5f;
    return fabsf(cached.left - left) <= tolerance &&
           fabsf(cached.top - top) <= tolerance &&
           fabsf(cached.right - right) <= tolerance &&
           fabsf(cached.bottom - bottom) <= tolerance;
}

bool render_block_try_retained_fragment(RenderContext* rdcon, ViewBlock* block) {
    if (!rdcon || !block || !rdcon->dl || !rdcon->retained_dl_cache ||
        !rdcon->has_dirty_union || rdcon->element_marker_suppression_depth > 0) {
        return false;
    }
    if (rdcon->has_transform || (block->transform && block->transform->functions)) {
        return false;
    }

    uint32_t view_id = static_cast<View*>(block)->id;
    const RetainedDisplayListFragment* fragment =
        retained_dl_cache_get(rdcon->retained_dl_cache, view_id);
    if (!fragment) return false;
    uint64_t current_video_generation = 0;
    if (rdcon->ui_context && rdcon->ui_context->document &&
        rdcon->ui_context->document->state) {
        current_video_generation = rdcon->ui_context->document->state->video_frame_generation;
    }
    if (!retained_dl_fragment_resources_valid(fragment, current_video_generation)) {
        return false;
    }

    float s = rdcon->scale > 0 ? rdcon->scale : 1.0f;
    float visual_overflow = render_geometry_block_visual_overflow(block) * s;
    Rect marker_rect = render_geometry_expand_rect(
        render_geometry_block_border_rect(&rdcon->block, block, s),
        visual_overflow);
    if (!render_retained_fragment_bounds_match(
            retained_dl_fragment_bounds(fragment), marker_rect)) {
        return false;
    }

    DirtyTracker* tracker = rdcon->dirty_tracker;
    if (!tracker || tracker->full_repaint || !tracker->dirty_list) return false;
    for (DirtyRect* dirty = tracker->dirty_list; dirty; dirty = dirty->next) {
        if (!render_dirty_rect_intersects_marker(rdcon, dirty, marker_rect)) {
            continue;
        }
        if (dirty->source_view_id == 0 ||
            render_view_subtree_contains_id(static_cast<View*>(block),
                                            dirty->source_view_id)) {
            return false;
        }
    }

    if (!retained_dl_append_fragment(rdcon->dl, fragment)) {
        return false;
    }
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

// Helper function to render linear gradient
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

        // CSS 2.1 §17.6.2: Use resolved borders for border-collapse cells
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
            // CSS 2.1 §17.6.2: Collapsed borders are centered on the cell edges.
            // Since cells are positioned half-border inward, we must shift borders
            // outward by half their width to center them on the cell edge.
            if (resolved_left && resolved_left->style != CSS_VALUE_NONE && resolved_left->color.a) {
                float bw = resolved_left->width * s;
                Rect border_rect = rect;
                border_rect.x = rect.x - bw / 2.0f;
                border_rect.width = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_left->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_right && resolved_right->style != CSS_VALUE_NONE && resolved_right->color.a) {
                float bw = resolved_right->width * s;
                Rect border_rect = rect;
                border_rect.x = rect.x + rect.width - bw / 2.0f;
                border_rect.width = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_right->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_top && resolved_top->style != CSS_VALUE_NONE && resolved_top->color.a) {
                float bw = resolved_top->width * s;
                Rect border_rect = rect;
                border_rect.y = rect.y - bw / 2.0f;
                border_rect.height = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_top->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_bottom && resolved_bottom->style != CSS_VALUE_NONE && resolved_bottom->color.a) {
                float bw = resolved_bottom->width * s;
                Rect border_rect = rect;
                border_rect.y = rect.y + rect.height - bw / 2.0f;
                border_rect.height = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_bottom->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
        } else {
            // Use new comprehensive border rendering
            render_border(rdcon, view, rect);
        }
    }

    // Note: outline is NOT rendered here. Per CSS spec (Appendix E),
    // outlines paint after all backgrounds/borders/shadows in the stacking context.
    // Outlines are rendered in a second pass by the parent's render loop.
}

/**
 * Render outline for a view block (called in a deferred pass after all siblings).
 */
void render_outline_deferred(RenderContext* rdcon, ViewBlock* view) {
    if (!view->bound || !view->bound->outline) return;
    float s = rdcon->scale;
    BlockBlot saved = rdcon->block;
    // Compute absolute position for this view
    rdcon->block.x = saved.x + view->x * s;
    rdcon->block.y = saved.y + view->y * s;
    Rect rect;
    rect.x = rdcon->block.x;  rect.y = rdcon->block.y;
    rect.width = view->width * s;  rect.height = view->height * s;
    if (view->bound->border) {
        resolve_border_radius_percentages(&view->bound->border->radius, view->width, view->height);
    }
    render_outline(rdcon, view, rect);
    rdcon->block = saved;
}

void draw_debug_rect(RenderContext* rdcon, Rect rect, Bound* clip) {
    RdtPath* p = rdt_path_new();
    rdt_path_move_to(p, rect.x, rect.y);
    rdt_path_line_to(p, rect.x + rect.width, rect.y);
    rdt_path_line_to(p, rect.x + rect.width, rect.y + rect.height);
    rdt_path_line_to(p, rect.x, rect.y + rect.height);
    rdt_path_close(p);

    // dash pattern for dotted line
    float dash_pattern[2] = {8.0f, 8.0f};
    Color debug_color;
    debug_color.r = 255; debug_color.g = 0; debug_color.b = 0; debug_color.a = 100;

    // clip region
    RdtPath* clip_p = rdt_path_new();
    rdt_path_add_rect(clip_p, clip->left, clip->top,
                      clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    rc_push_clip(rdcon, clip_p, NULL);

    rc_stroke_path(rdcon, p, debug_color, 2.0f, RDT_CAP_BUTT, RDT_JOIN_MITER, dash_pattern, 2, NULL);

    rc_pop_clip(rdcon);
    rdt_path_free(clip_p);
    rdt_path_free(p);
}

void render_block_view(RenderContext* rdcon, ViewBlock* block) {
    auto rbv_start = std::chrono::high_resolution_clock::now();
    double children_time = 0; // will accumulate time in child render calls
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_BLOCK);

    // Early exit if the block's own visual bounds are entirely outside the
    // dirty union.  Transformed blocks stay conservative because their marker
    // bounds are finalized from recorded display-list item bounds.
    if (render_block_dirty_misses(rdcon, block)) {
        return;
    }
    if (render_block_try_retained_fragment(rdcon, block)) {
        return;
    }

    log_debug("render block view:%s, clip:[%.0f,%.0f,%.0f,%.0f]", block->node_name(),
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
    log_enter();
    BlockBlot pa_block = rdcon->block;  FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    RenderElementMarkerScope dl_element_scope = render_element_marker_begin(rdcon, block);

    // CSS 2.1 §11.2: visibility:hidden — suppress own rendering but still render children
    // (children with visibility:visible should still appear)
    bool self_hidden = block->in_line && block->in_line->visibility == VIS_HIDDEN;

    RenderTransformScope transform_scope = render_state_push_transform(rdcon, block, &pa_block);

    if (block->font) {
        auto t1 = std::chrono::high_resolution_clock::now();
        setup_font(rdcon->ui_context, &rdcon->font, block->font);
        auto t2 = std::chrono::high_resolution_clock::now();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_SETUP_FONT,
            std::chrono::duration<double, std::milli>(t2 - t1).count());
    }
    // render bullet after setting the font, as bullet is rendered using the same font as the list item
    // Skip legacy render_list_bullet when a ::marker pseudo-element exists,
    // since render_marker_view will handle it during child traversal.
    if (!self_hidden && block->view_type == RDT_VIEW_LIST_ITEM) {
        DomElement* li_elem = lam::dom_require_element(lam::view_dom_node(block));
        if (!li_elem->pseudo || !li_elem->pseudo->marker) {
            render_list_bullet(rdcon, block);
        }
    }
    RenderClipScope css_clip_scope = render_clip_push_css_scope(rdcon, block,
        pa_block.x, pa_block.y, rdcon->scale);

    RenderEffectGroup effect_group = render_effect_group_begin(rdcon, block, &pa_block);

    if (!self_hidden && block->bound) {
        // CSS 2.1 Section 17.6.1: empty-cells: hide suppresses borders/backgrounds
        bool skip_bound = false;
        if (block->view_type == RDT_VIEW_TABLE_CELL) {
            ViewTableCell* cell = lam::view_require_table_cell(block);
            if (cell->td && cell->td->hide_empty) {
                skip_bound = true;
                log_debug("Skipping bound for empty cell (empty-cells: hide)");
            }
        }

        if (!skip_bound) {
            auto tb1 = std::chrono::high_resolution_clock::now();
            render_bound(rdcon, block);
            auto tb2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_BOUND,
                std::chrono::duration<double, std::milli>(tb2 - tb1).count());
        }
    }

    // Render vector path if present (for PDF curves and complex paths)
    if (block->vpath && block->vpath->segments) {
        render_vector_path(rdcon, block);
    }

    // Propagate position with scale applied (CSS logical pixels -> physical surface pixels)
    float s = rdcon->scale;
    rdcon->block.x = pa_block.x + block->x * s;  rdcon->block.y = pa_block.y + block->y * s;
    if (DEBUG_RENDER) {  // debugging outline around the block margin border
        Rect rc;
        rc.x = rdcon->block.x - (block->bound ? block->bound->margin.left * s : 0);
        rc.y = rdcon->block.y - (block->bound ? block->bound->margin.top * s : 0);
        rc.width = block->width * s + (block->bound ? (block->bound->margin.left + block->bound->margin.right) * s : 0);
        rc.height = block->height * s + (block->bound ? (block->bound->margin.top + block->bound->margin.bottom) * s : 0);
        draw_debug_rect(rdcon, rc, &rdcon->block.clip);
    }

    View* view = block->first_child;
    auto rc_start = std::chrono::high_resolution_clock::now();
    if (view) {
        if (block->in_line && block->in_line->has_color) {
            log_debug("[RENDER COLOR] element=%s setting color: #%02x%02x%02x (was #%02x%02x%02x) color.c=0x%08x",
                      block->node_name(),
                      block->in_line->color.r, block->in_line->color.g, block->in_line->color.b,
                      rdcon->color.r, rdcon->color.g, rdcon->color.b,
                      block->in_line->color.c);
            rdcon->color = block->in_line->color;
        } else {
            log_debug("[RENDER COLOR] element=%s inheriting color #%02x%02x%02x (in_line=%p, color.c=%u)",
                      block->node_name(), rdcon->color.r, rdcon->color.g, rdcon->color.b,
                      block->in_line, block->in_line ? block->in_line->color.c : 0);
        }
        // setup clip box
        if (block->scroller) {
            setup_scroller(rdcon, block);
        }

        RenderClipScope overflow_clip_scope = render_clip_push_overflow_scope(rdcon);

        // render negative z-index children
        render_children(rdcon, view);
        // render positive z-index children (sorted by z-index)
        if (block->position) {
            log_debug("render absolute/fixed positioned children");
            // collect positioned children into array for z-index sorting
            ViewBlock* abs_children[256];
            int abs_count = 0;
            ViewBlock* child_block = block->position->first_abs_child;
            while (child_block && abs_count < 256) {
                abs_children[abs_count++] = child_block;
                child_block = child_block->position->next_abs_sibling;
            }
            // sort by z-index (stable: preserve document order for equal z-index)
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
                render_block_view(rdcon, abs_children[i]);
            }
        }

        if (overflow_clip_scope.active) {
            auto toc1 = std::chrono::high_resolution_clock::now();
            render_clip_pop_scope(rdcon, &overflow_clip_scope);
            auto toc2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_OVERFLOW_CLIP,
                std::chrono::duration<double, std::milli>(toc2 - toc1).count());
        }

        // Deferred outline pass: CSS spec says outlines paint after all
        // backgrounds/borders/shadows in the stacking context. Walk direct
        // children and render their outlines on top of everything.
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
    else {
        log_debug("view has no child");
    }
    auto rc_end = std::chrono::high_resolution_clock::now();
    children_time = std::chrono::duration<double, std::milli>(rc_end - rc_start).count();

    // render scrollbars
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }

    // Render multi-column rules between columns
    if (block->multicol && block->multicol->computed_column_count > 1) {
        render_column_rules(rdcon, block);
    }

    render_effect_group_finish(&effect_group, block, &rdcon->block.clip);

    if (css_clip_scope.active) {
        auto tc1 = std::chrono::high_resolution_clock::now();
        render_clip_pop_scope(rdcon, &css_clip_scope);
        auto tc2 = std::chrono::high_resolution_clock::now();
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_CLIP,
            std::chrono::duration<double, std::milli>(tc2 - tc1).count());
    }

    render_state_pop_transform(&transform_scope);

    rdcon->block = pa_block;  rdcon->font = pa_font;  rdcon->color = pa_color;
    render_element_marker_end(rdcon, &dl_element_scope);
    log_leave();

    auto rbv_end = std::chrono::high_resolution_clock::now();
    double this_total = std::chrono::duration<double, std::milli>(rbv_end - rbv_start).count();
    // Self-time = total minus children traversal time
    render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_BLOCK_SELF, this_total - children_time);
}

void render_embed_doc(RenderContext* rdcon, ViewBlock* block) {
    BlockBlot pa_block = rdcon->block;
    if (block->bound) { render_bound(rdcon, block); }

    float s = rdcon->scale;
    rdcon->block.x = pa_block.x + block->x * s;  rdcon->block.y = pa_block.y + block->y * s;

    // Constrain clip region to iframe content box (before scroller setup)
    // This ensures embedded documents (SVG, PDF, etc.) don't render outside iframe bounds
    float content_left = rdcon->block.x;
    float content_top = rdcon->block.y;
    float content_right = rdcon->block.x + block->width * s;
    float content_bottom = rdcon->block.y + block->height * s;

    // Adjust for borders if present
    if (block->bound && block->bound->border) {
        content_left += block->bound->border->width.left * s;
        content_top += block->bound->border->width.top * s;
        content_right -= block->bound->border->width.right * s;
        content_bottom -= block->bound->border->width.bottom * s;
    }

    // Intersect with parent clip region
    rdcon->block.clip.left = max(rdcon->block.clip.left, content_left);
    rdcon->block.clip.top = max(rdcon->block.clip.top, content_top);
    rdcon->block.clip.right = min(rdcon->block.clip.right, content_right);
    rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, content_bottom);

    log_debug("iframe clip set to: left:%.0f, top:%.0f, right:%.0f, bottom:%.0f (content box)",
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);

    // setup clip box for scrolling
    if (block->scroller) { setup_scroller(rdcon, block); }
    // render the embedded doc
    if (block->embed && block->embed->doc) {
        DomDocument* doc = block->embed->doc;
        // render html doc
        if (doc && doc->view_tree && doc->view_tree->root) {
            View* root_view = doc->view_tree->root;
            if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
                log_debug("render doc root view:");
                // Save parent context and reset for embedded document
                FontBox pa_font = rdcon->font;
                Color pa_color = rdcon->color;

                // Reset color to black for embedded document (don't inherit from parent doc)
                // Each document should start with default black text color
                rdcon->color.c = 0xFF000000;  // opaque black (ABGR)

                // load default font
                FontProp* default_font = doc->view_tree->html_version == HTML5 ? &rdcon->ui_context->default_font : &rdcon->ui_context->legacy_default_font;
                log_debug("render_init default font: %s, html version: %d", default_font->family, doc->view_tree->html_version);
                setup_font(rdcon->ui_context, &rdcon->font, default_font);

                ViewBlock* root_block = lam::view_require_block(root_view);

                // Per CSS 2.1 §14.2: the iframe's viewport is its own canvas.
                // Propagate the body background (or html background) to fill the
                // iframe content box, otherwise the body only paints its own
                // intrinsic-sized box (often smaller than the iframe viewport,
                // leaving white gaps below the body content).
                if (root_block->tag_id != HTM_TAG_SVG &&
                    !(root_block->embed && root_block->embed->img)) {
                    Color canvas_bg;
                    canvas_bg.c = 0;
                    bool html_has_bg = root_block->bound && root_block->bound->background &&
                                       root_block->bound->background->color.a > 0;
                    if (html_has_bg) {
                        canvas_bg = root_block->bound->background->color;
                    } else {
                        // walk html children for body bg
                        View* c = root_block->first_child;
                        while (c) {
                            if (c->view_type == RDT_VIEW_BLOCK) {
                                ViewBlock* cb = lam::view_require_block(c);
                                const char* nm = cb->node_name();
                                if (nm && str_ieq_const(nm, strlen(nm), "body")) {
                                    if (cb->bound && cb->bound->background &&
                                        cb->bound->background->color.a > 0) {
                                        canvas_bg = cb->bound->background->color;
                                    }
                                    break;
                                }
                            }
                            c = static_cast<View*>(c->next_sibling);
                        }
                    }
                    if (canvas_bg.a > 0) {
                        // Fill iframe content box (already computed above as content_left/top/right/bottom).
                        rc_fill_rect(rdcon,
                                     content_left, content_top,
                                     content_right - content_left,
                                     content_bottom - content_top,
                                     canvas_bg);
                    }
                }

                // Check if root element is SVG - if so, render directly without background
                if (root_block->tag_id == HTM_TAG_SVG) {
                    log_debug("render embedded SVG document (no background)");
                    render_inline_svg(rdcon, root_block);
                } else if (root_block->embed && root_block->embed->img) {
                    // Image/SVG document root — use render_image_view
                    render_image_view(rdcon, root_block);
                } else {
                    // Regular HTML document - render with background
                    render_block_view(rdcon, root_block);
                }

                rdcon->font = pa_font;
                rdcon->color = pa_color;
            }
            else {
                log_debug("Invalid root view");
            }
        }
    }

    // Render scrollbar for the iframe scroll container
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }
    rdcon->block = pa_block;
}

void render_inline_view(RenderContext* rdcon, ViewSpan* view_span) {
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_INLINE);
    FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    log_debug("render inline view");

    bool self_hidden = view_span->in_line && view_span->in_line->visibility == VIS_HIDDEN;

    // Render border/outline for inline elements.
    // Background is rendered per-line-fragment in render_text_view so that
    // wrapping inline elements (e.g. <code> spanning two lines) don't fill
    // the entire bounding-box rectangle with background color.
    if (!self_hidden && view_span->bound) {
        BackgroundProp* saved_bg = view_span->bound->background;
        view_span->bound->background = nullptr;
        render_bound(rdcon, lam::unsafe_view_block_api_span(view_span));
        view_span->bound->background = saved_bg;
    }

    View* view = view_span->first_child;
    if (view) {
        if (view_span->font) {
            setup_font(rdcon->ui_context, &rdcon->font, view_span->font);
        }
        if (view_span->in_line && view_span->in_line->has_color) {
            log_debug("[RENDER COLOR INLINE] element=%s setting color: #%02x%02x%02x (was #%02x%02x%02x) color.c=0x%08x",
                      view_span->node_name(),
                      view_span->in_line->color.r, view_span->in_line->color.g, view_span->in_line->color.b,
                      pa_color.r, pa_color.g, pa_color.b,
                      view_span->in_line->color.c);
            rdcon->color = view_span->in_line->color;
        } else {
            log_debug("[RENDER COLOR INLINE] element=%s inheriting color #%02x%02x%02x (in_line=%p, color.c=%u)",
                      view_span->node_name(), pa_color.r, pa_color.g, pa_color.b,
                      view_span->in_line, view_span->in_line ? view_span->in_line->color.c : 0);
        }
        render_children(rdcon, view);
    }
    else {
        log_debug("view has no child");
    }
    rdcon->font = pa_font;  rdcon->color = pa_color;
}

void render_children(RenderContext* rdcon, View* view) {
    auto trc_start = std::chrono::high_resolution_clock::now();
    do {
        render_profiler_increment(rdcon->profiler, RENDER_PROFILE_DISPATCH);
        if (view->view_type == RDT_VIEW_BLOCK || view->view_type == RDT_VIEW_INLINE_BLOCK ||
            view->view_type == RDT_VIEW_TABLE || view->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
            view->view_type == RDT_VIEW_TABLE_ROW || view->view_type == RDT_VIEW_TABLE_CELL) {
            ViewBlock* block = lam::view_require_block(view);
            log_debug("[RENDER DISPATCH] view_type=%d, embed=%p, img=%p, width=%.0f, height=%.0f",
                      view->view_type, block->embed,
                      block->embed ? block->embed->img : NULL, block->width, block->height);
            if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form) {
                // Form control rendering (input, select, textarea, button)
                // For <button> elements with children, render default button background BEFORE
                // children so the gray fill doesn't cover the text content.
                if (block->form->control_type == FORM_CONTROL_BUTTON && block->first_child) {
                    render_form_control(rdcon, block);  // draw button chrome first
                    render_block_view(rdcon, block);    // then children (text) on top
                } else {
                    // Other form controls: render block first, then form decorations on top
                    log_debug("[RENDER DISPATCH] calling render_block_view for form control");
                    render_block_view(rdcon, block);
                    log_debug("[RENDER DISPATCH] calling render_form_control");
                    render_form_control(rdcon, block);
                }
            }
            else if (block->tag_id == HTM_TAG_SVG) {
                // Inline SVG element - paint CSS background/border first, then SVG content
                if (block->bound) { render_bound(rdcon, block); }
                log_debug("[RENDER DISPATCH] calling render_inline_svg for inline SVG");
                auto ts1 = std::chrono::high_resolution_clock::now();
                render_inline_svg(rdcon, block);
                auto ts2 = std::chrono::high_resolution_clock::now();
                render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_SVG,
                    std::chrono::duration<double, std::milli>(ts2 - ts1).count());
            }
            else if (block->embed && block->embed->img) {
                log_debug("[RENDER DISPATCH] calling render_image_view");
                auto ti1 = std::chrono::high_resolution_clock::now();
                render_image_view(rdcon, block);
                auto ti2 = std::chrono::high_resolution_clock::now();
                render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_IMAGE,
                    std::chrono::duration<double, std::milli>(ti2 - ti1).count());
            }
            else if (block->embed && block->embed->video) {
                log_debug("[RENDER DISPATCH] calling render_video_content for <video>");
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
                log_debug("[RENDER DISPATCH] calling render_webview_layer_content");
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
                // Skip only absolute/fixed positioned elements - they are rendered separately
                // Floats (which also have position struct) should be rendered in normal flow
                if (block->position &&
                    (block->position->position == CSS_VALUE_ABSOLUTE ||
                     block->position->position == CSS_VALUE_FIXED)) {
                    log_debug("absolute/fixed positioned block, skip in normal rendering");
                } else {
                    render_block_view(rdcon, block);
                }
            }
        }
        else if (view->view_type == RDT_VIEW_LIST_ITEM) {
            render_litem_view(rdcon, lam::view_require_block(view));
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require_element(view);
            auto tiv1 = std::chrono::high_resolution_clock::now();
            render_inline_view(rdcon, span);
            auto tiv2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_INLINE,
                std::chrono::duration<double, std::milli>(tiv2 - tiv1).count());
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            auto tt1 = std::chrono::high_resolution_clock::now();
            render_text_view(rdcon, text);
            auto tt2 = std::chrono::high_resolution_clock::now();
            render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_TEXT,
                std::chrono::duration<double, std::milli>(tt2 - tt1).count());
        }
        else if (view->view_type == RDT_VIEW_MARKER) {
            // List marker (bullet/number) with fixed width and vector graphics
            ViewSpan* marker = lam::view_require_element(view);
            render_marker_view(rdcon, marker);
        }
        else {
            log_debug("unknown view in rendering: %d", view->view_type);
        }
        view = view->next();
    } while (view);
    auto trc_end = std::chrono::high_resolution_clock::now();
    render_profiler_add_time(rdcon->profiler, RENDER_PROFILE_CHILDREN,
        std::chrono::duration<double, std::milli>(trc_end - trc_start).count());
}

void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    render_output_render_html_doc(uicon, view_tree, output_file);
}

void render_html_doc_tiled(UiContext* uicon, ViewTree* view_tree,
                           const char* output_file,
                           int total_width, int total_height) {
    render_output_render_tiled_png(uicon, view_tree, output_file, total_width, total_height);
}
