#include "event.hpp"
#include "render.hpp"
#include "layout.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include "../lambda/core/well_known_markup_names.h"
#include "../lambda/dom/dom_observers.h"

struct ScrollConfig {
    float SCROLLBAR_SIZE;
    float MIN_HANDLE_SIZE;
    float HANDLE_RADIUS;
    float SCROLL_BORDER_MAIN;
    float SCROLL_BORDER_CROSS;
    float BAR_COLOR = 0xF6;
    float HANDLE_COLOR = 0xC0;
};
ScrollConfig sc;

void scroll_config_init(void) {
    sc.SCROLLBAR_SIZE = 12.0f;
    sc.MIN_HANDLE_SIZE = 16.0f;
    sc.HANDLE_RADIUS = 4.0f;
    sc.SCROLL_BORDER_MAIN = 1.0f;
    sc.SCROLL_BORDER_CROSS = 2.0f;
}

struct ScrollHandleGeometry {
    float v_y;
    float v_height;
    float h_x;
    float h_width;
};

static ScrollHandleGeometry scrollpane_handle_geometry(
        ScrollPane* sp, DocState* state, View* view,
        float view_width, float view_height,
        float content_width, float content_height) {
    ScrollHandleGeometry geometry = {};
    if (!sp) return geometry;

    float h_scroll = 0.0f, v_scroll = 0.0f;
    float h_max = 0.0f, v_max = 0.0f;
    scroll_state_get_position_for_view(state, view, sp, &h_scroll, &v_scroll,
                                       &h_max, &v_max);
    if (content_height > 0.0f) {
        float bar_height = max(view_height - sc.SCROLLBAR_SIZE -
            sc.SCROLL_BORDER_MAIN * 2.0f, 0.0f);
        float ratio = min(view_height / content_height, 1.0f);
        geometry.v_height = min(max(sc.MIN_HANDLE_SIZE, ratio * bar_height),
                                bar_height);
        float range = v_max - sp->v_min_scroll;
        float scroll_ratio = range > 0.0f
            ? (v_scroll - sp->v_min_scroll) / range : 0.0f;
        geometry.v_y = sc.SCROLL_BORDER_MAIN + scroll_ratio *
            (bar_height - geometry.v_height);
    }
    if (content_width > 0.0f) {
        float bar_width = max(view_width - sc.SCROLLBAR_SIZE -
            sc.SCROLL_BORDER_MAIN * 2.0f, 0.0f);
        float ratio = min(view_width / content_width, 1.0f);
        geometry.h_width = min(max(sc.MIN_HANDLE_SIZE, ratio * bar_width),
                               bar_width);
        float range = h_max - sp->h_min_scroll;
        float scroll_ratio = range > 0.0f
            ? (h_scroll - sp->h_min_scroll) / range : 0.0f;
        geometry.h_x = sc.SCROLL_BORDER_MAIN + scroll_ratio *
            (bar_width - geometry.h_width);
    }
    return geometry;
}


void ScrollPane::reset() {
    DocState* state = state_ref;
    memset(this, 0, sizeof(ScrollPane));
    state_ref = state;
}

void scrollpane_render(RasterRenderContext* rdcon, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip, float scale,
    DocState* state, View* view,
    bool show_hz_scroll, bool show_vt_scroll) {
    log_info("SCROLLPANE: content size: %.1f x %.1f, view bounds: %.1f x %.1f",
        content_width, content_height, block_bound->width, block_bound->height);
    log_debug("render scroller content size: %f x %f, blk bounds: %f x %f",
        content_width, content_height, block_bound->width, block_bound->height);

    float view_x = block_bound->x, view_y = block_bound->y;
    float view_width = block_bound->width, view_height = block_bound->height;
    float scrollbar_size = sc.SCROLLBAR_SIZE * scale;
    float handle_radius = sc.HANDLE_RADIUS * scale;
    float border_cross = sc.SCROLL_BORDER_CROSS * scale;
    ScrollHandleGeometry geometry = scrollpane_handle_geometry(
        sp, state, view, view_width / scale, view_height / scale,
        content_width / scale, content_height / scale);

    // clip to visible bounds
    RdtPath* clip_path = rdt_path_new();
    rdt_path_add_rect(clip_path, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    rc_push_clip(rdcon, clip_path, nullptr);
    rdt_path_free(clip_path);

    // vertical scrollbar — only render if vertical overflow exists
    if (show_vt_scroll) {
        Color bar_color = {0}; bar_color.r = (uint8_t)sc.BAR_COLOR; bar_color.g = (uint8_t)sc.BAR_COLOR; bar_color.b = (uint8_t)sc.BAR_COLOR; bar_color.a = 255;
        rc_fill_rect(rdcon, view_x + view_width - scrollbar_size,
            view_y, scrollbar_size, view_height, bar_color);
        log_debug("v_scrollbar rect: x %f, y %f, wd %f, hg %f",
            view_x + view_width - scrollbar_size, view_y, scrollbar_size, view_height);

        if (content_height > 0) {
            Color handle_color = {0}; handle_color.r = (uint8_t)sc.HANDLE_COLOR; handle_color.g = (uint8_t)sc.HANDLE_COLOR; handle_color.b = (uint8_t)sc.HANDLE_COLOR; handle_color.a = 255;
            float v_handle_height = geometry.v_height * scale;
            float v_handle_y = geometry.v_y * scale;
            float v_scroll_x = view_x + view_width - scrollbar_size + border_cross;
            rc_fill_rounded_rect(rdcon, v_scroll_x, view_y + v_handle_y,
                scrollbar_size - border_cross * 2.0f, v_handle_height,
                handle_radius, handle_radius, handle_color);
            log_debug("v_scroll_handle rect: x %f, y %f, wd %f, hg %f",
                v_scroll_x, view_y + v_handle_y,
                scrollbar_size - border_cross * 2.0f, v_handle_height);
        }
    }

    // horizontal scrollbar — only render if horizontal overflow exists
    if (show_hz_scroll) {
        Color bar_color = {0}; bar_color.r = (uint8_t)sc.BAR_COLOR; bar_color.g = (uint8_t)sc.BAR_COLOR; bar_color.b = (uint8_t)sc.BAR_COLOR; bar_color.a = 255;
        rc_fill_rect(rdcon, view_x,
            view_y + view_height - scrollbar_size, view_width, scrollbar_size, bar_color);
        log_debug("h_scrollbar rect: %f, %f, %f, %f",
            view_x, view_y + view_height - scrollbar_size, view_width, scrollbar_size);

        if (content_width > 0) {
            Color handle_color = {0}; handle_color.r = (uint8_t)sc.HANDLE_COLOR; handle_color.g = (uint8_t)sc.HANDLE_COLOR; handle_color.b = (uint8_t)sc.HANDLE_COLOR; handle_color.a = 255;
            float h_handle_width = geometry.h_width * scale;
            float h_handle_x = geometry.h_x * scale;
            float h_scroll_y = view_y + view_height - scrollbar_size + border_cross;
            rc_fill_rounded_rect(rdcon, view_x + h_handle_x, h_scroll_y,
                h_handle_width, scrollbar_size - border_cross * 2.0f,
                handle_radius, handle_radius, handle_color);
        }
    }

    rc_pop_clip(rdcon);
    log_debug("finished rendering scroller");
}

void setup_scroller(RasterRenderContext* rdcon, ViewBlock* block) {
    float s = rdcon->raster_scale;
    Bound padding_clip; Corner radius;
    if (render_clip_overflow_geometry(block, &padding_clip, &radius)) {
        log_debug("setup scroller clip: left:%f, top:%f, right:%f, bottom:%f",
            padding_clip.left, padding_clip.top, padding_clip.right, padding_clip.bottom);
        rdcon->block.clip.left = max(rdcon->block.clip.left, rdcon->block.x + padding_clip.left * s);
        rdcon->block.clip.top = max(rdcon->block.clip.top, rdcon->block.y + padding_clip.top * s);
        rdcon->block.clip.right = min(rdcon->block.clip.right, rdcon->block.x + padding_clip.right * s);
        rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, rdcon->block.y + padding_clip.bottom * s);
        if (radiant_corner_has_radius(&radius)) {
            rdcon->block.has_clip_radius = true;
            rdcon->block.clip_radius = radiant_corner_scaled(&radius, s);
            constrain_corner_radii(&rdcon->block.clip_radius,
                rdcon->block.clip.right - rdcon->block.clip.left,
                rdcon->block.clip.bottom - rdcon->block.clip.top);
        }
    }
    if (block->scroll()->pane) {
        DocState* state = block->doc ? block->doc->state : NULL;
        float scroll_x = 0.0f, scroll_y = 0.0f;
        scroll_state_get_position_for_view(state, static_cast<View*>(block), block->scroll()->pane,
                                           &scroll_x, &scroll_y, NULL, NULL);
        rdcon->block.x -= scroll_x * s;
        rdcon->block.y -= scroll_y * s;
    }
}

void render_scroller(RasterRenderContext* rdcon, ViewBlock* block, BlockBlot* pa_block) {
    log_debug("render scrollbars");
    // need to reset block.x and y, which was changed by the scroller
    float s = rdcon->raster_scale;
    rdcon->block.x = pa_block->x + block->x * s;  rdcon->block.y = pa_block->y + block->y * s;
    if (block->scroll()->has_hz_scroll || block->scroll()->has_vt_scroll) {
        Rect rect = {rdcon->block.x, rdcon->block.y, block->width * s, block->height * s};
        if (block->bound && block->boundary_mut()->border) {
            BoxMetrics block_box = layout_box_metrics(block);
            rect.x += block->boundary()->border->width.left * s;
            rect.y += block->boundary()->border->width.top * s;
            rect.width -= block_box.border_h * s;
            rect.height -= block_box.border_v * s;
        }
        if (block->scroll()->pane) {
            DocState* state = block->doc ? block->doc->state : NULL;
            ScrollInteractionState interaction = {};
            scroll_state_get_interaction_for_view(state, static_cast<View*>(block),
                                                  &interaction);
            // Iframe layout transfers the embedded document's viewport scroll
            // to this outer block. Unlike an ordinary auto scroller, pointer
            // targeting enters the child document, so wheel scrolling never
            // produces a scrollbar-hover state for the iframe itself.
            bool iframe_scrollport = block->tag() == MARKUP_NAME_IFRAME;
            // Chromium's auto scrollbars overlay only appear while interacted with;
            // painting them at rest obscures content in static document renders.
            bool show_hz_scroll = block->scroll()->has_hz_scroll &&
                (block->scroll()->overflow_x == CSS_VALUE_SCROLL ||
                 iframe_scrollport ||
                 interaction.h_hovered || interaction.h_dragging);
            bool show_vt_scroll = block->scroll()->has_vt_scroll &&
                (block->scroll()->overflow_y == CSS_VALUE_SCROLL ||
                 iframe_scrollport ||
                 interaction.v_hovered || interaction.v_dragging);
            scrollpane_render(rdcon, block->scroll()->pane, &rect,
                block->content_width * s, block->content_height * s, &rdcon->block.clip, s,
                state, static_cast<View*>(block),
                show_hz_scroll, show_vt_scroll);
        } else {
            log_error("scroller has no scroll pane");
        }
    }
}

void scroll_apply_pending_element_scroll(ViewBlock* block) {
    if (!block || !block->scroller || !block->scroll()->pane) return;
    DomElement* elem = static_cast<DomElement*>(block);
    if (!elem->has_pending_element_scroll_x() &&
        !elem->has_pending_element_scroll_y()) {
        return;
    }

    DocState* state = elem->doc ? (DocState*)elem->doc->state : nullptr;
    float current_x = 0.0f;
    float current_y = 0.0f;
    scroll_state_get_position_for_view(state, static_cast<View*>(block),
        block->scroll()->pane, &current_x, &current_y, NULL, NULL);
    float target_x = elem->has_pending_element_scroll_x()
        ? elem->pending_scroll_x() : current_x;
    float target_y = elem->has_pending_element_scroll_y()
        ? elem->pending_scroll_y() : current_y;

    scroll_snap_adjust_position(block, &target_x, &target_y);
    scroll_state_set_position_for_view(state, static_cast<View*>(block),
        block->scroll()->pane, target_x, target_y, false);
    if (state) {
        // A state-backed pane now owns the scroll position across pool resets.
        elem->set_has_pending_element_scroll_x(false);
        elem->set_has_pending_element_scroll_y(false);
    }
}

struct ScrollSmoothTickContext {
    DomDocument* document;
    DocState* state;
    double now;
    bool active;
};

static bool scroll_smooth_tick_view(View* view, void* context) {
    ScrollSmoothTickContext* tick = (ScrollSmoothTickContext*)context;
    ViewBlock* block = lam::view_as_block(view);
    if (!block || !block->scroller || !block->scroll()->pane) return true;
    float old_x = 0.0f, old_y = 0.0f;
    scroll_state_get_position_for_view(tick->state, view, block->scroll()->pane,
                                       &old_x, &old_y, nullptr, nullptr);
    bool viewport = view == tick->document->view_tree->root;
    tick->active |= scroll_state_tick_smooth_for_view(tick->state, view,
        block->scroll()->pane, tick->now, viewport);
    float new_x = 0.0f, new_y = 0.0f;
    scroll_state_get_position_for_view(tick->state, view, block->scroll()->pane,
                                       &new_x, &new_y, nullptr, nullptr);
    if (new_x != old_x || new_y != old_y) {
        if (viewport)
            doc_state_sync_viewport_scroll(tick->state, tick->document,
                                           new_x, new_y);
        dom_notify_scroll_position_change(view, old_x, old_y);
    }
    return true;
}

bool scroll_smooth_tick_document(DomDocument* document, double now) {
    if (!document || !document->state ||
        !document->state->has_active_smooth_scroll ||
        !document->view_tree || !document->view_tree->root) return false;
    ScrollSmoothTickContext tick = {document, document->state, now, false};
    view_geometry_walk_elements(document->view_tree->root,
                                scroll_smooth_tick_view, &tick);
    document->state->has_active_smooth_scroll = tick.active;
    return tick.active;
}

struct ScrollSnapSearch {
    ViewBlock* container;
    float desired_x, desired_y;
    float port_width, port_height;
    float padding_used[4];
    float min_x, max_x, min_y, max_y;
    float best_x, best_y, distance_x, distance_y;
    float distance_pair;
    float previous_x, following_x, previous_y, following_y;
    bool oversized_x, oversized_y;
    bool use_x, use_y, found_x, found_y;
};

static bool scroll_snap_axis_enabled(ScrollSnapAxis axis, bool horizontal,
                                     bool inline_vertical) {
    if (axis == SCROLL_SNAP_AXIS_NONE) return false;
    if (axis == SCROLL_SNAP_AXIS_BOTH || axis == SCROLL_SNAP_AXIS_PAIR) return true;
    if (axis == SCROLL_SNAP_AXIS_X) return horizontal;
    if (axis == SCROLL_SNAP_AXIS_Y) return !horizontal;
    return axis == SCROLL_SNAP_AXIS_BLOCK
        ? horizontal == inline_vertical : horizontal != inline_vertical;
}

static bool scroll_snap_position_for_axis(ScrollSnapSearch* search,
                                           DomElement* target, bool horizontal,
                                           float* out_position,
                                           bool* out_oversized_cover = nullptr) {
    bool inline_vertical = layout_element_inline_axis_is_vertical(search->container);
    CssEnum align = horizontal == inline_vertical
        ? target->scroll()->snap_align_block
        : target->scroll()->snap_align_inline;
    if (align == CSS_VALUE_NONE) return false;
    LayoutLogicalSides logical = layout_logical_sides(inline_vertical,
        layout_element_writing_mode(search->container) == WM_VERTICAL_RL,
        search->container->block()->direction == CSS_VALUE_RTL);
    CssBoxSide start_side = horizontal == inline_vertical
        ? logical.block_start : logical.inline_start;
    bool start_is_min = start_side == (horizontal
        ? CSS_BOX_SIDE_LEFT : CSS_BOX_SIDE_TOP);
    int min_side = horizontal ? CSS_BOX_SIDE_LEFT : CSS_BOX_SIDE_TOP;
    int max_side = horizontal ? CSS_BOX_SIDE_RIGHT : CSS_BOX_SIDE_BOTTOM;
    float port_size = horizontal ? search->port_width : search->port_height;
    float local = layout_scroll_document_coord(target, horizontal) -
        layout_scrollport_start(search->container, horizontal);
    float target_size = horizontal ? target->width : target->height;
    float area_start = local - target->scroll()->scroll_margin[min_side].pixels;
    float area_end = local + target_size +
        target->scroll()->scroll_margin[max_side].pixels;
    float pad_start = search->padding_used[min_side];
    float pad_end = search->padding_used[max_side];
    if (out_oversized_cover) {
        float snapport_size = max(0.0f, port_size - pad_start - pad_end);
        float desired = horizontal ? search->desired_x : search->desired_y;
        *out_oversized_cover = snapport_size > 0.0f &&
            area_end - area_start > snapport_size &&
            area_start <= desired + pad_start &&
            area_end >= desired + port_size - pad_end;
    }
    float position = 0.0f;
    if (align == CSS_VALUE_CENTER) {
        position = (area_start + area_end - port_size - pad_start + pad_end) * 0.5f;
    } else if ((align == CSS_VALUE_START) == start_is_min) {
        position = area_start - pad_start;
    } else {
        position = area_end - port_size + pad_end;
    }
    float minimum = horizontal ? search->min_x : search->min_y;
    float maximum = horizontal ? search->max_x : search->max_y;
    position = max(minimum, min(maximum, position));
    *out_position = position;
    return true;
}

static void scroll_snap_consider_axis(ScrollSnapSearch* search,
                                      DomElement* target, bool horizontal) {
    float position = 0.0f;
    bool oversized_cover = false;
    if (!scroll_snap_position_for_axis(search, target, horizontal, &position,
                                       &oversized_cover)) return;
    float desired = horizontal ? search->desired_x : search->desired_y;
    float* previous = horizontal ? &search->previous_x : &search->previous_y;
    float* following = horizontal ? &search->following_x : &search->following_y;
    if (position <= desired) *previous = max(*previous, position);
    if (position >= desired) *following = min(*following, position);
    if (oversized_cover) {
        *(horizontal ? &search->oversized_x : &search->oversized_y) = true;
    }
    float distance = fabsf(position - (horizontal
        ? search->desired_x : search->desired_y));
    float* best_distance = horizontal ? &search->distance_x : &search->distance_y;
    if (distance < *best_distance) {
        *best_distance = distance;
        *(horizontal ? &search->best_x : &search->best_y) = position;
        *(horizontal ? &search->found_x : &search->found_y) = true;
    }
}

static void scroll_snap_consider_pair(ScrollSnapSearch* search,
                                      DomElement* target) {
    float x = 0.0f, y = 0.0f;
    // Pair positions must come from the same snap area in both axes.
    if (!scroll_snap_position_for_axis(search, target, true, &x) ||
        !scroll_snap_position_for_axis(search, target, false, &y)) return;
    float dx = (x - search->desired_x) / max(1.0f, search->port_width);
    float dy = (y - search->desired_y) / max(1.0f, search->port_height);
    float distance = dx * dx + dy * dy;
    if (distance < search->distance_pair) {
        search->distance_pair = distance;
        search->best_x = x;
        search->best_y = y;
        search->found_x = search->found_y = true;
    }
}

void scroll_snap_adjust_position(ViewBlock* block, float* x, float* y,
                                 bool explicit_target) {
    if (!block || !block->scroller || !block->scroll()->pane || !x || !y) return;
    bool inline_vertical = layout_element_inline_axis_is_vertical(block);
    ScrollSnapAxis axis = block->scroll()->snap_axis;
    ScrollSnapSearch search = {};
    search.container = block;
    search.use_x = scroll_snap_axis_enabled(axis, true, inline_vertical);
    search.use_y = scroll_snap_axis_enabled(axis, false, inline_vertical);
    if (!search.use_x && !search.use_y) return;
    search.desired_x = *x;
    search.desired_y = *y;
    search.distance_x = search.distance_y = search.distance_pair = INFINITY;
    search.port_width = layout_content_size_from_border_box(block, block->width, true);
    search.port_height = layout_content_size_from_border_box(block, block->height, false);
    LayoutContext length_context = {};
    length_context.doc = block->doc;
    length_context.view = lam::up(static_cast<View*>(block));
    length_context.elmt = lam::up(block);
    length_context.ui_context = lam::up(block->doc
        ? static_cast<UiContext*>(block->doc->js.host_ui_context) : nullptr);
    length_context.pool = lam::up(block->doc && block->doc->view_tree
        ? block->doc->view_tree->prop_pool : nullptr);
    length_context.width = length_context.ui_context
        ? length_context.ui_context->viewport_width : block->width;
    length_context.height = length_context.ui_context
        ? length_context.ui_context->viewport_height : block->height;
    length_context.root_font_size = block->doc && block->doc->root
        ? block->doc->root->fontp()->font_size : 16.0f;
    for (int side = 0; side < 4; side++) {
        float base = side == CSS_BOX_SIDE_LEFT || side == CSS_BOX_SIDE_RIGHT
            ? search.port_width : search.port_height;
        search.padding_used[side] = layout_scroll_spacing_used(
            &length_context, block, block->scroll()->scroll_padding[side], base);
    }
    DocState* state = block->doc ? block->doc->state : nullptr;
    scroll_state_get_range_for_view(state, static_cast<View*>(block),
        block->scroll()->pane, &search.min_x, &search.max_x,
        &search.min_y, &search.max_y);
    search.previous_x = search.min_x;
    search.following_x = search.max_x;
    search.previous_y = search.min_y;
    search.following_y = search.max_y;
    // Walk the current view tree without retaining DOM pointers across relayout.
    // A nested scroll container owns its descendants' snap areas.
    DomNode* node = block->first_child;
    while (node) {
        DomNode* next = node->next_sibling;
        if (node->is_element()) {
            DomElement* target = node->as_element();
            if (target->width > 0.0f && target->height > 0.0f &&
                (target->scroll()->snap_align_block != CSS_VALUE_NONE ||
                 target->scroll()->snap_align_inline != CSS_VALUE_NONE)) {
                if (axis == SCROLL_SNAP_AXIS_PAIR)
                    scroll_snap_consider_pair(&search, target);
                else {
                    if (search.use_x) scroll_snap_consider_axis(&search, target, true);
                    if (search.use_y) scroll_snap_consider_axis(&search, target, false);
                }
            }
            ViewBlock* nested = lam::view_as_block(static_cast<View*>(target));
            bool owns_descendants = nested &&
                layout_block_establishes_scroll_container(nested) &&
                !(target->tag() == MARKUP_NAME_BODY && target->parent == block);
            if (!owns_descendants && target->first_child) {
                node = target->first_child;
                continue;
            }
        }
        while (!next && node->parent != block) {
            node = node->parent;
            next = node->next_sibling;
        }
        node = next;
    }
    float proximity_x = search.port_width * 0.25f;
    float proximity_y = search.port_height * 0.25f;
    if (axis == SCROLL_SNAP_AXIS_PAIR) {
        if (search.found_x && (block->scroll()->snap_mandatory ||
            search.distance_pair <= 0.25f * 0.25f)) {
            *x = search.best_x;
            *y = search.best_y;
        }
        return;
    }
    // An oversized snap area admits every position where it covers the
    // snapport, provided adjacent snap points leave more than a snapport gap.
    float snapport_width = max(0.0f, search.port_width -
        search.padding_used[CSS_BOX_SIDE_LEFT] -
        search.padding_used[CSS_BOX_SIDE_RIGHT]);
    float snapport_height = max(0.0f, search.port_height -
        search.padding_used[CSS_BOX_SIDE_TOP] -
        search.padding_used[CSS_BOX_SIDE_BOTTOM]);
    if (!explicit_target && search.oversized_x &&
        search.following_x - search.previous_x > snapport_width)
        search.found_x = false;
    if (!explicit_target && search.oversized_y &&
        search.following_y - search.previous_y > snapport_height)
        search.found_y = false;
    if (search.found_x && (block->scroll()->snap_mandatory ||
        search.distance_x <= proximity_x)) *x = search.best_x;
    if (search.found_y && (block->scroll()->snap_mandatory ||
        search.distance_y <= proximity_y)) *y = search.best_y;
}

bool scrollpane_scroll(EventContext* evcon, ViewBlock* block, ScrollPane* sp,
                       float delta_x, float delta_y,
                       float* applied_x, float* applied_y) {
    if (applied_x) *applied_x = 0.0f;
    if (applied_y) *applied_y = 0.0f;
    if (!evcon || !block || !sp) return false;
    log_debug("wheel-chain: requested pixels %f, %f", delta_x, delta_y);

    DomDocument* doc = block && block->doc
        ? block->doc
        : (evcon && evcon->target_document
            ? evcon->target_document
            : (evcon && evcon->ui_context ? evcon->ui_context->document : nullptr));
    DocState* state = doc ? (DocState*)doc->state : nullptr;
    float h = 0.0f, v = 0.0f;
    scroll_state_get_position_for_view(state, (View*)block, sp, &h, &v, NULL, NULL);
    float h_min = 0.0f, h_max = 0.0f, v_min = 0.0f, v_max = 0.0f;
    scroll_state_get_range_for_view(state, (View*)block, sp,
                                    &h_min, &h_max, &v_min, &v_max);
    float previous_h = h;
    float previous_v = v;
    // signed reverse-flow ranges may scroll even when their upper bound is zero.
    if (delta_y != 0.0f && v_max > v_min &&
        scroll_axis_accepts_wheel(block->scroll()->overflow_y)) v += delta_y;
    if (delta_x != 0.0f && h_max > h_min &&
        scroll_axis_accepts_wheel(block->scroll()->overflow_x)) h += delta_x;

    float wheel_applied_x = max(h_min, min(h_max, h)) - previous_h;
    float wheel_applied_y = max(v_min, min(v_max, v)) - previous_v;
    if (wheel_applied_x != 0.0f || wheel_applied_y != 0.0f)
        scroll_snap_adjust_position(block, &h, &v);
    // Centralized writer path for scroll mutations.
    scroll_state_set_position_for_view(state, (View*)block, sp, h, v, false);

    scroll_state_get_position_for_view(state, (View*)block, sp, &h, &v, NULL, NULL);
    if (applied_x) *applied_x = wheel_applied_x;
    if (applied_y) *applied_y = wheel_applied_y;
    log_debug("wheel-chain: updated position %f, %f", h, v);
    if (h != previous_h || v != previous_v) evcon->need_repaint = true;
    // todo: set invalidate_rect
    return h != previous_h || v != previous_v;
}

static DocState* scrollpane_doc_state(EventContext* evcon, ViewBlock* block) {
    DomDocument* doc = block && block->doc
        ? block->doc
        : (evcon && evcon->target_document
            ? evcon->target_document
            : (evcon && evcon->ui_context ? evcon->ui_context->document : nullptr));
    return doc ? (DocState*)doc->state : nullptr;
}

bool scrollpane_target(EventContext* evcon, ViewBlock* block) {
    MousePositionEvent *event = &evcon->event.mouse_position;
    ScrollPane* sp = block->scroll()->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    float bottom = evcon->block.y + block->height;  float right = evcon->block.x + block->width;
    float scrollbar_css = sc.SCROLLBAR_SIZE;
    bool h_hovered = false, v_hovered = false;
    if (block->scroll()->has_hz_scroll) {
        if (evcon->block.x <= event->x && event->x < right &&
            bottom - scrollbar_css <= event->y && event->y < bottom) {
            h_hovered = true;
            scroll_state_set_hover_for_view(state, (View*)block, sp, h_hovered, v_hovered);
            return true;
        }
    }
    if (block->scroll()->has_vt_scroll) {
        if (evcon->block.y <= event->y && event->y < bottom &&
            right - scrollbar_css <= event->x && event->x < right) {
            v_hovered = true;
            scroll_state_set_hover_for_view(state, (View*)block, sp, h_hovered, v_hovered);
            return true;
        }
    }
    scroll_state_set_hover_for_view(state, (View*)block, sp, false, false);
    return false;
}

ScrollbarPressPart scrollpane_press_part(EventContext* evcon, ViewBlock* block) {
    if (!evcon || !block || !block->scroller || !block->scroll()->pane) {
        return SCROLLBAR_PRESS_NONE;
    }
    ScrollPane* sp = block->scroll()->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    ScrollInteractionState interaction;
    scroll_state_get_interaction_for_view(state, (View*)block, &interaction);
    ScrollHandleGeometry geometry = scrollpane_handle_geometry(
        sp, state, static_cast<View*>(block), block->width, block->height,
        block->content_width, block->content_height);

    if (interaction.h_hovered) {
        if (evcon->offset_x < geometry.h_x) return SCROLLBAR_PRESS_HORIZONTAL_BEFORE;
        if (evcon->offset_x > geometry.h_x + geometry.h_width) {
            return SCROLLBAR_PRESS_HORIZONTAL_AFTER;
        }
        return SCROLLBAR_PRESS_HORIZONTAL_THUMB;
    }
    if (interaction.v_hovered) {
        if (evcon->offset_y < geometry.v_y) return SCROLLBAR_PRESS_VERTICAL_BEFORE;
        if (evcon->offset_y > geometry.v_y + geometry.v_height) {
            return SCROLLBAR_PRESS_VERTICAL_AFTER;
        }
        return SCROLLBAR_PRESS_VERTICAL_THUMB;
    }
    return SCROLLBAR_PRESS_NONE;
}

const char* scrollpane_press_part_name(ScrollbarPressPart part) {
    switch (part) {
        case SCROLLBAR_PRESS_HORIZONTAL_BEFORE: return "horizontalBefore";
        case SCROLLBAR_PRESS_HORIZONTAL_THUMB: return "horizontalThumb";
        case SCROLLBAR_PRESS_HORIZONTAL_AFTER: return "horizontalAfter";
        case SCROLLBAR_PRESS_VERTICAL_BEFORE: return "verticalBefore";
        case SCROLLBAR_PRESS_VERTICAL_THUMB: return "verticalThumb";
        case SCROLLBAR_PRESS_VERTICAL_AFTER: return "verticalAfter";
        default: return "none";
    }
}

bool scrollpane_apply_press_operation(EventContext* evcon, ViewBlock* block,
                                      const char* operation) {
    if (!evcon || !block || !operation || !block->scroller ||
        !block->scroll()->pane) {
        return false;
    }

    MouseButtonEvent* event = &evcon->event.mouse_button;
    ScrollPane* sp = block->scroll()->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    ScrollbarPressPart part = scrollpane_press_part(evcon, block);
    float h = 0.0f, v = 0.0f;
    scroll_state_get_position_for_view(state, (View*)block, sp, &h, &v, NULL, NULL);

    // The operation name is package policy; this routine only translates the
    // current geometric part into the associated range/drag transition.
    if (strcmp(operation, "pageLeft") == 0 &&
        part == SCROLLBAR_PRESS_HORIZONTAL_BEFORE) {
        float next_h = h - block->width * 0.85f;
        scroll_state_set_position_for_view(state, (View*)block, sp, next_h, v, false);
    } else if (strcmp(operation, "pageRight") == 0 &&
               part == SCROLLBAR_PRESS_HORIZONTAL_AFTER) {
        float next_h = h + block->width * 0.85f;
        scroll_state_set_position_for_view(state, (View*)block, sp, next_h, v, false);
    } else if (strcmp(operation, "pageBackward") == 0 &&
               part == SCROLLBAR_PRESS_VERTICAL_BEFORE) {
        float next_v = v - block->height * 0.85f;
        scroll_state_set_position_for_view(state, (View*)block, sp, h, next_v, false);
    } else if (strcmp(operation, "pageForward") == 0 &&
               part == SCROLLBAR_PRESS_VERTICAL_AFTER) {
        float next_v = v + block->height * 0.85f;
        scroll_state_set_position_for_view(state, (View*)block, sp, h, next_v, false);
    } else if (strcmp(operation, "drag") == 0 &&
               (part == SCROLLBAR_PRESS_HORIZONTAL_THUMB ||
                part == SCROLLBAR_PRESS_VERTICAL_THUMB)) {
        bool horizontal = part == SCROLLBAR_PRESS_HORIZONTAL_THUMB;
        scroll_state_begin_drag_for_view(state, (View*)block, sp, horizontal,
                                         event->x, event->y, h, v);
        DragTransitionArgs drag_args = { .target = (View*)block, .dragging = true };
        drag_transition(state, DRAG_TRANSITION_SET_STATE, &drag_args);
        return false;
    } else {
        return false;
    }

    float next_h = h;
    float next_v = v;
    scroll_state_get_position_for_view(state, (View*)block, sp,
                                       &next_h, &next_v, NULL, NULL);
    bool changed = next_h != h || next_v != v;
    if (changed) evcon->need_repaint = true;
    return changed;
}

void scrollpane_mouse_up(EventContext* evcon, ViewBlock* block) {
    ScrollPane* sp = block->scroll()->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    if (scroll_state_is_dragging_for_view(state, (View*)block)) {
        scroll_state_clear_drag_for_view(state, (View*)block, sp);
        DragTransitionArgs drag_args = { .target = NULL, .dragging = false };
        drag_transition(state, DRAG_TRANSITION_SET_STATE, &drag_args);
    }
}

void scrollpane_drag(EventContext* evcon, ViewBlock* block) {
    MousePositionEvent *event = &evcon->event.mouse_position;
    ScrollPane* sp = block->scroll()->pane;
    DocState* state = scrollpane_doc_state(evcon, block);
    ScrollInteractionState interaction;
    scroll_state_get_interaction_for_view(state, (View*)block, &interaction);
    float h = 0.0f, v = 0.0f, h_max = 0.0f, v_max = 0.0f;
    scroll_state_get_position_for_view(state, (View*)block, sp, &h, &v, &h_max, &v_max);
    ScrollHandleGeometry geometry = scrollpane_handle_geometry(
        sp, state, static_cast<View*>(block), block->width, block->height,
        block->content_width, block->content_height);

    // Vertical dragging
    if (interaction.v_dragging) {
        float handle_h = geometry.v_height;
        float delta_y = event->y - interaction.drag_start_y;  // CSS pixels
        // scroll track length in CSS pixels = block height - scrollbar bottom strip - borders
        float scrollbar_css = sc.SCROLLBAR_SIZE;
        float border_css = sc.SCROLL_BORDER_MAIN;
        float scroll_track = block->height - scrollbar_css - border_css * 2;
        float scroll_range = scroll_track - handle_h;
        float v_extent = v_max - sp->v_min_scroll;
        float scroll_per_pixel = scroll_range > 0 ? v_extent / scroll_range : 0;
        float v_scroll_position = interaction.v_drag_start_scroll + (delta_y * scroll_per_pixel);
        if (v_scroll_position != v) {
            scroll_state_set_position_for_view(state, (View*)block, sp, h, v_scroll_position, false);
            evcon->need_repaint = true;
        }
    }

    // Horizontal dragging
    if (interaction.h_dragging) {
        float handle_w = geometry.h_width;
        float delta_x = event->x - interaction.drag_start_x;  // CSS pixels
        // scroll track length in CSS pixels = block width - scrollbar right strip - borders
        float scrollbar_css2 = sc.SCROLLBAR_SIZE;
        float border_css2 = sc.SCROLL_BORDER_MAIN;
        float scroll_track_h = block->width - scrollbar_css2 - border_css2 * 2;
        float scroll_range = scroll_track_h - handle_w;
        float h_extent = h_max - sp->h_min_scroll;
        float scroll_per_pixel = scroll_range > 0 ? h_extent / scroll_range : 0;
        float h_scroll_position = interaction.h_drag_start_scroll + (delta_x * scroll_per_pixel);
        if (h_scroll_position != h) {
            scroll_state_set_position_for_view(state, (View*)block, sp, h_scroll_position, v, false);
            evcon->need_repaint = true;
        }
    }
}

void update_scroller(ViewBlock* block, float content_width, float content_height) {
    if (!block->scroller) { return; }
    // handle horizontal overflow
    log_debug("update scroller for block:%s, content_width:%.1f, content_height:%.1f, block_width:%.1f, block_height:%.1f",
        block->node_name(), content_width, content_height, block->width, block->height);

    block->scroller->has_hz_overflow = false;
    block->scroller->has_vt_overflow = false;
    block->scroller->has_hz_scroll = false;
    block->scroller->has_vt_scroll = false;
    block->scroller->has_clip = false;

    // Update scroll pane max values through centralized API.
    if (block->scroll()->pane) {
        DocState* state = block->doc ? (DocState*)block->doc->state : nullptr;
        float h_max = content_width > block->width ? content_width - block->width : 0.0f;
        float v_max = content_height > block->height ? content_height - block->height : 0.0f;
        scroll_state_set_max_for_view(state, (View*)block, block->scroll()->pane, h_max, v_max);
        scroll_apply_pending_element_scroll(block);
        scroll_state_get_position_for_view(state, (View*)block, block->scroll()->pane,
                                           NULL, NULL, &h_max, &v_max);
        log_debug("update_scroller: h_max_scroll=%.1f, v_max_scroll=%.1f",
            h_max, v_max);
    }

    if (content_width > block->width) { // hz overflow
        block->scroller->has_hz_overflow = true;
        if (block->scroll()->overflow_x == CSS_VALUE_SCROLL ||
            block->scroll()->overflow_x == CSS_VALUE_AUTO) {
            block->scroller->has_hz_scroll = true;
        }
        if (block->scroll()->has_hz_scroll ||
            block->scroll()->overflow_x == CSS_VALUE_CLIP ||
            block->scroll()->overflow_x == CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
        }
    }
    else {
        block->scroller->has_hz_overflow = false;
    }
    // handle vertical overflow and determine block->height
    if (content_height > block->height) { // vt overflow
        block->scroller->has_vt_overflow = true;
        if (block->scroll()->overflow_y == CSS_VALUE_SCROLL ||
            block->scroll()->overflow_y == CSS_VALUE_AUTO) {
            block->scroller->has_vt_scroll = true;
        }
        if (block->scroll()->has_vt_scroll ||
            block->scroll()->overflow_y == CSS_VALUE_CLIP ||
            block->scroll()->overflow_y == CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
        }
    }
    else {
        block->scroller->has_vt_overflow = false;
    }
    // Always clip when overflow is hidden/clip, even without actual overflow
    // This is needed for border-radius clipping to work correctly
    bool should_clip = block->scroll()->has_vt_overflow || block->scroll()->has_hz_overflow ||
                       block->scroll()->overflow_x == CSS_VALUE_HIDDEN ||
                       block->scroll()->overflow_x == CSS_VALUE_CLIP ||
                       block->scroll()->overflow_y == CSS_VALUE_HIDDEN ||
                       block->scroll()->overflow_y == CSS_VALUE_CLIP;
    if (should_clip) {
        block->scroller->has_clip = true;
        // Keep ScrollProp::clip in border-box coordinates; setup_scroller owns
        // the single inset to the CSS overflow padding edge during rendering.
        block->scroll_mut()->clip.left = 0.0f;
        block->scroll_mut()->clip.top = 0.0f;
        block->scroll_mut()->clip.right = block->width;
        block->scroll_mut()->clip.bottom = block->height;
    }
}
