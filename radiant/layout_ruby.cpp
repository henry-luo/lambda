#include "layout_ruby.hpp"
#include "../lib/tagged.hpp"
#include <float.h>

// tier-3: one scratch scope owns the virtual box tree and its column measures.
// source nodes keep their DOM parents/siblings; anonymous boxes borrow node ranges.
struct LayoutRubyTextRun {
    DomNode* source;
    DomNode* root;
    LayoutTextBreakPlan plan;
    size_t source_start, source_length;
    size_t first, last;
};

struct LayoutRubyTextFragment {
    LayoutRubyTextRun* runs;
    size_t run_count;
    size_t first_run, end_run;
    LayoutTextBreakPlan plan;
    size_t first, last; // selected indices into the original-source opportunity plan
    bool emitted;
};

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
    bool has_forced_break;
    IntrinsicSizes sizes;
    float natural_start;
    float natural_width;
    LayoutRubyTextFragment* fragment;
    bool fragmented;
    int fragment_line;
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
    LayoutRubySegment* flow_source; // prepared columns retain their shared normalized level owner
    LayoutRubyColumn* columns;
    int column_count;
    int source_start;
    int source_end;
    int line_number;
    bool placed;
    float width;
    float group_width[2];
    float remaining_width[2];
    float prefix_width;
    bool has_text_flow;
    bool can_fragment_text;
    bool group_reaches_end[2];
};

struct LayoutRubyModel {
    LayoutRubySegment* first;
    LayoutRubySegment* last;
    DomNode** nodes;
    int node_count;
    bool* discarded;
    bool failed;
};

template <typename Callback>
static void ruby_for_each_level(LayoutRubySegment* segment, Callback visit) {
    visit(&segment->base);
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) visit(level);
}

static LayoutRubySegment* ruby_source_segment(LayoutRubySegment* segment) {
    return segment->flow_source ? segment->flow_source : segment;
}

// line callbacks borrow only segments already committed by the active formatter.
struct LayoutRubyFlowScope {
    LayoutContext* context;
    LayoutRubyFlowScope* previous;
    ViewSpan* ruby;
    LayoutRubyModel* model;
    LayoutRubyFlowScope(LayoutContext* layout, ViewSpan* owner, LayoutRubyModel* selected);
    ~LayoutRubyFlowScope();
    LayoutRubyFlowScope(const LayoutRubyFlowScope&) = delete;
    LayoutRubyFlowScope& operator=(const LayoutRubyFlowScope&) = delete;
};
LAM_NODE_OF(LayoutRubyFlowScope, NodeStack);

LayoutRubyFlowScope::LayoutRubyFlowScope(LayoutContext* layout, ViewSpan* owner, LayoutRubyModel* selected)
    : context(layout), previous(layout->active_ruby_flow.get()), ruby(owner), model(selected) {
    context->active_ruby_flow = lam::up(this);
}
LayoutRubyFlowScope::~LayoutRubyFlowScope() { context->active_ruby_flow = lam::up(previous); }

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
    segment->source_start = -1;
    segment->line_number = -1;
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
            if (segment) segment->source_end = origins[index - 1] + 1;
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
        if (segment) {
            if (segment->source_start < 0) segment->source_start = origins[first];
            segment->source_end = origins[index - 1] + 1;
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

static bool ruby_fragment_element_needs_full_flow(DomElement* element);
static bool ruby_build_box_text_plan(LayoutContext* context, LayoutRubyBox* box, ScratchScope* scope);

static IntrinsicSizes ruby_text_plan_sizes(LayoutRubyTextFragment* fragment) {
    LayoutTextBreakPlan* plan = &fragment->plan;
    float origin = plan->leading_space.next_advance;
    IntrinsicSizes sizes = {};
    sizes.max_content = max(0.0f, plan->points[plan->count - 1].advance - origin);
    LayoutIntrinsicMinRun minimum;
    for (size_t index = 1; index < plan->count; index++) {
        float width = max(0.0f, plan->points[index].advance - origin);
        // empty edge separators do not add an interior opportunity to a ruby box.
        if (width > 0.0f)
            layout_intrinsic_append_min_segments(&minimum, {width, width}, minimum.first >= 0.0f);
        origin = max(origin, plan->points[index].next_advance);
    }
    sizes.min_content = minimum.minimum;
    sizes.first_segment_min = minimum.first;
    sizes.last_segment_min = minimum.tail;
    sizes.interior_min_content = minimum.interior;
    sizes.has_min_content_break = minimum.has_break;
    sizes.has_used_content_break = minimum.has_used_break;
    return sizes;
}

static void ruby_measure_level(LayoutContext* context, LayoutRubyLevel* level, ScratchScope* scope) {
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
            bool atomic = node->is_element() && layout_intrinsic_element_is_atomic_inline(node->as_element());
            box->has_forced_break |= !atomic && (sizes.has_forced_break ||
                layout_intrinsic_node_has_forced_break_at_edge(node, false) ||
                layout_intrinsic_node_has_forced_break_at_edge(node, true));
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
        // CSS Ruby §2.5: adjacent source spans share collapse state within their parallel box.
        if (!ruby_fragment_element_needs_full_flow(level->element) &&
                ruby_build_box_text_plan(context, box, scope))
            box->sizes = ruby_text_plan_sizes(box->fragment);
    }
}

static LayoutRubyBox* ruby_box_in_column(LayoutRubyLevel* level, int column, bool single_span = false) {
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        if ((!box->spanning || (single_span && box->span == 1)) && box->column == column) return box;
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
        ruby_measure_level(context, &segment->base, scope);
        for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
            ruby_measure_level(context, level, scope);
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
    sizes.has_used_content_break = used_breaks.has_used_break;
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

static void ruby_refresh_inline_bounds(View* view, int) {
    if (view->view_type != RDT_VIEW_INLINE) return;
    ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
    recompute_span_bounding_box_after_line_layout(span, false,
        span->font ? span->fontp()->font_handle : nullptr);
}

static RdtLogicalPoint ruby_relative_offset(View* view) {
    View* scope = view ? view->parent.get() : nullptr;
    while (scope && scope->view_type == RDT_VIEW_INLINE && ruby_node_role(scope) != CSS_VALUE_RUBY)
        scope = scope->parent;
    return layout_applied_inline_relative_offset(view, scope);
}

static Rect ruby_flow_bounds(View* view, Rect bounds) {
    RdtLogicalPoint relative = ruby_relative_offset(view);
    bounds.x -= relative.x;
    bounds.y -= relative.y;
    return bounds;
}

static void ruby_publish_geometry(DomElement* element, float start, float width,
        bool normal_flow_start = false) {
    ViewSpan* span = lam::view_as<RDT_VIEW_INLINE>(element);
    if (!span) return;
    DomElementExt* ext = element->ensure_ext();
    if (!ext) return;
    if (normal_flow_start) start += ruby_relative_offset(span).x;
    ext->has_ruby_column_geometry = false;
    ruby_refresh_inline_bounds(span, 0);
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
    if (box->fragmented) {
        auto include = [&](View* view, bool leaf, int) {
            if (ViewText* text = lam::view_as<RDT_VIEW_TEXT>(view)) {
                for (TextRect* rect = text->rect; rect; rect = rect->next)
                    if (rect->line_number == box->fragment_line)
                        ruby_include_bounds(&result, &found, ruby_flow_bounds(text,
                            {rect->x, rect->y, rect->width, rect->height}));
            } else if (leaf && view->is_element() &&
                    layout_intrinsic_element_is_atomic_inline(view->as_element()) &&
                    view->inline_line_number == box->fragment_line) {
                // atomic boxes belong to the parent line; their descendants use an independent frame.
                ruby_include_bounds(&result, &found, ruby_flow_bounds(view,
                    {view->x, view->y, view->width, view->height}));
            }
        };
        ruby_walk_box(box, include, [](View*, int) {});
        return result;
    }
    for (int index = 0; index < box->node_count; index++) {
        View* view = static_cast<View*>(box->nodes[index]);
        if (view->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(view) ||
            (view->view_type == RDT_VIEW_TEXT && view_is_collapsed_whitespace_text(view))) continue;
        // column constraints use normal flow; relative offsets affect visual publication only.
        ruby_include_bounds(&result, &found, ruby_flow_bounds(view,
            {view->x, view->y, view->width, view->height}));
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
        Rect bounds = ruby_flow_bounds(view, {view->x, view->y, view->width, view->height});
        float bottom = max(result.y + result.height, bounds.y + bounds.height);
        result.y = min(result.y, bounds.y);
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

static bool ruby_level_has_visible_boxes(LayoutRubyLevel* level) {
    for (LayoutRubyBox* box = level->first; box; box = box->next)
        if (!box->hidden) return true;
    return false;
}

static void ruby_align_box(LayoutRubyBox* box, DomElement* inheritance_parent,
                           float start, float width) {
    float shift = start - box->natural_start;
    for (int index = 0; index < box->node_count; index++) {
        if (box->fragmented) layout_shift_inline_fragment(box->nodes[index], box->fragment_line, shift, 0.0f);
        else layout_shift_inline_flow(static_cast<View*>(box->nodes[index]), shift, 0.0f);
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
                if (box->fragmented && rect->line_number != box->fragment_line) continue;
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
            if (box->fragmented && rect->line_number != box->fragment_line) continue;
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
            layout_shift_inline_flow(view, offset, 0.0f);
            return;
        }
        ViewText* text = lam::view_require_text(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (box->fragmented && rect->line_number != box->fragment_line) continue;
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
    ruby_walk_box(box, align_view, ruby_refresh_inline_bounds);
    if (box->element && !box->fragmented) ruby_publish_geometry(box->element, start, width, true);
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
    if (!ruby_level_has_visible_boxes(level)) return;
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        if (box->hidden) continue;
        float box_start = start + ruby_column_range_width(segment, 0, box->column);
        float width = ruby_column_range_width(segment, box->column, box->span);
        ruby_align_box(box, level->element ? level->element : ruby, box_start, width);
    }
    if (level->element && (!level->first || !level->first->fragmented))
        ruby_publish_geometry(level->element, start, segment->width, true);
}

static void ruby_shift_level(LayoutRubyLevel* level, float offset_y, bool contents_only = false) {
    // a collapsed unit cannot translate the shared container holding another column's visible readings.
    if (!ruby_level_has_visible_boxes(level)) return;
    if (level->first && level->first->fragmented) {
        for (LayoutRubyBox* box = level->first; box; box = box->next)
            for (int index = 0; index < box->node_count; index++)
                layout_shift_inline_fragment(box->nodes[index], box->fragment_line, 0.0f, offset_y);
        return;
    }
    auto shift = [&](View* view) {
        if (contents_only && view->is_group()) {
            for (View* child = lam::view_require_element(view)->first_placed_child(); child; child = child->next())
                layout_shift_inline_flow(child, 0.0f, offset_y);
        } else layout_shift_inline_flow(view, 0.0f, offset_y);
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
            TextRect* rect = text->rect;
            if (level->first && level->first->fragmented)
                while (rect && rect->line_number != level->first->fragment_line) rect = rect->next;
            if (!rect) return;
            candidate = rect->y + ascent;
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
    float over_leading = excess > 0.0f ? extra * over_excess / excess : 0.0f;
    // existing block-start space can host annotation overflow without raising the base line.
    if (context->block.is_first_line)
        over_leading = max(0.0f, over_leading - context->block.annotation_start_space);
    float before = base_baseline + over_leading;
    float after = max(0.0f, base_line_height - base_baseline) +
        (excess > 0.0f ? extra * under_excess / excess : 0.0f);
    context->line.max_ascender = max(context->line.max_ascender, before);
    context->line.max_descender = max(context->line.max_descender, after);
    context->line.ruby_annotation_min_line_height = max(context->line.ruby_annotation_min_line_height, before + after);
}

static void ruby_place_annotation_levels(LayoutContext* context, ViewSpan* ruby, LayoutRubySegment* segment,
        Rect base, const Linebox* base_line, float baseline, float line_height) {
    float over = 0.0f, under = 0.0f;
    bool visible = false;
    auto contribute = [&](LayoutRubyLevel* level, Rect annotation, bool is_under) {
        visible |= annotation.width > 0.0f || annotation.height > 0.0f;
        if (ruby_has_text_box_trim_ancestor(ruby, is_under ? TEXT_BOX_TRIM_END : TEXT_BOX_TRIM_START) ||
                annotation.height <= 0.0f) return;
        float extent = max(annotation.height, level->line.max_atomic_inline_height);
        if (is_under) under = max(under, annotation.y + extent - base.y - base.height);
        else over = max(over, base.y - annotation.y + extent - annotation.height);
    };
    ruby_position_annotation_levels(ruby, segment, base, false, contribute);
    if (visible) ruby_contribute_line_metrics(context, base_line, &segment->base,
        baseline, line_height, base, over, under);
}

static bool ruby_place_segment(LayoutContext* context, ViewSpan* ruby, LayoutRubySegment* segment,
        float start, Rect empty_base, const Linebox* base_line, float baseline, float line_height,
        bool finish_annotations = true) {
    ruby_place_level(segment, &segment->base, ruby, start);
    Rect base = ruby_level_bounds(&segment->base);
    bool has_base = base.height > 0.0f;
    if (!has_base) base = empty_base;
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next)
        ruby_place_level(segment, level, ruby, start);
    if (finish_annotations)
        ruby_place_annotation_levels(context, ruby, segment, base, base_line, baseline, line_height);
    return has_base;
}

struct RubyLevelScope {
    LayoutContext* context;
    LayoutRubyLevel* previous;
    RubyLevelScope(LayoutContext* layout, LayoutRubyLevel* level)
        : context(layout), previous(layout->active_ruby_level.get()) { layout->active_ruby_level = lam::up(level); }
    ~RubyLevelScope() { context->active_ruby_level = lam::up(previous); }
};

static void ruby_flow_fragment_node(LayoutContext* context, LayoutRubyTextFragment* fragment, DomNode* node) {
    if (node->is_text()) {
        for (size_t index = fragment->first_run; index < fragment->end_run; index++) {
            LayoutRubyTextRun* run = &fragment->runs[index];
            if (run->source != node || run->first == run->last) continue;
            LayoutTextSliceScope slice(context, node, run->first, run->last, fragment->emitted);
            layout_flow_node(context, node);
            return;
        }
    } else layout_flow_node(context, node);
}

bool layout_ruby_active_level_children(LayoutContext* context, ViewSpan* span) {
    LayoutRubyLevel* level = context->active_ruby_level.get();
    if (!level) return false;
    LayoutRubyBox* box = level->first;
    if (box && box->fragmented && box->fragment) {
        bool owns = level->element == span;
        for (int index = 0; !owns && index < box->node_count; index++)
            owns = view_geometry_dom_is_descendant(span, box->nodes[index], true);
        if (!owns) return false;
        LayoutRubyTextFragment* fragment = box->fragment;
        DomNode* previous_child = nullptr;
        // materialize each selected source path once; unselected siblings generate no fragment or effects.
        for (size_t index = fragment->first_run; index < fragment->end_run; index++) {
            LayoutRubyTextRun* run = &fragment->runs[index];
            if (run->first == run->last) continue;
            DomNode* child = run->source;
            while (child->parent && child->parent != span) child = child->parent;
            if (child->parent != span || child == previous_child) continue;
            ruby_flow_fragment_node(context, fragment, child);
            previous_child = child;
        }
        return true;
    }
    if (level->element != span) return false;
    for (int index = 0; index < level->node_count; index++) {
        if (level->discarded && level->discarded[index]) layout_suppress_ignorable_container_text(level->nodes[index]);
        else if (!level->nodes[index]->ruby_annotation_hidden) layout_flow_node(context, level->nodes[index]);
    }
    return true;
}

static void ruby_flow_level(LayoutContext* context, LayoutRubyLevel* level) {
    RubyLevelScope selected(context, level);
    if (level->element) { layout_flow_node(context, level->element); return; }
    LayoutRubyBox* box = level->first;
    LayoutRubyTextFragment* fragment = box && box->fragmented ? box->fragment : nullptr;
    if (!fragment) {
        for (; box; box = box->next) {
            if (box->hidden) continue;
            for (int index = 0; index < box->node_count; index++) layout_flow_node(context, box->nodes[index]);
        }
        return;
    }
    DomNode* previous_root = nullptr;
    for (size_t index = fragment->first_run; index < fragment->end_run; index++) {
        LayoutRubyTextRun* run = &fragment->runs[index];
        if (run->first == run->last || run->root == previous_root) continue;
        ruby_flow_fragment_node(context, fragment, run->root);
        previous_root = run->root;
    }
}

bool layout_ruby_active_annotation(LayoutContext* context, ViewSpan* annotation) {
    LayoutRubyLevel* level = context->active_ruby_level.get();
    if (!level || !level->annotation) return false;
    if (level->element == annotation) return true;
    for (LayoutRubyBox* box = level->first; box; box = box->next)
        if (box->element == annotation) return true;
    return false;
}

static void ruby_flow_sources(LayoutContext* context, LayoutRubyModel* model, int first, int end) {
    for (int index = first; index < end; index++) {
        DomNode* node = model->nodes[index];
        if (model->discarded[index]) { layout_suppress_ignorable_container_text(node); continue; }
        if (layout_ruby_annotation_node(node)) continue;
        LayoutRubyLevel* base = nullptr;
        if (ruby_node_role(node) == CSS_VALUE_RUBY_BASE_CONTAINER) {
            for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
                if (segment->base.element == node) { base = &segment->base; break; }
            }
        }
        RubyLevelScope base_scope(context, base);
        layout_flow_node(context, node);
    }
}

static void ruby_clear_element_geometry(DomElement* element) {
    if (!element || !element->ext) return;
    element->ext->has_ruby_column_geometry = false;
    element->ext->has_empty_ruby_base = false;
    element->ext->has_ruby_parallel_fragments = false;
}

static void ruby_clear_level_geometry(LayoutRubyLevel* level) {
    ruby_clear_element_geometry(level->element);
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        ruby_clear_element_geometry(box->element);
        for (int index = 0; index < box->node_count; index++) box->nodes[index]->ruby_annotation_hidden = false;
    }
}

static void ruby_reset_box_views(LayoutContext* context, LayoutRubyBox* box) {
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
    }
}

static void ruby_suppress_annotation_box(LayoutContext* context, LayoutRubyBox* box) {
    ruby_reset_box_views(context, box);
    for (int index = 0; index < box->node_count; index++) box->nodes[index]->ruby_annotation_hidden = true;
}

static void ruby_finalize_annotation_line(LayoutContext* context, LayoutRubyLevel* level) {
    // the independent level owns its baseline phase; an empty level creates no annotation line.
    if (context->line.is_line_start || !context->line.start_view) return;
    if (level->element) view_vertical_align(context, level->element);
    else for (LayoutRubyBox* box = level->first; box; box = box->next) {
        if (box->hidden) continue;
        for (int index = 0; index < box->node_count; index++)
            view_vertical_align(context, box->nodes[index]);
    }
}

static void ruby_layout_annotation_level(LayoutContext* context, LayoutRubyLevel* level,
                                          const Linebox* base_line, float column_width) {
    LayoutContextScope state(context);
    LayoutViewScope view(context);
    RubyLevelScope level_scope(context, level);
    // this stage emits one selected parallel interval, not an independent soft-wrap decision.
    LayoutInlineKeepScope keep(context, true);
    context->block.inline_box_metrics = nullptr;
    // the measured columns contain every reading; RTL placement needs a finite alignment edge.
    line_init(context, base_line->left, base_line->left + column_width);
    for (LayoutRubyBox* box = level->first; box; box = box->next) {
        if (box->hidden) ruby_suppress_annotation_box(context, box);
    }
    if (!ruby_level_has_visible_boxes(level)) {
        // CSS Ruby §2.4: a collapsed level emits no line, including through a shared level container.
        level->line = context->line;
        ruby_measure_used_level(level);
        return;
    }
    ruby_flow_level(context, level);
    LayoutRubyTextFragment* fragment = level->first ? level->first->fragment : nullptr;
    // CSS Text §4.1.3 trims each parallel fragment's separator before ruby-align distributes its space.
    if (fragment && level->first->fragmented && fragment->last + 1 < fragment->plan.count)
        line_consume_trailing_collapsible_space(context, true, true, true);
    ruby_finalize_annotation_line(context, level);
    level->line = context->line;
    ruby_measure_used_level(level);
}

static void ruby_select_segment_line(LayoutRubySegment* segment, int line_number) {
    ruby_for_each_level(segment, [&](LayoutRubyLevel* level) {
        for (LayoutRubyBox* box = level->first; box; box = box->next) {
            box->fragmented = true;
            box->fragment_line = line_number;
        }
    });
}

static void ruby_align_annotation_model(ViewSpan* ruby, LayoutRubyModel* model,
        Rect empty_base, int line_number, bool selected = false) {
    LayoutRubySegment* previous = nullptr;
    for (LayoutRubySegment* unit = model->first; unit; unit = unit->next) {
        LayoutRubySegment* segment = ruby_source_segment(unit);
        if (segment == previous) continue;
        previous = segment;
        if (selected && (!segment->placed || segment->line_number != line_number)) continue;
        if (ruby->ext && ruby->ext->has_ruby_parallel_fragments)
            ruby_select_segment_line(segment, line_number);
        bool on_line = false;
        for (LayoutRubyBox* box = segment->base.first; box; box = box->next) {
            for (int index = 0; index < box->node_count; index++)
                on_line |= layout_inline_span_has_content_on_line(box->nodes[index], line_number);
        }
        if (segment->base.first && !on_line) continue;
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

static bool ruby_segment_is_unbroken(LayoutContext* context, LayoutRubySegment* segment) {
    if (!segment->base.first || segment->source_start < 0) return false;
    for (LayoutRubyBox* box = segment->base.first; box; box = box->next)
        if (box->has_forced_break) return false;
    LayoutIntrinsicMinRun breaks;
    ruby_segment_break_run(context, segment, &breaks, true);
    return !breaks.has_used_break;
}

static bool ruby_has_unbroken_segments(LayoutContext* context, LayoutRubyModel* model) {
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next)
        if (!ruby_segment_is_unbroken(context, segment)) return false;
    return model->first != nullptr;
}

static bool ruby_segment_boundary(LayoutContext* context, LayoutRubySegment* previous,
        LayoutRubySegment* segment, bool used_breaks) {
    LayoutIntrinsicMinRun edge;
    ruby_minimum_last_source(&edge, previous->base.last);
    LayoutRubyBox* first = segment->base.first;
    return first && first->node_count && layout_intrinsic_boundary_break(
        context, edge.previous, first->nodes[0], false, used_breaks);
}

static void ruby_resolve_break_groups(LayoutContext* context, LayoutRubyModel* model, bool used_breaks,
        bool include_text_breaks = false) {
    // cache each group's suffix in two linear walks; long words must not rescan all following units per unit.
    for (LayoutRubySegment* first = model->first; first;) {
        float width = 0.0f;
        LayoutRubySegment* end = first;
        bool interior_break = false;
        while (end) {
            bool text_break = include_text_breaks && end->can_fragment_text;
            width += text_break ? end->prefix_width : end->width;
            LayoutRubySegment* next = end->next;
            if (text_break || !next || ruby_segment_boundary(context, end, next, used_breaks)) {
                interior_break = text_break;
                end = next;
                break;
            }
            end = next;
        }
        float remaining = width;
        for (LayoutRubySegment* segment = first; segment != end; segment = segment->next) {
            segment->group_width[used_breaks] = width;
            segment->remaining_width[used_breaks] = remaining;
            segment->group_reaches_end[used_breaks] = !end && !interior_break;
            remaining -= segment->width;
        }
        first = end;
    }
}

static float ruby_start_overhang(LayoutRubySegment* segment, float space) {
    if (!segment || !segment->base.first) return 0.0f;
    float extra = segment->columns[0].max_content - segment->base.first->natural_width;
    return min(max(0.0f, extra * 0.5f), space);
}

static void ruby_annotation_line_membership(LayoutRubySegment* segment, int line_number) {
    auto stamp = [&](View* view, bool, int) {
        view->inline_line_number = line_number;
        if (ViewText* text = lam::view_as<RDT_VIEW_TEXT>(view))
            for (TextRect* rect = text->rect; rect; rect = rect->next) rect->line_number = line_number;
    };
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
        if (level->element) level->element->inline_line_number = line_number;
        for (LayoutRubyBox* box = level->first; box; box = box->next)
            ruby_walk_box(box, stamp, [](View*, int) {}, true);
    }
}

static bool ruby_finish_segment_flow(LayoutContext* context, ViewSpan* ruby,
        LayoutRubyModel* model, LayoutRubySegment* segment, ScratchScope* scope,
        float start, bool source_fragments, float preceding_space = 0.0f) {
    // whole units and selected text fragments commit through one column/baseline stage.
    ruby_measure_used_level(&segment->base);
    Linebox base_line = context->line;
    for (LayoutRubyLevel* level = segment->annotations; level; level = level->next)
        ruby_layout_annotation_level(context, level, &base_line, segment->width);
    ruby_resolve_segment_columns(segment, scope, &model->failed);
    if (model->failed) return false;
    float height = 0.0f;
    float baseline = line_baseline_position(context, &height);
    float overhang = source_fragments ? 0.0f : ruby_start_overhang(segment, preceding_space);
    ruby_place_segment(context, ruby, segment, start - overhang, {}, &base_line, baseline, height,
        !segment->flow_source);
    context->line.advance_x = start - overhang + segment->width;
    if (ruby->view_type == RDT_VIEW_INLINE)
        layout_inline_record_advance(context, ruby, start, context->line.advance_x);
    // selected source rectangles already carry their own line membership, including older fragments.
    if (!source_fragments) ruby_annotation_line_membership(segment, context->block.line_number);
    segment->placed = true;
    segment->line_number = context->block.line_number;
    LayoutRubySegment* source = ruby_source_segment(segment);
    source->placed = true;
    source->line_number = context->block.line_number;
    return true;
}

static void ruby_merge_annotation_line(Linebox* target, const Linebox* source) {
    // merge baseline/extent facts, not cursors or break opportunities belonging to individual columns.
    float Linebox::* const extents[] = {&Linebox::max_ascender, &Linebox::max_descender,
        &Linebox::max_css_baseline_ascender, &Linebox::max_inline_line_height,
        &Linebox::max_atomic_inline_height, &Linebox::max_text_ascender, &Linebox::max_text_descender,
        &Linebox::max_normal_line_height, &Linebox::c1_control_line_height,
        &Linebox::max_top_bottom_height, &Linebox::max_top_height, &Linebox::max_bottom_height};
    for (auto field : extents) target->*field = max(target->*field, source->*field);
    bool Linebox::* const facts[] = {&Linebox::has_native_metrics, &Linebox::has_expanded_inline_lh,
        &Linebox::has_baseline_shift, &Linebox::has_different_inline_font,
        &Linebox::has_inline_font_size_difference, &Linebox::has_c1_control_text,
        &Linebox::has_non_c1_text, &Linebox::has_direct_block_text};
    for (auto field : facts) target->*field = target->*field || source->*field;
    if (!target->start_view) target->start_view = source->start_view;
    target->is_line_start = target->is_line_start && source->is_line_start;
}

static void ruby_finalize_flow_line(LayoutContext* context, ViewSpan* ruby, LayoutRubyModel* model) {
    int line_number = context->block.line_number;
    for (LayoutRubySegment* first = model->first; first;) {
        LayoutRubySegment* segment = ruby_source_segment(first);
        LayoutRubySegment* end = first->next;
        while (end && ruby_source_segment(end) == segment) end = end->next;
        if (first->flow_source && segment->placed && segment->line_number == line_number) {
            ruby_select_segment_line(segment, line_number);
            for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
                level->line = {};
                level->line.is_line_start = true;
            }
            for (LayoutRubySegment* unit = first; unit != end; unit = unit->next) {
                if (!unit->placed || unit->line_number != line_number) continue;
                LayoutRubyLevel* level = segment->annotations;
                for (LayoutRubyLevel* selected = unit->annotations; selected; selected = selected->next) {
                    if (selected->line.start_view && !selected->line.is_line_start) {
                        if (!level->line.start_view) level->line = selected->line;
                        else ruby_merge_annotation_line(&level->line, &selected->line);
                    }
                    level = level->next;
                }
            }
            for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
                if (!level->line.start_view) continue;
                LayoutContextScope state(context);
                RubyLevelScope selected(context, level);
                context->line = level->line;
                context->block.inline_box_metrics = nullptr;
                ruby_finalize_annotation_line(context, level);
                level->line = context->line;
            }
            // all columns now share one annotation IFC and one envelope for this selected line.
            Linebox base_line = context->line;
            float height = 0.0f;
            float baseline = line_baseline_position(context, &height);
            ruby_place_annotation_levels(context, ruby, segment, ruby_level_bounds(&segment->base),
                &base_line, baseline, height);
        }
        first = end;
    }
}

static void ruby_flow_unbroken_segments(LayoutContext* context, ViewSpan* ruby,
        LayoutRubyModel* model, ScratchScope* scope) {
    LayoutRubyFlowScope selected(context, ruby, model);
    LayoutRubySegment* previous = nullptr;
    ruby_resolve_break_groups(context, model, false);
    ruby_resolve_break_groups(context, model, true);
    int source = 0;
    bool may_wrap = layout_inline_allows_soft_wrap(context, ruby);
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
        ruby_flow_sources(context, model, source, segment->source_start);
        float left = context->line.has_float_intrusion ? context->line.effective_left : context->line.left;
        float right = context->line.has_float_intrusion ? context->line.effective_right : context->line.right;
        // normal opportunities take precedence when the whole word fits on a fresh line.
        bool emergency = segment->group_width[false] > right - left;
        float required = segment->remaining_width[emergency];
        bool boundary = previous ? ruby_segment_boundary(context, previous, segment, emergency)
            : context->line.wrap_opportunity_before_nowrap;
        if (may_wrap && boundary && !context->line.is_line_start &&
                context->line.advance_x + required > right + kTextLayoutSubpixelEpsilon)
            line_break(context);
        float start = context->line.advance_x;
        float preceding_space = context->line.has_space
            ? layout_measure_space_advance(context, font_box_handle(&context->font), context->font.style) : 0.0f;
        {
            LayoutInlineKeepScope keep(context, true);
            ruby_flow_sources(context, model, segment->source_start, segment->source_end);
        }
        if (!ruby_finish_segment_flow(context, ruby, model, segment, scope,
                start, false, preceding_space)) return;
        previous = segment;
        source = segment->source_end;
    }
    ruby_flow_sources(context, model, source, model->node_count);
    if (ruby->view_type != RDT_VIEW_INLINE) {
        ruby_finish_block_line(context);
        // block Ruby is outside the ordinary inline ancestor callback chain.
        for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
            Rect base = ruby_level_bounds(&segment->base);
            ruby_position_annotation_levels(ruby, segment, base, true, [](LayoutRubyLevel*, Rect, bool) {});
        }
    }
}

static bool ruby_fragment_element_needs_full_flow(DomElement* element) {
    if (!element) return false;
    // break elements contribute non-text boundaries (or annotation separators) absent from a text-leaf plan.
    if (element->tag() == MARKUP_NAME_BR || element->tag() == MARKUP_NAME_WBR) return true;
    // fragment bounds descend ordinary inlines; independent formatting contexts remain whole units.
    DisplayValue display = resolve_display_value(element);
    if (!layout_display_is_inline_level(display.outer) || display.inner == CSS_VALUE_RUBY ||
            layout_intrinsic_element_is_atomic_inline(element)) return true;
    // generated children are additional sources, beyond this direct-text fragment's plan.
    if (dom_element_has_before_content(element) || dom_element_has_after_content(element)) return true;
    BoxMetrics edges = layout_boundary_metrics(element->bound);
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++)
        if (edges.margin.values[side] != 0.0f || edges.padding.values[side] != 0.0f ||
                edges.border.values[side] != 0.0f) return true;
    return false;
}

// prefix plans retain original text leaves; mixed boxes keep the general formatter.
static int ruby_count_text_runs(DomNode* node) {
    if (node->is_comment()) return 0;
    if (node->is_text()) return layout_text_combine_upright_applies(node) ? -1 : 1;
    DomElement* element = node->as_element();
    if (!element || ruby_fragment_element_needs_full_flow(element)) return -1;
    int count = 0;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        int nested = ruby_count_text_runs(child);
        if (nested < 0) return -1;
        count += nested;
    }
    return count;
}

static bool ruby_collect_text_runs(LayoutContext* context, DomNode* node, DomNode* root,
        ScratchScope* scope, LayoutRubyTextFragment* fragment) {
    if (node->is_comment()) return true;
    if (node->is_text()) {
        LayoutRubyTextRun* run = &fragment->runs[fragment->run_count++];
        run->source = node;
        run->root = root;
        if (!node->text_data()) return false;
        run->source_length = strlen((const char*)node->text_data());
        if (!layout_measure_text_break_plan(context, node, scope, &run->plan)) return false;
        if (fragment->run_count > 1) {
            LayoutRubyTextRun* previous = run - 1;
            run->source_start = previous->source_start + previous->source_length;
            if (previous->source_length && layout_text_is_collapsible_whitespace(previous->source,
                    previous->source_length - 1, 1) && run->plan.leading_space.next_advance > 0.0f &&
                    run->source_length &&
                    layout_text_is_collapsible_whitespace(node, 0, 1)) {
                // the earlier source owns a collapsed separator, including its font/spacing.
                LayoutTextBreakPoint leading = run->plan.leading_space;
                if (run->plan.count > 1 && run->plan.points[1].source_end == leading.source_end) {
                    memmove(run->plan.points + 1, run->plan.points + 2,
                        (--run->plan.count - 1) * sizeof(LayoutTextBreakPoint));
                }
                float repeated_space = leading.next_advance;
                for (size_t index = 1; index < run->plan.count; index++) {
                    run->plan.points[index].advance -= repeated_space;
                    run->plan.points[index].next_advance -= repeated_space;
                }
                run->plan.points[0].source_end = leading.source_end;
                run->plan.points[0].advance = run->plan.points[0].next_advance = 0.0f;
                run->plan.width -= repeated_space;
                run->plan.leading_space = {};
            }
        }
        return true;
    }
    for (DomNode* child = node->as_element()->first_child; child; child = child->next_sibling)
        if (!ruby_collect_text_runs(context, child, root, scope, fragment)) return false;
    return true;
}

static bool ruby_build_box_text_plan(LayoutContext* context, LayoutRubyBox* box, ScratchScope* scope) {
    if (box->fragment) return true;
    if (box->hidden || box->whitespace || box->has_forced_break) return false;
    int count = 0;
    for (int index = 0; index < box->node_count; index++) {
        int nested = ruby_count_text_runs(box->nodes[index]);
        if (nested < 0) return false;
        count += nested;
    }
    if (!count) return false;
    LayoutRubyTextFragment* fragment = scope->array_zero<LayoutRubyTextFragment>(1);
    if (!fragment) return false;
    fragment->runs = scope->array_zero<LayoutRubyTextRun>((size_t)count);
    if (!fragment->runs) return false;
    for (int index = 0; index < box->node_count; index++)
        if (!ruby_collect_text_runs(context, box->nodes[index], box->nodes[index], scope, fragment)) return false;
    size_t capacity = 2;
    for (size_t index = 0; index < fragment->run_count; index++) capacity += fragment->runs[index].plan.count;
    fragment->plan.points = scope->array_zero<LayoutTextBreakPoint>(capacity);
    if (!fragment->plan.points) return false;
    float advance = 0.0f;
    float trailing = 0.0f;
    for (size_t index = 0; index < fragment->run_count; index++) {
        LayoutRubyTextRun* run = &fragment->runs[index];
        if (!index) {
            fragment->plan.points[fragment->plan.count++] = run->plan.points[0];
            fragment->plan.leading_space = run->plan.leading_space;
        }
        else if (layout_intrinsic_boundary_break(context, (run - 1)->source, run->source, false, false)) {
            LayoutTextBreakPoint edge = (run - 1)->plan.points[(run - 1)->plan.count - 1];
            float trim = (run - 1)->plan.width - edge.advance;
            fragment->plan.points[fragment->plan.count++] = {run->source_start,
                advance - trim, advance + run->plan.points[0].next_advance};
        }
        for (size_t point = 1; point + 1 < run->plan.count; point++) {
            LayoutTextBreakPoint measured = run->plan.points[point];
            measured.source_end += run->source_start;
            measured.advance += advance;
            measured.next_advance += advance;
            fragment->plan.points[fragment->plan.count++] = measured;
        }
        advance += run->plan.width;
        if (run->plan.width > 0.0f)
            trailing = run->plan.width - run->plan.points[run->plan.count - 1].advance;
    }
    LayoutRubyTextRun* last = &fragment->runs[fragment->run_count - 1];
    fragment->plan.points[fragment->plan.count++] = {last->source_start + last->source_length, advance - trailing, advance};
    fragment->plan.width = advance;
    box->fragment = fragment;
    return true;
}

static bool ruby_prepare_text_level(LayoutContext* context, LayoutRubyLevel* level, ScratchScope* scope,
        float* prefix_width) {
    *prefix_width = 0.0f;
    LayoutRubyBox* box = level->first;
    if (ruby_fragment_element_needs_full_flow(level->element) || !box || box != level->last ||
            !ruby_build_box_text_plan(context, box, scope)) return false;
    LayoutRubyTextFragment* fragment = box->fragment;
    float origin = max(fragment->plan.points[0].next_advance, fragment->plan.leading_space.next_advance);
    for (size_t index = 1; index + 1 < fragment->plan.count; index++) {
        const LayoutTextBreakPoint& point = fragment->plan.points[index];
        if (point.advance > origin && point.next_advance < fragment->plan.width) {
            *prefix_width = point.advance - origin;
            break;
        }
    }
    return true;
}

static bool ruby_copy_column_flow_level(LayoutRubyLevel* source, LayoutRubyLevel* target,
        int column, ScratchScope* scope) {
    LayoutRubyBox* box = ruby_box_in_column(source, column, true);
    if (!box || box->whitespace || box->span != 1) return false;
    LayoutRubyBox* selected = scope->array_zero<LayoutRubyBox>(1);
    if (!selected) return false;
    *selected = *box;
    selected->next = nullptr;
    selected->column = 0;
    selected->spanning = false;
    *target = *source;
    target->next = nullptr;
    // hidden units still use the ordinary child walker; it must not replay other columns.
    target->nodes = selected->nodes;
    target->node_count = selected->node_count;
    target->discarded = nullptr;
    target->first = target->last = selected;
    target->box_count = 1;
    return true;
}

static bool ruby_prepare_column_flow_units(LayoutContext* context, LayoutRubyModel* source,
        LayoutRubyModel* target, ScratchScope* scope) {
    // units borrow normalized source ranges; splitting flow must not split DOM owners or spanning readings.
    target->nodes = source->nodes;
    target->node_count = source->node_count;
    target->discarded = source->discarded;
    RubyBuildContext build = {context, scope, target, false};
    for (LayoutRubySegment* segment = source->first; segment; segment = segment->next) {
        bool supported = true;
        ruby_for_each_level(segment, [&](LayoutRubyLevel* level) {
            for (LayoutRubyBox* box = level->first; box; box = box->next)
                supported &= !box->whitespace && box->span == 1;
        });
        if (!supported) return false;
        for (int column = 0; column < segment->column_count; column += 2) {
            LayoutRubySegment* unit = ruby_append_segment(&build);
            if (!unit) return false;
            unit->source_start = segment->source_start;
            unit->source_end = segment->source_end;
            unit->flow_source = segment;
            if (!ruby_copy_column_flow_level(&segment->base, &unit->base, column, scope)) return false;
            for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
                LayoutRubyLevel* selected = ruby_append_annotation_level(&build, unit);
                if (!selected || !ruby_copy_column_flow_level(level, selected, column, scope)) return false;
            }
            ruby_resolve_segment_columns(unit, scope, &target->failed);
            if (target->failed) return false;
        }
    }
    return true;
}

static float ruby_fragment_width(LayoutRubyTextFragment* fragment, size_t last) {
    float end = last + 1 == fragment->plan.count ? fragment->plan.width : fragment->plan.points[last].advance;
    return max(0.0f, end - fragment->plan.points[fragment->first].next_advance);
}

static bool ruby_select_fragment(LayoutRubySegment* segment, float available, bool overflow) {
    LayoutRubyTextFragment* base = segment->base.first->fragment;
    size_t final = base->plan.count - 1;
    float remaining = ruby_fragment_width(base, final);
    // CSS Ruby §3.4.2 recommends approximately equal proportions in parallel levels.
    for (size_t step = 1; step < final - base->first; step++) {
        size_t candidate = overflow ? base->first + step : final - step;
        float width = ruby_fragment_width(base, candidate);
        if (width <= 0.0f || base->plan.points[candidate].next_advance >= base->plan.width ||
                (!overflow && width > available + kTextLayoutSubpixelEpsilon)) continue;
        float proportion = width / remaining;
        bool fits = true;
        for (LayoutRubyLevel* level = segment->annotations; level; level = level->next) {
            if (level->first->hidden) continue;
            LayoutRubyTextFragment* reading = level->first->fragment;
            float reading_remaining = ruby_fragment_width(reading, reading->plan.count - 1);
            float best_distance = FLT_MAX;
            reading->last = reading->first;
            for (size_t index = reading->first + 1; index + 1 < reading->plan.count; index++) {
                float prefix = ruby_fragment_width(reading, index);
                if (prefix <= 0.0f || reading->plan.points[index].next_advance >= reading->plan.width ||
                        (!overflow && prefix > available + kTextLayoutSubpixelEpsilon)) continue;
                float distance = fabsf(prefix / reading_remaining - proportion);
                if (distance < best_distance) { best_distance = distance; reading->last = index; }
            }
            fits &= reading->last > reading->first;
        }
        if (fits) { base->last = candidate; return true; }
    }
    return false;
}

static void ruby_select_source_intervals(LayoutRubyTextFragment* fragment) {
    size_t first = fragment->plan.points[fragment->first].source_end;
    size_t last = fragment->plan.points[fragment->last].source_end;
    fragment->first_run = fragment->run_count;
    fragment->end_run = 0;
    for (size_t index = 0; index < fragment->run_count; index++) {
        LayoutRubyTextRun* run = &fragment->runs[index];
        run->first = first > run->source_start ? min(first - run->source_start, run->source_length) : 0;
        run->first = max(run->first, run->plan.points[0].source_end);
        run->last = last > run->source_start ? min(last - run->source_start, run->source_length) : 0;
        if (run->first >= run->last) run->first = run->last = 0;
        else { fragment->first_run = min(fragment->first_run, index); fragment->end_run = index + 1; }
    }
}

static bool ruby_flow_parallel_text(LayoutContext* context, ViewSpan* ruby,
        LayoutRubyModel* model, ScratchScope* scope) {
    // inline callbacks finalize selected readings; block ruby has a separate completion protocol.
    if (ruby->view_type != RDT_VIEW_INLINE || !model->first ||
            context->block.direction != CSS_VALUE_LTR || context->block.first_line_style_active ||
            layout_element_writing_mode(ruby) != WM_HORIZONTAL_TB ||
            !layout_inline_allows_soft_wrap(context, ruby) ||
            dom_element_has_before_content(ruby) || dom_element_has_after_content(ruby)) return false;
    // replaying a source float/positioned child per fragment would corrupt its single placement.
    for (int index = 0; index < model->node_count; index++)
        if (!ruby_node_in_flow(model->nodes[index])) return false;
    bool column_units = false;
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next)
        column_units |= segment->column_count > 1;
    LayoutRubyModel columns = {};
    if (column_units) {
        if (!ruby_prepare_column_flow_units(context, model, &columns, scope)) return false;
        model = &columns;
    }
    bool has_text_break = false;
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
        if (segment->column_count != 1 || segment->source_start < 0) return false;
        bool supported = true, parallel_break = true;
        float prefix = 0.0f;
        ruby_for_each_level(segment, [&](LayoutRubyLevel* level) {
            if (level->first && level->first->hidden) return;
            float level_prefix = 0.0f;
            supported &= ruby_prepare_text_level(context, level, scope, &level_prefix);
            parallel_break &= level_prefix > 0.0f;
            prefix = max(prefix, level_prefix);
        });
        // independent descendants remain whole when no parallel interior opportunity exists.
        if (!supported && !ruby_segment_is_unbroken(context, segment)) return false;
        segment->has_text_flow = supported;
        parallel_break &= supported;
        segment->can_fragment_text = parallel_break;
        segment->prefix_width = parallel_break ? prefix : segment->width;
        has_text_break |= parallel_break;
    }
    // indivisible columns can still wrap at their shared base-text seams.
    if (!has_text_break && !column_units) return false;
    // all applicability checks precede source reset or emission, including indivisible neighbors.
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next)
        ruby_for_each_level(segment, [&](LayoutRubyLevel* level) {
            ruby_reset_box_views(context, level->first);
            if (level->element) {
                level->element->view_type = RDT_VIEW_NONE;
                layout_set_view_geometry(level->element, 0.0f, 0.0f, 0.0f, 0.0f);
            }
        });
    ruby_resolve_break_groups(context, model, true, true);
    LayoutRubyFlowScope selected(context, ruby, model);
    ruby->ensure_ext()->has_ruby_parallel_fragments = true;
    LayoutRubySegment* previous = nullptr;
    int source = 0;
    for (LayoutRubySegment* segment = model->first; segment; segment = segment->next) {
        ruby_flow_sources(context, model, source, segment->source_start);
        LayoutRubyTextFragment* base = segment->has_text_flow ? segment->base.first->fragment : nullptr;
        auto each_box = [&](auto visit) {
            ruby_for_each_level(segment, [&](LayoutRubyLevel* level) {
                if (!level->first->hidden) visit(level->first);
            });
        };
        bool boundary = previous ? ruby_segment_boundary(context, previous, segment, true)
            : context->line.wrap_opportunity_before_nowrap;
        LayoutRubySegment* next = segment->next;
        bool bound_tail = next && !ruby_segment_boundary(context, segment, next, true);
        float tail = bound_tail ? next->remaining_width[true] : 0.0f;
        bool reaches_end = !next || (bound_tail && next->group_reaches_end[true]);
        while (!base || base->first + 1 < base->plan.count) {
            if (context->line.is_line_start) update_line_for_bfc_floats(context);
            if (base) each_box([&](LayoutRubyBox* box) {
                if (box == segment->base.first && !context->line.is_line_start && !context->line.has_space) return;
                LayoutRubyTextFragment* fragment = box->fragment;
                if (fragment->first == 0 && fragment->plan.leading_space.next_advance > 0.0f)
                    fragment->plan.points[0] = fragment->plan.leading_space;
                // a collapsed leading separator has no prefix; it belongs to the previous line's edge.
                while (fragment->first + 2 < fragment->plan.count &&
                        ruby_fragment_width(fragment, fragment->first + 1) <= 0.0f)
                    fragment->first++;
            });
            float left = context->line.has_float_intrusion ? context->line.effective_left : context->line.left;
            float right = context->line.has_float_intrusion ? context->line.effective_right : context->line.right;
            float available = max(0.0f, right - context->line.advance_x);
            float remaining = base ? 0.0f : segment->width;
            if (base) each_box([&](LayoutRubyBox* box) {
                box->fragmented = true;
                box->fragment_line = context->block.line_number;
                box->fragment->last = box->fragment->plan.count - 1;
                remaining = max(remaining, ruby_fragment_width(box->fragment, box->fragment->last));
            });
            // a final piece reserves the first legal paired prefix of any forbidden following group.
            bool full = remaining + tail <= available + kTextLayoutSubpixelEpsilon;
            if (full && reaches_end && !layout_inline_boundary_allows_soft_wrap(context, ruby))
                full = layout_following_inline_content_fits(context, ruby,
                    remaining + tail + layout_inline_end_edge(ruby));
            if (!full && (!segment->can_fragment_text || !ruby_select_fragment(segment, available, false))) {
                if (!context->line.is_line_start && ((base && base->emitted) || boundary)) {
                    ruby_finalize_flow_line(context, ruby, model);
                    line_break(context);
                    continue;
                }
                // an indivisible parallel unit overflows together when no legal smaller piece fits.
                if (base && (!segment->can_fragment_text || !ruby_select_fragment(segment, right - left, true)))
                    each_box([](LayoutRubyBox* box) { box->fragment->last = box->fragment->plan.count - 1; });
            }
            bool final = !base || base->last + 1 == base->plan.count;
            if (base) each_box([](LayoutRubyBox* box) { ruby_select_source_intervals(box->fragment); });
            float start = context->line.advance_x;
            {
                LayoutInlineKeepScope keep(context, true);
                ruby_flow_level(context, &segment->base);
                if (!final) line_consume_trailing_collapsible_space(context, true, true, true);
            }
            if (!ruby_finish_segment_flow(context, ruby, model, segment, scope, start, base != nullptr)) return true;
            if (!final) {
                ruby_finalize_flow_line(context, ruby, model);
                line_break(context);
            }
            if (!base) break;
            each_box([](LayoutRubyBox* box) {
                box->fragment->emitted = true;
                box->fragment->first = box->fragment->last;
            });
        }
        previous = segment;
        source = segment->source_end;
    }
    ruby_flow_sources(context, model, source, model->node_count);
    ruby_finalize_flow_line(context, ruby, model);
    return true;
}

static void ruby_prepare_unbroken_float_band(LayoutContext* context, ViewSpan* ruby,
        LayoutRubyModel* model, const IntrinsicSizes& sizes) {
    if (ruby->view_type != RDT_VIEW_INLINE || sizes.has_used_content_break ||
            layout_inline_flow_is_kept(context) ||
            context->block.direction != CSS_VALUE_LTR ||
            layout_element_writing_mode(ruby) != WM_HORIZONTAL_TB ||
            !ruby_has_unbroken_segments(context, model)) return;
    BlockContext* bfc = block_context_find_bfc(&context->block);
    if (!bfc || (!bfc->left_float_count && !bfc->right_float_count)) return;

    // D4.5.1v4: the existing measurement scope restores selected fragments and generated effects.
    float height = layout_ruby_intrinsic_height(context, ruby, sizes.max_content);
    height = max(height, context->line.max_ascender + context->line.max_descender);
    layout_prepare_inline_float_band(context, ruby, height);
    float width = sizes.max_content + layout_inline_end_edge(ruby);
    if (context->line.wrap_opportunity_before_nowrap &&
            line_has_prior_flow_content(&context->line) &&
            (context->line.advance_x + width > context->line.effective_right + kTextLayoutSubpixelEpsilon ||
             (!layout_inline_boundary_allows_soft_wrap(context, ruby) &&
              !layout_following_inline_content_fits(context, ruby, width)))) {
        // choose the boundary before emitting source; following unbreakable boxes keep their shared unit.
        line_break(context);
        layout_prepare_inline_float_band(context, ruby, height);
    }
    if (!line_has_prior_flow_content(&context->line) &&
            context->line.advance_x + width > context->line.effective_right &&
            width <= context->line.right - context->line.left) {
        float next_y = find_line_y_for_width_with_floats(bfc, &context->block, &context->line,
            width, context->block.advance_y, height);
        if (next_y > context->block.advance_y) {
            context->block.advance_y = next_y;
            line_reset(context);
            layout_prepare_inline_float_band(context, ruby, height);
        }
    }
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
        IntrinsicSizes sizes = ruby_measure_model(context, ruby, &model, &scope);
        parallel_wraps = sizes.has_used_content_break;
        if (model.failed) return;
        ruby_prepare_unbroken_float_band(context, ruby, &model, sizes);
        start = context->line.advance_x;
        if (parallel_wraps && ruby_flow_parallel_text(context, ruby, &model, &scope)) return;
        if (parallel_wraps && ruby_has_unbroken_segments(context, &model)) {
            ruby_flow_unbroken_segments(context, ruby, &model, &scope);
            return;
        }
    }
    {
        // CSS Ruby §3.4: soft breaks require an opportunity in every parallel box.
        LayoutInlineKeepScope parallel_flow(context, !parallel_wraps);
        ruby_flow_sources(context, &model, 0, model.node_count);
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
            ruby_layout_annotation_level(context, level, &base_line, segment->width);
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
    float overhang = ruby_start_overhang(model.first, preceding_space);
    float cursor = start - overhang;
    bool has_base = false;
    for (LayoutRubySegment* segment = model.first; segment; segment = segment->next) {
        Rect empty_base = {cursor, base_top, segment->width, max(base_height, font_height)};
        has_base |= ruby_place_segment(context, ruby, segment, cursor, empty_base,
            &base_line, base_baseline, base_line_height);
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
        // line_break advances the cursor; alignment still belongs to the completed line.
        int completed_line = context->block.line_number;
        ruby_finish_block_line(context);
        ruby_align_annotation_model(ruby, &model, empty_base, completed_line);
    }
}

void layout_ruby_align_annotations_after_line(LayoutContext* context, ViewSpan* ruby) {
    for (LayoutRubyFlowScope* flow = context->active_ruby_flow.get(); flow; flow = flow->previous) {
        if (flow->ruby != ruby) continue;
        ruby_align_annotation_model(ruby, flow->model, {}, context->block.line_number, true);
        return;
    }
    ScratchScope scope(&context->scratch);
    LayoutRubyModel model = layout_ruby_build_model(context, ruby, &scope, false);
    if (model.failed) { log_error("ruby model: final alignment scratch allocation failed"); return; }
    ruby_align_annotation_model(ruby, &model,
        {ruby->x, ruby->y, ruby->width, ruby->height}, context->block.line_number);
}

static void ruby_remove_box_alignment(LayoutRubyBox* box) {
    float offset = 0.0f;
    auto remove = [&](View* view, bool leaf, int) {
        if (!leaf) return;
        if (view->view_type != RDT_VIEW_TEXT) {
            layout_shift_inline_fragment(view, box->fragment_line, -offset, 0.0f);
            return;
        }
        ViewText* text = lam::view_require_text(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->line_number != box->fragment_line) continue;
            rect->x -= offset;
            rect->width -= rect->ruby_alignment_advance;
            offset += rect->ruby_alignment_advance;
            if (rect->ruby_alignment_advance != 0.0f) rect->justification_spacing = 0.0f;
            rect->ruby_alignment_offset = rect->ruby_alignment_advance = 0.0f;
        }
        adjust_text_bounds(text);
    };
    ruby_walk_box(box, remove, ruby_refresh_inline_bounds);
}

View* layout_ruby_trim_trailing_space(LayoutContext* context, ViewText* text, TextRect* rect,
        float* advance, bool suppress_text) {
    ViewSpan* ruby = nullptr;
    for (DomNode* parent = text ? text->parent.get() : nullptr; parent; parent = parent->parent) {
        // annotation and atomic descendants own independent whitespace contexts.
        if (layout_ruby_annotation_node(parent) || parent->view_type != RDT_VIEW_INLINE) return nullptr;
        if (resolve_display_value(parent).inner == CSS_VALUE_RUBY) {
            ruby = lam::view_require<RDT_VIEW_INLINE>(parent);
            break;
        }
    }
    if (!ruby || !ruby->ext || context->block.direction != CSS_VALUE_LTR ||
            layout_element_writing_mode(ruby) != WM_HORIZONTAL_TB ||
            dom_element_has_before_content(ruby) || dom_element_has_after_content(ruby) ||
            (!ruby->ext->has_ruby_column_geometry && !ruby->ext->has_ruby_parallel_fragments)) return nullptr;
    for (LayoutRubyFlowScope* flow = context->active_ruby_flow.get(); flow; flow = flow->previous) {
        if (flow->ruby != ruby) continue;
        bool placed = false;
        for (LayoutRubySegment* unit = flow->model->first; unit; unit = unit->next) {
            if (!unit->placed || unit->line_number != context->block.line_number) continue;
            for (LayoutRubyBox* box = unit->base.first; box; box = box->next)
                for (int index = 0; index < box->node_count; index++)
                    placed |= view_geometry_dom_is_descendant(text, box->nodes[index], true);
        }
        // a selected interval trimmed before emission has not allocated its columns yet.
        if (!placed) return nullptr;
        break;
    }

    ScratchScope scope(&context->scratch);
    LayoutRubyModel model = layout_ruby_build_model(context, ruby, &scope, false);
    if (model.failed) return nullptr;
    bool supported = true;
    float start = FLT_MAX, old_width = 0.0f;
    for (LayoutRubySegment* segment = model.first; segment; segment = segment->next) {
        ruby_select_segment_line(segment, context->block.line_number);
        ruby_for_each_level(segment, [&](LayoutRubyLevel* level) {
            supported &= !ruby_fragment_element_needs_full_flow(level->element);
            for (LayoutRubyBox* box = level->first; box; box = box->next) {
                supported &= !ruby_fragment_element_needs_full_flow(box->element);
                ruby_walk_box(box, [&](View* view, bool, int) {
                    if (view->view_type == RDT_VIEW_INLINE)
                        supported &= !ruby_fragment_element_needs_full_flow(view->as_element());
                }, [](View*, int) {});
            }
            ruby_measure_used_level(level);
            Rect bounds = ruby_level_bounds(level);
            if (bounds.width > 0.0f || bounds.height > 0.0f) start = min(start, bounds.x);
        });
        ruby_resolve_segment_columns(segment, &scope, &model.failed);
        old_width += segment->width;
    }
    if (!supported || model.failed || start == FLT_MAX) return nullptr;

    // CSS Text Phase II changes natural content; CSS Ruby §3.1.1 then reallocates each paired column.
    text_rect_trim_trailing_space(text, rect, *advance, suppress_text);
    float cursor = start;
    for (LayoutRubySegment* segment = model.first; segment; segment = segment->next) {
        ruby_for_each_level(segment, [&](LayoutRubyLevel* level) {
            for (LayoutRubyBox* box = level->first; box; box = box->next) ruby_remove_box_alignment(box);
            ruby_measure_used_level(level);
        });
        // reuse the already allocated scratch columns; no new failure can follow source mutation.
        ruby_resolve_segment_columns(segment, &scope, &model.failed);
        ruby_for_each_level(segment, [&](LayoutRubyLevel* level) {
            ruby_place_level(segment, level, ruby, cursor);
        });
        cursor += segment->width;
    }
    if (!inline_span_has_multiple_line_fragments(ruby)) {
        float before = layout_inline_span_edge_extent(ruby, false, true, false);
        float after = layout_inline_span_edge_extent(ruby, false, false, false);
        ruby_publish_geometry(ruby, start - before, cursor - start + before + after);
    } else {
        ruby->ext->has_ruby_column_geometry = false;
        ruby_refresh_inline_bounds(ruby, 0);
    }
    layout_inline_record_advance(context, ruby, start, cursor);
    // a wider reading can absorb the removed base space without changing the line's consumption.
    *advance = old_width - (cursor - start);
    return ruby;
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
