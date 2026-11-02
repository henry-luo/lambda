#include "layout_ruby.hpp"
#include "../lib/tagged.hpp"
#include <float.h>

// tier-3: one scratch scope owns the virtual box tree and its column measures.
// source nodes keep their DOM parents/siblings; anonymous boxes borrow node ranges.
struct LayoutRubyBox {
    DomNode** nodes;
    int node_count;
    DomElement* element;
    LayoutRubyBox* next;
    int column;
    int span;
    bool whitespace;
    bool spanning;
    bool hidden;
    IntrinsicSizes sizes;
    float natural_start;
    float natural_width;
};

struct LayoutRubyLevel {
    DomElement* element;
    DomNode** nodes;
    int node_count;
    bool* discarded;
    LayoutRubyBox* first;
    LayoutRubyBox* last;
    LayoutRubyLevel* next;
    int box_count;
    bool annotation;
    Linebox line;
};

struct LayoutRubyColumn {
    float min_content;
    float max_content;
    bool present;
};

struct LayoutRubySegment {
    LayoutRubyLevel base;
    LayoutRubyLevel* annotations;
    LayoutRubyLevel* last_annotation;
    LayoutRubySegment* next;
    LayoutRubyColumn* columns;
    int column_count;
    float width;
};

struct LayoutRubyModel {
    LayoutRubySegment* first;
    LayoutRubySegment* last;
    DomNode** nodes;
    int node_count;
    bool* discarded;
    bool failed;
};

struct RubyBuildContext {
    LayoutContext* layout;
    ScratchScope* scope;
    LayoutRubyModel* model;
    bool initialize_contents;
};

static bool ruby_include_text(DomNode* text, DomElement*) {
    return text && text->is_text();
}

static int ruby_collect_nodes(RubyBuildContext* context, DomElement* element,
                             DomNode*** result) {
    int capacity = layout_count_flattened_item_nodes(element, true);
    if (capacity <= 0) return 0;
    *result = context->scope->array_zero<DomNode*>((size_t)capacity);
    if (!*result) { context->model->failed = true; return 0; }
    LayoutFlattenedItemPolicy policy = {};
    policy.include_text = ruby_include_text;
    policy.initialize_contents = context->initialize_contents;
    return layout_collect_flattened_item_nodes(context->layout, element,
        element->first_child, *result, capacity, &policy);
}

static CssEnum ruby_node_role(DomNode* node) {
    return node && node->is_element() ? resolve_display_value(node).inner : CSS_VALUE_FLOW;
}

static bool ruby_node_in_flow(DomNode* node) {
    return node && (!node->is_element() ||
        !layout_element_is_out_of_flow(node->as_element()));
}

static bool ruby_nodes_are_whitespace(DomNode** nodes, int count) {
    if (count <= 0) return false;
    for (int index = 0; index < count; index++) {
        if (!nodes[index]->is_text() || !is_only_whitespace(
                (const char*)nodes[index]->text_data())) return false;
    }
    return true;
}

static LayoutRubySegment* ruby_append_segment(RubyBuildContext* context) {
    LayoutRubySegment* segment = context->scope->array_zero<LayoutRubySegment>(1);
    if (!segment) { context->model->failed = true; return nullptr; }
    if (context->model->last) context->model->last->next = segment;
    else context->model->first = segment;
    context->model->last = segment;
    return segment;
}

static LayoutRubyLevel* ruby_append_annotation_level(RubyBuildContext* context,
                                                    LayoutRubySegment* segment) {
    if (!segment) return nullptr;
    LayoutRubyLevel* level = context->scope->array_zero<LayoutRubyLevel>(1);
    if (!level) { context->model->failed = true; return nullptr; }
    level->annotation = true;
    if (segment->last_annotation) segment->last_annotation->next = level;
    else segment->annotations = level;
    segment->last_annotation = level;
    return level;
}

static void ruby_append_box(RubyBuildContext* context, LayoutRubyLevel* level,
        DomNode** nodes, int count, DomElement* element, bool whitespace) {
    if (!level || count <= 0) return;
    LayoutRubyBox* box = context->scope->array_zero<LayoutRubyBox>(1);
    if (!box) { context->model->failed = true; return; }
    box->nodes = nodes;
    box->node_count = count;
    box->element = element;
    box->whitespace = whitespace;
    // CSS Ruby §2.3.2: whitespace pairs between units without consuming a unit.
    box->column = whitespace ? max(0, level->box_count * 2 - 1) : level->box_count * 2;
    box->span = 1;
    if (!whitespace) level->box_count++;
    if (level->last) level->last->next = box;
    else level->first = box;
    level->last = box;
}

static int ruby_filter_in_flow(RubyBuildContext* context, DomNode** nodes,
                              int count, DomNode*** result, int** origins) {
    *result = context->scope->array_zero<DomNode*>((size_t)count);
    *origins = context->scope->array_zero<int>((size_t)count);
    if ((!*result || !*origins) && count) { context->model->failed = true; return 0; }
    int retained = 0;
    for (int index = 0; index < count; index++) {
        if (ruby_node_in_flow(nodes[index])) {
            (*origins)[retained] = index;
            (*result)[retained++] = nodes[index];
        }
    }
    return retained;
}

static void ruby_build_explicit_level(RubyBuildContext* context,
        LayoutRubyLevel* level, DomElement* element) {
    if (!level) return;
    level->element = element;
    level->node_count = ruby_collect_nodes(context, element, &level->nodes);
    level->discarded = context->scope->array_zero<bool>((size_t)level->node_count);
    if (!level->discarded && level->node_count) { context->model->failed = true; return; }
    DomNode** nodes = nullptr;
    int* origins = nullptr;
    int count = ruby_filter_in_flow(context, level->nodes, level->node_count, &nodes, &origins);
    CssEnum unit_role = level->annotation ? CSS_VALUE_RUBY_TEXT : CSS_VALUE_RUBY_BASE;
    for (int index = 0; index < count;) {
        if (ruby_node_role(nodes[index]) == unit_role) {
            ruby_append_box(context, level, nodes + index, 1, nodes[index]->as_element(), false);
            index++;
            continue;
        }
        int first = index++;
        while (index < count && ruby_node_role(nodes[index]) != unit_role) index++;
        bool whitespace = ruby_nodes_are_whitespace(nodes + first, index - first);
        // CSS Ruby §2.2: anonymous whitespace at either level edge is discarded.
        if (whitespace && (first == 0 || index == count) && index - first != count) {
            for (int cursor = first; cursor < index; cursor++) level->discarded[origins[cursor]] = true;
            continue;
        }
        ruby_append_box(context, level, nodes + first, index - first, nullptr, whitespace);
    }
    if (level->annotation && level->first == level->last && level->first &&
        !level->first->element && !level->first->whitespace) {
        level->first->spanning = true;
    }
}

static void ruby_append_box_text(StrBuf* text, LayoutRubyBox* box) {
    for (int index = 0; index < box->node_count; index++) dom_collect_text_content(box->nodes[index], text);
}

static void ruby_resolve_annotation_hiding(LayoutRubyModel* model, DomElement* ruby) {
    StrBuf* base_text = strbuf_new();
    StrBuf* annotation_text = strbuf_new();
    if (!base_text || !annotation_text) {
        strbuf_free(base_text); strbuf_free(annotation_text);
        model->failed = true;
        return;
    }
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
        for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
            for (LayoutRubyBox* annotation = level->first; annotation; annotation = annotation->next) {
                DomElement* owner = annotation->element ? annotation->element : level->element ? level->element : ruby;
                annotation->hidden = css_prop_compute_cascaded_keyword(owner, CSS_PROPERTY_VISIBILITY,
                    CSS_VALUE_VISIBLE) == CSS_VALUE_COLLAPSE;
                if (annotation->hidden) continue;
                strbuf_reset(base_text); strbuf_reset(annotation_text);
                for (LayoutRubyBox* base = segment->base.first; base; base = base->next) {
                    if (annotation->spanning || base->column == annotation->column) ruby_append_box_text(base_text, base);
                }
                ruby_append_box_text(annotation_text, annotation);
                // CSS Ruby §2.4 compares source text before collapse/transform,
                // after pairing so hidden readings never shift later associations.
                annotation->hidden = base_text->length == annotation_text->length &&
                    (base_text->length == 0 || memcmp(base_text->str, annotation_text->str, base_text->length) == 0);
            }
        }
    }
    strbuf_free(base_text); strbuf_free(annotation_text);
}

static LayoutRubyModel layout_ruby_build_model(LayoutContext* layout, DomElement* ruby,
                                      ScratchScope* scope, bool initialize_contents = true) {
    LayoutRubyModel model = {};
    RubyBuildContext context = {layout, scope, &model, initialize_contents};
    model.node_count = ruby_collect_nodes(&context, ruby, &model.nodes);
    model.discarded = scope->array_zero<bool>((size_t)model.node_count);
    if (!model.discarded && model.node_count) { model.failed = true; return model; }
    DomNode** nodes = nullptr;
    int* origins = nullptr;
    int count = ruby_filter_in_flow(&context, model.nodes, model.node_count, &nodes, &origins);
    LayoutRubySegment* segment = nullptr;
    LayoutRubyLevel* implicit_annotation = nullptr;
    CssEnum previous = CSS_VALUE__UNDEF;
    for (int index = 0; index < count;) {
        CssEnum role = ruby_node_role(nodes[index]);
        bool internal = css_display_is_ruby_internal(role);
        int first = index++;
        if (!internal) {
            while (index < count && !css_display_is_ruby_internal(ruby_node_role(nodes[index]))) index++;
        }
        bool whitespace = !internal && ruby_nodes_are_whitespace(nodes + first, index - first);
        CssEnum following = index < count ? ruby_node_role(nodes[index]) : CSS_VALUE__UNDEF;
        if (whitespace && count != index - first &&
            (first == 0 || index == count || following == CSS_VALUE_RUBY_TEXT_CONTAINER ||
             (following == CSS_VALUE_RUBY_TEXT && previous != CSS_VALUE_RUBY_TEXT))) {
            for (int cursor = first; cursor < index; cursor++) model.discarded[origins[cursor]] = true;
            continue;
        }
        if (whitespace && previous == CSS_VALUE_RUBY_TEXT && following == CSS_VALUE_RUBY_TEXT) {
            ruby_append_box(&context, implicit_annotation, nodes + first, index - first, nullptr, true);
            continue;
        }
        if (role == CSS_VALUE_RUBY_TEXT_CONTAINER) {
            if (!segment) segment = ruby_append_segment(&context);
            LayoutRubyLevel* level = ruby_append_annotation_level(&context, segment);
            ruby_build_explicit_level(&context, level, nodes[first]->as_element());
            implicit_annotation = nullptr;
        } else if (role == CSS_VALUE_RUBY_TEXT) {
            if (!segment) segment = ruby_append_segment(&context);
            if (!implicit_annotation) implicit_annotation = ruby_append_annotation_level(&context, segment);
            ruby_append_box(&context, implicit_annotation, nodes + first, 1, nodes[first]->as_element(), false);
        } else {
            bool new_segment = !segment || role == CSS_VALUE_RUBY_BASE_CONTAINER ||
                segment->annotations || segment->base.element ||
                (segment->base.first && segment->base.box_count == 0);
            if (new_segment) segment = ruby_append_segment(&context);
            if (!segment) break;
            implicit_annotation = nullptr;
            if (role == CSS_VALUE_RUBY_BASE_CONTAINER) {
                ruby_build_explicit_level(&context, &segment->base, nodes[first]->as_element());
            } else {
                ruby_append_box(&context, &segment->base, nodes + first, index - first,
                    role == CSS_VALUE_RUBY_BASE ? nodes[first]->as_element() : nullptr, whitespace);
            }
        }
        previous = role == CSS_VALUE_FLOW ? CSS_VALUE_RUBY_BASE : role;
    }
    if (!model.failed) ruby_resolve_annotation_hiding(&model, ruby);
    return model;
}

static void ruby_resolve_segment_columns(LayoutRubySegment* segment, ScratchScope* scope,
                                        bool* failed) {
    int units = segment->base.box_count;
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
        if (!(level->first && level->first->spanning)) units = max(units, level->box_count);
    }
    units = max(units, 1);
    segment->column_count = units * 2 - 1;
    // the same model transitions from intrinsic constraints to measured used columns.
    if (segment->columns) memset(segment->columns, 0, sizeof(LayoutRubyColumn) * segment->column_count);
    else segment->columns = scope->array_zero<LayoutRubyColumn>((size_t)segment->column_count);
    if (!segment->columns) { *failed = true; return; }
    auto include_level = [&](LayoutRubyLevel* level) {
        for (LayoutRubyBox* box = level->first; box; box = box->next) {
            if (box->spanning) { box->column = 0; box->span = segment->column_count; continue; }
            if (box->column >= segment->column_count) continue;
            LayoutRubyColumn* column = &segment->columns[box->column];
            column->min_content = max(column->min_content, box->sizes.min_content);
            column->max_content = max(column->max_content, box->sizes.max_content);
            column->present = true;
        }
    };
    include_level(&segment->base);
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) include_level(level);
    for (int index = 0; index < segment->column_count; index += 2) segment->columns[index].present = true;
    // CSS Ruby §3.1.1: spanning excess is shared by the columns it spans.
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
        LayoutRubyBox* box = level->first;
        if (!box || !box->spanning) continue;
        float min_sum = 0.0f, max_sum = 0.0f, columns = 0.0f;
        for (int index = 0; index < segment->column_count; index++) {
            min_sum += segment->columns[index].min_content;
            max_sum += segment->columns[index].max_content;
            if (segment->columns[index].present) columns += 1.0f;
        }
        float min_extra = max(0.0f, box->sizes.min_content - min_sum) / columns;
        float max_extra = max(0.0f, box->sizes.max_content - max_sum) / columns;
        for (int index = 0; index < segment->column_count; index++) {
            if (!segment->columns[index].present) continue;
            segment->columns[index].min_content += min_extra;
            segment->columns[index].max_content += max_extra;
        }
    }
    segment->width = 0.0f;
    for (int index = 0; index < segment->column_count; index++) {
        segment->width += segment->columns[index].max_content;
    }
}

static void layout_ruby_resolve_columns(LayoutRubyModel* model, ScratchScope* scope) {
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
        ruby_resolve_segment_columns(segment, scope, &model->failed);
    }
}

static void ruby_measure_level(LayoutContext* context, LayoutRubyLevel* level) {
    LayoutFontScope font_scope(context);
    if (level->element) {
        // materialize the level's inherited font before measuring anonymous text ranges.
        (void)calculate_node_intrinsic_widths(context, level->element);
        if (level->element->font) setup_font(context->ui_context, &context->font, level->element->font);
    }
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        if (box->hidden) continue;
        if (box->whitespace) {
            box->sizes.max_content = white_space_preserves_space_advance(get_white_space_value(box->nodes[0]))
                ? calculate_max_content_width(context, box->nodes[0])
                : layout_intrinsic_collapsed_space_width(context, box->nodes[0]);
            continue;
        }
        LayoutIntrinsicMinRun minimum;
        float separator_width = 0.0f;
        bool pending_separator = false;
        for (int index = 0; index < box->node_count; index++) {
            DomNode* node = box->nodes[index];
            if (!box->element && layout_intrinsic_node_is_collapsible_space_only(node)) {
                if (!pending_separator && box->sizes.max_content > 0.0f) {
                    separator_width = layout_intrinsic_collapsed_space_width(context, node);
                    pending_separator = true;
                }
                layout_intrinsic_break_min_run(&minimum);
                continue;
            }
            IntrinsicSizes sizes = calculate_node_intrinsic_widths(context, node);
            if (pending_separator && sizes.max_content > 0.0f) {
                box->sizes.max_content += separator_width;
                pending_separator = false;
            }
            box->sizes.max_content += sizes.max_content;
            layout_intrinsic_append_min_run(context, &minimum, node, sizes, false);
        }
        box->sizes.min_content = minimum.minimum;
        box->sizes.first_segment_min = minimum.first;
        box->sizes.last_segment_min = minimum.tail;
        box->sizes.interior_min_content = minimum.interior;
        box->sizes.has_min_content_break = minimum.has_break;
        box->sizes.has_used_content_break = minimum.has_used_break;
    }
}

static LayoutRubyBox* ruby_box_in_column(LayoutRubyLevel* level, int column) {
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        if (!box->spanning && box->column == column) return box;
    }
    return nullptr;
}

static void ruby_minimum_last_source(LayoutIntrinsicMinRun* run, LayoutRubyBox* box) {
    for (int index = box ? box->node_count - 1 : -1; index >= 0; index--) {
        if (layout_inline_text_edge_codepoint(box->nodes[index], true)) {
            run->previous = box->nodes[index];
            return;
        }
    }
}

static void ruby_append_minimum(LayoutContext* context, LayoutIntrinsicMinRun* run,
        LayoutRubyBox* base, const IntrinsicSizes& widths) {
    if (base && base->node_count > 0) {
        layout_intrinsic_append_min_run(context, run, base->nodes[0], widths, false);
        ruby_minimum_last_source(run, base);
    } else layout_intrinsic_append_min_segments(run, widths, false);
}

static bool ruby_box_allows_content_break(LayoutRubyBox* box, bool used_breaks) {
    return box && (box->sizes.has_min_content_break ||
        (used_breaks && box->sizes.has_used_content_break));
}

static void ruby_segment_break_run(LayoutContext* context, LayoutRubySegment* segment,
                                   LayoutIntrinsicMinRun* run, bool used_breaks) {
    bool unbroken_span = false;
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
        LayoutRubyBox* box = level->first;
        unbroken_span |= box && !box->hidden && box->spanning && box->sizes.max_content > 0.0f &&
            !ruby_box_allows_content_break(box, used_breaks);
    }
    if (unbroken_span) {
        // CSS Ruby §3.4.1: an unbroken spanning annotation binds its base columns.
        ruby_append_minimum(context, run, segment->base.first, {segment->width, segment->width});
        ruby_minimum_last_source(run, segment->base.last);
        return;
    }
    for (int column = 0; column < segment->column_count; column++) {
        LayoutRubyBox* base = ruby_box_in_column(&segment->base, column);
        if (base && base->whitespace) {
            layout_intrinsic_break_min_run(run);
            continue;
        }
        if (!segment->columns[column].present) continue;
        IntrinsicSizes widths = base ? base->sizes : IntrinsicSizes{};
        widths.min_content = segment->columns[column].min_content;
        widths.max_content = segment->columns[column].max_content;
        bool can_break = ruby_box_allows_content_break(base, used_breaks);
        for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
            LayoutRubyBox* annotation = ruby_box_in_column(level, column);
            if (!annotation || annotation->sizes.max_content <= 0.0f) continue;
            // CSS Ruby §3.4.2 requires opportunities in every parallel box.
            can_break &= ruby_box_allows_content_break(annotation, used_breaks);
            widths.first_segment_min = max(widths.first_segment_min,
                annotation->sizes.first_segment_min);
            widths.last_segment_min = max(widths.last_segment_min,
                annotation->sizes.last_segment_min);
            widths.interior_min_content = max(widths.interior_min_content,
                annotation->sizes.interior_min_content);
        }
        if (!can_break) {
            widths.min_content = widths.max_content;
            widths.first_segment_min = widths.last_segment_min = widths.min_content;
            widths.interior_min_content = 0.0f;
        }
        // the used pass carries availability only; its widths never replace CSS intrinsic sizes.
        widths.has_min_content_break = can_break;
        widths.has_used_content_break = can_break;
        ruby_append_minimum(context, run, base, widths);
    }
}

static IntrinsicSizes ruby_measure_model(LayoutContext* context, DomElement* ruby,
        LayoutRubyModel* model, ScratchScope* scope) {
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
        ruby_measure_level(context, &segment->base);
        for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
            ruby_measure_level(context, level);
        }
    }
    layout_ruby_resolve_columns(model, scope);
    IntrinsicSizes sizes = {};
    if (model->failed) { log_error("ruby model: measurement scratch allocation failed"); return sizes; }
    LayoutIntrinsicMinRun minimum;
    LayoutIntrinsicMinRun used_breaks;
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
        sizes.max_content += segment->width;
        ruby_segment_break_run(context, segment, &minimum, false);
        ruby_segment_break_run(context, segment, &used_breaks, true);
    }
    sizes.min_content = minimum.minimum;
    sizes.first_segment_min = minimum.first;
    sizes.last_segment_min = minimum.tail;
    sizes.interior_min_content = minimum.interior;
    sizes.has_min_content_break = minimum.has_break;
    sizes.has_used_content_break = used_breaks.has_break;
    if (get_text_wrap_mode_value(ruby, context) == CSS_VALUE_NOWRAP) {
        sizes.min_content = sizes.max_content;
        sizes.first_segment_min = sizes.last_segment_min = sizes.min_content;
        sizes.has_min_content_break = false;
        sizes.has_used_content_break = false;
    }
    return sizes;
}

IntrinsicSizes layout_ruby_intrinsic_sizes(LayoutContext* context, DomElement* ruby) {
    ScratchScope scope(&context->scratch);
    LayoutRubyModel model = layout_ruby_build_model(context, ruby, &scope);
    if (model.failed) { log_error("ruby model: intrinsic normalization allocation failed"); return {}; }
    return ruby_measure_model(context, ruby, &model, &scope);
}

bool ruby_has_text_box_trim_ancestor(const ViewSpan* ruby, uint8_t trim) {
    for (const DomNode* node = ruby ? static_cast<const DomNode*>(ruby) : nullptr;
         node; node = node->parent) {
        if (!node->is_element()) continue;
        const DomElement* element = node->as_element();
        if (element->blk && (element->block()->text_box_trim & trim) != 0) {
            return true;
        }
    }
    return false;
}

void layout_apply_ruby_column_geometry(ViewSpan* span) {
    if (!span || !span->ext || !span->ext->has_ruby_column_geometry ||
        (span->display.inner != CSS_VALUE_RUBY &&
         !css_display_is_ruby_internal(span->display.inner))) return;
    // the inset is relative to freshly collected child bounds, so line alignment
    // can translate the whole ruby without stale absolute column anchors.
    span->x += span->ext->ruby_column_start_offset;
    span->width += span->ext->ruby_column_end_offset - span->ext->ruby_column_start_offset;
    if (span->ext->has_empty_ruby_base) {
        span->height = max(span->height, span->ext->ruby_empty_base_block_size);
    }
}

static void ruby_publish_geometry(DomElement* element, float start, float width) {
    ViewSpan* span = lam::view_as<RDT_VIEW_INLINE>(element);
    if (!span) return;
    DomElementExt* ext = element->ensure_ext();
    if (!ext) return;
    ext->has_ruby_column_geometry = false;
    recompute_span_bounding_box_after_line_layout(span, false,
        element->font ? element->fontp()->font_handle : nullptr);
    ext->ruby_column_start_offset = start - span->x;
    ext->ruby_column_end_offset = start + width - (span->x + span->width);
    ext->has_ruby_column_geometry = true;
    layout_apply_ruby_column_geometry(span);
}

static void ruby_include_bounds(Rect* result, bool* found, Rect bounds) {
    if (!*found) { *result = bounds; *found = true; return; }
    float right = max(result->x + result->width, bounds.x + bounds.width);
    float bottom = max(result->y + result->height, bounds.y + bounds.height);
    result->x = min(result->x, bounds.x); result->y = min(result->y, bounds.y);
    result->width = right - result->x; result->height = bottom - result->y;
}

template <typename Visit, typename Leave>
static void ruby_walk_box(LayoutRubyBox* box, Visit visit, Leave leave,
                           bool include_nested_ruby = false) {
    for (int index = 0; index < box->node_count; index++) {
        View* root = static_cast<View*>(box->nodes[index]);
        auto enter = [&](View* view, int depth) {
            if (view->view_type == RDT_VIEW_NONE ||
                (!include_nested_ruby && view != static_cast<View*>(box->element) &&
                 layout_ruby_annotation_node(static_cast<DomNode*>(view)))) return false;
            bool nested_ruby = view != static_cast<View*>(box->element) && view->is_element() &&
                view->as_element()->display.inner == CSS_VALUE_RUBY;
            bool descend = view->view_type == RDT_VIEW_INLINE && (include_nested_ruby || !nested_ruby) &&
                lam::view_require_element(view)->first_placed_child();
            visit(view, !descend, depth);
            return descend;
        };
        layout_walk_view_tree(root, enter, leave, true, 0, false, root->next());
    }
}

static Rect ruby_box_bounds(LayoutRubyBox* box) {
    Rect result = {};
    if (box->hidden) return result;
    bool found = false;
    for (int index = 0; index < box->node_count; index++) {
        View* view = static_cast<View*>(box->nodes[index]);
        if (view->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(view) ||
            (view->view_type == RDT_VIEW_TEXT && view_is_collapsed_whitespace_text(view))) continue;
        ruby_include_bounds(&result, &found, {view->x, view->y, view->width, view->height});
    }
    if (!found) return result;
    // CSS Ruby §3.1.2 includes nested containers in the block-axis envelope,
    // without changing the source inline's font box or its column allocation.
    auto include_nested = [&](View* view, bool, int depth) {
        if (depth == 0 || (view->view_type == RDT_VIEW_TEXT && view_is_collapsed_whitespace_text(view))) return;
        CssEnum role = ruby_node_role(view);
        CssEnum parent_role = ruby_node_role(view->parent);
        bool box_root = parent_role == CSS_VALUE_RUBY ||
            parent_role == CSS_VALUE_RUBY_BASE_CONTAINER || parent_role == CSS_VALUE_RUBY_TEXT_CONTAINER;
        if (role != CSS_VALUE_RUBY && !css_display_is_ruby_internal(role) && !box_root) return;
        float bottom = max(result.y + result.height, view->y + view->height);
        result.y = min(result.y, view->y);
        result.height = bottom - result.y;
    };
    ruby_walk_box(box, include_nested, [](View*, int) {}, true);
    return result;
}

static Rect ruby_level_bounds(LayoutRubyLevel* level) {
    Rect result = {};
    bool found = false;
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        Rect bounds = ruby_box_bounds(box);
        if (bounds.width <= 0.0f && bounds.height <= 0.0f) continue;
        ruby_include_bounds(&result, &found, bounds);
    }
    return result;
}

static void ruby_align_box(LayoutRubyBox* box, DomElement* inheritance_parent,
                           float start, float width) {
    float shift = start - box->natural_start;
    for (int index = 0; index < box->node_count; index++) {
        layout_shift_view_tree(static_cast<View*>(box->nodes[index]), shift, 0.0f);
    }
    float extra = max(0.0f, width - box->natural_width);
    View* first_leaf = nullptr;
    View* last_leaf = nullptr;
    TextRect* first_rect = nullptr;
    TextRect* last_rect = nullptr;
    auto collect = [&](View* view, bool leaf, int) {
        if (!leaf) return;
        if (!first_leaf) first_leaf = view;
        last_leaf = view;
        if (view->view_type == RDT_VIEW_TEXT) {
            for (TextRect* rect = lam::view_require_text(view)->rect; rect; rect = rect->next) {
                if (!first_rect) first_rect = rect;
                last_rect = rect;
            }
        }
    };
    auto no_leave = [](View*, int) {};
    ruby_walk_box(box, collect, no_leave);
    int opportunities = 0;
    auto count = [&](View* view, bool, int) {
        if (view->view_type != RDT_VIEW_TEXT) return;
        ViewText* text = lam::view_require_text(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            opportunities += count_rendered_justify_opportunities(text, rect, rect == last_rect);
        }
    };
    ruby_walk_box(box, count, no_leave);
    CssEnum align = css_prop_compute_cascaded_keyword(box->element ? box->element : inheritance_parent,
        CSS_PROPERTY_RUBY_ALIGN, CSS_VALUE_SPACE_AROUND);
    float per_gap = 0.0f, leading = 0.0f, trailing = 0.0f;
    if (align != CSS_VALUE_START) {
        if ((align == CSS_VALUE_SPACE_AROUND || align == CSS_VALUE_SPACE_BETWEEN) && opportunities > 0) {
            per_gap = extra / (float)(opportunities + (align == CSS_VALUE_SPACE_AROUND ? 1 : 0));
            if (align == CSS_VALUE_SPACE_AROUND) leading = trailing = per_gap * 0.5f;
        } else {
            leading = trailing = extra * 0.5f;
        }
    } else trailing = extra;
    bool leading_on_text = first_leaf && first_leaf->view_type == RDT_VIEW_TEXT;
    bool trailing_on_text = last_leaf && last_leaf->view_type == RDT_VIEW_TEXT;
    float offset = leading_on_text ? 0.0f : leading;
    auto align_view = [&](View* view, bool leaf, int) {
        if (!leaf) return;
        if (view->view_type != RDT_VIEW_TEXT) {
            layout_shift_view_tree(view, offset, 0.0f);
            return;
        }
        ViewText* text = lam::view_require_text(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            rect->x += offset;
            float before = rect == first_rect && leading_on_text ? leading : 0.0f;
            float after = rect == last_rect && trailing_on_text ? trailing : 0.0f;
            float justified = (float)count_rendered_justify_opportunities(text, rect, rect == last_rect) * per_gap;
            float added = before + justified + after;
            rect->width += added;
            rect->ruby_alignment_offset += before;
            rect->ruby_alignment_advance += added;
            if (per_gap > 0.0f) rect->justification_spacing = per_gap;
            offset += added;
        }
        adjust_text_bounds(text);
    };
    auto finish = [](View* view, int) {
        if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
            recompute_span_bounding_box_after_line_layout(span, false,
                span->font ? span->fontp()->font_handle : nullptr);
        }
    };
    ruby_walk_box(box, align_view, finish);
    if (box->element) ruby_publish_geometry(box->element, start, width);
}

static void ruby_measure_used_level(LayoutRubyLevel* level) {
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        Rect bounds = ruby_box_bounds(box);
        box->natural_start = bounds.x;
        box->natural_width = bounds.width;
        box->sizes.min_content = box->sizes.max_content = bounds.width;
    }
}

static float ruby_column_range_width(LayoutRubySegment* segment, int first, int count) {
    float width = 0.0f;
    for (int index = first; index < min(first + count, segment->column_count); index++) {
        width += segment->columns[index].max_content;
    }
    return width;
}

static void ruby_place_level(LayoutRubySegment* segment, LayoutRubyLevel* level,
                              DomElement* ruby, float start) {
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        if (box->hidden) continue;
        float box_start = start + ruby_column_range_width(segment, 0, box->column);
        float width = ruby_column_range_width(segment, box->column, box->span);
        ruby_align_box(box, level->element ? level->element : ruby, box_start, width);
    }
    if (level->element) ruby_publish_geometry(level->element, start, segment->width);
}

static void ruby_shift_level(LayoutRubyLevel* level, float offset_y, bool contents_only = false) {
    auto shift = [&](View* view) {
        if (contents_only && view->is_group()) layout_shift_view_children(view, 0.0f, offset_y);
        else layout_shift_view_tree(view, 0.0f, offset_y);
    };
    if (level->element) {
        shift(level->element);
        return;
    }
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        for (int index = 0; index < box->node_count; index++) {
            shift(static_cast<View*>(box->nodes[index]));
        }
    }
}

template <typename Contribute>
static void ruby_position_annotation_levels(ViewSpan* ruby, LayoutRubySegment* segment,
        Rect base_bounds, bool after_line, Contribute contribute) {
    float over_edge = base_bounds.y, under_edge = base_bounds.y + base_bounds.height;
    bool previous_under = true;
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
        CssEnum position = css_prop_compute_cascaded_keyword(level->element ? level->element : ruby,
            CSS_PROPERTY_RUBY_POSITION, CSS_VALUE_ALTERNATE);
        bool under = position == CSS_VALUE_UNDER || (position == CSS_VALUE_ALTERNATE && !previous_under);
        Rect annotation = ruby_level_bounds(level);
        float target = under ? under_edge : over_edge - annotation.height;
        bool preserve_box = after_line && !under && ruby_has_text_box_trim_ancestor(ruby, TEXT_BOX_TRIM_START);
        ruby_shift_level(level, target - annotation.y, preserve_box);
        annotation.y = target;
        if (under) under_edge = target + annotation.height;
        else over_edge = target;
        previous_under = under;
        contribute(level, annotation, under);
    }
}

static float ruby_base_content_baseline(LayoutContext* context, LayoutRubyLevel* level, Rect bounds) {
    bool found = false;
    float baseline = 0.0f;
    auto collect = [&](View* view, bool leaf, int) {
        if (!leaf || layout_view_is_out_of_flow(view)) return;
        float candidate = view->y + view->height;
        if (ViewText* text = lam::view_as<RDT_VIEW_TEXT>(view)) {
            float ascent = radiant::compute_font_baseline_ascender(context, text->font, false, 0.0f);
            if (text->rect) candidate = text->rect->y + ascent;
        } else if (ViewBlock* block = lam::view_as_block(view)) {
            candidate = view->y + radiant::compute_element_last_baseline(context, block, true);
        } else if (view->is_element()) {
            candidate = view->y + radiant::compute_font_baseline_ascender(
                context, view->as_element()->font, false, view->height);
        }
        baseline = found ? max(baseline, candidate) : candidate;
        found = true;
    };
    auto no_leave = [](View*, int) {};
    for (LayoutRubyBox* box = level->first; box; box = box->next) ruby_walk_box(box, collect, no_leave);
    return found ? baseline - bounds.y : bounds.height;
}

static void ruby_contribute_line_metrics(LayoutContext* context, const Linebox* base_line,
        LayoutRubyLevel* base, float base_baseline, float base_line_height, Rect bounds,
        float over, float under) {
    if (base_line->has_initial_letter && over <= 0.0f && under <= 0.0f) return;
    if (over > 0.0f && base_line->has_initial_letter) {
        float over_shift = max(0.0f, over - context->block.lead_y);
        float required_ascender = base_line->max_ascender + over_shift - base_line->initial_letter_origin_advance;
        context->line.max_ascender = max(context->line.max_ascender, required_ascender);
        context->line.ruby_annotation_over_shift = max(context->line.ruby_annotation_over_shift, over_shift);
        float advance_shift = max(0.0f, over_shift - base_line->initial_letter_origin_advance);
        context->line.ruby_annotation_min_line_height = max(
            context->line.ruby_annotation_min_line_height, base_line_height + advance_shift);
        if (under <= 0.0f) return;
        over = 0.0f;
    }
    // CSS Ruby §3.6 reserves repeated-line spacing; annotations may overflow their own line box.
    float extra = max(0.0f, bounds.height + over + under - base_line_height);
    if (extra <= 0.0f) return;
    float half_leading = max(0.0f, (base_line_height - bounds.height) * 0.5f);
    float over_excess = max(0.0f, over - half_leading);
    float under_excess = max(0.0f, under - half_leading);
    float excess = over_excess + under_excess;
    if (excess <= 0.0f) {
        // a visible zero-height annotation still pairs with the base's physical font box.
        float baseline = ruby_base_content_baseline(context, base, bounds);
        over_excess = max(0.0f, baseline - base_baseline);
        under_excess = max(0.0f, bounds.height - baseline - max(0.0f, base_line_height - base_baseline));
        excess = over_excess + under_excess;
    }
    float before = base_baseline + (excess > 0.0f ? extra * over_excess / excess : 0.0f);
    float after = max(0.0f, base_line_height - base_baseline) +
        (excess > 0.0f ? extra * under_excess / excess : 0.0f);
    context->line.max_ascender = max(context->line.max_ascender, before);
    context->line.max_descender = max(context->line.max_descender, after);
    context->line.ruby_annotation_min_line_height = max(context->line.ruby_annotation_min_line_height, before + after);
}

struct RubyLevelScope {
    LayoutContext* context;
    LayoutRubyLevel* previous;
    RubyLevelScope(LayoutContext* layout, LayoutRubyLevel* level)
        : context(layout), previous(layout->active_ruby_level.get()) { layout->active_ruby_level = lam::up(level); }
    ~RubyLevelScope() { context->active_ruby_level = lam::up(previous); }
};

bool layout_ruby_active_level_children(LayoutContext* context, ViewSpan* span) {
    LayoutRubyLevel* level = context->active_ruby_level.get();
    if (!level || level->element != span) return false;
    for (int index = 0; index < level->node_count; index++) {
        if (level->discarded[index]) layout_suppress_ignorable_container_text(level->nodes[index]);
        else if (!level->nodes[index]->ruby_annotation_hidden) layout_flow_node(context, level->nodes[index]);
    }
    return true;
}

static void ruby_clear_element_geometry(DomElement* element) {
    if (!element || !element->ext) return;
    element->ext->has_ruby_column_geometry = false;
    element->ext->has_empty_ruby_base = false;
}

static void ruby_clear_level_geometry(LayoutRubyLevel* level) {
    ruby_clear_element_geometry(level->element);
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        ruby_clear_element_geometry(box->element);
        for (int index = 0; index < box->node_count; index++) box->nodes[index]->ruby_annotation_hidden = false;
    }
}

static void ruby_suppress_annotation_box(LayoutContext* context, LayoutRubyBox* box) {
    auto descend = [](View* view, int) { return view->is_element(); };
    auto suppress = [&](View* view, int) {
        if (DomText* text = view->as_text()) {
            context->doc->view_tree->recycle_text_rects(text->rect);
            text->rect = nullptr;
        } else if (DomElement* element = view->as_element()) {
            element->content_width = element->content_height = 0.0f;
            ruby_clear_element_geometry(element);
        }
        view->view_type = RDT_VIEW_NONE;
        layout_set_view_geometry(view, 0.0f, 0.0f, 0.0f, 0.0f);
    };
    for (int index = 0; index < box->node_count; index++) {
        View* root = box->nodes[index];
        layout_walk_view_tree(root, descend, suppress, false, 0, true, root->next());
        root->ruby_annotation_hidden = true;
    }
}

static void ruby_layout_annotation_level(LayoutContext* context, LayoutRubyLevel* level,
                                          const Linebox* base_line) {
    LayoutContextScope state(context);
    LayoutViewScope view(context);
    RubyLevelScope level_scope(context, level);
    context->block.inline_box_metrics = nullptr;
    line_init(context, base_line->left, FLT_MAX);
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        if (box->hidden) ruby_suppress_annotation_box(context, box);
    }
    if (level->element) layout_flow_node(context, level->element);
    else {
        for (LayoutRubyBox* box = level->first; box; box = box->next) {
            if (box->hidden) continue;
            for (int index = 0; index < box->node_count; index++) layout_flow_node(context, box->nodes[index]);
        }
    }
    level->line = context->line;
    ruby_measure_used_level(level);
}

static void ruby_align_annotation_model(ViewSpan* ruby,
        LayoutRubyModel* model, Rect empty_base) {
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
        Rect base = ruby_level_bounds(&segment->base);
        if (base.height <= 0.0f) base = empty_base;
        auto no_contribution = [](LayoutRubyLevel*, Rect, bool) {};
        ruby_position_annotation_levels(ruby, segment, base, true, no_contribution);
    }
}

static void ruby_finish_block_line(LayoutContext* context) {
    if (context->line.is_line_start) return;
    context->line.is_last_line = true;
    line_break(context);
}

void layout_ruby_children(LayoutContext* context, ViewSpan* ruby) {
    ScratchScope scope(&context->scratch);
    LayoutRubyModel model = layout_ruby_build_model(context, ruby, &scope);
    if (model.failed) { log_error("ruby model: used normalization allocation failed"); return; }
    DomElementExt* ext = ruby->ensure_ext();
    ruby_clear_element_geometry(ruby);
    bool inline_root = ruby->view_type == RDT_VIEW_INLINE;
    float start = context->line.advance_x;
    float preceding_space = context->line.has_space
        ? layout_measure_space_advance(context, font_box_handle(&context->font), context->font.style) : 0.0f;
    bool has_annotations = false;
    for (LayoutRubySegment* segment = model.first; segment; segment = segment->next) {
        ruby_clear_level_geometry(&segment->base);
        for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
            has_annotations = true;
            ruby_clear_level_geometry(level);
        }
    }
    bool parallel_wraps = true;
    if (has_annotations) {
        parallel_wraps = ruby_measure_model(context, ruby, &model, &scope).has_used_content_break;
        if (model.failed) return;
    }
    {
        // CSS Ruby §3.4: soft breaks require an opportunity in every parallel box.
        LayoutInlineKeepScope parallel_flow(context, !parallel_wraps);
        for (int index = 0; index < model.node_count; index++) {
            DomNode* node = model.nodes[index];
            if (model.discarded[index]) { layout_suppress_ignorable_container_text(node); continue; }
            if (layout_ruby_annotation_node(node)) continue;
            LayoutRubyLevel* base_level = nullptr;
            if (ruby_node_role(node) == CSS_VALUE_RUBY_BASE_CONTAINER) {
                for (LayoutRubySegment* segment = model.first; segment; segment = segment->next) {
                    if (segment->base.element == node) { base_level = &segment->base; break; }
                }
            }
            RubyLevelScope base_scope(context, base_level);
            layout_flow_node(context, node);
        }
    }
    // without annotation levels, base flow already owns each line's widths and cursors.
    if (!has_annotations) {
        if (!inline_root) ruby_finish_block_line(context);
        return;
    }
    float natural_advance = context->line.advance_x - start;
    if (inline_root) compute_span_bounding_box(ruby, false, font_box_handle(&context->font));
    float font_height = font_get_cell_height(font_box_handle(&context->font));
    float base_top = inline_root ? ruby->y : context->block.advance_y + context->block.lead_y;
    float base_height = inline_root ? ruby->height : font_height;
    Linebox base_line = context->line;
    bool base_has_content = false, annotations_have_content = false;
    for (LayoutRubySegment* segment = model.first; segment; segment = segment->next) {
        ruby_measure_used_level(&segment->base);
        Rect bounds = ruby_level_bounds(&segment->base);
        base_has_content |= bounds.width > 0.0f || bounds.height > 0.0f;
        for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
            ruby_layout_annotation_level(context, level, &base_line);
            annotations_have_content |= level->line.start_view.get() != nullptr ||
                level->line.max_atomic_inline_height > 0.0f;
        }
    }
    if (!base_has_content && annotations_have_content) {
        // CSS Ruby §2.3.1/§3.1.2 supplies an empty base, with the inherited font strut.
        contribute_inline_strut(context, ruby, inline_root ? ruby : nullptr);
        if (!context->line.start_view) context->line.start_view = lam::up(inline_root
            ? static_cast<View*>(ruby) : ruby->first_placed_child());
        context->line.is_line_start = false;
    }
    base_line = context->line;
    float base_line_height = 0.0f;
    float base_baseline = line_baseline_position(context, &base_line_height);
    if (!base_has_content && annotations_have_content) {
        float ascender = 0.0f, descender = 0.0f;
        font_get_content_area_split(font_box_handle(&context->font), &ascender, &descender);
        base_top = layout_inline_font_box_y(context, ruby, context->block.line_height,
            ascender, descender, base_baseline, 0.0f, 0.0f);
        if (inline_root) {
            LayoutInlineDecorationEdges edges = layout_inline_decoration_edges(ruby);
            ruby->y = base_top - edges.top;
        }
        base_height = font_height;
    }
    layout_ruby_resolve_columns(&model, &scope);
    if (model.failed) { log_error("ruby model: used-layout scratch allocation failed"); return; }
    float overhang = 0.0f;
    if (model.first && model.first->base.first) {
        float extra = model.first->columns[0].max_content - model.first->base.first->natural_width;
        overhang = min(max(0.0f, extra * 0.5f), preceding_space);
    }
    float cursor = start - overhang;
    bool has_base = false;
    for (LayoutRubySegment* segment = model.first; segment; segment = segment->next) {
        ruby_place_level(segment, &segment->base, ruby, cursor);
        Rect base_bounds = ruby_level_bounds(&segment->base);
        if (base_bounds.height > 0.0f) has_base = true;
        else base_bounds = {cursor, base_top, segment->width, max(base_height, font_height)};
        for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
            ruby_place_level(segment, level, ruby, cursor);
        }
        float over_extent = 0.0f, under_extent = 0.0f;
        bool visible_annotation = false;
        auto contribute = [&](LayoutRubyLevel* level, Rect annotation, bool under) {
            visible_annotation |= annotation.width > 0.0f || annotation.height > 0.0f;
            if (!ruby_has_text_box_trim_ancestor(ruby, under ? TEXT_BOX_TRIM_END : TEXT_BOX_TRIM_START)) {
                float extent = max(annotation.height, level->line.max_atomic_inline_height);
                if (annotation.height <= 0.0f) return;
                if (under) under_extent = max(under_extent,
                    annotation.y + extent - base_bounds.y - base_bounds.height);
                else over_extent = max(over_extent,
                    base_bounds.y - annotation.y + extent - annotation.height);
            }
        };
        ruby_position_annotation_levels(ruby, segment, base_bounds, false, contribute);
        if (visible_annotation) ruby_contribute_line_metrics(context, &base_line, &segment->base,
            base_baseline, base_line_height, base_bounds, over_extent, under_extent);
        cursor += segment->width;
    }
    float advance = cursor - start;
    context->line.advance_x += advance - natural_advance;
    if (ext) {
        ext->has_empty_ruby_base = !has_base;
        ext->ruby_empty_base_block_size = font_height;
    }
    if (inline_root) {
        // column geometry owns both decorations; margins remain external to the inline's border box.
        bool rtl = context->block.direction == CSS_VALUE_RTL;
        float before = layout_inline_span_edge_extent(ruby, rtl, true, false);
        float after = layout_inline_span_edge_extent(ruby, rtl, false, false);
        ruby_publish_geometry(ruby, start - before, advance + before + after);
    }
    else if (!context->line.is_line_start) {
        // finish the anonymous inline while its normalized model remains in scratch.
        float ascender = 0.0f, descender = 0.0f;
        font_get_content_area_split(font_box_handle(&context->font), &ascender, &descender);
        float baseline = line_baseline_position(context, nullptr);
        Rect empty_base = {start, context->block.advance_y + baseline - ascender, advance, font_height};
        ruby_finish_block_line(context);
        ruby_align_annotation_model(ruby, &model, empty_base);
    }
}

void layout_ruby_align_annotations_after_line(LayoutContext* context, ViewSpan* ruby) {
    ScratchScope scope(&context->scratch);
    LayoutRubyModel model = layout_ruby_build_model(context, ruby, &scope, false);
    if (model.failed) { log_error("ruby model: final alignment scratch allocation failed"); return; }
    ruby_align_annotation_model(ruby, &model,
        {ruby->x, ruby->y, ruby->width, ruby->height});
}

float layout_ruby_intrinsic_height(LayoutContext* context, DomElement* ruby, float available_width) {
    // D4.5.1v4: speculative flow must restore selected fragments as well as scalar boxes.
    ScratchScope counter_scratch(&context->scratch);
    CounterCheckpoint counters;
    if (!counters.enter(context->counter_context, &context->scratch, &counter_scratch.mark)) {
        log_error("ruby height: cannot isolate generated effects");
        return 0.0f;
    }
    radiant::LayoutFlowResult* selected = radiant::layout_flow_result_capture(context, ruby);
    if (!selected) { log_error("ruby height: cannot preserve selected flow"); return 0.0f; }
    radiant::LayoutMeasureScope measure(context, ruby);
    LayoutViewScope view(context);
    ViewBlock* block = lam::unsafe_view_block_element_storage(ruby);
    float content_width = available_width;
    if (layout_axis_has_given_size(block, true) &&
        !layout_axis_uses_intrinsic_size(block->blk, LAYOUT_AXIS_X)) {
        content_width = layout_content_size_if_border_box(block,
            layout_axis_given_size(block->block(), LAYOUT_AXIS_X), true);
    }
    content_width = max(content_width, 0.0f);
    block->view_type = RDT_VIEW_BLOCK;
    block->width = layout_border_size_from_content_box(block, content_width, true);
    block->height = layout_border_size_from_content_box(block, 0.0f, false);
    BlockContext parent = context->block;
    layout_setup_assigned_block_context(context, block, &parent, content_width, -1.0f);
    context->view = lam::up(static_cast<View*>(block));
    context->elmt = lam::up(static_cast<DomNode*>(ruby));
    setup_line_height(context, block);
    layout_setup_block_font_metrics(context);
    context->block.block_container_font = lam::up(context->font.style);
    setup_inline(context, block);
    float content_start = context->block.advance_y;
    layout_ruby_children(context, block);
    float height = max(0.0f, context->block.advance_y - content_start);
    if (!radiant::layout_flow_result_apply(context, selected))
        log_error("ruby height: selected flow restoration failed");
    radiant::layout_flow_result_release(context->pool, selected);
    return height;
}
