#include "event.hpp"
#include "layout.hpp"
#include "view.hpp"
#include "render.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include <math.h>
#include <string.h>

typedef struct SelectionPaintCtx {
    RasterRenderContext* rdcon;
    Color          color;
    float          scale;
    float          iframe_offset_x;
    float          iframe_offset_y;
} SelectionPaintCtx;

static void render_caret_in_scroll_clips(RasterRenderContext* rdcon, View* ancestor,
        RdtLogicalPoint document_offset, Rect rect, Color color, const RdtMatrix* transform) {
    if (!ancestor) {
        render_painter_fill_transformed_rect(rdcon, &rect, color.c, transform);
        return;
    }
    RenderClipScope clip = render_clip_push_viewport_scope(
        rdcon, lam::view_as_block(ancestor), document_offset);
    render_caret_in_scroll_clips(rdcon, ancestor->parent_view(), document_offset, rect, color, transform);
    render_clip_pop_scope(rdcon, &clip);
}

static void render_caret(RasterRenderContext* rdcon, DocState* state) {
    View* view = NULL;
    int caret_offset = 0;
    float caret_x = 0, caret_y = 0, caret_height = 0;
    float iframe_offset_x = 0, iframe_offset_y = 0;
    bool caret_visible = false;
    if (!caret_get_render_snapshot(state, &view, &caret_offset, &caret_x, &caret_y,
            &caret_height, &iframe_offset_x, &iframe_offset_y, &caret_visible) ||
        !caret_visible) {
        return;
    }

    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(lam::view_dom_node(view));
        if (elem->form_control() && form_control_is_text_editable(elem->form)) {
            return;
        }
    }

    float s = rdcon->raster_scale;
    RdtMatrix frame;
    if (!view_get_client_matrix(view, &frame)) return;
    RdtMatrix embedding = rdt_matrix_translate(iframe_offset_x, iframe_offset_y);
    frame = rdt_matrix_multiply(&embedding, &frame);
    // caret snapshots use containing-block coordinates; the client matrix starts at the view's origin.
    Rect rect = {caret_x - view->x, caret_y - view->y, 3.0f, caret_height};
    Bound bounds;
    if (!rdt_matrix_project_rect_bounds(&frame, rect.x, rect.y,
            rect.x + rect.width, rect.y + rect.height,
            &bounds.left, &bounds.top, &bounds.right, &bounds.bottom)) return;
    caret_project_previous_visual_rect(state, bounds.left, bounds.top, bounds.bottom - bounds.top);
    RdtMatrix raster_scale = {s, 0, 0, 0, s, 0, 0, 0, 1};
    RdtMatrix transform = rdt_matrix_multiply(&raster_scale, &frame);

    DomNode* caret_node = lam::view_dom_node(view);
    if (caret_node && !caret_node->is_element()) caret_node = caret_node->parent;
    ViewSpan* caret_element = caret_node && caret_node->is_element()
        ? static_cast<ViewSpan*>(caret_node->as_element()) : nullptr;
    Color caret_color = radiant_caret_color_for_view(caret_element);
    render_caret_in_scroll_clips(rdcon, view->parent_view(),
        {iframe_offset_x, iframe_offset_y}, rect, caret_color, &transform);
}

static RdtLogicalPoint selection_paint_to_canvas(const SelectionPaintCtx* ctx,
                                                 RdtLogicalPoint point) {
    if (!ctx) return point;
    // range rects and the embedding frame origin are both viewport-relative.
    point.x += ctx->iframe_offset_x;
    point.y += ctx->iframe_offset_y;
    return point;
}

static void selection_paint_rect_cb(float x, float y, float w, float h, void* ud) {
    SelectionPaintCtx* ctx = (SelectionPaintCtx*)ud;
    if (w <= 0 || h <= 0) return;
    float s = ctx->scale;
    RdtLogicalPoint point = selection_paint_to_canvas(ctx, {x, y});
    float px = point.x * s;
    float py = point.y * s;
    float pw = w * s;
    float ph = h * s;
    rc_fill_rect(ctx->rdcon, px, py, pw, ph, ctx->color);
}

static bool render_text_control_selection(RasterRenderContext* rdcon, DomRange* range) {
    if (!rdcon || !range) return false;
    if (!range->start.node || range->start.node != range->end.node) return false;
    if (!range->start.node->is_element()) return false;

    DomElement* elem = lam::dom_require_element(range->start.node);
    if (!tc_is_text_control(elem)) return false;
    if (range->start.offset == range->end.offset) return true;

    // Form controls paint their own selection inside render_form_control().
    // Painting it again in the text pass offsets the highlight a second time
    // in embedded/scrolled documents.
    return true;
}

static bool render_dom_node_is_in_current_tree(DomNode* root, DomNode* node) {
    if (!root || !node) return false;
    return view_geometry_dom_is_descendant(node, root);
}

static bool rebind_paint_boundary_to_current_tree(DomNode* root,
                                                  const DomBoundary* boundary,
                                                  DomBoundary* out) {
    if (!root || !boundary || !boundary->node || !out) return false;
    if (render_dom_node_is_in_current_tree(root, boundary->node)) {
        *out = *boundary;
        return false;
    }

    SourcePosC source_pos;
    if (!source_pos_from_dom_boundary(boundary, &source_pos)) return false;
    DomBoundary rebound = {NULL, 0};
    bool ok = dom_boundary_from_source_pos(root, &source_pos, &rebound);
    source_pos_free(&source_pos);
    if (!ok || !rebound.node) return false;
    *out = rebound;
    return true;
}

static DomRange* selection_paint_range_for_current_tree(RasterRenderContext* rdcon,
                                                        DomRange* range,
                                                        DomRange* scratch) {
    if (!rdcon || !rdcon->ui_context || !rdcon->ui_context->document ||
        !rdcon->ui_context->document->view_tree ||
        !rdcon->ui_context->document->view_tree->root || !range || !scratch) {
        return range;
    }

    DomNode* root = lam::view_dom_node(rdcon->ui_context->document->view_tree->root);
    DomBoundary rebound_start = range->start;
    DomBoundary rebound_end = range->end;
    bool rebound_any = false;
    rebound_any = rebind_paint_boundary_to_current_tree(root, &range->start, &rebound_start) || rebound_any;
    rebound_any = rebind_paint_boundary_to_current_tree(root, &range->end, &rebound_end) || rebound_any;
    if (!rebound_any) return range;

    *scratch = *range;
    scratch->start = rebound_start;
    scratch->end = rebound_end;
    scratch->layout_valid = false;
    scratch->start_view = NULL;
    scratch->end_view = NULL;
    return scratch;
}

static DomRange* render_active_selection_range(RasterRenderContext* rdcon,
                                                DomRange* scratch) {
    if (!rdcon || !rdcon->ui_context || !rdcon->ui_context->document)
        return nullptr;
    DocState* state = rdcon->ui_context->document->state;
    DomSelection* selection = state ? state->dom_selection : nullptr;
    if (!selection || selection->range_count == 0 ||
        dom_selection_is_collapsed(selection) || !selection->ranges[0]) return nullptr;
    return selection_paint_range_for_current_tree(
        rdcon, selection->ranges[0], scratch);
}

bool render_text_selection_span(RasterRenderContext* rdcon, ViewText* text_view,
                                int* start_byte, int* end_byte) {
    if (!text_view || !start_byte || !end_byte) return false;
    DomRange scratch;
    DomRange* range = render_active_selection_range(rdcon, &scratch);
    if (!range || render_text_control_selection(rdcon, range)) return false;
    return dom_range_text_byte_span(range,
        lam::dom_require_text(text_view), start_byte, end_byte);
}

void render_text_selection_rect(RasterRenderContext* rdcon, ViewText* text_view,
                                TextRect* text_rect) {
    if (!rdcon || !text_view || !text_rect || !rdcon->ui_context ||
        !rdcon->ui_context->document) return;

    DocState* state = rdcon->ui_context->document->state;
    if (!state) return;

    DomRange paint_range;
    DomRange* r = render_active_selection_range(rdcon, &paint_range);
    if (!r) return;

    SelectionPaintCtx ctx;
    ctx.rdcon = rdcon;
    ctx.scale = rdcon->raster_scale;
    selection_get_iframe_offset(state, &ctx.iframe_offset_x, &ctx.iframe_offset_y);
    ctx.color.r = 0x00; ctx.color.g = 0x78; ctx.color.b = 0xD7; ctx.color.a = 0x80;
    DomElement* paint_parent = text_view->parent && text_view->parent->is_element()
        ? text_view->parent->as_element() : nullptr;
    if (paint_parent && paint_parent->in_line &&
        paint_parent->inl()->has_selection_background_color) {
        ctx.color = paint_parent->inl()->selection_background_color;
    }

    if (render_text_control_selection(rdcon, r)) {
        return;
    }

    if (!dom_range_resolve_layout(r)) {
        return;
    }

    // Paint with this text fragment so later stacking contexts cover its
    // highlight, and the fragment inherits its normal overflow clips.
    dom_range_for_each_rect_in_text_rect(r, lam::dom_require_text(text_view),
        text_rect, rdcon->ui_context, selection_paint_rect_cb, &ctx);
}

void render_ui_overlays(RasterRenderContext* rdcon, DocState* state) {
    if (!state) {
        return;
    }

    if (state->open_dropdown) {
        ViewBlock* select = lam::view_require_block(state->open_dropdown);
        render_select_dropdown(rdcon, select, state);
    }

    context_menu_render(rdcon, state);

    if (state->drag_drop && state->drag_drop->active) {
        DragDropState* dd = state->drag_drop;
        float s = rdcon->raster_scale;

        if (dd->drop_target && dd->drop_target->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* dt = lam::view_require_block(dd->drop_target);
            RdtLogicalPoint origin = view_geometry_local_to_block_document(
                static_cast<View*>(dt), {dt->x, dt->y});
            float dx = origin.x;
            float dy = origin.y;
            dx *= s;  dy *= s;
            float dw = dt->width * s;
            float dh = dt->height * s;

            Color drop_stroke_color = {0};
            drop_stroke_color.r = 0x33; drop_stroke_color.g = 0x99; drop_stroke_color.b = 0xFF;
            drop_stroke_color.a = 0xC0;
            RdtPath* drop_path = rdt_path_new();
            rdt_path_add_rect(drop_path, dx - 2*s, dy - 2*s, dw + 4*s, dh + 4*s, 0, 0);
            rc_stroke_path(rdcon, drop_path, drop_stroke_color, 2.0f * s, RDT_CAP_BUTT,
                RDT_JOIN_MITER, nullptr, 0, nullptr);
            rdt_path_free(drop_path);

            Color drop_fill_color = {0};
            drop_fill_color.r = 0x33; drop_fill_color.g = 0x99; drop_fill_color.b = 0xFF;
            drop_fill_color.a = 0x20;
            rc_fill_rect(rdcon, dx, dy, dw, dh, drop_fill_color);
        }

        float cx = dd->current_x;
        float cy = dd->current_y;
        Color ind_fill = {0};
        ind_fill.r = 0x33; ind_fill.g = 0x99; ind_fill.b = 0xFF; ind_fill.a = 0x80;
        rc_fill_rounded_rect(rdcon, cx - 4*s, cy - 4*s, 8*s, 8*s, 2*s, 2*s, ind_fill);
        Color ind_stroke = {0};
        ind_stroke.r = 0x33; ind_stroke.g = 0x99; ind_stroke.b = 0xFF; ind_stroke.a = 0xFF;
        RdtPath* ind_path = rdt_path_new();
        rdt_path_add_rect(ind_path, cx - 4*s, cy - 4*s, 8*s, 8*s, 2*s, 2*s);
        rc_stroke_path(rdcon, ind_path, ind_stroke, 1.0f * s, RDT_CAP_BUTT, RDT_JOIN_MITER,
            nullptr, 0, nullptr);
        rdt_path_free(ind_path);
    }

    render_caret(rdcon, state);
}
