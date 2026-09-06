#include "view.hpp"

#include "../lib/tagged.hpp"

static void view_geometry_pane_scroll(ViewBlock* block, float* out_x,
                                      float* out_y, void* context);

static void view_geometry_resolve_scroll(
        ViewBlock* block, ViewGeometryScrollResolver resolve_scroll,
        void* context, float* out_x, float* out_y) {
    if (resolve_scroll) {
        resolve_scroll(block, out_x, out_y, context);
    } else {
        view_geometry_pane_scroll(block, out_x, out_y, context);
    }
}

static RdtLogicalPoint view_geometry_apply_node(
        View* view, RdtLogicalPoint point, bool blocks_only,
        float offset_direction, float scroll_direction,
        ViewGeometryScrollResolver resolve_scroll, void* context) {
    if (!view || (blocks_only && !view->is_block())) return point;
    point.x += view->x * offset_direction;
    point.y += view->y * offset_direction;
    if (scroll_direction != 0.0f && view->is_block()) {
        float scroll_x = 0.0f;
        float scroll_y = 0.0f;
        view_geometry_resolve_scroll(lam::view_require_block(view),
                                     resolve_scroll, context,
                                     &scroll_x, &scroll_y);
        point.x += scroll_x * scroll_direction;
        point.y += scroll_y * scroll_direction;
    }
    return point;
}

static RdtLogicalPoint view_geometry_map_chain(
        View* view, RdtLogicalPoint point, bool blocks_only,
        float offset_direction, float scroll_direction,
        ViewGeometryScrollResolver resolve_scroll = nullptr,
        void* context = nullptr) {
    for (View* current = view; current; current = current->parent) {
        point = view_geometry_apply_node(
            current, point, blocks_only, offset_direction, scroll_direction,
            resolve_scroll, context);
    }
    return point;
}

static void view_geometry_pane_scroll(ViewBlock* block, float* out_x,
                                      float* out_y, void*) {
    float x = 0.0f;
    float y = 0.0f;
    if (block && block->scroller && block->scroll()->pane) {
        x = block->scroll()->pane->h_scroll_position;
        y = block->scroll()->pane->v_scroll_position;
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

RdtLogicalPoint view_geometry_node_document_origin(View* view) {
    return view_geometry_map_chain(
        view, {0.0f, 0.0f}, false, 1.0f, 0.0f);
}

RdtLogicalPoint view_geometry_local_to_block_document(
        View* view, RdtLogicalPoint local) {
    return view_geometry_map_chain(
        view ? view->parent : nullptr, local, true, 1.0f, 0.0f);
}

RdtLogicalPoint view_geometry_node_viewport_origin(
        View* view, ViewGeometryScrollResolver resolve_scroll, void* context) {
    RdtLogicalPoint point = view_geometry_apply_node(
        view, {0.0f, 0.0f}, false, 1.0f, 0.0f, resolve_scroll, context);
    return view_geometry_map_chain(
        view ? view->parent : nullptr, point, false, 1.0f, -1.0f,
        resolve_scroll, context);
}

RdtLogicalPoint view_geometry_local_to_block_viewport(
        View* view, RdtLogicalPoint local,
        ViewGeometryScrollResolver resolve_scroll, void* context) {
    return view_geometry_map_chain(
        view ? view->parent : nullptr, local, true, 1.0f, -1.0f,
        resolve_scroll, context);
}

RdtLogicalPoint view_geometry_block_viewport_to_local(
        View* view, RdtLogicalPoint point,
        ViewGeometryScrollResolver resolve_scroll, void* context) {
    return view_geometry_map_chain(
        view ? view->parent : nullptr, point, true, -1.0f, 1.0f,
        resolve_scroll, context);
}

RdtLogicalPoint view_geometry_child_content_origin(
        View* view, RdtLogicalPoint parent_origin,
        ViewGeometryScrollResolver resolve_scroll, void* context) {
    return view_geometry_apply_node(
        view, parent_origin, true, 1.0f, -1.0f,
        resolve_scroll, context);
}

RdtLogicalPoint view_geometry_child_node_origin(
        View* view, RdtLogicalPoint parent_origin,
        ViewGeometryScrollResolver resolve_scroll, void* context) {
    return view_geometry_apply_node(
        view, parent_origin, false, 1.0f, -1.0f,
        resolve_scroll, context);
}

RdtLogicalPoint view_geometry_apply_external_viewport(
        RdtLogicalPoint point, ViewBlock* viewport_root,
        RdtLogicalPoint document_offset,
        ViewGeometryScrollResolver resolve_scroll, void* context) {
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    view_geometry_resolve_scroll(viewport_root, resolve_scroll, context,
                                 &scroll_x, &scroll_y);
    if (fabsf(document_offset.x + scroll_x) >= 0.5f) point.x -= scroll_x;
    if (fabsf(document_offset.y + scroll_y) >= 0.5f) point.y -= scroll_y;
    point.x += document_offset.x;
    point.y += document_offset.y;
    return point;
}

RdtLogicalPoint view_geometry_local_to_window(
        View* view, RdtLogicalPoint local, ViewBlock* viewport_root,
        RdtLogicalPoint document_offset,
        ViewGeometryScrollResolver resolve_scroll, void* context) {
    RdtLogicalPoint point = view_geometry_local_to_block_viewport(
        view, local, resolve_scroll, context);
    bool root_scroll_applied = false;
    for (View* current = view ? view->parent : nullptr; current;
         current = current->parent) {
        if (current == static_cast<View*>(viewport_root)) {
            root_scroll_applied = true;
            break;
        }
    }
    if (root_scroll_applied) {
        // Undo the in-chain root scroll before the common viewport step. That
        // step reapplies it only when the external document offset omits it.
        float scroll_x = 0.0f;
        float scroll_y = 0.0f;
        view_geometry_resolve_scroll(viewport_root, resolve_scroll, context,
                                     &scroll_x, &scroll_y);
        point.x += scroll_x;
        point.y += scroll_y;
    }
    return view_geometry_apply_external_viewport(
        point, viewport_root, document_offset, resolve_scroll, context);
}

static bool view_geometry_find_document_offset(
        View* view, DomDocument* target_document, RdtLogicalPoint parent_origin,
        ViewGeometryScrollResolver resolve_scroll, void* context,
        RdtLogicalPoint* out_offset) {
    if (!view || !target_document) return false;

    RdtLogicalPoint child_origin = view_geometry_child_node_origin(
        view, parent_origin, resolve_scroll, context);
    if (view->is_block() && view->is_element()) {
        ViewBlock* block = lam::view_require_block(view);
        DomDocument* embedded_document = block->embed && block->embedp()->doc
            ? block->embedp()->doc : nullptr;
        if (embedded_document == target_document) {
            if (out_offset) *out_offset = child_origin;
            return true;
        }
        if (embedded_document && embedded_document->view_tree &&
            embedded_document->view_tree->root &&
            view_geometry_find_document_offset(
                embedded_document->view_tree->root, target_document,
                child_origin, resolve_scroll, context, out_offset)) {
            return true;
        }
    }

    if (!view->is_element()) return false;
    DomElement* element = lam::dom_require_element(view);
    for (DomNode* child = element->first_child; child;
         child = child->next_sibling) {
        View* child_view = static_cast<View*>(child);
        if (!child_view->view_type) continue;
        if (view_geometry_find_document_offset(
                child_view, target_document, child_origin, resolve_scroll,
                context, out_offset)) {
            return true;
        }
    }
    return false;
}

RdtLogicalPoint view_geometry_document_viewport_offset(
        DomDocument* root_document, DomDocument* target_document,
        ViewGeometryScrollResolver resolve_scroll, void* context) {
    RdtLogicalPoint offset = {0.0f, 0.0f};
    if (!root_document || !target_document || root_document == target_document ||
        !root_document->view_tree || !root_document->view_tree->root) {
        return offset;
    }
    view_geometry_find_document_offset(root_document->view_tree->root,
                                       target_document, offset,
                                       resolve_scroll, context, &offset);
    return offset;
}

bool view_geometry_find_text_at(View* view, RdtLogicalPoint point,
                                RdtLogicalPoint origin,
                                bool include_trailing_edges,
                                ViewGeometryTextHit* out_hit) {
    if (out_hit) *out_hit = {};
    if (!view) return false;
    if (view->is_text()) {
        DomText* text = lam::dom_require_text(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            float width = view_geometry_text_rect_width(text, rect);
            float left = origin.x + rect->x;
            float top = origin.y + rect->y;
            float right = left + width;
            float bottom = top + rect->height;
            bool inside_x = point.x >= left &&
                (include_trailing_edges ? point.x <= right : point.x < right);
            bool inside_y = point.y >= top &&
                (include_trailing_edges ? point.y <= bottom : point.y < bottom);
            if (!inside_x || !inside_y) continue;
            if (out_hit) {
                out_hit->text = text;
                out_hit->rect = rect;
                out_hit->local_x = point.x - left;
            }
            return true;
        }
        return false;
    }
    if (!view->is_element()) return false;

    RdtLogicalPoint child_origin = view_geometry_child_content_origin(
        view, origin);
    DomElement* element = lam::dom_require_element(view);
    for (DomNode* child = element->first_child; child;
         child = child->next_sibling) {
        View* child_view = static_cast<View*>(child);
        if (!child_view->view_type) continue;
        if (view_geometry_find_text_at(child_view, point, child_origin,
                                       include_trailing_edges, out_hit)) {
            return true;
        }
    }
    return false;
}

bool view_geometry_rect_contains_point(Rect rect, RdtLogicalPoint point) {
    return rect.width > 0.0f && rect.height > 0.0f &&
        rect.x <= point.x && point.x < rect.x + rect.width &&
        rect.y <= point.y && point.y < rect.y + rect.height;
}

bool view_geometry_rect_contains_rect(Rect outer, Rect inner, float epsilon) {
    return inner.x >= outer.x - epsilon && inner.y >= outer.y - epsilon &&
        inner.x + inner.width <= outer.x + outer.width + epsilon &&
        inner.y + inner.height <= outer.y + outer.height + epsilon;
}

float view_geometry_point_rect_distance(Rect rect, RdtLogicalPoint point) {
    float dx = point.x < rect.x ? rect.x - point.x
        : point.x > rect.x + rect.width ? point.x - (rect.x + rect.width)
        : 0.0f;
    float dy = point.y < rect.y ? rect.y - point.y
        : point.y > rect.y + rect.height ? point.y - (rect.y + rect.height)
        : 0.0f;
    return dx + dy;
}

Bound view_geometry_intersect_bound_rect(Bound bound, Rect rect) {
    bound.left = max(bound.left, rect.x);
    bound.top = max(bound.top, rect.y);
    bound.right = min(bound.right, rect.x + rect.width);
    bound.bottom = min(bound.bottom, rect.y + rect.height);
    return bound;
}

Rect view_geometry_expand_rect(Rect rect, float expand) {
    if (expand <= 0.0f) return rect;
    rect.x -= expand;
    rect.y -= expand;
    rect.width = max(0.0f, rect.width + expand * 2.0f);
    rect.height = max(0.0f, rect.height + expand * 2.0f);
    return rect;
}

Bound view_geometry_rect_to_bound(Rect rect) {
    return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}

bool view_geometry_bounds_intersect(Bound first, Bound second) {
    return first.left < second.right && first.right > second.left &&
           first.top < second.bottom && first.bottom > second.top;
}

RdtLogicalPoint view_geometry_text_rect_document_origin(View* view,
                                                        TextRect* rect) {
    RdtLogicalPoint point = view_geometry_node_document_origin(
        view && view->parent ? view->parent : view);
    if (rect) {
        point.x += rect->x;
        point.y += rect->y;
    }
    return point;
}

bool view_geometry_pdf_text_metrics(DomText* text, float* out_width,
                                    bool* out_copy_space) {
    if (out_width) *out_width = 0.0f;
    if (out_copy_space) *out_copy_space = false;
    if (!text || !text->parent || !text->parent->is_element()) return false;

    DomElement* element = lam::dom_require_element(text->parent);
    const char* class_name = element->get_attribute("class");
    if (!class_name || !strstr(class_name, "pdf-text-run")) return false;

    const char* width_attribute = element->get_attribute("data-pdf-width");
    float width = width_attribute
        ? (float)str_to_double_default(
            width_attribute, strlen(width_attribute), 0.0) : 0.0f;
    if (width <= 0.0f) return false;

    const char* copy_attribute = element->get_attribute("data-pdf-copy-space");
    if (out_width) *out_width = width;
    if (out_copy_space) {
        *out_copy_space = copy_attribute && strcmp(copy_attribute, "1") == 0;
    }
    return true;
}

int view_geometry_pdf_visible_end_offset(DomText* text, TextRect* rect,
                                         bool copy_space) {
    int end = rect ? rect->start_index + max(rect->length, 0) : 0;
    if (!copy_space || !text || !rect || end <= rect->start_index) return end;
    unsigned char* data = text->text_data();
    return data && data[end - 1] == ' ' ? end - 1 : end;
}

float view_geometry_text_rect_width(DomText* text, TextRect* rect) {
    float pdf_width = 0.0f;
    bool copy_space = false;
    return view_geometry_pdf_text_metrics(text, &pdf_width, &copy_space)
        ? pdf_width : rect ? rect->width : 0.0f;
}

TextRect* view_geometry_text_rect_for_offset(
        DomText* text, int byte_offset, int* out_line, bool clamp_to_last) {
    if (out_line) *out_line = 0;
    if (!text || !text->rect) return nullptr;
    TextRect* last = text->rect;
    int line = 0;
    for (TextRect* rect = text->rect; rect; rect = rect->next) {
        if (byte_offset >= rect->start_index &&
            byte_offset <= rect->start_index + rect->length) {
            if (out_line) *out_line = line;
            return rect;
        }
        last = rect;
        line++;
    }
    if (out_line && line > 0) *out_line = line - 1;
    return clamp_to_last ? last : nullptr;
}

float view_geometry_interpolate_text_x(DomText* text, TextRect* rect,
                                       int byte_offset, bool pdf_metrics) {
    if (!rect || rect->length <= 0) return rect ? rect->x : 0.0f;
    float width = rect->width;
    int end = rect->start_index + rect->length;
    if (pdf_metrics) {
        float pdf_width = 0.0f;
        bool copy_space = false;
        if (view_geometry_pdf_text_metrics(text, &pdf_width, &copy_space)) {
            width = pdf_width;
            end = view_geometry_pdf_visible_end_offset(text, rect, copy_space);
        }
    }
    int length = end - rect->start_index;
    int local = byte_offset - rect->start_index;
    if (local <= 0 || length <= 0) return rect->x;
    if (byte_offset >= end) return rect->x + width;
    return rect->x + ((float)local / (float)length) * width;
}
