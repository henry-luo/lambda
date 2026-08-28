#include "layout.hpp"
#include "view.hpp"
#include "render.hpp"
#include "event.hpp"
#include "radiant.hpp"

#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/strbuf.h"
#include "../lib/str.h"
#include "../lib/font/font.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/input.hpp"

#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include <utf8proc.h>
#include <chrono>
#include <cfloat>
#include <cstdlib>
using namespace std::chrono;

extern void adjust_text_bounds(ViewText* text);
extern DomDocument* load_lambda_html_doc(Url* html_url, const char* css_filename,
    int viewport_width, int viewport_height, Pool* pool, const char* html_source,
    bool track_source_lines, bool execute_scripts);

static void align_and_discard_phantom_inline_line(LayoutContext* lycon);

bool line_has_prior_flow_content(const Linebox* line) {
    return line &&
        (line->last_text_rect ||
         line->has_replaced_content ||
         line->has_c1_control_text ||
         line->has_non_c1_text);
}

static FloatAvailableSpace layout_block_float_space_at_current_y(ViewBlock* block,
                                                                 BlockContext* bfc) {
    float y_in_bfc = block->y;
    ViewElement* parent = block->parent_view();
    while (parent && bfc->establishing_element && parent != bfc->establishing_element) {
        y_in_bfc += parent->y;
        parent = parent->parent_view();
    }
    float query_height = block->height > 0.0f ? block->height : 16.0f;
    return block_context_space_at_y(bfc, y_in_bfc, query_height);
}

static const char* stabilize_custom_layout_name(const char* name, char* storage, size_t storage_size) {
    if (!name || !storage || storage_size == 0) return nullptr;
    size_t i = 0;
    while (i + 1 < storage_size && name[i] != '\0') {
        storage[i] = name[i];
        i++;
    }
    storage[i] = '\0';
    if (name[i] != '\0') {
        log_error("CUSTOM_LAYOUT_NAME_TOO_LONG name starts with '%s'", storage);
        return nullptr;
    }
    return storage;
}

static bool view_is_descendant_of(ViewElement* child, ViewElement* ancestor) {
    ViewElement* walker = child->parent_view();
    while (walker) {
        if (walker == ancestor) return true;
        walker = walker->parent_view();
    }
    return false;
}

static const char* pseudo_css_value_extract_name(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_STRING) return value->data.string;
    if (value->type == CSS_VALUE_TYPE_URL) return value->data.url;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM) return value->data.custom_property.name;
    return nullptr;
}

typedef struct CssContentImage {
    const char* url;
    float resolution;
} CssContentImage;

static bool css_function_name_is(const CssFunction* func, const char* name) {
    return func && func->name && name &&
        str_ieq_const(func->name, strlen(func->name), name);
}

static bool css_image_set_resolution_from_value(const CssValue* value, float* out_resolution) {
    if (!value || !out_resolution) return false;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        float resolution = (float)value->data.length.value;
        if (value->data.length.unit == CSS_UNIT_DPPX) {
            *out_resolution = resolution;
            return resolution > 0.0f;
        }
        if (value->data.length.unit == CSS_UNIT_DPI) {
            *out_resolution = resolution / 96.0f;
            return resolution > 0.0f;
        }
        if (value->data.length.unit == CSS_UNIT_DPCM) {
            *out_resolution = resolution * 2.54f / 96.0f;
            return resolution > 0.0f;
        }
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        const char* text = value->data.custom_property.name;
        char* end = nullptr;
        double parsed = strtod(text, &end);
        if (end && *end == 'x' && end[1] == '\0' && parsed > 0.0) {
            *out_resolution = (float)parsed;
            return true;
        }
    }
    return false;
}

static bool css_image_set_candidate_from_value(const CssValue* value, CssContentImage* out_image) {
    if (!value || !out_image) return false;
    const char* url = nullptr;
    float resolution = 1.0f;
    if (value->type == CSS_VALUE_TYPE_URL) {
        url = value->data.url;
    } else if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
               css_function_name_is(value->data.function, "url") &&
               value->data.function->arg_count > 0 && value->data.function->args[0]) {
        url = pseudo_css_value_extract_name(value->data.function->args[0]);
    } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.values) {
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            CssContentImage nested = {nullptr, 1.0f};
            float item_resolution = 1.0f;
            if (!url && css_image_set_candidate_from_value(item, &nested)) {
                url = nested.url;
                resolution = nested.resolution;
            } else if (css_image_set_resolution_from_value(item, &item_resolution)) {
                resolution = item_resolution;
            }
        }
    }
    if (!url || !url[0] || resolution <= 0.0f) return false;
    out_image->url = url;
    out_image->resolution = resolution;
    return true;
}

static bool css_content_replacement_image(const CssValue* value, CssContentImage* out_image) {
    if (!value || !out_image) return false;
    if (value->type == CSS_VALUE_TYPE_URL) {
        out_image->url = value->data.url;
        out_image->resolution = 1.0f;
        return out_image->url && out_image->url[0];
    }
    if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
        css_function_name_is(value->data.function, "url") &&
        value->data.function->arg_count > 0 && value->data.function->args[0]) {
        out_image->url = pseudo_css_value_extract_name(value->data.function->args[0]);
        out_image->resolution = 1.0f;
        return out_image->url && out_image->url[0];
    }
    if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
        (css_function_name_is(value->data.function, "image-set") ||
         css_function_name_is(value->data.function, "-webkit-image-set"))) {
        for (int i = 0; i < value->data.function->arg_count; i++) {
            if (css_image_set_candidate_from_value(value->data.function->args[i], out_image)) {
                // CSS Images: a selected image's density converts resource pixels to CSS px.
                return true;
            }
        }
    }
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            if (css_content_replacement_image(value->data.list.values[i], out_image)) return true;
        }
    }
    return false;
}

static bool css_content_value_is_image_set(const CssValue* value) {
    return layout_css_value_any(value, [](const CssValue* item) {
        return item->type == CSS_VALUE_TYPE_FUNCTION && item->data.function &&
            (css_function_name_is(item->data.function, "image-set") ||
             css_function_name_is(item->data.function, "-webkit-image-set"));
    });
}

static bool block_has_auto_content_image_set(ViewBlock* block) {
    if (!block || !block->specified_style) return false;
    CssDeclaration* content_decl = style_tree_get_declaration(block->specified_style, CSS_PROPERTY_CONTENT);
    if (!content_decl || !css_content_value_is_image_set(content_decl->value)) return false;
    bool width_auto = !block->blk || block->block()->given_width < 0.0f ||
        block->block()->given_width_type == CSS_VALUE_AUTO ||
        block->block()->given_width_type == CSS_VALUE__UNDEF;
    bool height_auto = !block->blk || block->block()->given_height < 0.0f ||
        block->block()->given_height_type == CSS_VALUE_AUTO ||
        block->block()->given_height_type == CSS_VALUE__UNDEF;
    return width_auto || height_auto;
}

static int pseudo_check_quote_content(const CssValue* value) {
    if (!value) return 0;
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        if (strcmp(value->data.custom_property.name, "open-quote") == 0) return 1;
        if (strcmp(value->data.custom_property.name, "close-quote") == 0) return 2;
        if (strcmp(value->data.custom_property.name, "no-open-quote") == 0) return 3;
        if (strcmp(value->data.custom_property.name, "no-close-quote") == 0) return 4;
    }
    return 0;
}

typedef struct ObjectViewBoxUsedRect {
    bool valid;
    float x;
    float y;
    float width;
    float height;
} ObjectViewBoxUsedRect;

static bool resolve_object_view_box_component(LayoutContext* lycon, const CssValue* value,
                                              CssPropertyCode prop_id, float reference,
                                              float auto_value, float* out_value) {
    if (!value || !out_value) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
        *out_value = auto_value;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        *out_value = (float)(value->data.percentage.value / 100.0 * reference);
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
        *out_value = resolve_length_value(lycon, prop_id, value);
        return true;
    }
    return false;
}

static int collect_object_view_box_args(CssFunction* func, CssValue** args, int max_args) {
    if (!func || !args || max_args <= 0) return 0;
    int count = 0;
    for (int i = 0; i < func->arg_count && count < max_args; i++) {
        CssValue* arg = func->args ? func->args[i] : nullptr;
        if (!arg) continue;
        if (arg->type == CSS_VALUE_TYPE_LIST) {
            for (int j = 0; j < arg->data.list.count && count < max_args; j++) {
                CssValue* item = arg->data.list.values ? arg->data.list.values[j] : nullptr;
                if (item) args[count++] = item;
            }
        } else {
            args[count++] = arg;
        }
    }
    return count;
}

static bool image_is_generated_content_child(ViewBlock* block) {
    if (!block || block->tag() != MARKUP_NAME_IMG || !block->parent || !block->parent->is_element()) {
        return false;
    }
    DomElement* parent = lam::dom_require_element(block->parent);
    const char* tag = parent ? parent->tag_name : nullptr;
    return tag && (strcmp(tag, "::before") == 0 || strcmp(tag, "::after") == 0);
}

static bool layout_set_broken_image_alt_fallback(LayoutContext* lycon,
                                                  ViewBlock* block,
                                                  bool preserve_specified_size) {
    if (!lycon || !block) return false;
    const char* alt_text = block->get_attribute("alt");
    if (!alt_text || alt_text[0] == '\0') return false;
    if (!block->embed) block->ensure_embed(lycon);
    if (!block->embed) return false;

    block->embed->broken_alt_fallback = true;
    TextIntrinsicWidths alt_widths = measure_text_intrinsic_widths(
        lycon, alt_text, strlen(alt_text),
        get_element_text_transform(block->as_element()),
        get_element_font_variant(block->as_element()));
    float line_height = lycon->block.line_height;
    if (line_height <= 0.0f) {
        line_height = lycon->font.current_font_size > 0.0f ?
            lycon->font.current_font_size * 1.2f : 16.0f;
    }
    float alt_width = 16.0f + alt_widths.max_content;
    bool block_level_fallback = block->display.outer == CSS_VALUE_BLOCK ||
        block->display.outer == CSS_VALUE_LIST_ITEM;
    // CSS 2.1 §10.3.3: an auto-width block-level replaced box fills its
    // containing block; only inline fallback uses the measured alt width.
    if (preserve_specified_size) {
        if (!layout_axis_has_given_size(block, true)) {
            lycon->block.given_width = block_level_fallback ? -1.0f : alt_width;
        }
        if (!layout_axis_has_given_size(block, false)) {
            lycon->block.given_height = max(16.0f, line_height);
        }
    } else {
        lycon->block.given_width = block_level_fallback ? -1.0f : alt_width;
        lycon->block.given_height = max(16.0f, line_height);
    }
    return true;
}

static ObjectViewBoxUsedRect resolve_object_view_box_rect(LayoutContext* lycon,
                                                           DomElement* element,
                                                          float intrinsic_width,
                                                          float intrinsic_height);

typedef struct {
    bool width;
    bool height;
} ContainIntrinsicUsedAxes;

static bool block_axis_has_automatic_css_size(ViewBlock* block, bool horizontal) {
    if (!block || !block->specified_style) return true;
    CssPropertyCode property = horizontal ? CSS_PROPERTY_WIDTH : CSS_PROPERTY_HEIGHT;
    CssDeclaration* declaration = style_tree_get_declaration(block->specified_style, property);
    return !declaration || !declaration->value ||
        (declaration->value->type == CSS_VALUE_TYPE_KEYWORD &&
         declaration->value->data.keyword == CSS_VALUE_AUTO);
}

static bool layout_preserve_ratio_transferred_min_content(ViewBlock* block, bool horizontal) {
    if (!block || layout_intrinsic_min_size_keyword(block, horizontal) != CSS_VALUE_MIN_CONTENT ||
        !block_axis_has_automatic_css_size(block, horizontal) ||
        block_axis_has_automatic_css_size(block, !horizontal)) {
        return false;
    }
    // CSS Sizing 4 separates an explicit min-content constraint from the
    return layout_used_preferred_aspect_ratio(block) > 0.0f;
}

static bool apply_contain_intrinsic_axis(LayoutContext* lycon, ViewBlock* block,
                                         bool horizontal, bool size_contained,
                                         bool has_intrinsic, bool use_empty_fallback,
                                         float intrinsic_size, bool axis_is_auto,
                                         bool normal_flow_inline_axis,
                                         bool ratio_can_determine) {
    if (!size_contained || (!has_intrinsic && !use_empty_fallback) || !axis_is_auto ||
        (horizontal && normal_flow_inline_axis) ||
        (use_empty_fallback && ratio_can_determine)) {
        return false;
    }
    float used_size = layout_border_size_if_content_box(block, intrinsic_size, horizontal);
    layout_store_given_axis(lycon, block, used_size, horizontal, false);
    LayoutAxisRefs axis(block->block_mut(), horizontal);
    if (*axis.given_type == CSS_VALUE_AUTO) *axis.given_type = CSS_VALUE__UNDEF;
    if (block->scroller) {
        float* gutter = horizontal ? &block->scroll_mut()->intrinsic_gutter_width
                                   : &block->scroll_mut()->intrinsic_gutter_height;
        *gutter = layout_block_stable_scrollbar_gutter(block, horizontal);
    }
    return true;
}

void layout_apply_preferred_ratio_to_replaced_auto_axes(LayoutContext* lycon,
                                                        ViewBlock* block) {
    if (!lycon || !block) return;
    LayoutAxisPair<bool> automatic = {
        layout_block_has_automatic_size(block, true),
        layout_block_has_automatic_size(block, false)
    };
    float preferred_aspect_ratio = layout_used_preferred_aspect_ratio(block);
    if (preferred_aspect_ratio <= 0.0f || (!automatic.x && !automatic.y)) return;
    bool ratio_uses_content_box = layout_aspect_ratio_uses_content_box(block);
    bool ratio_uses_border_box = !ratio_uses_content_box && layout_uses_border_box(block);
    LayoutAxisPair<float> used = {lycon->block.given_width, lycon->block.given_height};
    if (automatic.x && automatic.y) {
        used.y = layout_ratio_transfer_axis(
            block, used.x, true, preferred_aspect_ratio,
            ratio_uses_border_box, ratio_uses_border_box, ratio_uses_border_box);
    } else if (automatic.x != automatic.y) {
        LayoutAxis source = automatic.x ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
        LayoutAxis target = source == LAYOUT_AXIS_X ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
        bool ratio_box = layout_uses_border_box(block);
        used[target] = layout_ratio_transfer_axis(
            block, used[source], source == LAYOUT_AXIS_X, preferred_aspect_ratio,
            ratio_box, ratio_box, !ratio_uses_content_box && ratio_box);
    }
    for (LayoutAxis axis : layout_axes()) {
        if (automatic[axis]) {
            layout_store_given_axis(lycon, block, used[axis], axis == LAYOUT_AXIS_X, true);
        }
    }
}

static ContainIntrinsicUsedAxes apply_contain_intrinsic_used_size(LayoutContext* lycon,
                                                                   ViewBlock* block) {
    ContainIntrinsicUsedAxes used_axes = {false, false};
    if (!lycon || !block || !block->blk ||
        (!block->block()->contain_size && !block->block()->contain_inline_size &&
         !block->block()->content_visibility_hidden)) {
        return used_axes;
    }
    bool width_is_auto = layout_block_has_automatic_size(block, true);
    bool css_height_is_auto = block_axis_has_automatic_css_size(block, false);
    LayoutAxisPair<bool> axis_is_auto = {width_is_auto, css_height_is_auto};
    bool is_out_of_flow = block->position &&
        (block->positionp()->position == CSS_VALUE_ABSOLUTE ||
         block->positionp()->position == CSS_VALUE_FIXED);
    bool is_normal_flow_block = block->display.outer == CSS_VALUE_BLOCK &&
        block->display.inner == CSS_VALUE_FLOW && !is_out_of_flow &&
        !(block->position && element_has_float(block));
    bool physical_width_is_normal_flow_inline_axis = is_normal_flow_block &&
        !layout_block_inline_axis_is_vertical(block);
    LayoutAxisPair<bool> has_intrinsic = {
        block->block()->contain_intrinsic_width >= 0.0f,
        block->block()->contain_intrinsic_height >= 0.0f
    };
    bool has_native_select_intrinsic_size = block->form &&
        block->form->control_type == FORM_CONTROL_SELECT &&
        !block->block()->content_visibility_hidden;
    LayoutAxisPair<float> empty_size = layout_axis_pair(
        layout_block_empty_content_size_in_axis(block, true),
        layout_block_empty_content_size_in_axis(block, false)
    );
    LayoutAxisPair<bool> uses_empty_fallback = {
        !has_intrinsic.x && block->display.inner != CSS_VALUE_GRID && !has_native_select_intrinsic_size,
        !has_intrinsic.y && block->display.inner != CSS_VALUE_GRID && !has_native_select_intrinsic_size
    };
    LayoutAxisPair<bool> has_size_containment = {
        layout_block_has_size_containment_in_axis(block, true),
        layout_block_has_size_containment_in_axis(block, false)
    };
    float preferred_aspect_ratio = layout_preferred_aspect_ratio(block);
    LayoutAxisPair<bool> ratio_can_determine = {
        preferred_aspect_ratio > 0.0f && width_is_auto && !css_height_is_auto,
        preferred_aspect_ratio > 0.0f && !width_is_auto
    };
    LayoutAxisPair<bool> normal_flow_inline_axis = {
        physical_width_is_normal_flow_inline_axis, false
    };
    LayoutAxisPair<float> intrinsic_size = {
        has_intrinsic.x ? block->block()->contain_intrinsic_width : empty_size.x,
        has_intrinsic.y ? block->block()->contain_intrinsic_height : empty_size.y
    };
    LayoutAxisPair<bool> used = {};
    for (LayoutAxis axis : layout_axes()) {
        if (has_size_containment[axis] && uses_empty_fallback[axis] && ratio_can_determine[axis]) {
            layout_clear_given_axis(lycon, block, axis == LAYOUT_AXIS_X);
        }
        used[axis] = apply_contain_intrinsic_axis(
            lycon, block, axis == LAYOUT_AXIS_X, has_size_containment[axis],
            has_intrinsic[axis], uses_empty_fallback[axis], intrinsic_size[axis],
            axis_is_auto[axis], normal_flow_inline_axis[axis], ratio_can_determine[axis]);
    }
    used_axes.width = used.x;
    used_axes.height = used.y;
    return used_axes;
}

static void apply_canvas_last_remembered_size(LayoutContext* lycon, ViewBlock* block) {
    if (!lycon || !block || block->tag() != MARKUP_NAME_CANVAS || !block->blk ||
        !block->block()->content_visibility_hidden) {
        return;
    }
    LayoutAxisPair<bool> use = {
        block->block()->contain_intrinsic_width_auto,
        block->block()->contain_intrinsic_height_auto
    };
    if (!use.x && !use.y) return;
    float natural_width = 0.0f;
    float natural_height = 0.0f;
    if (!layout_canvas_natural_size(block, &natural_width, &natural_height) ||
        natural_width <= 0.0f || natural_height <= 0.0f) {
        return;
    }
    // visibility skipped the box; the normal replaced fallback otherwise stretches
    LayoutAxisPair<float> natural = {natural_width, natural_height};
    for (LayoutAxis axis : layout_axes()) {
        if (use[axis]) {
            layout_store_given_axis(lycon, block, natural[axis], axis == LAYOUT_AXIS_X, true);
        }
    }
}

static void apply_canvas_object_view_box_auto_size(LayoutContext* lycon, ViewBlock* block) {
    if (!lycon || !block || block->tag() != MARKUP_NAME_CANVAS ||
        lycon->block.given_width <= 0.0f || lycon->block.given_height <= 0.0f) {
        return;
    }
    bool width_is_auto = !block->blk || block->block()->given_width < 0.0f ||
                         block->block()->given_width_type == CSS_VALUE_AUTO;
    bool height_is_auto = !block->blk || block->block()->given_height < 0.0f ||
                          block->block()->given_height_type == CSS_VALUE_AUTO;
    if (!width_is_auto && !height_is_auto) return;
    ObjectViewBoxUsedRect object_view_box = resolve_object_view_box_rect(
        lycon, block->as_element(), lycon->block.given_width, lycon->block.given_height);
    if (!object_view_box.valid) return;
    LayoutAxisPair<bool> automatic = {width_is_auto, height_is_auto};
    LayoutAxisPair<float> used = {
        lycon->block.given_width, lycon->block.given_height
    };
    LayoutAxisPair<float> view_box = {object_view_box.width, object_view_box.height};
    if (automatic.x && automatic.y) {
        used = view_box;
    } else {
        LayoutAxis target = automatic.x ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
        LayoutAxis source = target == LAYOUT_AXIS_X ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
        if (view_box[source] > 0.0f) {
            used[target] = used[source] * view_box[target] / view_box[source];
        }
    }
    for (LayoutAxis axis : layout_axes()) {
        if (automatic[axis]) {
            LayoutAxisRefs refs(&lycon->block, axis);
            if (refs.given) *refs.given = used[axis];
        }
    }
}

static float layout_block_intrinsic_content_height(LayoutContext* lycon, ViewBlock* block,
                                                   float content_width) {
    if (!lycon || !block || !block->is_element()) return -1.0f;
    // definite height is the value being constrained and must not short-circuit it.
    float intrinsic_border_height = calculate_max_content_height(
        lycon, static_cast<DomNode*>(block), content_width, true);
    BoxMetrics box = layout_box_metrics(block);
    return max(intrinsic_border_height - box.pad_border_v, 0.0f);
}

static bool layout_block_intrinsic_content_widths(LayoutContext* lycon, ViewBlock* block,
                                                  float* min_content, float* max_content) {
    if (!lycon || !block || !block->is_element() || !min_content || !max_content) return false;
    IntrinsicSizes intrinsic = layout_measure_intrinsic_widths(
        lycon, lam::dom_require<DOM_NODE_ELEMENT>(block), true);
    BoxMetrics box = layout_box_metrics(block);
    *min_content = max(intrinsic.min_content - box.pad_border_h, 0.0f);
    *max_content = max(intrinsic.max_content - box.pad_border_h, 0.0f);
    return true;
}

static float layout_resolve_intrinsic_fit_axis(LayoutContext* lycon, ViewBlock* block,
                                               bool horizontal, float intrinsic_min,
                                               float intrinsic_max) {
    if (!lycon || !block || !block->blk) return -1.0f;
    float available_outer = lycon->block.parent
        ? (horizontal ? lycon->block.parent->content_width
                      : lycon->block.parent->content_height) : -1.0f;
    if (block->bound) {
        LayoutAxisRefs refs(block,
            horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y);
        float start = refs.margins.start_type &&
            *refs.margins.start_type == CSS_VALUE_AUTO ? 0.0f : refs.margin_start();
        float end = refs.margins.end_type &&
            *refs.margins.end_type == CSS_VALUE_AUTO ? 0.0f : refs.margin_end();
        available_outer -= start + end;
    }
    LayoutAxisRefs axis(block->block_mut(), horizontal);
    CssEnum min_type = *axis.minimum_type;
    CssEnum max_type = *axis.maximum_type;
    float min_value = *axis.minimum;
    float max_value = *axis.maximum;
    float padding_border = layout_boundary_padding_border_axis(block->bound, horizontal);
    float fit_available = available_outer;
    if (min_type == CSS_VALUE_FIT_CONTENT && min_value >= 0.0f) {
        fit_available = layout_border_size_if_content_box(block, min_value, horizontal);
    } else if (max_type == CSS_VALUE_FIT_CONTENT && max_value >= 0.0f) {
        fit_available = layout_border_size_if_content_box(block, max_value, horizontal);
    }
    float fit_border = layout_resolve_intrinsic_size_keyword(
        CSS_VALUE_FIT_CONTENT, intrinsic_min + padding_border,
        intrinsic_max + padding_border, fit_available);
    return layout_uses_border_box(block)
        ? fit_border : layout_content_size_from_border_box(block, fit_border, horizontal);
}

static float layout_intrinsic_constraint_value(CssEnum type, float intrinsic_min,
                                               float intrinsic_max, float fit_content) {
    return type == CSS_VALUE_MIN_CONTENT ? intrinsic_min
        : type == CSS_VALUE_MAX_CONTENT ? intrinsic_max : fit_content;
}

static void layout_store_intrinsic_axis_constraints(ViewBlock* block, bool horizontal,
                                                    bool resolve_min, bool resolve_max,
                                                    float intrinsic_min, float intrinsic_max,
                                                    float fit_content) {
    if (!block || !block->blk) return;
    BlockProp* props = block->block_mut();
    LayoutAxisRefs axis(props, horizontal);
    if (resolve_min) {
        *axis.minimum = layout_intrinsic_constraint_value(
            *axis.minimum_type, intrinsic_min, intrinsic_max, fit_content);
        *axis.minimum = layout_border_size_if_content_box(block, *axis.minimum, horizontal);
        *axis.minimum_type = CSS_VALUE__UNDEF;
    }
    if (resolve_max) {
        *axis.maximum = layout_intrinsic_constraint_value(
            *axis.maximum_type, intrinsic_min, intrinsic_max, fit_content);
        *axis.maximum = layout_border_size_if_content_box(block, *axis.maximum, horizontal);
        *axis.maximum_type = CSS_VALUE__UNDEF;
    }
}

bool layout_block_resolve_intrinsic_axis_constraints(LayoutContext* lycon,
                                                     ViewBlock* block,
                                                     LayoutAxis selected_axis,
                                                     float content_width) {
    bool horizontal = selected_axis == LAYOUT_AXIS_X;
    if (!lycon || !block || !block->blk) return false;
    if (block->view_type == RDT_VIEW_TABLE) return false;
    if (horizontal && block->tag() == MARKUP_NAME_CANVAS &&
        layout_axis_uses_stretch_size(block->blk, LAYOUT_AXIS_Y) &&
        (block->block()->given_min_width_type == CSS_VALUE_FIT_CONTENT ||
         block->block()->given_max_width_type == CSS_VALUE_FIT_CONTENT)) {
        return false;
    }
    BlockProp* props = block->blk;
    LayoutAxisRefs refs(props, selected_axis);
    CssEnum min_type = *refs.minimum_type;
    CssEnum max_type = *refs.maximum_type;
    bool preserve_ratio_min_content = layout_preserve_ratio_transferred_min_content(block, horizontal);
    bool has_intrinsic_min = (min_type == CSS_VALUE_MIN_CONTENT ||
        min_type == CSS_VALUE_MAX_CONTENT || min_type == CSS_VALUE_FIT_CONTENT) &&
        !preserve_ratio_min_content;
    bool has_intrinsic_max = max_type == CSS_VALUE_MIN_CONTENT ||
        max_type == CSS_VALUE_MAX_CONTENT || max_type == CSS_VALUE_FIT_CONTENT;
    if (!has_intrinsic_min && !has_intrinsic_max) return false;
    float intrinsic_min = 0.0f;
    float intrinsic_max = 0.0f;
    if (horizontal) {
        if (layout_block_inline_axis_is_vertical(block)) {
            float inline_measure = lycon->block.parent
                ? lycon->block.parent->content_height : -1.0f;
            inline_measure -= layout_box_metrics(block).pad_border_v;
            if (inline_measure < 0.0f) return false;
            intrinsic_min = intrinsic_max = layout_block_intrinsic_content_height(
                lycon, block, inline_measure);
        } else if (!layout_block_intrinsic_content_widths(
                       lycon, block, &intrinsic_min, &intrinsic_max)) {
            return false;
        }
        bool definite_width = props->given_width >= 0.0f &&
            props->given_width_type != CSS_VALUE_AUTO &&
            props->given_width_type != CSS_VALUE_MIN_CONTENT &&
            props->given_width_type != CSS_VALUE_MAX_CONTENT &&
            props->given_width_type != CSS_VALUE_FIT_CONTENT &&
            props->given_width_type != CSS_VALUE_STRETCH;
        if (max_type == CSS_VALUE_FIT_CONTENT && props->given_max_width >= 0.0f &&
            definite_width) {
            float definite_content_width = layout_css_size_to_content_box(
                block->bound, layout_box_sizing(block), props->given_width, true);
            intrinsic_min = intrinsic_max = definite_content_width;
        }
    } else {
        if (props->given_height >= 0.0f && block->is_element() &&
            layout_has_cyclic_percentage_replaced_descendant(block->as_element())) {
            return false;
        }
        float intrinsic_query_width = content_width;
        if (props->given_width_type == CSS_VALUE_MIN_CONTENT ||
            props->given_width_type == CSS_VALUE_MAX_CONTENT) {
            float min_width = 0.0f;
            float max_width = 0.0f;
            if (layout_block_intrinsic_content_widths(lycon, block, &min_width, &max_width)) {
                intrinsic_query_width = props->given_width_type == CSS_VALUE_MIN_CONTENT
                    ? min_width : max_width;
            }
        }
        float intrinsic_height = layout_block_intrinsic_content_height(
            lycon, block, intrinsic_query_width);
        if (intrinsic_height < 0.0f) return false;
        intrinsic_min = intrinsic_max = intrinsic_height;
        CssEnum preferred_width_keyword = layout_intrinsic_preferred_size_keyword(block, true);
        float preferred_aspect_ratio = layout_used_preferred_aspect_ratio(block);
        if (block->display.inner == RDT_DISPLAY_REPLACED &&
            preferred_width_keyword == CSS_VALUE_MAX_CONTENT && preferred_aspect_ratio > 0.0f) {
            float min_width = 0.0f;
            float max_width = 0.0f;
            if (layout_block_intrinsic_content_widths(lycon, block, &min_width, &max_width)) {
                float ratio_height = max_width / preferred_aspect_ratio;
                if (!layout_aspect_ratio_uses_content_box(block) && layout_uses_border_box(block)) {
                    float ratio_width_border = layout_border_size_from_content_box(block, max_width, true);
                    ratio_height = layout_content_size_from_border_box(
                        block, ratio_width_border / preferred_aspect_ratio, false);
                }
                intrinsic_min = intrinsic_max = ratio_height;
            }
        }
    }
    layout_store_intrinsic_axis_constraints(
        block, horizontal, has_intrinsic_min, has_intrinsic_max,
        intrinsic_min, intrinsic_max,
        (min_type == CSS_VALUE_FIT_CONTENT || max_type == CSS_VALUE_FIT_CONTENT)
            ? layout_resolve_intrinsic_fit_axis(
                lycon, block, horizontal, intrinsic_min, intrinsic_max) : -1.0f);
    return true;
}

static float layout_definite_abspos_content_height(ViewBlock* block) {
    if (!block || !block->blk || !block->position ||
        !block->positionp()->has_top || !block->positionp()->has_bottom) {
        return -1.0f;
    }
    if (block->block()->given_min_height_type == CSS_VALUE_STRETCH ||
        block->block()->given_max_height_type == CSS_VALUE_STRETCH) {
        return -1.0f;
    }
    return layout_block_given_content_size(block, false);
}

static void layout_block_prepare_canvas_auto_size(
    LayoutContext* lycon, ViewBlock* block,
    ContainIntrinsicUsedAxes contain_intrinsic_used_axes) {
    if (!lycon || !block || block->tag() != MARKUP_NAME_CANVAS || !block->blk) return;
    // CSS Sizing 4 treats stretch as auto when its containing block axis is
    bool stretch_height_has_definite_parent = lycon->block.parent &&
        lycon->block.parent->given_height >= 0.0f;
    if (layout_axis_uses_stretch_size(block->blk, LAYOUT_AXIS_X) ||
        (layout_axis_uses_stretch_size(block->blk, LAYOUT_AXIS_Y) &&
         stretch_height_has_definite_parent)) {
        return;
    }
    bool width_is_automatic = layout_css_size_is_automatic(block, true);
    bool height_is_automatic = layout_css_size_is_automatic(block, false);
    ViewBlock* containing_block = layout_nearest_block_ancestor(block->parent_view());
    float containing_abspos_content_height =
        layout_definite_abspos_content_height(containing_block);
    if (!height_is_automatic && block->blk &&
        !isnan(block->block()->given_height_percent) &&
        containing_abspos_content_height >= 0.0f) {
        float resolved_height = containing_abspos_content_height *
            block->block()->given_height_percent / 100.0f;
        layout_store_given_axis(lycon, block, resolved_height, false, false);
    }
    bool containing_height_is_intrinsic = containing_block &&
        layout_axis_uses_intrinsic_size(containing_block->blk, LAYOUT_AXIS_Y);
    // An unresolved percentage height on a canvas is auto only when its
    if (!height_is_automatic && containing_height_is_intrinsic && block->blk &&
        !isnan(block->block()->given_height_percent)) {
        height_is_automatic = true;
    }
    // sizes, so the canvas natural dimensions must not replace the used values.
    if (contain_intrinsic_used_axes.width) {
        width_is_automatic = false;
    }
    if (contain_intrinsic_used_axes.height) {
        height_is_automatic = false;
    }
    CssEnum intrinsic_width = layout_intrinsic_preferred_size_keyword(block, true);
    CssEnum intrinsic_height = layout_intrinsic_preferred_size_keyword(block, false);
    bool has_intrinsic_height_constraint =
        block->block()->given_min_height_type == CSS_VALUE_MIN_CONTENT ||
        block->block()->given_min_height_type == CSS_VALUE_MAX_CONTENT ||
        block->block()->given_min_height_type == CSS_VALUE_FIT_CONTENT ||
        block->block()->given_max_height_type == CSS_VALUE_MIN_CONTENT ||
        block->block()->given_max_height_type == CSS_VALUE_MAX_CONTENT ||
        block->block()->given_max_height_type == CSS_VALUE_FIT_CONTENT;
    if (has_intrinsic_height_constraint &&
        (intrinsic_width != CSS_VALUE__UNDEF ||
         (block->tag() == MARKUP_NAME_CANVAS && width_is_automatic &&
          !block->block()->content_visibility_hidden))) {
        width_is_automatic = true;
        height_is_automatic = true;
    }
    if (intrinsic_width != CSS_VALUE__UNDEF &&
        !block->block()->content_visibility_hidden &&
        !contain_intrinsic_used_axes.width) {
        width_is_automatic = true;
    }
    if (intrinsic_height != CSS_VALUE__UNDEF &&
        !block->block()->content_visibility_hidden &&
        !contain_intrinsic_used_axes.height) {
        height_is_automatic = true;
    }
    if (intrinsic_width != CSS_VALUE__UNDEF && height_is_automatic) {
        width_is_automatic = true;
    }
    if (intrinsic_height != CSS_VALUE__UNDEF && width_is_automatic) {
        height_is_automatic = true;
    }
    if (!width_is_automatic && !height_is_automatic) return;
    float natural_width = 0.0f;
    float natural_height = 0.0f;
    if (!layout_canvas_natural_size(block, &natural_width, &natural_height) ||
        natural_width <= 0.0f || natural_height <= 0.0f) {
        return;
    }
    bool canvas_has_css_preferred_ratio =
        layout_preferred_aspect_ratio(block) > 0.0f;
    if (block->tag() == MARKUP_NAME_CANVAS &&
        !block->block()->content_visibility_hidden &&
        !canvas_has_css_preferred_ratio) {
        if (block->block()->given_min_height_type == CSS_VALUE_MIN_CONTENT ||
            block->block()->given_min_height_type == CSS_VALUE_MAX_CONTENT ||
            block->block()->given_min_height_type == CSS_VALUE_FIT_CONTENT) {
            block->block_mut()->given_min_height = natural_height;
            block->block_mut()->given_min_height_type = CSS_VALUE__UNDEF;
        }
        if (block->block()->given_max_height_type == CSS_VALUE_MIN_CONTENT ||
            block->block()->given_max_height_type == CSS_VALUE_MAX_CONTENT ||
            block->block()->given_max_height_type == CSS_VALUE_FIT_CONTENT) {
            block->block_mut()->given_max_height = natural_height;
            block->block_mut()->given_max_height_type = CSS_VALUE__UNDEF;
        }
    }
    float aspect_ratio = layout_used_preferred_aspect_ratio(block);
    bool ratio_uses_content_box = layout_aspect_ratio_uses_content_box(block);
    if (aspect_ratio <= 0.0f) {
        aspect_ratio = natural_width / natural_height;
        ratio_uses_content_box = true;
    }
    bool ratio_uses_border_box = !ratio_uses_content_box && layout_uses_border_box(block);
    float content_width = natural_width;
    float content_height = natural_height;
    if (!width_is_automatic && block->block()->given_width >= 0.0f) {
        content_width = layout_content_size_if_border_box(
            block, block->block()->given_width, true);
        content_width = layout_apply_min_max_axis(
            block, content_width, true, layout_uses_border_box(block));
    }
    if (!height_is_automatic && block->block()->given_height >= 0.0f) {
        content_height = layout_content_size_if_border_box(
            block, block->block()->given_height, false);
    }
    if (width_is_automatic && height_is_automatic) {
        content_height = layout_ratio_transfer_axis(
            block, content_width, true, aspect_ratio, false, false,
            ratio_uses_border_box);
        layout_apply_aspect_ratio_min_max_constraints(
            block, aspect_ratio, &content_width, &content_height);
    } else if (width_is_automatic) {
        content_width = layout_ratio_transfer_axis(
            block, content_height, false, aspect_ratio, false, false,
            ratio_uses_border_box);
    } else if (height_is_automatic) {
        content_height = layout_ratio_transfer_axis(
            block, content_width, true, aspect_ratio, false, false,
            ratio_uses_border_box);
    }
    LayoutAxisPair<bool> automatic = {width_is_automatic, height_is_automatic};
    LayoutAxisPair<float> content = {content_width, content_height};
    for (LayoutAxis axis : layout_axes()) {
        if (automatic[axis]) {
            bool horizontal = axis == LAYOUT_AXIS_X;
            float used = layout_border_size_if_content_box(block, content[axis], horizontal);
            layout_store_given_axis(lycon, block, used, horizontal, true);
        }
    }
}

static float text_wrap_balance_line_height(LayoutContext* lycon) {
    if (!lycon) return 16.0f;
    if (lycon->block.line_height > 0.0f) return lycon->block.line_height;
    if (lycon->font.current_font_size > 0.0f) return lycon->font.current_font_size * 1.2f;
    return 16.0f;
}

static int text_wrap_balance_normal_line_count(LayoutContext* lycon, ViewBlock* block,
                                               float content_width) {
    if (!lycon || !block || content_width <= 0.0f) return 1;
    float line_height = text_wrap_balance_line_height(lycon);
    float normal_height = calculate_max_content_height(lycon, static_cast<DomNode*>(block), content_width);
    if (normal_height <= line_height + 0.5f) return 1;
    int line_count = (int)ceilf((normal_height - 0.01f) / line_height); // INT_CAST_OK: line-count estimate for balance wrapping
    if (line_count < 1) line_count = 1;
    return line_count;
}

static int text_wrap_balance_intrinsic_line_count(float max_content,
                                                  float content_width) {
    if (max_content <= 0.0f || content_width <= 0.0f) return 1;
    int line_count = (int)ceilf((max_content - 0.01f) / content_width); // INT_CAST_OK: line-count estimate from intrinsic width
    return line_count > 0 ? line_count : 1;
}

static float text_wrap_balance_measure(LayoutContext* lycon, ViewBlock* block,
                                       float content_width) {
    if (!lycon || !block || !block->blk || content_width <= 0.0f) return 0.0f;
    if (lycon->block.text_wrap_style != CSS_VALUE_BALANCE) return 0.0f;
    if (block->block()->white_space == CSS_VALUE_NOWRAP || block->block()->white_space == CSS_VALUE_PRE) return 0.0f;
    if (block->display.inner != CSS_VALUE_FLOW && block->display.inner != CSS_VALUE_FLOW_ROOT) return 0.0f;
    bool clamp_active = lycon->block.line_clamp > 0;
    float max_content = 0.0f;
    float min_content = 0.0f;
    IntrinsicSizes balance_sizes = measure_element_intrinsic_widths(
        lycon, static_cast<DomElement*>(static_cast<DomNode*>(block)), true);
    BoxMetrics balance_box = layout_box_metrics(block);
    max_content = max(balance_sizes.max_content - balance_box.pad_border_h, 0.0f);
    min_content = max(balance_sizes.min_content - balance_box.pad_border_h, 0.0f);
    if (max_content <= content_width + 0.5f) return 0.0f;
    if (min_content >= content_width - 0.5f) return 0.0f;
    int target_lines = clamp_active ? lycon->block.line_clamp :
        text_wrap_balance_intrinsic_line_count(max_content, content_width);
    if (target_lines <= 1) return 0.0f;
    // CSS Text 4 permits UAs to skip balancing above a small line-count limit.
    if (target_lines > 5) return 0.0f;
    int balance_line_limit = target_lines;
    float content_measure = max_content;
    if (clamp_active) {
        // css overflow 4 §5.3: clamp the stable layout first, then balance
        // the remaining visible lines without letting a discarded unbreakable
        // tail prevent the balance pass from starting.
        balance_line_limit = max(target_lines,
            text_wrap_balance_normal_line_count(lycon, block, content_width));
        content_measure = max(max_content - min_content, 0.0f);
    }
    float lower_bound = content_measure / (float)target_lines;
    if (!clamp_active && lower_bound < min_content) {
        lower_bound = min_content;
    }
    if (lower_bound >= content_width - 0.5f) return 0.0f;
    float low = lower_bound;
    float high = content_width;
    for (int step = 0; step < 12; step++) {
        float mid = (low + high) * 0.5f;
        float measured_height = calculate_max_content_height(lycon, static_cast<DomNode*>(block), mid);
        int line_count = (int)ceilf((measured_height - 0.01f) / text_wrap_balance_line_height(lycon)); // INT_CAST_OK: probe result as line count
        if (line_count <= balance_line_limit) {
            high = mid;
        } else {
            low = mid;
        }
    }
    float balanced = ceilf(high * 2.0f) * 0.5f;
    if (balanced < lower_bound) balanced = lower_bound;
    if (!clamp_active && balanced < min_content) {
        balanced = min_content;
    }
    if (balanced >= content_width - 0.5f) return 0.0f;
    return balanced;
}

static ObjectViewBoxUsedRect resolve_object_view_box_rect(LayoutContext* lycon,
                                                          DomElement* element,
                                                          float intrinsic_width,
                                                          float intrinsic_height) {
    ObjectViewBoxUsedRect rect = {false, 0.0f, 0.0f, intrinsic_width, intrinsic_height};
    if (!lycon || !element || intrinsic_width <= 0.0f || intrinsic_height <= 0.0f) return rect;
    CssDeclaration* decl = dom_element_get_specified_value(element, CSS_PROPERTY_OBJECT_VIEW_BOX);
    if (!decl || !decl->value) return rect;
    if (decl->value->type == CSS_VALUE_TYPE_KEYWORD && decl->value->data.keyword == CSS_VALUE_NONE) return rect;
    if (decl->value->type != CSS_VALUE_TYPE_FUNCTION || !decl->value->data.function) return rect;
    CssFunction* func = decl->value->data.function;
    if (!func->name || !func->args) return rect;
    CssValue* args[4] = {nullptr, nullptr, nullptr, nullptr};
    int arg_count = collect_object_view_box_args(func, args, 4);
    if (strcmp(func->name, "rect") == 0 && arg_count == 4) {
        float top = 0.0f;
        float right = intrinsic_width;
        float bottom = intrinsic_height;
        float left = 0.0f;
        if (!resolve_object_view_box_component(lycon, args[0], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_height, 0.0f, &top) ||
            !resolve_object_view_box_component(lycon, args[1], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_width, intrinsic_width, &right) ||
            !resolve_object_view_box_component(lycon, args[2], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_height, intrinsic_height, &bottom) ||
            !resolve_object_view_box_component(lycon, args[3], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_width, 0.0f, &left)) {
            return rect;
        }
        rect.x = left;
        rect.y = top;
        rect.width = right - left;
        rect.height = bottom - top;
    } else if (strcmp(func->name, "xywh") == 0 && arg_count == 4) {
        if (!resolve_object_view_box_component(lycon, args[0], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_width, 0.0f, &rect.x) ||
            !resolve_object_view_box_component(lycon, args[1], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_height, 0.0f, &rect.y) ||
            !resolve_object_view_box_component(lycon, args[2], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_width, intrinsic_width, &rect.width) ||
            !resolve_object_view_box_component(lycon, args[3], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_height, intrinsic_height, &rect.height)) {
            return rect;
        }
    } else if (strcmp(func->name, "inset") == 0 && arg_count >= 1 && arg_count <= 4) {
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        float left = 0.0f;
        if (!resolve_object_view_box_component(lycon, args[0], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_height, 0.0f, &top)) return rect;
        if (arg_count == 1) {
            right = bottom = left = top;
        } else if (arg_count == 2) {
            bottom = top;
            if (!resolve_object_view_box_component(lycon, args[1], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_width, 0.0f, &right)) return rect;
            left = right;
        } else if (arg_count == 3) {
            if (!resolve_object_view_box_component(lycon, args[1], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_width, 0.0f, &right) ||
                !resolve_object_view_box_component(lycon, args[2], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_height, 0.0f, &bottom)) {
                return rect;
            }
            left = right;
        } else {
            if (!resolve_object_view_box_component(lycon, args[1], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_width, 0.0f, &right) ||
                !resolve_object_view_box_component(lycon, args[2], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_height, 0.0f, &bottom) ||
                !resolve_object_view_box_component(lycon, args[3], CSS_PROPERTY_OBJECT_VIEW_BOX, intrinsic_width, 0.0f, &left)) {
                return rect;
            }
        }
        rect.x = left;
        rect.y = top;
        rect.width = intrinsic_width - left - right;
        rect.height = intrinsic_height - top - bottom;
    } else {
        return rect;
    }
    if (rect.width < 0.0f) rect.width = 0.0f;
    if (rect.height < 0.0f) rect.height = 0.0f;
    if (rect.width <= 0.0f || rect.height <= 0.0f) return {false, 0.0f, 0.0f, intrinsic_width, intrinsic_height};
    rect.valid = true;
    return rect;
}

static const char* pseudo_resolve_quote_char(DomElement* element, bool is_open_quote, int depth) {
    DomElement* cur = element;
    CssDeclaration* quotes_decl = nullptr;
    while (cur) {
        quotes_decl = dom_element_get_specified_value(cur, CSS_PROPERTY_QUOTES);
        if (quotes_decl && quotes_decl->value) break;
        cur = cur->parent_element();
    }
    if (!quotes_decl || !quotes_decl->value) {
        return is_open_quote ? "\xe2\x80\x9c" : "\xe2\x80\x9d";
    }
    CssValue* qval = quotes_decl->value;
    if (qval->type == CSS_VALUE_TYPE_KEYWORD && qval->data.keyword == CSS_VALUE_NONE) {
        return "";
    }
    if (qval->type == CSS_VALUE_TYPE_LIST && qval->data.list.count >= 2) {
        int pair_count = qval->data.list.count / 2;
        int pair_index = depth < pair_count ? depth : pair_count - 1;
        int str_index = pair_index * 2 + (is_open_quote ? 0 : 1);
        if (str_index < qval->data.list.count) {
            CssValue* sv = qval->data.list.values[str_index];
            if (sv && sv->type == CSS_VALUE_TYPE_STRING && sv->data.string) {
                return sv->data.string;
            }
        }
    }
    if (qval->type == CSS_VALUE_TYPE_STRING && qval->data.string) {
        return qval->data.string;
    }
    return is_open_quote ? "\xe2\x80\x9c" : "\xe2\x80\x9d";
}

static void pseudo_append_child(DomElement* parent, DomNode* child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->next_sibling = nullptr;
    if (!parent->first_child) {
        child->prev_sibling = nullptr;
        parent->first_child = child;
        parent->last_child = child;
        return;
    }
    child->prev_sibling = parent->last_child;
    parent->last_child->next_sibling = child;
    parent->last_child = child;
}

static void pseudo_append_text_child(DomElement* pseudo_elem, const char* text) {
    if (!pseudo_elem || !pseudo_elem->doc || !text) return;
    size_t text_len = strlen(text);
    DomText* text_node = DomText::create_copy(text, text_len, pseudo_elem);
    if (!text_node) return;
    pseudo_append_child(pseudo_elem, static_cast<DomNode*>(text_node));
}

static DomElement* pseudo_create_image_child(LayoutContext* lycon, DomElement* pseudo_elem,
                                             const CssDeclaration* content_decl, const char* raw_url) {
    if (!lycon || !pseudo_elem || !raw_url || !raw_url[0]) return nullptr;
    DomElement* img_elem = DomElement::create(pseudo_elem->doc, "img", nullptr);
    if (!img_elem) return nullptr;
    if (!img_elem->embed) {
        img_elem->ensure_embed(lycon);
    }
    if (!img_elem->embed) return nullptr;
    char* resolved_url = resolve_css_resource_url(lycon, content_decl, raw_url);
    if (!resolved_url) return nullptr;
    img_elem->embed->img = load_image(lycon->ui_context, resolved_url);
    return img_elem;
}

static const char* pseudo_resolve_text_fragment(LayoutContext* lycon, DomElement* element,
                                                const CssValue* item, int quote_depth) {
    if (!element || !item) return nullptr;
    if (item->type == CSS_VALUE_TYPE_STRING) {
        return item->data.string ? item->data.string : "";
    }
    if (item->type == CSS_VALUE_TYPE_ATTR) {
        CSSAttrRef* attr_ref = item->data.attr_ref;
        if (attr_ref && attr_ref->name) {
            const char* attr_value = element->get_attribute(attr_ref->name);
            return attr_value ? attr_value : "";
        }
        return "";
    }
    if (item->type == CSS_VALUE_TYPE_FUNCTION) {
        CssFunction* func = item->data.function;
        if (!func || !func->name) return nullptr;
        if (strcmp(func->name, "attr") == 0 && func->arg_count > 0) {
            const char* attr_name = pseudo_css_value_extract_name(func->args[0]);
            if (!attr_name) return "";
            const char* attr_value = element->get_attribute(attr_name);
            return attr_value ? attr_value : "";
        }
        if (!lycon->counter_context) return nullptr;
        if (strcmp(func->name, "counter") == 0 && func->arg_count >= 1) {
            const char* counter_name = pseudo_css_value_extract_name(func->args[0]);
            uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL
            if (func->arg_count >= 2 && func->args[1] &&
                func->args[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                style_type = func->args[1]->data.keyword;
            }
            char* buffer = (char*)scratch_alloc(&lycon->scratch, 64);
            if (!buffer || !counter_name) return "";
            counter_format((CounterContext*)lycon->counter_context, counter_name, style_type, buffer, 64);
            return buffer;
        }
        if (strcmp(func->name, "counters") == 0 && func->arg_count >= 2) {
            const char* counter_name = pseudo_css_value_extract_name(func->args[0]);
            const char* separator = func->args[1] ? func->args[1]->data.string : ".";
            uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL
            if (func->arg_count >= 3 && func->args[2] &&
                func->args[2]->type == CSS_VALUE_TYPE_KEYWORD) {
                style_type = func->args[2]->data.keyword;
            }
            char* buffer = (char*)scratch_alloc(&lycon->scratch, 128);
            if (!buffer || !counter_name) return "";
            counters_format((CounterContext*)lycon->counter_context, counter_name,
                            separator ? separator : ".", style_type, buffer, 128);
            return buffer;
        }
        return nullptr;
    }
    int quote_type = pseudo_check_quote_content(item);
    if (quote_type == 1 || quote_type == 2) {
        return pseudo_resolve_quote_char(element, quote_type == 1, quote_depth);
    }
    if (quote_type == 3 || quote_type == 4) {
        return "";
    }
    return nullptr;
}

static bool pseudo_materialize_content_children(LayoutContext* lycon, DomElement* parent,
                                                DomElement* pseudo_elem, bool is_before) {
    if (!lycon || !parent || !pseudo_elem) return false;
    StyleTree* pseudo_styles = is_before ? parent->pseudo_style(PSEUDO_STYLE_BEFORE) : parent->pseudo_style(PSEUDO_STYLE_AFTER);
    if (!pseudo_styles) return false;
    CssDeclaration* content_decl = style_tree_get_declaration(pseudo_styles, CSS_PROPERTY_CONTENT);
    if (!content_decl || !content_decl->value) return false;
    CssValue* value = content_decl->value;
    StrBuf* text_buf = strbuf_new_cap(64);
    if (!text_buf) return false;
    bool appended_any = false;
    int open_quote_count = 0;
    int item_count = (value->type == CSS_VALUE_TYPE_LIST) ? value->data.list.count : 1;
    for (int i = 0; i < item_count; i++) {
        CssValue* item = (value->type == CSS_VALUE_TYPE_LIST) ? value->data.list.values[i] : value;
        if (!item) continue;
        CssContentImage content_image = {nullptr, 1.0f};
        bool has_content_image = css_content_replacement_image(item, &content_image);
        if (has_content_image) {
            if (text_buf->length > 0) {
                pseudo_append_text_child(pseudo_elem, text_buf->str);
                text_buf->length = 0;
                if (text_buf->str) text_buf->str[0] = '\0';
                appended_any = true;
            }
            DomElement* img_elem = pseudo_create_image_child(lycon, pseudo_elem, content_decl, content_image.url);
            if (img_elem) {
                img_elem->embed->content_image_resolution = content_image.resolution;
                pseudo_append_child(pseudo_elem, static_cast<DomNode*>(img_elem));
                appended_any = true;
            }
        } else {
            const char* fragment = pseudo_resolve_text_fragment(lycon, parent, item, open_quote_count);
            if (fragment && fragment[0]) {
                strbuf_append_str(text_buf, fragment);
            }
        }
        int quote_type = pseudo_check_quote_content(item);
        if (quote_type == 1 || quote_type == 3) {
            open_quote_count++;
        }
    }
    if (text_buf->length > 0) {
        pseudo_append_text_child(pseudo_elem, text_buf->str);
        appended_any = true;
    }
    strbuf_free(text_buf);
    return appended_any;
}
// CSS 2.1 §8.3.1: Collapse two margins according to spec rules
static inline float collapse_margins(float a, float b) {
    if (a >= 0 && b >= 0) return max(a, b);
    if (a < 0 && b < 0) return min(a, b);
    return a + b;
}
// CSS 2.1 §8.3.1: Retrieve margin chain components from a block's margin.bottom.
static inline void get_margin_chain(ViewBlock* block, float* out_pos, float* out_neg) {
    if (!block || !block->bound) { *out_pos = 0; *out_neg = 0; return; }
    if (block->boundary()->margin_chain_positive != 0 || block->boundary()->margin_chain_negative != 0) {
        *out_pos = block->boundary()->margin_chain_positive;
        *out_neg = block->boundary()->margin_chain_negative;
    } else {
        *out_pos = max(block->boundary()->margin.bottom, 0.f);
        *out_neg = min(block->boundary()->margin.bottom, 0.f);
    }
}
// CSS 2.1 §8.3.1: Store a margin value along with its chain components.
static inline void set_margin_chain(BoundaryProp* bound, float positive, float negative) {
    bound->margin.bottom = positive + negative;
    bound->margin_chain_positive = positive;
    bound->margin_chain_negative = negative;
}
// CSS 2.1 §8.3.1: Get chain components for a single margin value (not chained yet).
static inline void margin_to_chain(float margin, float* out_pos, float* out_neg) {
    *out_pos = max(margin, 0.f);
    *out_neg = min(margin, 0.f);
}
// CSS 2.1 §8.3.1: Check if a block's margin chain has non-trivial components
static inline bool has_margin_chain(BoundaryProp* bound) {
    return bound && (bound->margin_chain_positive != 0 || bound->margin_chain_negative != 0);
}

static View* layout_rendered_first_placed_child(ViewBlock* block) {
    return block ? static_cast<View*>(layout_rendered_first_child_node(block->as_element()))
                 : nullptr;
}

static void get_self_margin_chain(ViewBlock* block, float margin_top,
                                  float* positive, float* negative) {
    if (has_margin_chain(block->bound)) {
        *positive = max(block->boundary()->margin_chain_positive, max(margin_top, 0.0f));
        *negative = min(block->boundary()->margin_chain_negative, min(margin_top, 0.0f));
    } else {
        *positive = max(max(margin_top, 0.0f), max(block->boundary()->margin.bottom, 0.0f));
        *negative = min(min(margin_top, 0.0f), min(block->boundary()->margin.bottom, 0.0f));
    }
}

static ViewBlock* find_previous_margin_collapse_block(ViewBlock* block) {
    if (!block) return nullptr;
    for (View* previous = block->prev_placed_view();
         previous && previous->is_block();
         previous = previous->prev_placed_view()) {
        ViewBlock* candidate = lam::view_require_block(previous);
        if ((candidate->position && element_has_float(candidate)) ||
            layout_block_is_out_of_flow_positioned(candidate) ||
            (candidate->height == 0.0f && !candidate->bound)) {
            continue;
        }
        if (candidate->view_type != RDT_VIEW_INLINE_BLOCK && candidate->bound) return candidate;
        break;
    }
    return nullptr;
}

static void shift_margin_collapse_floats(FloatBox* floats, ViewElement* parent,
                                         ViewElement* child, float delta,
                                         const char* source_loc) {
    for (FloatBox* box = floats; box; box = box->next) {
        if (box->element && view_is_descendant_of(lam::view_require_element(box->element), parent) &&
            !view_is_descendant_of(lam::view_require_element(box->element), child)) {
            box->margin_box_top += delta;
            box->margin_box_bottom += delta;
            box->y += delta;
        }
    }
}

static inline bool is_root_element_block(ViewBlock* block) {
    return block && block->tag_id == MARKUP_NAME_HTML;
}

static bool layout_source_has_in_flow_block_child(ViewBlock* block) {
    if (!block || !block->is_element()) return false;
    DomElement* element = block->as_element();
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_element = child->as_element();
        CssEnum position = layout_specified_keyword(
            child_element, CSS_PROPERTY_POSITION, CSS_VALUE_STATIC);
        if (position == CSS_VALUE_ABSOLUTE || position == CSS_VALUE_FIXED) continue;
        DisplayValue display = resolve_display_value(child);
        if (display.outer == CSS_VALUE_BLOCK ||
            display.outer == CSS_VALUE_LIST_ITEM ||
            display.outer == CSS_VALUE_TABLE) {
            return true;
        }
    }
    return false;
}

static inline bool is_quirky_margin_tag(NameId tag) {
    static const NameId quirky_tags[] = {
        MARKUP_NAME_P, MARKUP_NAME_H1, MARKUP_NAME_H2, MARKUP_NAME_H3,
        MARKUP_NAME_H4, MARKUP_NAME_H5, MARKUP_NAME_H6, MARKUP_NAME_UL,
        MARKUP_NAME_OL, MARKUP_NAME_BLOCKQUOTE, MARKUP_NAME_PRE, MARKUP_NAME_DL,
        MARKUP_NAME_FIGURE, MARKUP_NAME_HR, MARKUP_NAME_FIELDSET,
        MARKUP_NAME_MENU, MARKUP_NAME_DIR};
    return layout_tag_in_list(tag, quirky_tags, sizeof(quirky_tags) / sizeof(*quirky_tags));
}

static inline bool has_quirky_margin(ViewBlock* block, bool top) {
    if (!block || !block->bound || !is_quirky_margin_tag(block->tag_id)) return false;
    return (top ? block->boundary_mut()->margin.top_specificity
                : block->boundary_mut()->margin.bottom_specificity) < 0;
}

static inline bool is_quirky_container(ViewBlock* block, LayoutContext* lycon) {
    if (!block || !lycon->doc || !lycon->doc->view_tree) return false;
    if (!is_quirks_mode(lycon->doc->view_tree->html_version)) return false;
    return block->tag_id == MARKUP_NAME_BODY ||
           block->tag_id == MARKUP_NAME_TD ||
           block->tag_id == MARKUP_NAME_TH;
}

bool layout_quirky_container_ignores_child_margin_bottom(
    LayoutContext* lycon, ViewBlock* container, ViewBlock* child) {
    return is_quirky_container(container, lycon) && has_quirky_margin(child, false);
}
// CSS 2.1 §10.6.4: When an ancestor block's y changes after its absolutely positioned
// collapse), the descendants' positions must be updated by the same delta.
static void adjust_abs_descendants_y(ViewElement* parent, float delta) {
    View* child = parent->first_child;
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = lam::view_require_block(child);
            bool is_positioned = vb->position &&
                vb->positionp()->position != CSS_VALUE_STATIC;
            if (is_positioned) {
                bool is_abs_fixed = layout_block_is_out_of_flow_positioned(vb);
                if (is_abs_fixed && !vb->positionp()->has_top && !vb->positionp()->has_bottom) {
                    vb->y += delta;
                    if (vb->positionp()->has_static_parent_offset_y) {
                        vb->position->static_parent_offset_y += delta;
                    }
                }
            } else {
                adjust_abs_descendants_y(lam::view_require_element(vb), delta);
            }
        }
        child = static_cast<View*>(child->next_sibling);
    }
}

static bool is_inline_substantial(ViewElement* ve);

static View* previous_collapsible_sibling(ViewBlock* block) {
    View* previous = block ? block->prev_placed_view() : nullptr;
    while (previous) {
        if (previous->is_block()) {
            ViewBlock* previous_block = lam::view_require_block(previous);
            if (!((previous_block->position && element_has_float(previous_block)) ||
                  layout_block_is_out_of_flow_positioned(previous_block))) {
                break;
            }
            previous = previous->prev_placed_view();
            continue;
        }
        // Out-of-flow-only inline trees generate no line box, so they cannot
        // separate adjoining sibling margins (CSS 2.1 §8.3.1, §9.4.2).
        if (previous->view_type == RDT_VIEW_INLINE &&
            !is_inline_substantial(lam::view_require_element(previous))) {
            previous = previous->prev_placed_view();
            continue;
        }
        break;
    }
    return previous;
}

static float sibling_margin_collapse_amount(ViewBlock* block) {
    if (!block || !block->bound || block->view_type == RDT_VIEW_INLINE_BLOCK) return 0.0f;
    View* previous = previous_collapsible_sibling(block);
    while (previous && previous->is_block()) {
        ViewBlock* previous_block = lam::view_require_block(previous);
        if (previous_block->height == 0.0f && !previous_block->bound) {
            previous = previous->prev_placed_view();
            continue;
        }
        break;
    }
    if (!previous || !previous->is_block() || previous->view_type == RDT_VIEW_INLINE_BLOCK) {
        return 0.0f;
    }
    ViewBlock* previous_block = lam::view_require_block(previous);
    if (!previous_block->bound) return 0.0f;
    float previous_margin = previous_block->boundary()->margin.bottom;
    float current_margin = block->boundary()->margin.top;
    if (previous_margin == 0.0f && current_margin == 0.0f &&
        !has_margin_chain(previous_block->bound)) {
        return 0.0f;
    }
    float collapsed;
    if (has_margin_chain(previous_block->bound)) {
        float combined_positive = max(previous_block->boundary()->margin_chain_positive,
                                      max(current_margin, 0.0f));
        float combined_negative = min(previous_block->boundary()->margin_chain_negative,
                                      min(current_margin, 0.0f));
        collapsed = combined_positive + combined_negative;
    } else {
        collapsed = collapse_margins(previous_margin, current_margin);
    }
    return previous_margin + current_margin - collapsed;
}

static void shift_descendant_float_boxes(BlockContext* bfc, ViewBlock* ancestor, float delta) {
    if (!bfc || !ancestor || delta == 0.0f) return;
    ViewElement* ancestor_element = lam::view_require_element(ancestor);
    for (int pass = 0; pass < 2; pass++) {
        FloatBox* floating = pass == 0 ? bfc->left_floats : bfc->right_floats;
        for (; floating; floating = floating->next) {
            if (!floating->element) continue;
            // float ownership follows the generated view tree; raw DOM ancestry can
            if (view_is_descendant_of(
                    lam::view_require_element(floating->element), ancestor_element)) {
                floating->margin_box_top += delta;
                floating->margin_box_bottom += delta;
                floating->y += delta;
            }
        }
    }
    block_context_recompute_lowest_float_bottom(bfc);
}

static bool radiant_verify_incremental_layout_enabled() {
    static int cached = -1;
    if (cached < 0) {
        const char* value = getenv("RADIANT_VERIFY_INCREMENTAL_LAYOUT");
        cached = (value && value[0] == '1') ? 1 : 0;
    }
    return cached == 1;
}

static void verify_incremental_layout_skip(LayoutContext* lycon, DomNode* child,
                                           float pre_advance_y) {
    if (!lycon || !child || !radiant_verify_incremental_layout_enabled()) return;
    float cached_contribution = child->layout_height_contribution;
    {
        radiant::LayoutMeasureScope verify_scope(lycon, child);
        lycon->run_mode = radiant::RunMode::PerformLayout;
        layout_flow_node(lycon, child);
        float replayed_contribution = lycon->block.advance_y - pre_advance_y;
        float delta = fabsf(replayed_contribution - cached_contribution);
        if (delta > 0.5f) {
            log_error("[RAD_VERIFY_INCREMENTAL] stale layout_height_contribution for %s: cached=%.3f replayed=%.3f",
                      child->source_loc(), cached_contribution, replayed_contribution);
            assert(delta <= 0.5f);
        }
    }
}

extern double g_table_layout_time;
extern double g_flex_layout_time;
extern double g_grid_layout_time;
extern double g_block_layout_time;
extern int64_t g_block_layout_count;

extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);
void resolve_inline_default(LayoutContext* lycon, ViewSpan* span);
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_form_control(LayoutContext* lycon, ViewBlock* block);
void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line);

bool wrap_orphaned_table_children(LayoutContext* lycon, DomElement* parent);

static DomElement* create_pseudo_element(LayoutContext* lycon, DomElement* parent,
                                          const char* content, bool is_before,
                                          FontProp* parent_font) {
    if (!lycon || !parent) return nullptr;
    DomElement* pseudo_elem = DomElement::create(parent->doc, is_before ? "::before" : "::after", nullptr);
    if (!pseudo_elem) return nullptr;
    pseudo_elem->parent = parent;
    pseudo_elem->first_child = nullptr;
    pseudo_elem->last_child = nullptr;
    pseudo_elem->next_sibling = nullptr;
    pseudo_elem->prev_sibling = nullptr;
    // IMPORTANT: Do NOT share parent's FontProp pointer with pseudo-element!
    pseudo_elem->font = nullptr;
    // pseudo_elem->bound = parent->bound;  // BUG: causes shared BackgroundProp
    pseudo_elem->bound = nullptr;  // Will be allocated when CSS properties are applied
    // pseudo_elem->in_line = parent->in_line;  // BUG: causes shared opacity
    pseudo_elem->in_line = nullptr;  // Will be allocated when CSS properties are applied
    pseudo_elem->display.outer = CSS_VALUE_INLINE;
    pseudo_elem->display.inner = CSS_VALUE_FLOW;
    StyleTree* pseudo_styles = is_before ? parent->pseudo_style(PSEUDO_STYLE_BEFORE) : parent->pseudo_style(PSEUDO_STYLE_AFTER);
    if (pseudo_styles && pseudo_styles->tree) {
        dom_element_borrow_specified_style(pseudo_elem, pseudo_styles);
        // Generated pseudo boxes use the same display cascade as authored elements;
        // resolving only a few keyword cases left display:list-item pseudos inline.
        pseudo_elem->display = resolve_display_value(pseudo_elem);
    }
    bool has_counter_content = false;
    if (pseudo_styles) {
        CssDeclaration* content_decl = style_tree_get_declaration(pseudo_styles, CSS_PROPERTY_CONTENT);
        CssValue* content_value = content_decl ? content_decl->value : nullptr;
        int item_count = (content_value && content_value->type == CSS_VALUE_TYPE_LIST) ? content_value->data.list.count : 1;
        for (int i = 0; content_value && i < item_count; i++) {
            CssValue* item = (content_value->type == CSS_VALUE_TYPE_LIST) ? content_value->data.list.values[i] : content_value;
            if (item && item->type == CSS_VALUE_TYPE_FUNCTION && item->data.function && item->data.function->name &&
                (strcmp(item->data.function->name, "counter") == 0 || strcmp(item->data.function->name, "counters") == 0)) {
                has_counter_content = true;
                break;
            }
        }
    }
    bool materialized_children = has_counter_content ? false :
        pseudo_materialize_content_children(lycon, parent, pseudo_elem, is_before);
    if (!materialized_children && ((content && *content) || has_counter_content)) {
        log_info("%s [PSEUDO] Creating fallback text node for pseudo-element, content_len=%zu, first_byte=0x%02x", parent->source_loc(),
            content ? strlen(content) : 0, content ? (unsigned char)*content : 0);
        pseudo_append_text_child(pseudo_elem, content ? content : "");
    } else if (!materialized_children) {
        log_info("%s [PSEUDO] NOT creating text node: content=%p, first_byte=%s", parent->source_loc(),
            (void*)content, content ? ((*content) ? "nonzero" : "ZERO") : "NULL");
    }
    return pseudo_elem;
}
// drifting while preserving that ordering invariant.
static const char* resolve_pseudo_generated_content(LayoutContext* lycon,
                                                    DomElement* element,
                                                    bool is_before) {
    if (!lycon || !element) return nullptr;
    PseudoElementType pseudo = is_before ? PSEUDO_ELEMENT_BEFORE : PSEUDO_ELEMENT_AFTER;
    // CSS Lists 3 §4.4.1: generated content takes its counters from its
    // flattened-tree position, so resolve it when the pseudo box is laid out.
    return dom_element_get_pseudo_element_content(element, pseudo);
}

void layout_update_pseudo_content_with_counters(LayoutContext* lycon,
                                                DomElement* pseudo_element) {
    if (!lycon || !lycon->counter_context || !pseudo_element ||
        !pseudo_element->parent || !pseudo_element->parent->is_element()) {
        return;
    }
    bool is_before = pseudo_element->tag_name &&
        strcmp(pseudo_element->tag_name, "::before") == 0;
    bool is_after = pseudo_element->tag_name &&
        strcmp(pseudo_element->tag_name, "::after") == 0;
    if (!is_before && !is_after) return;

    DomElement* origin = lam::dom_as<DOM_NODE_ELEMENT>(pseudo_element->parent);
    PseudoElementType pseudo = is_before ? PSEUDO_ELEMENT_BEFORE : PSEUDO_ELEMENT_AFTER;
    StyleTree* style = origin->pseudo_style(
        is_before ? PSEUDO_STYLE_BEFORE : PSEUDO_STYLE_AFTER);
    apply_pseudo_counter_ops(lycon, style);
    const char* content = dom_element_get_pseudo_element_content_with_counters(
        origin, pseudo, lycon->counter_context, lycon->scratch.arena);
    if (!content) content = dom_element_get_pseudo_element_content(origin, pseudo);
    if (!content) content = "";
    DomNode* first = pseudo_element->first_child;
    if (first && first->is_text()) {
        DomText* text_node = lam::dom_as<DOM_NODE_TEXT>(first);
        size_t content_len = strlen(content);
        String* text_string = dom_document_create_string(
            pseudo_element->doc, content, content_len);
        if (text_string) {
            dom_text_adopt_document_string(
                text_node, pseudo_element->doc, text_string);
            text_node->rect = nullptr;
        }
    } else if (content[0]) {
        pseudo_append_text_child(pseudo_element, content);
    }
}

PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block || !block->is_element()) return nullptr;
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    if (block->pseudo && block->pseudo->before_generated && block->pseudo->after_generated) {
        return block->pseudo;
    }
    bool has_before = dom_element_has_before_content(elem);
    bool has_after = dom_element_has_after_content(elem);
    if (!has_before && !has_after) return block->pseudo;  // Return existing (may have marker) or nullptr
    PseudoContentProp* pseudo = block->pseudo;
    if (!pseudo) {
        pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
        if (!pseudo) return nullptr;
        memset(pseudo, 0, sizeof(PseudoContentProp));
    }
    if (has_before && !pseudo->before_generated) {
        log_info("%s [PSEUDO] Getting before content for <%s>", block->source_loc(), elem->tag_name ? elem->tag_name : "?");
        const char* before_content = resolve_pseudo_generated_content(lycon, elem, true);
        pseudo->before = create_pseudo_element(lycon, elem, before_content ? before_content : "", true, block->font);
        pseudo->before_generated = true;
    }
    if (has_after && !pseudo->after_generated) {
        const char* after_content = resolve_pseudo_generated_content(lycon, elem, false);
        pseudo->after = create_pseudo_element(lycon, elem, after_content ? after_content : "", false, block->font);
        pseudo->after_generated = true;
    }
    return pseudo;
}

static bool is_first_letter_punctuation(utf8proc_int32_t codepoint) {
    utf8proc_category_t cat = utf8proc_category(codepoint);
    return cat == UTF8PROC_CATEGORY_PS ||  // open punctuation: ( [ {
           cat == UTF8PROC_CATEGORY_PE ||  // close punctuation: ) ] }
           cat == UTF8PROC_CATEGORY_PI ||  // initial quote: « " '
           cat == UTF8PROC_CATEGORY_PF ||  // final quote: » " '
           cat == UTF8PROC_CATEGORY_PO;    // other punctuation: ! @ # % & * , . / : ; ? \ etc.
}

static int find_first_letter_boundary(const unsigned char* text, int text_len) {
    if (!text || text_len <= 0) return 0;
    const unsigned char* p = text;
    const unsigned char* end = text + text_len;
    bool found_letter = false;
    const unsigned char* after_letter = nullptr;
    while (p < end) {
        unsigned char ch = *p;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            p++;
        } else {
            break;
        }
    }
    const unsigned char* content_start = p;
    while (p < end) {
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &codepoint);
        if (bytes <= 0) break;
        if (!found_letter) {
            if (is_first_letter_punctuation(codepoint)) {
                p += bytes;  // include leading punctuation
            } else {
                p += bytes;
                found_letter = true;
                after_letter = p;
            }
        } else {
            if (is_first_letter_punctuation(codepoint)) {
                p += bytes;
                after_letter = p;
            } else {
                break;  // non-punctuation after letter — done
            }
        }
    }
    if (!found_letter) return 0;
    return (int)(after_letter - content_start);
}

static bool is_text_all_whitespace(const unsigned char* data) {
    if (!data) return true;
    while (*data) {
        unsigned char c = *data;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
            return false;
        }
        data++;
    }
    return true;
}

static DomText* find_first_text_node(DomNode* node, bool* suppressed) {
    if (!node) return nullptr;
    if (node->is_text()) {
        DomText* text = node->as_text();
        unsigned char* data = node->text_data();
        if (data && *data && !is_text_all_whitespace(data)) {
            return text;
        }
        return nullptr;
    }
    if (node->is_element()) {
        DomElement* elem = node->as_element();
        // CSS 2.1 §5.12.2: If a replaced element (image, video, etc.) or other
        // must not be created. Check for replaced elements before recursing.
        NameId tag = elem->tag();
        bool is_replaced = (elem->display.inner == RDT_DISPLAY_REPLACED) ||
            tag == MARKUP_NAME_IMG || tag == MARKUP_NAME_VIDEO || tag == MARKUP_NAME_CANVAS ||
            tag == MARKUP_NAME_IFRAME || tag == MARKUP_NAME_EMBED || tag == MARKUP_NAME_OBJECT ||
            tag == MARKUP_NAME_INPUT || tag == MARKUP_NAME_TEXTAREA || tag == MARKUP_NAME_SELECT ||
            tag == MARKUP_NAME_SVG || tag == MARKUP_NAME_BR || tag == MARKUP_NAME_AUDIO;
        if (is_replaced) {
            if (suppressed) *suppressed = true;
            return nullptr;
        }
        DomNode* child = elem->first_child;
        while (child) {
            DomText* result = find_first_text_node(child, suppressed);
            if (result) return result;
            if (suppressed && *suppressed) return nullptr;
            child = child->next_sibling;
        }
    }
    return nullptr;
}

static void create_first_letter_pseudo(LayoutContext* lycon, ViewBlock* block) {
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    if (!elem->pseudo_style(PSEUDO_STYLE_FIRST_LETTER)) return;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (child->is_element() && child->as_element()->tag_name &&
            strcmp(child->as_element()->tag_name, "::first-letter") == 0) {
            // keep the existing split; re-running layout must not nest pseudo-elements.
            return;
        }
    }
    // CSS 2.1 §5.12.2: If non-eligible content (e.g., an image) precedes the first
    // letter, ::first-letter must not be created.
    bool suppressed = false;
    DomText* text_node = find_first_text_node(elem, &suppressed);
    if (!text_node || suppressed) {
        return;
    }
    unsigned char* text_data = text_node->text_data();
    if (!text_data || !*text_data) return;
    int text_len = (int)strlen((const char*)text_data); // INT_CAST_OK: string length
    CssEnum white_space = get_white_space_value(text_node);
    bool preserves_space_advance = white_space_preserves_space_advance(white_space);
    const unsigned char* p = text_data;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (!*p) return;
    int ws_offset = (int)(p - text_data);
    int boundary = find_first_letter_boundary(p, text_len - ws_offset);
    if (boundary <= 0) {
        return;
    }
    Pool* pool = lycon->doc->view_tree->prop_pool;
    if (!pool) return;
    DomElement* fl_elem = lam::pool_alloc_dom_element(pool);
    if (!fl_elem) return;
    dom_element_retain_tag_name(fl_elem, lam::borrow_const(lam::promote_to_pool(pool, "::first-letter")));
    fl_elem->doc = elem->doc;
    fl_elem->parent = text_node->parent;  // same parent as the text node
    dom_element_borrow_specified_style(
        fl_elem, elem->pseudo_style(PSEUDO_STYLE_FIRST_LETTER));
    // Set display — default to inline, but check for float (CSS 2.1 §5.12.2)
    // block-level box (CSS 2.1 §9.7 blockification)
    CssEnum fl_float_value = layout_specified_keyword(
        fl_elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
    if (fl_float_value == CSS_VALUE_LEFT || fl_float_value == CSS_VALUE_RIGHT) {
        // CSS 2.1 §9.7: floated elements are blockified
        fl_elem->display.outer = CSS_VALUE_BLOCK;
        fl_elem->display.inner = CSS_VALUE_FLOW;
        fl_elem->ensure_position(lycon);
        fl_elem->position->float_prop = fl_float_value;
    } else {
        fl_elem->display.outer = CSS_VALUE_INLINE;
        fl_elem->display.inner = CSS_VALUE_FLOW;
    }
    int preserved_prefix = preserves_space_advance ? ws_offset : 0;
    int first_letter_length = preserved_prefix + boundary;
    char* fl_text = (char*)pool_calloc(pool, first_letter_length + 1);
    if (!fl_text) return;
    memcpy(fl_text, preserves_space_advance ? text_data : p, first_letter_length);
    fl_text[first_letter_length] = '\0';
    DomText* fl_text_node = lam::pool_alloc_dom_text(pool);
    if (!fl_text_node) return;
    fl_text_node->parent = fl_elem;
    fl_text_node->text = fl_text;
    fl_text_node->length = first_letter_length;
    fl_elem->first_child = fl_text_node;
    int skip = ws_offset + boundary;
    text_node->text = text_node->text + skip;
    text_node->length = text_node->length > (size_t)skip ? text_node->length - skip : 0;
    DomNode* text_parent = text_node->parent;
    fl_elem->parent = text_parent;
    fl_elem->next_sibling = text_node;
    fl_elem->prev_sibling = text_node->prev_sibling;
    if (text_node->prev_sibling) {
        text_node->prev_sibling->next_sibling = fl_elem;
    } else if (text_parent && text_parent->is_element()) {
        lam::dom_require<DOM_NODE_ELEMENT>(text_parent)->first_child = fl_elem;
    }
    text_node->prev_sibling = fl_elem;
}

static View* margin_collapse_last_in_flow_child(ViewBlock* block) {
    View* last = nullptr;
    for (View* child = layout_rendered_first_placed_child(block); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type && child->is_block()) {
            ViewBlock* candidate = lam::view_require_block(child);
            bool out_of_flow = candidate->view_type == RDT_VIEW_INLINE_BLOCK ||
                layout_block_is_out_of_flow_positioned(candidate) ||
                (candidate->position && element_has_float(candidate));
            if (!out_of_flow) last = child;
        } else if (child->view_type) {
            last = child;
        }
    }
    return last;
}

static View* previous_collapsible_sibling(ViewBlock* block);

static View* margin_collapse_effective_last_child(View* child) {
    while (child) {
        if (child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
            float margin_bottom = block->bound ? block->boundary()->margin.bottom : 0.0f;
            bool has_chain = block->bound && has_margin_chain(block->bound);
            if (margin_bottom == 0.0f && !has_chain && layout_block_is_self_collapsing(block)) {
                child = previous_collapsible_sibling(block);
                continue;
            }
            break;
        }
        if (child->height > 0.0f) break;
        child = child->prev_placed_view();
    }
    return child;
}

static bool margin_collapse_has_separating_content_after(View* child,
                                                          bool include_unlaid_content = false) {
    for (View* sibling = child ? static_cast<View*>(child->next_sibling) : nullptr;
         sibling; sibling = static_cast<View*>(sibling->next_sibling)) {
        if (!sibling->view_type) {
            if (include_unlaid_content && sibling->is_element()) {
                DisplayValue display = resolve_display_value(static_cast<void*>(sibling));
                if (display.outer != CSS_VALUE_NONE && display.outer != CSS_VALUE_INLINE) {
                    return true;
                }
            }
            continue;
        }
        if (!sibling->is_block()) {
            if (sibling->view_type == RDT_VIEW_INLINE && sibling->is_element() &&
                sibling->as_element()->display.outer == CSS_VALUE_CONTENTS &&
                is_inline_substantial(lam::view_require_element(sibling))) {
                // CSS Display 3: descendants of a boxless sibling remain inline
                // content and therefore separate a preceding block's bottom margin.
                return true;
            }
            if (sibling->height > 0.0f ||
                (include_unlaid_content && sibling->view_type == RDT_VIEW_TEXT)) {
                return true;
            }
            continue;
        }
        ViewBlock* block = lam::view_require_block(sibling);
        if (block->view_type == RDT_VIEW_INLINE_BLOCK) return true;
        bool out_of_flow = layout_block_is_out_of_flow_positioned(block) ||
            (block->position && element_has_float(block));
        if (!out_of_flow && block->height > 0.0f) return true;
        if (!out_of_flow && include_unlaid_content) {
            BoxMetrics box = layout_box_metrics(block);
            bool has_separating_box = box.border.top > 0.0f || box.border.bottom > 0.0f ||
                box.padding.top > 0.0f || box.padding.bottom > 0.0f ||
                layout_axis_has_given_size(block, false) || block->first_child != nullptr;
            if (has_separating_box) return true;
        }
    }
    return false;
}
// CSS 2.1 §10.6.3 + §8.3.1 + erratum q313: Compute the amount of bottom margin
// that must be excluded from auto height BEFORE min/max-height constraints.
// collapses with the parent's bottom margin. That collapsed margin must be
static float compute_collapsible_bottom_margin(ViewBlock* block) {
    // CSS 2.1 §8.3.1: Root element margins do not collapse.
    if (!block->parent || !block->parent->is_block()) return 0;
    // CSS Box 4 §3.1: When margin-trim:block-end is set, the last child's
    if (block->blk && (block->block()->margin_trim & MARGIN_TRIM_BLOCK_END)) return 0;
    bool has_border_bottom = block->bound && block->boundary_mut()->border && block->boundary_mut()->border->width.bottom > 0;
    bool has_padding_bottom = block->bound && block->boundary_mut()->padding.bottom > 0;
    if (has_border_bottom || has_padding_bottom) return 0;
    if (block_context_establishes_bfc(block)) return 0;
    if (layout_axis_has_given_size(block, false)) return 0;
    if (!layout_rendered_first_placed_child(block)) return 0;
    View* last_in_flow = margin_collapse_last_in_flow_child(block);
    // CSS 2.1 §9.2.1.1: Inline content between/after block children is wrapped
    View* effective_last = margin_collapse_effective_last_child(last_in_flow);
    if (!effective_last || !effective_last->is_block()) return 0;
    ViewBlock* last = lam::view_require_block(effective_last);
    // CSS 2.1 §8.3.1: Bottom margins collapse regardless of sign.
    if (!last->bound || (last->boundary()->margin.bottom == 0 && !has_margin_chain(last->bound))) return 0;
    if (margin_collapse_has_separating_content_after(effective_last)) return 0.0f;
    // CSS 2.1 §8.3.1: If the last child's margin chain includes a self-collapsing
    if (last->boundary()->clearance_in_margin_chain) {
        return 0;
    }
    return last->boundary()->margin.bottom;
}
// CSS Inline Level 3 §5: text-box-trim

static float compute_block_lead_y(ViewBlock* block) {
    if (!block->font || !block->fontp()->font_handle) return 0;
    float ascender, descender;
    font_get_content_area_split(block->fontp()->font_handle, &ascender, &descender);
    float line_height;
    const CssValue* lh = nullptr;
    ViewElement* ancestor = lam::view_require_element(block);
    while (ancestor) {
        if (ancestor->blk && ancestor->block_mut()->line_height) {
            lh = ancestor->block()->line_height;
            if (lh->type == CSS_VALUE_TYPE_KEYWORD && lh->data.keyword == CSS_VALUE_INHERIT) {
                ancestor = ancestor->parent_view();
                continue;
            }
            break;
        }
        ancestor = ancestor->parent_view();
    }
    if (lh) {
        if (lh->type == CSS_VALUE_TYPE_KEYWORD && lh->data.keyword == CSS_VALUE_NORMAL) {
            line_height = calc_normal_line_height(block->fontp()->font_handle);
        } else if (lh->type == CSS_VALUE_TYPE_NUMBER) {
            line_height = lh->data.number.value * block->fontp()->font_size;
        } else if (lh->type == CSS_VALUE_TYPE_LENGTH) {
            line_height = (float)lh->data.length.value;
            if (lh->data.length.unit == CSS_UNIT_EM) {
                line_height *= block->fontp()->font_size;
            }
        } else {
            line_height = calc_normal_line_height(block->fontp()->font_handle);
        }
    } else {
        line_height = calc_normal_line_height(block->fontp()->font_handle);
    }
    float lead_y = max(0.0f, (line_height - (ascender + descender)) / 2);
    return lead_y;
}

static bool is_inline_level_atomic_block(View* child, ViewBlock* block) {
    if (!child || !block) return false;
    if (child->view_type == RDT_VIEW_INLINE_BLOCK) return true;
    return child->view_type == RDT_VIEW_TABLE &&
        (block->display.outer == CSS_VALUE_INLINE ||
         block->display.outer == CSS_VALUE_INLINE_BLOCK);
}

bool layout_classify_vertical_flow_child(ViewBlock* parent, View* child,
                                         LayoutVerticalFlowChild* result) {
    if (!parent || !child || !result || !child->is_block()) return false;
    ViewBlock* block = lam::view_require_block(child);
    if (!block) return false;
    result->block = block;
    result->atomic_inline = is_inline_level_atomic_block(child, block);
    result->normal_block = block->display.outer == CSS_VALUE_BLOCK ||
        block->display.outer == CSS_VALUE_LIST_ITEM ||
        block->view_type == RDT_VIEW_TABLE;
    result->orthogonal = !result->atomic_inline &&
        layout_block_inline_axis_is_vertical(parent) !=
        layout_block_inline_axis_is_vertical(block);
    result->same_flow = !result->atomic_inline &&
        layout_block_inline_axis_is_vertical(parent) &&
        layout_block_inline_axis_is_vertical(block);
    return true;
}

struct InFlowBlockEdge {
    ViewBlock* block;
    View* owner_child;
};

static InFlowBlockEdge find_in_flow_block_edge(ViewElement* container, bool find_last) {
    InFlowBlockEdge result = {};
    if (!container) return result;
    for (View* child = container->first_placed_child(); child; child = child->next()) {
        ViewBlock* candidate = nullptr;
        if (child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
            if (!layout_block_is_out_of_flow(block) && !is_inline_level_atomic_block(child, block)) {
                candidate = block;
            }
        } else if (child->view_type == RDT_VIEW_INLINE) {
            candidate = find_in_flow_block_edge(
                lam::view_require_element(child), find_last).block;
        }
        if (candidate) {
            result.block = candidate;
            result.owner_child = child;
            if (!find_last) return result;
        }
    }
    return result;
}
// block child, rather than being split into anonymous blocks per CSS 2.1 §9.2.1.1.
static ViewBlock* find_first_block_in_inline(View* inline_view) {
    if (inline_view->view_type != RDT_VIEW_INLINE) return nullptr;
    return find_in_flow_block_edge(lam::view_require_element(inline_view), false).block;
}

static bool shrink_inline_wrappers_containing_block(View* inline_view, ViewBlock* target, float trim) {
    if (!inline_view || inline_view->view_type != RDT_VIEW_INLINE || !target) return false;
    ViewElement* span = lam::view_require_element(inline_view);
    View* child = span->first_placed_child();
    while (child) {
        if (child->is_block() && lam::view_require_block(child) == target) {
            inline_view->height -= trim;
            return true;
        }
        if (child->view_type == RDT_VIEW_INLINE &&
            shrink_inline_wrappers_containing_block(child, target, trim)) {
            inline_view->height -= trim;
            return true;
        }
        child = child->next();
    }
    return false;
}

static bool inline_edge_has_content(ViewBlock* container, bool after_last_block,
                                    bool require_nonzero) {
    if (!container) return false;
    View* child = nullptr;
    if (after_last_block) {
        InFlowBlockEdge edge = find_in_flow_block_edge(container, true);
        child = edge.owner_child ? edge.owner_child->next() : nullptr;
    } else {
        child = container->first_placed_child();
    }
    while (child) {
        if (child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
            if (is_inline_level_atomic_block(child, block)) {
                return !require_nonzero || child->width > 0.0f || child->height > 0.0f;
            }
            if (!after_last_block && !layout_block_is_out_of_flow(block)) return false;
        } else if (child->view_type == RDT_VIEW_INLINE) {
            if (find_first_block_in_inline(child)) {
                if (!after_last_block) return false;
            } else if (!require_nonzero || child->width > 0.0f || child->height > 0.0f) {
                return true;
            }
        } else if (child->view_type == RDT_VIEW_TEXT &&
                   (!require_nonzero || child->width > 0.0f)) {
            return true;
        }
        child = child->next();
    }
    return false;
}

static ViewBlock* find_line_clamped_descendant_in_view(View* view);

static ViewBlock* find_line_clamped_descendant_block(ViewBlock* container) {
    View* child = container->first_placed_child();
    while (child) {
        ViewBlock* clamped = find_line_clamped_descendant_in_view(child);
        if (clamped) return clamped;
        child = child->next();
    }
    return nullptr;
}

static ViewBlock* find_line_clamped_descendant_in_view(View* view) {
    if (!view) return nullptr;
    if (view->is_block()) {
        ViewBlock* vb = lam::view_require_block(view);
        if (layout_block_is_out_of_flow(vb) || is_inline_level_atomic_block(view, vb)) {
            return nullptr;
        }
        if (vb->blk && vb->block_mut()->line_clamped && vb->block_mut()->line_clamp_inherited) {
            ViewBlock* nested = find_line_clamped_descendant_block(vb);
            return nested ? nested : vb;
        }
        return find_line_clamped_descendant_block(vb);
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        View* child = lam::view_require<RDT_VIEW_INLINE>(view)->first_placed_child();
        while (child) {
            ViewBlock* clamped = find_line_clamped_descendant_in_view(child);
            if (clamped) return clamped;
            child = child->next();
        }
    }
    return nullptr;
}

enum FormattedLineContent {
    FORMATTED_LINE_EMPTY,
    FORMATTED_LINE_INLINE,
    FORMATTED_LINE_BLOCK
};

static FormattedLineContent classify_formatted_line_content(ViewBlock* container) {
    bool has_inline_content = false;
    View* child = container->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
        if (!layout_block_is_out_of_flow(block) && !is_inline_level_atomic_block(child, block)) {
                return FORMATTED_LINE_BLOCK;
            }
            if (is_inline_level_atomic_block(child, block)) {
                has_inline_content = true;
            }
        } else if (find_first_block_in_inline(child)) {
            return FORMATTED_LINE_BLOCK;
        } else if (child->view_type == RDT_VIEW_TEXT || child->view_type == RDT_VIEW_INLINE) {
            has_inline_content = true;
        }
        child = child->next();
    }
    return has_inline_content ? FORMATTED_LINE_INLINE : FORMATTED_LINE_EMPTY;
}
// Find the block containing an edge formatted line, following CSS Inline 3 §5.
static ViewBlock* find_formatted_line_block(ViewBlock* container, bool find_last) {
    if (!container) return nullptr;
    if (find_last && container->blk && container->block_mut()->line_clamped) {
        ViewBlock* clamped_descendant = find_line_clamped_descendant_block(container);
        if (clamped_descendant) {
            return find_formatted_line_block(clamped_descendant, true);
        }
    }
    FormattedLineContent content = classify_formatted_line_content(container);
    if (content != FORMATTED_LINE_BLOCK) {
        return content == FORMATTED_LINE_INLINE ? container : nullptr;
    }
    bool has_inline = inline_edge_has_content(container, find_last, false);
    bool has_content = inline_edge_has_content(container, find_last, true);
    if (has_inline) {
        return has_content ? container : nullptr;
    }
    InFlowBlockEdge edge = find_in_flow_block_edge(container, find_last);
    return edge.block ? find_formatted_line_block(edge.block, find_last) : nullptr;
}
// text-box-edge is an inherited property (CSS Inline 3 §5.1).
static void get_text_box_edge(ViewBlock* block, CssEnum* over_edge, CssEnum* under_edge) {
    if (block->blk && block->block_mut()->text_box_over_edge != CSS_VALUE__UNDEF) {
        *over_edge = block->block()->text_box_over_edge;
        *under_edge = block->block()->text_box_under_edge;
        return;
    }
    ViewElement* parent = block->parent_view();
    while (parent) {
        ViewBlock* pb = lam::view_as_block(parent);
        if (pb && pb->blk && pb->block_mut()->text_box_over_edge != CSS_VALUE__UNDEF) {
            *over_edge = pb->block()->text_box_over_edge;
            *under_edge = pb->block()->text_box_under_edge;
            return;
        }
        parent = parent->parent_view();
    }
    *over_edge = CSS_VALUE_TEXT;
    *under_edge = CSS_VALUE_TEXT;
}

static void get_block_font_metrics(ViewBlock* block, float* ascender, float* descender) {
    if (!block->font || !block->fontp()->font_handle) {
        *ascender = *descender = 0;
        return;
    }
    font_get_content_area_split(block->fontp()->font_handle, ascender, descender);
}
// CSS Inline 3 §5: The trim is the distance from the line box's over edge
static float compute_text_box_trim(ViewBlock* line_block, CssEnum edge, bool over) {
    float block_ascender, block_descender;
    get_block_font_metrics(line_block, &block_ascender, &block_descender);
    float block_edge = over ? block_ascender : block_descender;
    float line_box_edge;
    if (line_block->blk && (over
            ? line_block->block_mut()->first_line_max_ascender > 0
            : line_block->block_mut()->last_line_max_descender > 0)) {
        line_box_edge = over ? line_block->block()->first_line_max_ascender
                             : line_block->block()->last_line_max_descender;
    } else {
        line_box_edge = block_edge + compute_block_lead_y(line_block);
    }
    if (edge == CSS_VALUE_TEXT || edge == CSS_VALUE_AUTO) {
        float trim = line_box_edge - block_edge;
        return max(0.0f, trim);
    }
    if (!over && edge == CSS_VALUE_ALPHABETIC) {
        return max(0.0f, line_box_edge);
    }
    if (over && line_block->font && line_block->font->font_handle) {
        const FontMetrics* metrics = font_get_metrics(line_block->font->font_handle);
        if (metrics) {
            float metric = edge == CSS_VALUE_CAP ? metrics->cap_height
                : edge == CSS_VALUE_EX ? metrics->x_height : block_edge;
            if (metric > 0.0f) return max(0.0f, line_box_edge - metric);
        }
    }
    return max(0.0f, line_box_edge - block_edge);
}

static void adjust_text_bounds_in_view(View* view);

static bool text_rect_overlaps_first_fragment(ViewBlock* block, TextRect* rect) {
    if (!block || !rect) return false;
    DomElement* elem = lam::dom_require_element(block);
    LayoutFragmentBox* first = elem->layout_fragment_list();
    if (!first) return false;
    float left = rect->x - block->x;
    float top = rect->y - block->y;
    float right = left + rect->width;
    float bottom = top + rect->height;
    return right > first->x && bottom > first->y &&
        left < first->x + first->width && top < first->y + first->height;
}

enum TextRectShiftMode {
    TEXT_RECT_SHIFT_ALL,
    TEXT_RECT_SHIFT_FIRST_FRAGMENT,
    TEXT_RECT_SHIFT_INLINE_ONLY
};

static void shift_text_geometry(View* view, float delta, TextRectShiftMode mode,
                                ViewBlock* first_fragment_block = nullptr) {
    if (!view) return;
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        if (mode == TEXT_RECT_SHIFT_INLINE_ONLY) view->y += delta;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (mode == TEXT_RECT_SHIFT_FIRST_FRAGMENT &&
                !text_rect_overlaps_first_fragment(first_fragment_block, rect)) {
                continue;
            }
            rect->y += delta;
        }
        if (mode == TEXT_RECT_SHIFT_FIRST_FRAGMENT) adjust_text_bounds_in_view(view);
        return;
    }
    if (mode == TEXT_RECT_SHIFT_INLINE_ONLY) {
        if (view->view_type != RDT_VIEW_INLINE) return;
        View* child = lam::view_require<RDT_VIEW_INLINE>(view)->first_placed_child();
        while (child) {
            if (!child->is_block()) {
                shift_text_geometry(child, delta, mode, first_fragment_block);
            }
            child = child->next();
        }
        return;
    }
    if (view->is_group()) {
        for (View* child = lam::view_require_element(view)->first_placed_child();
             child; child = child->next()) {
            shift_text_geometry(child, delta, mode, first_fragment_block);
        }
    }
}

static bool text_rect_y_bounds(View* view, float* out_min_y, float* out_max_y) {
    if (!view || !out_min_y || !out_max_y) return false;
    bool found = false;
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        LayoutTextRectBounds bounds = layout_text_rect_bounds(text->rect);
        if (bounds.valid) {
            *out_min_y = bounds.min_y;
            *out_max_y = bounds.max_y;
            found = true;
        }
    } else if (view->is_group()) {
        for (View* child = lam::view_require_element(view)->first_placed_child();
             child; child = child->next()) {
            float child_min_y = 0.0f;
            float child_max_y = 0.0f;
            if (!text_rect_y_bounds(child, &child_min_y, &child_max_y)) continue;
            if (!found) {
                *out_min_y = child_min_y;
                *out_max_y = child_max_y;
                found = true;
            } else {
                *out_min_y = min(*out_min_y, child_min_y);
                *out_max_y = max(*out_max_y, child_max_y);
            }
        }
    }
    return found;
}

static void adjust_text_bounds_in_view(View* view) {
    if (!view) return;
    if (view->view_type == RDT_VIEW_TEXT) {
        adjust_text_bounds(lam::view_require<RDT_VIEW_TEXT>(view));
    } else if (view->is_group()) {
        for (View* child = lam::view_require_element(view)->first_placed_child();
             child; child = child->next()) {
            adjust_text_bounds_in_view(child);
        }
    }
}

static void center_button_text_in_block(View* first_child, float block_extent) {
    if (!first_child || block_extent <= 0.0f) return;
    float min_y = 0.0f;
    float max_y = 0.0f;
    if (!text_rect_y_bounds(first_child, &min_y, &max_y)) return;
    float delta = (block_extent - (max_y - min_y)) / 2.0f - min_y;
    if (fabsf(delta) < 0.001f) return;
    shift_text_geometry(first_child, delta, TEXT_RECT_SHIFT_ALL);
    adjust_text_bounds_in_view(first_child);
}

static void shift_inline_with_block_children_y(View* view, float delta) {
    shift_text_geometry(view, delta, TEXT_RECT_SHIFT_INLINE_ONLY);
    View* child = lam::view_require<RDT_VIEW_INLINE>(view)->first_placed_child();
    while (child) {
        if (child->is_block() && !layout_block_is_out_of_flow(lam::view_require_block(child))) {
            child->y += delta;
        }
        child = child->next();
    }
}

static void shift_block_axis_content_for_alignment(ViewBlock* block, float delta) {
    View* child = block->first_placed_child();
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = lam::view_require_block(child);
            if (!layout_block_is_out_of_flow(vb)) {
                child->y += delta;
            }
        } else if (child->view_type == RDT_VIEW_INLINE) {
            child->y += delta;
            shift_inline_with_block_children_y(child, delta);
        } else {
            child->y += delta;
            shift_text_geometry(child, delta, TEXT_RECT_SHIFT_ALL);
        }
        child = child->next();
    }
}

static void apply_block_axis_content_alignment(ViewBlock* block, float flow_height) {
    if (!block || !block->blk) return;
    CssEnum align = block->block()->align_content;
    if (align == CSS_VALUE__UNDEF || align == CSS_VALUE_NORMAL || align == CSS_VALUE_STRETCH) return;
    if (block->display.inner != CSS_VALUE_FLOW && block->display.inner != CSS_VALUE_FLOW_ROOT) return;
    float free_space = block->height - flow_height;
    float offset = radiant::compute_alignment_offset_simple(align, free_space);
    if (offset == 0.0f) return;
    shift_block_axis_content_for_alignment(block, offset);
}

static bool block_has_layout_fragments(ViewBlock* block) {
    if (!block || !block->is_element()) return false;
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    return elem->layout_fragment_list() && elem->layout_fragments_count() > 1;
}

static void apply_start_trim_recursive(ViewBlock* container, ViewBlock* target, float trim) {
    if (container == target) {
        bool has_fragmented_content = block_has_layout_fragments(container);
        View* child = container->first_placed_child();
        while (child) {
            bool skip = false;
            if (child->is_block()) {
                ViewBlock* vb = lam::view_require_block(child);
                if (layout_block_is_out_of_flow_positioned(vb)) {
                    skip = true;
                }
            }
            if (!skip) {
                if (has_fragmented_content) {
                    if (child->is_block()) {
                        child->y -= trim;
                    } else {
                        shift_text_geometry(child, -trim,
                            TEXT_RECT_SHIFT_FIRST_FRAGMENT, container);
                    }
                    child = child->next();
                    continue;
                }
                bool shift_child_box = true;
                if (child->is_block()) {
                    ViewBlock* vb = lam::view_require_block(child);
                    if (is_inline_level_atomic_block(child, vb)) {
                        shift_child_box = !(vb->blk &&
                            (vb->block()->text_box_trim_applied & TEXT_BOX_TRIM_START));
                        if (shift_child_box) child->y -= trim;
                        child = child->next();
                        continue;
                    }
                }
                if (shift_child_box) child->y -= trim;
                if (child->view_type == RDT_VIEW_INLINE) {
                    shift_inline_with_block_children_y(child, -trim);
                } else {
                    shift_text_geometry(child, -trim, TEXT_RECT_SHIFT_ALL);
                }
            }
            child = child->next();
        }
        return;
    }
    View* child = container->first_placed_child();
    bool found_first = false;
    while (child) {
        if (child->is_block()) {
            ViewBlock* vb = lam::view_require_block(child);
        if (layout_block_is_out_of_flow(vb)) {
                child = child->next();
                continue;
            }
            if (is_inline_level_atomic_block(child, vb)) {
                if (found_first) {
                    child->y -= trim;
                }
                child = child->next();
                continue;
            }
            if (!found_first) {
                found_first = true;
                if (!block_has_layout_fragments(vb)) {
                    vb->height -= trim;
                    vb->content_height -= trim;
                }
                apply_start_trim_recursive(vb, target, trim);
            } else {
                child->y -= trim;
            }
        } else if (!found_first && child->view_type == RDT_VIEW_INLINE) {
            ViewBlock* bii = find_first_block_in_inline(child);
            if (bii) {
                found_first = true;
                shrink_inline_wrappers_containing_block(child, bii, trim);
                if (!block_has_layout_fragments(bii)) {
                    bii->height -= trim;
                    bii->content_height -= trim;
                }
                apply_start_trim_recursive(bii, target, trim);
            }
        } else if (found_first) {
            child->y -= trim;
            if (child->view_type == RDT_VIEW_INLINE) {
                shift_inline_with_block_children_y(child, -trim);
            } else {
                shift_text_geometry(child, -trim, TEXT_RECT_SHIFT_ALL);
            }
        }
        child = child->next();
    }
}

static void apply_end_trim_recursive(ViewBlock* container, ViewBlock* target, float trim) {
    if (container == target) {
        // CSS Inline 3: invisible line boxes (containing no glyphs with non-zero
        float last_visible_bottom = 0;
        View* child = container->first_placed_child();
        while (child) {
            if (child->view_type == RDT_VIEW_TEXT || child->view_type == RDT_VIEW_BR) {
                float bottom = child->y + child->height;
                if (bottom > last_visible_bottom) last_visible_bottom = bottom;
            }
            child = child->next();
        }
        // are structural boundaries between anonymous blocks and must not be moved.
        if (last_visible_bottom > 0) {
            child = container->first_placed_child();
            while (child) {
                if (child->y >= last_visible_bottom) {
                    if (child->is_block()) {
                        ViewBlock* vb = lam::view_require_block(child);
                        if (element_has_float(vb)) {
                            child->y -= trim;
                        }
                    } else if (child->view_type != RDT_VIEW_TEXT &&
                        child->view_type != RDT_VIEW_BR) {
                        child->y -= trim;
                    }
                }
                child = child->next();
            }
        }
        return;
    }
    InFlowBlockEdge edge = find_in_flow_block_edge(container, true);
    if (edge.block) {
        if (edge.owner_child->view_type == RDT_VIEW_INLINE) {
            shrink_inline_wrappers_containing_block(edge.owner_child, edge.block, trim);
        }
        if (!block_has_layout_fragments(edge.block)) {
            edge.block->height -= trim;
            edge.block->content_height -= trim;
        }
        apply_end_trim_recursive(edge.block, target, trim);
    }
}

static bool has_padding_or_border_between(ViewBlock* container, ViewBlock* target, bool start) {
    if (container == target) return false;
    ViewBlock* current = container;
    while (current != target) {
        ViewBlock* next_block = find_in_flow_block_edge(current, !start).block;
        if (!next_block || !next_block->bound) break;
        const BoundaryProp* bound = next_block->boundary();
        float padding = start ? bound->padding.top : bound->padding.bottom;
        float border = bound->border
            ? (start ? bound->border->width.top : bound->border->width.bottom) : 0.0f;
        if (padding > 0.0f || border > 0.0f) return true;
        current = next_block;
    }
    return false;
}
// CSS Inline 3 §5: trims half-leading from first/last formatted lines.
// CSS Inline Level 3 §5: Compute text-box-trim amounts and adjust child
static float apply_text_box_trim(ViewBlock* block, float end_trim_limit) {
    if (!block->blk) return 0;
    block->blk->text_box_trim_applied = 0;
    block->blk->text_box_trim_start_amount = 0.0f;
    block->blk->text_box_trim_end_amount = 0.0f;
    if (!block->block()->text_box_trim) return 0;
    uint8_t trim = block->block()->text_box_trim;
    float start_trim = 0, end_trim = 0;
    ViewBlock* first_line_block = nullptr;
    ViewBlock* last_line_block = nullptr;
    if (trim & TEXT_BOX_TRIM_START) {
        first_line_block = find_formatted_line_block(block, false);
        if (first_line_block && !has_padding_or_border_between(block, first_line_block, true)) {
            // CSS Inline 3 §5: text-box-edge is inherited; use the value from
            CssEnum over_edge, under_edge;
            get_text_box_edge(first_line_block, &over_edge, &under_edge);
            start_trim = compute_text_box_trim(first_line_block, over_edge, true);
        }
    }
    if (trim & TEXT_BOX_TRIM_END) {
        last_line_block = find_formatted_line_block(block, true);
        if (last_line_block && !has_padding_or_border_between(block, last_line_block, false)) {
            CssEnum over_edge, under_edge;
            get_text_box_edge(last_line_block, &over_edge, &under_edge);
            end_trim = compute_text_box_trim(last_line_block, under_edge, false);
            if (end_trim_limit >= 0.0f && end_trim > end_trim_limit) {
                end_trim = end_trim_limit;
            }
        }
    }
    if (start_trim <= 0 && end_trim <= 0) return 0;
    if (start_trim > 0) {
        block->blk->text_box_trim_applied |= TEXT_BOX_TRIM_START;
        block->blk->text_box_trim_start_amount = start_trim;
        apply_start_trim_recursive(block, first_line_block, start_trim);
    }
    if (end_trim > 0) {
        block->blk->text_box_trim_applied |= TEXT_BOX_TRIM_END;
        block->blk->text_box_trim_end_amount = end_trim;
        apply_end_trim_recursive(block, last_line_block, end_trim);
    }
    float total_trim = start_trim + end_trim;
    return total_trim;
}

struct InlineSpanRecomputeSummary {
    bool has_recomputable_box;
    bool has_atomic_child;
    bool has_in_flow_block;
};

static InlineSpanRecomputeSummary inline_span_recompute_summary(ViewSpan* span) {
    InlineSpanRecomputeSummary summary = {};
    if (!span) return summary;
    for (View* child = span->first_child; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE) continue;
        if (ViewBlock* block = lam::view_as_block(child)) {
            if (layout_block_is_out_of_flow(block)) continue;
            bool atomic = is_inline_level_atomic_block(child, block);
            bool is_inline_level_table = child->view_type == RDT_VIEW_TABLE &&
                (block->display.outer == CSS_VALUE_INLINE ||
                 block->display.outer == CSS_VALUE_INLINE_BLOCK);
            summary.has_in_flow_block = summary.has_in_flow_block ||
                (child->view_type != RDT_VIEW_INLINE_BLOCK && !is_inline_level_table);
            summary.has_atomic_child = summary.has_atomic_child || atomic;
            if (atomic) continue;
            summary.has_recomputable_box = true;
            continue;
        }
        summary.has_recomputable_box = summary.has_recomputable_box ||
            child->width > 0.0f || child->height > 0.0f;
    }
    return summary;
}

static void recompute_inline_descendant_bounds(View* view, FontHandle* fallback_fh) {
    if (!view || !view->is_element()) return;
    DomElement* element = lam::dom_require<DOM_NODE_ELEMENT>(view);
    for (DomNode* child_node = element->first_child; child_node; child_node = child_node->next_sibling) {
        recompute_inline_descendant_bounds(static_cast<View*>(child_node), fallback_fh);
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
        ViewBlock* fragment_owner = layout_nearest_block_ancestor(span);
        if (fragment_owner && layout_block_inline_axis_is_vertical(fragment_owner) &&
            fragment_owner->is_element() &&
            fragment_owner->as_element()->layout_fragments_count() > 1) {
            // CSS Fragmentation: the multicol projection is the final inline
            // union; recomputing it from the pre-fragment line boxes restores
            // the old column width after fragmentation has been resolved.
            return;
        }
        InlineSpanRecomputeSummary summary = inline_span_recompute_summary(span);
        if (!summary.has_recomputable_box || summary.has_atomic_child ||
            summary.has_in_flow_block) return;
        recompute_span_bounding_box_after_line_layout(
            span, inline_span_has_multiple_line_fragments(span), fallback_fh);
    }
}
// CSS 2.1 §10.3.3: After an inline-block shrinks to fit, its block-level children
// in normal flow with auto width must be adjusted to match the new containing block
struct DeferredInlineLineRun {
    int line_number;
    float y;
    float min_x;
    float max_x;
    bool used;
};

static bool deferred_line_run_matches(const DeferredInlineLineRun* run,
                                      int line_number, float y) {
    const float y_tolerance = 1.0f;
    if (line_number >= 0 || run->line_number >= 0) {
        return line_number >= 0 && run->line_number == line_number;
    }
    return fabsf(run->y - y) <= y_tolerance;
}

static int find_deferred_line_run(DeferredInlineLineRun* runs, int* run_count,
                                  int line_number, float y) {
    for (int i = 0; i < *run_count; i++) {
        if (deferred_line_run_matches(&runs[i], line_number, y)) {
            return i;
        }
    }
    const int max_runs = 256;
    if (*run_count >= max_runs) return -1;
    int index = *run_count;
    runs[index].line_number = line_number;
    runs[index].y = y;
    runs[index].min_x = FLT_MAX;
    runs[index].max_x = -FLT_MAX;
    runs[index].used = true;
    (*run_count)++;
    return index;
}

static void add_deferred_line_extent(DeferredInlineLineRun* runs, int* run_count,
                                     int line_number, float y,
                                     float min_x, float max_x) {
    if (max_x <= min_x) return;
    int index = find_deferred_line_run(runs, run_count, line_number, y);
    if (index < 0) return;
    if (min_x < runs[index].min_x) runs[index].min_x = min_x;
    if (max_x > runs[index].max_x) runs[index].max_x = max_x;
}

static int first_deferred_inline_line_number(View* child) {
    while (child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            if (text->rect) return text->rect->line_number;
        } else if (child->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(child);
            int line_number = first_deferred_inline_line_number(span->first_child);
            if (line_number >= 0) return line_number;
        } else if (child->view_type == RDT_VIEW_INLINE_BLOCK) {
            return child->inline_line_number;
        }
        child = child->next();
    }
    return -1;
}

static bool deferred_inline_atomic_anchor(View* child, int* line_number) {
    if (!child || !line_number) return false;
    if (child->view_type != RDT_VIEW_INLINE_BLOCK &&
        child->view_type != RDT_VIEW_BLOCK &&
        child->view_type != RDT_VIEW_LIST_ITEM) return false;
    *line_number = child->view_type == RDT_VIEW_INLINE_BLOCK
        ? child->inline_line_number : -1;
    return true;
}

static void collect_deferred_inline_line_runs(View* child, DeferredInlineLineRun* runs,
                                              int* run_count) {
    while (child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                add_deferred_line_extent(runs, run_count, rect->line_number,
                                         rect->y, rect->x,
                                         rect->x + rect->width);
            }
        } else if (child->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(child);
            if (span->width > 0.0f && span->height > 0.0f) {
                // otherwise trailing padding is treated as free space and centered into.
                int line_number = first_deferred_inline_line_number(span->first_child);
                add_deferred_line_extent(runs, run_count, line_number, span->y, span->x,
                                         span->x + span->width);
            }
            if (span->first_child) {
                collect_deferred_inline_line_runs(span->first_child, runs, run_count);
            }
        } else {
            int line_number = -1;
            if (!deferred_inline_atomic_anchor(child, &line_number)) {
                child = child->next();
                continue;
            }
            add_deferred_line_extent(runs, run_count, line_number, child->y, child->x,
                                     child->x + child->width);
        }
        child = child->next();
    }
}

static bool view_has_deferred_line_content(View* child,
                                           const DeferredInlineLineRun* run) {
    while (child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (deferred_line_run_matches(run, rect->line_number, rect->y) &&
                    rect->width > 0.0f) {
                    return true;
                }
            }
        } else if (child->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(child);
            if (span->first_child &&
                view_has_deferred_line_content(span->first_child, run)) {
                return true;
            }
        } else {
            int line_number = -1;
            if (deferred_inline_atomic_anchor(child, &line_number) &&
                deferred_line_run_matches(run, line_number, child->y)) return true;
        }
        child = child->next();
    }
    return false;
}

static void shift_deferred_inline_line(View* child,
                                       const DeferredInlineLineRun* run,
                                       float shift) {
    while (child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            bool shifted_rect = false;
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (deferred_line_run_matches(run, rect->line_number, rect->y)) {
                    rect->x += shift;
                    shifted_rect = true;
                }
            }
            if (shifted_rect) {
                text->x += shift;
            }
        } else if (child->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(child);
            if (span->first_child &&
                view_has_deferred_line_content(span->first_child, run)) {
                span->x += shift;
                shift_deferred_inline_line(span->first_child, run, shift);
            }
        } else {
            int line_number = -1;
            if (deferred_inline_atomic_anchor(child, &line_number) &&
                deferred_line_run_matches(run, line_number, child->y)) {
                child->x += shift;
            }
        }
        child = child->next();
    }
}

void layout_align_deferred_inline_line_runs(ViewElement* parent, float final_content_width,
                                             CssEnum text_align) {
    if (!parent || final_content_width <= 0.0f ||
        (text_align != CSS_VALUE_CENTER && text_align != CSS_VALUE_RIGHT)) {
        return;
    }
    DeferredInlineLineRun runs[256] = {};
    int run_count = 0;
    collect_deferred_inline_line_runs(parent->first_placed_child(), runs, &run_count);
    float padding_left = layout_box_metrics(lam::view_as_block(parent)).padding.left;
    for (int i = 0; i < run_count; i++) {
        if (!runs[i].used || runs[i].max_x <= runs[i].min_x) continue;
        float line_width = runs[i].max_x - runs[i].min_x;
        float target_x = padding_left;
        if (text_align == CSS_VALUE_CENTER) {
            target_x += (final_content_width - line_width) / 2.0f;
        } else {
            target_x += final_content_width - line_width;
        }
        float shift = target_x - runs[i].min_x;
        if (fabsf(shift) > 0.5f) {
            shift_deferred_inline_line(parent->first_placed_child(), &runs[i], shift);
        }
    }
}

static float layout_shrink_to_fit_available_width(ViewBlock* block, float containing_width) {
    if (!block || !block->bound) return containing_width;
    const BoundaryProp* bound = block->boundary();
    return containing_width -
        (bound->margin.left_type == CSS_VALUE_AUTO ? 0.0f : bound->margin.left) -
        (bound->margin.right_type == CSS_VALUE_AUTO ? 0.0f : bound->margin.right);
}

static float layout_non_auto_margin_right(ViewBlock* block) {
    return block && block->bound &&
            block->boundary()->margin.right_type != CSS_VALUE_AUTO
        ? block->boundary()->margin.right : 0.0f;
}

static float layout_strut_below_baseline(LayoutContext* lycon) {
    if (!lycon || layout_quirks_block_ignores_line_height(lycon, nullptr)) return 0.0f;
    float half_leading = (lycon->block.line_height -
        (lycon->block.init_ascender + lycon->block.init_descender)) / 2.0f;
    return max(lycon->block.init_descender + half_leading, 0.0f);
}

static bool parent_margin_collapse_uses_physical_y(ViewBlock* block) {
    if (!block || !block->parent || !block->parent->is_block()) return true;
    return !layout_block_inline_axis_is_vertical(
        lam::view_require_block(static_cast<View*>(block->parent)));
}

static bool margin_collapse_ancestor_has_clearance(ViewBlock* block) {
    for (ViewBlock* ancestor = block; ancestor; ) {
        if (ancestor->bound && ancestor->boundary()->has_clearance) return true;
        if (ancestor != block && block_context_establishes_bfc(ancestor)) break;
        View* parent_view = ancestor->parent_view();
        if (!parent_view || !parent_view->is_block()) break;
        ancestor = lam::view_require_block(parent_view);
    }
    return false;
}

float layout_vertical_flow_block_start_margin(ViewBlock* child, WritingMode parent_mode) {
    if (!child || !child->bound) return 0.0f;
    return parent_mode == WM_VERTICAL_RL
        ? child->boundary()->margin.right : child->boundary()->margin.left;
}

float layout_vertical_flow_block_end_margin(ViewBlock* child, WritingMode parent_mode) {
    if (!child || !child->bound) return 0.0f;
    return parent_mode == WM_VERTICAL_RL
        ? child->boundary()->margin.left : child->boundary()->margin.right;
}

static float vertical_inline_prefix_before_block(ViewBlock* parent) {
    if (!parent || !parent->is_element()) return 0.0f;
    float prefix = 0.0f;
    for (View* child = lam::view_require_element(parent)->first_placed_child();
         child; child = child->next()) {
        if (child->is_block()) {
            LayoutVerticalFlowChild info = {};
            if (layout_classify_vertical_flow_child(parent, child, &info) &&
                info.normal_block && !info.atomic_inline &&
                !layout_block_is_out_of_flow_positioned(info.block)) {
                break;
            }
            continue;
        }
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(child);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                // CSS 2.1 §9.4.1: collapsed whitespace does not establish a
                // line box advance before the following block in normal flow.
                if (!layout_text_rect_has_painted_codepoint(text, rect)) continue;
                float rect_extent = parent->blk && parent->blk->vertical_geometry_published
                    ? rect->width : rect->height;
                prefix = max(prefix, rect_extent);
            }
        } else if (child->is_element() && child->view_type == RDT_VIEW_INLINE) {
            prefix = max(prefix, child->width);
        }
    }
    return prefix;
}

static ViewBlock* vertical_flow_first_in_flow_child(ViewBlock* parent) {
    if (!parent || !parent->is_element()) return nullptr;
    View* first = lam::view_require_element(parent)->first_placed_child();
    while (first && first->is_block() &&
           layout_block_is_out_of_flow_positioned(lam::view_require_block(first))) {
        first = first->next();
    }
    return first && first->is_block() ? lam::view_require_block(first) : nullptr;
}

static bool vertical_flow_margin_collapses_through_child(ViewBlock* child) {
    if (!child || !child->is_element() || block_context_establishes_bfc(child)) {
        return false;
    }
    ViewBlock* first_block = vertical_flow_first_in_flow_child(child);
    return first_block &&
        layout_parent_block_edge_is_unedged(first_block, true, true);
}

static float vertical_flow_effective_block_start_margin(ViewBlock* child,
                                                        WritingMode parent_mode) {
    float own_margin = layout_vertical_flow_block_start_margin(child, parent_mode);
    if (!vertical_flow_margin_collapses_through_child(child)) {
        return own_margin;
    }
    ViewBlock* first_block = vertical_flow_first_in_flow_child(child);
    // CSS Writing Modes resolves parent-child block-start adjacency in the
    float first_margin = layout_vertical_flow_block_start_margin(
        first_block, parent_mode);
    float collapsed = collapse_margins(own_margin, first_margin);
    return collapsed;
}

static bool vertical_parent_has_atomic_block_flow(ViewBlock* parent) {
    return parent && parent->blk && parent->block()->given_height >= 0.0f && parent->in_line &&
        parent->inl()->vertical_align == CSS_VALUE_BOTTOM;
}

bool layout_vertical_parent_has_block_flow_child(ViewBlock* parent) {
    if (!parent || !parent->is_element()) return false;
    for (View* child = lam::view_require_element(parent)->first_placed_child();
         child; child = child->next()) {
        LayoutVerticalFlowChild info = {};
        if (!layout_classify_vertical_flow_child(parent, child, &info)) continue;
        if (((info.normal_block && !info.atomic_inline) ||
             (info.atomic_inline && vertical_parent_has_atomic_block_flow(parent))) &&
            !layout_block_is_out_of_flow_positioned(info.block)) {
            return true;
        }
    }
    return false;
}

static float compute_vertical_child_block_overflow(View* view, float origin_x,
                                                   float ancestor_x) {
    if (!view) return 0.0f;
    float absolute_x = ancestor_x + view->x;
    float extent = absolute_x + view->width - origin_x;
    if (!view->is_element()) {
        return max(extent, 0.0f);
    }
    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        extent = max(extent, compute_vertical_child_block_overflow(
            child, origin_x, absolute_x));
    }
    return max(extent, 0.0f);
}

static float vertical_child_block_contribution(ViewBlock* child_block) {
    if (!child_block) return 0.0f;
    float contribution = child_block->width;
    if (is_multicol_container(child_block)) {
        // CSS Multicol overflow fragments do not enlarge the container's
        // outer block-size contribution to its parent flow.
        return contribution;
    }
    float overflow = 0.0f;
    if (layout_block_inline_axis_is_vertical(child_block) && child_block->blk &&
        child_block->block()->given_width >= 0.0f) {
        overflow = compute_vertical_child_block_overflow(
            static_cast<View*>(child_block), child_block->x, 0.0f);
        if (overflow > child_block->width) {
            // CSS Sizing 3 §5.2: a definite small block-size does not cap the
            BoxMetrics child_box = layout_box_metrics(child_block);
            overflow += child_box.border.left + child_box.padding.left +
                compute_block_lead_y(child_block);
        }
        contribution = max(contribution, overflow);
    }
    return contribution;
}

static bool block_has_vertical_flow_child(ViewBlock* parent) {
    if (!parent || !parent->is_element()) return false;
    for (View* child = lam::view_require_element(parent)->first_placed_child();
         child; child = child->next()) {
        if (child->is_block() &&
            layout_block_inline_axis_is_vertical(lam::view_require_block(child))) {
            return true;
        }
    }
    return false;
}

static float layout_vertical_inline_gap_before(ViewBlock* child) {
    if (!child) return 0.0f;
    DomNode* previous = child->prev_sibling;
    previous = layout_previous_view_with_type(previous);
    if (!previous || previous->view_type != RDT_VIEW_TEXT) return 0.0f;
    ViewText* text = lam::view_require_text(static_cast<View*>(previous));
    float gap = 0.0f;
    for (TextRect* rect = text->rect; rect; rect = rect->next) {
        if (!layout_text_rect_has_painted_codepoint(text, rect)) {
            float advance = rect->width;
            if (rect->height < advance) advance = rect->height;
            gap += max(advance, 0.0f);
        }
    }
    return gap;
}

static float layout_vertical_flow_extent(ViewBlock* parent, bool margin_box_mode) {
    if (!parent || !parent->is_element()) return 0.0f;
    BoxMetrics parent_box = layout_box_metrics(parent);
    float content_left = parent_box.border.left + parent_box.padding.left;
    float min_offset = 0.0f;
    float max_extent = 0.0f;
    float max_physical = 0.0f;
    float logical_block_cursor = 0.0f;
    bool has_in_flow_child = false;
    bool has_normal_block_child = false;
    bool has_atomic_inline_child = false;
    bool has_block_flow_child = layout_vertical_parent_has_block_flow_child(parent);
    bool children_are_axis_mapped = parent->blk &&
        parent->blk->vertical_geometry_published;
    bool has_line_clamp = parent->blk && parent->block()->line_clamp > 0;
    float previous_block_margin_end = 0.0f;
    bool has_previous_block_flow = false;
    float inline_prefix = has_block_flow_child
        ? vertical_inline_prefix_before_block(parent) : 0.0f;
    logical_block_cursor = inline_prefix;
    for (View* child = lam::view_require_element(parent)->first_placed_child();
         child; child = child->next()) {
        if (!child->is_block()) continue;
        LayoutVerticalFlowChild info = {};
        if (!layout_classify_vertical_flow_child(parent, child, &info)) continue;
        ViewBlock* child_block = info.block;
        if (layout_block_is_out_of_flow(child_block)) continue;
        if (!margin_box_mode && has_line_clamp &&
            child_block->display.outer != CSS_VALUE_BLOCK &&
            child_block->display.outer != CSS_VALUE_LIST_ITEM) continue;
        bool is_atomic_inline = info.atomic_inline ||
            child_block->view_type == RDT_VIEW_TABLE;
        if (margin_box_mode) {
            bool is_normal_block = child_block->display.outer == CSS_VALUE_BLOCK ||
                child_block->display.outer == CSS_VALUE_LIST_ITEM;
            if (!is_normal_block && child_block->view_type != RDT_VIEW_INLINE_BLOCK) continue;
            has_normal_block_child = true;
            if (child_block->view_type == RDT_VIEW_INLINE_BLOCK &&
                !vertical_parent_has_atomic_block_flow(parent)) {
                float child_extent = vertical_child_block_contribution(child_block) +
                    layout_non_auto_margin_right(child_block);
                max_extent = max(max_extent, child_extent);
                has_atomic_inline_child = true;
                continue;
            }
            float child_block_extent = vertical_child_block_contribution(child_block);
            float child_extent = child_block_extent;
            child_extent += !children_are_axis_mapped
                ? logical_block_cursor
                : child_block->x - parent->x - content_left;
            child_extent += layout_non_auto_margin_right(child_block);
            max_extent = max(max_extent, child_extent);
            if (!children_are_axis_mapped) {
                WritingMode parent_mode = layout_block_writing_mode(parent);
                logical_block_cursor +=
                    layout_vertical_flow_block_start_margin(child_block, parent_mode) +
                    child_block_extent +
                    layout_vertical_flow_block_end_margin(child_block, parent_mode);
            }
            continue;
        }
        if (!has_block_flow_child && is_atomic_inline) {
            float child_extent = child_block->width +
                layout_box_metrics(child_block).margin_h;
            if (child_block->tag() == MARKUP_NAME_TEXTAREA && child_block->form &&
                child_block->form->last_text_baseline_overflow > 0.0f &&
                child_block->block()->baseline_source == CSS_VALUE_LAST && parent->blk) {
                child_extent = max(child_extent,
                    child_block->width + parent->block()->last_line_max_descender);
            }
            max_extent = max(max_extent, child_extent);
            max_physical = max(max_physical, child_extent);
            has_in_flow_child = true;
            continue;
        }
        float logical_block_offset;
        if (children_are_axis_mapped) {
            logical_block_offset = child_block->x - content_left;
        } else if (has_block_flow_child) {
            WritingMode parent_mode = layout_block_writing_mode(parent);
            float margin_start = vertical_flow_effective_block_start_margin(
                child_block, parent_mode);
            float collapsed_margin = has_previous_block_flow
                ? collapse_margins(previous_block_margin_end, margin_start)
                : margin_start;
            logical_block_offset = logical_block_cursor + collapsed_margin;
        } else {
            logical_block_offset = child_block->y -
                parent_box.border.top - parent_box.padding.top;
        }
        float child_extent = vertical_child_block_contribution(child_block);
        min_offset = min(min_offset, logical_block_offset);
        max_extent = max(max_extent, logical_block_offset + child_extent);
        float physical_child_start = children_are_axis_mapped
            ? child_block->x : child_block->x - parent->x;
        max_physical = max(max_physical, physical_child_start + child_extent);
        has_in_flow_child = true;
        if (!children_are_axis_mapped && has_block_flow_child) {
            WritingMode parent_mode = layout_block_writing_mode(parent);
            float margin_start = vertical_flow_effective_block_start_margin(
                child_block, parent_mode);
            float collapsed_margin = has_previous_block_flow
                ? collapse_margins(previous_block_margin_end, margin_start)
                : margin_start;
            logical_block_cursor += collapsed_margin + child_extent;
            previous_block_margin_end = layout_vertical_flow_block_end_margin(
                child_block, parent_mode);
            has_previous_block_flow = true;
        }
    }
    if (margin_box_mode) {
        return (has_normal_block_child || has_atomic_inline_child) && max_extent > 0.0f
            ? max_extent + parent_box.pad_border_h : 0.0f;
    }
    if (!has_in_flow_child) return 0.0f;
    float physical_extent = max_physical - content_left;
    float extent = children_are_axis_mapped ? physical_extent : max_extent - min_offset;
    if (!children_are_axis_mapped && has_block_flow_child) {
        extent = max(extent, logical_block_cursor);
    }
    return max(extent + parent_box.pad_border_h, parent_box.pad_border_h);
}

static float layout_horizontal_flow_extent(ViewBlock* parent,
                                            bool margin_box_mode) {
    if (!parent || !parent->is_element()) return 0.0f;
    float extent = 0.0f;
    for (View* child = lam::view_require_element(parent)->first_placed_child();
         child; child = child->next()) {
        if (!child->view_type || (margin_box_mode && !child->is_block())) continue;
        if (layout_marker_is_outside(child)) continue;
        if (child->is_block() &&
            layout_block_is_out_of_flow(lam::view_require_block(child))) continue;
        if (margin_box_mode) {
            ViewBlock* child_block = lam::view_require_block(child);
            extent = max(extent, child_block->x + child_block->width +
                layout_non_auto_margin_right(child_block));
        } else {
            float child_extent = child->width;
            if (child->is_block()) {
                ViewBlock* child_block = lam::view_require_block(child);
                child_extent += layout_axis_margin_start(
                    child_block->bound, LAYOUT_AXIS_X) +
                    layout_axis_margin_end(child_block->bound, LAYOUT_AXIS_X);
            }
            extent = max(extent, child_extent);
        }
    }
    if (margin_box_mode && extent > 0.0f) {
        BoxMetrics parent_box = layout_box_metrics(parent);
        extent += parent_box.padding.right + parent_box.border.right;
    }
    return extent;
}

float layout_compute_in_flow_child_width_extent(ViewBlock* parent,
                                                bool include_margin_box) {
    if (!parent || !parent->is_element()) return 0.0f;
    if (layout_block_inline_axis_is_vertical(parent)) {
        return layout_vertical_flow_extent(parent, include_margin_box);
    }
    return layout_horizontal_flow_extent(parent, include_margin_box);
}

bool layout_compute_vertical_in_flow_child_inline_extent(ViewBlock* parent,
                                                         float* out_extent) {
    if (!parent || !parent->is_element() || !out_extent) return false;
    BoxMetrics parent_box = layout_box_metrics(parent);
    float content_left = parent_box.border.left + parent_box.padding.left;
    float extent = 0.0f;
    bool has_normal_block_child = false;
    bool has_atomic_inline_child = false;
    float atomic_inline_cursor = 0.0f;
    float direct_inline_extent = 0.0f;
    float previous_atomic_x_end = 0.0f;
    bool have_previous_atomic = false;
    int previous_atomic_line = -1;
    bool use_surrogate_inline_cursor = radiant::layout_uses_explicit_baseline_source(parent);
    float vertical_inline_gap_total = 0.0f;
    for (View* child = lam::view_require_element(parent)->first_placed_child();
         child; child = child->next()) {
        if (!child->is_block()) continue;
        LayoutVerticalFlowChild info = {};
        if (!layout_classify_vertical_flow_child(parent, child, &info)) continue;
        ViewBlock* child_block = info.block;
        if (layout_block_is_out_of_flow_positioned(child_block)) continue;
        if (child_block->view_type == RDT_VIEW_INLINE_BLOCK &&
            !(parent && parent->blk && parent->block()->given_height >= 0.0f && parent->in_line &&
              parent->inl()->vertical_align == CSS_VALUE_BOTTOM)) {
            vertical_inline_gap_total += layout_vertical_inline_gap_before(child_block);
            if (child_block->inline_line_number >= 0 &&
                previous_atomic_line >= 0 &&
                child_block->inline_line_number != previous_atomic_line) {
                // A forced break starts a new vertical line; it must not grow
                atomic_inline_cursor = 0.0f;
                previous_atomic_x_end = 0.0f;
                have_previous_atomic = false;
            }
            if (!have_previous_atomic) {
                atomic_inline_cursor = parent->block()->direction == CSS_VALUE_RTL
                    ? 0.0f : max((use_surrogate_inline_cursor ? child_block->x
                        - content_left : child_block->y - parent_box.border.top -
                        parent_box.padding.top), 0.0f);
                atomic_inline_cursor += vertical_inline_gap_total;
                direct_inline_extent = parent->block()->direction == CSS_VALUE_RTL
                    ? 0.0f : max((use_surrogate_inline_cursor ? child_block->x
                        - content_left : child_block->y - parent_box.border.top -
                        parent_box.padding.top), 0.0f);
            }
            float surrogate_gap = have_previous_atomic
                ? max(child_block->x - previous_atomic_x_end, 0.0f) : 0.0f;
            BoxEdges margin = layout_boundary_margin_edges(child_block->bound);
            float margin_top = margin.top;
            float margin_bottom = margin.bottom;
            atomic_inline_cursor += surrogate_gap + margin_top +
                child_block->height + margin_bottom;
            previous_atomic_x_end = child_block->x + child_block->height;
            have_previous_atomic = true;
            previous_atomic_line = child_block->inline_line_number;
            extent = max(extent, atomic_inline_cursor +
                (parent->block()->direction == CSS_VALUE_RTL
                    ? direct_inline_extent : 0.0f));
            has_atomic_inline_child = true;
            continue;
        }
        if (child_block->display.outer != CSS_VALUE_BLOCK &&
            child_block->display.outer != CSS_VALUE_LIST_ITEM) continue;
        float child_extent = child_block->height;
        BoxMetrics child_box = layout_box_metrics(child_block);
        child_extent += child_box.margin.top + child_box.margin.bottom;
        if (layout_block_inline_axis_is_vertical(child_block)) {
            float inline_offset = max(child_block->x - content_left, 0.0f);
            child_extent = max(child_extent,
                inline_offset + child_block->height + child_box.margin.bottom);
        }
        extent = max(extent, child_extent);
        has_normal_block_child = true;
    }
    *out_extent = extent;
    return has_normal_block_child || has_atomic_inline_child;
}

static bool layout_resolve_percentage_width_constraints(
        LayoutContext* lycon, ViewBlock* block, float containing_width) {
    if (!lycon || !block || !block->blk || !block->is_element() ||
        !block->specified_style) return false;
    BlockContext containing_context = lycon->block.parent
        ? *lycon->block.parent : BlockContext{};
    containing_context.content_width = containing_width;
    containing_context.given_width = containing_width;
    LayoutContext resolve_context = *lycon;
    resolve_context.block.parent = &containing_context;
    bool resolved_any = false;
    DomElement* element = block->as_element();
    for (int i = 0; i < 2; i++) {
        bool minimum = i == 0;
        CssPropertyCode property = minimum ? CSS_PROPERTY_MIN_WIDTH : CSS_PROPERTY_MAX_WIDTH;
        CssDeclaration* declaration = layout_specified_physical_minmax_size_declaration(
            element, true, minimum);
        if (!declaration || !declaration->value ||
            (declaration->value->type != CSS_VALUE_TYPE_PERCENTAGE &&
             declaration->value->type != CSS_VALUE_TYPE_FUNCTION)) {
            continue;
        }
        float resolved = resolve_length_value(&resolve_context, property, declaration->value);
        if (!isfinite(resolved) || resolved < 0.0f) continue;
        if (minimum) {
            block->block_mut()->given_min_width = resolved;
        } else {
            block->block_mut()->given_max_width = resolved;
        }
        resolved_any = true;
    }
    return resolved_any;
}

static bool layout_parent_has_cyclic_min_width_child(ViewBlock* parent) {
    if (!parent || !parent->is_element()) return false;
    for (View* child = lam::view_require_element(parent)->first_placed_child();
         child; child = child->next()) {
        if (!child->is_block()) continue;
        ViewBlock* child_block = lam::view_require_block(child);
        if (layout_block_is_out_of_flow_positioned(child_block) ||
            !child_block->is_element() || !child_block->specified_style) continue;
        CssDeclaration* declaration = layout_specified_physical_minmax_size_declaration(
            child_block->as_element(), true, true);
        if (declaration && declaration->value &&
            layout_css_value_has_nonzero_percentage(declaration->value)) {
            return true;
        }
    }
    return false;
}

static void adjust_block_children_after_shrink(LayoutContext* lycon,
                                               ViewBlock* parent, float new_parent_cw,
                                               CssEnum inherited_text_align) {
    // CSS Flexbox §9: Flex item widths are determined by the flex algorithm,
    if (parent->display.inner == CSS_VALUE_FLEX) return;
    float child_containing_width = new_parent_cw;
    if (parent->multicol_prop() && is_multicol_container(parent) &&
        parent->multicol_prop()->computed_column_count > 1 &&
        parent->multicol_prop()->computed_column_width > 0.0f) {
        child_containing_width = parent->multicol_prop()->computed_column_width;
    }
    for (View* child = lam::view_require_element(parent)->first_placed_child(); child; child = child->next()) {
        if (child->view_type != RDT_VIEW_BLOCK && child->view_type != RDT_VIEW_LIST_ITEM)
            continue;
        ViewBlock* cb = lam::view_require_block(child);
        if (cb->blk) {
            bool has_definite_width = cb->block()->given_width >= 0.0f;
            bool has_percentage_width = !isnan(cb->block()->given_width_percent);
            bool has_intrinsic_keyword_width =
                cb->block()->given_width_type != CSS_VALUE_AUTO &&
                cb->block()->given_width_type != CSS_VALUE__UNDEF;
            if (has_definite_width || has_percentage_width || has_intrinsic_keyword_width)
                continue;
        }
        if (element_has_float(cb))
            continue;
        if (layout_block_is_out_of_flow_positioned(cb))
            continue;
        float ml = 0, mr = 0;
        if (cb->bound) {
            ml = (cb->boundary()->margin.left_type == CSS_VALUE_AUTO) ? 0 : cb->boundary()->margin.left;
            mr = (cb->boundary()->margin.right_type == CSS_VALUE_AUTO) ? 0 : cb->boundary()->margin.right;
        }
        BoxMetrics cb_box = layout_box_metrics(cb);
        float pb = cb_box.pad_border_h;
        float new_avail_cw = 0.0f;
        float new_width = max(child_containing_width - ml - mr, 0.0f);
        bool same_vertical_flow = layout_block_inline_axis_is_vertical(parent) &&
            layout_block_inline_axis_is_vertical(cb);
        if (same_vertical_flow) {
            new_width = max(layout_compute_in_flow_child_width_extent(cb), pb);
        }
        bool resolved_percentage_constraints =
            layout_resolve_percentage_width_constraints(
                lycon, cb, max(child_containing_width, 0.0f));
        if (resolved_percentage_constraints) {
            new_width = layout_apply_min_max_axis(cb, new_width, true, true);
        }
        float old_width = cb->width;
        if (fabsf(new_width - old_width) < 0.5f)
            continue;
        cb->width = new_width;
        new_avail_cw = new_width - pb;
        cb->content_width = max(new_avail_cw, 0.0f);
        // text-align: use child's own value if it has blk, otherwise inherit from parent
        CssEnum ta = cb->blk ? cb->block()->text_align : inherited_text_align;
        if (ta == CSS_VALUE_CENTER || ta == CSS_VALUE_RIGHT) {
            layout_align_deferred_inline_line_runs(lam::view_require_element(cb), new_avail_cw, ta);
        }
        adjust_block_children_after_shrink(lycon, cb, max(new_avail_cw, 0.0f), ta);
    }
}

static void set_block_scroller_clip(ViewBlock* block) {
    block->scroller->has_clip = true;
    block->scroll_mut()->clip.left = 0.0f;
    block->scroll_mut()->clip.top = 0.0f;
    block->scroll_mut()->clip.right = block->width;
    block->scroll_mut()->clip.bottom = block->height;
}

static float block_context_float_bottom(const BlockContext* context,
                                         bool include_lowest) {
    if (!context) return 0.0f;
    float max_bottom = include_lowest ? context->lowest_float_bottom : 0.0f;
    for (FloatBox* float_box = context->left_floats; float_box; float_box = float_box->next) {
        max_bottom = max(max_bottom, float_box->margin_box_bottom);
    }
    for (FloatBox* float_box = context->right_floats; float_box; float_box = float_box->next) {
        max_bottom = max(max_bottom, float_box->margin_box_bottom);
    }
    return max_bottom;
}

void layout_publish_vertical_children(ViewBlock* block, WritingMode mode,
                                      bool swap_dimensions, float line_height,
                                      float line_block_start,
                                      bool publish_atomic_lines) {
    if (!block || !block->is_element() || !block->blk) return;
    if (block->blk->vertical_geometry_published) return;
    block->blk->vertical_geometry_published = true;
    BoxMetrics box = layout_box_metrics(block);
    float content_left = box.border.left + box.padding.left;
    float content_top = box.border.top + box.padding.top;
    float content_width = layout_content_size_from_border_box(block, block->width, true);
    ViewElement* element = lam::view_require_element(block);
    bool atomic_block_flow = vertical_parent_has_atomic_block_flow(block);
    bool has_block_flow_child = layout_vertical_parent_has_block_flow_child(block);
    // Vertical table cells defer geometry publication until track sizing, so
    // their surrogate y offsets still identify forced-break columns here.
    bool has_explicit_baseline_child =
        radiant::layout_inline_context_has_explicit_baseline_source(block) ||
        block->view_type == RDT_VIEW_TABLE_CELL;
    float logical_block_cursor = 0.0f;
    float logical_inline_cursor = 0.0f;
    bool has_atomic_line_break = false;
    if (publish_atomic_lines && !atomic_block_flow) {
        int first_atomic_line = -1;
        for (View* child = element->first_placed_child(); child; child = child->next()) {
            LayoutVerticalFlowChild info = {};
            if (!layout_classify_vertical_flow_child(block, child, &info) ||
                !info.atomic_inline || layout_block_is_out_of_flow_positioned(info.block)) {
                continue;
            }
            if (info.block->inline_line_number < 0) continue;
            if (first_atomic_line < 0) {
                first_atomic_line = info.block->inline_line_number;
            } else if (info.block->inline_line_number != first_atomic_line) {
                has_atomic_line_break = true;
                break;
            }
        }
    }
    float atomic_line_block_offset = publish_atomic_lines && has_atomic_line_break
        ? max(line_block_start, 0.0f) : 0.0f;
    float atomic_line_cross_extent = 0.0f;
    float previous_block_margin_end = 0.0f;
    bool has_previous_block_flow = false;
    float previous_atomic_x_end = 0.0f;
    bool have_previous_atomic = false;
    int previous_atomic_line = -1;
    CssEnum text_orientation = layout_specified_keyword(
        block->as_element(), CSS_PROPERTY_TEXT_ORIENTATION, CSS_VALUE_MIXED);
    bool sideways_lr_ltr_inline_flow = layout_element_css_writing_mode(block->as_element()) ==
        CSS_VALUE_SIDEWAYS_LR &&
        block->blk && block->block()->given_height >= 0.0f &&
        (block->block()->direction == CSS_VALUE_LTR ||
         text_orientation == CSS_VALUE_UPRIGHT);
    bool vertical_multicol = layout_block_inline_axis_is_vertical(block) &&
        is_multicol_container(block);
    bool sideways_lr_multicol_columns = vertical_multicol &&
        layout_element_css_writing_mode(block->as_element()) == CSS_VALUE_SIDEWAYS_LR &&
        block->block()->direction == CSS_VALUE_LTR &&
        block->multicol_prop()->computed_column_width > 0.0f &&
        block->multicol_prop()->computed_column_count > 1;
    float previous_multicol_inline_offset = 0.0f;
    bool have_previous_multicol_inline_offset = false;
    ViewBlock* previous_vertical_multicol_child = nullptr;
    float vertical_inline_gap_total = 0.0f;
    float inline_prefix = has_block_flow_child
        ? vertical_inline_prefix_before_block(block) : 0.0f;
    logical_block_cursor = inline_prefix;

    for (View* child = element->first_placed_child(); child; child = child->next()) {
        LayoutVerticalFlowChild info = {};
        if (!layout_classify_vertical_flow_child(block, child, &info)) {
            continue;
        }
        ViewBlock* child_block = info.block;
        bool is_atomic_inline = info.atomic_inline;
        bool is_orthogonal_block = info.orthogonal;
        bool same_vertical_flow = info.same_flow;
        if (layout_block_is_out_of_flow_positioned(child_block) ||
            (!layout_block_inline_axis_is_vertical(child_block) &&
            !is_atomic_inline && !is_orthogonal_block)) continue;
        float vertical_inline_gap = is_atomic_inline && !atomic_block_flow
            ? layout_vertical_inline_gap_before(child_block) : 0.0f;
        vertical_inline_gap_total += vertical_inline_gap;
        bool starts_new_atomic_line = is_atomic_inline && !atomic_block_flow &&
            child_block->inline_line_number >= 0 &&
            previous_atomic_line >= 0 &&
            child_block->inline_line_number != previous_atomic_line;
        if (starts_new_atomic_line) {
            if (publish_atomic_lines) {
                // CSS Writing Modes maps each inline line to a new block-axis
                // position; the surrogate horizontal pass leaves that mapping
                // implicit, so publish the line box advance here.
                atomic_line_block_offset += max(
                    atomic_line_cross_extent, line_height);
                atomic_line_cross_extent = 0.0f;
            }
            logical_inline_cursor = 0.0f;
            previous_atomic_x_end = 0.0f;
            have_previous_atomic = false;
            if (publish_atomic_lines) {
                // trailing collapsible whitespace belongs to the previous line,
                // not to the inline-start of the newly created line.
                vertical_inline_gap_total = 0.0f;
            }
        }
        float surrogate_gap = is_atomic_inline && !atomic_block_flow &&
            have_previous_atomic
            ? max(child_block->x - previous_atomic_x_end, 0.0f) : 0.0f;
        BoxEdges margin = layout_boundary_margin_edges(child_block->bound);
        float margin_top = margin.top;
        float margin_bottom = margin.bottom;
        float logical_inline_offset;
        if (is_atomic_inline && !atomic_block_flow) {
            // surrogate gap must be applied before placing its inline-start.
            if (!have_previous_atomic) {
                logical_inline_cursor = block->block()->direction == CSS_VALUE_RTL
                    ? 0.0f : (publish_atomic_lines && starts_new_atomic_line
                        ? 0.0f : max(child_block->y - content_top, 0.0f));
                logical_inline_cursor += vertical_inline_gap_total;
                if (block->block()->direction != CSS_VALUE_RTL &&
                    has_explicit_baseline_child) {
                    logical_inline_cursor = max(
                        child_block->x - content_left, 0.0f);
                }
            }
            logical_inline_offset = logical_inline_cursor + surrogate_gap + margin_top;
        } else if (same_vertical_flow && has_block_flow_child && !vertical_multicol) {
            logical_inline_offset = margin_top;
        } else {
            if (is_atomic_inline && atomic_block_flow &&
                child_block->inline_line_number > 0) {
                logical_inline_offset = child_block->x - content_left;
            } else {
                logical_inline_offset = is_atomic_inline
                    ? child_block->y - content_top
                    : (same_vertical_flow ? child_block->y - content_top
                                           : child_block->x - content_left);
            }
        }
        bool child_margin_collapsed_through =
            vertical_flow_margin_collapses_through_child(child_block);
        float margin_start = child_margin_collapsed_through
            ? vertical_flow_effective_block_start_margin(child_block, mode)
            : layout_vertical_flow_block_start_margin(child_block, mode);
        float margin_end = layout_vertical_flow_block_end_margin(child_block, mode);
        if (has_block_flow_child && logical_block_cursor == inline_prefix &&
            inline_prefix == 0.0f &&
            !child_margin_collapsed_through &&
            !block_context_establishes_bfc(block) &&
            layout_parent_block_edge_is_unedged(child_block, true, true) &&
            !(child_block->bound &&
              child_block->boundary()->margin.right_type == CSS_VALUE__PERCENTAGE)) {
            // to an intrinsic auto block-size and cannot collapse out here.
            margin_start = 0.0f;
        }
        float collapsed_margin_start = has_previous_block_flow
            ? collapse_margins(previous_block_margin_end, margin_start)
            : margin_start;
        float logical_block_offset = has_block_flow_child
            ? logical_block_cursor + collapsed_margin_start
            : (is_atomic_inline && !atomic_block_flow && publish_atomic_lines
                ? atomic_line_block_offset
                : (is_atomic_inline && !has_explicit_baseline_child
                    ? 0.0f : child_block->y - content_top));
        float surrogate_x = child_block->x;
        if (vertical_multicol) {
            logical_inline_offset = child_block->y - content_top;
            bool follows_fragmented_column = false;
            if (previous_vertical_multicol_child &&
                previous_vertical_multicol_child->is_element() &&
                previous_vertical_multicol_child->as_element()->layout_fragments_count() > 1) {
                LayoutFragmentBox* last_fragment =
                    previous_vertical_multicol_child->as_element()->layout_fragment_list();
                while (last_fragment && last_fragment->next) last_fragment = last_fragment->next;
                if (last_fragment &&
                    fabsf(logical_inline_offset - last_fragment->y) <= 0.5f) {
                    follows_fragmented_column = true;
                }
            }
            bool child_is_fragmented = child_block->is_element() &&
                child_block->as_element()->layout_fragments_count() > 1;
            if (child_is_fragmented) {
                // css fragmentation: the element's union origin was already
                // normalized by its fragments; do not add prior block flow.
                logical_block_offset = max(surrogate_x - content_left, 0.0f);
            } else if (follows_fragmented_column) {
                logical_block_offset = max(surrogate_x - content_left, 0.0f);
            } else if (!have_previous_multicol_inline_offset ||
                       fabsf(logical_inline_offset - previous_multicol_inline_offset) > 0.5f) {
                logical_block_cursor = 0.0f;
                logical_block_offset = margin_start;
            }
        }
        if (!has_block_flow_child) {
            logical_block_offset += layout_vertical_flow_block_start_margin(
                child_block, mode);
        }
        float child_width = child_block->width;
        float child_height = child_block->height;
        float surrogate_child_y = child_block->y;
        float child_block_contribution = vertical_child_block_contribution(child_block);
        float sideways_lr_inline_y = content_top +
            layout_content_size_from_border_box(block, block->height, false) -
            child_height - margin_bottom;
        if (is_orthogonal_block) {
            // only its parent block-axis static position must be mapped here.
            child_block->x = mode == WM_VERTICAL_RL
                ? content_left + content_width - logical_block_offset - child_width
                : content_left + logical_block_offset;
            child_block->y = content_top;
            if (sideways_lr_ltr_inline_flow) {
                // CSS Writing Modes maps sideways-lr LTR inline-start to the
                child_block->y = sideways_lr_inline_y;
            }
            continue;
        }
        // CSS Writing Modes maps the logical inline axis to physical y and
        if (swap_dimensions) {
            child_block->width = child_height;
            child_block->height = child_width;
        }
        child_block->y = sideways_lr_ltr_inline_flow
            ? sideways_lr_inline_y : content_top + logical_inline_offset;
        child_block->x = mode == WM_VERTICAL_RL
            ? content_left + content_width - logical_block_offset - child_block->width
            : content_left + logical_block_offset;
        if (sideways_lr_multicol_columns &&
            !(child_block->is_element() &&
              child_block->as_element()->layout_fragments_count() > 1)) {
            float column_pitch = block->multicol_prop()->computed_column_width +
                multicol_column_gap(block);
            int column_index = column_pitch > 0.0f
                ? (int)floorf((surrogate_child_y - content_top) / column_pitch + 0.5f)
                : 0; // INT_CAST_OK: column index from a positive physical inline pitch
            int column_count = block->multicol_prop()->computed_column_count;
            if (column_index < 0) column_index = 0;
            if (column_index >= column_count) column_index = column_count - 1;
            float column_start = content_top +
                (column_count - 1 - column_index) * column_pitch;
            child_block->y = column_start +
                block->multicol_prop()->computed_column_width - child_block->height;
        }
        if (vertical_multicol && !sideways_lr_ltr_inline_flow &&
            child_block->is_element() &&
            child_block->as_element()->layout_fragments_count() > 1) {
            LayoutFragmentBox* first_fragment = child_block->as_element()->layout_fragment_list();
            if (first_fragment) {
                child_block->y = content_top + first_fragment->y;
            }
        }
        if (has_block_flow_child) {
            logical_block_cursor += collapsed_margin_start + child_block_contribution;
            previous_block_margin_end = margin_end;
            has_previous_block_flow = true;
        }
        if (vertical_multicol) {
            previous_multicol_inline_offset = logical_inline_offset;
            if (child_block->is_element() &&
                child_block->as_element()->layout_fragments_count() > 1) {
                LayoutFragmentBox* last_fragment = child_block->as_element()->layout_fragment_list();
                while (last_fragment && last_fragment->next) last_fragment = last_fragment->next;
                if (last_fragment) previous_multicol_inline_offset = last_fragment->y;
            }
            have_previous_multicol_inline_offset = true;
            previous_vertical_multicol_child = child_block;
        }
        if (is_atomic_inline && !atomic_block_flow) {
            logical_inline_cursor += surrogate_gap + margin_top +
                child_height + margin_bottom;
            if (publish_atomic_lines) {
                atomic_line_cross_extent = max(atomic_line_cross_extent,
                    child_width + margin.left + margin.right);
            }
            previous_atomic_x_end = surrogate_x + child_height;
            have_previous_atomic = true;
            previous_atomic_line = child_block->inline_line_number;
        }
    }
}

static bool vertical_break_anchor_x(View* view, float* anchor_x) {
    if (!view || !anchor_x) return false;
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        TextRect* last_rect = nullptr;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->width > 0.0f && rect->height > 0.0f) last_rect = rect;
        }
        if (!last_rect) return false;
        *anchor_x = last_rect->x;
        return true;
    }
    if (view->view_type == RDT_VIEW_INLINE_BLOCK) {
        *anchor_x = view->x;
        return true;
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
        for (View* child = span->last_placed_child(); child; ) {
            if (vertical_break_anchor_x(child, anchor_x)) return true;
            DomNode* previous = child->prev_sibling;
            previous = layout_previous_view_with_type(previous);
            child = previous ? static_cast<View*>(previous) : nullptr;
        }
    }
    return false;
}

void layout_normalize_vertical_breaks(ViewBlock* block) {
    if (!block || !layout_block_inline_axis_is_vertical(block) ||
        !block->is_element() || block->display.inner == CSS_VALUE_FLEX) return;
    ViewElement* element = lam::view_require_element(block);
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        ViewElement* child_view = lam::view_require_element(static_cast<View*>(child));
        if (child->tag() == MARKUP_NAME_BR && child_view->view_type) {
            DomNode* previous = child->prev_sibling;
            previous = layout_previous_view_with_type(previous);
            View* previous_view = previous ? static_cast<View*>(previous) : nullptr;
            float anchor_x = 0.0f;
            if (previous_view && vertical_break_anchor_x(previous_view, &anchor_x)) {
                child_view->x = anchor_x;
            }
        }
        if (child_view->is_block()) {
            layout_normalize_vertical_breaks(
                lam::view_require_block(child_view));
        }
    }
}

static bool block_has_in_flow_orthogonal_child(ViewBlock* block) {
    if (!block || !block->is_element()) return false;
    bool parent_vertical = layout_block_inline_axis_is_vertical(block);
    for (View* child = lam::view_require_element(block)->first_placed_child();
         child; child = child->next()) {
        if (!child->is_block()) continue;
        ViewBlock* child_block = lam::view_require_block(child);
        if (!layout_block_is_out_of_flow_positioned(child_block) &&
            layout_block_inline_axis_is_vertical(child_block) != parent_vertical) {
            return true;
        }
    }
    return false;
}

static void layout_update_axis_overflow(LayoutContext* lycon, ViewBlock* block,
                                        LayoutAxis axis, float flow_extent,
                                        CssEnum display) {
    if (!lycon || !block || flow_extent <= layout_axis_size(block, axis)) return;

    bool horizontal = layout_axis_is_horizontal(axis);
    block->ensure_scroll(lycon);
    ScrollProp* scroll = block->scroll_mut();
    bool* has_overflow = horizontal
        ? &scroll->has_hz_overflow : &scroll->has_vt_overflow;
    bool* has_scroll = horizontal
        ? &scroll->has_hz_scroll : &scroll->has_vt_scroll;
    CssEnum overflow = horizontal ? scroll->overflow_x : scroll->overflow_y;
    *has_overflow = true;
    if (overflow == CSS_VALUE_VISIBLE) {
        if (lycon->block.parent) {
            float parent_extent = horizontal ? flow_extent : block->y + flow_extent;
            bool suppress_parent_propagation = horizontal &&
                (display == CSS_VALUE_INLINE_BLOCK ||
                 (block->blk && block->block()->given_max_width >= 0) ||
                 layout_block_is_out_of_flow_positioned(block));
            if (!suppress_parent_propagation) {
                float* parent_max = horizontal
                    ? &lycon->block.parent->max_width : &lycon->block.parent->max_height;
                *parent_max = max(*parent_max, parent_extent);
            }
        }
    } else if (overflow == CSS_VALUE_SCROLL || overflow == CSS_VALUE_AUTO) {
        *has_scroll = true;
    }
    if (*has_scroll || overflow == CSS_VALUE_CLIP || overflow == CSS_VALUE_HIDDEN) {
        set_block_scroller_clip(block);
    }
}

static bool layout_list_item_has_visible_marker(ViewBlock* block) {
    if (!block) return false;
    if (block->pseudo_style(PSEUDO_STYLE_MARKER)) {
        CssDeclaration* content_decl = style_tree_get_declaration(
            block->pseudo_style(PSEUDO_STYLE_MARKER), CSS_PROPERTY_CONTENT);
        if (content_decl && content_decl->value &&
            content_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
            content_decl->value->data.keyword == CSS_VALUE_NONE) {
            return false;
        }
    }
    // A generated marker is the used box-tree state after counter/style
    // resolution; retain it even when the HTML table UA pass left the inherited
    // list-style field at its default sentinel.
    if (block->pseudo && block->pseudo->marker) return true;
    if (block->blk && block->block_mut()->list_style_type == CSS_VALUE_NONE) {
        return false;
    }
    return true;
}

static float layout_list_item_marker_line_height(LayoutContext* lycon) {
    if (!lycon) return 18.0f;
    float line_height = lycon->block.line_height;
    if (line_height <= 0.0f) {
        line_height = lycon->font.current_font_size > 0.0f
            ? lycon->font.current_font_size * 1.2f : 18.0f;
    }
    return line_height;
}

static bool layout_empty_editing_host(ViewBlock* block) {
    if (!block || !block->is_element() || block->first_placed_child()) return false;
    EditingHost host = {};
    DomElement* element = block->as_element();
    return editing_host_lookup(element, &host) && host.host == element;
}

void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display) {
    float flow_width, flow_height;
    bool preserved_empty_vertical_multicol_line = false;
    BoxMetrics block_box = layout_box_metrics(block);
    block->content_width = lycon->block.max_width + block_box.padding.right;
    block->content_height = lycon->block.advance_y + block_box.padding.bottom;
    flow_width = block->content_width + block_box.border.right;
    flow_height = block->content_height + block_box.border.bottom;
    float scroll_flow_width = flow_width;
    float scroll_flow_height = flow_height;
    float scroll_min_x = 0.0f;
    float scroll_min_y = 0.0f;
    float content_min_x = 0.0f;
    float content_max_x = 0.0f;
    float content_min_y = 0.0f;
    float content_max_y = 0.0f;
    if (layout_empty_editing_host(block) && lycon->block.line_height > 0.0f) {
        // html editing hosts expose the caret's anonymous line box when empty.
        lycon->block.advance_y += lycon->block.line_height;
        block->content_height = lycon->block.advance_y + block_box.padding.bottom;
        flow_height = block->content_height + block_box.border.bottom;
    }
    bool is_root_element = block->tag_id == MARKUP_NAME_HTML;
    if (is_root_element) {
        // root's used inline size is its own border box; descendant overflow is
        // measured separately so body margins contribute to the root edge once.
        float root_content_width = layout_content_size_from_border_box(block, block->width, true);
        block->content_width = root_content_width + block_box.padding.right;
        lycon->block.max_width = root_content_width;
        flow_width = block->content_width + block_box.border.right;
        scroll_flow_width = flow_width;
        scroll_flow_height = flow_height;
    }
    if ((block->scroller || is_root_element) && block->is_element()) {
        ViewElement* element = lam::view_require_element(block);
        bool can_scroll_x = is_root_element || block->scroll()->overflow_x != CSS_VALUE_CLIP;
        bool can_scroll_y = is_root_element || block->scroll()->overflow_y != CSS_VALUE_CLIP;
        // scroll ranges include visible descendants without changing an auto-height ancestor.
        if (can_scroll_x) {
            layout_in_flow_content_bounds(
                element, LAYOUT_AXIS_X, true, &content_min_x, &content_max_x);
            scroll_flow_width = max(scroll_flow_width,
                content_max_x + block_box.border.right);
            scroll_min_x = min(content_min_x, 0.0f);
        }
        if (can_scroll_y) {
            layout_in_flow_content_bounds(
                element, LAYOUT_AXIS_Y, true, &content_min_y, &content_max_y);
            scroll_flow_height = max(scroll_flow_height,
                content_max_y + block_box.border.bottom);
            scroll_min_y = min(content_min_y, 0.0f);
        }
    }
    if (is_multicol_container(block) &&
        layout_block_inline_axis_is_vertical(block) &&
        flow_height <= 0.0f) {
        float normal_font_size = lycon->font.style &&
                lycon->font.style->font_size > 0.0f
            ? lycon->font.style->font_size
            : (lycon->ui_context ? lycon->ui_context->default_font.font_size : 0.0f);
        float empty_line_extent = lycon->block.line_height_is_normal &&
                normal_font_size > 0.0f
            ? normal_font_size
            : lycon->block.line_height;
        if (empty_line_extent > 0.0f) {
            lycon->block.advance_y = empty_line_extent;
            block->content_height = empty_line_extent + block_box.padding.bottom;
            flow_height = block->content_height + block_box.border.bottom;
            preserved_empty_vertical_multicol_line = true;
        }
    }
    // CSS 2.1 §12.5: List-items with visible markers generate at least one line box.
    // Float blockification changes view_type to block, so the display role is the
    // invariant that preserves the empty marker line for blockified list items.
    if (block->display.list_item) {
        float content_area_height = lycon->block.advance_y -
            layout_axis_decoration_start(block->bound, LAYOUT_AXIS_Y);
        if (content_area_height <= 0) {
            bool has_marker = layout_list_item_has_visible_marker(block);
            // HTML fieldsets use a special principal box whose empty used height
            // is its resolved border/padding box; the generic list-item line floor
            // would add an anonymous 18px content line that browsers do not create.
            bool fieldset_has_empty_principal_box = block->tag_id == MARKUP_NAME_FIELDSET;
            bool button_has_anonymous_content_box = block->tag_id == MARKUP_NAME_BUTTON &&
                block->display.inner == CSS_VALUE_FLOW_ROOT;
            if (has_marker && !fieldset_has_empty_principal_box &&
                !button_has_anonymous_content_box) {
                float min_line_height = layout_list_item_marker_line_height(lycon);
                flow_height += min_line_height;
                block->content_height += min_line_height;
            }
        }
    }
    // CSS Inline Level 3 §5: Apply text-box-trim to the intrinsic flow height
    if (block->blk) {
        if (lycon->block.line_clamped && lycon->block.line_clamp_advance_y >= 0.0f) {
            float clamp_content_height = lycon->block.line_clamp_advance_y +
                block_box.padding.bottom;
            flow_height = clamp_content_height + block_box.border.bottom;
            block->content_height = clamp_content_height;
            lycon->block.last_line_ascender = lycon->block.line_clamp_last_line_ascender;
            lycon->block.last_line_max_ascender = lycon->block.line_clamp_last_line_max_ascender;
            lycon->block.last_line_max_descender = lycon->block.line_clamp_last_line_max_descender;
        }
        block->blk->first_line_max_ascender = lycon->block.first_line_max_ascender;
        block->blk->first_line_max_descender = lycon->block.first_line_max_descender;
        block->blk->last_line_max_ascender = lycon->block.last_line_max_ascender;
        block->blk->last_line_max_descender = lycon->block.last_line_max_descender;
        block->blk->line_clamped = lycon->block.line_clamped;
        block->blk->line_clamp_advance_y = lycon->block.line_clamp_advance_y;
        block->blk->line_clamp_last_line_ascender = lycon->block.line_clamp_last_line_ascender;
        block->blk->line_clamp_last_line_max_ascender = lycon->block.line_clamp_last_line_max_ascender;
        block->blk->line_clamp_last_line_max_descender = lycon->block.line_clamp_last_line_max_descender;
        // is restored; an empty table line must not erase those cached sets.
        if (block->view_type == RDT_VIEW_TABLE) {
            if (lycon->block.first_line_ascender > 0.0f) {
                block->blk->first_line_baseline = lycon->block.first_line_ascender;
            }
            if (lycon->block.last_line_ascender > 0.0f) {
                block->blk->last_line_baseline = lycon->block.last_line_ascender;
            }
        } else {
            block->blk->first_line_baseline = lycon->block.first_line_ascender;
            block->blk->last_line_baseline = lycon->block.last_line_ascender;
        }
    }
    float end_trim_limit = -1.0f;
    if (lycon->block.saved_clear_y >= 0.0f) {
        end_trim_limit = max(lycon->block.advance_y - lycon->block.saved_clear_y, 0.0f);
    }
    float text_box_trim_amount = apply_text_box_trim(block, end_trim_limit);
    if (text_box_trim_amount > 0) {
        flow_height -= text_box_trim_amount;
        block->content_height -= text_box_trim_amount;
        lycon->block.advance_y -= text_box_trim_amount;
        recompute_inline_descendant_bounds(static_cast<View*>(block), font_box_handle(&lycon->font));
    }
    bool uses_axis_aware_layout = block->display.inner == CSS_VALUE_FLEX ||
        block->display.inner == CSS_VALUE_GRID ||
        block->display.inner == CSS_VALUE_TABLE;
    bool defer_vertical_geometry = layout_block_is_out_of_flow_positioned(block);
    if (layout_block_inline_axis_is_vertical(block) && !uses_axis_aware_layout) {
        // CSS Writing Modes maps the logical inline axis to physical y and the
        float logical_block_flow = flow_height;
        float logical_inline_flow = lycon->block.max_width;
        float normal_block_inline_extent = 0.0f;
        bool vertical_multicol = is_multicol_container(block);
        bool has_normal_block_child = false;
        if (vertical_multicol) {
            if (preserved_empty_vertical_multicol_line) {
                logical_block_flow = block_box.pad_border_h;
            }
            logical_inline_flow = flow_height + block_box.pad_border_v;
        } else if (layout_compute_vertical_in_flow_child_inline_extent(
                       block, &normal_block_inline_extent)) {
            has_normal_block_child = true;
            bool sideways_rl_rtl =
                layout_element_css_writing_mode(block->as_element()) ==
                    CSS_VALUE_SIDEWAYS_RL &&
                block->block()->direction == CSS_VALUE_RTL;
            bool has_direct_text = false;
            for (View* placed = lam::view_require_element(block)->first_placed_child();
                 placed; placed = placed->next()) {
                if (placed->is_text() && placed->width > 0.0f) {
                    has_direct_text = true;
                    break;
                }
            }
            if (sideways_rl_rtl &&
                !layout_vertical_parent_has_block_flow_child(block) &&
                has_direct_text) {
                normal_block_inline_extent += lycon->block.max_width;
            }
            float child_inline_flow = normal_block_inline_extent +
                block_box.pad_border_v;
            logical_inline_flow = sideways_rl_rtl
                ? max(logical_inline_flow, child_inline_flow)
                : child_inline_flow;
        }
        if (lycon->block.given_height >= 0.0f &&
            !block_axis_has_automatic_css_size(block, false)) {
            // flow measurement; a ratio-derived provisional size must not
            logical_inline_flow = block->height;
        }
        if (block->blk && block->block()->given_width >= 0.0f) {
            // A definite physical block-size must not be replaced by the
            logical_block_flow = block->width;
        }
        if (!vertical_multicol && !has_normal_block_child) {
            logical_inline_flow += layout_axis_decoration_end(block->bound, LAYOUT_AXIS_X);
        }
        if (lycon->block.initial_letter_margin_box_bottom >
            lycon->block.initial_letter_margin_box_top) {
            // CSS Inline 3 §7.9.1: an initial letter's margin box contributes
            float initial_margin_box_extent =
                lycon->block.initial_letter_margin_box_bottom -
                lycon->block.initial_letter_margin_box_top;
            logical_block_flow = max(logical_block_flow,
                max(initial_margin_box_extent - block_box.border_h, 0.0f));
        }
        block->content_width = max(logical_block_flow - block_box.border.left - block_box.border.right, 0.0f);
        block->content_height = max(logical_inline_flow - block_box.border.top - block_box.border.bottom, 0.0f);
        flow_width = logical_block_flow;
        flow_height = logical_inline_flow;
        if (!block->blk || block->block()->given_width < 0.0f) {
            if (vertical_multicol) {
                float column_block_extent = multicol_used_block_axis_extent(block);
                if (column_block_extent > 0.0f) {
                    logical_block_flow = column_block_extent + block_box.pad_border_h;
                    flow_width = logical_block_flow;
                    block->content_width = max(logical_block_flow -
                        block_box.border.left - block_box.border.right, 0.0f);
                }
            } else {
                float auto_block_extent = layout_compute_in_flow_child_width_extent(block);
                if (auto_block_extent > 0.0f) {
                    flow_width = logical_block_flow = auto_block_extent;
                    block->content_width = max(logical_block_flow -
                        block_box.border.left - block_box.border.right, 0.0f);
                }
            }
            block->width = flow_width;
        }
        if (!defer_vertical_geometry) {
            layout_publish_vertical_flow_geometry(lycon, block, flow_height);
        }
    }
    if (lycon->block.initial_letter_trimmed_start_candidate > 0.0f) {
        // CSS Inline 3 §7.9.1 keeps a raised initial's margin-box
        lycon->block.initial_letter_trimmed_start_contribution +=
            lycon->block.initial_letter_trimmed_start_candidate;
    }
    if (lycon->block.initial_letter_trimmed_start_contribution > 0.0f) {
        block->ensure_block(lycon)->initial_letter_trimmed_start_contribution =
            lycon->block.initial_letter_trimmed_start_contribution;
    }
    bool is_button_auto_width = block->tag_id == MARKUP_NAME_BUTTON &&
        block->display.inner == CSS_VALUE_FLOW_ROOT &&
        block->display.outer != CSS_VALUE_INLINE_BLOCK;
    bool is_inline_block_layout = display == CSS_VALUE_INLINE_BLOCK ||
        block->view_type == RDT_VIEW_INLINE_BLOCK || is_button_auto_width;
    if (is_inline_block_layout && lycon->block.given_width < 0) {
        // CSS Display 3: an inline flow-root is represented by an inline-block
        // view for used sizing even though its computed outer display stays inline.
        // CSS 2.1 §10.3.9: inline-block auto width uses the shrink-to-fit
        // available width; otherwise short inline-blocks incorrectly stretch.
        float available_width = block->width;
        IntrinsicSizes intrinsic = {0, 0};
        if (block->is_element()) {
            intrinsic = layout_measure_intrinsic_widths(
                lycon, lam::dom_require<DOM_NODE_ELEMENT>(block));
        }
        float shrink_to_fit_width = min(max(intrinsic.min_content, available_width),
                                        intrinsic.max_content);
        ViewBlock* vertical_parent = layout_nearest_block_ancestor(block->parent_view());
        bool vertical_definite_inline_auto = block->display.inner == CSS_VALUE_FLOW &&
            layout_block_inline_axis_is_vertical(block) && vertical_parent &&
            layout_block_inline_axis_is_vertical(vertical_parent) &&
            (vertical_parent->display.inner == CSS_VALUE_FLOW ||
             vertical_parent->display.inner == CSS_VALUE_FLOW_ROOT) &&
            block->blk &&
            (block->block()->given_height_type == CSS_VALUE_AUTO ||
             block->block()->given_height_type == CSS_VALUE__UNDEF) &&
            vertical_parent->blk && vertical_parent->block()->given_height >= 0.0f;
        if (vertical_definite_inline_auto) {
            shrink_to_fit_width = flow_width;
        }
        if (layout_block_inline_axis_is_vertical(block) &&
            is_multicol_container(block)) {
            shrink_to_fit_width = min(shrink_to_fit_width, flow_width);
        }
        // CSS 2.1 §10.3.9: shrink-to-fit width cannot be less than border+padding
        float min_bp_width = block_box.pad_border_h;
        // CSS 2.1 §10.3.9: Shrink-to-fit width = min(max(preferred_minimum_width,
        bool has_cyclic_min_width_child =
            layout_parent_has_cyclic_min_width_child(block);
        if (block->display.inner != CSS_VALUE_FLEX && !has_cyclic_min_width_child) {
                flow_width = max(flow_width,
                    layout_compute_in_flow_child_width_extent(block, true));
        } else if (has_cyclic_min_width_child) {
            flow_width = min(flow_width, shrink_to_fit_width);
        }
        // overflow it, so their post-flex margin boxes must not enlarge that width.
        block->width = max(flow_width, min_bp_width);
        // CSS 2.1 §10.3.9 + §10.4: Apply min-width/max-width constraints
        block->width = layout_apply_min_max_axis(block, block->width, true, false);
        if (block->width < shrink_to_fit_width) {
            block->width = shrink_to_fit_width;
        }
        if (lycon->block.text_align == CSS_VALUE_CENTER || lycon->block.text_align == CSS_VALUE_RIGHT) {
            float final_content_width = block->width;
            final_content_width -= block_box.pad_border_h;
            layout_align_deferred_inline_line_runs(lam::view_require_element(block),
                                                   final_content_width,
                                                   lycon->block.text_align);
        }
        // CSS 2.1 §10.3.3: Adjust block-level children that stretched to the
        float shrunk_cw = block->width;
        shrunk_cw -= block_box.pad_border_h;
        adjust_block_children_after_shrink(lycon, block, max(shrunk_cw, 0.0f),
                                            lycon->block.text_align);
    }
    if (block->tag_id == MARKUP_NAME_LEGEND && display != CSS_VALUE_INLINE_BLOCK) {
        ViewElement* parent_view = block->parent_view();
        while (parent_view && parent_view->display.outer == CSS_VALUE_CONTENTS) {
            parent_view = parent_view->parent_view();
        }
        ViewBlock* fieldset_parent = parent_view &&
            parent_view->tag_id == MARKUP_NAME_FIELDSET
            ? lam::view_as_block(static_cast<View*>(parent_view)) : nullptr;
        if (fieldset_parent) {
            bool is_first_legend = find_fieldset_rendered_legend(fieldset_parent) ==
                static_cast<DomElement*>(block);
            bool width_is_auto = !block->blk ||
                block->block()->given_width_type == CSS_VALUE_AUTO ||
                block->block()->given_width_type == CSS_VALUE__UNDEF;
            if (is_first_legend && width_is_auto) {
                float min_bp_width = block_box.pad_border_h;
                float shrunk = min(max(flow_width, min_bp_width), block->width);
                block->width = shrunk;
                float legend_cw = block->width;
                legend_cw -= block_box.pad_border_h;
                CssEnum ta = block->blk ? block->block()->text_align : CSS_VALUE__UNDEF;
                adjust_block_children_after_shrink(lycon, block, max(legend_cw, 0.0f), ta);
            }
        }
    }
    if (layout_block_inline_axis_is_vertical(block) &&
        block->display.inner != CSS_VALUE_FLEX &&
        block->display.inner != CSS_VALUE_GRID &&
        block->display.inner != CSS_VALUE_TABLE &&
        !defer_vertical_geometry) {
        layout_publish_vertical_children(block, layout_block_writing_mode(block), false,
            lycon->block.line_height, lycon->block.first_line_max_descender);
        layout_normalize_vertical_breaks(block);
    }
    bool intrinsic_width = layout_axis_uses_intrinsic_size(
        block->blk, LAYOUT_AXIS_X);
    if (intrinsic_width && block_has_in_flow_orthogonal_child(block) &&
        flow_width > block->width) {
        block->width = layout_apply_min_max_axis(block, flow_width, true, true);
        block->content_width = max(block->width - block_box.pad_border_h, 0.0f);
    }
    float overflow_flow_width = max(flow_width, scroll_flow_width);
    float overflow_flow_height = max(flow_height, scroll_flow_height);
    layout_update_axis_overflow(lycon, block, LAYOUT_AXIS_X, overflow_flow_width, display);
    float block_given_height = layout_axis_has_given_size(block, false)
        ? layout_axis_given_size(block->block(), LAYOUT_AXIS_Y) : -1.0f;
    bool ratio_auto_height = block->blk && block->block()->aspect_ratio_auto_height;
    if (block_given_height >= 0 && !ratio_auto_height) { // got specified height
        if (block->height <= 0) {
            block->height = block_given_height;
        }
        block->height = layout_apply_min_max_axis(block, block->height, false, true);
        layout_update_axis_overflow(lycon, block, LAYOUT_AXIS_Y, overflow_flow_height, display);
    }
    else {
        bool has_embed = block->embed != nullptr;
        bool has_flex = has_embed && block->display.inner == CSS_VALUE_FLEX;
        bool is_table = (block->view_type == RDT_VIEW_TABLE);
        if (!has_flex && !is_table) {
            // CSS Box 4 §margin-trim: block-end is handled earlier in
            // CSS 2.1 §10.6.3 + erratum q313: When computing auto height, exclude
            // min/max-height must not affect margin adjacency (q313).
            // inline axis; they were not collapsed, so they must not be
            float collapsible_mb = block && !layout_block_inline_axis_is_vertical(block)
                ? compute_collapsible_bottom_margin(block) : 0.0f;
            if (collapsible_mb != 0 && is_quirky_container(block, lycon)) {
                View* last_child = static_cast<View*>(block->first_child);
                View* last_in_flow = nullptr;
                while (last_child) {
                    if (last_child->view_type && last_child->is_block()) {
                        ViewBlock* vb = lam::view_require_block(last_child);
                        bool is_out_of_flow = (vb->view_type == RDT_VIEW_INLINE_BLOCK) ||
                            layout_block_is_out_of_flow_positioned(vb) ||
                            (vb->position && element_has_float(vb));
                        if (!is_out_of_flow) last_in_flow = last_child;
                    }
                    last_child = static_cast<View*>(last_child->next_sibling);
                }
                if (last_in_flow && last_in_flow->is_block() &&
                    has_quirky_margin(lam::view_require_block(last_in_flow), false)) {
                    collapsible_mb = 0;
                }
            }
            float auto_height = flow_height - collapsible_mb;
            // CSS 2.1 §10.6.3: collapsed negative margins cannot shrink an auto
            // block below its own padding and border box.
            auto_height = max(auto_height, block_box.pad_border_v);
            // CSS 2.1 §10.6.3: Auto height cannot be negative. When all children
            if (auto_height < 0) auto_height = 0;
            // CSS 2.1 §10.7: min-height/max-height refer to the content area for
            float final_height = layout_apply_min_max_axis(
                block, auto_height, false, true);
            float preferred_aspect_ratio = layout_used_preferred_aspect_ratio(block);
            bool height_is_auto_for_ratio = ratio_auto_height || !block->blk ||
                block->block()->given_height < 0.0f ||
                block->block()->given_height_type == CSS_VALUE_AUTO ||
                block->block()->given_height_type == CSS_VALUE__UNDEF;
            bool preserve_ratio_height = layout_preserve_ratio_transferred_min_content(block, false);
            if (height_is_auto_for_ratio && block->display.inner != RDT_DISPLAY_REPLACED &&
                preferred_aspect_ratio > 0.0f && block->width > 0.0f) {
                bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(block) &&
                    layout_uses_border_box(block);
                float ratio_source_width = ratio_uses_border_box
                    ? block->width
                    : layout_content_size_from_border_box(block, block->width, true);
                float ratio_height = layout_aspect_ratio_height(
                    ratio_source_width, preferred_aspect_ratio);
                if (ratio_height >= 0.0f) {
                    float ratio_border_height;
                    if (ratio_uses_border_box) {
                        ratio_border_height = layout_apply_min_max_axis(block, ratio_height, false, true);
                    } else {
                        ratio_height = layout_apply_min_max_axis(block, ratio_height, false, false);
                        ratio_border_height = layout_border_size_from_content_box(block, ratio_height, false);
                    }
                    bool overflow_not_visible = block->scroller &&
                        (block->scroll()->overflow_x != CSS_VALUE_VISIBLE ||
                         block->scroll()->overflow_y != CSS_VALUE_VISIBLE);
                    if (!overflow_not_visible && block->tag_id == MARKUP_NAME_FIELDSET) {
                        DomElement* rendered_legend = find_fieldset_rendered_legend(block);
                        ViewBlock* legend_view = rendered_legend
                            ? lam::view_as_block(static_cast<View*>(rendered_legend)) : nullptr;
                        if (legend_view && legend_view->height > 0.0f) {
                            BoxMetrics fieldset_box = layout_box_metrics(block);
                            float legend_allocation = max(
                                legend_view->height - fieldset_box.padding.bottom, 0.0f);
                            final_height = ratio_border_height + legend_allocation;
                        } else {
                            final_height = max(final_height, ratio_border_height);
                        }
                    } else {
                        if (preserve_ratio_height || overflow_not_visible) {
                            final_height = ratio_border_height;
                        } else {
                            final_height = max(final_height, ratio_border_height);
                        }
                    }
                }
            }
            block->height = final_height;
        }
        bool list_item_table = block->tag_id == MARKUP_NAME_TABLE &&
            block->display.list_item;
        if (list_item_table && block->height <= 0.0f &&
            layout_list_item_has_visible_marker(block)) {
            // CSS Display keeps the list-item marker in the principal flow;
            // a zero-height table grid must not discard that marker line.
            float marker_height = layout_list_item_marker_line_height(lycon);
            block->height = layout_border_size_from_content_box(
                block, marker_height, false);
            block->content_height = marker_height;
        }
    }
    if (block->scroller) {
        if (block->scroll()->intrinsic_gutter_width > 0.0f) {
            block->width += block->scroll()->intrinsic_gutter_width;
            block->scroll_mut()->intrinsic_gutter_width = 0.0f;
        }
        if (block->scroll()->intrinsic_gutter_height > 0.0f) {
            block->height += block->scroll()->intrinsic_gutter_height;
            block->scroll_mut()->intrinsic_gutter_height = 0.0f;
        }
    }
    if (block->form_control() &&
        block->form_control()->control_type == FORM_CONTROL_BUTTON &&
        !layout_block_inline_axis_is_vertical(block) && block->first_child &&
        (!block->blk || !block->block()->text_box_trim)) {
        center_button_text_in_block(
            static_cast<View*>(block->first_child), block->height);
    }
    apply_block_axis_content_alignment(block, flow_height);
    // CSS 2.1 §10.6.7: For BFC roots with AUTO height, floating descendants
    bool has_text_box_trim = block->blk && block->block_mut()->text_box_trim;
    if (lycon->block.establishing_element == block && block_given_height < 0 &&
        !has_text_box_trim) {
        float max_float_bottom = block_context_float_bottom(&lycon->block, false);
        float float_border_box_height = max_float_bottom +
            layout_axis_decoration_end(block->bound, LAYOUT_AXIS_Y);
        if (float_border_box_height > block->height) {
            block->height = layout_apply_min_max_axis(block, float_border_box_height, false, true);
        }
    }
    // CSS Values 4 permits approximating used values outside the layout range;
    // clamp the aggregate border box because separately clamped padding sides
    // can otherwise produce a box beyond the engine's coordinate invariant.
    float bounded_width = layout_clamp_dimension(block->width);
    float bounded_height = layout_clamp_dimension(block->height);
    if (bounded_width != block->width || bounded_height != block->height) {
        block->width = bounded_width;
        block->height = bounded_height;
        block->content_width = max(
            block->width - layout_boundary_padding_border_axis(block->bound, true), 0.0f);
        block->content_height = max(
            block->height - layout_boundary_padding_border_axis(block->bound, false), 0.0f);
    }
    if (block->scroller && block->scroll_mut()->has_clip) {
        set_block_scroller_clip(block);
    }
    if (block->scroller && !block->scroll()->has_clip) {
        if (block->scroll()->overflow_x == CSS_VALUE_HIDDEN ||
            block->scroll()->overflow_x == CSS_VALUE_CLIP ||
            block->scroll()->overflow_y == CSS_VALUE_HIDDEN ||
            block->scroll()->overflow_y == CSS_VALUE_CLIP) {
            set_block_scroller_clip(block);
        }
    }
    if (block->scroller && block->scroll_mut()->pane) {
        DocState* state = lycon && lycon->doc ? (DocState*)lycon->doc->state : nullptr;
        // CSS Writing Modes gives vertical-rl a signed horizontal scroll range;
        // use the inherited/resolved mode so a raw default does not clamp a
        // pending negative scrollLeft to zero before sticky positioning sees it.
        bool reverse_x_scroll = layout_block_writing_mode(block) == WM_VERTICAL_RL;
        if (reverse_x_scroll && scroll_flow_width > block->width) {
            // CSS Writing Modes: vertical-rl establishes the horizontal scroll
            // origin at the block-end edge, so overflow extends into negatives.
            scroll_min_x = min(scroll_min_x, block->width - scroll_flow_width);
        }
        float h_max = reverse_x_scroll && scroll_min_x < 0.0f
            ? 0.0f
            : (scroll_flow_width > block->width ? scroll_flow_width - block->width : 0.0f);
        float v_max = scroll_flow_height > block->height ? scroll_flow_height - block->height : 0.0f;
        // CSS Overflow 3: reverse flex flows may place the scroll origin before
        // the padding edge, so their scroll range has a signed lower endpoint.
        scroll_state_set_range_for_view(state, static_cast<View*>(block),
            block->scroll()->pane, scroll_min_x, h_max, scroll_min_y, v_max);
        scroll_apply_pending_element_scroll(block);
    }
}

static void layout_resolve_auto_margins_after_width_change(
        ViewBlock* block, float containing_width, bool is_float) {
    if (!block || !block->bound || is_float) return;
    if (block->display.outer == CSS_VALUE_INLINE ||
        block->display.outer == CSS_VALUE_INLINE_BLOCK) return;
    if (block->boundary()->margin.left_type != CSS_VALUE_AUTO &&
        block->boundary()->margin.right_type != CSS_VALUE_AUTO) return;
    float available_width = containing_width > 0.0f ? containing_width : 0.0f;
    float old_margin_left = block->boundary()->margin.left;
    layout_resolve_auto_margin_pair(
        available_width, block->width,
        block->boundary()->margin.left_type == CSS_VALUE_AUTO,
        block->boundary()->margin.right_type == CSS_VALUE_AUTO,
        &block->boundary_mut()->margin.left, &block->boundary_mut()->margin.right);
    block->x += block->boundary()->margin.left - old_margin_left;
}

static void update_multipass_advance_y(LayoutContext* lycon, ViewBlock* block) {
    lycon->block.advance_y = block->height;
    lycon->block.advance_y -= layout_axis_decoration_end(block->bound, LAYOUT_AXIS_Y);
    if (block->display.inner == CSS_VALUE_FLEX && block->is_element() && block->scroller) {
        ViewElement* flex_element = lam::view_require_element(block);
        if (block->scroll()->overflow_x != CSS_VALUE_CLIP) {
            lycon->block.max_width = max(lycon->block.max_width,
                layout_in_flow_content_extent(flex_element, LAYOUT_AXIS_X));
        }
        if (block->scroll()->overflow_y != CSS_VALUE_CLIP) {
            lycon->block.advance_y = max(lycon->block.advance_y,
                layout_in_flow_content_extent(flex_element, LAYOUT_AXIS_Y));
        }
    }
}

static void update_inline_multipass_width(LayoutContext* lycon, ViewBlock* block,
                                          bool include_text) {
    if (block->display.outer != CSS_VALUE_INLINE_BLOCK ||
        layout_axis_has_given_size(block, true)) return;
    float max_right = 0.0f;
    for (View* child = block->first_child; child; child = child->next_sibling) {
        if (include_text && child->view_type == RDT_VIEW_TEXT) {
            max_right = max(max_right, child->x + child->width);
            continue;
        }
        if (child->view_type != RDT_VIEW_BLOCK &&
            child->view_type != RDT_VIEW_INLINE_BLOCK &&
            child->view_type != RDT_VIEW_LIST_ITEM) continue;
        ViewElement* item = lam::view_as_element(child);
        if (!item || layout_block_is_out_of_flow_positioned(lam::view_require_block(child))) {
            continue;
        }
        float right = item->x + item->width +
            layout_box_metrics(lam::view_as_block(item)).margin.right;
        max_right = max(max_right, right);
    }
    if (max_right <= 0.0f) return;
    lycon->block.max_width = max_right;
}

static void layout_empty_flex_or_grid(LayoutContext* lycon, ViewBlock* block,
                                      bool is_grid) {
    auto start = high_resolution_clock::now();
    if (is_grid) layout_grid_content(lycon, block);
    else layout_flex_content(lycon, block);
    double elapsed = duration<double, std::milli>(
        high_resolution_clock::now() - start).count();
    if (is_grid) g_grid_layout_time += elapsed;
    else g_flex_layout_time += elapsed;
    update_multipass_advance_y(lycon, block);
    finalize_block_flow(lycon, block, block->display.outer);
}

static void layout_table_block_content(LayoutContext* lycon, ViewBlock* block,
                                       bool empty) {
    auto start = high_resolution_clock::now();
    float margin_containing_width = lycon->block.content_width;
    layout_table_content(lycon, block, block->display);
    g_table_layout_time += duration<double, std::milli>(high_resolution_clock::now() - start).count();
    update_multipass_advance_y(lycon, block);
    finalize_block_flow(lycon, block, block->display.outer);
    if (!block->blk || block->block()->given_width < 0) {
        float shrink_width = block->content_width +
            (block->bound && block->boundary_mut()->border ? block->boundary_mut()->border->width.right : 0.0f);
        block->width = empty ? layout_apply_min_max_axis(block, shrink_width, true, false)
                             : layout_floor_min_axis(block, shrink_width, true);
        layout_resolve_auto_margins_after_width_change(block, margin_containing_width,
            block->position && element_has_float(block));
    }
}

static Url* clone_document_url_for_iframe(LayoutContext* lycon) {
    DomDocument* parent_doc = lycon && lycon->ui_context ?
        lycon->ui_context->document : nullptr;
    const char* href = parent_doc && parent_doc->url ?
        url_get_href(parent_doc->url) : nullptr;
    Url* cloned = href && *href ? url_parse(href) : nullptr;
    if (cloned && cloned->is_valid) return cloned;
    if (cloned) url_destroy(cloned);
    return get_current_dir();
}

static DomDocument* load_iframe_srcdoc_doc(LayoutContext* lycon,
                                           const char* srcdoc,
                                           int viewport_width,
                                           int viewport_height) {
    if (!srcdoc || !*srcdoc) return nullptr;
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "iframe_srcdoc");
    if (!pool) {
        log_error("iframe_srcdoc_load: failed to create memory pool");
        return nullptr;
    }
    Url* base_url = clone_document_url_for_iframe(lycon);
    if (!base_url) {
        log_error("iframe_srcdoc_load: failed to create base URL");
        pool_destroy(pool);
        return nullptr;
    }
    DomDocument* doc = load_lambda_html_doc(base_url, nullptr,
        viewport_width, viewport_height, pool, srcdoc, false, true);
    if (!doc) {
        url_destroy(base_url);
        pool_destroy(pool);
    }
    return doc;
}

static DomDocument* load_iframe_src_doc(LayoutContext* lycon,
                                        const char* src,
                                        int viewport_width,
                                        int viewport_height) {
    if (!lycon || !lycon->ui_context || !lycon->ui_context->document ||
        !lycon->ui_context->document->url || !src || !*src) {
        return nullptr;
    }
    size_t src_len = strlen(src);
    StrBuf* src_buf = strbuf_new_cap(src_len);
    strbuf_append_str_n(src_buf, src, src_len);
    DomDocument* doc = load_html_doc(lycon->ui_context->document->url,
        src_buf->str, viewport_width, viewport_height, 1.0f);
    strbuf_free(src_buf);
    return doc;
}

void layout_iframe_embedded_doc(LayoutContext* lycon, DomDocument* doc,
                                int iframe_width, int iframe_height) {
    if (!lycon || !lycon->ui_context || !doc || !doc->html_root) return;
    DomDocument* parent_doc = lycon->ui_context->document;
    float saved_window_width = lycon->ui_context->window_width;
    float saved_window_height = lycon->ui_context->window_height;
    int saved_viewport_width = lycon->ui_context->viewport_width;
    int saved_viewport_height = lycon->ui_context->viewport_height;
    lycon->ui_context->document = doc;
    lycon->ui_context->window_width = (float)iframe_width;
    lycon->ui_context->window_height = (float)iframe_height;
    lycon->ui_context->viewport_width = iframe_width;
    lycon->ui_context->viewport_height = iframe_height;
    process_document_font_faces(lycon->ui_context, doc);
    layout_html_doc(lycon->ui_context, doc, false);
    lycon->ui_context->document = parent_doc;
    lycon->ui_context->window_width = saved_window_width;
    lycon->ui_context->window_height = saved_window_height;
    lycon->ui_context->viewport_width = saved_viewport_width;
    lycon->ui_context->viewport_height = saved_viewport_height;
}

void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    DomDocument* doc = NULL;
    // owned by this UI tree so unrelated documents on the same thread cannot
    if (lycon->ui_context->iframe_depth >= MAX_IFRAME_DEPTH) {
        log_warn("iframe: maximum nesting depth (%d) exceeded, skipping", MAX_IFRAME_DEPTH);
        return;
    }
    if (!(block->embed && block->embedp()->doc)) {
        const char* srcdoc = block->get_attribute("srcdoc");
        const char* src = block->get_attribute("src");
        if ((srcdoc && *srcdoc) || (src && *src)) {
            LayoutContentBox iframe_content = layout_content_box(block);
            // The embedded viewport is the iframe content box; using the outer
            // border box creates false overflow and an inner scrollbar.
            int iframe_width = block->width > 0 ?
                (int)iframe_content.width : (int)lycon->ui_context->window_width; // INT_CAST_OK: iframe viewport expects int
            int iframe_height = block->height > 0 ?
                (int)iframe_content.height : (int)lycon->ui_context->window_height; // INT_CAST_OK: iframe viewport expects int
            lycon->ui_context->iframe_depth++;
            if (srcdoc && *srcdoc) {
                doc = load_iframe_srcdoc_doc(lycon, srcdoc,
                    iframe_width, iframe_height);
            } else {
                doc = load_iframe_src_doc(lycon, src,
                    iframe_width, iframe_height);
            }
            if (!doc) {
                lycon->ui_context->iframe_depth--;
            } else {
                radiant_document_ensure_state(doc, "layout_iframe");
                if (!(block->embed)) block->ensure_embed(lycon);
                block->embed->doc = doc; // assign loaded document to embed property
                layout_iframe_embedded_doc(lycon, doc, iframe_width, iframe_height);
                lycon->ui_context->iframe_depth--;
            }
        }
    }
    else {
        doc = block->embedp()->doc;
    }
    if (doc && doc->view_tree && doc->view_tree->root) {
        ViewBlock* root = lam::view_require_block(doc->view_tree->root);
        float iframe_width = root->content_width > 0 ? root->content_width : root->width;
        float iframe_height = root->content_height > 0 ? root->content_height : root->height;
        lycon->block.max_width = iframe_width;
        lycon->block.advance_y = iframe_height;
        if (root->scroller) {
            if (root->content_height > root->height) {
                root->height = root->content_height;  // restore full content height
            }
            root->scroller = NULL;
        }
    }
    block->ensure_scroll(lycon);
    block->scroller->overflow_y = CSS_VALUE_AUTO;
    finalize_block_flow(lycon, block, display.outer);
    if (block->scroller && block->scroll_mut()->pane) {
        float v_max = block->content_height > block->height ?
            block->content_height - block->height : 0;
        DocState* state = (DocState*)lycon->doc->state;
        float h_max = 0.0f;
        scroll_state_get_position_for_view(state, static_cast<View*>(block), block->scroll()->pane,
                                           NULL, NULL, &h_max, NULL);
        scroll_state_set_max_for_view(state, static_cast<View*>(block), block->scroll()->pane, h_max, v_max);
    }
}

void layout_inline_svg(LayoutContext* lycon, ViewBlock* block) {
    Element* native_elem = dom_element_backing(lam::dom_require_element(block));
    if (!native_elem) {
        block->width = 300;  // HTML default for SVG
        block->height = 150;
        return;
    }
    SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(native_elem);
    float used_aspect_ratio = layout_used_preferred_aspect_ratio(block);
    if (used_aspect_ratio <= 0.0f) used_aspect_ratio = intrinsic.aspect_ratio;
    bool width_uses_intrinsic_keyword = layout_intrinsic_preferred_size_keyword(block, true) !=
        CSS_VALUE__UNDEF;
    bool parent_has_definite_slot = lycon->block.parent &&
        lycon->block.parent->content_width > 0.0f &&
        lycon->block.parent->content_height > 0.0f;
    bool parent_has_definite_inline_slot = lycon->block.parent &&
        lycon->block.parent->content_width > 0.0f;
    bool use_parent_slot = parent_has_definite_slot &&
        !intrinsic.has_intrinsic_width &&
        !intrinsic.has_intrinsic_height &&
        !intrinsic.has_intrinsic_aspect_ratio;
    bool use_parent_ratio_slot = parent_has_definite_inline_slot &&
        !intrinsic.has_intrinsic_width &&
        !intrinsic.has_intrinsic_height &&
        intrinsic.has_intrinsic_aspect_ratio;
    float preferred_aspect_ratio = layout_used_preferred_aspect_ratio(block);
    bool use_zero_ratio_slot = lycon->block.parent &&
        lycon->block.parent->content_width <= 0.0f &&
        !intrinsic.has_intrinsic_width && !intrinsic.has_intrinsic_height &&
        (intrinsic.has_intrinsic_aspect_ratio || preferred_aspect_ratio > 0.0f);
    bool width_is_automatic = layout_block_has_automatic_size(block, true);
    bool height_is_automatic = layout_block_has_automatic_size(block, false);
    float width = (!width_is_automatic && layout_axis_has_given_size(block, true) &&
                   block->block()->given_width_type != CSS_VALUE_AUTO)
        ? block->block()->given_width : -1.0f;
    float height = (!height_is_automatic && layout_axis_has_given_size(block, false) &&
                    block->block()->given_height_type != CSS_VALUE_AUTO)
        ? block->block()->given_height : -1.0f;
    bool containment_used_width = width_is_automatic &&
        layout_block_has_size_containment_in_axis(block, true) &&
        block->block()->given_width >= 0.0f;
    bool containment_used_height = height_is_automatic &&
        layout_block_has_size_containment_in_axis(block, false) &&
        block->block()->given_height >= 0.0f;
    if (containment_used_width) {
        width = block->block()->given_width;
    }
    if (containment_used_height) {
        height = block->block()->given_height;
    }
    bool is_border_box = layout_uses_border_box(block);
    float content_width = -1.0f;
    float content_height = -1.0f;
    bool width_is_specified = width >= 0.0f;
    bool height_is_specified = height >= 0.0f;
    if (width_is_specified) {
        content_width = layout_content_size_if_border_box(block, width, true);
    }
    if (height_is_specified) {
        // result; otherwise a specified height can overwrite max-height:min-content.
        height = layout_apply_min_max_axis(block, height, false, false);
        content_height = layout_content_size_if_border_box(block, height, false);
    }
    if (content_width >= 0 && content_height < 0) {
        if (containment_used_width && !containment_used_height) {
            content_height = intrinsic.height;
        } else if (used_aspect_ratio > 0) {
            content_height = content_width / used_aspect_ratio;
        } else {
            content_height = intrinsic.height;
        }
    } else if (content_height >= 0 && content_width < 0) {
        if (containment_used_height && !containment_used_width) {
            content_width = intrinsic.width;
        } else if (width_uses_intrinsic_keyword && intrinsic.has_intrinsic_width) {
            content_width = intrinsic.width;
        } else if (used_aspect_ratio > 0) {
            content_width = content_height * used_aspect_ratio;
        } else {
            content_width = intrinsic.width;
        }
    } else if (content_width < 0 && content_height < 0) {
        if (use_zero_ratio_slot) {
            content_width = 0.0f;
        } else if (use_parent_ratio_slot) {
            float stretch_css_width = layout_stretch_fit_used_css_size(
                block, lycon->block.parent->content_width, true);
            content_width = layout_content_size_if_border_box(block, stretch_css_width, true);
        } else if (use_parent_slot) {
            content_width = lycon->block.parent->content_width;
        } else if (width_is_automatic && height_is_automatic &&
                   !intrinsic.has_intrinsic_width && !intrinsic.has_intrinsic_height &&
                   intrinsic.has_intrinsic_aspect_ratio && lycon->block.parent &&
                   lycon->block.parent->content_width > 0.0f) {
            content_width = lycon->block.parent->content_width;
        } else if (intrinsic.has_intrinsic_width) {
            content_width = intrinsic.width;
        } else {
            content_width = 300;  // HTML default
        }
        if (use_zero_ratio_slot) {
            content_height = 0.0f;
        } else if (use_parent_ratio_slot) {
            // CSS Sizing 3 §5.1: a ratio-only replaced box derives its block
            content_height = used_aspect_ratio > 0.0f
                ? content_width / used_aspect_ratio : 0.0f;
        } else if (use_parent_slot) {
            content_height = lycon->block.parent->content_height;
        } else if (intrinsic.has_intrinsic_height) {
            content_height = intrinsic.height;
        } else if (used_aspect_ratio > 0) {
            content_height = content_width / used_aspect_ratio;
        } else {
            content_height = 150;  // HTML default
        }
    }
    if (width_is_automatic && height_is_automatic && used_aspect_ratio > 0.0f) {
        layout_apply_aspect_ratio_min_max_constraints(
            block, used_aspect_ratio, &content_width, &content_height);
    }
    LayoutAxisPair<bool> automatic = {width_is_automatic, height_is_automatic};
    LayoutAxisPair<bool> specified = {width_is_specified, height_is_specified};
    LayoutAxisPair<float> content = layout_axis_pair(
        max(content_width, 0.0f), max(content_height, 0.0f));
    LayoutAxisPair<float> authored = layout_axis_pair(width, height);
    for (LayoutAxis axis : layout_axes()) {
        bool horizontal = layout_axis_is_horizontal(axis);
        if (automatic[axis]) {
            float used = layout_border_size_if_content_box(block, content[axis], horizontal);
            layout_store_given_axis(lycon, block, used, horizontal, false);
        }
        if (specified[axis] && is_border_box) {
            authored[axis] = layout_floor_border_box_axis(block, authored[axis], horizontal);
        }
    }
    block->content_width = content.x;
    block->content_height = content.y;
    block->width = layout_border_size_from_content_box(block, content.x, true);
    block->height = layout_border_size_from_content_box(block, content.y, false);
    if (specified.x && is_border_box) {
        block->width = authored.x;
    }
    if (specified.y && is_border_box) {
        block->height = authored.y;
    }
}

void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before) {
    if (!parent || !pseudo) return;
    for (DomNode* c = parent->first_child; c; c = c->next_sibling) {
        if (dom_subtree_contains_node(c, static_cast<DomNode*>(pseudo))) return;
    }
    if (is_before) {
        DomNode* old_first = parent->first_child;
        pseudo->next_sibling = old_first;
        pseudo->prev_sibling = nullptr;
        if (old_first) {
            old_first->prev_sibling = pseudo;
        }
        parent->first_child = pseudo;
    } else {
        if (!parent->first_child) {
            parent->first_child = pseudo;
            pseudo->prev_sibling = nullptr;
            pseudo->next_sibling = nullptr;
        } else {
            DomNode* last = parent->first_child;
            while (last->next_sibling) {
                last = last->next_sibling;
            }
            last->next_sibling = pseudo;
            pseudo->prev_sibling = last;
            pseudo->next_sibling = nullptr;
        }
    }
}

static void remove_pseudo_from_dom(DomElement* parent, DomElement* pseudo) {
    if (!parent || !pseudo) return;
    DomNode* previous = nullptr;
    for (DomNode* child = parent->first_child; child;
         child = child->next_sibling) {
        if (child != static_cast<DomNode*>(pseudo)) {
            previous = child;
            continue;
        }
        DomNode* next = child->next_sibling;
        if (previous) previous->next_sibling = next;
        else parent->first_child = next;
        if (next) next->prev_sibling = previous;
        if (parent->last_child == child) parent->last_child = previous;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
        return;
    }
}

static void insert_pseudo_into_rendered_tree(DomElement* element,
                                             DomElement* pseudo,
                                             bool is_before) {
    if (!element || !pseudo) return;
    DomElement* shadow_root = element->shadow_root_element();
    if (!shadow_root) {
        insert_pseudo_into_dom(element, pseudo, is_before);
        return;
    }
    // CSS Shadow DOM: host-generated content is in the host's rendered child
    // sequence, but must stay out of light-DOM slot assignment.
    remove_pseudo_from_dom(element, pseudo);
    insert_pseudo_into_dom(shadow_root, pseudo, is_before);
}

void layout_materialize_pseudo_content(LayoutContext* lycon, ViewBlock* block,
                                       bool include_marker, bool create_first_letter) {
    if (!lycon || !block || !block->is_element()) return;
    block->pseudo = alloc_pseudo_content_prop(lycon, block);
    DomElement* element = lam::dom_require<DOM_NODE_ELEMENT>(block);
    if (block->pseudo) {
        if (block->pseudo->before) {
            insert_pseudo_into_rendered_tree(element, block->pseudo->before, true);
        }
        if (block->pseudo->after) {
            insert_pseudo_into_rendered_tree(element, block->pseudo->after, false);
        }
        if (include_marker && block->pseudo->marker) {
            insert_pseudo_into_dom(element, block->pseudo->marker, true);
        }
    }
    if (create_first_letter && element->pseudo_style(PSEUDO_STYLE_FIRST_LETTER)) {
        create_first_letter_pseudo(lycon, block);
    }
}

void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display);

static DisplayValue layout_button_used_display(DomElement* element,
                                                DisplayValue computed) {
    if (!element || element->tag() != MARKUP_NAME_BUTTON ||
        computed.outer == CSS_VALUE_NONE || computed.outer == CSS_VALUE_CONTENTS) {
        return computed;
    }
    // HTML Rendering §15.5.3: buttons preserve flex/grid, use inline-block for
    // an inline outer type, and otherwise establish a flow-root for children.
    if (computed.inner == CSS_VALUE_FLEX || computed.inner == CSS_VALUE_GRID) {
        return computed;
    }
    computed.list_item = false;
    if (computed.outer == CSS_VALUE_LIST_ITEM) {
        // HTML button layout replaces the list-item principal box with a
        // flow-root; the browser therefore does not expose a marker box here.
        computed.outer = CSS_VALUE_BLOCK;
    } else if (computed.outer == CSS_VALUE_INLINE ||
               computed.outer == CSS_VALUE_INLINE_BLOCK) {
        computed.outer = CSS_VALUE_INLINE_BLOCK;
    } else {
        computed.outer = CSS_VALUE_BLOCK;
    }
    computed.inner = CSS_VALUE_FLOW_ROOT;
    return computed;
}

static CssEnum get_element_float_value(DomElement* elem) {
    if (!elem) return CSS_VALUE_NONE;
    if (elem->position) {
        return elem->positionp()->float_prop;
    }
    return layout_specified_keyword(elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
}

static DomElement* find_fieldset_legend_in_contents(DomNode* first_child) {
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* candidate = child->as_element();
        DisplayValue display = resolve_display_value(candidate);
        if (layout_display_is_none(display)) continue;
        if (display.outer == CSS_VALUE_CONTENTS) {
            DomElement* nested = find_fieldset_legend_in_contents(
                candidate->first_child);
            if (nested) return nested;
            continue;
        }
        if (candidate->tag() != MARKUP_NAME_LEGEND ||
            get_element_float_value(candidate) != CSS_VALUE_NONE) {
            continue;
        }
        CssEnum position = candidate->position
            ? candidate->positionp()->position
            : layout_specified_keyword(candidate, CSS_PROPERTY_POSITION,
                                       CSS_VALUE_STATIC);
        if (position != CSS_VALUE_ABSOLUTE && position != CSS_VALUE_FIXED) {
            return candidate;
        }
    }
    return nullptr;
}

DomElement* find_fieldset_rendered_legend(ViewBlock* fieldset) {
    if (!fieldset || !fieldset->is_element() ||
        fieldset->tag() != MARKUP_NAME_FIELDSET) {
        return nullptr;
    }
    // CSS Display 3 box generation: fieldset legend selection follows the
    // flattened child tree through display:contents wrappers.
    return find_fieldset_legend_in_contents(fieldset->first_child);
}

static float fieldset_legend_content_width(ViewBlock* legend,
                                           float* content_start) {
    if (!legend || !legend->view_type) return 0.0f;
    float width = 0.0f;
    float start = legend->x;
    for (View* child = legend->first_child; child; child = child->next()) {
        if (!child->view_type) continue;
        width = max(width, child->width);
        start = min(start, child->x);
    }
    if (content_start) *content_start = start;
    return width;
}

static void shift_vertical_fieldset_baselines(ViewBlock* fieldset,
                                              float delta) {
    if (!fieldset || delta == 0.0f) return;
    if (fieldset->blk) {
        fieldset->block_mut()->first_line_baseline += delta;
        fieldset->block_mut()->last_line_baseline += delta;
        fieldset->block_mut()->last_line_max_ascender += delta;
    }
    if (fieldset->embed && fieldset->embed->flex) {
        fieldset->embedp()->flex->first_baseline += delta;
        fieldset->embedp()->flex->last_baseline += delta;
    }
}

static float fieldset_vertical_last_baseline_from_line_context(
    ViewBlock* fieldset, float baseline) {
    if (!fieldset || !fieldset->blk ||
        fieldset->tag() != MARKUP_NAME_FIELDSET ||
        !layout_block_inline_axis_is_vertical(fieldset) ||
        fieldset->block()->baseline_source != CSS_VALUE_LAST) {
        return baseline;
    }
    return baseline - fieldset->y;
}

static void shift_fieldset_vertical_content_x(ViewBlock* fieldset,
                                              DomElement* rendered_legend,
                                              float delta, bool shift_breaks) {
    if (!fieldset || !rendered_legend || delta == 0.0f) return;
    for (DomNode* child = fieldset->first_child; child; child = child->next_sibling) {
        if (child == static_cast<DomNode*>(rendered_legend) ||
            (!shift_breaks && child->tag() == MARKUP_NAME_BR) ||
            !child->view_type) continue;
        if (child->is_element()) child->as_element()->x += delta;
    }
}

static void align_fieldset_vertical_content_to_legend(
    ViewBlock* fieldset, DomElement* rendered_legend,
    bool vertical_rl, float clearance) {
    if (!fieldset || !rendered_legend || !fieldset->is_element()) return;
    ViewBlock* legend = lam::view_as_block(static_cast<View*>(rendered_legend));
    if (!legend || !legend->view_type) return;
    float content_edge = vertical_rl ? -FLT_MAX : FLT_MAX;
    bool have_content = false;
    for (DomNode* child = fieldset->first_child; child; child = child->next_sibling) {
        if (child == static_cast<DomNode*>(rendered_legend) ||
            child->tag() == MARKUP_NAME_BR || !child->view_type || !child->is_element()) {
            continue;
        }
        ViewBlock* child_block = lam::view_as_block(static_cast<View*>(child));
        if (!child_block) continue;
        float edge = vertical_rl
            ? child_block->x + child_block->width
            : child_block->x;
        content_edge = vertical_rl ? max(content_edge, edge) : min(content_edge, edge);
        have_content = true;
    }
    if (!have_content) return;
    float legend_edge = vertical_rl
        ? legend->x
        : legend->x + legend->width;
    float target_edge = vertical_rl
        ? legend_edge - clearance
        : legend_edge + clearance;
    shift_fieldset_vertical_content_x(
        fieldset, rendered_legend, target_edge - content_edge, false);
}

static bool fieldset_contains_node(DomNode* ancestor, DomNode* node) {
    for (DomNode* current = node; current; current = current->parent) {
        if (current == ancestor) return true;
    }
    return false;
}

static DomNode* fieldset_first_flow_node(DomNode* first_child,
                                          DomElement* rendered_legend) {
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (child == static_cast<DomNode*>(rendered_legend)) continue;
        if (child->is_element()) {
            DisplayValue display = resolve_display_value(child);
            if (layout_display_is_none(display)) continue;
            if (display.outer == CSS_VALUE_CONTENTS &&
                fieldset_contains_node(child, static_cast<DomNode*>(rendered_legend))) {
                DomNode* nested = fieldset_first_flow_node(
                    child->as_element()->first_child, rendered_legend);
                if (nested) return nested;
                continue;
            }
        }
        return child;
    }
    return nullptr;
}

static void fieldset_initialize_contents_ancestors(
        LayoutContext* lycon, DomElement* fieldset, DomNode* node) {
    if (!lycon || !fieldset || !node || node == static_cast<DomNode*>(fieldset)) return;
    if (node->parent && node->parent != static_cast<DomNode*>(fieldset)) {
        fieldset_initialize_contents_ancestors(
            lycon, fieldset, node->parent);
    }
    if (node->is_element()) {
        DomElement* element = node->as_element();
        if (resolve_display_value(element).outer == CSS_VALUE_CONTENTS) {
            layout_init_display_contents_view(lycon, element);
        }
    }
}

static DomNode* fieldset_next_flow_child(ViewBlock* fieldset, DomNode* current,
                                         DomElement* rendered_legend) {
    if (!fieldset || !current || !rendered_legend) return nullptr;
    if (current == static_cast<DomNode*>(rendered_legend)) {
        return fieldset_first_flow_node(fieldset->first_child, rendered_legend);
    }

    DomNode* cursor = current;
    DomNode* next = cursor->next_sibling;
    while (true) {
        while (next) {
            DomNode* flow_node = fieldset_first_flow_node(next, rendered_legend);
            if (flow_node) return flow_node;
            next = next->next_sibling;
        }
        DomNode* parent = cursor->parent;
        if (!parent || parent == static_cast<DomNode*>(fieldset)) return nullptr;
        cursor = parent;
        next = cursor->next_sibling;
    }
}

static bool prescan_node_has_in_flow_inline_content(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) {
        return layout_dom_text_has_non_whitespace(node->as_text());
    }
    if (!node->is_element()) return false;
    DomElement* elem = node->as_element();
    DisplayValue display = resolve_display_value(node);
    if (layout_display_is_none(display)) return false;
    if (elem->position &&
        (layout_position_is_abs_fixed(elem->position) ||
         layout_position_is_floated(elem->position))) {
        return false;
    }
    if (display.outer != CSS_VALUE_INLINE) return false;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (prescan_node_has_in_flow_inline_content(child)) return true;
    }
    return false;
}

void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* parent_block) {
    if (!first_child) return;
    bool has_floats = false;
    bool has_inline_content = false;
    float preceding_content_width = 0.0f;  // Estimated width of content before first float
    float container_width = lycon->block.content_width;
    DomNode* first_float_node = nullptr;
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) {
            if (child->is_text()) {
                DomText* text = child->as_text();
                if (text && text->text && !first_float_node) {
                    const char* p = text->text;
                    int char_count = 0;
                    while (*p) {
                        if (!str_char_is_ascii_space(*p)) char_count++;
                        p++;
                    }
                    preceding_content_width += char_count * 8.0f;
                    if (char_count > 0) has_inline_content = true;
                }
            }
            continue;
        }
        DomElement* elem = child->as_element();
        if (elem->float_prelaid()) continue;
        CssEnum float_value = get_element_float_value(elem);
        if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
            has_floats = true;
            if (!first_float_node) first_float_node = child;
            continue;
        }
        if (!first_float_node) {
            DisplayValue display = resolve_display_value(child);
            if (elem->tag() == MARKUP_NAME_BUTTON) {
                display = layout_button_used_display(elem, display);
            }
            if (display.outer == CSS_VALUE_INLINE || display.outer == CSS_VALUE_INLINE_BLOCK) {
                // css 2.1 §9.5: an empty inline box does not consume line
                // space before a later float and must not suppress float prescan.
                if (display.outer == CSS_VALUE_INLINE
                    ? prescan_node_has_in_flow_inline_content(child) : true) {
                    has_inline_content = true;
                }
                for (DomNode* text_node = elem->first_child; text_node; text_node = text_node->next_sibling) {
                    if (text_node->is_text()) {
                        DomText* text = text_node->as_text();
                        if (text && text->text) {
                            const char* p = text->text;
                            int char_count = 0;
                            while (*p) {
                                if (!str_char_is_ascii_space(*p)) char_count++;
                                p++;
                            }
                            preceding_content_width += char_count * 8.0f;
                        }
                    }
                }
            } else if (display.outer == CSS_VALUE_BLOCK) {
                return;
            }
        }
    }
    if (!has_floats) {
        return;
    }
    if (has_inline_content && container_width > 0) {
        float float_width = 100.0f;  // Conservative estimate
        if (preceding_content_width + float_width > container_width) {
            return;
        }
    }
    // CSS 2.1 §9.5: Floats belong to their nearest BFC ancestor, not to non-BFC
    if (!lycon->block.establishing_element && parent_block) {
        if (block_context_establishes_bfc(parent_block)) {
            lycon->block.establishing_element = parent_block;
            lycon->block.float_right_edge = parent_block->content_width > 0 ? parent_block->content_width : parent_block->width;
        }
    }
    BlockContext* prescan_bfc = block_context_find_bfc(&lycon->block);
    if (!prescan_bfc || !prescan_bfc->establishing_element) {
        return;
    }
    // for correct float positioning, but it must be restored so the main inline
    float saved_advance_y = lycon->block.advance_y;
    // CSS 2.1 §9.5.1 Rule 6: "The outer top of a floating box may not be higher than
    // appear at or below that block's top edge - they cannot be pre-scanned to y=0.
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (!child->is_element()) {
            if (prescan_node_has_in_flow_inline_content(child)) break;
            continue;
        }
        DomElement* elem = child->as_element();
        if (elem->float_prelaid()) continue;
        DisplayValue display = resolve_display_value(child);
        if (layout_display_is_none(display)) continue;
        CssEnum float_value = get_element_float_value(elem);
        // Subsequent floats must be laid out in normal flow order
        if (float_value != CSS_VALUE_LEFT && float_value != CSS_VALUE_RIGHT) {
            if (display.outer == CSS_VALUE_BLOCK) {
                break;  // Stop pre-scanning - remaining floats go through normal flow
            }
            if (prescan_node_has_in_flow_inline_content(child)) {
                break;
            }
            // CSS 2.1 §9.5.2: <br> with clear property forces subsequent floats to
            if (elem->tag() == MARKUP_NAME_BR && elem->specified_style && elem->specified_style->tree) {
                AvlNode* clear_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_CLEAR);
                if (clear_node) {
                    StyleNode* sn = (StyleNode*)clear_node->declaration;
                    if (sn && sn->winning_decl && sn->winning_decl->value &&
                        sn->winning_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                        sn->winning_decl->value->data.keyword != CSS_VALUE_NONE) {
                        CssEnum clear_type = sn->winning_decl->value->data.keyword;
                        BlockContext* bfc = block_context_find_bfc(&lycon->block);
                        if (bfc) {
                            float clear_y = block_context_clear_y(bfc, clear_type);
                            float local_clear_y = clear_y - lycon->block.bfc_offset_y;
                            if (local_clear_y > lycon->block.advance_y) {
                                lycon->block.advance_y = local_clear_y;
                            }
                        }
                    }
                }
            }
            continue;  // Skip non-float non-block elements
        }

        display.outer = CSS_VALUE_BLOCK;  // Floats become block per CSS 9.7
        elem->set_float_prelaid(true);
        layout_block(lycon, child, display);
    }
    lycon->block.advance_y = saved_advance_y;
    // IMPORTANT: Floats are registered to the BFC (parent chain), not lycon->block
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc && (bfc->left_float_count > 0 || bfc->right_float_count > 0)) {
        float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;
        float bfc_y_offset = 0.0f;
        float bfc_x_offset = 0.0f;
        ViewElement* walker = parent_block;
        ViewBlock* bfc_elem = bfc->establishing_element;
        while (walker && walker != bfc_elem) {
            bfc_y_offset += walker->y;
            bfc_x_offset += walker->x;
            walker = walker->parent_view();
        }
        float query_y = bfc_y_offset + lycon->block.advance_y;
        // CSS 2.1 §9.5: a line inside a negatively offset run-in uses its
        // local inline origin when testing the shared BFC's float intrusion.
        FloatAvailableSpace space = block_context_space_at_y(
            bfc, query_y, line_height, false, false, bfc_x_offset);
        if (space.has_left_float) {
            float local_left = space.left - bfc_x_offset;
            if (local_left > lycon->line.effective_left) {
                lycon->line.effective_left = local_left;
                lycon->line.has_float_intrusion = true;
                if (lycon->line.advance_x < local_left) {
                    lycon->line.advance_x = local_left;
                }
            }
        }
        if (space.has_right_float) {
            float local_right = space.right - bfc_x_offset;
            if (local_right < lycon->line.effective_right) {
                lycon->line.effective_right = local_right;
                lycon->line.has_float_intrusion = true;
            }
        }
    }

}

void layout_block_inner_content(LayoutContext* lycon, ViewBlock* block) {
    if (block->position) {
        ViewBlock* abs_walker = block->positionp()->first_abs_child;
        while (abs_walker) {
            ViewBlock* abs_next = abs_walker->position ? abs_walker->positionp()->next_abs_sibling : nullptr;
            if (abs_walker->position) abs_walker->position->next_abs_sibling = nullptr;
            abs_walker = abs_next;
        }
        block->position->first_abs_child = nullptr;
        block->position->last_abs_child = nullptr;
    }
    if (block->blk && block->block()->content_visibility_hidden) {
        return;
    }
    if (block->is_element()) {
        layout_materialize_pseudo_content(lycon, block, true,
            block->tag() != MARKUP_NAME_BUTTON);
        if (block->tag() == MARKUP_NAME_BUTTON) {
            // button layout consumes display:list-item as flow-root behavior;
            // its anonymous button content must not acquire a list marker line.
            block->display.list_item = false;
        }
    }
    // Buttons are form controls for intrinsic sizing but their CSS flow inner
    // box still owns authored children; treating them as replaced drops that subtree.
    bool is_replaced_element = block->display.inner == RDT_DISPLAY_REPLACED ||
        (block->tag() != MARKUP_NAME_BUTTON &&
         layout_element_is_replaced(block->as_element()));
    if (is_replaced_element) {  // image, iframe, hr, form controls, SVG
        NameId elmt_name = block->tag();
        bool canvas_width_is_contained = elmt_name == MARKUP_NAME_CANVAS &&
            layout_block_has_size_containment_in_axis(block, true) &&
            block_axis_has_automatic_css_size(block, true) &&
            block->block()->given_width >= 0.0f;
        bool canvas_height_is_contained = elmt_name == MARKUP_NAME_CANVAS &&
            layout_block_has_size_containment_in_axis(block, false) &&
            block_axis_has_automatic_css_size(block, false) &&
            block->block()->given_height >= 0.0f;
        if (elmt_name == MARKUP_NAME_CANVAS &&
            (canvas_width_is_contained || canvas_height_is_contained)) {
            float natural_width = 0.0f;
            float natural_height = 0.0f;
            layout_canvas_natural_size(block, &natural_width, &natural_height);
            bool width_is_auto = layout_css_size_is_automatic(block, true);
            bool width_is_contained = canvas_width_is_contained;
            bool height_is_contained = canvas_height_is_contained;
            float used_width = natural_width;
            float used_height = natural_height;
            if (width_is_contained && !height_is_contained) {
                used_width = lycon->block.given_width >= 0.0f
                    ? lycon->block.given_width : 0.0f;
                used_height = natural_height;
            } else if (height_is_contained && !width_is_contained) {
                used_width = !width_is_auto && lycon->block.given_width >= 0.0f
                    ? lycon->block.given_width : natural_width;
                used_height = lycon->block.given_height >= 0.0f
                    ? lycon->block.given_height : 0.0f;
            } else {
                if (lycon->block.given_width >= 0.0f) {
                    used_width = lycon->block.given_width;
                }
                if (lycon->block.given_height >= 0.0f) {
                    used_height = lycon->block.given_height;
                }
            }
            block->content_width = max(used_width, 0.0f);
            block->content_height = max(used_height, 0.0f);
            block->width = layout_border_size_from_content_box(block, block->content_width, true);
            block->height = layout_border_size_from_content_box(block, block->content_height, false);
        }
        else if (elmt_name == MARKUP_NAME_IFRAME) {
            layout_iframe(lycon, block, block->display);
        }
        else if (elmt_name == MARKUP_NAME_WEBVIEW) {
            if (!block->embed) {
                block->ensure_embed(lycon);
            }
            if (!block->embedp()->webview) {
                WebViewProp* wv = (WebViewProp*)alloc_prop(lycon, sizeof(WebViewProp));
                // webview attributes are borrowed from the DOM pool and must stay registered across retained view-pool resets.
                radiant_retain_webview_src(wv, lam::PoolPtr<const char>(block->get_attribute("src")));
                radiant_retain_webview_srcdoc(wv, lam::PoolPtr<const char>(block->get_attribute("srcdoc")));
                const char* mode_attr = block->get_attribute("mode");
                wv->mode = (mode_attr && strcmp(mode_attr, "layer") == 0)
                    ? WEBVIEW_MODE_LAYER : WEBVIEW_MODE_WINDOW;
                wv->needs_create = true;
                block->embed->webview = wv;
                log_info("%s webview layout: src=%s mode=%s size=%.0fx%.0f",
                         block->source_loc(),
                         wv->src ? wv->src : "(srcdoc)",
                         wv->mode == WEBVIEW_MODE_WINDOW ? "window" : "layer",
                         block->width, block->height);
            }
        }
        else if (elmt_name == MARKUP_NAME_SVG) {
            layout_inline_svg(lycon, block);
        }
        else if (elmt_name == MARKUP_NAME_HR) {
            // hr element: Use explicit height if specified, otherwise use border height
            BoxMetrics hr_box = layout_box_metrics(block);
            if (lycon->block.given_height >= 0) {
                float content_height = lycon->block.given_height;
                block->height = content_height + hr_box.pad_border_v;
            } else {
                block->height = hr_box.border_v;
            }
        }
        else if (block->form_control() &&
                 elmt_name != MARKUP_NAME_BUTTON) {
            layout_form_control(lycon, block);
        }
    } else if (block->form_control() &&
               block->tag() != MARKUP_NAME_BUTTON) {
        layout_form_control(lycon, block);
    } else {  // layout block child content
        DomNode *child = nullptr;
        if (block->is_element()) {
            child = layout_render_child_list(block->as_element());
        }
        if (child) {
            // CSS 2.1 §17.2.1: Orphaned table-internal elements (table-row, table-cell, etc.)
            bool is_orphaned_table_internal =
                is_table_internal_display(block->display.inner);
            // CSS 2.1 §17.2.1: Before flow layout, check if any children are orphaned
            if ((block->display.inner == CSS_VALUE_FLOW || block->display.inner == CSS_VALUE_FLOW_ROOT) && !is_orphaned_table_internal) {
                DomElement* block_elem = block->as_element();
                if (block_elem && wrap_orphaned_table_children(lycon, block_elem)) {
                    // fixup changes the DOM child chain; the retained view chain still describes the pre-fixup tree.
                    child = block_elem->first_child;
                }
            }
            if (block->display.inner == CSS_VALUE_FLOW || block->display.inner == CSS_VALUE_FLOW_ROOT || is_orphaned_table_internal) {
                bool is_multicol = is_multicol_container(block);
                if (is_multicol) {
                    layout_multicol_content(lycon, block);
                    block_context_refresh_descendant_float_geometry(
                        block_context_find_bfc(&lycon->block),
                        lam::view_require_element(block));
                    finalize_block_flow(lycon, block, block->display.outer);
                    return;
                } else {
                    DomElement* rendered_legend = find_fieldset_rendered_legend(block);
                    if (rendered_legend) {
                        fieldset_initialize_contents_ancestors(
                            lycon, block->as_element(),
                            static_cast<DomNode*>(rendered_legend));
                        child = static_cast<DomNode*>(rendered_legend);
                    }
                    prescan_and_layout_floats(lycon, child, block);
                    // CSS 2.1 §16.1: text-indent is "with respect to the left (or right)
                    if (!lycon->block.is_first_line && lycon->block.text_indent != 0 &&
                        lycon->block.direction != CSS_VALUE_RTL) {
                        BlockContext* bfc = block_context_find_bfc(&lycon->block);
                        if (bfc && (bfc->left_float_count > 0 || bfc->right_float_count > 0)) {
                            float bfc_y = lycon->block.bfc_offset_y + lycon->block.advance_y;
                            float line_h = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;
                            FloatAvailableSpace space = block_context_space_at_y(bfc, bfc_y, line_h);
                            if (space.has_left_float) {
                                float float_left = space.left - lycon->block.bfc_offset_x;
                                float target = float_left + lycon->block.text_indent;
                                if (target > lycon->line.advance_x) {
                                    lycon->line.advance_x = target;
                                    lycon->line.effective_left = target;
                                    lycon->line.has_float_intrusion = true;
                                }
                            }
                        }
                    }
                    do {
                        float pre_advance_y = lycon->block.advance_y;
                        bool child_is_floated = false;
                        if (child->is_element()) {
                            DomElement* child_elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
                            CssEnum child_float = get_element_float_value(child_elem);
                            child_is_floated = child_float == CSS_VALUE_LEFT || child_float == CSS_VALUE_RIGHT;
                        }
                        // HTML button layout's anonymous content box does not
                        // create a line for indentation-only DOM whitespace.
                        bool suppress_button_whitespace = block->tag() == MARKUP_NAME_BUTTON &&
                            child->is_text() && !layout_text_node_has_content(child);
                        if (suppress_button_whitespace) {
                            child->layout_height_contribution = 0.0f;
                        } else if (lycon->doc && lycon->doc->incremental_layout
                            && child->is_element() && !child->layout_dirty
                            && !child_is_floated
                            && child->height > 0 && child->view_type != RDT_VIEW_NONE) {
                            DomElement* skip_elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
                            verify_incremental_layout_skip(lycon, child, pre_advance_y);
                            skip_elem->y = lycon->block.advance_y;
                            lycon->block.advance_y += skip_elem->layout_height_contribution;
                            if (skip_elem->bound) {
                                lycon->block.max_width = max(lycon->block.max_width,
                                    lycon->line.left + skip_elem->width
                                    + skip_elem->boundary()->margin.left + skip_elem->boundary()->margin.right);
                            } else {
                                lycon->block.max_width = max(lycon->block.max_width,
                                    lycon->line.left + skip_elem->width);
                            }
                            log_info("[TIMING] Phase 16: skip unchanged subtree %s (h=%.1f, contrib=%.1f)",
                                skip_elem->source_loc(), skip_elem->height, skip_elem->layout_height_contribution);
                        } else {
                            layout_flow_node(lycon, child);
                        }
                        child->layout_height_contribution = lycon->block.advance_y - pre_advance_y;
                        child = rendered_legend
                            ? fieldset_next_flow_child(block, child, rendered_legend)
                            : child->next_sibling;
                    } while (child);
                    if (!lycon->line.is_line_start) {
                        lycon->line.is_last_line = true;
                        line_break(lycon);
                    } else {
                        align_and_discard_phantom_inline_line(lycon);
                    }
                }
            }
            else if (block->display.inner == CSS_VALUE_FLEX) {
                auto t_flex_start = high_resolution_clock::now();
                DomElement* rendered_legend = find_fieldset_rendered_legend(block);
                if (rendered_legend) {
                    fieldset_initialize_contents_ancestors(
                        lycon, block->as_element(),
                        static_cast<DomNode*>(rendered_legend));
                    // collection must not consume or reposition this box.
                    LayoutContextScope context_scope(lycon);
                    LayoutViewScope view_scope(lycon);
                    layout_flow_node(lycon, static_cast<DomNode*>(rendered_legend));
                    if (ViewBlock* legend_view = lam::view_as_block(static_cast<View*>(rendered_legend))) {
                        legend_view->width = max(
                            legend_view->width,
                            layout_compute_in_flow_child_width_extent(legend_view, true));
                    }
                }
                layout_flex_content(lycon, block);
                g_flex_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_flex_start).count();
                bool vertical_fieldset = layout_block_inline_axis_is_vertical(block);
                bool vertical_rl_fieldset = layout_block_writing_mode(block) == WM_VERTICAL_RL;
                if (rendered_legend) {
                    ViewBlock* legend_view = lam::view_as_block(static_cast<View*>(rendered_legend));
                    if (legend_view) {
                        if (vertical_fieldset) {
                            BoxMetrics fieldset_box = layout_box_metrics(block);
                            float legend_content_start = legend_view->x;
                            float legend_content_width = fieldset_legend_content_width(
                                legend_view, &legend_content_start);
                            if (legend_content_width > 0.0f &&
                                (!block->blk || block->block()->given_width < 0.0f)) {
                                float block_start_border = vertical_rl_fieldset
                                    ? fieldset_box.border.right
                                    : fieldset_box.border.left;
                                float legend_contribution = max(
                                    legend_content_width - block_start_border,
                                    0.0f);
                                block->width += legend_contribution;
                                float baseline_delta = legend_contribution;
                                shift_vertical_fieldset_baselines(
                                    block, baseline_delta);
                                block->content_width = max(
                                    block->width - fieldset_box.pad_border_h, 0.0f);
                                legend_view->width = legend_content_width;
                                legend_view->x = vertical_rl_fieldset
                                    ? block->width - legend_view->width : 0.0f;
                                legend_view->y = fieldset_box.border.top +
                                    fieldset_box.padding.top;
                                for (View* child = legend_view->first_child;
                                     child; child = child->next()) {
                                    if (!child->view_type) continue;
                                    child->x -= legend_content_start;
                                    child->y += max(
                                        (legend_view->height - child->height) / 2.0f,
                                        0.0f);
                                }
                                if (!vertical_rl_fieldset) {
                                    shift_fieldset_vertical_content_x(
                                        block, rendered_legend, legend_contribution, true);
                                }
                            }
                            float content_height = max(
                                block->height - fieldset_box.pad_border_v,
                                legend_view->height);
                            block->height = content_height + fieldset_box.pad_border_v;
                            block->content_height = content_height;
                        } else {
                        BoxMetrics fieldset_box = layout_box_metrics(block);
                        if (!block->blk || block->block()->given_width < 0.0f) {
                            float content_width = max(
                                block->width - fieldset_box.pad_border_h,
                                legend_view->width);
                            block->width = content_width + fieldset_box.pad_border_h;
                            block->content_width = max(block->content_width, content_width);
                        }
                        float border_top = fieldset_box.border.top;
                        float legend_reserve = max(legend_view->height - border_top, 0.0f);
                        for (DomNode* child = block->first_child; child; child = child->next_sibling) {
                            if (child == static_cast<DomNode*>(rendered_legend)) continue;
                            if (child->is_element() && child->as_element()->view_type) {
                                child->as_element()->y += legend_reserve;
                            } else if (child->is_text() && child->view_type) {
                                shift_text_geometry(static_cast<View*>(child), legend_reserve,
                                    TEXT_RECT_SHIFT_ALL);
                            }
                        }
                        block->height += legend_view->height;
                        block->content_height += legend_view->height;
                        }
                    }
                }
                update_multipass_advance_y(lycon, block);
                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_GRID) {
                auto t_grid_start = high_resolution_clock::now();
                layout_grid_content(lycon, block);
                g_grid_layout_time += duration<double, std::milli>(high_resolution_clock::now() - t_grid_start).count();
                update_multipass_advance_y(lycon, block);
                // CSS Grid §12.1: For inline-grid with auto width, compute
                update_inline_multipass_width(lycon, block, false);
                finalize_block_flow(lycon, block, block->display.outer);
                return;
            }
            else if (block->display.inner == CSS_VALUE_TABLE) {
                layout_table_block_content(lycon, block, false);
                return;
            }
        } else {
            if (block->display.inner == CSS_VALUE_FLEX) {
                layout_empty_flex_or_grid(lycon, block, false);
                return;
            }
            else if (block->display.inner == CSS_VALUE_GRID) {
                layout_empty_flex_or_grid(lycon, block, true);
                return;
            }
            else if (block->display.inner == CSS_VALUE_TABLE) {
                layout_table_block_content(lycon, block, true);
                return;
            }
        }
        if (!lycon->line.is_line_start) {
            lycon->line.is_last_line = true;
            line_break(lycon);
        } else {
            align_and_discard_phantom_inline_line(lycon);
        }
        // CSS Box 4 §3.1 margin-trim: block-end — trim the last in-flow child's
        if (block->blk && (block->block()->margin_trim & MARGIN_TRIM_BLOCK_END)) {
            View* last = block->last_placed_child();
            while (last && last->is_block()) {
                ViewBlock* lvb = lam::view_require_block(last);
                if ((lvb->position && element_has_float(lvb)) ||
                    layout_block_is_out_of_flow_positioned(lvb)) {
                    last = last->prev_placed_view();
                    continue;
                }
                break;
            }
            if (last && last->is_block()) {
                ViewBlock* last_block = lam::view_require_block(last);
                if (last_block->bound) {
                    bool is_sc = layout_block_is_self_collapsing(last_block);
                    if (is_sc) {
                        // CSS Box 4 §3.1: When the last in-flow child is self-collapsing,
                        View* prev = last_block;
                        while (prev) {
                            ViewBlock* vb = lam::view_require_block(prev);
                            if (vb->bound) {
                                vb->boundary_mut()->margin.bottom = 0;
                                vb->bound->margin_chain_positive = 0;
                                vb->bound->margin_chain_negative = 0;
                            }
                            View* p = prev->prev_placed_view();
                            while (p && p->is_block()) {
                                ViewBlock* pb = lam::view_require_block(p);
                                if ((pb->position && element_has_float(pb)) ||
                                    layout_block_is_out_of_flow_positioned(pb)) {
                                    p = p->prev_placed_view();
                                    continue;
                                }
                                break;
                            }
                            if (p && p->is_block() && layout_block_is_self_collapsing(lam::view_require_block(p))) {
                                prev = p;
                                continue;
                            }
                            if (p && p->is_block()) {
                                ViewBlock* anchor = lam::view_require_block(p);
                                if (anchor->bound) {
                                    anchor->boundary_mut()->margin.bottom = 0;
                                    anchor->bound->margin_chain_positive = 0;
                                    anchor->bound->margin_chain_negative = 0;
                                }
                                lycon->block.advance_y = anchor->y + anchor->height;
                            } else {
                                lycon->block.advance_y = 0;
                            }
                            break;
                        }
                    } else {
                        float trimmed = last_block->boundary()->margin.bottom;
                        if (trimmed != 0) {
                            lycon->block.advance_y -= trimmed;
                            last_block->boundary_mut()->margin.bottom = 0;
                        }
                        last_block->bound->margin_chain_positive = 0;
                        last_block->bound->margin_chain_negative = 0;
                    }
                }
            }
        }
        finalize_block_flow(lycon, block, block->display.outer);
    }
}

void layout_publish_vertical_flow_geometry(LayoutContext* lycon, ViewBlock* block,
                                           float flow_height) {
    if (!lycon || !block || !block->first_child ||
        !layout_block_inline_axis_is_vertical(block)) {
        return;
    }

    BoxMetrics block_box = layout_box_metrics(block);
    WritingMode writing_mode = layout_block_writing_mode(block);
    float surrogate_inline_origin = block_box.border.left + block_box.padding.left;
    float physical_inline_origin = block_box.border.top + block_box.padding.top;
    float physical_block_origin = writing_mode == WM_VERTICAL_RL
        ? block_box.border.right + block_box.padding.right
        : block_box.border.left + block_box.padding.left;
    CssEnum text_orientation = layout_specified_keyword(
        block->as_element(), CSS_PROPERTY_TEXT_ORIENTATION, CSS_VALUE_MIXED);
    bool reverse_vertical_inline = block->is_element() &&
        ((layout_element_css_writing_mode(block->as_element()) ==
            CSS_VALUE_SIDEWAYS_LR && block->blk &&
            (block->block()->direction == CSS_VALUE_LTR ||
             text_orientation == CSS_VALUE_UPRIGHT)) ||
         layout_element_css_writing_mode(block->as_element()) ==
            CSS_VALUE_SIDEWAYS_RL);
    float physical_inline_extent = block->height > 0.0f
        ? layout_content_size_from_border_box(block, block->height, false)
        : max(flow_height - block_box.pad_border_v, 0.0f);
    layout_map_vertical_writing_text_geometry(
        static_cast<View*>(block->first_child), writing_mode, block->width,
        physical_inline_extent,
        lycon->block.line_height,
        lycon->line.has_clamped_baseline_tail
            ? lycon->line.clamped_baseline_tail : 0.0f,
        surrogate_inline_origin,
        physical_inline_origin,
        physical_inline_origin, physical_block_origin, false,
        block->block()->dominant_baseline == CSS_VALUE_AUTO ||
        block->block()->dominant_baseline == CSS_VALUE_CENTRAL,
        reverse_vertical_inline);
}

static bool layout_block_has_multicol_ancestor(ViewBlock* block) {
    for (ViewElement* ancestor = block; ancestor; ancestor = ancestor->parent_view()) {
        if (!ancestor->is_block()) continue;
        ViewBlock* ancestor_block = lam::view_as_block(ancestor);
        if (ancestor_block && is_multicol_container(ancestor_block)) return true;
    }
    return false;
}

void setup_inline(LayoutContext* lycon, ViewBlock* block) {
    float content_width = lycon->block.content_width;
    float line_content_width = content_width;
    CssEnum text_orientation = layout_specified_keyword(
        block->as_element(), CSS_PROPERTY_TEXT_ORIENTATION, CSS_VALUE_MIXED);
    bool upright_vertical_text = layout_block_inline_axis_is_vertical(block) &&
        text_orientation == CSS_VALUE_UPRIGHT;
    bool rtl_vertical_block = block->blk &&
        block->block()->direction == CSS_VALUE_RTL;
    if ((rtl_vertical_block || layout_block_has_multicol_ancestor(block)) &&
        layout_block_inline_axis_is_vertical(block) &&
        block->height > 0.0f) {
        // CSS Writing Modes maps a vertical multicol inline axis to physical y;
        // the physical block width lets an exact-fit line overrun its extent.
        line_content_width = layout_content_size_from_border_box(
            block, block->height, false);
    }
    if (block->blk && block->block()->vertical_auto_inline_size_constrained &&
        lycon->block.parent && lycon->block.parent->content_height >= 0.0f) {
        line_content_width = layout_stretch_fit_used_css_size(
            block, lycon->block.parent->content_height, false);
    }
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    // CSS 2.1 §16.1: text-indent applies only to the first formatted line of a block container
    lycon->block.is_first_line = true;
    lycon->block.first_line_font = nullptr;
    lycon->block.first_line_style_active = false;
    lycon->block.initial_letter_exclusion_width = 0.0f;
    lycon->block.initial_letter_exclusion_right = 0.0f;
    lycon->block.initial_letter_exclusion_lines = 0;
    lycon->block.initial_letter_exclusion_bottom = 0.0f;
    lycon->block.initial_letter_margin_box_left = 0.0f;
    lycon->block.initial_letter_margin_box_right = 0.0f;
    lycon->block.initial_letter_margin_box_top = 0.0f;
    lycon->block.initial_letter_margin_box_bottom = 0.0f;
    lycon->block.initial_letter_border_box_bottom = 0.0f;
    lycon->block.initial_letter_origin_line_number = -1;
    lycon->block.initial_letter_clears_later_start_floats = false;
    lycon->block.initial_letter_exclusion_requires_intersection = false;
    lycon->block.initial_letter_origin_offset_applied = false;
    lycon->block.initial_letter_continuation_cleared = false;
    lycon->block.initial_letter_trimmed_start_candidate = 0.0f;
    lycon->block.initial_letter_trimmed_start_contribution = 0.0f;
    lycon->block.line_number = 0;
    lycon->block.line_clamp = (block->blk && block->block_mut()->line_clamp > 0) ? block->block_mut()->line_clamp : 0;
    lycon->block.line_clamped = false;
    lycon->block.line_clamp_advance_y = -1.0f;
    lycon->block.line_clamp_last_line_ascender = 0.0f;
    lycon->block.line_clamp_last_line_max_ascender = 0.0f;
    lycon->block.line_clamp_last_line_max_descender = 0.0f;
    CssEnum inherited_text_wrap_style = lycon->block.parent ?
        lycon->block.parent->text_wrap_style : CSS_VALUE_AUTO;
    if (!inherited_text_wrap_style) inherited_text_wrap_style = CSS_VALUE_AUTO;
    lycon->block.text_wrap_style = (block->blk && block->block_mut()->text_wrap_style) ?
        block->block()->text_wrap_style : inherited_text_wrap_style;
    lycon->block.balance_wrap_active = false;
    lycon->block.balance_wrap_width = 0.0f;
    if (block->blk) {
        block->blk->line_clamp_inherited = false;
        block->blk->line_clamped = false;
        block->blk->line_clamp_advance_y = -1.0f;
        block->blk->line_clamp_last_line_ascender = 0.0f;
        block->blk->line_clamp_last_line_max_ascender = 0.0f;
        block->blk->line_clamp_last_line_max_descender = 0.0f;
    }
    if (lycon->block.parent && lycon->block.parent->line_clamp > 0 &&
        !lycon->block.parent->line_clamped && lycon->block.line_clamp == 0 && block->blk) {
        lycon->block.line_clamp = lycon->block.parent->line_clamp;
        lycon->block.line_number = lycon->block.parent->line_number;
        block->blk->line_clamp_inherited = true;
    }
    float resolved_text_indent = 0.0f;
    if (block->blk) {
        if (block->block()->text_indent_calc) {
            // CSS Text 3: text-indent percentage resolves against the block's own content width
            float saved_parent_width = 0;
            bool has_parent = lycon->block.parent != nullptr;
            if (has_parent) {
                saved_parent_width = lycon->block.parent->content_width;
                lycon->block.parent->content_width = content_width;
            }
            resolved_text_indent = resolve_length_value(lycon, CSS_PROPERTY_TEXT_INDENT, block->block()->text_indent_calc);
            if (has_parent) {
                lycon->block.parent->content_width = saved_parent_width;
            }
        } else if (!isnan(block->block()->text_indent_percent)) {
            resolved_text_indent = content_width * block->block()->text_indent_percent / 100.0f;
        } else if (block->block()->text_indent != 0.0f) {
            resolved_text_indent = block->block()->text_indent;
        }
    }
    lycon->block.text_indent = resolved_text_indent;
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc) {
        BlockContextOffset bfc_offset = block_context_offset_to_bfc(
            lam::view_require_element(block), bfc);
        lycon->block.bfc_offset_x = bfc_offset.x;
        lycon->block.bfc_offset_y = bfc_offset.y;
        // CSS 2.1 §8.3.1: When this block's margin-top will collapse with its parent's
        if (block->bound && block->boundary_mut()->margin.top > 0 && (bfc->left_float_count > 0 || bfc->right_float_count > 0)) {
            ViewElement* parent = block->parent_view();
            if (parent && parent->parent_view() && parent->is_block()) {
                ViewBlock* pa = lam::view_require_block(parent);
                bool pa_creates_bfc = block_context_establishes_bfc(pa);
                float pa_decoration_top = layout_axis_decoration_start(
                    pa->bound ? pa->boundary() : nullptr, LAYOUT_AXIS_Y);
                bool is_first_inflow = (block->y == block->boundary()->margin.top);
                if (!pa_creates_bfc && pa_decoration_top == 0 && is_first_inflow) {
                    bool has_float_children = false;
                    for (DomNode* ch = block->first_child; ch; ch = ch->next_sibling) {
                        if (ch->is_element()) {
                            CssEnum fv = get_element_float_value(ch->as_element());
                            if (fv == CSS_VALUE_LEFT || fv == CSS_VALUE_RIGHT) {
                                has_float_children = true;
                                break;
                            }
                        }
                    }
                    if (!has_float_children) {
                        lycon->block.bfc_offset_y -= block->boundary()->margin.top;
                    }
                }
            }
        }
    } else {
        lycon->block.bfc_offset_x = 0;
        lycon->block.bfc_offset_y = 0;
    }
    float inner_left = layout_axis_decoration_start(
        block->bound ? block->boundary() : nullptr, LAYOUT_AXIS_X);
    lycon->block.advance_y += layout_axis_decoration_start(
        block->bound ? block->boundary() : nullptr, LAYOUT_AXIS_Y);
    float inner_right = inner_left + line_content_width;
    lycon->line.left = inner_left;
    lycon->line.right = inner_right;
    lycon->line.align_left = inner_left;
    lycon->line.align_right = inner_right;
    lycon->line.effective_left = inner_left;
    lycon->line.effective_right = inner_right;
    lycon->line.has_float_intrusion = false;
    lycon->line.advance_x = inner_left;
    lycon->line.inline_start_edge_pending = 0;
    if (block->blk) lycon->block.text_align = block->block()->text_align;
    if (block->blk) lycon->block.text_align_last = block->block()->text_align_last;
    // CSS 2.1 §9.2.1: Propagate direction to block context
    if (block->blk) {
        lycon->block.direction = block->block()->direction;
        if (upright_vertical_text) {
            // css writing modes 4 §6.4: upright vertical text uses an LTR
            // inline direction even when the authored direction is RTL.
            lycon->block.direction = CSS_VALUE_LTR;
        }
        bool has_outside_marker = block->display.list_item && block->pseudo &&
            block->pseudo->marker_generated && block->pseudo->marker &&
            block->pseudo->marker->blk &&
            reinterpret_cast<MarkerProp*>(block->pseudo->marker->blk)->is_outside;
        if (block->block()->unicode_bidi == CSS_VALUE_PLAINTEXT &&
            !has_outside_marker) {
            // CSS Writing Modes §2.2: plaintext derives the paragraph base
            // direction from the first strong character in the content.
            lycon->block.direction = layout_resolve_plaintext_direction(
                lam::dom_require<DOM_NODE_ELEMENT>(block), lycon->block.direction);
        }
    }
    lycon->line.vertical_align = CSS_VALUE_BASELINE;

    line_reset(lycon);
    if (block->font) {
        block->font->used_zoom = layout_effective_zoom((View*)block);
        setup_font(lycon->ui_context, &lycon->font, block->font);
    }
    // CSS Text 3 §4.2: save the block container's font for tab-size calculation.
    lycon->block.block_container_font = lycon->font.style;
    // CSS 2.1 §10.8.1: Update line_start_font to the block's own font, since
    lycon->line.line_start_font = lycon->font;
    setup_line_height(lycon, block);
    // CSS 2.1 §10.8.1: The strut is a zero-width inline box with the block's font.
    if (font_box_handle(&lycon->font)) {
        if (lycon->block.line_height_is_normal) {
            float split_asc = 0, split_desc = 0;
            font_get_normal_lh_split(font_box_handle(&lycon->font), &split_asc, &split_desc);
            lycon->block.init_ascender = split_asc;
            lycon->block.init_descender = split_desc;
        } else {
            font_get_content_area_split(font_box_handle(&lycon->font),
                                        &lycon->block.init_ascender,
                                        &lycon->block.init_descender);
        }
    }
    if (block->is_element() && lycon->font.style) {
        DomElement* block_element = lam::dom_require<DOM_NODE_ELEMENT>(block);
        lycon->block.first_line_font = layout_resolve_first_line_font(
            lycon, block_element, lycon->font.style);
        lycon->block.first_line_style_active =
            lycon->block.first_line_font != nullptr;
    }
    float balance_width = text_wrap_balance_measure(lycon, block, line_content_width);
    if (balance_width > 0.0f) {
        lycon->block.balance_wrap_active = true;
        lycon->block.balance_wrap_width = balance_width;
        lycon->line.left = inner_left;
        lycon->line.right = inner_left + balance_width;
        lycon->line.align_left = inner_left;
        lycon->line.align_right = inner_right;
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.advance_x = lycon->line.left;
        lycon->line.has_float_intrusion = false;
        lycon->line.inline_start_edge_pending = 0;
        lycon->block.is_first_line = true;
        line_reset(lycon);
    }
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height - (lycon->block.init_ascender + lycon->block.init_descender)) / 2);
}

LayoutTextRectContentKind layout_text_rect_content_kind(ViewText* text,
                                                        TextRect* rect) {
    if (!text || !rect || rect->length <= 0) {
        return LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE;
    }
    const unsigned char* data = text->text_data();
    if (!data) return LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE;
    const char* cursor = reinterpret_cast<const char*>(data + rect->start_index);
    size_t remaining = static_cast<size_t>(rect->length);
    bool has_non_collapsible_space = false;
    while (remaining > 0) {
        uint32_t codepoint = 0;
        int bytes = str_utf8_decode(cursor, remaining, &codepoint);
        if (bytes <= 0 || static_cast<size_t>(bytes) > remaining) {
            return LAYOUT_TEXT_RECT_PAINTED_CONTENT;
        }
        utf8proc_category_t category = utf8proc_category(
            static_cast<utf8proc_int32_t>(codepoint));
        bool is_non_collapsible_space = codepoint == 0x00A0 ||
            codepoint == 0x2007 || codepoint == 0x202F;
        if (is_non_collapsible_space) has_non_collapsible_space = true;
        bool is_line_whitespace = !is_non_collapsible_space &&
            (codepoint == ' ' || codepoint == '\t' ||
            codepoint == '\n' || codepoint == '\r' || codepoint == '\f' ||
            codepoint == 0x2028 || codepoint == 0x2029 ||
            category == UTF8PROC_CATEGORY_ZS);
        if (!is_line_whitespace && !is_non_collapsible_space) {
            return LAYOUT_TEXT_RECT_PAINTED_CONTENT;
        }
        cursor += bytes;
        remaining -= static_cast<size_t>(bytes);
    }
    return has_non_collapsible_space
        ? LAYOUT_TEXT_RECT_NON_COLLAPSIBLE_WHITESPACE
        : LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE;
}

bool layout_text_rect_has_painted_codepoint(ViewText* text, TextRect* rect) {
    return layout_text_rect_content_kind(text, rect) ==
        LAYOUT_TEXT_RECT_PAINTED_CONTENT;
}

static bool layout_vertical_whitespace_inline_anchor(ViewText* text,
                                                     TextRect* rect,
                                                     float* inline_offset) {
    if (!text || !rect || !inline_offset ||
        layout_text_rect_has_painted_codepoint(text, rect)) return false;
    DomNode* next = static_cast<DomNode*>(text)->next_sibling;
    next = layout_first_view_with_type(next);
    if (!next || (next->view_type != RDT_VIEW_INLINE_BLOCK &&
                  next->view_type != RDT_VIEW_TABLE)) return false;
    ViewBlock* next_block = lam::view_require_block(static_cast<View*>(next));
    float gap = rect->width;
    if (rect->height < gap) gap = rect->height;
    *inline_offset = max(next_block->y - gap, 0.0f);
    return true;
}

static bool layout_vertical_subtree_has_isolated_inline(View* view) {
    while (view) {
        if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
            if (layout_inline_span_isolate(span)) {
                return true;
            }
            if (span->first_child &&
                layout_vertical_subtree_has_isolated_inline(span->first_child)) {
                return true;
            }
        }
        view = view->next();
    }
    return false;
}

void layout_map_vertical_writing_text_geometry(View* view, WritingMode mode,
                                               float block_extent,
                                               float inline_extent,
                                               float line_height,
                                               float clamped_baseline_tail,
                                               float surrogate_inline_origin,
                                               float physical_inline_origin,
                                               float surrogate_block_origin,
                                               float physical_block_origin,
                                               bool center_block_axis,
                                               bool use_central_baseline,
                                               bool reverse_inline_axis) {
    while (view) {
        if (view->view_type == RDT_VIEW_MARKER) {
            float logical_inline_offset = view->x - surrogate_inline_origin;
            float logical_y = view->y - surrogate_block_origin + physical_block_origin;
            float logical_width = view->width;
            float logical_height = view->height;
            view->x = mode == WM_VERTICAL_RL
                ? block_extent - logical_y - logical_height : logical_y;
            view->y = physical_inline_origin + logical_inline_offset;
            view->width = logical_height;
            view->height = logical_width;
        }
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                float logical_x = rect->x - surrogate_inline_origin + physical_inline_origin;
                float logical_inline_offset = rect->x - surrogate_inline_origin;
                float logical_width = rect->width;
                float logical_height = rect->height;
                LayoutTextRectContentKind content_kind =
                    layout_text_rect_content_kind(text, rect);
                bool has_painted_codepoint = content_kind ==
                    LAYOUT_TEXT_RECT_PAINTED_CONTENT;
                bool has_non_collapsible_whitespace = content_kind ==
                    LAYOUT_TEXT_RECT_NON_COLLAPSIBLE_WHITESPACE;
                bool has_collapsed_whitespace = content_kind ==
                    LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE;
                float whitespace_inline_offset = 0.0f;
                bool has_whitespace_inline_anchor =
                    layout_vertical_whitespace_inline_anchor(
                        text, rect, &whitespace_inline_offset);
                if (has_whitespace_inline_anchor) {
                    logical_x = whitespace_inline_offset;
                }
                // CSS Writing Modes centers the glyph area in the vertical line
                ViewBlock* text_block = layout_nearest_block_ancestor(
                    text->parent_view());
                bool line_has_isolated_inline = text_block &&
                    layout_vertical_subtree_has_isolated_inline(
                        text_block->first_child);
                bool line_axis_already_trimmed = text_block && text_block->blk &&
                    text_block->block()->text_box_trim_applied;
                float inline_leading = !line_axis_already_trimmed &&
                    has_painted_codepoint &&
                    line_height > logical_height
                    ? (line_height - logical_height) / 2.0f : 0.0f;
                bool atomic_vertical_container = text_block &&
                    text_block->view_type == RDT_VIEW_INLINE_BLOCK;
                ViewBlock* text_parent_block = atomic_vertical_container
                    ? layout_nearest_block_ancestor(text_block->parent_view()) : nullptr;
                bool mixed_vertical_baseline = atomic_vertical_container &&
                    text_parent_block &&
                    layout_block_inline_axis_is_vertical(text_parent_block) !=
                        layout_block_inline_axis_is_vertical(text_block);
                // CSS Writing Modes 4 §4.2: an isolated vertical inline uses
                // the central baseline; retain the orthogonal inline-block
                // case while extending it to marker-like isolated runs.
                bool vertical_central = use_central_baseline &&
                    !reverse_inline_axis &&
                    (mixed_vertical_baseline || line_has_isolated_inline) &&
                    (mode == WM_VERTICAL_LR || mode == WM_VERTICAL_RL);
                bool center_painted_vertical_glyph = vertical_central &&
                    has_painted_codepoint;
                bool center_vertical_space = has_non_collapsible_whitespace &&
                    text_block && layout_block_inline_axis_is_vertical(text_block) &&
                    line_height > logical_height;
                bool vertical_whitespace = text_block &&
                    layout_block_inline_axis_is_vertical(text_block) &&
                    has_collapsed_whitespace;
                float logical_y = vertical_whitespace
                    ? physical_block_origin
                    : center_block_axis || center_painted_vertical_glyph ||
                        center_vertical_space
                    ? (block_extent - logical_height) / 2.0f
                    : rect->y - surrogate_block_origin + physical_block_origin +
                        inline_leading;
                logical_y += clamped_baseline_tail;
                InitialLetterInfo initial_letter = {};
                bool is_initial_letter = layout_get_text_initial_letter_info(
                    static_cast<DomNode*>(text), &initial_letter);
                if (is_initial_letter && mode == WM_VERTICAL_LR &&
                    !reverse_inline_axis && !center_block_axis) {
                    // CSS Inline 3 §7.5 positions the initial against the
                    InitialLetterBoxInsets insets =
                        layout_initial_letter_box_insets(text);
                    logical_y = physical_block_origin + insets.top - line_height;
                }
                bool reverse_text_inline = reverse_inline_axis;
                ViewElement* text_parent = text->parent_view();
                if (reverse_inline_axis && text_parent && text_parent->is_element()) {
                    CssEnum text_writing_mode = layout_element_css_writing_mode(
                        text_parent->as_element());
                    reverse_text_inline = text_writing_mode == CSS_VALUE_SIDEWAYS_LR ||
                        text_writing_mode == CSS_VALUE_SIDEWAYS_RL;
                    if (text_writing_mode == CSS_VALUE_SIDEWAYS_RL && text_block &&
                        text_block->view_type != RDT_VIEW_INLINE_BLOCK) {
                        reverse_text_inline = false;
                    }
                }
                rect->x = mode == WM_VERTICAL_RL
                    ? block_extent - logical_y - logical_height : logical_y;
                rect->y = reverse_text_inline
                    ? physical_inline_origin + inline_extent -
                        logical_inline_offset - logical_width
                    : logical_x;
                rect->width = rect->height;
                rect->height = logical_width;
            }
            adjust_text_bounds(text);
        } else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
            float logical_x = span->x - surrogate_inline_origin + physical_inline_origin;
            float logical_inline_offset = span->x - surrogate_inline_origin;
            float logical_width = span->width;
            float logical_height = span->height;
            ViewBlock* span_block = layout_nearest_block_ancestor(
                span->parent_view());
            bool line_has_isolated_inline = span_block &&
                layout_vertical_subtree_has_isolated_inline(
                    span_block->first_child);
            // CSS Writing Modes 4 §4.2: an isolated vertical inline uses the
            // central baseline; retain existing inline-block baseline behavior
            // while extending it to marker-like isolated runs.
            bool vertical_central = use_central_baseline &&
                !reverse_inline_axis &&
                (line_has_isolated_inline ||
                 (span_block && span_block->view_type == RDT_VIEW_INLINE_BLOCK)) &&
                (mode == WM_VERTICAL_LR || mode == WM_VERTICAL_RL);
            float logical_y = vertical_central
                ? (block_extent - logical_height) / 2.0f
                : span->y - surrogate_block_origin + physical_block_origin;
            span->x = mode == WM_VERTICAL_RL
                ? block_extent - logical_y - logical_height : logical_y;
            bool reverse_span_inline = false;
            if (reverse_inline_axis && span->is_element()) {
                CssEnum span_writing_mode = layout_element_css_writing_mode(
                    span->as_element());
                reverse_span_inline = span_writing_mode == CSS_VALUE_SIDEWAYS_LR ||
                    span_writing_mode == CSS_VALUE_SIDEWAYS_RL;
                if (span_writing_mode == CSS_VALUE_SIDEWAYS_RL && span_block &&
                    span_block->view_type != RDT_VIEW_INLINE_BLOCK) {
                    reverse_span_inline = false;
                }
            }
            span->y = reverse_span_inline
                ? physical_inline_origin + inline_extent -
                    logical_inline_offset - logical_width
                : logical_x;
            span->width = logical_height;
            span->height = logical_width;
            if (span->first_child) {
                layout_map_vertical_writing_text_geometry(
                    span->first_child, mode, block_extent, inline_extent,
                    line_height, clamped_baseline_tail,
                    surrogate_inline_origin, physical_inline_origin,
                    surrogate_block_origin, physical_block_origin,
                    center_block_axis, use_central_baseline, reverse_inline_axis);
            }
            bool has_direct_positioned_child = false;
            ViewBlock* span_parent_block = layout_nearest_block_ancestor(span->parent_view());
            for (View* child = span->first_child; child; child = child->next()) {
                ViewBlock* child_block = lam::view_as_block(child);
                if (child_block && layout_block_is_out_of_flow_positioned(child_block)) {
                    has_direct_positioned_child = true;
                    break;
                }
            }
            // css Position 3 §4.1: an RTL vertical inline containing block's
            // static edge is reconstructed after relative positioning; fragmented
            // spans already carry that offset in their fragment geometry.
            if (span->position && span->positionp()->position == CSS_VALUE_RELATIVE &&
                has_direct_positioned_child &&
                !inline_span_has_multiple_line_fragments(span) &&
                span_parent_block && span_parent_block->blk &&
                span_parent_block->block()->direction == CSS_VALUE_RTL &&
                (mode == WM_VERTICAL_LR || mode == WM_VERTICAL_RL)) {
                float relative_y = 0.0f;
                layout_relative_position_offset(
                    lam::unsafe_view_block_api_span(span),
                    nullptr, &relative_y);
                // css Writing Modes finalizes the surrogate inline coordinate
                // here, so preserve the physical Y relative offset after mapping.
                if (relative_y != 0.0f) {
                    span->y += relative_y;
                    layout_shift_inline_descendants(
                        lam::view_require_element(span), 0.0f, relative_y);
                }
            }
        } else if (view->view_type == RDT_VIEW_BR) {
            float logical_x = view->x - surrogate_inline_origin + physical_inline_origin;
            float logical_inline_offset = view->x - surrogate_inline_origin;
            float logical_width = view->width;
            float logical_y = center_block_axis
                ? (block_extent - view->height) / 2.0f
                : view->y - surrogate_block_origin + physical_block_origin;
            view->x = mode == WM_VERTICAL_RL
                ? block_extent - logical_y - view->height : logical_y;
            ViewBlock* br_block = layout_nearest_block_ancestor(view->parent_view());
            bool reverse_br_inline = reverse_inline_axis;
            if (br_block && br_block->is_element() &&
                layout_element_css_writing_mode(br_block->as_element()) ==
                    CSS_VALUE_SIDEWAYS_RL &&
                br_block->view_type != RDT_VIEW_INLINE_BLOCK) {
                reverse_br_inline = false;
            }
            view->y = reverse_br_inline
                ? physical_inline_origin + inline_extent -
                    logical_inline_offset - logical_width
                : logical_x;
            view->width = view->height;
            view->height = logical_width;
        }
        view = view->next();
    }
}
// CSS 2.1 §9.4.2: Check if an inline subtree generates any line boxes.
static bool is_inline_substantial(ViewElement* ve) {
    if (ve->bound) {
        // CSS Inline 3 §2.1: An inline box is NOT invisible if ANY individual
        BoxMetrics metrics = layout_boundary_metrics(ve->bound);
        if (metrics.margin.left != 0 || metrics.margin.right != 0 ||
            metrics.padding.left != 0 || metrics.padding.right != 0 ||
            metrics.border.left != 0 || metrics.border.right != 0) return true;
    }
    View* c = ve->first_placed_child();
    while (c) {
        if (c->view_type == RDT_VIEW_TEXT) return true;
        if (c->view_type == RDT_VIEW_INLINE) {
            if (is_inline_substantial(lam::view_require_element(c))) return true;
        } else if (c->is_block()) {
            ViewBlock* child_block = lam::view_require_block(c);
            // make their inline ancestor's otherwise phantom line box substantial.
            bool is_out_of_flow = (child_block->position && element_has_float(child_block)) ||
                layout_block_is_out_of_flow_positioned(child_block);
            if (!is_out_of_flow && !layout_block_is_self_collapsing(child_block)) return true;
        } else if (c->view_type) {
            return true;
        }
        View* next = static_cast<View*>(c->next_sibling);
        next = layout_first_view_with_type(next);
        c = next;
    }
    return false;
}
// CSS 2.1 §8.3.1: Check if an in-flow block can be considered self-collapsing
bool layout_block_is_self_collapsing(ViewBlock* vb) {
    if (vb->height > 0) return false;
    // Tables and table internals are never self-collapsing (CSS 2.1 §17)
    if (vb->view_type == RDT_VIEW_TABLE || vb->view_type == RDT_VIEW_TABLE_ROW ||
        vb->view_type == RDT_VIEW_TABLE_ROW_GROUP || vb->view_type == RDT_VIEW_TABLE_CELL) return false;
    // CSS 2.1 §8.3.1: Self-collapsing applies only to block-level boxes.
    if (vb->view_type == RDT_VIEW_INLINE_BLOCK) return false;
    BoxMetrics box = layout_box_metrics(vb);
    if (box.border.top > 0 || box.border.bottom > 0 ||
        box.padding.top > 0 || box.padding.bottom > 0) return false;
    // BFC roots and floats don't self-collapse (CSS 2.1 §8.3.1)
    bool creates_bfc = vb->scroller &&
        (vb->scroll()->overflow_x != CSS_VALUE_VISIBLE ||
         vb->scroll()->overflow_y != CSS_VALUE_VISIBLE);
    if (creates_bfc) return false;
    if (vb->position && element_has_float(vb)) return false;
    if (block_context_establishes_bfc(vb)) {
        // CSS 2.1 §8.3.1: a BFC child blocks margin collapse through its
        // parent, even when its own used block-size is zero.
        return false;
    }
    if (vb->display.inner == CSS_VALUE_FLOW_ROOT ||
        vb->display.inner == CSS_VALUE_FLEX ||
        vb->display.inner == CSS_VALUE_GRID) return false;
    View* child = layout_rendered_first_placed_child(vb);
    while (child) {
        if (child->is_block()) {
            ViewBlock* cvb = lam::view_require_block(child);
            bool is_out_of_flow = (cvb->position && element_has_float(cvb)) ||
                layout_block_is_out_of_flow_positioned(cvb);
            if (!is_out_of_flow && !layout_block_is_self_collapsing(cvb)) return false;
        } else {
            // CSS 2.1 §9.4.2: Line boxes that contain no text, no preserved
            // or borders, and no other in-flow content must be treated as not
            bool is_substantial = false;
            if (child->view_type == RDT_VIEW_INLINE) {
                if (is_inline_substantial(lam::view_require_element(child))) is_substantial = true;
            } else if (child->view_type == RDT_VIEW_TEXT) {
                is_substantial = true;
            } else {
                if (child->view_type == RDT_VIEW_MARKER) {
                    // CSS 2.2 §12.5 + §8.3.1: An outside marker with visible content
                    MarkerProp* mp = child->is_element() ? reinterpret_cast<MarkerProp*>(lam::dom_require<DOM_NODE_ELEMENT>(child)->blk) : nullptr;
                    is_substantial = (mp != nullptr);  // marker exists = has content
                } else {
                    is_substantial = true;
                }
            }
            if (is_substantial) return false;
        }
        View* next = static_cast<View*>(child->next_sibling);
        next = layout_first_view_with_type(next);
        child = next;
    }
    return true;
}

static int layout_block_content_count = 0;

static float layout_image_ratio_transfer(ViewBlock* block, float definite_size,
                                         float ratio, bool definite_horizontal,
                                         bool apply_constraints, bool use_content_box,
                                         bool css_ratio_uses_content_box) {
    if (ratio <= 0.0f) return 0.0f;
    bool border_box = layout_uses_border_box(block);
    if (apply_constraints) {
        definite_size = layout_apply_min_max_axis(block, definite_size,
                                                   definite_horizontal, border_box);
    }
    if (use_content_box && border_box) {
        definite_size = definite_horizontal
            ? layout_content_size_from_border_box(block, definite_size, true)
            : layout_content_size_from_border_box(block, definite_size, false);
    }
    float transferred = definite_horizontal ? definite_size / ratio : definite_size * ratio;
    if (apply_constraints && css_ratio_uses_content_box && border_box) {
        transferred = definite_horizontal
            ? layout_border_size_from_content_box(block, transferred, false)
            : layout_border_size_from_content_box(block, transferred, true);
    }
    return transferred;
}

static void layout_store_percentage_axis(LayoutContext* lycon, ViewBlock* block,
                                         float percent, bool horizontal) {
    if (!block) return;
    block->ensure_block(lycon);
    LayoutAxisRefs axis(block->block_mut(), horizontal);
    *axis.given_percent = percent;
    layout_store_given_axis(lycon, block, -1.0f, horizontal, false);
}

static void layout_clear_auto_axis_type(ViewBlock* block, bool horizontal) {
    if (!block || !block->blk) return;
    LayoutAxisRefs axis(block->block_mut(), horizontal);
    if (*axis.given_type == CSS_VALUE_AUTO) *axis.given_type = CSS_VALUE__UNDEF;
}

static bool view_is_floated_box(View* view) {
    if (!view || !view->is_element()) return false;
    ViewBlock* block = lam::view_as_block(view);
    return block && block->position && element_has_float(block);
}

static void shift_current_line_view_for_float(View* view, float offset, float line_y) {
    if (!view || view_is_floated_box(view)) return;
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require_text(view);
        bool shifted = false;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->y >= line_y - 1.0f) {
                rect->x += offset;
                shifted = true;
            }
        }
        if (shifted) {
            adjust_text_bounds(text);
        }
    } else if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
        if (span->y >= line_y - 1.0f) {
            span->x += offset;
        }
        for (View* child = span->first_child; child; child = child->next()) {
            shift_current_line_view_for_float(child, offset, line_y);
        }
    } else if (view->view_type == RDT_VIEW_INLINE_BLOCK ||
               view->view_type == RDT_VIEW_TABLE ||
               view->view_type == RDT_VIEW_MARKER) {
        if (view->y >= line_y - 1.0f) {
            view->x += offset;
        }
    }
}

static void shift_current_line_content_before_float(ViewBlock* float_block, float offset, float line_y) {
    DomNode* cursor = static_cast<DomNode*>(float_block);
    while (cursor) {
        DomNode* node = cursor->prev_sibling;
        while (node) {
            View* view = static_cast<View*>(node);
            if (view->view_type != RDT_VIEW_NONE) {
                shift_current_line_view_for_float(view, offset, line_y);
            }
            node = node->prev_sibling;
        }
        DomNode* parent = cursor->parent;
        if (!parent || !parent->is_element() || parent->view_type != RDT_VIEW_INLINE) break;
        cursor = parent;
    }
}

static float line_trimmable_end_space_width(Linebox* line) {
    if (!line) return 0.0f;
    float trimmable_width = 0.0f;
    if (line->trailing_space_width > 0.0f) {
        trimmable_width = line->trailing_space_width;
    } else if (line->committed_trailing_space > 0.0f) {
        trimmable_width = line->committed_trailing_space;
    }
    if (line->hanging_space_width > 0.0f) {
        trimmable_width += line->hanging_space_width;
    }
    return trimmable_width;
}

static float current_line_used_width_for_float(Linebox* line, float line_left) {
    if (!line) return 0.0f;
    // CSS 2.1 §16.6.1 and CSS Text §4.1.3: trailing collapsible or
    float used_width = line->advance_x - line_left - line_trimmable_end_space_width(line);
    return max(used_width, 0.0f);
}

static void adjust_current_line_after_same_line_float(BlockContext* pa_block, Linebox* pa_line,
                                                      BlockContext* bfc, ViewBlock* float_block) {
    if (!pa_block || !pa_line || !bfc || !float_block) return;
    if (pa_line->is_line_start || !pa_line->start_view) return;
    if (!float_block->position) return;
    CssEnum float_side = float_block->positionp()->float_prop;
    if (float_side != CSS_VALUE_LEFT && float_side != CSS_VALUE_RIGHT) return;
    float line_height = pa_block->line_height > 0 ? pa_block->line_height : 16.0f;
    float query_y = pa_block->bfc_offset_y + pa_block->advance_y;
    FloatAvailableSpace space = block_context_space_at_y(bfc, query_y, line_height);
    if (float_side == CSS_VALUE_LEFT && !space.has_left_float) return;
    if (float_side == CSS_VALUE_RIGHT && !space.has_right_float) return;
    float local_left = space.has_left_float
        ? space.left - pa_block->bfc_offset_x
        : pa_line->left;
    float local_right = space.has_right_float
        ? space.right - pa_block->bfc_offset_x
        : pa_line->right;
    float new_effective_left = max(local_left, pa_line->left);
    float new_effective_right = min(local_right, pa_line->right);
    if (float_side == CSS_VALUE_RIGHT) {
        if (new_effective_right >= pa_line->effective_right - 0.01f) return;
        pa_line->effective_left = new_effective_left;
        pa_line->effective_right = new_effective_right;
        pa_line->has_float_intrusion = true;
        return;
    }
    if (new_effective_left <= pa_line->effective_left + 0.01f) return;
    float old_effective_left = pa_line->has_float_intrusion ? pa_line->effective_left : pa_line->left;
    float current_line_width = current_line_used_width_for_float(pa_line, old_effective_left);
    float available_width = max(new_effective_right - new_effective_left, 0.0f);
    if (current_line_width > available_width + 0.5f) {
        return;
    }
    float offset = new_effective_left - old_effective_left;
    if (offset <= 0.01f) return;
    shift_current_line_content_before_float(float_block, offset, pa_block->advance_y);
    pa_line->advance_x += offset;
    pa_line->effective_left = new_effective_left;
    pa_line->effective_right = new_effective_right;
    pa_line->has_float_intrusion = true;
}

static bool same_line_float_needs_next_line(BlockContext* pa_block, Linebox* pa_line,
                                            ViewBlock* float_block) {
    if (!pa_block || !pa_line || !float_block || !float_block->position) return false;
    if (pa_line->is_line_start || !pa_line->start_view) return false;
    CssEnum float_side = float_block->positionp()->float_prop;
    if (float_side != CSS_VALUE_LEFT && float_side != CSS_VALUE_RIGHT) return false;
    BoxEdges margin = layout_boundary_margin_edges(float_block->bound);
    float margin_left = margin.left;
    float margin_right = margin.right;
    float float_outer_width = float_block->width + margin_left + margin_right;
    float line_left = pa_line->has_float_intrusion ? pa_line->effective_left : pa_line->left;
    float line_right = pa_line->has_float_intrusion ? pa_line->effective_right : pa_line->right;
    float current_line_used = current_line_used_width_for_float(pa_line, line_left);
    float available_width = max(line_right - line_left, 0.0f);
    bool needs_next_line = current_line_used + float_outer_width > available_width + 0.5f;
    return needs_next_line;
}

static float find_line_y_for_width_with_floats(BlockContext* bfc, BlockContext* block_ctx,
                                               Linebox* line, float required_width,
                                               float min_local_y, float query_height) {
    if (!bfc || !block_ctx || !line) return min_local_y;
    if (bfc->left_float_count == 0 && bfc->right_float_count == 0) return min_local_y;
    float y_bfc = block_ctx->bfc_offset_y + min_local_y;
    int max_iterations = 100;
    while (max_iterations-- > 0) {
        FloatAvailableSpace space = block_context_space_at_y(bfc, y_bfc, query_height);
        float local_left = max(space.left - block_ctx->bfc_offset_x, line->left);
        float local_right = min(space.right - block_ctx->bfc_offset_x, line->right);
        float available_width = max(local_right - local_left, 0.0f);
        if (available_width >= required_width) {
            return y_bfc - block_ctx->bfc_offset_y;
        }
        float next_y = block_context_next_float_boundary(bfc, y_bfc);
        if (next_y <= y_bfc || isinf(next_y) || next_y == FLT_MAX) break;
        y_bfc = next_y;
    }
    if (max_iterations < 0) {
        log_warn("[RAD_CAP_FLOAT_LINE_Y] exhausted float-step search at y=%.1f for required_width=%.1f",
                 y_bfc, required_width);
    }
    return y_bfc - block_ctx->bfc_offset_y;
}

static float push_inline_block_below_floats(LayoutContext* lycon, BlockContext* bfc,
                                            float required_width, float query_height) {
    float new_y = find_line_y_for_width_with_floats(
        bfc, &lycon->block, &lycon->line, required_width,
        lycon->block.advance_y, query_height);
    if (new_y > lycon->block.advance_y) {
        lycon->block.advance_y = new_y;
        line_reset(lycon);
        update_line_for_bfc_floats(lycon, query_height);
    }
    return lycon->line.has_float_intrusion ? lycon->line.effective_left : lycon->line.left;
}

typedef struct BlockFloatFitResult {
    float y;
    float offset_x;
    float width_reduction;
} BlockFloatFitResult;

static BlockFloatFitResult find_block_float_fit(
    BlockContext* bfc, float start_y, float x_in_bfc, float parent_width,
    float query_height, float lowest_float_bottom, bool auto_height,
    bool explicit_width, float required_width, float leading_margin,
    const char* source_loc) {
    BlockFloatFitResult result = {start_y, 0.0f, 0.0f};
    float current_y = start_y;
    int max_iterations = 100;
    while (max_iterations-- > 0) {
        float height = query_height;
        if (auto_height && lowest_float_bottom > current_y) {
            height = lowest_float_bottom - current_y;
        }
        FloatAvailableSpace space = block_context_space_at_y(bfc, current_y, height);
        float local_left = space.left - x_in_bfc;
        float local_right = space.right - x_in_bfc;
        float effective_left = max(local_left, 0.0f);
        float effective_right = min(local_right, parent_width);
        float available_width = max(effective_right - effective_left, 0.0f);
        bool fits = !space.has_left_float && !space.has_right_float;
        if (explicit_width) {
            fits = fits || available_width >= required_width;
        } else {
            fits = fits || (required_width > 0.0f
                ? available_width >= required_width : available_width > 0.0f);
        }
        if (fits) {
            float intrusion_left = max(0.0f, local_left);
            float intrusion_right = max(0.0f, parent_width - local_right);
            result.y = current_y;
            result.offset_x = space.has_left_float
                ? max(0.0f, intrusion_left - leading_margin) : 0.0f;
            result.width_reduction = intrusion_left + intrusion_right;
            return result;
        }
        float next_y = block_context_next_float_boundary(bfc, current_y);
        if (next_y == FLT_MAX || next_y <= current_y) break;
        current_y = next_y;
    }
    if (max_iterations < 0) {
        log_warn("[RAD_CAP_BFC_FLOAT_AVOID] exhausted float avoidance for %s at y=%.1f",
                 source_loc, current_y);
    }
    result.y = current_y;
    return result;
}

static float layout_ratio_transfer_block_to_inline(ViewBlock* block, float block_size,
                                                    float preferred_aspect_ratio,
                                                    bool apply_block_constraints) {
    if (!block || preferred_aspect_ratio <= 0.0f) return 0.0f;
    bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(block) &&
        layout_uses_border_box(block);
    float ratio_block_size = ratio_uses_border_box
        ? layout_css_size_to_border_box(block->bound, layout_box_sizing(block), block_size, false)
        : block_size;
    if (apply_block_constraints) {
        ratio_block_size = layout_apply_min_max_axis(
            block, ratio_block_size, false, layout_uses_border_box(block));
    }
    float transferred_width = layout_apply_min_max_axis(
        block, ratio_block_size * preferred_aspect_ratio, true,
        layout_uses_border_box(block));
    transferred_width = layout_content_size_if_border_box(block, transferred_width, true);
    return transferred_width;
}

static bool layout_percentage_width_basis_is_cyclic(BlockContext* containing_context) {
    if (!containing_context) return false;
    // css 2.1 §10.3.5/§10.3.9: shrink-to-fit width cannot resolve a child
    // percentage before that child's intrinsic contribution is known.
    return layout_is_shrink_to_fit_width(containing_context->establishing_element);
}

static bool layout_closed_details_has_contents_without_summary(ViewBlock* block) {
    if (!block || !block->is_element() || block->tag() != MARKUP_NAME_DETAILS) {
        return false;
    }
    DomElement* details = block->as_element();
    if (details->has_attribute(MARKUP_NAME_OPEN)) return false;
    bool has_summary = false;
    for (DomNode* child = details->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        if (child_elem->tag() == MARKUP_NAME_SUMMARY) {
            has_summary = true;
        }
    }
    return !has_summary;
}

__attribute__((noinline))
void layout_block_content(LayoutContext* lycon, ViewBlock* block, BlockContext *pa_block,
                          Linebox *pa_line, float* out_original_margin_top,
                          bool* out_sibling_margin_collapsed_before_layout) {
    layout_block_content_count++;
    if (layout_block_content_count % 5000 == 0) {
        log_notice("layout_block_content: count=%d", layout_block_content_count);
    }
    if (block->blk) {
        block->blk->bfc_float_avoidance_shift_y = 0.0f;
        block->blk->initial_letter_float_clearance = false;
        block->blk->vertical_geometry_published = false;
    }
    block->x = pa_line->left;  block->y = pa_block->advance_y;
    bool is_float = layout_position_is_floated(block->position);
    *out_original_margin_top = 0.0f;
    *out_sibling_margin_collapsed_before_layout = false;
    bool establishes_bfc = block_context_establishes_bfc(block);
    // CSS 2.1 Section 9.5: "The border box of a table, a block-level replaced element,
    // must not overlap the margin box of any floats in the same block formatting context
    bool is_block_level_replaced = (block->display.outer == CSS_VALUE_BLOCK &&
                                    block->display.inner == RDT_DISPLAY_REPLACED);
    bool is_normal_flow = !is_float &&
        (!block->position || (block->positionp()->position != CSS_VALUE_ABSOLUTE &&
                              block->positionp()->position != CSS_VALUE_FIXED));
    bool should_avoid_floats = (establishes_bfc || is_block_level_replaced) && is_normal_flow;
    bool stretch_width_sizing = layout_axis_uses_stretch_size(block->blk, LAYOUT_AXIS_X) ||
        (block->blk && (
         block->block()->given_min_width_type == CSS_VALUE_STRETCH ||
         block->block()->given_max_width_type == CSS_VALUE_STRETCH));
    float stretch_fit_leading_margin = 0.0f;
    if (stretch_width_sizing && block->bound && !layout_block_inline_axis_is_vertical(block)) {
        bool is_rtl = pa_block->direction == CSS_VALUE_RTL;
        if (is_rtl) {
            if (block->boundary()->margin.right_type != CSS_VALUE_AUTO) {
                stretch_fit_leading_margin = block->boundary()->margin.right;
            }
        } else if (block->boundary()->margin.left_type != CSS_VALUE_AUTO) {
            stretch_fit_leading_margin = block->boundary()->margin.left;
        }
    }
    float bfc_float_offset_x = 0;
    float bfc_available_width_reduction = 0;
    bool bfc_width_was_reduced = false;  // true if auto-width was reduced for float avoidance
    float bfc_shift_down = 0;  // Amount to shift down if element doesn't fit beside floats
    BlockContext* parent_bfc = nullptr;
    float inline_block_static_offset_x = 0.0f;
    View* inline_parent = block->parent_view();
    if (inline_parent && inline_parent->is_inline() && pa_line->is_line_start &&
        !line_has_prior_flow_content(pa_line) && should_avoid_floats) {
        // CSS 2.2 §9.5: only a BFC or block-level replaced fragment avoids
        // floats; a normal block keeps its anonymous block edge as its origin.
        inline_block_static_offset_x = pa_line->advance_x - pa_line->left;
    }
    if (should_avoid_floats) {
        parent_bfc = block_context_find_bfc(pa_block);
        if (parent_bfc && (parent_bfc->left_float_count > 0 || parent_bfc->right_float_count > 0)) {
            // CSS 2.1 §9.5: Float avoidance is based on the border edge, so include margin-top
            float block_margin_top = layout_axis_margin_start(block->bound, LAYOUT_AXIS_Y);
            // CSS 2.1 §8.3.1: If this block's margin-top will collapse with its parent
            bool margin_will_collapse_with_parent = false;
            if (block_margin_top > 0 && block->y == 0) {
                ViewBlock* pa = block->parent_view()->is_block() ? lam::view_require_block(block->parent_view()) : nullptr;
                if (pa && pa->parent_view()) {
                    bool pa_creates_bfc = block_context_establishes_bfc(pa);
                    float pa_decoration_top = layout_axis_decoration_start(
                        pa->bound ? pa->boundary() : nullptr, LAYOUT_AXIS_Y);
                    if (!pa_creates_bfc && pa_decoration_top == 0) {
                        margin_will_collapse_with_parent = true;
                    }
                }
            }
            float effective_margin = margin_will_collapse_with_parent ? 0 : block_margin_top;
            float y_in_bfc = block->y + effective_margin;
            float x_in_bfc = block->x;
            ViewElement* walker = block->parent_view();
            while (walker && walker != parent_bfc->establishing_element) {
                y_in_bfc += walker->y;
                x_in_bfc += walker->x;
                walker = walker->parent_view();
            }
            if (block->parent_view() && block->parent_view()->is_inline()) {
                // CSS 2.1 §9.2.1.1: the block fragment's line origin is
                // already present in block->x; do not add it twice through
                // the provisional inline fragment position.
                x_in_bfc -= pa_line->left;
            }
            // For elements with explicit CSS width, use that; otherwise use parent width
            float element_required_width = pa_block->content_width;
            bool has_explicit_width = false;
            bool has_stretch_width_constraint = block->blk &&
                (block->block()->given_min_width_type == CSS_VALUE_STRETCH ||
                 block->block()->given_max_width_type == CSS_VALUE_STRETCH);
            if (block->blk) {
                if (block->block()->given_width > 0) {
                    element_required_width = block->block()->given_width;
                    has_explicit_width = true;
                } else if (!isnan(block->block()->given_width_percent)) {
                    element_required_width = pa_block->content_width * block->block()->given_width_percent / 100.0f;
                    has_explicit_width = true;
                }
            }
            // the pre-clamp specified width must not force an unnecessary clear.
            if (has_stretch_width_constraint) {
                has_explicit_width = false;
            }
            if ((block->display.inner == CSS_VALUE_TABLE ||
                 block->view_type == RDT_VIEW_TABLE) && block->blk &&
                block->block()->given_min_width >= 0.0f) {
                element_required_width = layout_apply_min_max_axis(
                    block, element_required_width, true, true);
            }
            if (has_explicit_width && block->bound) {
                if (block->boundary()->margin.left_type != CSS_VALUE_AUTO)
                    element_required_width += block->boundary()->margin.left;
                if (block->boundary()->margin.right_type != CSS_VALUE_AUTO)
                    element_required_width += block->boundary()->margin.right;
            }
            // CSS 2.1 §9.5: "The border box... must not overlap the margin box of
            float element_border_box_height = 1.0f;  // fallback for auto-height
            if (layout_axis_has_given_size(block, false)) {
                element_border_box_height = block->block()->given_height;
                BoxMetrics block_box = layout_box_metrics(block);
                element_border_box_height += block_box.pad_border_v;
            } else if (parent_bfc->lowest_float_bottom > y_in_bfc) {
                // CSS 2.1 §9.5 requires no overlap with ANY float. Since we don't know
                element_border_box_height = parent_bfc->lowest_float_bottom - y_in_bfc;
            }
            float current_y = y_in_bfc;
            if (has_explicit_width) {
                BlockFloatFitResult fit = find_block_float_fit(
                    parent_bfc, current_y, x_in_bfc, pa_block->content_width,
                    element_border_box_height, parent_bfc->lowest_float_bottom,
                    block->blk && block->block_mut()->given_height < 0,
                    true, element_required_width, stretch_fit_leading_margin,
                    block->source_loc());
                current_y = fit.y;
                bfc_float_offset_x = fit.offset_x;
                bfc_available_width_reduction = fit.width_reduction;
            } else {
                // CSS 2.1 §9.5: "The border box of a table, a block-level replaced element,
                // context... must not overlap the margin box of any floats in the same BFC.
                FloatAvailableSpace space = block_context_space_at_y(parent_bfc, current_y, element_border_box_height);
                float local_left = space.left - x_in_bfc;
                float local_right = space.right - x_in_bfc;
                float float_intrusion_left = max(0.0f, local_left);
                float float_intrusion_right = max(0.0f, pa_block->content_width - local_right);
                float available_beside_float = pa_block->content_width - float_intrusion_left - float_intrusion_right;
                bool is_table = (block->display.inner == CSS_VALUE_TABLE ||
                                 block->view_type == RDT_VIEW_TABLE);
                bool has_rigid_min_width = is_table || is_block_level_replaced;
                float min_required = 0;
                // A BFC cannot remain beside floats when their vertical union
                bool requires_positive_available_width = available_beside_float <= 0.0f;
                bool should_step_down = requires_positive_available_width;
                if (has_rigid_min_width && block->is_element()) {
                    IntrinsicSizes isizes = layout_measure_intrinsic_widths(
                        lycon, lam::dom_require<DOM_NODE_ELEMENT>(block));
                    min_required = isizes.min_content;
                    BoxMetrics block_box = layout_box_metrics(block);
                    min_required += block_box.pad_border_h;
                    if (min_required > available_beside_float + 0.5f) {
                        should_step_down = true;
                    }
                }
                if (!should_step_down) {
                    if (space.has_left_float && float_intrusion_left > 0) {
                        bfc_float_offset_x = max(
                            0.0f, float_intrusion_left - stretch_fit_leading_margin);
                    }
                    bfc_available_width_reduction = float_intrusion_left + float_intrusion_right;
                } else {
                    BlockFloatFitResult fit = find_block_float_fit(
                        parent_bfc, current_y, x_in_bfc, pa_block->content_width,
                        element_border_box_height, parent_bfc->lowest_float_bottom,
                        block->blk && block->block_mut()->given_height < 0,
                        false, min_required, stretch_fit_leading_margin,
                        block->source_loc());
                    current_y = fit.y;
                    bfc_float_offset_x = fit.offset_x;
                    bfc_available_width_reduction = fit.width_reduction;
                }
            }
            bfc_shift_down = current_y - y_in_bfc;
            if (bfc_shift_down > 0) {
                if (block->blk) {
                    block->blk->bfc_float_avoidance_shift_y = bfc_shift_down;
                }
                block->y += bfc_shift_down;
                pa_block->advance_y += bfc_shift_down;
            }
        }
    }
    if (establishes_bfc) {
        lycon->block.is_bfc_root = true;
        lycon->block.establishing_element = block;
        block_context_reset_floats(&lycon->block);
        block_context_reset_initial_letters(&lycon->block);
    } else {
        lycon->block.is_bfc_root = false;
        lycon->block.establishing_element = nullptr;
    }
    NameId elmt_name = block->tag();
    ContainIntrinsicUsedAxes contain_intrinsic_used_axes =
        apply_contain_intrinsic_used_size(lycon, block);
    apply_canvas_last_remembered_size(lycon, block);
    apply_canvas_object_view_box_auto_size(lycon, block);
    // CSS 2.1 §10.3.2/§10.6.2: For replaced elements with 'width: auto' or
    bool is_open_popover_object = elmt_name == MARKUP_NAME_OBJECT &&
        block->is_element() && block->as_element()->is_popover_open() &&
        !block->get_attribute(MARKUP_NAME_DATA);
    bool object_uses_default_size = block->is_element() &&
        layout_object_uses_default_size(block->as_element());
    if (elmt_name == MARKUP_NAME_IFRAME || is_open_popover_object ||
        object_uses_default_size) {
        // Table-internal display resolution can skip BlockProp creation, but an
        // element's intrinsic fallback must persist on the box's used-size slots.
        block->ensure_block(lycon);
        LayoutAxisPair<float> defaults = {300.0f, 150.0f};
        LayoutAxisPair<float> parent_sizes = {
            pa_block ? pa_block->content_width : 0.0f,
            pa_block ? pa_block->content_height : 0.0f
        };
        for (LayoutAxis axis : layout_axes()) {
            bool horizontal = layout_axis_is_horizontal(axis);
            LayoutAxisRefs context(&lycon->block, axis);
            LayoutAxisRefs props(block->block_mut(), axis);
            bool has_percent = props.given_percent && !isnan(*props.given_percent);
            if (context.given && *context.given >= 0.0f &&
                props.given && *props.given < 0.0f && !has_percent) {
                // The fallback was selected before sizing allocated BlockProp;
                // retain it when CSS fit-content would otherwise collapse it.
                layout_store_given_axis(lycon, block, *context.given, horizontal, false);
                layout_clear_auto_axis_type(block, horizontal);
            }
            if (context.given && *context.given < 0.0f && !has_percent) {
                layout_store_given_axis(lycon, block, defaults[axis], horizontal, false);
                layout_clear_auto_axis_type(block, horizontal);
            }
            if (!has_percent || !context.given || *context.given >= 0.0f) continue;
            float base = parent_sizes[axis];
            if (base > 0.0f) {
                float used = base * *props.given_percent / 100.0f;
                layout_store_given_axis(lycon, block, used, horizontal, false);
            } else if (axis == LAYOUT_AXIS_Y) {
                // CSS 2.1 §10.5: percentage height cannot resolve without a basis.
                layout_store_given_axis(lycon, block, defaults[axis], horizontal, false);
            }
        }
        layout_apply_preferred_ratio_to_replaced_auto_axes(lycon, block);
    } else if (elmt_name == MARKUP_NAME_VIDEO) {
        layout_apply_preferred_ratio_to_replaced_auto_axes(lycon, block);
        if (!contain_intrinsic_used_axes.height &&
            layout_used_preferred_aspect_ratio(block) <= 0.0f &&
            layout_block_has_automatic_size(block, false)) {
            // A ratio-less video still has the replaced-element 150px intrinsic
            // block size; treating its empty DOM contents as the auto height
            // collapses the remembered content-visibility size to its borders.
            IntrinsicSize intrinsic = layout_measure_replaced(
                lycon, block, AvailableSpace::make_max_content());
            layout_store_given_axis(
                lycon, block, intrinsic.max_height, false, false);
            layout_clear_auto_axis_type(block, false);
        }
    } else if (elmt_name == MARKUP_NAME_EMBED ||
               (elmt_name == MARKUP_NAME_OBJECT && block->get_attribute(MARKUP_NAME_DATA))) {
        if (block->is_element() && layout_aspect_ratio_uses_content_box(block)) {
            layout_ensure_replaced_image_surface(lycon, block, block->as_element());
        }
        layout_apply_preferred_ratio_to_replaced_auto_axes(lycon, block);
    }
    if (elmt_name == MARKUP_NAME_SVG &&
        !(block->blk && block->block()->content_visibility_hidden)) {
        Element* native_elem = block->as_element() ? dom_element_backing(block->as_element()) : nullptr;
        SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(native_elem);
        float preferred_aspect_ratio = layout_used_preferred_aspect_ratio(block);
        if (block->is_element()) {
            DomElement* svg_element = block->as_element();
            for (LayoutAxis axis : layout_axes()) {
                bool horizontal = layout_axis_is_horizontal(axis);
                CssDeclaration* declaration =
                    layout_specified_physical_size_declaration(svg_element, horizontal);
                const char* attribute = block->get_attribute(horizontal ? "width" : "height");
                if (!declaration && attribute && strchr(attribute, '%')) {
                    float percent = (float)atof(attribute);
                    if (percent >= 0.0f) {
                        layout_store_percentage_axis(lycon, block, percent, horizontal);
                    }
                }
            }
        }
        if (block->blk && pa_block) {
            LayoutAxisPair<float> parent_sizes = {
                pa_block->content_width, pa_block->content_height};
        for (LayoutAxis axis : layout_axes()) {
            LayoutAxisRefs refs(block->block_mut(),
                    axis);
                if (!isnan(*refs.given_percent) && parent_sizes[axis] > 0.0f) {
                    layout_store_given_axis(lycon, block,
                        parent_sizes[axis] * *refs.given_percent / 100.0f,
                        axis == LAYOUT_AXIS_X, false);
                }
            }
        }
        bool parent_has_definite_slot = pa_block &&
            pa_block->content_width > 0.0f &&
            pa_block->content_height > 0.0f;
        bool use_parent_slot = parent_has_definite_slot &&
            !intrinsic.has_intrinsic_width &&
            !intrinsic.has_intrinsic_height &&
            !intrinsic.has_intrinsic_aspect_ratio;
        // CSS Sizing 3 §5.1: viewBox-only SVGs provide a ratio, not natural
        bool use_parent_ratio_slot = pa_block &&
            pa_block->content_width > 0.0f &&
            !intrinsic.has_intrinsic_width &&
            !intrinsic.has_intrinsic_height &&
            intrinsic.has_intrinsic_aspect_ratio;
        bool has_width_percent = block->blk && !isnan(block->block()->given_width_percent);
        bool has_height_percent = block->blk && !isnan(block->block()->given_height_percent);
        bool width_is_auto = !block->blk || block->block()->given_width_type == CSS_VALUE_AUTO ||
                             block->block()->given_width_type == CSS_VALUE__UNDEF;
        bool height_is_auto = !block->blk || block->block()->given_height_type == CSS_VALUE_AUTO ||
                              block->block()->given_height_type == CSS_VALUE__UNDEF;
        float ratio_slot_content_width = -1.0f;
        if (width_is_auto && lycon->block.given_width < 0 && !has_width_percent) {
            if (use_parent_ratio_slot) {
                float stretch_css_width = layout_stretch_fit_used_css_size(
                    block, pa_block->content_width, true);
                ratio_slot_content_width = layout_content_size_if_border_box(
                    block, stretch_css_width, true);
                lycon->block.given_width = layout_uses_border_box(block)
                    ? stretch_css_width : ratio_slot_content_width;
            } else {
                lycon->block.given_width = use_parent_slot ? pa_block->content_width :
                    (intrinsic.has_intrinsic_width ? intrinsic.width : 300.0f);
            }
            block->ensure_block(lycon);
            layout_store_given_axis(lycon, block, lycon->block.given_width, true, false);
            layout_clear_auto_axis_type(block, true);
        }
        if (height_is_auto && lycon->block.given_height < 0 && !has_height_percent) {
            if (use_parent_ratio_slot && lycon->block.given_width < 0.0f) {
                if (ratio_slot_content_width < 0.0f) {
                    float stretch_css_width = layout_stretch_fit_used_css_size(
                        block, pa_block->content_width, true);
                    ratio_slot_content_width = layout_content_size_if_border_box(
                        block, stretch_css_width, true);
                }
                float ratio_content_height = ratio_slot_content_width /
                    intrinsic.aspect_ratio;
                lycon->block.given_height = layout_border_size_if_content_box(
                    block, ratio_content_height, false);
            } else if (use_parent_slot) {
                lycon->block.given_height = pa_block->content_height;
            } else if (preferred_aspect_ratio > 0.0f && lycon->block.given_width > 0.0f &&
                       !contain_intrinsic_used_axes.width) {
                lycon->block.given_height = lycon->block.given_width /
                    preferred_aspect_ratio;
            } else if (intrinsic.has_intrinsic_height) {
                lycon->block.given_height = intrinsic.height;
            } else if (intrinsic.aspect_ratio > 0.0f && lycon->block.given_width > 0.0f &&
                       !contain_intrinsic_used_axes.width) {
                lycon->block.given_height = lycon->block.given_width / intrinsic.aspect_ratio;
            } else {
                lycon->block.given_height = 150.0f;
            }
            block->ensure_block(lycon);
            layout_store_given_axis(lycon, block, lycon->block.given_height, false, false);
            layout_clear_auto_axis_type(block, false);
        }
    }
    if (block->blk && !isnan(block->block()->given_height_percent)) {
        ViewBlock* containing_view = layout_nearest_block_ancestor(block->parent_view());
        bool parent_context_has_definite_height = lycon->block.parent &&
            lycon->block.parent->given_height >= 0.0f;
        bool containing_height_is_auto = !containing_view ||
            (layout_block_has_automatic_size(containing_view, false) &&
             !layout_percentage_height_basis_is_algorithmically_definite(containing_view) &&
             !parent_context_has_definite_height);
        bool iframe_intrinsic_fallback = block->tag() == MARKUP_NAME_IFRAME &&
            lycon->block.given_height >= 0.0f;
        bool canvas_abspos_percentage_has_basis = block->tag() == MARKUP_NAME_CANVAS &&
            layout_definite_abspos_content_height(containing_view) >= 0.0f;
        if (containing_height_is_auto && !iframe_intrinsic_fallback &&
            !canvas_abspos_percentage_has_basis) {
            // A cyclic percentage height must become auto before a replaced
            block->blk->given_height = -1.0f;
            block->blk->given_height_type = CSS_VALUE_AUTO;
            lycon->block.given_height = -1.0f;
        }
    }
    if (lycon->table_cell_first_row_layout && block->blk &&
        !isnan(block->block()->given_height_percent)) {
        ViewElement* parent = block->parent_view();
        bool is_direct_cell_child = parent && parent->view_type == RDT_VIEW_TABLE_CELL;
        bool is_replaced = block->display.inner == RDT_DISPLAY_REPLACED;
        bool non_scrolling_overflow = !block->scroller ||
            ((block->scroll()->overflow_x == CSS_VALUE_VISIBLE ||
              block->scroll()->overflow_x == CSS_VALUE_CLIP ||
              block->scroll()->overflow_x == CSS_VALUE_HIDDEN) &&
             (block->scroll()->overflow_y == CSS_VALUE_VISIBLE ||
              block->scroll()->overflow_y == CSS_VALUE_CLIP ||
              block->scroll()->overflow_y == CSS_VALUE_HIDDEN));
        if (is_direct_cell_child && !is_replaced && !non_scrolling_overflow) {
            // CSS Tables 3 §3.10.2: scrolling percentage-height children are
            // zero-sized for row sizing, leaving min-height as their contribution.
            layout_store_given_axis(lycon, block, 0.0f, false, false);
            block->block_mut()->given_height_type = CSS_VALUE__LENGTH;
        }
    }
    // CSS 2.1 §10.4: Track whether image dimensions were auto-derived from intrinsic ratio.
    bool image_height_auto_derived = false;
    bool image_width_auto_derived = false;
    bool image_height_blocks_ratio_transfer = false;
    bool image_width_blocks_ratio_transfer = false;
    bool image_height_auto_derived_from_css_ratio = false;
    bool image_width_auto_derived_from_css_ratio = false;
    float image_auto_size_css_aspect_ratio = 0.0f;
    bool image_auto_size_css_ratio_uses_content_box = false;
    CssDeclaration* content_replacement_decl = nullptr;
    CssContentImage content_replacement_image = {nullptr, 1.0f};
    if (elmt_name != MARKUP_NAME_IMG && block->display.inner == RDT_DISPLAY_REPLACED &&
        block->specified_style) {
        content_replacement_decl = style_tree_get_declaration(block->specified_style, CSS_PROPERTY_CONTENT);
        if (content_replacement_decl) {
            css_content_replacement_image(content_replacement_decl->value, &content_replacement_image);
        }
    }
    bool is_generated_content_image = content_replacement_image.url && content_replacement_image.url[0];
    if (elmt_name == MARKUP_NAME_IMG || is_generated_content_image) { // load image intrinsic width and height
        if (elmt_name == MARKUP_NAME_IMG && block->is_element() &&
            (!block->embed || !block->embedp()->img)) {
            // Picture source selection happens before replaced sizing; otherwise
            layout_ensure_replaced_image_surface(lycon, block, block->as_element());
        }
        const char *value = is_generated_content_image ? content_replacement_image.url : block->get_attribute("src");
        bool has_src_attr = value && value[0] != '\0';
        if (has_src_attr) {
            size_t value_len = strlen(value);
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, value, value_len);
            if (!block->embed) {
                block->ensure_embed(lycon);
            }
            block->embed->content_image_resolution = is_generated_content_image ?
                content_replacement_image.resolution : 0.0f;
            char* resolved_content_url = is_generated_content_image ?
                resolve_css_resource_url(lycon, content_replacement_decl, src->str) : nullptr;
            const char* image_url = resolved_content_url ? resolved_content_url : src->str;
            ImageSurface* loaded_img = block->embedp()->img ? block->embedp()->img :
                load_image(lycon->ui_context, image_url);
            if (loaded_img) {
                if (block->embedp()->img && block->embedp()->img != loaded_img && !block->embedp()->img->url) {
                    image_surface_destroy(block->embedp()->img);
                }
                block->embed->img = loaded_img;
            }
            strbuf_free(src);
            if (block->embedp()->img) {
                block->embed->broken_alt_fallback = false;
            }
        }
        if (block->embed && block->embedp()->img) {
            ImageSurface* img = block->embedp()->img;
            bool from_image_orientation = layout_image_orientation_uses_from_image(block->as_element());
            float w = (from_image_orientation || img->encoded_width <= 0) ? img->width : img->encoded_width;
            float h = (from_image_orientation || img->encoded_height <= 0) ? img->height : img->encoded_height;
            bool image_has_ratio_without_natural_size =
                !img->has_intrinsic_size && img->has_intrinsic_aspect_ratio;
            float image_resolution = block->embedp()->content_image_resolution > 0.0f ?
                block->embedp()->content_image_resolution : 1.0f;
            w /= image_resolution;
            h /= image_resolution;
            ObjectViewBoxUsedRect object_view_box = img->has_intrinsic_size
                ? resolve_object_view_box_rect(lycon, block->as_element(), w, h)
                : ObjectViewBoxUsedRect{false, 0.0f, 0.0f, w, h};
            if (object_view_box.valid) {
                // CSS Images 4 §2.1: object-view-box changes the source object
                w = object_view_box.width;
                h = object_view_box.height;
            }
            if (block->blk && pa_block) {
                bool percentage_width_cyclic = layout_percentage_width_basis_is_cyclic(pa_block);
                if (!isnan(block->block()->given_width_percent) &&
                    (pa_block->given_width >= 0.0f ||
                     (percentage_width_cyclic && pa_block->content_width > 0.0f))) {
                    lycon->block.given_width = pa_block->content_width *
                        block->block()->given_width_percent / 100.0f;
                }
                if (!isnan(block->block()->given_height_percent) &&
                    pa_block->given_height >= 0.0f) {
                    lycon->block.given_height = pa_block->content_height *
                        block->block()->given_height_percent / 100.0f;
                }
            }
            // css 2.1 §10.3.2/§10.6.2: a percentage against a definite zero
            // containing block is zero; intrinsic image dimensions must not replace it.
            if (lycon->block.given_width < 0 || lycon->block.given_height < 0) {
                float intrinsic_aspect_ratio = h > 0.0f ? w / h : 0.0f;
                float css_aspect_ratio = layout_preferred_aspect_ratio(block);
                bool css_ratio_uses_content_box = layout_aspect_ratio_uses_content_box(block);
                bool css_ratio_overrides_intrinsic = css_aspect_ratio > 0.0f &&
                    (!css_ratio_uses_content_box ||
                     intrinsic_aspect_ratio <= 0.0f);
                float image_auto_size_aspect_ratio = css_ratio_overrides_intrinsic ?
                    css_aspect_ratio : intrinsic_aspect_ratio;
                LayoutAxisPair<float> natural_size = {w, h};
                LayoutAxisPair<float> given_size = {
                    lycon->block.given_width, lycon->block.given_height};
                LayoutAxisPair<bool> contained_size = {
                    contain_intrinsic_used_axes.width,
                    contain_intrinsic_used_axes.height};
                LayoutAxisPair<bool*> auto_derived = {
                    &image_width_auto_derived, &image_height_auto_derived};
                LayoutAxisPair<bool*> blocks_ratio_transfer = {
                    &image_width_blocks_ratio_transfer,
                    &image_height_blocks_ratio_transfer};
                LayoutAxisPair<bool*> css_ratio_derived = {
                    &image_width_auto_derived_from_css_ratio,
                    &image_height_auto_derived_from_css_ratio};
                bool has_one_axis_given = given_size.x >= 0.0f ||
                    (given_size.x < 0.0f && given_size.y >= 0.0f);
                LayoutAxis source_axis = given_size.x >= 0.0f
                    ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
                LayoutAxis target_axis = source_axis == LAYOUT_AXIS_X
                    ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
                if (has_one_axis_given) {
                    // The two one-axis ratio paths differ only by physical axis;
                    // keeping the transfer paired prevents width/height fixes from drifting.
                    LayoutAxisRefs target_context(&lycon->block, target_axis);
                    float target_size = 0.0f;
                    // Size containment freezes the given axis; its natural paired axis must
                    // survive instead of being recomputed through the aspect ratio.
                    if (contained_size[source_axis] && !contained_size[target_axis]) {
                        target_size = natural_size[target_axis];
                        *blocks_ratio_transfer[target_axis] = true;
                    } else {
                        bool ratio_uses_content_box = css_ratio_uses_content_box ||
                            !css_ratio_overrides_intrinsic;
                        target_size = layout_image_ratio_transfer(
                            block, given_size[source_axis], image_auto_size_aspect_ratio,
                            layout_axis_is_horizontal(source_axis),
                            css_ratio_overrides_intrinsic, ratio_uses_content_box,
                            css_ratio_uses_content_box);
                        *css_ratio_derived[target_axis] = css_ratio_overrides_intrinsic;
                        if (css_ratio_overrides_intrinsic) {
                            image_auto_size_css_aspect_ratio = css_aspect_ratio;
                            image_auto_size_css_ratio_uses_content_box = ratio_uses_content_box;
                        }
                    }
                    *target_context.given = target_size;
                    *auto_derived[target_axis] = true;
                } else {
                    CssEnum intrinsic_height_keyword = layout_intrinsic_preferred_size_keyword(
                        block, false);
                    bool height_uses_intrinsic_keyword = intrinsic_height_keyword != CSS_VALUE__UNDEF;
                    if (css_ratio_overrides_intrinsic && !height_uses_intrinsic_keyword &&
                        w > 0.0f) {
                        float ratio_width = layout_border_size_if_content_box(block, w, true);
                        float ratio_height = ratio_width / image_auto_size_aspect_ratio;
                        lycon->block.given_width = ratio_width;
                        lycon->block.given_height = ratio_height;
                        image_height_auto_derived = true;
                        image_width_auto_derived = true;
                        image_height_auto_derived_from_css_ratio = true;
                        image_width_auto_derived_from_css_ratio = true;
                        image_auto_size_css_aspect_ratio = css_aspect_ratio;
                        image_auto_size_css_ratio_uses_content_box = css_ratio_uses_content_box;
                    } else if (css_ratio_overrides_intrinsic && height_uses_intrinsic_keyword &&
                        w > 0.0f) {
                        float ratio_source_width = layout_border_size_if_content_box(block, w, true);
                        float ratio_height = ratio_source_width / image_auto_size_aspect_ratio;
                        lycon->block.given_width = w;
                        lycon->block.given_height = ratio_height;
                        image_height_auto_derived = true;
                        image_width_auto_derived = true;
                        image_height_auto_derived_from_css_ratio = true;
                        image_auto_size_css_aspect_ratio = css_aspect_ratio;
                        image_auto_size_css_ratio_uses_content_box = css_ratio_uses_content_box;
                    } else if (image_has_ratio_without_natural_size && pa_block &&
                        pa_block->content_width > 0.0f) {
                        // CSS Sizing 3 §5.1 sizes a ratio-only replaced box by
                        float stretch_css_width = layout_stretch_fit_used_css_size(
                            block, pa_block->content_width, true);
                            float stretch_content_width = layout_content_size_if_border_box(
                                block, stretch_css_width, true);
                        lycon->block.given_width = max(stretch_content_width, 0.0f);
                        lycon->block.given_height = image_auto_size_aspect_ratio > 0.0f
                            ? stretch_content_width / image_auto_size_aspect_ratio : h;
                    } else {
                        // CSS 2.1 §10.3.2: a replaced element with natural
                        lycon->block.given_width = w;
                        lycon->block.given_height = h;
                    }
                    image_height_auto_derived = true;
                    image_width_auto_derived = true;
                }
            }
            // CSS 2.1 §10.3.4: Block-level replaced elements use intrinsic width
            layout_clear_auto_axis_type(block, true);
            layout_clear_auto_axis_type(block, false);
            if (img->format == IMAGE_FORMAT_SVG) {
                img->max_render_width = max(lycon->block.given_width, img->max_render_width);
            }
        }
        else if (!has_src_attr) {
            // HTML replaced-element fallback also applies when src is omitted;
            // otherwise an alt-bearing list item contributes no line box.
            bool has_explicit_size = layout_axis_has_given_size(block, true) ||
                layout_axis_has_given_size(block, false);
            if (has_explicit_size || !layout_set_broken_image_alt_fallback(lycon, block, true)) {
                if (image_is_generated_content_child(block) && block->embed) {
                    block->embed->broken_alt_fallback = false;
                }
                float fallback = image_is_generated_content_child(block) ? 16.0f : 0.0f;
                for (LayoutAxis axis : layout_axes()) {
                    bool horizontal = layout_axis_is_horizontal(axis);
                    if (!layout_axis_has_given_size(block, horizontal)) {
                        if (horizontal) lycon->block.given_width = fallback;
                        else lycon->block.given_height = fallback;
                    }
                }
            }
        }
        else { // failed to load image
            const char* alt_text = block->get_attribute("alt");
            CssDeclaration* css_width_decl = block->specified_style ?
                style_tree_get_declaration(block->specified_style, CSS_PROPERTY_WIDTH) : nullptr;
            CssDeclaration* css_height_decl = block->specified_style ?
                style_tree_get_declaration(block->specified_style, CSS_PROPERTY_HEIGHT) : nullptr;
            bool has_css_width = css_width_decl && css_width_decl->value &&
                !(css_width_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                  css_width_decl->value->data.keyword == CSS_VALUE_AUTO);
            bool has_css_height = css_height_decl && css_height_decl->value &&
                !(css_height_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                  css_height_decl->value->data.keyword == CSS_VALUE_AUTO);
            float preferred_aspect_ratio = layout_preferred_aspect_ratio(block);
            bool css_ratio_transfers_width = has_css_width && !has_css_height &&
                preferred_aspect_ratio > 0.0f;
            bool css_ratio_transfers_height = !has_css_width && has_css_height &&
                preferred_aspect_ratio > 0.0f;
            CssDeclaration* display_decl = block->specified_style
                ? style_tree_get_declaration(block->specified_style, CSS_PROPERTY_DISPLAY) : nullptr;
            // preferred ratio only when author CSS establishes a display box.
            bool has_author_display = display_decl && display_decl->origin == CSS_ORIGIN_AUTHOR;
            css_ratio_transfers_width = css_ratio_transfers_width && has_author_display;
            css_ratio_transfers_height = css_ratio_transfers_height && has_author_display;
            if (css_ratio_transfers_width || css_ratio_transfers_height) {
                layout_apply_preferred_ratio_to_replaced_auto_axes(lycon, block);
            }
            const char* html_width_attr = block->get_attribute("width");
            const char* html_height_attr = block->get_attribute("height");
            bool has_html_width = html_width_attr && html_width_attr[0] >= '0' && html_width_attr[0] <= '9';
            bool has_html_height = html_height_attr && html_height_attr[0] >= '0' && html_height_attr[0] <= '9';
            // HTML dimensions establish the failed image's replaced box; alt text
            // must not replace a size supplied by the markup.
            bool has_explicit_dimension = has_css_width || has_css_height ||
                has_html_width || has_html_height;
            bool block_level_markup_dimensions =
                (block->display.outer == CSS_VALUE_BLOCK ||
                 block->display.outer == CSS_VALUE_LIST_ITEM) &&
                has_html_width && has_html_height;
            bool default_inline_alt_fallback = alt_text && alt_text[0] != '\0' &&
                !has_author_display;
            if (alt_text && alt_text[0] != '\0' &&
                (!has_explicit_dimension ||
                 (default_inline_alt_fallback && !block_level_markup_dimensions))) {
                layout_set_broken_image_alt_fallback(lycon, block, false);
            } else {
                if (block->embed) {
                    block->embed->broken_alt_fallback = false;
                }
                bool has_definite_width = has_css_width || has_html_width;
                bool has_definite_height = has_css_height || has_html_height;
                if ((!has_definite_width || !has_definite_height) &&
                    !css_ratio_transfers_width && !css_ratio_transfers_height) {
                    // cannot resolve the other, while two definite axes retain the box.
                    if (has_definite_width) lycon->block.given_width = -1.0f;
                    if (has_definite_height) lycon->block.given_height = -1.0f;
                }
                bool has_width_percent = block->blk && !isnan(block->block()->given_width_percent);
                bool block_auto_width = block->display.outer == CSS_VALUE_BLOCK &&
                    !element_has_float(block) &&
                    (!block->blk || block->block()->given_width_type == CSS_VALUE_AUTO ||
                     block->block()->given_width_type == CSS_VALUE__UNDEF) &&
                    !has_width_percent;
                if (lycon->block.given_width < 0.0f && !block_auto_width) {
                    layout_store_given_axis(lycon, block, 16.0f, true, false);
                }
                if (lycon->block.given_height < 0.0f) {
                    ViewBlock* percentage_parent = layout_nearest_block_ancestor(
                        block->parent_view());
                    bool definite_zero_percentage_height = block->blk &&
                        !isnan(block->block()->given_height_percent) &&
                        percentage_parent && percentage_parent->blk &&
                        percentage_parent->block()->given_height >= 0.0f &&
                        !layout_block_has_automatic_size(percentage_parent, false) &&
                        percentage_parent->content_height <= 0.0f;
                    // CSS Sizing 3: a percentage against a definite zero-height
                    // containing block is 0, not the broken-image fallback size.
                    layout_store_given_axis(lycon, block,
                        definite_zero_percentage_height ? 0.0f : 16.0f,
                        false, false);
                }
            }
        }
    }
    if (block->blk && lycon->block.given_width < 0.0f &&
        !isnan(block->block()->given_width_percent) &&
        pa_block && pa_block->given_width >= 0.0f) {
        float container_width = pa_block->content_width;
        layout_store_given_axis(lycon, block,
            container_width * block->block()->given_width_percent / 100.0f,
            true, false);
    }
    if (block->blk && pa_block) {
        if (!isnan(block->block()->given_min_width_percent)) {
            block->blk->given_min_width = pa_block->content_width *
                block->block()->given_min_width_percent / 100.0f;
        }
        if (!isnan(block->block()->given_max_width_percent)) {
            block->blk->given_max_width = pa_block->content_width *
                block->block()->given_max_width_percent / 100.0f;
        }
    }
    if (block_has_auto_content_image_set(block)) {
        block->ensure_block(lycon);
        if (lycon->block.given_width < 0.0f &&
            (block->block()->given_width < 0.0f || block->block()->given_width_type == CSS_VALUE_AUTO ||
             block->block()->given_width_type == CSS_VALUE__UNDEF)) {
            layout_store_given_axis(lycon, block, 0.0f, true, true);
        }
        if (lycon->block.given_height < 0.0f &&
            (block->block()->given_height < 0.0f || block->block()->given_height_type == CSS_VALUE_AUTO ||
             block->block()->given_height_type == CSS_VALUE__UNDEF)) {
            layout_store_given_axis(lycon, block, 0.0f, false, true);
        }
    }
    float stretch_constraint_available_width = max(
        pa_block->content_width - bfc_available_width_reduction, 0.0f);
    if (bfc_available_width_reduction > 0.0f) {
        stretch_constraint_available_width += stretch_fit_leading_margin;
    }
    layout_resolve_stretch_minmax_axis(block, stretch_constraint_available_width, true, true);
    layout_resolve_stretch_minmax_axis(block, pa_block->content_height,
                                       pa_block->given_height >= 0.0f, false);
    layout_block_resolve_intrinsic_axis_constraints(
        lycon, block, LAYOUT_AXIS_X, 0.0f);
    bool width_is_auto = block->blk && block->block()->given_width < 0.0f &&
        (block->block()->given_width_type == CSS_VALUE_AUTO ||
         block->block()->given_width_type == CSS_VALUE__UNDEF);
    bool height_is_auto = !block->blk ||
        (block->block()->given_height < 0.0f &&
         (block->block()->given_height_type == CSS_VALUE_AUTO ||
          block->block()->given_height_type == CSS_VALUE__UNDEF));
    layout_block_prepare_canvas_auto_size(lycon, block, contain_intrinsic_used_axes);
    bool orthogonal_auto_inline_size = lycon->block.given_width < 0.0f &&
        block->is_element() &&
        !layout_element_inline_axis_is_vertical(block->as_element()) &&
        (block->display.inner == CSS_VALUE_FLOW ||
         block->display.inner == CSS_VALUE_FLOW_ROOT) &&
        layout_inline_box_is_orthogonal_to_parent(block);
    bool orthogonal_auto_block_size = lycon->block.given_width < 0.0f &&
        block->is_element() &&
        layout_element_inline_axis_is_vertical(block->as_element()) &&
        pa_block &&
        pa_block->given_width >= 0.0f &&
        layout_inline_box_is_orthogonal_to_parent(block);
    ViewBlock* parent_block = layout_nearest_block_ancestor(block->parent_view());
    bool parent_is_root_element = parent_block && is_root_element_block(parent_block);
    bool vertical_auto_inline_formatting = height_is_auto &&
        layout_block_inline_axis_is_vertical(block) && parent_block &&
        layout_block_inline_axis_is_vertical(parent_block) &&
        (parent_is_root_element ||
         parent_block->display.inner == CSS_VALUE_FLOW ||
         parent_block->display.inner == CSS_VALUE_FLOW_ROOT) &&
        pa_block && pa_block->given_height >= 0.0f &&
        (block->display.inner == CSS_VALUE_FLOW ||
         block->display.inner == CSS_VALUE_FLOW_ROOT) &&
        (block->tag() != MARKUP_NAME_BODY ||
         layout_source_has_in_flow_block_child(block));
    bool vertical_auto_inline_size = vertical_auto_inline_formatting &&
        (block->display.outer == CSS_VALUE_BLOCK ||
         block->display.outer == CSS_VALUE_LIST_ITEM ||
         (block->display.outer == CSS_VALUE_INLINE_BLOCK && parent_block &&
          parent_block->display.outer == CSS_VALUE_INLINE_BLOCK));
    float vertical_inline_basis = pa_block && pa_block->given_height >= 0.0f
        ? pa_block->given_height : (pa_block ? pa_block->content_height : 0.0f);
    bool is_stretch_height = layout_axis_uses_stretch_size(
        block->blk, LAYOUT_AXIS_Y);
    bool has_stretch_height_constraint = block->blk &&
        (is_stretch_height ||
         block->block()->given_min_height_type == CSS_VALUE_STRETCH ||
         block->block()->given_max_height_type == CSS_VALUE_STRETCH);
    bool is_stretch_width = layout_axis_uses_stretch_size(
        block->blk, LAYOUT_AXIS_X);
    ViewBlock* stretch_parent = block->parent_view() && block->parent_view()->is_block()
        ? lam::view_require_block(block->parent_view()) : nullptr;
    bool quirks_body_stretch_basis = stretch_parent &&
        stretch_parent->tag_id == MARKUP_NAME_BODY &&
        is_quirks_mode(lycon->doc->view_tree->html_version);
    bool stretch_height_has_definite_basis = pa_block &&
        (pa_block->given_height >= 0.0f || quirks_body_stretch_basis);
    float stretch_height_basis = quirks_body_stretch_basis
        ? lycon->height : pa_block->content_height;
    float ratio_determining_height = lycon->block.given_height;
    if (is_stretch_height && stretch_height_has_definite_basis) {
        // before automatic width would otherwise fill the normal block axis.
        ratio_determining_height = layout_stretch_fit_used_css_size(
            block, stretch_height_basis, false);
    }
    float preferred_aspect_ratio = layout_preferred_aspect_ratio(block);
    bool uses_replaced_natural_ratio = false;
    float canvas_natural_width = 0.0f;
    float canvas_natural_height = 0.0f;
    bool canvas_width_is_auto = elmt_name == MARKUP_NAME_CANVAS &&
        layout_css_size_is_automatic(block, true);
    bool canvas_height_is_auto = elmt_name == MARKUP_NAME_CANVAS &&
        layout_css_size_is_automatic(block, false);
    bool has_canvas_natural_size = block->display.inner == RDT_DISPLAY_REPLACED &&
        layout_canvas_natural_size(block, &canvas_natural_width, &canvas_natural_height) &&
        canvas_natural_width > 0.0f && canvas_natural_height > 0.0f;
    if (has_stretch_height_constraint && preferred_aspect_ratio <= 0.0f &&
        has_canvas_natural_size &&
        (canvas_width_is_auto || canvas_height_is_auto || width_is_auto || height_is_auto)) {
        width_is_auto = canvas_width_is_auto;
        height_is_auto = canvas_height_is_auto;
        if (!width_is_auto && !height_is_auto) {
            canvas_natural_width = 0.0f;
            canvas_natural_height = 0.0f;
        }
        if (canvas_natural_width > 0.0f && canvas_natural_height > 0.0f) {
            preferred_aspect_ratio = canvas_natural_width / canvas_natural_height;
            uses_replaced_natural_ratio = true;
        }
    }
    if (is_stretch_height && !stretch_height_has_definite_basis &&
        preferred_aspect_ratio <= 0.0f &&
        has_canvas_natural_size) {
        preferred_aspect_ratio = canvas_natural_width / canvas_natural_height;
        uses_replaced_natural_ratio = true;
    }
    if (is_stretch_width && block->tag() == MARKUP_NAME_CANVAS &&
        height_is_auto && preferred_aspect_ratio <= 0.0f &&
        has_canvas_natural_size) {
        preferred_aspect_ratio = canvas_natural_width / canvas_natural_height;
        uses_replaced_natural_ratio = true;
    }
    bool has_canvas_fit_content_width = block->tag() == MARKUP_NAME_CANVAS && block->blk &&
        (block->block()->given_min_width_type == CSS_VALUE_FIT_CONTENT ||
         block->block()->given_max_width_type == CSS_VALUE_FIT_CONTENT);
    if (is_stretch_height && has_canvas_fit_content_width &&
        canvas_natural_width > 0.0f && canvas_natural_height > 0.0f) {
        float natural_ratio = canvas_natural_width / canvas_natural_height;
        float fit_width = stretch_height_has_definite_basis
            ? ratio_determining_height * (preferred_aspect_ratio > 0.0f
                ? preferred_aspect_ratio : natural_ratio)
            : canvas_natural_width;
        fit_width = layout_border_size_if_content_box(block, fit_width, true);
        if (block->block()->given_min_width_type == CSS_VALUE_FIT_CONTENT) {
            block->block_mut()->given_min_width = fit_width;
            block->block_mut()->given_min_width_type = CSS_VALUE__UNDEF;
        }
        if (block->block()->given_max_width_type == CSS_VALUE_FIT_CONTENT) {
            block->block_mut()->given_max_width = fit_width;
            block->block_mut()->given_max_width_type = CSS_VALUE__UNDEF;
        }
    }
    float ratio_source_height = is_stretch_height
        ? ratio_determining_height
        : layout_apply_min_max_axis(
            block, ratio_determining_height, false, layout_uses_border_box(block));
    bool replaced_height_constraint_changed = uses_replaced_natural_ratio &&
        fabsf(ratio_source_height - lycon->block.given_height) > 0.01f;
    if (width_is_auto && ratio_determining_height >= 0.0f &&
        preferred_aspect_ratio > 0.0f &&
        (block->display.inner != RDT_DISPLAY_REPLACED ||
         (uses_replaced_natural_ratio && replaced_height_constraint_changed))) {
        bool ratio_uses_content_box = uses_replaced_natural_ratio ||
            layout_aspect_ratio_uses_content_box(block);
        bool ratio_uses_border_box = !ratio_uses_content_box && layout_uses_border_box(block);
        if (ratio_uses_content_box && layout_uses_border_box(block)) {
            ratio_source_height = layout_content_size_from_border_box(block, ratio_source_height, false);
        }
        float ratio_width = ratio_source_height * preferred_aspect_ratio;
        float transferred_width = ratio_uses_border_box || !layout_uses_border_box(block)
            ? ratio_width : layout_border_size_from_content_box(block, ratio_width, true);
        if (block->is_element() &&
            block_axis_has_automatic_css_size(block, true) &&
            !layout_preserve_ratio_transferred_min_content(block, true) &&
            (block->display.inner == CSS_VALUE_FLOW ||
             block->display.inner == CSS_VALUE_FLOW_ROOT) &&
            block->as_element()->first_child) {
            IntrinsicSizes intrinsic = layout_measure_intrinsic_widths(
                lycon, block->as_element(), true);
                    float intrinsic_width = layout_border_size_if_content_box(
                        block, intrinsic.min_content, true);
            // minimum; a transferred max-width must not erase that contribution.
            transferred_width = max(transferred_width, intrinsic_width);
        }
        // Derive auto width before block fill; otherwise the containing width falsely becomes definite.
        layout_store_given_axis(lycon, block, transferred_width, true, true);
    }
    float content_width = -1;
    bool parent_is_intrinsic_sizing = lycon->available_space.is_intrinsic_sizing();
    // CSS 2.2 Section 10.3.5: Floats with auto width use shrink-to-fit width
    bool has_auto_width = !block->blk ||
                          block->block()->given_width_type == CSS_VALUE_AUTO ||
                          block->block()->given_width_type == CSS_VALUE__UNDEF;
    bool is_float_auto_width = element_has_float(block) && lycon->block.given_width < 0 && has_auto_width;
    // CSS 2.1 §10.3.9: Inline-blocks with auto width use shrink-to-fit.
    bool is_button_auto_width = block->tag() == MARKUP_NAME_BUTTON &&
        block->display.inner == CSS_VALUE_FLOW_ROOT &&
        block->display.outer != CSS_VALUE_INLINE_BLOCK &&
        lycon->block.given_width < 0 && has_auto_width;
    bool is_inline_block_auto_width = (block->view_type == RDT_VIEW_INLINE_BLOCK ||
        is_button_auto_width) &&
        lycon->block.given_width < 0 && has_auto_width && !is_float_auto_width;
    // A definite descendant width must not inherit that measurement mode: the
    bool descendant_width_is_definite = lycon->block.given_width >= 0.0f;
    bool width_is_inline_axis = !layout_block_inline_axis_is_vertical(block);
    bool is_max_content_width = (width_is_inline_axis && block->blk &&
                                 block->block_mut()->given_width_type == CSS_VALUE_MAX_CONTENT) ||
                                (width_is_inline_axis && parent_is_intrinsic_sizing &&
                                 !descendant_width_is_definite &&
                                 lycon->available_space.is_width_max_content());
    bool is_min_content_width = (width_is_inline_axis && block->blk &&
                                 block->block_mut()->given_width_type == CSS_VALUE_MIN_CONTENT) ||
                                (width_is_inline_axis && parent_is_intrinsic_sizing &&
                                 !descendant_width_is_definite &&
                                 lycon->available_space.is_width_min_content());
    bool is_fit_content_width = block->blk &&
        block->block_mut()->given_width_type == CSS_VALUE_FIT_CONTENT &&
        width_is_inline_axis &&
        block->display.inner != RDT_DISPLAY_REPLACED;
    if (is_stretch_width) {
        float stretch_available_width = pa_block->content_width;
        if (bfc_available_width_reduction > 0.0f) {
            stretch_available_width = max(
                stretch_available_width - bfc_available_width_reduction +
                stretch_fit_leading_margin, 0.0f);
        }
        float stretch_border_width = layout_stretch_fit_border_box_size(
            block, stretch_available_width, true);
        float stretch_css_width = layout_uses_border_box(block)
            ? stretch_border_width
            : layout_content_size_from_border_box(block, stretch_border_width, true);
        stretch_css_width = layout_apply_min_max_axis(block, stretch_css_width, true, false);
        content_width = layout_content_size_if_border_box(
            block, stretch_css_width, true);
        // the used value for replaced/form finalization, which otherwise
        layout_store_given_axis(lycon, block, stretch_css_width, true, false);
    }
    else if (is_max_content_width || is_min_content_width || is_fit_content_width) {
        float available_width = layout_shrink_to_fit_available_width(
            block, pa_block->content_width);
        content_width = available_width;
    }
    else if (is_float_auto_width) {
        float available_width = layout_shrink_to_fit_available_width(
            block, pa_block->content_width);
        content_width = available_width;
        content_width = layout_apply_min_max_axis(block, content_width, true, false);
        content_width = layout_content_size_if_border_box(block, content_width, true);
    }
    else if (lycon->block.given_width >= 0 && (!block->blk || block->block()->given_width_type != CSS_VALUE_AUTO)) {
        content_width = max(lycon->block.given_width, 0);
        bool width_was_clamped = false;
        float pre_clamp_width = content_width;
        content_width = layout_apply_min_max_axis(block, content_width, true, false);
        width_was_clamped = (content_width != pre_clamp_width);
        // CSS 2.1 §10.4: For replaced elements (images) with intrinsic ratio,
        if (image_height_auto_derived && !image_height_blocks_ratio_transfer &&
            block->embed && block->embedp()->img && width_was_clamped) {
            float iw = block->embedp()->img->width;
            float ih = block->embedp()->img->height;
            if (image_auto_size_css_aspect_ratio > 0.0f) {
                float ratio_source_width = image_auto_size_css_ratio_uses_content_box
                    ? layout_content_size_if_border_box(block, content_width, true)
                    : content_width;
                float ratio_height = ratio_source_width / image_auto_size_css_aspect_ratio;
                lycon->block.given_height = image_auto_size_css_ratio_uses_content_box
                    ? layout_border_size_if_content_box(block, ratio_height, false)
                    : ratio_height;
            } else if (iw > 0) {
                lycon->block.given_height = content_width * ih / iw;
            }
        }
        // CSS 2.1 §10.3.2: For replaced elements with 'width: auto', the intrinsic
        if (layout_uses_border_box(block) &&
            (!image_width_auto_derived || width_was_clamped ||
             image_width_auto_derived_from_css_ratio)) {
            content_width = layout_content_size_if_border_box(block, content_width, true);
        }
    }
    else { // derive from parent block width
        if (orthogonal_auto_block_size) {
            IntrinsicSizes intrinsic = layout_measure_intrinsic_widths(
                lycon, lam::dom_require<DOM_NODE_ELEMENT>(block),
                true);
            content_width = max(intrinsic.max_content, 0.0f);
        } else if (orthogonal_auto_inline_size) {
            float available_width = pa_block->content_width;
            if (block->bound) {
                available_width -= block->boundary()->margin.left_type == CSS_VALUE_AUTO
                    ? 0.0f : block->boundary()->margin.left;
                available_width -= block->boundary()->margin.right_type == CSS_VALUE_AUTO
                    ? 0.0f : block->boundary()->margin.right;
            }
            float fit_border_width = calculate_fit_content_width(
                lycon, static_cast<DomNode*>(block), max(available_width, 0.0f));
            BoxMetrics block_box = layout_box_metrics(block);
            // CSS Writing Modes §7.3.2: an orthogonal auto inline-size is
            // fit-content; the parent physical width must not force-fill it.
            content_width = max(fit_border_width - block_box.pad_border_h, 0.0f);
        } else if (vertical_auto_inline_formatting &&
                   !is_multicol_container(block)) {
            content_width = layout_stretch_fit_used_css_size(
                block, vertical_inline_basis, false);
        }
        float available_from_parent = pa_block->content_width;
        if (bfc_available_width_reduction > 0) {
            available_from_parent -= bfc_available_width_reduction;
            bfc_width_was_reduced = true;
        }
        if ((vertical_auto_inline_formatting &&
             !is_multicol_container(block)) || orthogonal_auto_inline_size ||
            orthogonal_auto_block_size) {
            available_from_parent = content_width;
        } else if (block->bound) {
            content_width = available_from_parent
                - (block->boundary()->margin.left_type == CSS_VALUE_AUTO ? 0 : block->boundary()->margin.left)
                - (block->boundary()->margin.right_type == CSS_VALUE_AUTO ? 0 : block->boundary()->margin.right);
        }
        else { content_width = available_from_parent; }
        // CSS Tables 3: For auto-width tables, max-width is handled by the table
        bool is_auto_width_table = (block->view_type == RDT_VIEW_TABLE) &&
            (!block->blk || block->block()->given_width < 0 || block->block()->given_width_type == CSS_VALUE_AUTO);
        if ((!vertical_auto_inline_formatting || is_multicol_container(block)) &&
            layout_uses_border_box(block)) {
            if (is_auto_width_table) {
                content_width = layout_floor_min_axis(block, content_width, true);
            } else {
                content_width = layout_apply_min_max_axis(block, content_width, true, false);
            }
            content_width = layout_content_size_if_border_box(block, content_width, true);
        } else if (!vertical_auto_inline_formatting || is_multicol_container(block)) {
            if (block->bound) {
                content_width = layout_content_size_from_border_box(block, content_width, true);
            }
            if (is_auto_width_table) {
                content_width = layout_floor_min_axis(block, content_width, true);
            } else {
                content_width = layout_apply_min_max_axis(block, content_width, true, false);
            }
        }
    }
    if (content_width < 0) content_width = 0;
    if (uses_replaced_natural_ratio && height_is_auto &&
        lycon->block.given_width >= 0.0f) {
        float ratio_source_width = layout_apply_min_max_axis(
            block, lycon->block.given_width, true, layout_uses_border_box(block));
        ratio_source_width = layout_content_size_if_border_box(block, ratio_source_width, true);
        float ratio_height = ratio_source_width / preferred_aspect_ratio;
        float used_height = layout_border_size_if_content_box(block, ratio_height, false);
        layout_store_given_axis(lycon, block, used_height, false, true);
    }
    if (width_is_auto && height_is_auto && preferred_aspect_ratio > 0.0f) {
        float min_height = layout_explicit_min_axis_or(block, false, -1.0f);
        if (min_height >= 0.0f) {
            float transferred_width = layout_ratio_transfer_block_to_inline(
                block, min_height, preferred_aspect_ratio, false);
            if (transferred_width > content_width) {
                content_width = transferred_width;
            }
        }
        float max_height = layout_explicit_max_axis_or(block, false, -1.0f);
        if (max_height >= 0.0f) {
            float transferred_width = layout_ratio_transfer_block_to_inline(
                block, max_height, preferred_aspect_ratio, true);
            if (transferred_width < content_width) {
                content_width = transferred_width;
            }
        }
    }
    layout_block_resolve_intrinsic_axis_constraints(
        lycon, block, LAYOUT_AXIS_Y, content_width);
    bool has_intrinsic_height_keyword = layout_axis_uses_intrinsic_size(
        block->blk, LAYOUT_AXIS_Y);
    if (has_intrinsic_height_keyword && block->is_element() &&
        layout_block_has_size_containment_in_axis(block, false)) {
        float intrinsic_border_height = calculate_max_content_height(
            lycon, static_cast<DomNode*>(block), content_width);
        BoxMetrics block_box = layout_box_metrics(block);
        float intrinsic_content_height = max(
            intrinsic_border_height - block_box.pad_border_v, 0.0f);
        float used_height = layout_border_size_if_content_box(
            block, intrinsic_content_height, false);
        layout_store_given_axis(lycon, block,
            layout_apply_min_max_axis(block, used_height, false, false), false, true);
    }
    float content_height = -1;
    if (is_stretch_height && stretch_height_has_definite_basis) {
        float stretch_css_height = layout_stretch_fit_used_css_size(
            block, stretch_height_basis, false);
        content_height = layout_content_size_if_border_box(
            block, stretch_css_height, false);
        // otherwise treats the deferred keyword as an automatic height.
        layout_store_given_axis(lycon, block, stretch_css_height, false, false);
    }
    else if (!is_stretch_height && lycon->block.given_height >= 0) {
        content_height = max(lycon->block.given_height, 0);
        bool height_was_clamped = false;
        float pre_clamp_height = content_height;
        content_height = layout_apply_min_max_axis(block, content_height, false, false);
        height_was_clamped = (content_height != pre_clamp_height);
        // CSS 2.1 §10.7: For replaced elements with intrinsic ratio,
        if (image_width_auto_derived && !image_width_blocks_ratio_transfer &&
            block->embed && block->embedp()->img && height_was_clamped) {
            float iw = block->embedp()->img->width;
            float ih = block->embedp()->img->height;
            if (image_auto_size_css_aspect_ratio > 0.0f) {
                float ratio_source_height = image_auto_size_css_ratio_uses_content_box
                    ? layout_content_size_if_border_box(block, content_height, false)
                    : content_height;
                float ratio_width = ratio_source_height * image_auto_size_css_aspect_ratio;
                content_width = !image_auto_size_css_ratio_uses_content_box
                    ? layout_content_size_if_border_box(block, ratio_width, true)
                    : ratio_width;
            } else if (ih > 0) {
                content_width = content_height * iw / ih;
            }
        }
        // CSS 2.1 §10.6.2: For replaced elements with 'height: auto', the intrinsic
        if (layout_uses_border_box(block) &&
            (!image_height_auto_derived || height_was_clamped ||
             image_height_auto_derived_from_css_ratio)) {
            content_height = layout_content_size_if_border_box(block, content_height, false);
        }
    }
    else if (vertical_auto_inline_size) {
        // border is added afterward and must not be subtracted from that size.
        float vertical_margin = layout_axis_margin_start(block->bound, LAYOUT_AXIS_Y) +
            layout_axis_margin_end(block->bound, LAYOUT_AXIS_Y);
        content_height = max(vertical_inline_basis - vertical_margin, 0.0f);
        lycon->block.content_height = content_height;
        block->ensure_block(lycon);
        layout_store_given_axis(lycon, block, content_height, false, false);
        block->blk->vertical_auto_inline_size_constrained = true;
    }
    else { // auto height - will be determined by content
        bool parent_has_definite_height = pa_block && pa_block->given_height >= 0.0f;
        if (parent_has_definite_height && block->is_element() &&
            layout_has_cyclic_percentage_replaced_descendant(block->as_element())) {
            float cyclic_intrinsic_height = layout_block_intrinsic_content_height(
                lycon, block, content_width);
            if (cyclic_intrinsic_height > 0.0f) {
                // CSS Sizing 3 §5.2.1: a replaced percentage contribution supplies
                content_height = cyclic_intrinsic_height;
            }
        }
        float aspect_ratio = preferred_aspect_ratio;
        if (aspect_ratio > 0.0f && content_width >= 0.0f && !is_float_auto_width) {
            bool ratio_uses_content_box = layout_aspect_ratio_uses_content_box(block);
            bool ratio_uses_border_box = !ratio_uses_content_box && layout_uses_border_box(block);
            float ratio_source_width = ratio_uses_border_box
                ? layout_border_size_if_content_box(block, content_width, true) : content_width;
            float ratio_height = layout_aspect_ratio_height(ratio_source_width, aspect_ratio);
            if (ratio_height >= 0.0f) {
                content_height = !ratio_uses_border_box && layout_uses_border_box(block)
                    ? layout_border_size_from_content_box(block, ratio_height, false) : ratio_height;
                block->ensure_block(lycon);
                layout_store_given_axis(lycon, block, content_height, false, false);
                block->blk->aspect_ratio_auto_height = block->first_child != nullptr;
            }
        }
        if (content_height < 0.0f) content_height = 0.0f;
        content_height = layout_apply_min_max_axis(block, content_height, false, false);
        content_height = layout_content_size_if_border_box(block, content_height, false);
    }
    assert(content_height >= 0);
    lycon->block.content_width = content_width;  lycon->block.content_height = content_height;
    // This must be done AFTER content_width is calculated
    if (lycon->block.is_bfc_root && lycon->block.establishing_element == block) {
        lycon->block.float_left_edge = 0;
        lycon->block.float_right_edge = content_width;
    }
    if (!lycon->available_space.is_intrinsic_sizing()) {
        lycon->available_space.width = AvailableSize::make_definite(content_width);
        if (content_height > 0) {
            lycon->available_space.height = AvailableSize::make_definite(content_height);
        }
    }
    if (block->bound) {
        BoxMetrics block_box = layout_box_metrics(block);
        block->width = content_width + block_box.pad_border_h;
        block->height = content_height + block_box.pad_border_v;
        CssEnum parent_legacy_block_align = CSS_VALUE__UNDEF;
        for (View* ancestor = block->parent; ancestor && ancestor->is_element(); ancestor = ancestor->parent) {
            uintptr_t ancestor_tag = ancestor->tag();
            ViewBlock* ancestor_block = ancestor->is_block()
                ? lam::view_require_block(ancestor) : nullptr;
            if (ancestor_tag == MARKUP_NAME_CENTER) {
                parent_legacy_block_align = CSS_VALUE_CENTER;
                break;
            }
            if (ancestor_block && ancestor_block->blk) {
                if (ancestor_block->block()->legacy_block_align == CSS_VALUE_CENTER ||
                    ancestor_block->block()->legacy_block_align == CSS_VALUE_RIGHT) {
                    parent_legacy_block_align = ancestor_block->block()->legacy_block_align;
                    break;
                }
                if (ancestor_block->block()->legacy_align_center_blocks) {
                    parent_legacy_block_align = CSS_VALUE_CENTER;
                    break;
                }
            }
            if (ancestor->is_element()) {
                DomElement* ancestor_elem = ancestor->as_element();
                CssDeclaration* text_align_decl = ancestor_elem && ancestor_elem->specified_style
                    ? style_tree_get_declaration(ancestor_elem->specified_style, CSS_PROPERTY_TEXT_ALIGN)
                    : nullptr;
                if (text_align_decl && text_align_decl->value &&
                    !(text_align_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                      text_align_decl->value->data.keyword == CSS_VALUE_INHERIT)) {
                    break;
                }
            }
            if (ancestor_tag == MARKUP_NAME_TD || ancestor_tag == MARKUP_NAME_TH) break;
        }
        if (parent_legacy_block_align == CSS_VALUE_CENTER ||
            parent_legacy_block_align == CSS_VALUE_RIGHT) {
            if (block->width < pa_block->content_width &&
                block->boundary()->margin.left_type != CSS_VALUE_AUTO &&
                block->boundary()->margin.right_type != CSS_VALUE_AUTO) {
                block->boundary_mut()->margin.left_type = CSS_VALUE_AUTO;
                if (parent_legacy_block_align == CSS_VALUE_CENTER) {
                    block->boundary_mut()->margin.right_type = CSS_VALUE_AUTO;
                }
            }
        }
        bool is_rtl = pa_block->direction == CSS_VALUE_RTL;
        bool is_inline_level = (block->display.outer == CSS_VALUE_INLINE_BLOCK ||
                                block->display.outer == CSS_VALUE_INLINE);
        layout_resolve_in_flow_horizontal_margins(
            block, pa_block->content_width - bfc_available_width_reduction,
            is_rtl, is_float || is_inline_level);
        // margin-trim — parent trims children's margins (CSS Box 4 §3.1)
        if (ViewBlock* mt_pa = layout_nearest_block_ancestor(block->parent_view())) {
            if (is_quirky_container(mt_pa, lycon) &&
                has_quirky_margin(block, true)) {
                View* first = mt_pa->first_placed_child();
                if (first == static_cast<View*>(block)) {
                    block->boundary_mut()->margin.top = 0;
                }
            }
            if (mt_pa->blk && mt_pa->block_mut()->margin_trim) {
                // CSS Box 4 §3.1: margin-trim:inline on a block container only
                if ((mt_pa->block()->margin_trim & MARGIN_TRIM_BLOCK_START) && block->boundary_mut()->margin.top != 0) {
                    View* first = mt_pa->first_placed_child();
                    while (first && first->is_block()) {
                        ViewBlock* fvb = lam::view_require_block(first);
                        if (fvb->view_type == RDT_VIEW_MARKER ||
                            (fvb->position && element_has_float(fvb))) {
                            View* next = static_cast<View*>(first->next_sibling);
                            next = layout_first_view_with_type(next);
                            first = next;
                            continue;
                        }
                        break;
                    }
                    if (first == static_cast<View*>(block)) {
                        block->boundary_mut()->margin.top = 0;
                    }
                }
            }
        }
        if (!block->boundary()->has_flow_margin) {
            // Preserve the resolved margin box before normal-flow collapse can
            // consume it; fragmentation needs the uncollapsed CSS margins.
            block->boundary_mut()->flow_margin = block->boundary()->margin;
            block->boundary_mut()->has_flow_margin = true;
        }
        *out_original_margin_top = block->boundary()->margin.top;
        block->x += block->boundary()->margin.left;
        block->y += block->boundary()->margin.top;
        block->x += inline_block_static_offset_x;
        if (bfc_float_offset_x > 0) {
            block->x += bfc_float_offset_x;
        }
    }
    else {
        block->width = content_width;  block->height = content_height;
        block->x += inline_block_static_offset_x;
        if (bfc_float_offset_x > 0) {
            block->x += bfc_float_offset_x;
        }
    }
    // IMPORTANT: Apply clear BEFORE setting up inline context and laying out children
    // CSS 2.1 §9.5.2: 'clear' applies only to block-level elements, not inline-level
    bool is_block_level_for_clear = (block->display.outer != CSS_VALUE_INLINE &&
                                     block->display.outer != CSS_VALUE_INLINE_BLOCK);
    // CSS 2.1 §9.5.2: Track whether clearance was applied for margin collapsing
    pa_block->saved_clear_y = -1;  // -1 = no clearance applied
    if (is_block_level_for_clear && block->position &&
        (block->positionp()->clear == CSS_VALUE_LEFT ||
                             block->positionp()->clear == CSS_VALUE_RIGHT ||
                             block->positionp()->clear == CSS_VALUE_BOTH)) {
        bool is_float = block->position && element_has_float(block);
        if (is_float) {
            layout_clear_element(lycon, block);
            // CSS 2.1 §9.5.2: Recalculate x offset after clear moves element below floats
            if (bfc_float_offset_x > 0) {
                BlockContext* clear_bfc = lycon->block.parent
                    ? block_context_find_bfc(lycon->block.parent)
                    : block_context_find_bfc(&lycon->block);
                if (clear_bfc) {
                    FloatAvailableSpace space = layout_block_float_space_at_current_y(block, clear_bfc);
                    if (!space.has_left_float && !space.has_right_float) {
                        block->x -= bfc_float_offset_x;
                        bfc_float_offset_x = 0;
                    }
                }
            }
        } else {
            // CSS 2.1 §9.5.2: In-flow non-float block with margins.
            BlockContext* bfc = lycon->block.parent
                ? block_context_find_bfc(lycon->block.parent)
                : block_context_find_bfc(&lycon->block);
            float clear_y = 0;
            if (bfc) {
                float clear_y_bfc = block_context_clear_y(bfc, block->positionp()->clear);
                BlockContextOffset parent_offset = block_context_offset_to_bfc(
                    block->parent_view(), bfc);
                clear_y = clear_y_bfc - parent_offset.y;
            }
            float own_margin_top = layout_axis_margin_start(block->bound, LAYOUT_AXIS_Y);
            float uncollapsed_y = block->y;
            float hypothetical_y = uncollapsed_y;
            // CSS 2.1 §9.5.2 + §8.3.1: The hypothetical position must account for
            float child_margin_contribution = 0;
            {
                float block_decoration_top = layout_axis_decoration_start(
                    block->bound ? block->boundary() : nullptr, LAYOUT_AXIS_Y);
                if (block_decoration_top == 0) {
                    DomNode* dom_child = block->first_child;
                    while (dom_child) {
                        if (dom_child->is_element()) {
                            DomElement* child_elem = dom_child->as_element();
                            ViewBlock* child_vb = lam::view_as_block(static_cast<View*>(child_elem));
                            if (!child_vb) break;
                            bool child_is_float = layout_position_is_floated(child_vb->position);
                            if (!child_is_float) {
                                float child_mt = layout_axis_margin_start(child_vb->bound, LAYOUT_AXIS_Y);
                                if (child_mt == 0 && child_elem->specified_style) {
                                    CssDeclaration* mt_decl = style_tree_get_declaration(
                                        child_elem->specified_style, CSS_PROPERTY_MARGIN_TOP);
                                    if (mt_decl && mt_decl->value) {
                                        float resolved = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_TOP, mt_decl->value);
                                        if (resolved > child_mt) child_mt = resolved;
                                    }
                                    if (child_mt == 0) {
                                        CssDeclaration* m_decl = style_tree_get_declaration(
                                            child_elem->specified_style, CSS_PROPERTY_MARGIN);
                                        if (m_decl && m_decl->value) {
                                            const CssValue* top_val = m_decl->value;
                                            if (top_val->type == CSS_VALUE_TYPE_LIST && top_val->data.list.count > 0) {
                                                top_val = top_val->data.list.values[0];
                                            }
                                            float resolved = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, top_val);
                                            if (resolved > child_mt) child_mt = resolved;
                                        }
                                    }
                                }
                                if (child_mt > own_margin_top) {
                                    child_margin_contribution = child_mt - own_margin_top;
                                }
                                break;
                            }
                        } else if (dom_child->is_text()) {
                            const unsigned char* text = lam::dom_require<DOM_NODE_TEXT>(dom_child)->text_data();
                            if (text) {
                                bool all_ws = true;
                                for (const unsigned char* p = text; *p; p++) {
                                    unsigned char c = *p;
                                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
                                        all_ws = false; break;
                                    }
                                }
                                if (!all_ws) break;  // real text content stops parent-child collapse
                            }
                        }
                        dom_child = dom_child->next_sibling;
                    }
                }
            }
            View* first_in_flow = block->parent_view()->first_placed_child();
            while (first_in_flow && first_in_flow->is_block()) {
                ViewBlock* vb = lam::view_require_block(first_in_flow);
                if (vb->position && element_has_float(vb)) {
                    first_in_flow = static_cast<View*>(first_in_flow->next_sibling);
                    first_in_flow = layout_first_view_with_type(first_in_flow);
                    continue;
                }
                break;
            }
            if (first_in_flow == static_cast<View*>(block)) {
                ViewBlock* parent = layout_nearest_block_ancestor(block->parent_view());
                bool parent_creates_bfc = parent && block_context_establishes_bfc(parent);
                float parent_decoration_top = layout_axis_decoration_start(
                    parent && parent->bound ? parent->boundary() : nullptr, LAYOUT_AXIS_Y);
                float effective_mt = own_margin_top;
                if (is_quirky_container(parent, lycon) && has_quirky_margin(block, true))
                    effective_mt = 0;
                if (parent && parent->parent && !parent_creates_bfc &&
                    parent_decoration_top == 0 &&
                    effective_mt != 0) {
                    float advance_y_before = block->y - own_margin_top;
                    hypothetical_y = advance_y_before;
                }
            } else {
                View* prev_view = previous_collapsible_sibling(block);
                if (prev_view && prev_view->is_block() && prev_view->view_type != RDT_VIEW_INLINE_BLOCK
                    && lam::view_require_block(prev_view)->bound) {
                    ViewBlock* prev_block = lam::view_require_block(prev_view);
                    float prev_mb = prev_block->boundary()->margin.bottom;
                    float cur_mt = own_margin_top;
                    if (prev_mb != 0 || cur_mt != 0) {
                        float collapsed = collapse_margins(prev_mb, cur_mt);
                        float collapse_amount = (prev_mb + cur_mt) - collapsed;
                        hypothetical_y = uncollapsed_y - collapse_amount;
                    }
                }
            }
            // CSS 2.1 §9.5.2 + §8.3.1: Include the child margin contribution in
            hypothetical_y += child_margin_contribution;
            if (hypothetical_y < clear_y) {
                // Clearance IS needed. CSS 2.1 §9.5.2: clearance places border edge
                if (clear_y >= block->y) {
                    layout_clear_element(lycon, block);
                } else {
                    float delta = clear_y - block->y;
                    block->y = clear_y;
                    pa_block->advance_y += delta;
                }
                pa_block->saved_clear_y = clear_y;
                block->ensure_boundary(lycon);
                block->bound->has_clearance = true;
                // CSS 2.1 §9.5.2: After clearance moves the element below the floats,
                if (bfc_float_offset_x > 0) {
                    BlockContext* clear_bfc = lycon->block.parent
                        ? block_context_find_bfc(lycon->block.parent)
                        : block_context_find_bfc(&lycon->block);
                    if (clear_bfc) {
                        FloatAvailableSpace space = layout_block_float_space_at_current_y(block, clear_bfc);
                        float new_offset_x = 0;
                        if (space.has_left_float) {
                            float x_in_bfc = block->x - bfc_float_offset_x;  // original x in BFC coords
                            float local_left = space.left - x_in_bfc +
                                layout_axis_margin_start(block->bound, LAYOUT_AXIS_X);
                            new_offset_x = max(0.0f, local_left);
                        }
                        float offset_delta = new_offset_x - bfc_float_offset_x;
                        if (offset_delta != 0) {
                            block->x += offset_delta;
                            bfc_float_offset_x = new_offset_x;
                        }
                    }
                }
            }
        }
    }
    // CSS 2.1 §9.5 + §9.5.2: After clearance moves a BFC block below floats,
    if (bfc_width_was_reduced && pa_block->saved_clear_y >= 0 && parent_bfc) {
        FloatAvailableSpace space = layout_block_float_space_at_current_y(block, parent_bfc);
        float new_reduction = 0;
        if (space.has_left_float || space.has_right_float) {
            float fi_left = space.has_left_float ? space.left : 0;
            float fi_right = space.has_right_float ? (parent_bfc->establishing_element->width - space.right) : 0;
            new_reduction = fi_left + fi_right;
        }
        if (new_reduction < bfc_available_width_reduction) {
            float width_increase = bfc_available_width_reduction - new_reduction;
            bfc_available_width_reduction = new_reduction;
            content_width += width_increase;
            if (block->bound) {
                block->width += width_increase;
            }
            lycon->block.content_width = content_width;
        }
    }
    bool has_direct_line_text = false;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (layout_text_node_has_content(child)) {
            has_direct_line_text = true;
            break;
        }
    }
    if (!is_float && block->bound && pa_block->saved_clear_y < 0.0f &&
        has_direct_line_text) {
        float collapse = sibling_margin_collapse_amount(block);
        if (collapse != 0.0f) {
            block->y -= collapse;
            block->boundary_mut()->margin.top -= collapse;
            *out_sibling_margin_collapsed_before_layout = true;
        }
    }
    float collapsed_margin_top = block->bound ? block->boundary()->margin.top : 0.0f;
    setup_inline(lycon, block);
    if (out_sibling_margin_collapsed_before_layout &&
        *out_sibling_margin_collapsed_before_layout && block->bound &&
        fabsf(block->boundary()->margin.top - collapsed_margin_top) > 0.01f) {
        // css 2.1 §8.3.1: intrinsic balance measurement may re-resolve the
        // authored margin; keep the already-consumed used margin collapsed.
        block->boundary_mut()->margin.top = collapsed_margin_top;
    }
    if (pa_block && pa_block->line_clamp > 0 && !pa_block->line_clamped &&
        lycon->block.line_clamp == 0 && block->blk) {
        lycon->block.line_clamp = pa_block->line_clamp;
        lycon->block.line_number = pa_block->line_number;
        block->blk->line_clamp_inherited = true;
    }
    // CSS 2.1 §10.3.5 (floats) and §10.3.9 (inline-blocks): shrink-to-fit width =
    if ((is_float_auto_width || is_inline_block_auto_width || is_max_content_width ||
         is_min_content_width || is_fit_content_width) &&
        block->is_element()) {
        DomElement* dom_element = lam::dom_require<DOM_NODE_ELEMENT>(block);
        float available = layout_shrink_to_fit_available_width(
            block, pa_block->content_width);
        // CSS Sizing: max-content is the preferred contribution itself; the
        float fit_content_available = available;
        if (is_fit_content_width && block->blk) {
            ViewBlock* intrinsic_parent_block = block->parent_view() &&
                block->parent_view()->is_block()
                ? lam::view_require_block(block->parent_view()) : nullptr;
            bool parent_is_max_content = intrinsic_parent_block && intrinsic_parent_block->blk &&
                intrinsic_parent_block->block()->given_width_type == CSS_VALUE_MAX_CONTENT;
            if (!parent_is_intrinsic_sizing && !parent_is_max_content &&
                block->block()->given_width_fit_content_limit >= 0.0f) {
                fit_content_available = min(
                    fit_content_available,
                    block->block()->given_width_fit_content_limit);
            } else if (!parent_is_intrinsic_sizing && !parent_is_max_content &&
                       !isnan(block->block()->given_width_fit_content_percent)) {
                fit_content_available = min(
                    fit_content_available,
                    available * block->block()->given_width_fit_content_percent / 100.0f);
            }
        }
        float fit_content = is_max_content_width
            ? max(calculate_min_content_width(lycon, static_cast<DomNode*>(dom_element)),
                 calculate_max_content_width(lycon, static_cast<DomNode*>(dom_element)))
            : calculate_fit_content_width(lycon, dom_element, fit_content_available);
        if (is_min_content_width) {
            fit_content = calculate_min_content_width(lycon, static_cast<DomNode*>(dom_element));
        }
        if (is_max_content_width && block->display.inner == RDT_DISPLAY_REPLACED &&
            block->blk && block->block()->given_min_height >= 0.0f && block->embed &&
            block->embedp()->img && block->embedp()->img->width > 0 &&
            block->embedp()->img->height > 0) {
            float ratio_height = layout_apply_min_max_axis(
                block, block->block()->given_min_height, false, layout_uses_border_box(block));
            ratio_height = layout_content_size_if_border_box(block, ratio_height, false);
            float transferred_width = ratio_height *
                (float)block->embedp()->img->width / (float)block->embedp()->img->height;
            transferred_width = layout_border_size_if_content_box(block, transferred_width, true);
            // otherwise it restores the replaced resource's smaller natural width.
            fit_content = max(fit_content, transferred_width);
        }
        // CSS 2.1 §10.3.5: Float/shrink-to-fit width replaces the initial placeholder.
        if (fit_content >= 0 && fabsf(fit_content - block->width) > 0.01f) {
            block->width = fit_content;
            // CSS 2.1 §10.4: Apply min-width/max-width constraints to the
            // display:block and max-width:100%) must not expand beyond their
            block->width = layout_apply_min_max_axis(block, block->width, true, true);
            float new_content_width = block->width;
            BoxMetrics block_box = layout_box_metrics(block);
            new_content_width -= block_box.pad_border_h;
            block->content_width = max(new_content_width, 0.0f);
            // final block sizing otherwise reconstructed the border box from
            content_width = block->content_width;
            lycon->block.content_width = block->content_width;
            if (is_float_auto_width && height_is_auto && preferred_aspect_ratio > 0.0f) {
                bool ratio_uses_content_box = layout_aspect_ratio_uses_content_box(block);
                bool ratio_uses_border_box = !ratio_uses_content_box &&
                    layout_uses_border_box(block);
                float ratio_source_width = ratio_uses_border_box
                    ? layout_content_size_from_border_box(block, block->width, true)
                    : block->width;
                float ratio_height = ratio_source_width / preferred_aspect_ratio;
                content_height = ratio_uses_border_box
                    ? layout_content_size_from_border_box(block, ratio_height, false)
                    : ratio_height;
                float used_height = ratio_uses_border_box
                    ? block->height - block->content_height + content_height
                    : content_height;
                block->content_height = content_height;
                block->height = used_height;
                lycon->block.content_height = content_height;
                block->ensure_block(lycon);
                layout_store_given_axis(lycon, block, used_height, false, false);
            }
            float line_content_width = block->content_width;
            bool vertical_auto_inline_axis = layout_block_inline_axis_is_vertical(block) &&
                height_is_auto;
            if (vertical_auto_inline_axis && pa_block &&
                pa_block->given_height >= 0.0f && vertical_auto_inline_formatting) {
                line_content_width = layout_stretch_fit_used_css_size(
                    block, vertical_inline_basis, false);
            } else if (vertical_auto_inline_axis) {
                IntrinsicSizes intrinsic = measure_element_intrinsic_widths(
                    lycon, dom_element);
                float intrinsic_inline_width = intrinsic.max_content - block_box.pad_border_h;
                if (intrinsic_inline_width > line_content_width) {
                    line_content_width = intrinsic_inline_width;
                }
            }
            if (lycon->block.is_bfc_root && lycon->block.establishing_element == block) {
                lycon->block.float_left_edge = 0;
                lycon->block.float_right_edge = block->content_width;
            }
            // CSS 2.1 §16.1: Reset is_first_line BEFORE line_init so that
            lycon->block.is_first_line = true;
            float inner_left = layout_axis_decoration_start(
                block->bound ? block->boundary() : nullptr, LAYOUT_AXIS_X);
            float inner_right = inner_left + line_content_width;
            float balance_width = text_wrap_balance_measure(lycon, block, block->content_width);
            if (balance_width > 0.0f) {
                lycon->block.balance_wrap_active = true;
                lycon->block.balance_wrap_width = balance_width;
                line_init(lycon, inner_left, inner_left + balance_width);
                lycon->line.align_left = inner_left;
                lycon->line.align_right = inner_right;
            } else {
                lycon->block.balance_wrap_active = false;
                lycon->block.balance_wrap_width = 0.0f;
                line_init(lycon, inner_left, inner_right);
            }
        }
        // CSS 2.1 §10.3.3: Re-resolve auto margins after shrink-to-fit changed the
        if (block->bound && !is_float &&
            block->display.outer != CSS_VALUE_INLINE_BLOCK &&
            block->display.outer != CSS_VALUE_INLINE &&
            (block->boundary()->margin.left_type == CSS_VALUE_AUTO || block->boundary()->margin.right_type == CSS_VALUE_AUTO)) {
            float margin_available = pa_block->content_width - bfc_available_width_reduction;
            layout_resolve_auto_margins_after_width_change(
                block, margin_available, is_float);
        }
    }
    bool closed_details_contents = layout_closed_details_has_contents_without_summary(block);
    float closed_details_reserved_line = closed_details_contents
        ? layout_list_item_marker_line_height(lycon) : 0.0f;
    if (closed_details_contents) {
        // HTML rendering reserves the internal default summary line when no
        // direct summary child exists, including boxless content wrappers.
        lycon->block.advance_y += closed_details_reserved_line;
    }
    if (block->tag() == MARKUP_NAME_OPTGROUP &&
        !layout_optgroup_is_native_child(block->as_element()) &&
        !layout_axis_has_given_size(block, false)) {
        // HTML rendering gives an external optgroup an anonymous label line,
        // including when all of its option descendants are display:none.
        float line_height = layout_optgroup_anonymous_line_height(lycon);
        lycon->block.advance_y = max(lycon->block.advance_y, line_height);
    }
    layout_block_inner_content(lycon, block);
    if (closed_details_contents) {
        // The collapsed details box clips its used height to the control line;
        // descendants remain laid out as visible overflow after that line.
        lycon->block.advance_y = closed_details_reserved_line;
        block->height = layout_border_size_from_content_box(
            block, closed_details_reserved_line, false);
        block->content_height = closed_details_reserved_line;
    }
    recompute_inline_descendant_bounds(static_cast<View*>(block), font_box_handle(&lycon->font));
    if (block->tag() == MARKUP_NAME_SVG && block->blk) {
        bool is_border_box = layout_uses_border_box(block);
        if (block->block()->given_width >= 0.0f) {
            block->content_width = layout_content_size_if_border_box(
                block, block->block()->given_width, true);
            block->width = is_border_box
                ? layout_floor_border_box_axis(block, block->block()->given_width, true)
                : layout_border_size_from_content_box(block, block->content_width, true);
        }
        if (block->block()->given_height >= 0.0f) {
            float used_height = layout_apply_min_max_axis(block, block->block()->given_height, false, false);
            block->content_height = layout_content_size_if_border_box(
                block, used_height, false);
            block->height = is_border_box
                ? layout_floor_border_box_axis(block, used_height, false)
                : layout_border_size_from_content_box(block, block->content_height, false);
        }
    }
    if (block->is_element() && block->tag() == MARKUP_NAME_FIELDSET && block->first_child) {
        bool is_flow_fieldset = block->display.inner == CSS_VALUE_FLOW ||
            block->display.inner == CSS_VALUE_FLOW_ROOT;
        bool is_vertical_writing = layout_block_inline_axis_is_vertical(block);
        bool is_vertical_rl = layout_block_writing_mode(block) == WM_VERTICAL_RL;
        BoxMetrics fieldset_box = layout_box_metrics(block);
        float legend_shift = fieldset_box.border.top + fieldset_box.padding.top;
        if (is_vertical_writing && is_flow_fieldset) {
            DomElement* rendered_legend = find_fieldset_rendered_legend(block);
            ViewBlock* first_legend = rendered_legend
                ? lam::view_require_block(static_cast<View*>(rendered_legend)) : nullptr;
            if (first_legend && first_legend->view_type) {
                float legend_content_width = 0.0f;
                float legend_content_start = first_legend->x;
                legend_content_width = fieldset_legend_content_width(
                    first_legend, &legend_content_start);
                if (legend_content_width > 0.0f) {
                    float legend_leading = max(
                        first_legend->width - legend_content_width, 0.0f);
                    float block_start_border = is_vertical_rl
                        ? fieldset_box.border.right : fieldset_box.border.left;
                    float pre_normalization_width = block->width;
                    block->width = max(
                        block->width - block_start_border - legend_leading, 0.0f);
                    float block_start_padding = is_vertical_rl
                        ? fieldset_box.padding.right : fieldset_box.padding.left;
                    float baseline_delta = pre_normalization_width - block->width +
                        block_start_padding;
                    shift_vertical_fieldset_baselines(block, baseline_delta);
                    block->content_width = max(block->width - fieldset_box.border_h, 0.0f);
                    first_legend->width = legend_content_width;
                    first_legend->x = is_vertical_rl
                        ? block->width - first_legend->width : 0.0f;
                    first_legend->y = fieldset_box.border.top + fieldset_box.padding.top;
                    for (View* child = first_legend->first_child; child; child = child->next()) {
                        if (child->view_type) {
                            child->x -= legend_content_start;
                            child->y += max(
                                (first_legend->height - child->height) / 2.0f, 0.0f);
                        }
                    }
                    // so vertical-lr and vertical-rl use the same invariant.
                    align_fieldset_vertical_content_to_legend(
                        block, rendered_legend, is_vertical_rl,
                        block_start_padding);
                }
            }
        }
        if (legend_shift > 0 && !is_vertical_writing) {
            DomElement* rendered_legend = find_fieldset_rendered_legend(block);
            ViewBlock* first_legend = rendered_legend
                ? lam::view_require_block(static_cast<View*>(rendered_legend)) : nullptr;
            if (first_legend && first_legend->view_type) {
                first_legend->y -= legend_shift;
                bool passed_legend = false;
                for (DomNode* child = block->first_child; child; child = child->next_sibling) {
                    if (child == static_cast<DomNode*>(rendered_legend)) {
                        passed_legend = true;
                        continue;
                    }
                    if (!is_flow_fieldset && !passed_legend) continue;
                    if (child->is_element() && child->as_element()->view_type) {
                        child->as_element()->y -= fieldset_box.border.top;
                    } else if (child->is_text() && child->view_type) {
                        shift_text_geometry(static_cast<View*>(child), -fieldset_box.border.top,
                            TEXT_RECT_SHIFT_ALL);
                    }
                }
                block->height -= fieldset_box.border.top;
                if (block->content_height > fieldset_box.border.top)
                    block->content_height -= fieldset_box.border.top;
            }
        }
    }
    // CSS 2.1 §10.3.4: For block-level replaced elements (SVG, IMG) with auto margins,
    // Skip floats and inline-level elements — their auto margins resolve to 0 (CSS 2.1 §10.3.5, §10.3.2).
    if (block->bound && block->display.inner == RDT_DISPLAY_REPLACED && !is_float &&
        block->display.outer != CSS_VALUE_INLINE_BLOCK && block->display.outer != CSS_VALUE_INLINE &&
        (block->boundary()->margin.left_type == CSS_VALUE_AUTO || block->boundary()->margin.right_type == CSS_VALUE_AUTO)) {
        layout_resolve_auto_margins_after_width_change(
            block, pa_block->content_width, is_float);
    }
    // CSS 2.2 Section 8.3.1: Margins collapse when parent has no border/padding
    // IMPORTANT: Elements that establish a BFC do NOT collapse margins with their children
    // CSS 2.1 §8.3.1: Bottom margin of parent collapses with last child's bottom margin
    bool has_border_bottom = block->bound && block->boundary_mut()->border && block->boundary_mut()->border->width.bottom > 0;
    bool has_padding_bottom = block->bound && block->boundary_mut()->padding.bottom > 0;
    // CSS 2.1 §9.4.1: Elements that establish a BFC prevent margin collapsing
    bool creates_bfc_for_collapse = block_context_establishes_bfc(block);
    // CSS 2.1 §8.3.1: Bottom margins only collapse when parent has auto computed height.
    // Per CSS 2.1 erratum q313, min-height has no influence on bottom margin adjacency.
    bool has_explicit_height = layout_axis_has_given_size(block, false);
    bool quirky_container_bottom = is_quirky_container(block, lycon);
    if (block && !layout_block_inline_axis_is_vertical(block) &&
        !has_border_bottom && !has_padding_bottom && !creates_bfc_for_collapse &&
        !has_explicit_height && layout_rendered_first_placed_child(block)) {
        // CSS 2.2 Section 8.3.1: An empty block allows margins to collapse "through" it when:
        View* last_in_flow = margin_collapse_last_in_flow_child(block);
        // CSS 2.2 Section 8.3.1: Margins collapse through self-collapsing blocks
        // CSS 2.1 §9.2.1.1: Zero-height inline content is wrapped in implicit
        View* effective_last = margin_collapse_effective_last_child(last_in_flow);
        if (effective_last && effective_last->is_block() && lam::view_require_block(effective_last)->bound) {
            ViewBlock* last_child_block = lam::view_require_block(effective_last);
            // CSS 2.1 §8.3.1: Check if last child has margin to collapse with parent.
            bool child_mb_is_quirky = quirky_container_bottom &&
                has_quirky_margin(last_child_block, false);
            float effective_child_mb = child_mb_is_quirky ? 0 : last_child_block->boundary()->margin.bottom;
            if (effective_child_mb != 0 || (!child_mb_is_quirky && has_margin_chain(last_child_block->bound))) {
                // CSS 2.1 §8.3.1: Bottom margins collapse regardless of sign (positive or negative).
                // CSS 2.2 Section 8.3.1: Margins collapse only if there's NO content separating them.
                bool has_content_after = margin_collapse_has_separating_content_after(effective_last);
                if (!has_content_after && !last_child_block->boundary()->clearance_in_margin_chain) {
                    // CSS 2.1 §8.3.1: If the last child's margin chain includes a
                    float parent_margin = block->bound ? block->boundary()->margin.bottom : 0;
                    // CSS 2.1 §8.3.1: Use chain-aware collapse when the child has chain components.
                    float margin_bottom;
                    float chain_pos = 0, chain_neg = 0;
                    if (!child_mb_is_quirky && has_margin_chain(last_child_block->bound)) {
                        float parent_pos, parent_neg;
                        margin_to_chain(parent_margin, &parent_pos, &parent_neg);
                        chain_pos = max(parent_pos, last_child_block->boundary()->margin_chain_positive);
                        chain_neg = min(parent_neg, last_child_block->boundary()->margin_chain_negative);
                        margin_bottom = chain_pos + chain_neg;
                    } else {
                        margin_bottom = collapse_margins(parent_margin, effective_child_mb);
                        margin_to_chain(margin_bottom, &chain_pos, &chain_neg);
                    }
                    // CSS 2.1 §10.6.3 + erratum q313: The collapsible margin was already
                    if (!block->bound) {
                        block->ensure_boundary(lycon);
                    }
                    float own_mb = block->boundary()->margin.bottom;
                    block->boundary_mut()->margin.bottom = margin_bottom;
                    block->bound->margin_chain_positive = chain_pos;
                    block->bound->margin_chain_negative = chain_neg;
                    block->bound->collapsed_through_mb = max(0.f, margin_bottom - own_mb);
                    last_child_block->boundary_mut()->margin.bottom = 0;
                    last_child_block->bound->margin_chain_positive = 0;
                    last_child_block->bound->margin_chain_negative = 0;
                }
            }
        }
    }
    // CSS 2.1 §8.3.1: Transitive margin adjacency through self-collapsing children.
    if (block && !layout_block_inline_axis_is_vertical(block) && block->bound &&
        block->boundary_mut()->margin.bottom != block->boundary_mut()->margin.top &&
        !has_border_bottom && !has_padding_bottom && !creates_bfc_for_collapse &&
        !has_explicit_height && !block->boundary()->has_clearance) {
        bool has_border_t = block->boundary_mut()->border && block->boundary_mut()->border->width.top > 0;
        bool has_padding_t = block->boundary()->padding.top > 0;
        if (!has_border_t && !has_padding_t) {
            bool all_self_collapsing = true;
            bool has_any_in_flow = false;
            View* sc_child = lam::view_require_element(block)->first_placed_child();
            while (sc_child) {
                if (sc_child->is_block()) {
                    ViewBlock* vb = lam::view_require_block(sc_child);
                    bool oof = (vb->position && element_has_float(vb)) ||
                        layout_block_is_out_of_flow_positioned(vb);
                    if (!oof) {
                        has_any_in_flow = true;
                        if (!layout_block_is_self_collapsing(vb)) {
                            all_self_collapsing = false;
                            break;
                        }
                        // CSS 2.1 §9.5.2: clearance breaks margin adjacency.
                        if (vb->bound && vb->boundary_mut()->clearance_in_margin_chain) {
                            all_self_collapsing = false;
                            break;
                        }
                    }
                } else if (sc_child->view_type) {
                    if (sc_child->height > 0) {
                        all_self_collapsing = false;
                        break;
                    }
                    // CSS Inline 3 §2.1: zero-height inline with non-zero inline-axis
                    if (sc_child->view_type == RDT_VIEW_INLINE &&
                        is_inline_substantial(lam::view_require_element(sc_child))) {
                        all_self_collapsing = false;
                        break;
                    }
                }
                View* next = static_cast<View*>(sc_child->next_sibling);
                next = layout_first_view_with_type(next);
                sc_child = next;
            }
            if (has_any_in_flow && all_self_collapsing) {
                float old_mt = block->boundary()->margin.top;
                float new_mt = block->boundary()->margin.bottom;
                float delta = new_mt - old_mt;
                block->boundary_mut()->margin.top = new_mt;
                block->boundary_mut()->margin.bottom = 0;  // consumed by unified collapse
                block->bound->margin_chain_positive = 0;
                block->bound->margin_chain_negative = 0;
                block->y += delta;
                // CSS 2.1 §10.6.4: The parent block's y changed; adjust any abs-pos
                if (delta != 0 && (!block->position || block->positionp()->position == CSS_VALUE_STATIC)) {
                    adjust_abs_descendants_y(lam::view_require_element(block), delta);
                }
                // CSS 2.1 §8.3.1: All self-collapsing children's margins were absorbed
                View* fix_child = lam::view_require_element(block)->first_placed_child();
                while (fix_child) {
                    if (fix_child->is_block()) {
                        ViewBlock* vb = lam::view_require_block(fix_child);
                        bool oof = (vb->position && element_has_float(vb)) ||
                            layout_block_is_out_of_flow_positioned(vb);
                        if (!oof) {
                            float child_delta = -vb->y;  // resetting to 0
                            vb->y = 0;
                            if (vb->bound) vb->boundary_mut()->margin.top = 0;
                            if (child_delta != 0 &&
                                (!vb->position || vb->positionp()->position == CSS_VALUE_STATIC)) {
                                adjust_abs_descendants_y(lam::view_require_element(vb), child_delta);
                            }
                        }
                    }
                    View* next = static_cast<View*>(fix_child->next_sibling);
                    next = layout_first_view_with_type(next);
                    fix_child = next;
                }
                // CSS 2.1 §8.3.1: Floats inside this block were positioned in
                shift_descendant_float_boxes(
                    block_context_find_bfc(&lycon->block), block, delta);
            }
        }
    }
    // CSS 2.1 §10.6.7: In certain cases (BFC roots with AUTO height), the heights
    bool creates_bfc = block->scroller &&
                       (block->scroll()->overflow_x != CSS_VALUE_VISIBLE ||
                        block->scroll()->overflow_y != CSS_VALUE_VISIBLE);
    if ((creates_bfc || lycon->block.is_bfc_root) && !has_explicit_height) {
        if (lycon->block.establishing_element == block) {
            float max_float_bottom = block_context_float_bottom(&lycon->block, true);
            float content_bottom = block->y + block->height;
            if (max_float_bottom > content_bottom - block->y) {
                block->height = layout_apply_min_max_axis(block, max_float_bottom, false, true);
            }
        }
        if (lycon->block.establishing_element == block) {
            float max_float_bottom = block_context_float_bottom(&lycon->block, false);
            if (max_float_bottom > block->height) {
                block->height = layout_apply_min_max_axis(block, max_float_bottom, false, true);
                if (block->scroller && block->scroll_mut()->has_clip) {
                    block->scroll_mut()->clip.bottom = block->height;
                }
            }
        }
    }
    // CSS 2.1 §10.5: Re-resolve percentage heights for absolutely positioned children.
    if (block->position && block->positionp()->first_abs_child) {
        bool had_auto_height = !layout_axis_has_given_size(block, false);
        bool had_percent_height = block->blk && !isnan(block->block()->given_height_percent);
        if (had_auto_height || had_percent_height) {
            re_resolve_abs_children_vertical(block);
        }
    }
    // IMPORTANT: Floats must be added to the BFC root, not just the immediate parent
    if (block->position && element_has_float(block)) {
        // CSS 2.1 §9.5.1: A float encountered on a non-empty line can stay on
        // existing line content. Auto-width floats must be checked here after
        if (same_line_float_needs_next_line(pa_block, pa_line, block)) {
            float line_height = pa_block->line_height > 0 ? pa_block->line_height : 18.0f;
            float margin_top = layout_axis_margin_start(block->bound, LAYOUT_AXIS_Y);
            block->y = pa_block->advance_y + line_height + margin_top;
        }
        layout_float_element(lycon, block);
        BlockContext* bfc = block_context_find_bfc(pa_block);
        if (bfc) {
            block_context_add_float(bfc, block);
            adjust_current_line_after_same_line_float(pa_block, pa_line, bfc, block);
        } else {
            block_context_add_float(pa_block, block);
            adjust_current_line_after_same_line_float(pa_block, pa_line, pa_block, block);
        }
    }
}

static int layout_block_count = 0;

static void align_and_discard_phantom_inline_line(LayoutContext* lycon) {
    if (!lycon || !lycon->line.is_line_start) return;
    // CSS 2.1 §9.4.2 suppresses the phantom line's height, but §16.2 still
    if (lycon->line.start_view && lycon->line.has_phantom_inline_fragment) {
        line_align(lycon);
    }
    lycon->line.start_view = NULL;
    lycon->line.has_phantom_inline_fragment = false;
}

void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    layout_block_count++;
    auto t_block_start = high_resolution_clock::now();
    log_enter();
    // CSS 2.2: Floats are removed from normal flow and don't cause line breaks
    bool is_float = false;
    if (elmt->is_element()) {
        DomElement* elem = elmt->as_element();
        if (elem->position && elem->positionp()->float_prop != CSS_VALUE_NONE) {
            is_float = true;
        } else {
            CssEnum value = layout_specified_keyword(
                elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
            is_float = value == CSS_VALUE_LEFT || value == CSS_VALUE_RIGHT;
        }
    }
    // CSS 2.1 §10.3.7: For absolutely positioned elements whose specified display
    bool is_blockified_inline_abspos = false;
    bool is_out_of_flow_positioned = false;
    bool out_of_flow_parent_is_inline = false;
    if (elmt->is_element()) {
        DomElement* elem = elmt->as_element();
        bool was_inline_level = layout_element_was_inline(elem);
        bool is_abspos = layout_element_is_abs_or_fixed(elem);
        is_out_of_flow_positioned = is_abspos;
        out_of_flow_parent_is_inline = is_abspos && elem->parent &&
            elem->parent->view_type == RDT_VIEW_INLINE;
        if (was_inline_level && is_abspos) {
            is_blockified_inline_abspos = true;
        }
    }
    bool is_inline_table = display.outer == CSS_VALUE_INLINE &&
        display.inner == CSS_VALUE_TABLE;
    bool is_inline_atomic = display.outer == CSS_VALUE_INLINE_BLOCK ||
        is_inline_table;
    if (!is_inline_atomic && !is_float && !is_blockified_inline_abspos &&
        (!is_out_of_flow_positioned || !out_of_flow_parent_is_inline)) {
        // CSS 2.1 §9.6.1: an out-of-flow child in an inline sequence keeps
        // that line for static-position resolution without a phantom break.
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        } else if (lycon->line.start_view) {
            align_and_discard_phantom_inline_line(lycon);
        }
    }
    BlockContext pa_block = lycon->block;  Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // -1 as unresolved
    lycon->block.parent = &pa_block;  lycon->elmt = elmt;
    lycon->block.content_width = lycon->block.content_height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;
    lycon->block.saved_clear_y = -1;
    lycon->block.first_line_ascender = 0;
    lycon->block.last_line_ascender = 0;
    lycon->block.last_line_max_ascender = 0;
    lycon->block.last_line_max_descender = 0;
    lycon->block.first_line_max_ascender = 0;
    lycon->block.first_line_max_descender = 0;
    ViewBlock* block = lam::view_require_block(set_view(lycon,
        display.inner == CSS_VALUE_TABLE ? RDT_VIEW_TABLE :
        display.outer == CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK :
        display.outer == CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM :
        RDT_VIEW_BLOCK,
        elmt));
    block->display = display;
    dom_node_resolve_style(elmt, lycon);
    if (elmt->is_element() && elmt->as_element()->has_animated_display()) {
        // CSS Animations apply after the cascade; refresh the local routing
        // value because this call may have entered through the underlying box.
        display = resolve_display_value(elmt->as_element());
        block->display = display;
    }
    if (elmt->is_element() && elmt->as_element()->tag() == MARKUP_NAME_BUTTON) {
        // HTML Rendering §15.5.3: button layout is a used-value transformation;
        // keep the computed display on DomElement, but lay out the principal box
        // as flow-root so authored children form the anonymous button content.
        display = layout_button_used_display(elmt->as_element(),
            resolve_display_value(elmt));
        block->display = display;
    }
    if (display.outer == CSS_VALUE_NONE) {
        // A sampled display:none suppresses this provisional box and must not
        // leave the pre-resolution line break in the parent formatting context.
        block->view_type = RDT_VIEW_NONE;
        block->x = 0.0f;
        block->y = 0.0f;
        block->width = 0.0f;
        block->height = 0.0f;
        block->content_width = 0.0f;
        block->content_height = 0.0f;
        lycon->block = pa_block;
        lycon->font = pa_font;
        lycon->line = pa_line;
        log_leave();
        auto t_block_end = high_resolution_clock::now();
        g_block_layout_time += duration<double, std::milli>(
            t_block_end - t_block_start).count();
        g_block_layout_count++;
        return;
    }
    if (display.inner == RDT_DISPLAY_REPLACED &&
        layout_element_is_replaced(block->as_element()) &&
        is_table_internal_display(block->display.inner)) {
        // Preserve replaced layout semantics after style resolution restores the
        // computed table-internal display (CSS Tables 3 §2.1).
        block->display.inner = RDT_DISPLAY_REPLACED;
    }
    if (block->tag() == MARKUP_NAME_FRAMESET && lycon->ui_context) {
        block->ensure_block(lycon);
        if (layout_css_size_is_automatic(block, true) &&
            lycon->ui_context->viewport_width > 0) {
            layout_store_given_axis(lycon, block, lycon->ui_context->viewport_width, true, true);
        }
        if (layout_css_size_is_automatic(block, false) &&
            lycon->ui_context->viewport_height > 0) {
            layout_store_given_axis(lycon, block, lycon->ui_context->viewport_height, false, true);
        }
    }
    if (block->blk && block->block_mut()->aspect_ratio_auto_height) {
        block->block_mut()->given_height = -1.0f;
        block->block_mut()->aspect_ratio_auto_height = false;
    }
    DomElement* dom_elem = elmt->is_element() ? elmt->as_element() : nullptr;
    const char* custom_layout_name = custom_layout_name_for_element(dom_elem);
    char custom_layout_name_storage[128];
    if (custom_layout_name && custom_layout_name[0] != '\0') {
        custom_layout_name = stabilize_custom_layout_name(
            custom_layout_name, custom_layout_name_storage, sizeof(custom_layout_name_storage));
    }
    bool has_custom_layout = custom_layout_name && custom_layout_name[0] != '\0';
    radiant::KnownDimensions known_dims = radiant::layout_known_dimensions_from_block(block);
    radiant::SizeF cached_size;
    if (!has_custom_layout &&
        radiant::layout_pass_cache_get(lycon, dom_elem, known_dims, &cached_size, "BLOCK")) {
        block->width = cached_size.width;
        block->height = cached_size.height;
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
        log_leave();
        auto t_block_end = high_resolution_clock::now();
        g_block_layout_time += duration<double, std::milli>(t_block_end - t_block_start).count();
        g_block_layout_count++;
        return;
    }
    if (!has_custom_layout && lycon->run_mode == radiant::RunMode::ComputeSize) {
        bool has_definite_width = (block->blk && block->block_mut()->given_width > 0);
        bool has_definite_height = (block->blk && block->block_mut()->given_height > 0);
        if (has_definite_width && has_definite_height) {
            block->width = block->block()->given_width;
            block->height = block->block()->given_height;
            log_info("%s BLOCK EARLY BAILOUT: Both dimensions known (%.1fx%.1f), skipping full layout", elmt->source_loc(),
                     block->width, block->height);
            lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
            log_leave();
            auto t_block_end = high_resolution_clock::now();
            g_block_layout_time += duration<double, std::milli>(t_block_end - t_block_start).count();
            g_block_layout_count++;
            return;
        }
    }
    // CSS Counter handling (CSS 2.1 Section 12.4)
    if (lycon->counter_context) {
        counter_push_scope(lycon->counter_context);
        // OL/UL/MENU/DIR implicit counter-reset: list-item (CSS 2.1 §12.5)
        setup_list_container_counters(lycon, block, dom_elem);
        if (block->blk && block->block_mut()->counter_reset) {
            counter_reset(lycon->counter_context, block->block()->counter_reset);
            compute_reversed_counter_initial(lycon, dom_elem);
        }
        if (block->blk && block->block_mut()->counter_increment) {
            counter_increment(lycon->counter_context, block->block()->counter_increment);
        }
        if (block->blk && block->block_mut()->counter_set) {
            counter_set(lycon->counter_context, block->block()->counter_set);
        }
        // CSS 2.1 Section 12.5: List markers use implicit "list-item" counter
        if (display.outer == CSS_VALUE_LIST_ITEM || display.list_item) {
            process_list_item(lycon, block, elmt, dom_elem, display);
            // CSS 2.1 §8.3.1: Ensure list items have BoundaryProp allocated so
            // wrappers) fires incorrectly, and parent-child collapse cannot
            if (!block->bound) {
                block->ensure_boundary(lycon);
            }
        }
    }
    float original_margin_top = 0.0f;
    bool sibling_margin_collapsed_before_layout = false;
    if (layout_block_is_out_of_flow_positioned(block)) {
        layout_abs_block(lycon, elmt, block, &pa_block, &pa_line);
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
    } else {
        layout_block_content(lycon, block, &pa_block, &pa_line,
                             &original_margin_top,
                             &sibling_margin_collapsed_before_layout);
        if (has_custom_layout) {
            layout_custom_apply(lycon, block, custom_layout_name);
        }
        bool child_line_clamp_inherited = block->blk && block->block_mut()->line_clamp_inherited;
        bool child_line_clamped = block->blk && block->block_mut()->line_clamped;
        int child_line_number = lycon->block.line_number;
        float child_line_clamp_advance_y = block->blk ? block->block()->line_clamp_advance_y : -1.0f;
        float child_line_clamp_last_line_ascender = block->blk ? block->block()->line_clamp_last_line_ascender : 0.0f;
        float child_line_clamp_last_line_max_ascender = block->blk ? block->block()->line_clamp_last_line_max_ascender : 0.0f;
        float child_line_clamp_last_line_max_descender = block->blk ? block->block()->line_clamp_last_line_max_descender : 0.0f;
        // CSS 2.1 Section 10.8.1: For non-replaced inline-blocks with in-flow line boxes
        float content_last_line_ascender;
        if (lycon->line.max_ascender > 0) {
            content_last_line_ascender = lycon->block.advance_y + lycon->line.max_ascender;
            content_last_line_ascender =
                fieldset_vertical_last_baseline_from_line_context(
                    block, content_last_line_ascender);
        } else {
            content_last_line_ascender = lycon->block.last_line_ascender;
            content_last_line_ascender =
                fieldset_vertical_last_baseline_from_line_context(
                    block, content_last_line_ascender);
        }
        bool used_descendant_last_baseline = false;
        if (content_last_line_ascender <= 0.0f && block->blk &&
            layout_block_inline_axis_is_vertical(block) &&
            block->block()->baseline_source == CSS_VALUE_LAST) {
            float descendant_last_baseline = find_last_baseline_recursive(
                lycon, static_cast<View*>(block), 0.0f, true);
            if (descendant_last_baseline >= 0.0f) {
                // CSS Box Alignment takes a block container's last baseline
                content_last_line_ascender = descendant_last_baseline;
                used_descendant_last_baseline = true;
            }
        }
        if (!used_descendant_last_baseline) {
            content_last_line_ascender = radiant::layout_inline_baseline_for_source(
                block, content_last_line_ascender);
        }
        float baseline_extent = layout_block_inline_axis_is_vertical(block) &&
                !is_multicol_container(block)
            ? block->width : block->height;
        if (display.outer == CSS_VALUE_INLINE_BLOCK &&
            block_has_vertical_flow_child(block) &&
            content_last_line_ascender > baseline_extent) {
            content_last_line_ascender = baseline_extent;
        }
        bool is_inline_grid = display.outer == CSS_VALUE_INLINE_BLOCK &&
            display.inner == CSS_VALUE_GRID;
        if (is_inline_grid && block->blk) {
            content_last_line_ascender = radiant::layout_select_cached_baseline(
                block, block->block()->first_line_baseline,
                block->block()->last_line_baseline, false,
                content_last_line_ascender);
        }
        bool is_inline_flex = display.outer == CSS_VALUE_INLINE_BLOCK &&
            display.inner == CSS_VALUE_FLEX;
        if (is_inline_flex && block->embed && block->embedp()->flex) {
            float flex_baseline = radiant::layout_select_cached_baseline(
                block, block->embedp()->flex->first_baseline,
                block->embedp()->flex->last_baseline, false, 0.0f);
            if (flex_baseline > 0.0f) {
                content_last_line_ascender = layout_block_start_content_offset(block) +
                    flex_baseline;
            }
        }
        bool content_has_line_boxes = content_last_line_ascender > 0;
        // CSS 2.1 §10.8.1: Replaced elements (img, iframe, video, embed, object)
        // Textarea is a multi-line scrollable control; per CSS 2.1 §10.8.1,
        bool is_select_listbox = block->tag() == MARKUP_NAME_SELECT &&
            (!block->form || block->form->multiple || block->form->select_size > 1);
        bool is_replaced = (block->tag() == MARKUP_NAME_IMG || block->tag() == MARKUP_NAME_IFRAME ||
            block->tag() == MARKUP_NAME_VIDEO || block->tag() == MARKUP_NAME_EMBED ||
            (block->tag() == MARKUP_NAME_OBJECT && block->get_attribute("data")) ||
            block->tag() == MARKUP_NAME_TEXTAREA ||
            is_select_listbox);
        bool form_control_margin_baseline = block->tag() == MARKUP_NAME_BUTTON ||
            (block->tag() == MARKUP_NAME_INPUT && block->form &&
             block->form->control_type == FORM_CONTROL_TEXT);
        bool is_broken_alt_image = block->tag() == MARKUP_NAME_IMG &&
            block->embed && block->embedp()->broken_alt_fallback;
        bool textarea_uses_explicit_baseline_source =
            block->tag() == MARKUP_NAME_TEXTAREA &&
            radiant::layout_uses_explicit_baseline_source(block) &&
            radiant::layout_form_control_baseline_for_source(block) > 0.0f;
        if (textarea_uses_explicit_baseline_source) {
            content_last_line_ascender =
                radiant::layout_form_control_baseline_for_source(block);
            content_has_line_boxes = true;
        }
        if (is_broken_alt_image) {
            is_replaced = false;
        }
        if (is_replaced && !textarea_uses_explicit_baseline_source) {
            content_has_line_boxes = false;
        }
        if (block->blk && content_has_line_boxes && content_last_line_ascender > 0) {
            block->blk->last_line_max_ascender = content_last_line_ascender;
        }
        BlockContext* child_bfc = block_context_find_bfc(&lycon->block);
        float initial_margin_bottom_bfc = lycon->block.bfc_offset_y +
            lycon->block.initial_letter_margin_box_bottom;
        float initial_border_bottom_bfc = lycon->block.bfc_offset_y +
            lycon->block.initial_letter_border_box_bottom;
        float block_bottom_bfc = lycon->block.bfc_offset_y + block->height;
        if (child_bfc && lycon->block.initial_letter_margin_box_bottom > 0.0f &&
            initial_margin_bottom_bfc > block_bottom_bfc + 0.01f) {
            bool source_is_short = initial_border_bottom_bfc > block_bottom_bfc + 0.01f;
            block_context_add_initial_letter(child_bfc, block,
                lycon->block.bfc_offset_x + lycon->block.initial_letter_margin_box_left,
                lycon->block.bfc_offset_y + lycon->block.initial_letter_margin_box_top,
                lycon->block.bfc_offset_x + lycon->block.initial_letter_margin_box_right,
                initial_margin_bottom_bfc, lycon->block.direction, source_is_short);
        }
        lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
        bool is_float_element = block->position && element_has_float(block);
        if (!is_float_element && !layout_block_is_out_of_flow_positioned(block) &&
            block->blk) {
            lycon->block.initial_letter_trimmed_start_contribution +=
                block->block()->initial_letter_trimmed_start_contribution;
        }
        // CSS 2.1 §9.7: Floats are blockified — a floated element that was
        if (is_inline_atomic && !is_float_element) {
            if (!lycon->line.start_view) lycon->line.start_view = static_cast<View*>(block);
            // CSS 2.1 §9.5.1: inline-blocks must account for floats across their
            float inline_block_height = block->height;
            update_line_for_bfc_floats(lycon, inline_block_height);
            float effective_left = lycon->line.has_float_intrusion ?
                lycon->line.effective_left : lycon->line.left;
            float effective_right = lycon->line.has_float_intrusion ?
                lycon->line.effective_right : lycon->line.right;
            // CSS 2.1 §16.1: negative text-indent can legitimately position first-line
            if (lycon->line.advance_x < effective_left &&
                lycon->line.advance_x >= lycon->line.left) {
                lycon->line.advance_x = effective_left;
            }
            bool parent_nowrap = false;
            View* nowrap_descendant = static_cast<View*>(block);
            for (DomNode* cur = block->parent; cur; cur = cur->parent) {
                if (!cur->is_element()) break;
                DomElement* elem = lam::dom_require_element(cur);
                if (elem->view_type == RDT_VIEW_INLINE) {
                    ViewSpan* inline_parent = lam::view_require<RDT_VIEW_INLINE>(elem);
                    // CSS Text: a nowrap inline box may still start on a new
                    // line; only its prior in-flow content makes this child
                    // part of an unbreakable sequence.
                    bool has_prior_inline_content =
                        inline_parent->first_placed_child() != nowrap_descendant;
                    if (elem->blk && elem->block_mut()->white_space != 0) {
                        CssEnum ws = elem->block()->white_space;
                        if (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE) {
                            parent_nowrap = has_prior_inline_content;
                            break;
                        }
                    }
                    nowrap_descendant = static_cast<View*>(inline_parent);
                    continue;
                }
                if (elem->blk && elem->block_mut()->white_space != 0) {
                    CssEnum ws = elem->block()->white_space;
                    parent_nowrap = (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE);
                    break;
                }
            }
            ViewBlock* inline_parent_for_placement =
                layout_nearest_block_ancestor(block->parent_view());
            bool vertical_inline_parent_for_placement = inline_parent_for_placement &&
                layout_block_inline_axis_is_vertical(inline_parent_for_placement);
            bool vertical_absolute_inline_parent =
                vertical_inline_parent_for_placement &&
                layout_block_is_out_of_flow_positioned(inline_parent_for_placement);
            // CSS Writing Modes: the absolute vertical-flow pass uses the
            // parent's physical height as its logical inline extent.
            float margin_box_width = vertical_absolute_inline_parent
                ? block->height + (block->bound
                    ? block->boundary()->margin.top + block->boundary()->margin.bottom : 0.0f)
                : block->width + (block->bound
                    ? block->boundary()->margin.left + block->boundary()->margin.right : 0.0f);
            bool has_prior_flow_content = line_has_prior_flow_content(&lycon->line);
            if (block->blk && block->block_mut()->bfc_float_avoidance_shift_y > 0.0f &&
                !lycon->line.has_float_intrusion &&
                !has_prior_flow_content &&
                lycon->line.advance_x > effective_left &&
                effective_left + margin_box_width <= effective_right) {
                lycon->line.advance_x = effective_left;
            }
            // CSS Text: a collapsible separator is a break opportunity before
            // an atomic inline; include it in fit testing so overflow selects
            // that opportunity, then line_break() removes the separator.
            if (lycon->line.advance_x + margin_box_width > effective_right && !parent_nowrap) {
                if (!lycon->line.is_line_start && has_prior_flow_content) {
                    // CSS 2.1 §9.4.2: Break to next line if there's prior content
                    line_break(lycon);
                    update_line_for_bfc_floats(lycon, inline_block_height);
                    effective_left = lycon->line.has_float_intrusion ?
                        lycon->line.effective_left : lycon->line.left;
                    effective_right = lycon->line.has_float_intrusion ?
                        lycon->line.effective_right : lycon->line.right;
                    // CSS 2.1 §9.5: If the block still doesn't fit on the new line
                    if (lycon->line.has_float_intrusion &&
                        effective_left + margin_box_width > effective_right) {
                        BlockContext* bfc = block_context_find_bfc(&lycon->block);
                        if (bfc) {
                            effective_left = push_inline_block_below_floats(
                                lycon, bfc, margin_box_width, inline_block_height);
                        }
                    }
                    block->x = effective_left;
                    // CSS 2.1 §16.2: Walk up to find the topmost ancestor inline span
                    if (!lycon->line.start_view) {
                        View* line_start = static_cast<View*>(block);
                        DomNode* p = static_cast<DomNode*>(block)->parent;
                        while (p && p->is_element()) {
                            DomElement* pe = p->as_element();
                            if (pe->view_type != RDT_VIEW_INLINE) break;
                            View* fc = pe->first_child;
                            while (fc) {
                                if (fc->view_type != RDT_VIEW_NONE) {
                                    bool oof = false;
                                    DomNode* fn = static_cast<DomNode*>(fc);
                                    if (fn->is_element()) {
                                        DomElement* fe = lam::dom_require_element(fn);
                                        if (layout_position_is_abs_fixed(fe->position) ||
                                            layout_position_is_floated(fe->position)) {
                                            oof = true;
                                        }
                                    }
                                    if (!oof) break;
                                }
                                fc = fc->next();
                            }
                            if (fc != line_start) break;
                            line_start = static_cast<View*>(pe);
                            p = p->parent;
                        }
                        lycon->line.start_view = line_start;
                    }
                } else if (lycon->line.has_float_intrusion) {
                    // CSS 2.1 §9.5: First item on line doesn't fit due to float —
                    BlockContext* bfc = block_context_find_bfc(&lycon->block);
                    if (bfc) {
                        effective_left = push_inline_block_below_floats(
                            lycon, bfc, margin_box_width, inline_block_height);
                    }
                    block->x = effective_left;
                } else if (lycon->line.has_float_intrusion &&
                           lycon->line.advance_x > effective_left &&
                           effective_left + margin_box_width <= effective_right) {
                    lycon->line.advance_x = effective_left;
                    block->x = effective_left;
                } else {
                    block->x = lycon->line.advance_x;
                }
            } else {
                block->x = lycon->line.advance_x;
            }
            bool form_control_line_item = block->form_control() != nullptr;
            if (lycon->block.direction == CSS_VALUE_RTL &&
                !vertical_inline_parent_for_placement &&
                !form_control_line_item) {
                block->x = layout_rtl_inline_item_x(
                    &lycon->line, margin_box_width);
            }
            if (lycon->block.direction == CSS_VALUE_RTL && form_control_line_item) {
                block->x = lycon->line.left;
                layout_shift_preceding_inline_line_views(
                    lycon, static_cast<View*>(block), margin_box_width);
            }
            block->inline_line_number = lycon->block.line_number;
            bool has_explicit_valign = (block->in_line && block->inl()->vertical_align);
            bool is_inline_table = (display.inner == CSS_VALUE_TABLE);
            BoxMetrics inline_block_box = layout_box_metrics(block);
            // CSS 2.1 §17.5.1: inline-table baseline = baseline of the first row
            float table_baseline = -1;
            if (is_inline_table) {
                bool prefers_last = radiant::layout_prefers_last_baseline(block, false);
                table_baseline = layout_table_baseline_for_source(
                    lycon, block, prefers_last);
            }
            {
                CssEnum valign = has_explicit_valign ?
                    block->inl()->vertical_align : CSS_VALUE_BASELINE;
                float valign_offset = (block->in_line) ?
                    block->inl()->vertical_align_offset : 0;
                float item_height = layout_inline_atomic_extent(lycon, block);
                if (!is_broken_alt_image) {
                    lycon->line.max_atomic_inline_height = max(
                        lycon->line.max_atomic_inline_height, item_height);
                }
                // CSS 2.1 §17.5.1: inline-table baseline = first-row baseline
                bool overflow_visible = !block->scroller ||
                    (block->scroll()->overflow_x == CSS_VALUE_VISIBLE &&
                     block->scroll()->overflow_y == CSS_VALUE_VISIBLE);
                float item_baseline;
                if (is_inline_table && table_baseline >= 0) {
                    item_baseline = inline_block_box.margin.top + table_baseline;
                } else if (block->tag() == MARKUP_NAME_TEXTAREA &&
                           textarea_uses_explicit_baseline_source) {
                    item_baseline = inline_block_box.margin.top + content_last_line_ascender;
                } else if (block->tag() == MARKUP_NAME_TEXTAREA && overflow_visible) {
                    item_baseline = block->height + inline_block_box.margin.top;
                } else if (!layout_inline_box_is_orthogonal_to_parent(block) &&
                           content_has_line_boxes &&
                           (overflow_visible || radiant::layout_uses_explicit_baseline_source(block))) {
                    // CSS Writing Modes synthesizes an orthogonal inline-block
                    item_baseline = inline_block_box.margin.top + content_last_line_ascender;
                } else if (block->display.inner == RDT_DISPLAY_REPLACED && overflow_visible) {
                    item_baseline = block->height + inline_block_box.margin.top;
                } else {
                    item_baseline = item_height;
                }
                // CSS 2.1 §10.8.1: Use the strut's baseline as a reference for alignment
                float baseline_ref = lycon->line.max_ascender > 0 ?
                    lycon->line.max_ascender :
                    lycon->block.init_ascender + lycon->block.lead_y;
                float line_height = max(lycon->block.line_height, baseline_ref + lycon->line.max_descender);
                float offset = calculate_vertical_align_offset(
                    lycon, valign, item_height, line_height,
                    baseline_ref, item_baseline, valign_offset);
                if (!is_broken_alt_image && valign != CSS_VALUE_TOP && valign != CSS_VALUE_BOTTOM) {
                    float asc_contribution, desc_contribution;
                    if (valign == CSS_VALUE_MIDDLE) {
                        // CSS 2.1 §10.8.1: "Align the vertical midpoint of the box with
                        float x_height_half;
                        float parent_font_size = lycon->line.parent_font_size;
                        if (parent_font_size <= 0.0f && lycon->font.style) {
                            parent_font_size = lycon->font.style->font_size;
                        }
                        if (parent_font_size <= 0.0f) {
                            parent_font_size = lycon->block.init_ascender + lycon->block.init_descender;
                        }
                        if (font_box_handle(&lycon->font)) {
                            float x_ratio = font_get_x_height_ratio(font_box_handle(&lycon->font));
                            float x_height_font_size = lycon->font.current_font_size > 0.0f
                                ? lycon->font.current_font_size : parent_font_size;
                            x_height_half = x_height_font_size * x_ratio / 2.0f;
                        } else {
                            x_height_half = parent_font_size * 0.25f;
                        }
                        asc_contribution = item_height / 2.0f + x_height_half;
                        desc_contribution = item_height / 2.0f - x_height_half;
                    } else if (valign == CSS_VALUE_TEXT_TOP) {
                        asc_contribution = lycon->line.parent_font_ascender;
                        desc_contribution = item_height - asc_contribution;
                    } else if (valign == CSS_VALUE_TEXT_BOTTOM) {
                        desc_contribution = lycon->line.parent_font_descender;
                        asc_contribution = item_height - desc_contribution;
                    } else {
                        float baseline_shift = vertical_align_baseline_shift(
                            lycon, valign, valign_offset);
                        asc_contribution = item_baseline + baseline_shift;
                        desc_contribution = item_height - item_baseline - baseline_shift;
                        ViewBlock* line_container = lycon->line.start_view
                            ? layout_nearest_block_ancestor(lycon->line.start_view->parent_view())
                            : nullptr;
                        bool line_container_is_inline = line_container &&
                            (line_container->display.outer == CSS_VALUE_INLINE ||
                             line_container->display.outer == CSS_VALUE_INLINE_BLOCK);
                        bool quirks_mode = lycon->doc && lycon->doc->view_tree &&
                            is_quirks_mode(lycon->doc->view_tree->html_version);
                        if (block->tag() == MARKUP_NAME_CANVAS &&
                            (!quirks_mode || line_container_is_inline)) {
                            desc_contribution = max(desc_contribution,
                                item_height - item_baseline +
                                lycon->block.init_descender - baseline_shift);
                        }
                    }
                    lycon->line.max_ascender = max(lycon->line.max_ascender, asc_contribution);
                    lycon->line.max_descender = max(lycon->line.max_descender, desc_contribution);
                    float updated_baseline_ref = lycon->line.max_ascender;
                    float updated_line_height = max(lycon->block.line_height,
                        updated_baseline_ref + lycon->line.max_descender);
                    offset = calculate_vertical_align_offset(
                        lycon, valign, item_height, updated_line_height,
                        updated_baseline_ref, item_baseline, valign_offset);
                }
                block->y = lycon->block.advance_y + max(offset, 0.0f);  // block->boundary()->margin.top will be added below
                ViewBlock* parent_line_block =
                    layout_nearest_block_ancestor(block->parent_view());
                View* parent_view = block->parent_view();
                ViewSpan* parent_inline_span = parent_view &&
                    parent_view->view_type == RDT_VIEW_INLINE
                    ? lam::view_require<RDT_VIEW_INLINE>(parent_view) : nullptr;
                bool parent_uses_text_top_baseline = parent_inline_span &&
                    parent_inline_span->blk &&
                    parent_inline_span->block()->dominant_baseline == CSS_VALUE_TEXT_TOP;
                parent_uses_text_top_baseline = parent_uses_text_top_baseline ||
                    (parent_line_block &&
                    parent_line_block->blk &&
                    parent_line_block->block()->dominant_baseline == CSS_VALUE_TEXT_TOP);
                if (layout_inline_box_is_orthogonal_to_parent(block) &&
                    parent_uses_text_top_baseline) {
                    block->y = lycon->block.advance_y;
                }
                if (layout_zero_sized_atomic_in_vertical_lr(block)) {
                    float line_cross_size = lycon->block.line_height > 0.0f
                        ? lycon->block.line_height
                        : lycon->block.init_ascender + lycon->block.init_descender;
                    block->x = lycon->line.left + line_cross_size / 2.0f;
                    block->y = lycon->block.advance_y;
                }
            }
            bool vertical_inline_parent = vertical_inline_parent_for_placement;
            // CSS Writing Modes maps a vertical inline cursor to physical y;
            lycon->line.advance_x += vertical_inline_parent ? block->height : block->width;
            block->x += inline_block_box.margin.left;
            block->y += inline_block_box.margin.top;
            if (vertical_inline_parent) {
                lycon->line.advance_x += inline_block_box.margin_v;
            } else {
                lycon->line.advance_x += inline_block_box.margin_h;
            }
            // zero; otherwise following collapsible whitespace is misclassified
            // as line-start whitespace and removed (CSS Inline 3 §5).
            lycon->line.is_line_start = false;
            recompute_inline_descendant_bounds(static_cast<View*>(block), font_box_handle(&lycon->font));
            // CSS Flexbox §4.1: Re-resolve abs-pos children of inline-level flex/grid
            if (dom_elem && (display.inner == CSS_VALUE_FLEX || display.inner == CSS_VALUE_GRID)) {
                bool is_containing_block = block->position &&
                    (block->positionp()->position == CSS_VALUE_RELATIVE ||
                     block->positionp()->position == CSS_VALUE_ABSOLUTE ||
                     block->positionp()->position == CSS_VALUE_FIXED ||
                     block->positionp()->position == CSS_VALUE_STICKY);
                if (!is_containing_block) {
                DomNode* fc = dom_elem->first_child;
                bool has_abs_children = false;
                while (fc && !has_abs_children) {
                    if (fc->is_element()) {
                        ViewBlock* cb = lam::view_as_block(static_cast<View*>(fc->as_element()));
                        if (layout_block_is_out_of_flow_positioned(cb)) {
                            has_abs_children = true;
                        }
                    }
                    fc = fc->next_sibling;
                }
                if (has_abs_children) {
                    float container_to_cb_x = 0, container_to_cb_y = 0;
                    ViewBlock* p = block;
                    while (p) {
                        container_to_cb_x += p->x;
                        container_to_cb_y += p->y;
                        ViewElement* gp = p->parent_view();
                        while (gp && !gp->is_block()) gp = gp->parent_view();
                        if (!gp || !gp->is_block()) break;
                        p = lam::view_require_block(gp);
                        if (p->position &&
                            (p->positionp()->position == CSS_VALUE_RELATIVE ||
                             p->positionp()->position == CSS_VALUE_ABSOLUTE ||
                             p->positionp()->position == CSS_VALUE_FIXED ||
                             p->positionp()->position == CSS_VALUE_STICKY)) {
                            break;  // reached the containing block
                        }
                    }
                    fc = dom_elem->first_child;
                    while (fc) {
                        if (fc->is_element()) {
                            ViewBlock* cb = lam::view_require_block(static_cast<View*>(fc->as_element()));
                            if (layout_block_is_out_of_flow_positioned(cb)) {
                                if (!cb->positionp()->has_left && !cb->positionp()->has_right) {
                                    cb->x += container_to_cb_x;
                                    cb->position->static_x_needs_parent_offset = false;
                                    cb->position->has_static_parent_offset_x = true;
                                    cb->position->static_parent_offset_x = container_to_cb_x;
                                }
                                if (!cb->positionp()->has_top && !cb->positionp()->has_bottom) {
                                    cb->y += container_to_cb_y;
                                    cb->position->static_y_needs_parent_offset = false;
                                    cb->position->has_static_parent_offset_y = true;
                                    cb->position->static_parent_offset_y = container_to_cb_y;
                                }
                            }
                        }
                        fc = fc->next_sibling;
                    }
                }
                } // !is_containing_block
            }
            if (!is_broken_alt_image) {
                lycon->line.has_replaced_content = true;  // inline-block contributes to line box
                lycon->line.atomic_inline_count++;
            }
            // CSS 2.1 §10.8.1: vertical-align defaults to 'baseline' (CSS_VALUE__UNDEF=0 also means baseline).
            bool has_non_baseline_valign = block->in_line &&
                block->inl()->vertical_align != 0 &&
                block->inl()->vertical_align != CSS_VALUE_BASELINE;
            if (has_non_baseline_valign) {
                float block_flow_height = block->height + inline_block_box.margin_v;
                if (block->inl()->vertical_align == CSS_VALUE_TEXT_TOP) {
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->block.init_ascender);
                }
                else if (block->inl()->vertical_align == CSS_VALUE_TEXT_BOTTOM) {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, block_flow_height - lycon->block.init_descender);
                }
                else if (block->inl()->vertical_align == CSS_VALUE_TOP) {
                    // CSS 2.1 §10.8.1: vertical-align:top/bottom elements don't participate
                    lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, block_flow_height);
                    lycon->line.max_top_height = max(lycon->line.max_top_height, block_flow_height);
                }
                else if (block->inl()->vertical_align == CSS_VALUE_BOTTOM) {
                    // CSS 2.1 §10.8.1: Same second-pass treatment as vertical-align:top.
                    lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, block_flow_height);
                    lycon->line.max_bottom_height = max(lycon->line.max_bottom_height, block_flow_height);
                }
                else {
                    lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->line.max_ascender);
                }
            } else {
                // CSS 2.1 Section 10.8.1:
                // CSS 2.1 §17.5.1: inline-table baseline = baseline of first row
                bool overflow_visible = !block->scroller ||
                    (block->scroll()->overflow_x == CSS_VALUE_VISIBLE &&
                     block->scroll()->overflow_y == CSS_VALUE_VISIBLE);
                bool uses_content_baseline =
                    (!layout_inline_box_is_orthogonal_to_parent(block) &&
                     content_has_line_boxes &&
                     (overflow_visible || radiant::layout_uses_explicit_baseline_source(block))) ||
                    (is_inline_table && table_baseline >= 0);
                float effective_baseline = content_last_line_ascender;
                if (is_inline_table && table_baseline >= 0) {
                    effective_baseline = table_baseline;
                }
                if (!is_broken_alt_image && uses_content_baseline) {
                    lycon->line.max_ascender = max(lycon->line.max_ascender, effective_baseline +
                        inline_block_box.margin.top);
                    float descender_part = block->height - effective_baseline +
                        inline_block_box.margin.bottom;
                    if (block->tag() == MARKUP_NAME_TEXTAREA &&
                        layout_block_inline_axis_is_vertical(block) &&
                        radiant::layout_uses_explicit_baseline_source(block) &&
                        block->block()->baseline_source == CSS_VALUE_LAST &&
                        block->form &&
                        block->form->last_text_baseline_overflow > 0.0f) {
                        // CSS Box Alignment clamps the selected baseline, but
                        float strut_extent = max(lycon->block.line_height,
                            lycon->line.parent_font_size);
                        float clamped_tail = strut_extent * 0.5f;
                        lycon->line.clamped_baseline_tail = max(
                            lycon->line.clamped_baseline_tail, clamped_tail);
                        lycon->line.has_clamped_baseline_tail = true;
                        descender_part = max(descender_part, clamped_tail);
                    }
                    lycon->line.max_descender = max(lycon->line.max_descender, descender_part);
                    // CSS 2.1 §10.8.1: The block container's strut is present
                    // below-baseline extent so an inline-table cannot collapse
                    if (!form_control_margin_baseline) {
                        lycon->line.max_descender = max(
                            lycon->line.max_descender,
                            layout_strut_below_baseline(lycon));
                    }
                } else {
                    // CSS 2.1 §10.8.1 says the baseline is the bottom MARGIN edge,
                    bool is_replaced_inline = (block->display.inner == RDT_DISPLAY_REPLACED);
                    bool replaced_uses_margin_edge_baseline = is_replaced_inline &&
                        !overflow_visible;
                    if (block->bound) {
                        if (is_replaced_inline) {
                            float baseline_extent = replaced_uses_margin_edge_baseline
                                ? block->height + inline_block_box.margin_v
                                : block->height + inline_block_box.margin.top;
                            lycon->line.max_ascender = max(lycon->line.max_ascender,
                                baseline_extent);
                            if (!replaced_uses_margin_edge_baseline &&
                                inline_block_box.margin.bottom > 0) {
                                lycon->line.max_descender = max(lycon->line.max_descender,
                                    inline_block_box.margin.bottom);
                            }
                        } else {
                            lycon->line.max_ascender = max(lycon->line.max_ascender,
                                block->height + inline_block_box.margin_v);
                        }
                    }
                    else {
                        lycon->line.max_ascender = max(lycon->line.max_ascender, block->height);
                    }
                    // CSS 2.1 §10.8.1: The strut defines minimum height above and
                    if (!form_control_margin_baseline) {
                        lycon->line.max_descender = max(
                            lycon->line.max_descender,
                            layout_strut_below_baseline(lycon));
                    }
                }
            }
            lycon->line.max_desc_before_last_text = max(lycon->line.max_desc_before_last_text,
                lycon->line.max_descender);
            lycon->line.reset_space();
        }
        else { // normal block
            // CSS 2.1 §8.3.1: Propagate first-child margin through pass-through blocks.
            {
                float stored_mt = block->bound ? block->boundary()->margin.top : 0;
                float y_shift = block->y - pa_block.advance_y;
                float unaccounted = y_shift - stored_mt;
                if (fabsf(unaccounted) > 0.01f) {
                    ViewBlock* gp = layout_nearest_block_ancestor(block->parent_view());
                    if (gp) {
                        View* first = gp->first_placed_child();
                        while (first) {
                            if (first->view_type == RDT_VIEW_MARKER) {
                                first = static_cast<View*>(first->next_sibling);
                                first = layout_first_view_with_type(first);
                                continue;
                            }
                            if (!first->is_block()) {
                                if (first->height > 0) break;
                                if (first->view_type == RDT_VIEW_INLINE &&
                                    is_inline_substantial(lam::view_require_element(first))) break;
                                first = static_cast<View*>(first->next_sibling);
                                first = layout_first_view_with_type(first);
                                continue;
                            }
                            ViewBlock* fvb = lam::view_require_block(first);
                            if (fvb->position && element_has_float(fvb)) {
                                first = static_cast<View*>(first->next_sibling);
                                first = layout_first_view_with_type(first);
                                continue;
                            }
                            break;
                        }
                        if (first == static_cast<View*>(block)) {
                            if (!block->bound) {
                                block->ensure_boundary(lycon);
                            }
                            block->boundary_mut()->margin.top = y_shift;
                        }
                    }
                }
            }
            bool is_float = block->position && element_has_float(block);
            if (is_float) {
                if (block->bound) {
                    lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width
                        + block->boundary()->margin.left + block->boundary()->margin.right);
                } else {
                    lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + block->width);
                }
            } else if (block->bound) {
                // Skip floats AND empty zero-height blocks (CSS 2.2 Section 8.3.1)
                DomElement* shadow_formatting_parent =
                    layout_shadow_formatting_parent((DomNode*)block);
                bool first_shadow_child = shadow_formatting_parent &&
                    layout_rendered_first_child_node(
                        shadow_formatting_parent->shadow_root_element()) == (DomNode*)block;
                ViewElement* collapse_parent_view = shadow_formatting_parent
                    ? (ViewElement*)shadow_formatting_parent : block->parent_view();
                View* first_in_flow_child = first_shadow_child
                    ? static_cast<View*>(block)
                    : (collapse_parent_view
                        ? collapse_parent_view->first_placed_child() : nullptr);
                while (first_in_flow_child) {
                    if (first_in_flow_child->view_type == RDT_VIEW_MARKER) {
                        View* next = static_cast<View*>(first_in_flow_child->next_sibling);
                        next = layout_first_view_with_type(next);
                        first_in_flow_child = next;
                        continue;
                    }
                    if (!first_in_flow_child->is_block()) {
                        // CSS 2.1 §8.3.1 + CSS Inline 3 §2.1: Only real (non-phantom)
                        if (first_in_flow_child->view_type == RDT_VIEW_INLINE &&
                            !is_inline_substantial(lam::view_require_element(first_in_flow_child))) {
                            View* next = static_cast<View*>(first_in_flow_child->next_sibling);
                            next = layout_first_view_with_type(next);
                            first_in_flow_child = next;
                            continue;
                        }
                        break;
                    }
                    ViewBlock* vb = lam::view_require_block(first_in_flow_child);
                    // CSS 2.1 §9.4.1: Out-of-flow elements don't participate in
                    if (layout_block_is_out_of_flow(vb)) {
                        View* next = static_cast<View*>(first_in_flow_child->next_sibling);
                        next = layout_first_view_with_type(next);
                        first_in_flow_child = next;
                        continue;
                    }
                    // CSS 2.1 §8.3.1: Self-collapsing blocks (height=0, no border/padding) with margins
                    if (vb->height == 0) {
                        // CSS 2.1 §8.3.1 + §9.5.2: Never skip blocks with a clear property.
                        bool has_clear_prop = vb->position &&
                            (vb->positionp()->clear == CSS_VALUE_LEFT ||
                             vb->positionp()->clear == CSS_VALUE_RIGHT ||
                             vb->positionp()->clear == CSS_VALUE_BOTH);
                        if (has_clear_prop) {
                            break;
                        }
                        // CSS 2.1 §17 + §8.3.1: Tables and other BFC-establishing elements
                        if (!layout_block_is_self_collapsing(vb)) {
                            break;
                        }
                        BoxEdges border = layout_boundary_border_edges(
                            vb->bound ? vb->boundary() : nullptr);
                        BoxEdges padding = layout_boundary_padding_edges(
                            vb->bound ? vb->boundary() : nullptr);
                        BoxEdges margin = layout_boundary_margin_edges(
                            vb->bound ? vb->boundary() : nullptr);
                        float border_top = border.top;
                        float border_bottom = border.bottom;
                        float padding_top = padding.top;
                        float padding_bottom = padding.bottom;
                        float margin_top_val = margin.top;
                        float margin_bottom_val = margin.bottom;
                        bool has_chain_margins = vb->bound && has_margin_chain(vb->bound);
                        if (border_top == 0 && border_bottom == 0 && padding_top == 0 && padding_bottom == 0
                            && margin_top_val == 0 && margin_bottom_val == 0 && !has_chain_margins) {
                            View* next = static_cast<View*>(first_in_flow_child->next_sibling);
                            next = layout_first_view_with_type(next);
                            first_in_flow_child = next;
                            continue;
                        }
                    }
                    break;
                }
                bool parent_child_collapsed = false;
                if (first_in_flow_child == block) {  // first in-flow child
                    // CSS Box 4 §margin-trim: block-start trims the first child's block-start
                    // CSS 2.1 §9.5.2: If clearance was applied (saved_clear_y >= 0),
                    bool has_clearance = (lycon->block.saved_clear_y >= 0);
                    ViewBlock* parent = nullptr;
                    if (first_shadow_child) {
                        parent = collapse_parent_view && collapse_parent_view->is_block()
                            ? lam::view_require_block(collapse_parent_view) : nullptr;
                    } else {
                        parent = layout_nearest_block_ancestor(block->parent_view());
                    }
                    bool parent_creates_bfc = parent && block_context_establishes_bfc(parent);
                    float parent_decoration_top = layout_axis_decoration_start(
                        parent && parent->bound ? parent->boundary() : nullptr, LAYOUT_AXIS_Y);
                    float parent_margin_top = parent && parent->bound ? parent->boundary()->margin.top : 0;
                    // CSS 2.1 §8.3.1: Parent-child top margin collapse.
                    bool first_child_self_collapsing = layout_block_is_self_collapsing(block);
                    bool has_self_collapsing_margins = first_child_self_collapsing &&
                        (block->boundary()->margin.bottom != 0 || has_margin_chain(block->bound));
                    bool stretch_margin_after_float_avoidance =
                        layout_axis_uses_stretch_size(block->blk, LAYOUT_AXIS_Y) &&
                        !layout_block_inline_axis_is_vertical(block) &&
                        block->block()->bfc_float_avoidance_shift_y > 0.0f;
                    if (!has_clearance && parent_margin_collapse_uses_physical_y(block) &&
                        (block->boundary()->margin.top != 0 || has_self_collapsing_margins)) {
                        // CSS 2.1 §9.5.2: If the PARENT had clearance applied, the parent's
                        bool parent_has_clearance = parent &&
                            margin_collapse_ancestor_has_clearance(parent);
                            bool quirky_container = is_quirky_container(parent, lycon);
                        if (parent && parent->parent && !is_root_element_block(parent) &&
                            !parent_creates_bfc &&
                            !layout_inline_has_prior_in_flow_content(
                                static_cast<DomNode*>(block->parent_view())) &&
                            parent_decoration_top == 0) {
                            float child_mt = (quirky_container && has_quirky_margin(block, true))
                                ? 0 : block->boundary()->margin.top;
                            if (stretch_margin_after_float_avoidance) {
                                // CSS Sizing 4 stretch-fit clearance treats the
                                child_mt = 0.0f;
                            }
                            // CSS 2.1 §8.3.1: collapse child and parent margins
                            float margin_top = collapse_margins(child_mt, parent_margin_top);
                            // CSS 2.1 §8.3.1: For self-collapsing first children, both mt and mb
                            if (first_child_self_collapsing) {
                                // CSS 2.1 §8.3.1: Use chain-aware 3-way collapse to avoid
                            float child_mb = (quirky_container && has_quirky_margin(block, false))
                                    ? 0 : block->boundary()->margin.bottom;
                                float chain_pos = max(max(child_mt, parent_margin_top), 0.f);
                                chain_pos = max(chain_pos, max(child_mb, 0.f));
                                float chain_neg = min(min(child_mt, parent_margin_top), 0.f);
                                chain_neg = min(chain_neg, min(child_mb, 0.f));
                                // CSS 2.1 §8.3.1: If child has a margin chain from its own
                                if (has_margin_chain(block->bound)) {
                                    chain_pos = max(chain_pos, block->boundary()->margin_chain_positive);
                                    chain_neg = min(chain_neg, block->boundary()->margin_chain_negative);
                                }
                                margin_top = chain_pos + chain_neg;
                            }
                            // CSS 2.1 §8.3.1: Parent-child collapse propagates the child's
                            if (!parent_has_clearance) {
                                float y_delta = margin_top - parent_margin_top;
                                if (!stretch_margin_after_float_avoidance) {
                                    parent->y += y_delta;
                                }
                                // CSS 2.1 §8.3.1: When parent-child margin collapse shifts
                                if (y_delta != 0) {
                                    BlockContext* float_bfc = block_context_find_bfc(&lycon->block);
                                    if (float_bfc) {
                                        ViewElement* child_view = lam::view_require_element(block);
                                        shift_margin_collapse_floats(float_bfc->left_floats, parent,
                                            child_view, y_delta, block->source_loc());
                                        shift_margin_collapse_floats(float_bfc->right_floats, parent,
                                            child_view, y_delta, block->source_loc());
                                        block_context_recompute_lowest_float_bottom(float_bfc);
                                    }
                                }
                                // CSS 2.1 §8.3.1: Ensure parent->bound exists so margin.top
                                if (!parent->bound) {
                                    parent->ensure_boundary(lycon);
                                }
                                parent->boundary_mut()->margin.top = margin_top;
                            }
                            block->y = stretch_margin_after_float_avoidance
                                ? block->block()->bfc_float_avoidance_shift_y : 0.0f;
                            block->boundary_mut()->margin.top = 0;
                            parent_child_collapsed = true;
                            // CSS 2.1 §8.3.1: Track chain components (max positive, most negative)
                            if (first_child_self_collapsing) {
                                float sc_mt = (quirky_container && has_quirky_margin(block, true))
                                    ? 0 : original_margin_top;
                                float sc_mb = (quirky_container && has_quirky_margin(block, false))
                                    ? 0 : block->boundary()->margin.bottom;
                                float chain_pos = max(max(sc_mt, parent_margin_top), 0.f);
                                chain_pos = max(chain_pos, max(sc_mb, 0.f));
                                float chain_neg = min(min(sc_mt, parent_margin_top), 0.f);
                                chain_neg = min(chain_neg, min(sc_mb, 0.f));
                                // CSS 2.1 §8.3.1: Incorporate child's existing margin chain
                                if (has_margin_chain(block->bound)) {
                                    chain_pos = max(chain_pos, block->boundary()->margin_chain_positive);
                                    chain_neg = min(chain_neg, block->boundary()->margin_chain_negative);
                                }
                                set_margin_chain(block->bound, chain_pos, chain_neg);
                            }
                        }
                    }
                }
                else {
                    // CSS 2.1 §9.5.2: If clearance was applied (saved_clear_y >= 0),
                    bool has_clearance = (lycon->block.saved_clear_y >= 0);
                    if (!has_clearance && parent_margin_collapse_uses_physical_y(block)) {
                        float collapse = sibling_margin_collapsed_before_layout
                            ? 0.0f : sibling_margin_collapse_amount(block);
                        if (collapse != 0) {
                            block->y -= collapse;
                            block->boundary_mut()->margin.top -= collapse;
                            // CSS 2.1 §10.6.4: Adjust abs-pos descendants whose static
                            if (!block->position || block->positionp()->position == CSS_VALUE_STATIC) {
                                adjust_abs_descendants_y(lam::view_require_element(block), -collapse);
                            }
                            // CSS 2.1 §10.6.7: Float boxes in the parent BFC were
                            shift_descendant_float_boxes(
                                block_context_find_bfc(&lycon->block), block, -collapse);
                        }
                    }
                }
                // CSS 2.2 Section 8.3.1: Self-collapsing blocks
                // CSS 2.1 §8.3.1: Elements with clearance CAN be self-collapsing.
                bool is_self_collapsing = layout_block_is_self_collapsing(block);
                // CSS 2.1 §8.3.1: Track if this block (or its margin chain) includes
                bool block_has_clearance = (lycon->block.saved_clear_y >= 0);
                if (is_self_collapsing && parent_margin_collapse_uses_physical_y(block)) {
                    if (!parent_child_collapsed) {
                        // CSS 2.1 §8.3.1: The element's own margins merge via collapse_margins
                        ViewBlock* prev_block_for_chain =
                            find_previous_margin_collapse_block(block);
                        float prev_mb = prev_block_for_chain
                            ? prev_block_for_chain->boundary()->margin.bottom : 0.0f;
                        bool prev_has_clearance_chain = prev_block_for_chain &&
                            prev_block_for_chain->boundary()->clearance_in_margin_chain;
                        float self_collapsed = collapse_margins(original_margin_top, block->boundary()->margin.bottom);
                        float new_pending, contribution;
                        float combined_pos = 0.0f;
                        float combined_neg = 0.0f;
                        if (block_has_clearance) {
                            // CSS 2.1 §8.3.1 + §9.5.2: Clearance separates this element's
                            new_pending = self_collapsed;
                            contribution = 0.0f;
                        } else {
                            // CSS 2.1 §8.3.1: Multi-way collapse using chain components.
                            float prev_pos = 0.0f, prev_neg = 0.0f;
                            get_margin_chain(prev_block_for_chain, &prev_pos, &prev_neg);
                            float cur_pos, cur_neg;
                            get_self_margin_chain(block, original_margin_top, &cur_pos, &cur_neg);
                            bool use_chain = (prev_neg < 0 || cur_neg < 0 ||
                                              has_margin_chain(block->bound) ||
                                              (prev_block_for_chain && has_margin_chain(prev_block_for_chain->bound)));
                            if (use_chain) {
                                combined_pos = max(prev_pos, cur_pos);
                                combined_neg = min(prev_neg, cur_neg);
                                new_pending = combined_pos + combined_neg;
                                // CSS 2.1 §8.3.1: contribution can undo up to prev_mb
                                // (which is already baked into advance_y), but cannot push
                                contribution = max(0.f, new_pending) - prev_mb;
                            } else {
                                new_pending = collapse_margins(prev_mb, self_collapsed);
                                contribution = max(0.f, new_pending - prev_mb);
                            }
                        }
                        bool first_self_collapsing_negative_margin =
                            !prev_block_for_chain && new_pending < 0.0f &&
                            margin_collapse_has_separating_content_after(
                                static_cast<View*>(block), true);
                        if (first_self_collapsing_negative_margin) {
                            // a negative first-child margin must move a following sibling;
                            // without one, propagating it would make the parent auto-height negative.
                            contribution = new_pending;
                        }
                        lycon->block.advance_y += contribution;
                        // CSS 2.1 §8.3.1: When chain collapse produces a negative contribution,
                        if (contribution < 0) {
                            if (!first_self_collapsing_negative_margin) {
                                block->y += contribution;
                            }
                            // CSS 2.1 §10.6.4: Adjust abs-pos descendants whose static
                            if (!block->position || block->positionp()->position == CSS_VALUE_STATIC) {
                                adjust_abs_descendants_y(lam::view_require_element(block), contribution);
                            }
                        }
                        if (block_has_clearance) {
                            block->boundary_mut()->margin.bottom = new_pending;
                        } else {
                            if (combined_pos == 0.0f && combined_neg == 0.0f) {
                                float prev_pos = 0.0f, prev_neg = 0.0f;
                                get_margin_chain(prev_block_for_chain, &prev_pos, &prev_neg);
                                float cur_pos = 0.0f, cur_neg = 0.0f;
                                get_self_margin_chain(block, original_margin_top, &cur_pos, &cur_neg);
                                combined_pos = max(prev_pos, cur_pos);
                                combined_neg = min(prev_neg, cur_neg);
                            }
                            set_margin_chain(block->bound, combined_pos, combined_neg);
                        }
                        // CSS 2.1 §8.3.1: Propagate clearance flag through the margin chain.
                        // included a cleared element, the resulting margin must not collapse
                        if (block_has_clearance || prev_has_clearance_chain) {
                            block->bound->clearance_in_margin_chain = true;
                        }
                    }
                } else {
                    lycon->block.advance_y += block->height + block->boundary()->margin.top + block->boundary()->margin.bottom;
                }
                float child_w = block->width;
                if (lycon->block.given_width < 0
                    && (!block->blk || (block->block()->given_width < 0 && isnan(block->block()->given_width_percent)))) {
                    child_w = block->content_width;
                    if (block->boundary()->border) {
                        child_w += block->boundary()->border->width.right;
                    }
                }
                lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + child_w
                    + block->boundary()->margin.left + block->boundary()->margin.right);
            } else {
                lycon->block.advance_y = block->y + block->height;
                float child_w_nb = block->width;
                if (lycon->block.given_width < 0
                    && (!block->blk || (block->block()->given_width < 0 && isnan(block->block()->given_width_percent)))
                    && block->content_width < block->width) {
                    child_w_nb = block->content_width;
                }
                lycon->block.max_width = max(lycon->block.max_width, lycon->line.left + child_w_nb);
            }
            if (!is_float) {
                assert(lycon->line.is_line_start);
            }
            // CSS 2.1 §10.8.1: Propagate last line baseline from block children
            if (!is_float) {
                float child_first_line_baseline = block->blk
                    ? block->block()->first_line_baseline : 0.0f;
                float child_last_line_baseline = block->blk
                    ? block->block()->last_line_baseline : 0.0f;
                if (block->view_type == RDT_VIEW_TABLE) {
                    child_first_line_baseline = find_first_baseline_recursive(
                        lycon, static_cast<View*>(block), 0.0f, true);
                    child_last_line_baseline = block->block()->last_line_baseline;
                }
                if (lycon->block.first_line_ascender == 0 &&
                    child_first_line_baseline > 0) {
                    lycon->block.first_line_ascender = block->y + child_first_line_baseline;
                }
                if (block->view_type == RDT_VIEW_TABLE &&
                    child_last_line_baseline > 0) {
                    content_last_line_ascender = child_last_line_baseline;
                }
                if (content_last_line_ascender > 0) {
                    lycon->block.last_line_ascender = block->y + content_last_line_ascender;
                }
            }
            if (!is_float && child_line_clamp_inherited) {
                lycon->block.line_number = child_line_number;
                if (child_line_clamped && !lycon->block.line_clamped &&
                    child_line_clamp_advance_y >= 0.0f) {
                    lycon->block.line_clamped = true;
                    lycon->block.line_clamp_advance_y = block->y + child_line_clamp_advance_y;
                    lycon->block.line_clamp_last_line_ascender =
                        block->y + child_line_clamp_last_line_ascender;
                    lycon->block.line_clamp_last_line_max_ascender =
                        child_line_clamp_last_line_max_ascender;
                    lycon->block.line_clamp_last_line_max_descender =
                        child_line_clamp_last_line_max_descender;
                }
            }
        }
        // CSS 2.1 §9.4.3: For inline-blocks, relative positioning is deferred to
        if (block->position && block->positionp()->position == CSS_VALUE_RELATIVE
            && block->view_type != RDT_VIEW_INLINE_BLOCK) {
            layout_relative_positioned(lycon, block);
        } else if (block->position && block->positionp()->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, block);
        }
    }
    // propagate_resets=true for regular elements (sibling visibility per CSS 2.1 §12.4.1)
    if (lycon->counter_context) {
        counter_pop_scope_propagate(lycon->counter_context, true);
    }
    if (!has_custom_layout) {
        radiant::SizeF result = radiant::size_f(block->width, block->height);
        radiant::layout_pass_cache_store(lycon, dom_elem, known_dims, result, "BLOCK");
    }
    log_leave();
    auto t_block_end = high_resolution_clock::now();
    double block_ms = duration<double, std::milli>(t_block_end - t_block_start).count();
    g_block_layout_time += block_ms;
    g_block_layout_count++;
    if (block_ms > 50.0) {
        log_warn("SLOW BLOCK: %s took %.0fms (count=%d)", elmt->source_loc(), block_ms, layout_block_count);
    }
}
