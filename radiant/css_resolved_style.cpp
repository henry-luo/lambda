#include "css_resolved_style.hpp"
#include "render.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/dom/dom.h"
#include "../lib/tagged.hpp"
#include "../lib/generation.h"

int64_t g_style_resolve_incremental = 0;
int64_t g_style_geometry_evaluations = 0;

namespace radiant {

uint64_t css_style_misses[CSS_STYLE_MISS_COUNT] = {};
const char* css_style_miss_name(CssStyleMiss reason) {
    static const char* names[] = {"cold", "context", "style", "view-reset", "parent", "font",
        "storage", "viewport", "inherit", "percentage", "container", "effect", "allocation"};
    static_assert(sizeof(names) / sizeof(names[0]) == CSS_STYLE_MISS_COUNT, "CSS miss names drift");
    return reason < CSS_STYLE_MISS_COUNT ? names[reason] : "unknown";
}

enum CssGeometryDependency : uint8_t {
    CSS_GEOMETRY_X = 1,
    CSS_GEOMETRY_Y = 2,
    CSS_GEOMETRY_INLINE = 4,
    CSS_GEOMETRY_ALL = 7,
};

struct CssGeometryBinding {
    CssDeclaration* declaration;
    CssGeometryBinding* next;
    int64_t priority;
    CssPropertyCode property;
    uint8_t dependencies;
};

struct CssAxisValues {
    float given, minimum, maximum, percent, min_percent, max_percent;
    float fit_limit, fit_percent;
    CssEnum type, min_type, max_type;
};

struct CssUsedGeometry {
    bool context_width, context_height;
    float context_given_width, context_given_height;
    CssAxisValues axes[2];
    Margin margin;
    Spacing padding;
    Spacing border_width;
    float insets[4], inset_percents[4];
    bool inset_present[4];
    float flex_basis;
    float flex_grow, flex_shrink, aspect_ratio;
    CssEnum align_self;
    int order;
    bool flex_percent, flex_content, flex_stretch;
    CssEnum position, clear, float_prop;
    int z_index;
    bool has_z_index;
};

struct CssGeometryContext {
    SizeF block, parent, viewport;
    float inline_basis;
    float parent_given_width, parent_given_height;
    bool has_parent, intrinsic_width, intrinsic_height, vertical_inline;
};

struct CssResolvedStyle {
    DomElement* element;
    StyleTree* specified;
    CssGeometryBinding* bindings;
    CssGeometryBinding* last_binding;
    CssUsedGeometry geometry;
    CssGeometryContext context;
    CssFontKey parent_font, own_font;
    uint64_t query_epoch, font_generation;
    uint32_t style_version, layout_generation, revision, parent_revision;
    float viewport_width, viewport_height, raster_scale, root_font_size;
    ViewType view_type;
    DisplayValue display;
    uint32_t role_flags;
    uint8_t properties;
    uint8_t dependencies;
    CssStyleMiss last_miss;
    bool valid, reusable, viewport_dependent, root_font_dependent;
};

CssStyleMiss css_resolved_style_last_miss(DomElement* element) {
    return element && element->layout_cache && element->layout_cache->resolved_style
        ? element->layout_cache->resolved_style->last_miss : CSS_STYLE_COLD;
}

static bool css_style_miss(CssResolvedStyle* style, CssStyleMiss reason) {
    css_style_misses[reason]++;
    if (style) style->last_miss = reason;
    return false;
}

static void css_style_unsupported(CssResolvedStyle* style, CssStyleMiss reason) {
    style->reusable = false;
    style->last_miss = reason;
}

CssFontKey css_font_key(const FontProp* font) {
    if (!font) return {};
    return {font->family, font->font_size, font->used_zoom,
        font->letter_spacing, font->word_spacing, font->font_style,
        font->font_weight, font->font_variant, font->font_kerning, font->font_weight_numeric};
}

bool css_font_keys_equal(const CssFontKey& a, const CssFontKey& b) {
    return a.family == b.family && a.size == b.size && a.zoom == b.zoom &&
        a.letter_spacing == b.letter_spacing && a.word_spacing == b.word_spacing &&
        a.style == b.style && a.weight == b.weight && a.variant == b.variant &&
        a.kerning == b.kerning && a.numeric_weight == b.numeric_weight;
}

bool css_resolved_style_is_containing_block_independent(DomElement* element) {
    CssResolvedStyle* style = element && element->layout_cache
        ? element->layout_cache->resolved_style.get() : nullptr;
    // unknown consumers retain the complete measurement context as their key.
    // viewport/root-font inputs stay in the intrinsic query's key, independently of boxes.
    return style && style->valid && style->dependencies == 0;
}

static DomElement* css_style_parent(DomElement* element) {
    return element && element->parent && element->parent->is_element()
        ? element->parent->as_element() : nullptr;
}

static uint32_t css_parent_revision(LayoutContext* context, DomElement* element) {
    // source wrappers above the selected root do not own this view's caches (D4.5.1v4).
    DomElement* parent = element != context->doc->root ? css_style_parent(element) : nullptr;
    CssResolvedStyle* style = parent && parent->layout_cache
        ? parent->layout_cache->resolved_style.get() : nullptr;
    return style ? style->revision : 0;
}

static bool css_style_cache_allowed(LayoutContext* context, DomElement* element) {
    if (!context || !element || !context->doc || !context->pool ||
        !context->doc->view_tree || context->pool != context->doc->view_tree->prop_pool ||
        context->css_query_probe || context->selected_style ||
        context->doc->view_tree->model || element->doc != context->doc ||
        element->scratch_lived() || element->is_table_fixup() || element->form_control() ||
        element->table_prop() ||
        dom_element_is_svg(element) || layout_element_is_replaced(element) ||
        !dom_element_is_connected(element) ||
        (context->selected_view_tree && context->selected_view_tree != context->doc->view_tree)) return false;
    DisplayValue display = element->display;
    return display.inner != CSS_VALUE_TABLE_ROW && display.inner != CSS_VALUE_TABLE_ROW_GROUP &&
        display.inner != CSS_VALUE_TABLE_HEADER_GROUP && display.inner != CSS_VALUE_TABLE_FOOTER_GROUP &&
        display.inner != CSS_VALUE_TABLE_COLUMN && display.inner != CSS_VALUE_TABLE_COLUMN_GROUP &&
        display.inner != CSS_VALUE_TABLE_CELL;
}

static uint64_t css_font_generation(LayoutContext* context) {
    return context->ui_context && context->ui_context->font_ctx
        ? font_context_resource_generation(context->ui_context->font_ctx) : 0;
}

static uint8_t css_geometry_properties(DomElement* element) {
    return (element->blk ? 1 : 0) | (element->bound ? 2 : 0) |
        (element->bound && element->boundary()->border ? 4 : 0) |
        (element->position ? 8 : 0) | (element->flex_item() ? 16 : 0);
}

static CssGeometryContext css_geometry_context(LayoutContext* context) {
    BlockContext* parent = context->block.parent;
    DomElement* element = context->view && context->view->is_element() ? context->view->as_element() : nullptr;
    ViewBlock* parent_view = lam::view_as_block(css_style_parent(element));
    return {{context->block.content_width, context->block.content_height},
        {parent ? parent->content_width : 0.0f, parent ? parent->content_height : 0.0f},
        {context->width, context->height}, layout_containing_inline_percentage_base(context),
        parent ? parent->given_width : -1.0f, parent ? parent->given_height : -1.0f,
        parent != nullptr, context->available_space.width.is_intrinsic(),
        context->available_space.height.is_intrinsic(),
        parent_view && layout_block_inline_axis_is_vertical(parent_view)};
}

static uint8_t css_geometry_changes(const CssGeometryContext& a, const CssGeometryContext& b) {
    if (a.has_parent != b.has_parent || a.intrinsic_width != b.intrinsic_width ||
        a.intrinsic_height != b.intrinsic_height || a.vertical_inline != b.vertical_inline) return CSS_GEOMETRY_ALL;
    uint8_t changed = 0;
    if (a.block.width != b.block.width || a.parent.width != b.parent.width ||
        a.parent_given_width != b.parent_given_width || a.viewport.width != b.viewport.width)
        changed |= CSS_GEOMETRY_X;
    if (a.block.height != b.block.height || a.parent.height != b.parent.height ||
        a.parent_given_height != b.parent_given_height || a.viewport.height != b.viewport.height)
        changed |= CSS_GEOMETRY_Y;
    if (a.inline_basis != b.inline_basis) changed |= CSS_GEOMETRY_INLINE;
    if (b.vertical_inline && (changed & CSS_GEOMETRY_Y)) changed |= CSS_GEOMETRY_INLINE;
    return changed;
}

template<typename T>
static void css_transfer_value(T* applied, T* retained, bool capture) {
    if (capture) *retained = *applied;
    else *applied = *retained;
}

static void css_geometry_transfer(LayoutContext* context, DomElement* element,
                                  CssUsedGeometry* geometry, bool capture) {
    // CSS can write the caller's sizing context before the node has a block view.
    if (geometry->context_width) css_transfer_value(&context->block.given_width,
        &geometry->context_given_width, capture);
    if (geometry->context_height) css_transfer_value(&context->block.given_height,
        &geometry->context_given_height, capture);
    if (element->blk) {
        for (size_t axis = 0; axis < 2; axis++) {
            LayoutAxisRefs refs(element->block_mut(), axis == 0);
            CssAxisValues* values = &geometry->axes[axis];
#define CSS_AXIS_VALUE(member, saved) css_transfer_value(refs.member.get(), &values->saved, capture)
            CSS_AXIS_VALUE(given, given);
            CSS_AXIS_VALUE(minimum, minimum);
            CSS_AXIS_VALUE(maximum, maximum);
            CSS_AXIS_VALUE(given_percent, percent);
            CSS_AXIS_VALUE(minimum_percent, min_percent);
            CSS_AXIS_VALUE(maximum_percent, max_percent);
            CSS_AXIS_VALUE(given_fit_content_limit, fit_limit);
            CSS_AXIS_VALUE(given_fit_content_percent, fit_percent);
            CSS_AXIS_VALUE(given_type, type);
            CSS_AXIS_VALUE(minimum_type, min_type);
            CSS_AXIS_VALUE(maximum_type, max_type);
#undef CSS_AXIS_VALUE
        }
        if (!capture) {
            element->blk->aspect_ratio_auto_height = false;
            element->blk->percentage_height_resolved_late = false;
        }
    }
    if (element->bound) {
        BoundaryProp* boundary = element->boundary_mut();
        css_transfer_value(&boundary->margin, &geometry->margin, capture);
        css_transfer_value(&boundary->padding, &geometry->padding, capture);
        if (boundary->border) css_transfer_value(&boundary->border->width, &geometry->border_width, capture);
        if (!capture) layout_reset_boundary_flow_state(boundary);
    }
    if (element->position) {
        if (!capture) layout_reset_position_style(context,
            lam::view_require_element(static_cast<View*>(element)));
        PositionProp* position = element->position;
        for (size_t side = 0; side < 4; side++) {
            css_transfer_value(&position->inset_values[side], &geometry->insets[side], capture);
            css_transfer_value(&position->inset_percents[side], &geometry->inset_percents[side], capture);
            css_transfer_value(&position->inset_present[side], &geometry->inset_present[side], capture);
        }
        css_transfer_value(&position->position, &geometry->position, capture);
        css_transfer_value(&position->clear, &geometry->clear, capture);
        css_transfer_value(&position->float_prop, &geometry->float_prop, capture);
        css_transfer_value(&position->z_index, &geometry->z_index, capture);
        css_transfer_value(&position->has_z_index, &geometry->has_z_index, capture);
    }
    if (FlexItemProp* item = element->flex_item()) {
        // flex initialization resets its CSS fields together with measurement scratch.
        css_transfer_value(&item->flex_grow, &geometry->flex_grow, capture);
        css_transfer_value(&item->flex_shrink, &geometry->flex_shrink, capture);
        css_transfer_value(&item->aspect_ratio, &geometry->aspect_ratio, capture);
        css_transfer_value(&item->align_self, &geometry->align_self, capture);
        css_transfer_value(&item->order, &geometry->order, capture);
        if (capture) {
            geometry->flex_basis = item->flex_basis;
            geometry->flex_percent = item->flex_basis_is_percent;
            geometry->flex_content = item->flex_basis_is_content;
            geometry->flex_stretch = item->flex_basis_is_stretch;
        } else {
            item->flex_basis = geometry->flex_basis;
            item->flex_basis_is_percent = geometry->flex_percent;
            item->flex_basis_is_content = geometry->flex_content;
            item->flex_basis_is_stretch = geometry->flex_stretch;
        }
    }
}

static uint8_t css_geometry_property(CssPropertyCode property) {
    if (property == CSS_PROPERTY_WIDTH || property == CSS_PROPERTY_MIN_WIDTH ||
        property == CSS_PROPERTY_MAX_WIDTH || property == CSS_PROPERTY_LEFT ||
        property == CSS_PROPERTY_RIGHT) return CSS_GEOMETRY_X;
    if (property == CSS_PROPERTY_HEIGHT || property == CSS_PROPERTY_MIN_HEIGHT ||
        property == CSS_PROPERTY_MAX_HEIGHT || property == CSS_PROPERTY_TOP ||
        property == CSS_PROPERTY_BOTTOM) return CSS_GEOMETRY_Y;
    if (property == CSS_PROPERTY_MARGIN || property == CSS_PROPERTY_PADDING ||
        (property >= CSS_PROPERTY_MARGIN_TOP && property <= CSS_PROPERTY_MARGIN_LEFT) ||
        (property >= CSS_PROPERTY_PADDING_TOP && property <= CSS_PROPERTY_PADDING_LEFT) ||
        (property >= CSS_PROPERTY_MARGIN_BLOCK && property <= CSS_PROPERTY_MARGIN_INLINE_END) ||
        (property >= CSS_PROPERTY_PADDING_BLOCK && property <= CSS_PROPERTY_PADDING_INLINE_END))
        return CSS_GEOMETRY_INLINE;
    if (property == CSS_PROPERTY_INSET ||
        (property >= CSS_PROPERTY_INSET_BLOCK && property <= CSS_PROPERTY_INSET_INLINE_END))
        return CSS_GEOMETRY_X | CSS_GEOMETRY_Y;
    return 0;
}

static void css_bindings_clear(CssResolvedStyle* style, Pool* pool) {
    CssGeometryBinding* binding = style->bindings;
    while (binding) {
        CssGeometryBinding* next = binding->next;
        css_declaration_destroy_owned(binding->declaration, pool);
        pool_free(pool, binding);
        binding = next;
    }
    style->bindings = style->last_binding = nullptr;
    style->dependencies = 0;
    style->valid = false;
}

void css_resolved_style_release(LayoutCache* cache, Pool* pool) {
    if (!cache || !cache->resolved_style) return;
    css_bindings_clear(cache->resolved_style, pool);
    lam::free_owned(pool, cache->resolved_style);
}

CssStyleRecordScope::CssStyleRecordScope(LayoutContext* c, DomElement* element)
    : context(c), saved(c->css_style_recording), recording(nullptr) {
    context->css_style_recording = nullptr;
    if (!css_style_cache_allowed(context, element)) return;
    LayoutCache* cache = layout_pass_ensure_cache(context, element);
    if (!cache) return;
    if (!cache->resolved_style) cache->resolved_style = lam::own(
        (CssResolvedStyle*)pool_calloc(context->pool, sizeof(CssResolvedStyle)));
    recording = cache->resolved_style;
    if (!recording) return;
    css_bindings_clear(recording, context->pool);
    recording->element = element;
    recording->reusable = true;
    recording->viewport_dependent = false;
    recording->root_font_dependent = false;
    recording->geometry.context_width = recording->geometry.context_height = false;
    recording->revision = generation_next32(recording->revision);
    DomElement* parent = css_style_parent(element);
    recording->parent_font = css_font_key(parent ? parent->font.get() : nullptr);
    context->css_style_recording = lam::up(recording);
}

void CssStyleRecordScope::commit() {
    context->css_style_recording = lam::up(saved);
    if (!recording) return;
    DomElement* element = recording->element;
    // these consumers can change computed inputs during sizing, not just used box lengths.
    if ((element->blk && (element->block()->container_axes ||
        element->block()->content_visibility != CSS_VALUE_VISIBLE)) ||
        (element->font && element->font->initial_letter_computed_font_size > 0.0f) ||
        element->web_animation_state()) css_style_unsupported(recording, CSS_STYLE_EFFECT);
    recording->specified = element->specified_style;
    recording->style_version = element->style_version;
    recording->query_epoch = context->doc->style_query_epoch;
    recording->layout_generation = context->doc->view_tree->layout_generation;
    recording->parent_revision = css_parent_revision(context, element);
    recording->own_font = css_font_key(element->font);
    recording->font_generation = css_font_generation(context);
    recording->raster_scale = ui_context_raster_scale(context->ui_context);
    recording->root_font_size = context->root_font_size;
    recording->properties = css_geometry_properties(element);
    recording->view_type = element->view_type;
    recording->display = element->display;
    recording->role_flags = element->elmt_flags &
        (ELMT_FLAG_PARENT_ITEM_KIND_MASK | ELMT_FLAG_ROLE_KIND_MASK);
    recording->viewport_width = context->width;
    recording->viewport_height = context->height;
    recording->context = css_geometry_context(context);
    css_geometry_transfer(context, element, &recording->geometry, true);
    recording->valid = recording->reusable;
    recording = nullptr;
}

CssStyleRecordScope::~CssStyleRecordScope() {
    context->css_style_recording = lam::up(saved);
}

void css_resolved_style_record(LayoutContext* context, CssPropertyCode property,
    const CssDeclaration* declaration, const CssValue* value, int64_t priority) {
    CssResolvedStyle* style = context->css_style_recording;
    if (!style || style->element != context->view || !style->reusable) return;
    if ((property == CSS_PROPERTY_ANIMATION || property == CSS_PROPERTY_ANIMATION_NAME ||
        property == CSS_PROPERTY_TRANSITION || property == CSS_PROPERTY_TRANSITION_PROPERTY ||
        property == CSS_PROPERTY_TRANSITION_DURATION) && !css_value_keyword_equals(value, CSS_VALUE_NONE)) {
        // transition snapshots exist even without an effect; only authored motion excludes reuse.
        css_style_unsupported(style, CSS_STYLE_EFFECT);
        return;
    }
    uint8_t dependencies = css_geometry_property(property);
    if (dependencies && css_value_keyword_equals(value, CSS_VALUE_INHERIT)) {
        // legacy explicit box inheritance reads the parent's mutable used values.
        css_style_unsupported(style, CSS_STYLE_INHERIT);
        return;
    }
    if (!dependencies || !layout_css_value_has_percentage(value)) return;
    CssGeometryBinding* binding = (CssGeometryBinding*)pool_calloc(context->pool, sizeof(CssGeometryBinding));
    if (!binding) { css_style_unsupported(style, CSS_STYLE_ALLOCATION); return; }
    CssDeclaration source = *declaration;
    source.value = const_cast<CssValue*>(value);
    source.pending_shorthand_code = (CssPropertyCode)0;
    binding->declaration = css_declaration_snapshot(&source, context->pool);
    if (!binding->declaration) {
        pool_free(context->pool, binding);
        css_style_unsupported(style, CSS_STYLE_ALLOCATION);
        return;
    }
    // fold font-relative terms once; percentage terms remain for the new basis.
    CssResolvedStyle* saved = context->css_style_recording;
    context->css_style_recording = nullptr;
    layout_compute_math_lengths(context, binding->declaration->value, property, true);
    context->css_style_recording = lam::up(saved);
    binding->priority = priority;
    binding->property = property;
    binding->dependencies = dependencies;
    if (style->last_binding) style->last_binding->next = binding;
    else style->bindings = binding;
    style->last_binding = binding;
    style->dependencies |= dependencies;
}

void css_resolved_style_note_length(LayoutContext* context, uintptr_t property,
    const CssValue* value) {
    CssResolvedStyle* style = context ? context->css_style_recording.get() : nullptr;
    if (!style || !style->reusable || !value) return;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        CssUnit unit = value->data.length.unit;
        if (unit >= CSS_UNIT_CQW && unit <= CSS_UNIT_CQMAX) css_style_unsupported(style, CSS_STYLE_CONTAINER);
        if (unit >= CSS_UNIT_VW && unit <= CSS_UNIT_DVMAX) style->viewport_dependent = true;
        if (unit == CSS_UNIT_REM) style->root_font_dependent = true;
    }
    if (value->type != CSS_VALUE_TYPE_PERCENTAGE || css_geometry_property((CssPropertyCode)property)) return;
    // these percentages compute against stable font/scalar inputs, not a containing box.
    if (property != CSS_PROPERTY_FONT_SIZE && property != CSS_PROPERTY_LINE_HEIGHT &&
        property != CSS_PROPERTY_LETTER_SPACING && property != CSS_PROPERTY_WORD_SPACING &&
        property != CSS_PROPERTY_VERTICAL_ALIGN && property != CSS_PROPERTY_OPACITY)
        css_style_unsupported(style, CSS_STYLE_PERCENTAGE);
}

void css_resolved_style_note_axis(LayoutContext* context, LayoutAxis axis) {
    CssResolvedStyle* style = context->css_style_recording;
    if (!style || style->element != context->view) return;
    if (axis == LAYOUT_AXIS_X) style->geometry.context_width = true;
    else style->geometry.context_height = true;
}

bool css_resolved_style_apply(LayoutContext* context, DomElement* element) {
    if (!css_style_cache_allowed(context, element)) return css_style_miss(nullptr, CSS_STYLE_CONTEXT);
    if (!element->layout_cache || !element->layout_cache->resolved_style)
        return css_style_miss(nullptr, CSS_STYLE_COLD);
    CssResolvedStyle* style = element->layout_cache->resolved_style;
    if (element->web_animation_state()) return css_style_miss(style, CSS_STYLE_EFFECT);
    DomElement* parent = css_style_parent(element);
    if (!style->valid) return css_style_miss(style, style->last_miss);
    if (style->specified != element->specified_style || style->style_version != element->style_version ||
        style->query_epoch != context->doc->style_query_epoch) return css_style_miss(style, CSS_STYLE_CHANGED);
    if (style->layout_generation != context->doc->view_tree->layout_generation)
        return css_style_miss(style, CSS_STYLE_VIEW_RESET);
    if (style->parent_revision != css_parent_revision(context, element) ||
        !css_font_keys_equal(style->parent_font, css_font_key(parent ? parent->font.get() : nullptr)))
        return css_style_miss(style, CSS_STYLE_PARENT);
    // line-width math and font metrics also depend on the physical raster scale.
    if (style->raster_scale != ui_context_raster_scale(context->ui_context) ||
        (style->root_font_dependent && style->root_font_size != context->root_font_size) ||
        style->font_generation != css_font_generation(context) ||
        !css_font_keys_equal(style->own_font, css_font_key(element->font)))
        return css_style_miss(style, CSS_STYLE_FONT);
    if (style->properties != css_geometry_properties(element)) return css_style_miss(style, CSS_STYLE_STORAGE);
    // provisional inline/block views and item roles have different property applicability.
    if (style->view_type != element->view_type ||
        style->role_flags != (element->elmt_flags & (ELMT_FLAG_PARENT_ITEM_KIND_MASK | ELMT_FLAG_ROLE_KIND_MASK)) ||
        style->display.outer != element->display.outer || style->display.inner != element->display.inner ||
        style->display.legacy_box != element->display.legacy_box ||
        style->display.legacy_vertical != element->display.legacy_vertical)
        return css_style_miss(style, CSS_STYLE_STORAGE);
    if (style->viewport_dependent && (style->viewport_width != context->width || style->viewport_height != context->height))
        return css_style_miss(style, CSS_STYLE_VIEWPORT);
    CssGeometryContext current = css_geometry_context(context);
    uint8_t changed = css_geometry_changes(style->context, current);
    css_geometry_transfer(context, element, &style->geometry, false);
    if (element->font && context->ui_context) setup_font(context->ui_context, &context->font, element->font);
    // cached CSS owns clean values; layout can consume its applied copy freely.
    for (CssGeometryBinding* binding = style->bindings; binding; binding = binding->next) {
        if (!(binding->dependencies & changed)) continue;
        css_apply_computed_property(binding->property, binding->declaration,
            binding->declaration->value, binding->priority, context);
        g_style_geometry_evaluations++;
    }
    css_geometry_transfer(context, element, &style->geometry, true);
    style->context = current;
    g_style_resolve_incremental++;
    return true;
}

} // namespace radiant
