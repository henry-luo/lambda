#include "view.hpp"
#include "layout.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/math_utils.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RadiantCssomUsedValueSync s_cssom_used_value_sync = nullptr;

size_t font_text_decoration_names(const FontProp* font, char* out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    out[0] = '\0';
    bool underline = font && (font->text_deco == CSS_VALUE_UNDERLINE ||
        (font->text_deco_extra & CSS_TEXT_DECO_UNDERLINE));
    bool overline = font && (font->text_deco == CSS_VALUE_OVERLINE ||
        (font->text_deco_extra & CSS_TEXT_DECO_OVERLINE));
    bool line_through = font && (font->text_deco == CSS_VALUE_LINE_THROUGH ||
        (font->text_deco_extra & CSS_TEXT_DECO_LINE_THROUGH));
    bool blink = font && (font->text_deco == CSS_VALUE_BLINK ||
        (font->text_deco_extra & CSS_TEXT_DECO_BLINK));
    size_t used = 0;
    const char* names[] = {"underline", "overline", "line-through", "blink"};
    bool selected[] = {underline, overline, line_through, blink};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (!selected[i]) continue;
        if (used) used = str_cat(out, used, capacity, " ", 1);
        used = str_cat(out, used, capacity, names[i], strlen(names[i]));
    }
    if (!used) used = str_copy(out, capacity, "none", 4);
    return used;
}

static const CssPropertyRuntimeMetadata kCssPropertyRuntimeMetadata[] = {
    {CSS_PROPERTY_OPACITY, ANIM_VAL_FLOAT, true, false},
    {CSS_PROPERTY_TRANSFORM, ANIM_VAL_TRANSFORM, false, false},
    {CSS_PROPERTY_BACKGROUND_COLOR, ANIM_VAL_COLOR, true, false},
    {CSS_PROPERTY_COLOR, ANIM_VAL_COLOR, true, false},
    {CSS_PROPERTY_BORDER_TOP_COLOR, ANIM_VAL_COLOR, true, false},
    {CSS_PROPERTY_BORDER_RIGHT_COLOR, ANIM_VAL_COLOR, true, false},
    {CSS_PROPERTY_BORDER_BOTTOM_COLOR, ANIM_VAL_COLOR, true, false},
    {CSS_PROPERTY_BORDER_LEFT_COLOR, ANIM_VAL_COLOR, true, false},
    {CSS_PROPERTY_WIDTH, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_HEIGHT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_MIN_WIDTH, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_MAX_WIDTH, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_MIN_HEIGHT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_MAX_HEIGHT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_TOP, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_RIGHT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_BOTTOM, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_LEFT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_MARGIN_TOP, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_MARGIN_RIGHT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_MARGIN_BOTTOM, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_MARGIN_LEFT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_PADDING_TOP, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_PADDING_RIGHT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_PADDING_BOTTOM, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_PADDING_LEFT, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_BORDER_TOP_WIDTH, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_BORDER_RIGHT_WIDTH, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_BORDER_BOTTOM_WIDTH, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_BORDER_LEFT_WIDTH, ANIM_VAL_LENGTH, true, false},
    {CSS_PROPERTY_ASPECT_RATIO, ANIM_VAL_ASPECT_RATIO, true, false},
    {CSS_PROPERTY_DISPLAY, ANIM_VAL_DISPLAY, false, false},
    {CSS_PROPERTY_FONT, ANIM_VAL_NONE, false, true},
    {CSS_PROPERTY_FONT_SIZE, ANIM_VAL_NONE, false, true},
    {CSS_PROPERTY_FONT_FAMILY, ANIM_VAL_NONE, false, true},
    {CSS_PROPERTY_FONT_WEIGHT, ANIM_VAL_NONE, false, true},
    {CSS_PROPERTY_FONT_STYLE, ANIM_VAL_NONE, false, true},
    {CSS_PROPERTY_FONT_VARIANT, ANIM_VAL_NONE, false, true},
    {CSS_PROPERTY_LINE_HEIGHT, ANIM_VAL_NONE, false, true},
};

const CssPropertyRuntimeMetadata* css_property_runtime_metadata(CssPropertyCode property) {
    size_t count = sizeof(kCssPropertyRuntimeMetadata) /
        sizeof(kCssPropertyRuntimeMetadata[0]);
    for (size_t i = 0; i < count; i++) {
        if (kCssPropertyRuntimeMetadata[i].property == property) {
            return &kCssPropertyRuntimeMetadata[i];
        }
    }
    return nullptr;
}

size_t css_property_runtime_metadata_count() {
    return sizeof(kCssPropertyRuntimeMetadata) /
        sizeof(kCssPropertyRuntimeMetadata[0]);
}

const CssPropertyRuntimeMetadata* css_property_runtime_metadata_at(size_t index) {
    return index < css_property_runtime_metadata_count()
        ? &kCssPropertyRuntimeMetadata[index] : nullptr;
}

bool css_property_runtime_inherited(CssPropertyCode property) {
    static const CssPropertyCode resolver_inherited[] = {
        CSS_PROPERTY_FONT_FAMILY, CSS_PROPERTY_FONT_SIZE,
        CSS_PROPERTY_FONT_WEIGHT, CSS_PROPERTY_FONT_STYLE,
        CSS_PROPERTY_FONT_VARIANT, CSS_PROPERTY_COLOR,
        CSS_PROPERTY_LINE_HEIGHT, CSS_PROPERTY_TEXT_ALIGN,
        CSS_PROPERTY_TEXT_DECORATION, CSS_PROPERTY_TEXT_EMPHASIS,
        CSS_PROPERTY_TEXT_EMPHASIS_STYLE, CSS_PROPERTY_TEXT_EMPHASIS_POSITION,
        CSS_PROPERTY_TEXT_TRANSFORM, CSS_PROPERTY_TEXT_INDENT,
        CSS_PROPERTY_TEXT_SPACING_TRIM, CSS_PROPERTY_HYPHENATE_CHARACTER,
        CSS_PROPERTY_DOMINANT_BASELINE, CSS_PROPERTY_LETTER_SPACING,
        CSS_PROPERTY_WORD_SPACING, CSS_PROPERTY_WHITE_SPACE,
        CSS_PROPERTY_TAB_SIZE,
        CSS_PROPERTY_FILL, CSS_PROPERTY_STROKE, CSS_PROPERTY_STROKE_WIDTH,
        CSS_PROPERTY_ACCENT_COLOR, CSS_PROPERTY_VISIBILITY,
        CSS_PROPERTY_EMPTY_CELLS, CSS_PROPERTY_DIRECTION,
        CSS_PROPERTY_LIST_STYLE_POSITION, CSS_PROPERTY_LIST_STYLE_TYPE,
        CSS_PROPERTY_LIST_STYLE, CSS_PROPERTY_RUBY_POSITION,
        CSS_PROPERTY_IMAGE_RENDERING
    };
    for (size_t i = 0; i < sizeof(resolver_inherited) / sizeof(resolver_inherited[0]); i++) {
        if (resolver_inherited[i] == property) return true;
    }
    return false;
}

void radiant_set_cssom_used_value_sync(RadiantCssomUsedValueSync sync) {
    s_cssom_used_value_sync = sync;
}

static bool copy_text(char* out, size_t out_size, const char* text) {
    if (!out || out_size == 0) return false;
    const char* value = text ? text : "";
    str_copy(out, out_size, value, strlen(value));
    return true;
}

static bool format_number(char* out, size_t out_size, double value, const char* unit) {
    if (!out || out_size == 0) return false;
    snprintf(out, out_size, "%.6g%s", value, unit ? unit : "");
    return true;
}

static bool format_color(char* out, size_t out_size, Color color) {
    if (color.a == 255) {
        snprintf(out, out_size, "rgb(%u, %u, %u)",
                 (unsigned)color.r, (unsigned)color.g, (unsigned)color.b);
    } else {
        snprintf(out, out_size, "rgba(%u, %u, %u, %.3g)",
                 (unsigned)color.r, (unsigned)color.g, (unsigned)color.b,
                 css_color_legacy_alpha(color.a));
    }
    return true;
}

static Color rgba_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Color color;
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

static const void* prop_group_base(const DomElement* element, PropGroupKind group) {
    if (!element) return nullptr;
    switch (group) {
        case PROP_GROUP_BLOCK: return element->block();
        case PROP_GROUP_BOUNDARY: return element->boundary();
        case PROP_GROUP_FONT: return element->fontp();
        case PROP_GROUP_INLINE: return element->inl();
        case PROP_GROUP_SCROLL: return element->scroll();
        case PROP_GROUP_POSITION: return element->positionp();
        case PROP_GROUP_TRANSFORM: return element->transformp();
        case PROP_GROUP_FLEX_ITEM:
            return element->flex_item() ? element->flex_item() : &FLEX_ITEM_PROP_DEFAULT;
        case PROP_GROUP_GRID_ITEM:
            return element->grid_item() ? element->grid_item() : &GRID_ITEM_PROP_DEFAULT;
        case PROP_GROUP_OUTLINE:
            return element->boundary() ? element->boundary()->outline : nullptr;
        case PROP_GROUP_NONE:
        default: return nullptr;
    }
}

static const char* property_initial(CssPropertyCode id) {
    const CssProperty* property = css_property_get_by_code(id);
    return property && property->initial_value ? property->initial_value : "";
}

static bool serialize_direct(const CssPropAccessor* accessor, DomElement* element,
                             int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const uint8_t* base = (const uint8_t*)prop_group_base(element, accessor->group_kind);
    if (!base) return false;
    const void* field = base + accessor->offset;
    switch (accessor->value_kind) {
        case CSS_PROP_VALUE_ENUM: {
            CssEnum value = *(const CssEnum*)field;
            const CssEnumInfo* info = css_enum_info(value);
            return copy_text(out, out_size,
                info && info->name ? info->name : property_initial(accessor->id));
        }
        case CSS_PROP_VALUE_PX:
            return format_number(out, out_size, *(const float*)field, "px");
        case CSS_PROP_VALUE_NUMBER:
            return format_number(out, out_size, *(const float*)field, "");
        case CSS_PROP_VALUE_INTEGER:
            snprintf(out, out_size, "%d", *(const int*)field);
            return true;
        case CSS_PROP_VALUE_COLOR:
            return format_color(out, out_size, *(const Color*)field);
        case CSS_PROP_VALUE_STRING: {
            const char* value = *(char* const*)field;
            return copy_text(out, out_size, value ? value : property_initial(accessor->id));
        }
        case CSS_PROP_VALUE_SPECIAL:
        default:
            return false;
    }
}

static CssDeclaration* computed_decl(DomElement* element, CssPropertyCode id, int pseudo_type) {
    if (!element) return nullptr;
    if (pseudo_type == 1 || pseudo_type == 2) {
        return dom_element_get_pseudo_element_value(element, id, pseudo_type);
    }
    CssDeclaration* declaration = dom_element_get_specified_value(element, id);
    if (!declaration && (id == CSS_PROPERTY_OVERFLOW_X || id == CSS_PROPERTY_OVERFLOW_Y)) {
        declaration = dom_element_get_specified_value(element, CSS_PROPERTY_OVERFLOW);
    }
    return declaration;
}

static const CssValue* computed_box_side_value(DomElement* element,
                                                   CssPropertyCode id,
                                                   int pseudo_type,
                                                   CssDeclaration** declaration) {
    if (!element || pseudo_type != 0 || !declaration) return nullptr;
    bool margin = id >= CSS_PROPERTY_MARGIN_TOP && id <= CSS_PROPERTY_MARGIN_LEFT;
    bool padding = id >= CSS_PROPERTY_PADDING_TOP && id <= CSS_PROPERTY_PADDING_LEFT;
    bool border_color = id >= CSS_PROPERTY_BORDER_TOP_COLOR && id <= CSS_PROPERTY_BORDER_LEFT_COLOR;
    if (!margin && !padding && !border_color) return nullptr;

    CssDeclaration* shorthand = computed_decl(
        element, margin ? CSS_PROPERTY_MARGIN : padding ? CSS_PROPERTY_PADDING
            : CSS_PROPERTY_BORDER_COLOR, pseudo_type);
    if (!shorthand || !shorthand->value ||
        (*declaration && css_declaration_cascade_compare(shorthand, *declaration) <= 0)) {
        return nullptr;
    }
    *declaration = shorthand;
    return css_box_shorthand_side_value(shorthand->value, radiant_css_box_side(id));
}

static bool format_decl_color(DomElement* element, const CssValue* value,
                              char* out, size_t out_size) {
    CssComputedColor color;
    if (!css_color_compute(value, &color)) return false;
    if (color.type == CSS_COLOR_COLOR || color.missing) return false;
    if (color.type == CSS_COLOR_CURRENTCOLOR)
        return element && element->in_line ? format_color(out, out_size, element->inl()->color) : false;
    Color rgba;
    return css_color_to_rgba(&color, &rgba.r, &rgba.g, &rgba.b, &rgba.a)
        ? format_color(out, out_size, rgba) : false;
}

static bool format_css_value(DomElement* element, CssPropertyCode id,
                             const CssValue* value, char* out, size_t out_size,
                             Pool* scratch = nullptr) {
    if (!value) return copy_text(out, out_size, property_initial(id));
    if (id == CSS_PROPERTY_OPACITY) {
        // numeric opacity math needs a value context, without consuming pending geometry.
        Pool* scratch = pool_create();
        if (!scratch) return false;
        LayoutContext context = {};
        context.pool = lam::up(scratch);
        context.view = lam::up(static_cast<View*>(element));
        bool result = format_number(out, out_size, resolve_css_opacity_value(&context, value), "");
        pool_destroy(scratch);
        return result;
    }
    if (id == CSS_PROPERTY_COLOR || id == CSS_PROPERTY_BACKGROUND_COLOR ||
        id == CSS_PROPERTY_BORDER_TOP_COLOR || id == CSS_PROPERTY_BORDER_RIGHT_COLOR ||
        id == CSS_PROPERTY_BORDER_BOTTOM_COLOR || id == CSS_PROPERTY_BORDER_LEFT_COLOR) {
        // Computed-style serialization has no live LayoutContext; use the
        // element's already-resolved currentColor instead of calling the
        // cascade resolver with an invalid context.
        if (format_decl_color(element, value, out, out_size)) return true;
    }
    Pool* pool = scratch ? scratch : element && element->doc ? element->doc->document_pool : nullptr;
    if (!pool) return false;
    CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!formatter) return false;
    CssComputedColor color;
    const CssProperty* property = css_property_get_by_code(id);
    formatter->options.computed_colors = property && property->type == PROP_TYPE_COLOR && css_color_compute(value, &color);
    css_format_value(formatter, (CssValue*)value);
    String* result = stringbuf_to_string(formatter->output);
    return copy_text(out, out_size, result ? result->chars : "");
}

static bool format_legacy_font_color(DomElement* element, CssPropertyCode id,
                                     int pseudo_type, char* out, size_t out_size) {
    if (!element || id != CSS_PROPERTY_COLOR || pseudo_type != 0 ||
        element->tag() != MARKUP_NAME_FONT) {
        return false;
    }
    const char* value = element->get_attribute("color");
    CssColor color = {};
    if (!value || !css_parse_color(value, &color) || color.type == CSS_COLOR_CURRENT) {
        return false;
    }
    return format_color(out, out_size, rgba_color(color.r, color.g, color.b, color.a));
}

static bool serialize_decl_recursive(DomElement* element, CssPropertyCode id,
                                     int pseudo_type, int depth,
                                     char* out, size_t out_size, Pool* scratch,
                                     bool preserve_color_model = false) {
    if (!element || depth > 16) return false;
    CssDeclaration* declaration = computed_decl(element, id, pseudo_type);
    const CssValue* value = declaration ? declaration->value : nullptr;
    if (pseudo_type == 0 && (css_animation_longhand_index(id) >= 0 ||
        css_transition_longhand_index(id) >= 0)) {
        const CssValue* animation_value = css_motion_computed_value(scratch, element, id);
        return format_css_value(element, id, animation_value, out, out_size, scratch);
    }
    // Legacy presentational attributes participate before inherited color is
    // consulted, including for dynamic computed-style reads without layout.
    if (!value && format_legacy_font_color(element, id, pseudo_type, out, out_size)) {
        if (preserve_color_model) return false;
        return true;
    }
    // The specified-style tree keeps shorthands intact for CSSOM mutation.
    // Resolve their winning physical component before serializing a longhand.
    const CssValue* shorthand_value = computed_box_side_value(
        element, id, pseudo_type, &declaration);
    if (shorthand_value) value = shorthand_value;
    bool background_shorthand = false;
    if (id == CSS_PROPERTY_BACKGROUND_COLOR) {
        CssDeclaration* background = computed_decl(element, CSS_PROPERTY_BACKGROUND, pseudo_type);
        if (background && (!declaration || css_declaration_cascade_compare(background, declaration) > 0)) {
            value = background->value;
            background_shorthand = true;
        }
    }
    if (css_value_contains_pending_substitution(value)) {
        // A live declaration read can precede layout; use the same substitution and validation as layout.
        value = css_resolve_element_var_value(scratch, element, value,
            background_shorthand ? CSS_PROPERTY_BACKGROUND : id);
        if (!value || !css_property_validate_value(background_shorthand ? CSS_PROPERTY_BACKGROUND : id, value)) {
            DomElement* parent = css_property_is_inherited(id) ? dom_parent_element(element) : nullptr;
            return parent ? serialize_decl_recursive(parent, id, 0, depth + 1, out, out_size, scratch, preserve_color_model)
                          : !preserve_color_model && copy_text(out, out_size, property_initial(id));
        }
    }
    if (value && value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INHERIT ||
            (keyword == CSS_VALUE_UNSET && css_property_is_inherited(id))) {
            DomElement* parent = dom_parent_element(element);
            return parent ? serialize_decl_recursive(parent, id, 0, depth + 1, out, out_size, scratch, preserve_color_model)
                          : !preserve_color_model && copy_text(out, out_size, property_initial(id));
        }
        if (keyword == CSS_VALUE_INITIAL || keyword == CSS_VALUE_REVERT ||
            keyword == CSS_VALUE_UNSET) {
            return !preserve_color_model && copy_text(out, out_size, property_initial(id));
        }
    }
    if (!value && css_property_is_inherited(id)) {
        DomElement* parent = dom_parent_element(element);
        if (parent) return serialize_decl_recursive(parent, id, 0, depth + 1, out, out_size, scratch, preserve_color_model);
    }
    if (background_shorthand) value = css_background_color_component(value);
    if (preserve_color_model) {
        CssComputedColor color;
        // A byte paint snapshot cannot serialize a predefined color space or missing channels.
        if (!css_color_compute(value, &color) || (color.type != CSS_COLOR_COLOR && !color.missing)) return false;
    }
    if (id == CSS_PROPERTY_CONTENT && pseudo_type != 0 &&
        (!value || (value->type == CSS_VALUE_TYPE_KEYWORD &&
                    value->data.keyword == CSS_VALUE_NORMAL))) {
        return copy_text(out, out_size, "none");
    }
    return value ? format_css_value(element, id, value, out, out_size, scratch)
                 : !preserve_color_model && copy_text(out, out_size, property_initial(id));
}

static bool serialize_decl_value(DomElement* element, CssPropertyCode id,
                                 int pseudo_type, char* out, size_t out_size,
                                 bool preserve_color_model = false) {
    Pool* scratch = pool_create();
    if (!scratch) return false;
    bool result = serialize_decl_recursive(element, id, pseudo_type, 0, out, out_size, scratch, preserve_color_model);
    pool_destroy(scratch);
    return result;
}

static bool serialize_decl(const CssPropAccessor* accessor, DomElement* element,
                           int pseudo_type, char* out, size_t out_size) {
    return accessor && serialize_decl_value(element, accessor->id, pseudo_type, out, out_size);
}

static const CssValue* inherited_decl_value(DomElement* element, CssPropertyCode id,
                                            int pseudo_type, int depth,
                                            DomElement** declaring_element) {
    if (!element || depth > 16) return nullptr;
    CssDeclaration* declaration = computed_decl(element, id, pseudo_type);
    const CssValue* value = declaration ? declaration->value : nullptr;
    if (value && value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INHERIT ||
            (keyword == CSS_VALUE_UNSET && css_property_is_inherited(id))) {
            DomElement* parent = dom_parent_element(element);
            return parent ? inherited_decl_value(parent, id, 0, depth + 1,
                                                 declaring_element) : nullptr;
        }
        if (keyword == CSS_VALUE_INITIAL || keyword == CSS_VALUE_REVERT ||
            keyword == CSS_VALUE_UNSET) {
            return nullptr;
        }
    }
    if (value) {
        if (declaring_element) *declaring_element = element;
        return value;
    }
    if (css_property_is_inherited(id)) {
        DomElement* parent = dom_parent_element(element);
        return parent ? inherited_decl_value(parent, id, 0, depth + 1,
                                             declaring_element) : nullptr;
    }
    return nullptr;
}

static bool cssom_font_size_px(DomElement* element, int pseudo_type,
                               float* font_size) {
    if (!element || !font_size) return false;
    char serialized[64];
    if (!serialize_decl_value(element, CSS_PROPERTY_FONT_SIZE, pseudo_type,
                                  serialized, sizeof(serialized))) {
        return false;
    }
    char* end = nullptr;
    float parsed = strtof(serialized, &end);
    if (end == serialized || strcmp(end, "px") != 0 || parsed < 0.0f ||
        !isfinite(parsed)) {
        return false;
    }
    *font_size = parsed;
    return true;
}

static const CssValue* cssom_font_size_decl_value(DomElement* element,
                                                   int pseudo_type) {
    if (!element) return nullptr;
    CssDeclaration* longhand = computed_decl(
        element, CSS_PROPERTY_FONT_SIZE, pseudo_type);
    CssDeclaration* shorthand = computed_decl(element, CSS_PROPERTY_FONT,
                                               pseudo_type);
    if (shorthand && shorthand->value && pseudo_type == 0 &&
        (!longhand || css_declaration_cascade_compare(shorthand, longhand) > 0)) {
        CssFontShorthandParts parts = {};
        if (css_parse_font_shorthand(shorthand->value, &parts)) return parts.size;
    }
    return longhand ? longhand->value : nullptr;
}

static bool cssom_resolve_font_size_value(DomElement* element,
    const CssValue* value, float parent_font_size, float root_font_size, float* font_size,
    bool math_operand = false);

struct CssomFontMathContext {DomElement* element; float parent_size; float root_size;};

static bool cssom_font_math_leaf(void* data, const CssValue* value, double* result) {
    CssomFontMathContext* context = (CssomFontMathContext*)data;
    float size = 0.0f;
    if (!cssom_resolve_font_size_value(context->element, value, context->parent_size,
        context->root_size, &size, true)) return false;
    *result = size;
    return true;
}

static bool cssom_resolve_font_size_value(DomElement* element,
                                          const CssValue* value,
                                          float parent_font_size,
                                          float root_font_size,
                                          float* font_size, bool math_operand) {
    if (!element || !value || !font_size) return false;
    float resolved = -1.0f;
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        // Font math uses the parent's percentage/em basis before registered descendants inherit lengths.
        CssomFontMathContext leaf_context = {element, parent_font_size, root_font_size};
        CssMathEvaluationContext context = {cssom_font_math_leaf, &leaf_context, 1.0, false};
        CssMathResult math = css_math_evaluate(value, &context);
        if (!math.resolved || (math.type != CSS_MATH_LENGTH && math.type != CSS_MATH_PERCENT &&
            math.type != CSS_MATH_LENGTH_PERCENT && !(math.type == CSS_MATH_NUMBER && math.value == 0.0)))
            return false;
        resolved = isnan(math.value) ? 0.0 : math_operand ? math.value : fmax(0.0, math.value);
    } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
        double absolute_pixels = 0.0;
        if (css_absolute_length_to_px(value->data.length.unit,
                                      value->data.length.value, &absolute_pixels)) {
            resolved = (float)absolute_pixels;
        } else {
            float viewport_width = element->doc ? element->doc->viewport.width : 0.0f;
            float viewport_height = element->doc ? element->doc->viewport.height : 0.0f;
            double viewport_pixels = 0.0;
            WritingMode mode = element->blk ? element->block()->writing_mode
                : WM_HORIZONTAL_TB;
            if (css_viewport_length_to_px(value->data.length.unit,
                    value->data.length.value, viewport_width, viewport_height,
                    mode == WM_VERTICAL_LR || mode == WM_VERTICAL_RL,
                    &viewport_pixels)) {
                resolved = (float)viewport_pixels;
            } else {
            switch (value->data.length.unit) {
                case CSS_UNIT_EM: resolved = (float)value->data.length.value * parent_font_size; break;
                case CSS_UNIT_REM: resolved = (float)value->data.length.value * root_font_size; break;
                default: return false;
            }
            }
        }
    } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        resolved = (float)(value->data.percentage.value * parent_font_size / 100.0);
    } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        if (value->data.number.value == 0.0) resolved = 0.0f;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INHERIT || keyword == CSS_VALUE_UNSET) {
            resolved = parent_font_size;
        } else if (keyword == CSS_VALUE_LARGER) {
            resolved = parent_font_size * 1.2f;
        } else if (keyword == CSS_VALUE_SMALLER) {
            resolved = parent_font_size / 1.2f;
        } else if (keyword == CSS_VALUE_INITIAL || keyword == CSS_VALUE_REVERT) {
            resolved = css_font_size_keyword_px(CSS_VALUE_MEDIUM);
        } else {
            resolved = css_font_size_keyword_px(keyword);
        }
    }
    if (!isfinite(resolved) || (!math_operand && resolved < 0.0f)) return false;
    *font_size = resolved;
    return true;
}

static bool cssom_resolve_font_size_recursive(DomElement* element,
                                              int pseudo_type, int depth,
                                              float* root_font_size,
                                              float* font_size, bool refresh_cascade) {
    if (!element || !root_font_size || !font_size || depth > 64) return false;
    DomElement* parent = dom_parent_element(element);
    float parent_font_size = 16.0f;
    if (parent && !cssom_resolve_font_size_recursive(
            parent, 0, depth + 1, root_font_size, &parent_font_size, refresh_cascade)) {
        return false;
    }
    // Resolve this element before using its font as the next inherited basis;
    // CSSOM reads run before layout creates a ViewTree or LayoutContext.
    if (refresh_cascade) radiant_cascade_styles_for_element(element);
    const CssValue* value = cssom_font_size_decl_value(element, pseudo_type);
    if (!value) {
        *font_size = parent_font_size;
        return true;
    }
    Pool* scratch = pool_create();
    if (!scratch) return false;
    value = css_resolve_element_var_value(scratch, element, value, CSS_PROPERTY_FONT_SIZE);
    bool resolved = cssom_resolve_font_size_value(element, value, parent_font_size,
                                                  *root_font_size, font_size);
    pool_destroy(scratch);
    if (!resolved) *font_size = parent_font_size;
    if (!parent) *root_font_size = *font_size;
    return true;
}

bool css_compute_cascaded_font_size(DomElement* element, float* font_size) {
    float root_font_size = 16.0f;
    // Layout has already matched this tree; reading its font must not invalidate the cascade.
    return cssom_resolve_font_size_recursive(element, 0, 0, &root_font_size, font_size, false);
}

static bool serialize_cssom_font_size(DomElement* element, int pseudo_type,
                                      char* out, size_t out_size) {
    float root_font_size = 16.0f;
    float font_size = 0.0f;
    if (!cssom_resolve_font_size_recursive(element, pseudo_type, 0,
                                           &root_font_size, &font_size, true)) {
        return false;
    }
    return format_number(out, out_size, font_size, "px");
}

static bool serialize_line_height(const CssPropAccessor* accessor, DomElement* element,
                                  int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element) return false;
    DomElement* declaring_element = nullptr;
    const CssValue* value = inherited_decl_value(
        element, CSS_PROPERTY_LINE_HEIGHT, pseudo_type, 0, &declaring_element);
    if (value && value->type == CSS_VALUE_TYPE_NUMBER) {
        float font_size = 0.0f;
        // CSS Inline: inherited unitless values retain their multiplier and use
        // the target element's font size, unlike inherited percentages.
        if (cssom_font_size_px(element, pseudo_type, &font_size)) {
            return format_number(out, out_size, value->data.number.value * font_size, "px");
        }
    } else if (value && value->type == CSS_VALUE_TYPE_PERCENTAGE && declaring_element) {
        float font_size = 0.0f;
        // Percentages compute to a length on the declaring element before inheritance.
        if (cssom_font_size_px(declaring_element, 0, &font_size)) {
            return format_number(out, out_size,
                                 value->data.percentage.value * font_size / 100.0f, "px");
        }
    }
    return serialize_decl(accessor, element, pseudo_type, out, out_size);
}

static bool format_self_alignment(char* out, size_t out_size,
                                  CssSelfAlignment alignment) {
    const CssEnumInfo* info = css_enum_info(alignment.value);
    if (!info || !info->name) return false;
    const char* modifier = alignment.overflow_explicit
        ? (alignment.safe ? "safe " : "unsafe ") : "";
    if (alignment.value == CSS_VALUE_BASELINE && alignment.last_baseline) {
        snprintf(out, out_size, "%slast baseline", modifier);
    } else {
        snprintf(out, out_size, "%s%s", modifier, info->name);
    }
    return true;
}

static bool self_alignment_equal(CssSelfAlignment left, CssSelfAlignment right) {
    return left.value == right.value && left.safe == right.safe &&
        left.overflow_explicit == right.overflow_explicit &&
        left.last_baseline == right.last_baseline &&
        left.legacy == right.legacy;
}

typedef CssSelfAlignment (*PlaceComponentParser)(const CssValue*, bool);

static bool resolve_place_component(CssDeclaration* shorthand,
                                    CssDeclaration* longhand,
                                    bool allow_physical,
                                    PlaceComponentParser parser,
                                    CssSelfAlignment* component) {
    if (!component || !longhand ||
        (shorthand && css_declaration_cascade_compare(longhand, shorthand) <= 0)) {
        return true;
    }
    CssSelfAlignment parsed = parser(longhand->value, allow_physical);
    if (parsed.value == CSS_VALUE__UNDEF) return false;
    *component = parsed;
    return true;
}

typedef bool (*PlaceShorthandParser)(const CssValue*, CssSelfAlignment*, CssSelfAlignment*);

static bool serialize_place_alignment(
        DomElement* element, int pseudo_type, char* out, size_t out_size,
        CssPropertyCode shorthand_id, CssPropertyCode align_id,
        CssPropertyCode justify_id, CssEnum initial_value,
        PlaceShorthandParser shorthand_parser,
        PlaceComponentParser component_parser) {
    if (!element || pseudo_type != 0) return false;
    CssDeclaration* shorthand = computed_decl(element, shorthand_id, 0);
    CssSelfAlignment align = {initial_value};
    CssSelfAlignment justify = {initial_value};
    if (shorthand && (!shorthand->value ||
        !shorthand_parser(shorthand->value, &align, &justify))) {
        return serialize_decl_value(element, shorthand_id, 0, out, out_size);
    }
    // CSS Cascade expands a shorthand before constituent longhands compete.
    if (!resolve_place_component(shorthand, computed_decl(element, align_id, 0),
            false, component_parser, &align) ||
        !resolve_place_component(shorthand, computed_decl(element, justify_id, 0),
            true, component_parser, &justify)) {
        return false;
    }

    char align_text[64];
    char justify_text[64];
    if (!format_self_alignment(align_text, sizeof(align_text), align) ||
        !format_self_alignment(justify_text, sizeof(justify_text), justify)) {
        return false;
    }
    // CSS Align 3 §8 omits the second component when both longhand values match.
    if (self_alignment_equal(align, justify)) return copy_text(out, out_size, align_text);
    snprintf(out, out_size, "%s %s", align_text, justify_text);
    return true;
}

static bool serialize_place_self(const CssPropAccessor*, DomElement* element,
                                 int pseudo_type, char* out, size_t out_size) {
    return serialize_place_alignment(
        element, pseudo_type, out, out_size, CSS_PROPERTY_PLACE_SELF,
        CSS_PROPERTY_ALIGN_SELF, CSS_PROPERTY_JUSTIFY_SELF, CSS_VALUE_AUTO,
        css_parse_place_self_alignment, css_parse_self_alignment_value);
}

static bool serialize_place_content(const CssPropAccessor*, DomElement* element,
                                    int pseudo_type, char* out, size_t out_size) {
    return serialize_place_alignment(
        element, pseudo_type, out, out_size, CSS_PROPERTY_PLACE_CONTENT,
        CSS_PROPERTY_ALIGN_CONTENT, CSS_PROPERTY_JUSTIFY_CONTENT, CSS_VALUE_NORMAL,
        css_parse_place_content_alignment, css_parse_content_alignment_value);
}

static bool serialize_display(const CssPropAccessor*, DomElement* element, int pseudo_type,
                              char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    if (element->display.outer == CSS_VALUE_NONE) return copy_text(out, out_size, "none");
    if (element->display.inner == CSS_VALUE_FLEX) {
        return copy_text(out, out_size,
            element->display.outer == CSS_VALUE_INLINE ? "inline-flex" : "flex");
    }
    if (element->display.inner == CSS_VALUE_GRID) {
        return copy_text(out, out_size,
            element->display.outer == CSS_VALUE_INLINE ? "inline-grid" : "grid");
    }
    if (element->display.inner == CSS_VALUE_TABLE) {
        return copy_text(out, out_size,
            element->display.outer == CSS_VALUE_INLINE ? "inline-table" : "table");
    }
    const CssEnumInfo* info = css_enum_info(element->display.outer);
    return copy_text(out, out_size, info && info->name ? info->name : "block");
}

static bool serialize_visibility(const CssPropAccessor*, DomElement* element,
                                 int pseudo_type, char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const InlineProp* in_line = element->inl();
    Visibility visibility = in_line
        ? (Visibility)in_line->visibility : VIS_VISIBLE;
    // Visibility is a compact render enum, not a CssEnum; indexing the CSS
    // keyword table with it serialized VIS_HIDDEN as the unrelated "_length".
    switch (visibility) {
        case VIS_HIDDEN: return copy_text(out, out_size, "hidden");
        case VIS_COLLAPSE: return copy_text(out, out_size, "collapse");
        case VIS_VISIBLE:
        default: return copy_text(out, out_size, "visible");
    }
}

static bool serialize_transform(const CssPropAccessor* accessor, DomElement* element,
                                 int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    TransformFunction* functions = element->transform ? element->transform->functions.get() : nullptr;
    if (!functions) return copy_text(out, out_size, "none");
    // CSS Transforms 1 §4.2 serializes the function product without transform-origin.
    RdtMatrix4 matrix = radiant::compute_transform_matrix_3d(functions,
        element->width, element->height, 0.0f, 0.0f, 0.0f);
    const float* values = matrix.values;
    bool is_2d = rdt_matrix4_is_2d(&matrix);
    const int planar_indices[] = {0, 4, 1, 5, 3, 7};
    int count = is_2d ? 6 : 16;
    size_t used = str_copy(out, out_size, is_2d ? "matrix(" : "matrix3d(", is_2d ? 7 : 9);
    for (int index = 0; index < count; index++) {
        if (index) used = str_cat(out, used, out_size, ", ", 2);
        int component = is_2d ? planar_indices[index] : (index % 4) * 4 + index / 4;
        char number[64];
        format_number(number, sizeof(number), values[component], "");
        used = str_cat(out, used, out_size, number, strlen(number));
    }
    str_cat(out, used, out_size, ")", 1);
    return true;
}

static bool serialize_used_size(const CssPropAccessor* accessor, DomElement* element,
                                int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    float value = accessor->id == CSS_PROPERTY_WIDTH ? element->width : element->height;
    if (!element->doc || !element->doc->view_tree || !element->doc->view_tree->root) {
        const BlockProp* block = element->block();
        float specified = accessor->id == CSS_PROPERTY_WIDTH
            ? block->given_width : block->given_height;
        // Load-time scripts run after cascade but before the first ViewTree;
        // the resolved CSS size is the only valid computed value at that seam.
        if (specified >= 0.0f) value = specified;
    }
    return format_number(out, out_size, value, "px");
}

static bool serialize_edge(const CssPropAccessor* accessor, DomElement* element,
                           int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const BoundaryProp* boundary = element->boundary();
    float value = 0.0f;
    CssEnum type = CSS_VALUE__UNDEF;
    switch (accessor->id) {
        case CSS_PROPERTY_MARGIN_TOP: value = boundary->margin.top; type = boundary->margin.top_type; break;
        case CSS_PROPERTY_MARGIN_RIGHT: value = boundary->margin.right; type = boundary->margin.right_type; break;
        case CSS_PROPERTY_MARGIN_BOTTOM: value = boundary->margin.bottom; type = boundary->margin.bottom_type; break;
        case CSS_PROPERTY_MARGIN_LEFT: value = boundary->margin.left; type = boundary->margin.left_type; break;
        case CSS_PROPERTY_PADDING_TOP: value = boundary->padding.top; break;
        case CSS_PROPERTY_PADDING_RIGHT: value = boundary->padding.right; break;
        case CSS_PROPERTY_PADDING_BOTTOM: value = boundary->padding.bottom; break;
        case CSS_PROPERTY_PADDING_LEFT: value = boundary->padding.left; break;
        default: return false;
    }
    if (type == CSS_VALUE_AUTO) return copy_text(out, out_size, "auto");
    return format_number(out, out_size, value, "px");
}

static bool serialize_border_component(const CssPropAccessor* accessor,
                                       DomElement* element, int pseudo_type,
                                       char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const BoundaryProp* boundary = element->boundary();
    RadiantBorderSide side = radiant_border_side(boundary ? boundary->border : nullptr,
                                                 radiant_css_box_side(accessor->id));
    switch (accessor->id) {
        case CSS_PROPERTY_BORDER_TOP_WIDTH:
        case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
        case CSS_PROPERTY_BORDER_LEFT_WIDTH:
            // resolved edge widths include none/hidden and UA border defaults.
            return format_number(out, out_size, side.width ? *side.width : 0.0f, "px");
        case CSS_PROPERTY_BORDER_TOP_STYLE:
        case CSS_PROPERTY_BORDER_RIGHT_STYLE:
        case CSS_PROPERTY_BORDER_BOTTOM_STYLE:
        case CSS_PROPERTY_BORDER_LEFT_STYLE: {
            const CssEnumInfo* info = side.style ? css_enum_info(*side.style) : nullptr;
            return copy_text(out, out_size, info && info->name ? info->name : "none");
        }
        default:
            // colors come from the computed side, including winning shorthands.
            return format_color(out, out_size, side.color ? *side.color : element->inl()->color);
    }
}

static bool serialize_inset(const CssPropAccessor* accessor, DomElement* element,
                            int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const PositionProp* position = element->positionp();
    bool present = false;
    float value = 0.0f;
    switch (accessor->id) {
        case CSS_PROPERTY_TOP: present = position->has_top; value = position->top; break;
        case CSS_PROPERTY_RIGHT: present = position->has_right; value = position->right; break;
        case CSS_PROPERTY_BOTTOM: present = position->has_bottom; value = position->bottom; break;
        case CSS_PROPERTY_LEFT: present = position->has_left; value = position->left; break;
        default: return false;
    }
    return present ? format_number(out, out_size, value, "px")
                   : copy_text(out, out_size, "auto");
}

static bool serialize_font_weight(const CssPropAccessor*, DomElement* element, int pseudo_type,
                                  char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const FontProp* font = element->fontp();
    if (font->font_weight_numeric > 0) {
        snprintf(out, out_size, "%d", (int)font->font_weight_numeric);
        return true;
    }
    const CssEnumInfo* info = css_enum_info(font->font_weight);
    return copy_text(out, out_size, info && info->name ? info->name : "normal");
}

static bool serialize_color_prop(const CssPropAccessor*, DomElement* element, int pseudo_type,
                                 char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const InlineProp* inl = element->inl();
    Color color = inl->color;
    if (!inl->has_color) { color.r = color.g = color.b = 0; color.a = 255; }
    return format_color(out, out_size, color);
}

static bool serialize_background_color(const CssPropAccessor*, DomElement* element,
                                       int pseudo_type, char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const BoundaryProp* boundary = element->boundary();
    if (boundary && boundary->background) {
        return format_color(out, out_size, boundary->background->color);
    }

    CssDeclaration* longhand = computed_decl(
        element, CSS_PROPERTY_BACKGROUND_COLOR, pseudo_type);
    CssDeclaration* shorthand = computed_decl(
        element, CSS_PROPERTY_BACKGROUND, pseudo_type);
    // A shorthand and its longhand occupy separate style-tree nodes; compare
    // them before layout so computed style cannot expose the losing longhand.
    if (shorthand && (!longhand ||
        css_declaration_cascade_compare(shorthand, longhand) > 0)) {
        if (format_decl_color(element, shorthand->value, out, out_size)) return true;
    }
    return longhand
        ? format_css_value(element, CSS_PROPERTY_BACKGROUND_COLOR,
                           longhand->value, out, out_size)
        : copy_text(out, out_size, "rgba(0, 0, 0, 0)");
}

static bool serialize_minmax(const CssPropAccessor* accessor, DomElement* element,
                             int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const BlockProp* block = element->block();
    float value = -1.0f;
    bool maximum = false;
    switch (accessor->id) {
        case CSS_PROPERTY_MIN_WIDTH: value = block->given_min_width; break;
        case CSS_PROPERTY_MIN_HEIGHT: value = block->given_min_height; break;
        case CSS_PROPERTY_MAX_WIDTH: value = block->given_max_width; maximum = true; break;
        case CSS_PROPERTY_MAX_HEIGHT: value = block->given_max_height; maximum = true; break;
        default: return false;
    }
    if (value < 0.0f) return copy_text(out, out_size, maximum ? "none" : "0px");
    return format_number(out, out_size, value, "px");
}

static bool serialize_z_index(const CssPropAccessor*, DomElement* element, int pseudo_type,
                              char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const PositionProp* position = element->positionp();
    if (position->position == CSS_VALUE_STATIC && position->z_index == 0) {
        return copy_text(out, out_size, "auto");
    }
    snprintf(out, out_size, "%d", position->z_index);
    return true;
}

static bool serialize_outline(const CssPropAccessor* accessor, DomElement* element,
                              int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    OutlineProp* outline = element->boundary() ? element->boundary()->outline : nullptr;
    if (!outline) {
        if (accessor->id == CSS_PROPERTY_OUTLINE_STYLE) return copy_text(out, out_size, "none");
        if (accessor->id == CSS_PROPERTY_OUTLINE_WIDTH) return copy_text(out, out_size, "0px");
        return copy_text(out, out_size, "rgba(0, 0, 0, 0)");
    }
    if (accessor->id == CSS_PROPERTY_OUTLINE_STYLE) {
        const CssEnumInfo* info = css_enum_info(outline->style);
        return copy_text(out, out_size, info && info->name ? info->name : "none");
    }
    if (accessor->id == CSS_PROPERTY_OUTLINE_WIDTH) {
        return format_number(out, out_size, outline->width, "px");
    }
    if (accessor->id == CSS_PROPERTY_OUTLINE_COLOR) {
        return format_color(out, out_size, outline->color);
    }
    return false;
}

static const char* scroll_snap_axis_name(ScrollSnapAxis axis) {
    switch (axis) {
        case SCROLL_SNAP_AXIS_X: return "x";
        case SCROLL_SNAP_AXIS_Y: return "y";
        case SCROLL_SNAP_AXIS_BOTH: return "both";
        case SCROLL_SNAP_AXIS_BLOCK: return "block";
        case SCROLL_SNAP_AXIS_INLINE: return "inline";
        case SCROLL_SNAP_AXIS_PAIR: return "pair";
        case SCROLL_SNAP_AXIS_NONE: default: return "none";
    }
}

static bool serialize_individual_transform(const CssPropAccessor* accessor,
    DomElement* element, int, char* out, size_t out_size) {
    int index = css_individual_transform_index(accessor->id);
    const TransformFunction* function = element && element->transform
        ? &element->transformp()->individual[index] : nullptr;
    if (!function || function->type == TRANSFORM_NONE) return copy_text(out, out_size, "none");
    if (accessor->id == CSS_PROPERTY_TRANSLATE) {
        bool three_d = function->type == TRANSFORM_TRANSLATE3D;
        float x = three_d ? function->params.translate3d.x : function->params.translate.x;
        float y = three_d ? function->params.translate3d.y : function->params.translate.y;
        float z = three_d ? function->params.translate3d.z : 0.0f;
        char components[3][64];
        bool x_percent = !isnan(function->translate_x_percent);
        bool y_percent = !isnan(function->translate_y_percent);
        format_number(components[0], sizeof(components[0]),
            x_percent ? function->translate_x_percent : x, x_percent ? "%" : "px");
        format_number(components[1], sizeof(components[1]),
            y_percent ? function->translate_y_percent : y, y_percent ? "%" : "px");
        format_number(components[2], sizeof(components[2]), z, "px");
        if (z != 0.0f) snprintf(out, out_size, "%s %s %s", components[0], components[1], components[2]);
        else if (y != 0.0f || y_percent) snprintf(out, out_size, "%s %s", components[0], components[1]);
        else return copy_text(out, out_size, components[0]);
    } else if (accessor->id == CSS_PROPERTY_SCALE) {
        bool three_d = function->type == TRANSFORM_SCALE3D;
        float x = three_d ? function->params.scale3d.x : function->params.scale.x;
        float y = three_d ? function->params.scale3d.y : function->params.scale.y;
        float z = three_d ? function->params.scale3d.z : 1.0f;
        if (z != 1.0f) snprintf(out, out_size, "%.6g %.6g %.6g", x, y, z);
        else if (x != y) snprintf(out, out_size, "%.6g %.6g", x, y);
        else return format_number(out, out_size, x, "");
    } else {
        bool three_d = function->type == TRANSFORM_ROTATE3D;
        float angle = (float)math_radians_to_degrees_d(three_d
            ? function->params.rotate3d.angle : function->params.angle);
        if (!three_d || (function->params.rotate3d.x == 0.0f &&
            function->params.rotate3d.y == 0.0f && function->params.rotate3d.z > 0.0f)) {
            return format_number(out, out_size, angle, "deg");
        }
        if (function->params.rotate3d.z == 0.0f &&
            ((function->params.rotate3d.x != 0.0f && function->params.rotate3d.y == 0.0f) ||
             (function->params.rotate3d.y != 0.0f && function->params.rotate3d.x == 0.0f))) {
            bool x_axis = function->params.rotate3d.x != 0.0f;
            float axis = x_axis ? function->params.rotate3d.x : function->params.rotate3d.y;
            snprintf(out, out_size, "%s %.6gdeg", x_axis ? "x" : "y", axis < 0.0f ? -angle : angle);
            return true;
        }
        snprintf(out, out_size, "%.6g %.6g %.6g %.6gdeg", function->params.rotate3d.x,
            function->params.rotate3d.y, function->params.rotate3d.z, angle);
    }
    return true;
}

static bool serialize_scroll_snap(const CssPropAccessor* accessor,
                                  DomElement* element, int pseudo_type,
                                  char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const ScrollProp* scroll = element->scroll();
    if (accessor->id == CSS_PROPERTY_SCROLL_SNAP_TYPE) {
        const char* axis = scroll_snap_axis_name(scroll->snap_axis);
        if (scroll->snap_axis == SCROLL_SNAP_AXIS_NONE ||
            !scroll->snap_strictness_explicit) {
            return copy_text(out, out_size, axis);
        }
        snprintf(out, out_size, "%s %s", axis,
                 scroll->snap_mandatory ? "mandatory" : "proximity");
        return true;
    }
    const CssEnumInfo* block = css_enum_info(scroll->snap_align_block);
    const CssEnumInfo* inline_axis = css_enum_info(scroll->snap_align_inline);
    snprintf(out, out_size, "%s %s",
             block && block->name ? block->name : "none",
             inline_axis && inline_axis->name ? inline_axis->name : "none");
    return true;
}

static bool serialize_overscroll_behavior(const CssPropAccessor* accessor,
                                          DomElement* element, int pseudo_type,
                                          char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const ScrollProp* scroll = element->scroll();
    const CssEnumInfo* x = css_enum_info(scroll->overscroll_x);
    const CssEnumInfo* y = css_enum_info(scroll->overscroll_y);
    const char* x_name = x && x->name ? x->name : "auto";
    const char* y_name = y && y->name ? y->name : "auto";
    if (strcmp(x_name, y_name) == 0) return copy_text(out, out_size, x_name);
    snprintf(out, out_size, "%s %s", x_name, y_name);
    return true;
}

#define DIRECT_ROW(prop_id, group, type, field, kind, row_flags) \
    {prop_id, group, (uint16_t)offsetof(type, field), kind, row_flags, serialize_direct, nullptr}
#define DERIVED_ROW(prop_id, fn, row_flags) \
    {prop_id, PROP_GROUP_NONE, 0, CSS_PROP_VALUE_SPECIAL, row_flags, fn, fn}
#define DECL_ROW(prop_id) DERIVED_ROW(prop_id, serialize_decl, 0)

static const CssPropAccessor CSS_PROP_ROWS[] = {
    DERIVED_ROW(CSS_PROPERTY_DISPLAY, serialize_display, 0),
    DIRECT_ROW(CSS_PROPERTY_POSITION, PROP_GROUP_POSITION, PositionProp, position, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_TOP, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_RIGHT, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BOTTOM, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_LEFT, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_Z_INDEX, serialize_z_index, 0),
    DIRECT_ROW(CSS_PROPERTY_FLOAT, PROP_GROUP_POSITION, PositionProp, float_prop, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_CLEAR, PROP_GROUP_POSITION, PositionProp, clear, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_OVERFLOW_X, PROP_GROUP_SCROLL, ScrollProp, overflow_x, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_OVERFLOW_Y, PROP_GROUP_SCROLL, ScrollProp, overflow_y, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_SCROLL_BEHAVIOR, PROP_GROUP_SCROLL, ScrollProp, scroll_behavior, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_OVERSCROLL_BEHAVIOR_X, PROP_GROUP_SCROLL, ScrollProp, overscroll_x, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_OVERSCROLL_BEHAVIOR_Y, PROP_GROUP_SCROLL, ScrollProp, overscroll_y, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_OVERSCROLL_BEHAVIOR, serialize_overscroll_behavior, 0),
    DERIVED_ROW(CSS_PROPERTY_SCROLL_SNAP_TYPE, serialize_scroll_snap, 0),
    DERIVED_ROW(CSS_PROPERTY_SCROLL_SNAP_ALIGN, serialize_scroll_snap, 0),
    DERIVED_ROW(CSS_PROPERTY_VISIBILITY, serialize_visibility, 0),
    DERIVED_ROW(CSS_PROPERTY_WIDTH, serialize_used_size, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_HEIGHT, serialize_used_size, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MIN_WIDTH, serialize_minmax, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MIN_HEIGHT, serialize_minmax, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MAX_WIDTH, serialize_minmax, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MAX_HEIGHT, serialize_minmax, CSS_PROP_ACCESSOR_USED_VALUE),
    // CSSOM support detection reads the computed aspect ratio before layout;
    // omitting it makes valid animation values appear unsupported to WPT.
    DECL_ROW(CSS_PROPERTY_ASPECT_RATIO),
    DIRECT_ROW(CSS_PROPERTY_BOX_SIZING, PROP_GROUP_BLOCK, BlockProp, box_sizing, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_MARGIN_TOP, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MARGIN_RIGHT, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MARGIN_BOTTOM, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MARGIN_LEFT, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_PADDING_TOP, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_PADDING_RIGHT, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_PADDING_BOTTOM, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_PADDING_LEFT, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DIRECT_ROW(CSS_PROPERTY_FONT_FAMILY, PROP_GROUP_FONT, FontProp, family, CSS_PROP_VALUE_STRING, 0),
    // CSSOM resolves relative font sizes before layout; loader scripts use this
    // value to construct replacement DOM with the correct intrinsic metrics.
    DIRECT_ROW(CSS_PROPERTY_FONT_SIZE, PROP_GROUP_FONT, FontProp, font_size, CSS_PROP_VALUE_PX, 0),
    DERIVED_ROW(CSS_PROPERTY_FONT_WEIGHT, serialize_font_weight, 0),
    DIRECT_ROW(CSS_PROPERTY_FONT_STYLE, PROP_GROUP_FONT, FontProp, font_style, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_FONT_VARIANT, PROP_GROUP_FONT, FontProp, font_variant, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_LINE_HEIGHT, serialize_line_height,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DIRECT_ROW(CSS_PROPERTY_LETTER_SPACING, PROP_GROUP_FONT, FontProp, letter_spacing, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_WORD_SPACING, PROP_GROUP_FONT, FontProp, word_spacing, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_TEXT_ALIGN, PROP_GROUP_BLOCK, BlockProp, text_align, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_WHITE_SPACE, PROP_GROUP_BLOCK, BlockProp, white_space, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_TEXT_WRAP_MODE, PROP_GROUP_BLOCK, BlockProp, text_wrap_mode, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_TEXT_INDENT, PROP_GROUP_BLOCK, BlockProp, text_indent, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_VERTICAL_ALIGN, PROP_GROUP_INLINE, InlineProp, vertical_align, CSS_PROP_VALUE_ENUM, 0),
    // Outline longhands are resolved from the retained shorthand expansion.
    DERIVED_ROW(CSS_PROPERTY_OUTLINE_STYLE, serialize_outline,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_OUTLINE_WIDTH, serialize_outline,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_OUTLINE_COLOR, serialize_outline,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_COLOR, serialize_color_prop, 0),
    DIRECT_ROW(CSS_PROPERTY_OPACITY, PROP_GROUP_INLINE, InlineProp, opacity, CSS_PROP_VALUE_NUMBER, 0),
    DIRECT_ROW(CSS_PROPERTY_CURSOR, PROP_GROUP_INLINE, InlineProp, cursor, CSS_PROP_VALUE_ENUM, 0),
    DECL_ROW(CSS_PROPERTY_ALIGN_SELF),
    // CSS Align modifiers such as safe/unsafe are retained in the declaration tree.
    DECL_ROW(CSS_PROPERTY_JUSTIFY_SELF),
    DERIVED_ROW(CSS_PROPERTY_PLACE_SELF, serialize_place_self, 0),
    DIRECT_ROW(CSS_PROPERTY_FLEX_GROW, PROP_GROUP_FLEX_ITEM, FlexItemProp, flex_grow, CSS_PROP_VALUE_NUMBER, 0),
    DIRECT_ROW(CSS_PROPERTY_FLEX_SHRINK, PROP_GROUP_FLEX_ITEM, FlexItemProp, flex_shrink, CSS_PROP_VALUE_NUMBER, 0),
    DIRECT_ROW(CSS_PROPERTY_FLEX_BASIS, PROP_GROUP_FLEX_ITEM, FlexItemProp, flex_basis, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_ORDER, PROP_GROUP_FLEX_ITEM, FlexItemProp, order, CSS_PROP_VALUE_INTEGER, 0),
    DECL_ROW(CSS_PROPERTY_FLEX_DIRECTION),
    DECL_ROW(CSS_PROPERTY_FLEX_WRAP),
    DECL_ROW(CSS_PROPERTY_JUSTIFY_CONTENT),
    DECL_ROW(CSS_PROPERTY_ALIGN_ITEMS),
    DECL_ROW(CSS_PROPERTY_ALIGN_CONTENT),
    DERIVED_ROW(CSS_PROPERTY_PLACE_CONTENT, serialize_place_content, 0),
    DECL_ROW(CSS_PROPERTY_GAP),
    DECL_ROW(CSS_PROPERTY_ROW_GAP),
    DECL_ROW(CSS_PROPERTY_COLUMN_GAP),
    // This longhand can be supplied by the retained background shorthand.
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_COLOR, serialize_background_color,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BORDER_TOP_WIDTH, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_RIGHT_WIDTH, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_BOTTOM_WIDTH, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_LEFT_WIDTH, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_TOP_STYLE, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_RIGHT_STYLE, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_BOTTOM_STYLE, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_LEFT_STYLE, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_TOP_COLOR, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_RIGHT_COLOR, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_BOTTOM_COLOR, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_LEFT_COLOR, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_TRANSFORM, serialize_transform, CSS_PROP_ACCESSOR_USED_VALUE),
    DIRECT_ROW(CSS_PROPERTY_BACKFACE_VISIBILITY, PROP_GROUP_TRANSFORM, TransformProp,
        backface_visibility, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_TRANSFORM_STYLE, PROP_GROUP_TRANSFORM, TransformProp,
        transform_style, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_TRANSLATE, serialize_individual_transform, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_ROTATE, serialize_individual_transform, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_SCALE, serialize_individual_transform, CSS_PROP_ACCESSOR_USED_VALUE),
    DECL_ROW(CSS_PROPERTY_FILTER),
    DECL_ROW(CSS_PROPERTY_ANIMATION_NAME),
    DECL_ROW(CSS_PROPERTY_ANIMATION_DURATION),
    DECL_ROW(CSS_PROPERTY_ANIMATION_TIMING_FUNCTION),
    DECL_ROW(CSS_PROPERTY_ANIMATION_DELAY),
    DECL_ROW(CSS_PROPERTY_ANIMATION_ITERATION_COUNT),
    DECL_ROW(CSS_PROPERTY_ANIMATION_DIRECTION),
    DECL_ROW(CSS_PROPERTY_ANIMATION_FILL_MODE),
    DECL_ROW(CSS_PROPERTY_ANIMATION_PLAY_STATE),
    DECL_ROW(CSS_PROPERTY_TRANSITION_DURATION),
    DECL_ROW(CSS_PROPERTY_TRANSITION_DELAY),
    DECL_ROW(CSS_PROPERTY_TRANSITION_PROPERTY),
    DECL_ROW(CSS_PROPERTY_TRANSITION_TIMING_FUNCTION),
    DECL_ROW(CSS_PROPERTY_DIRECTION),
    DECL_ROW(CSS_PROPERTY_CONTENT),
    DECL_ROW(CSS_PROPERTY_FIELD_SIZING),
};

#undef DECL_ROW
#undef DERIVED_ROW
#undef DIRECT_ROW

struct CssPropIndex {
    const CssPropAccessor* rows[CSS_PROPERTY_COUNT];
    CssPropIndex() : rows{} {
        size_t count = sizeof(CSS_PROP_ROWS) / sizeof(CSS_PROP_ROWS[0]);
        for (size_t i = 0; i < count; i++) {
            CssPropertyCode id = CSS_PROP_ROWS[i].id;
            if (id > CSS_PROPERTY_UNKNOWN && id < CSS_PROPERTY_COUNT) rows[id] = &CSS_PROP_ROWS[i];
        }
    }
};

const CssPropAccessor* css_prop_accessor(CssPropertyCode id) {
    static const CssPropIndex index;
    return id > CSS_PROPERTY_UNKNOWN && id < CSS_PROPERTY_COUNT ? index.rows[id] : nullptr;
}

const CssPropAccessor* css_prop_accessors(size_t* count) {
    if (count) *count = sizeof(CSS_PROP_ROWS) / sizeof(CSS_PROP_ROWS[0]);
    return CSS_PROP_ROWS;
}

bool dom_ensure_computed(DomElement* element, bool needs_used_value) {
    if (!element || !element->doc) return false;
    // A clean committed ViewTree already owns the used values. Re-reading a
    // used-value property must not discard that snapshot and fall back to the
    // unexpanded declaration tree (for example, `padding: 4px`).
    bool missing_used_value_snapshot = needs_used_value &&
        (!element->doc->view_tree || !element->doc->view_tree->root);
    bool dirty = missing_used_value_snapshot || element->layout_dirty;
    // the Range/Selection document stub above doc->root never participates in CSS resolution.
    for (DomNode* node = element; node && !dirty;
         node = node == element->doc->root ? nullptr : node->parent.get()) {
        if (node->layout_dirty || (node->is_element() &&
            (!node->as_element()->styles_resolved() ||
             node->as_element()->needs_style_recompute()))) dirty = true;
    }
    dirty = dirty || element->doc->js.mutation_count > 0;
    if (!dirty) return true;

    if (needs_used_value && s_cssom_used_value_sync &&
            s_cssom_used_value_sync(element->doc)) {
        if (!element->doc->js.host_driven_loop) {
            // CSSOM width/height resolve to used values for displayed boxes.
            return true;
        }
        // EventSim deliberately retains its committed geometry through a
        // handler. Re-cascade the declaration tree so a live CSSStyleDeclaration
        // observes the write without advancing that geometry snapshot.
        radiant_cascade_styles_for_element(element);
        return false;
    }

    // A declaration read still recascades when no usable layout snapshot exists.
    radiant_cascade_styles_for_element(element);
    return false;
}

bool css_prop_serialize_computed(DomElement* element, CssPropertyCode id,
                                 int pseudo_type, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    const CssPropAccessor* accessor = css_prop_accessor(id);
    if (!accessor) {
        static bool logged[CSS_PROPERTY_COUNT] = {};
        if (id > CSS_PROPERTY_UNKNOWN && id < CSS_PROPERTY_COUNT && !logged[id]) {
            logged[id] = true;
            const CssProperty* property = css_property_get_by_code(id);
            log_debug("computed-style table: unsupported property '%s'",
                      property && property->name ? property->name : "?");
            (void)property;
        }
        return false;
    }
    if (id == CSS_PROPERTY_FONT_SIZE && element->doc) {
        // CSSOM font-size is an absolute length even when a preliminary view
        // tree exists but has not yet propagated inherited font properties.
        return serialize_cssom_font_size(element, pseudo_type, out, out_size);
    }
    // only actual effects add a sampling dependency to ordinary declaration reads.
    const CssPropertyRuntimeMetadata* metadata = css_property_runtime_metadata(id);
    bool needs_used = (accessor->flags & CSS_PROP_ACCESSOR_USED_VALUE) != 0;
    bool computed = dom_ensure_computed(element, needs_used);
    if (!computed && !needs_used && metadata && metadata->animation_type != ANIM_VAL_NONE &&
        css_animation_needs_computed_sample(element, id)) {
        computed = dom_ensure_computed(element, true);
    }
    const CssProperty* property = css_property_get_by_code(id);
    if (property && property->type == PROP_TYPE_COLOR &&
        !css_animation_needs_computed_sample(element, id) &&
        serialize_decl_value(element, id, pseudo_type, out, out_size, true)) return true;

    if (!computed) {
        // Before the first UiContext exists, loader scripts can only observe
        // the already-cascaded declaration tree; keep this compatibility seam
        // inside the table instead of reviving a second JS serializer.
        if ((accessor->flags & CSS_PROP_ACCESSOR_CASCADE_RESOLVED) ||
            (id >= CSS_PROPERTY_BORDER_TOP_WIDTH &&
             id <= CSS_PROPERTY_BORDER_LEFT_STYLE)) {
            return accessor->serialize && accessor->serialize(
                accessor, element, pseudo_type, out, out_size);
        }
        return serialize_decl(accessor, element, pseudo_type, out, out_size);
    }
    if (pseudo_type != 0) return serialize_decl(accessor, element, pseudo_type, out, out_size);
    return accessor->serialize && accessor->serialize(accessor, element, pseudo_type,
                                                      out, out_size);
}

String* css_prop_serialize_custom_property(Pool* pool, DomElement* element,
    const char* name, size_t name_length) {
    if (!pool || !element || !element->doc) return nullptr;
    DomDocument* doc = element->doc;
    const CssPropertyRegistration* registration = css_find_document_property_registration(doc, name, name_length);
    dom_ensure_computed(element, false);
    Pool* scratch = pool_create();
    if (!scratch) return nullptr;
    StrView text = {};
    const CssValue* value = css_compute_element_custom_property_text(scratch, element, name, name_length, &text);
    const CssPropertySyntaxComponent* matched = registration
        ? css_match_property_syntax(registration, value) : nullptr;
    CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!formatter) {pool_destroy(scratch); return nullptr;}
    formatter->options.computed_colors = matched && matched->type == CSS_SYNTAX_COLOR;
    if (value && text.str) {
        // authored custom tokens preserve spelling; typed registrations use computed serialization.
        stringbuf_append_str_n(formatter->output, text.str, text.length);
    } else if (value) css_format_value(formatter, (CssValue*)value);
    String* result = stringbuf_to_string(formatter->output);
    pool_destroy(scratch);
    return result;
}
