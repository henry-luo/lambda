#include "render.hpp"
#include "context_menu.hpp"
#include "dom_range.hpp"
#include "dom_range_resolver.hpp"
#include "editing_geometry.hpp"
#include "form_control.hpp"
#include "render.hpp"
#include "source_pos_bridge.hpp"
#include "state_store.hpp"
#include "text_control.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include <math.h>
#include <string.h>

typedef struct SelectionPaintCtx {
    RenderContext* rdcon;
    Color          color;
    float          scale;
    float          iframe_offset_x;
    float          iframe_offset_y;
} SelectionPaintCtx;

static void render_focus_outline(RenderContext* rdcon, DocState* state) {
    View* focused = focus_get_visible(state);
    if (!focused) return;
    if (focused->view_type != RDT_VIEW_BLOCK) return;

    ViewBlock* block = lam::view_require_block(focused);
    float s = rdcon->scale;

    float x = block->x;
    float y = block->y;
    float width = block->width;
    float height = block->height;

    View* parent = block->parent;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* parent_block = lam::view_require_block(parent);
            x += parent_block->x;
            y += parent_block->y;
        }
        parent = parent->parent;
    }

    x *= s;  y *= s;
    width *= s;  height *= s;

    float outline_offset = 2.0f * s;
    float outline_width = 2.0f * s;
    float ox = x - outline_offset;
    float oy = y - outline_offset;
    float ow = width + outline_offset * 2;
    float oh = height + outline_offset * 2;

    RdtPath* path = rdt_path_new();
    rdt_path_add_rect(path, ox, oy, ow, oh, 0, 0);
    float dash_pattern[] = {4.0f * s, 2.0f * s};
    Color focus_color = {0};
    focus_color.r = 0x00; focus_color.g = 0x5F; focus_color.b = 0xCC; focus_color.a = 0xFF;
    rc_stroke_path(rdcon, path, focus_color, outline_width, RDT_CAP_BUTT, RDT_JOIN_MITER,
        dash_pattern, 2, nullptr);
    rdt_path_free(path);
    log_debug("[FOCUS] Rendered focus outline at (%.0f,%.0f) size %.0fx%.0f", ox, oy, ow, oh);
}

static void render_caret(RenderContext* rdcon, DocState* state) {
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
        if (elem->item_prop_type == DomElement::ITEM_PROP_FORM &&
            elem->form &&
            (elem->form->control_type == FORM_CONTROL_TEXT ||
             elem->form->control_type == FORM_CONTROL_TEXTAREA)) {
            return;
        }
    }

    float s = rdcon->scale;
    float x = caret_x;
    float y = caret_y;

    View* parent = view->parent;
    bool applied_root_scroll = false;
    DomDocument* doc = rdcon && rdcon->ui_context ? rdcon->ui_context->document : nullptr;
    ViewBlock* root_block = nullptr;
    if (doc && doc->view_tree && doc->view_tree->root && doc->view_tree->root->view_type == RDT_VIEW_BLOCK) {
        root_block = lam::view_require_block(doc->view_tree->root);
    }
    float root_scroll_x = 0.0f;
    float root_scroll_y = 0.0f;
    if (root_block && root_block->scroller && root_block->scroller->pane) {
        DocState* root_state = root_block->doc ? root_block->doc->state : NULL;
        scroll_state_get_position_for_view(root_state, static_cast<View*>(root_block), root_block->scroller->pane,
                                           &root_scroll_x, &root_scroll_y, NULL, NULL);
    }
    bool offset_covers_root_scroll_x = fabsf(iframe_offset_x + root_scroll_x) < 0.5f;
    bool offset_covers_root_scroll_y = fabsf(iframe_offset_y + root_scroll_y) < 0.5f;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK ||
            parent->view_type == RDT_VIEW_INLINE_BLOCK ||
            parent->view_type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* block = lam::view_require_block(parent);
            x += block->x;
            y += block->y;
            if (block->scroller && block->scroller->pane) {
                DocState* block_state = block->doc ? block->doc->state : NULL;
                float scroll_x = 0.0f, scroll_y = 0.0f;
                scroll_state_get_position_for_view(block_state, static_cast<View*>(block), block->scroller->pane,
                                                   &scroll_x, &scroll_y, NULL, NULL);
                if (!(block == root_block && offset_covers_root_scroll_x)) {
                    x -= scroll_x;
                }
                if (!(block == root_block && offset_covers_root_scroll_y)) {
                    y -= scroll_y;
                }
                if (block == root_block) applied_root_scroll = true;
            }
        }
        parent = parent->parent;
    }

    if (!applied_root_scroll && root_block && root_block->scroller && root_block->scroller->pane) {
        if (!offset_covers_root_scroll_x) {
            x -= root_scroll_x;
        }
        if (!offset_covers_root_scroll_y) {
            y -= root_scroll_y;
        }
    }

    x += iframe_offset_x;
    y += iframe_offset_y;

    float css_x = x;
    float css_y = y;
    caret_project_previous_visual_rect(state, css_x, css_y, caret_height);

    x *= s;  y *= s;
    float height = caret_height * s;
    float caret_width = 3.0f * s;

    log_debug("[CARET] Before render: CSS pos (%.1f,%.1f), physical pos (%.1f,%.1f) height=%.1f",
        css_x, css_y, x, y, height);

    Color caret_color = {0};
    caret_color.r = 0x66; caret_color.g = 0x66; caret_color.b = 0x66; caret_color.a = 0xCC;
    rc_fill_rect(rdcon, x, y, caret_width, height, caret_color);
    log_debug("[CARET] Drew caret at (%.0f,%.0f) size %.0fx%.0f", x, y, caret_width, height);
    log_debug("[CARET] Rendered caret at (%.0f,%.0f) height=%.0f", x, y, height);
}

static void selection_paint_rect_cb(float x, float y, float w, float h, void* ud) {
    SelectionPaintCtx* ctx = (SelectionPaintCtx*)ud;
    if (w <= 0 || h <= 0) return;
    float s = ctx->scale;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    DomDocument* doc = ctx->rdcon && ctx->rdcon->ui_context ? ctx->rdcon->ui_context->document : nullptr;
    if (doc && doc->view_tree && doc->view_tree->root && doc->view_tree->root->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* root = lam::view_require_block(doc->view_tree->root);
        if (root->scroller && root->scroller->pane) {
            DocState* root_state = root->doc ? root->doc->state : NULL;
            scroll_state_get_position_for_view(root_state, static_cast<View*>(root), root->scroller->pane,
                                               &scroll_x, &scroll_y, NULL, NULL);
        }
    }
    bool offset_covers_scroll_x = fabsf(ctx->iframe_offset_x + scroll_x) < 0.5f;
    bool offset_covers_scroll_y = fabsf(ctx->iframe_offset_y + scroll_y) < 0.5f;
    float paint_scroll_x = offset_covers_scroll_x ? 0.0f : scroll_x;
    float paint_scroll_y = offset_covers_scroll_y ? 0.0f : scroll_y;
    float px = (x + ctx->iframe_offset_x - paint_scroll_x) * s;
    float py = (y + ctx->iframe_offset_y - paint_scroll_y) * s;
    float pw = w * s;
    float ph = h * s;
    rc_fill_rect(ctx->rdcon, px, py, pw, ph, ctx->color);
}

static bool render_text_control_selection(RenderContext* rdcon, DomRange* range,
                                          SelectionPaintCtx* paint) {
    if (!rdcon || !range || !paint) return false;
    if (!range->start.node || range->start.node != range->end.node) return false;
    if (!range->start.node->is_element()) return false;

    DomElement* elem = lam::dom_require_element(range->start.node);
    if (!tc_is_text_control(elem)) return false;
    if (range->start.offset == range->end.offset) return true;

    // Form controls paint their own selection inside render_form_control().
    // Painting it again as a document overlay offsets the highlight a second
    // time in embedded/scrolled documents.
    return true;
}

static bool render_dom_node_is_in_current_tree(DomNode* root, DomNode* node) {
    if (!root || !node) return false;
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur == root) return true;
    }
    return false;
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

static DomRange* selection_paint_range_for_current_tree(RenderContext* rdcon,
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
    log_debug("[SELECTION PAINT REBIND] rebound stale paint range endpoints start=%p end=%p",
        (void*)rebound_start.node, (void*)rebound_end.node);
    return scratch;
}

static void render_selection(RenderContext* rdcon, DocState* state) {
    if (!state) return;

    DomSelection* ds = state->dom_selection;
    bool use_dom = ds && ds->range_count > 0 && !dom_selection_is_collapsed(ds);
    if (!use_dom) {
        return;
    }

    DomRange* r = ds->ranges[0];
    if (!r) return;

    SelectionPaintCtx ctx;
    ctx.rdcon = rdcon;
    ctx.scale = rdcon->scale;
    selection_get_iframe_offset(state, &ctx.iframe_offset_x, &ctx.iframe_offset_y);
    ctx.color.r = 0x00; ctx.color.g = 0x78; ctx.color.b = 0xD7; ctx.color.a = 0x80;

    DomRange paint_range;
    r = selection_paint_range_for_current_tree(rdcon, r, &paint_range);
    if (render_text_control_selection(rdcon, r, &ctx)) {
        log_debug("[SELECTION] Rendered text-control DomSelection range via editing geometry");
        return;
    }

    if (!dom_range_resolve_layout(r)) {
        log_debug("[SELECTION] dom_range_resolve_layout failed");
        return;
    }

    dom_range_for_each_rect(r, rdcon->ui_context, selection_paint_rect_cb, &ctx);
    log_debug("[SELECTION] Rendered DomSelection range via dom_range_for_each_rect");
}

void render_ui_overlays(RenderContext* rdcon, DocState* state) {
    if (!state) {
        log_debug("[UI_OVERLAY] No state");
        return;
    }

    log_debug("[UI_OVERLAY] Rendering overlays: caret=%s",
        caret_has_projection(state) ? "present" : "none");

    render_selection(rdcon, state);

    if (state->open_dropdown) {
        ViewBlock* select = lam::view_require_block(state->open_dropdown);
        render_select_dropdown(rdcon, select, state);
    }

    context_menu_render(rdcon, state);

    if (state->drag_drop && state->drag_drop->active) {
        DragDropState* dd = state->drag_drop;
        float s = rdcon->scale;

        if (dd->drop_target && dd->drop_target->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* dt = lam::view_require_block(dd->drop_target);
            float dx = dt->x, dy = dt->y;
            View* par = dt->parent;
            while (par) {
                if (par->view_type == RDT_VIEW_BLOCK) {
                    ViewBlock* parent_block = lam::view_require_block(par);
                    dx += parent_block->x;
                    dy += parent_block->y;
                }
                par = par->parent;
            }
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
            log_debug("[DRAG] Drop target highlight at (%.0f,%.0f) size %.0fx%.0f", dx, dy, dw, dh);
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
    render_focus_outline(rdcon, state);
}
