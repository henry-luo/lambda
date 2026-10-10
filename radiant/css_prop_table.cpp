#include "view.hpp"
#include "layout.hpp"
#include "render.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/math_utils.h"
#include "../lib/mem_factory.h"
#include "../lib/arraylist.h"
#include "../lambda/dom/dom.h"

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
    {CSS_PROPERTY_TRANSLATE, ANIM_VAL_TRANSFORM, false, false},
    {CSS_PROPERTY_ROTATE, ANIM_VAL_TRANSFORM, false, false},
    {CSS_PROPERTY_SCALE, ANIM_VAL_TRANSFORM, false, false},
    {CSS_PROPERTY_FILTER, ANIM_VAL_FILTER, false, false},
    {CSS_PROPERTY_BACKGROUND_COLOR, ANIM_VAL_COLOR, true, false},
    {CSS_PROPERTY_BACKGROUND_IMAGE, ANIM_VAL_IMAGE, false, false},
    {CSS_PROPERTY_BACKGROUND_POSITION_X, ANIM_VAL_BACKGROUND_POSITION, false, false},
    {CSS_PROPERTY_BACKGROUND_POSITION_Y, ANIM_VAL_BACKGROUND_POSITION, false, false},
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
    if (css_property_is_svg_presentation(property) && !css_property_is_svg_paint(property)) {
        const CssProperty* metadata = css_property_get_by_code(property);
        return metadata && metadata->inheritance == PROP_INHERIT_YES;
    }
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

static bool copy_text(StringBuf* out, const char* text) {
    if (!out) return false;
    const char* value = text ? text : "";
    size_t length = strlen(value);
    if (length >= UINT32_MAX || !stringbuf_ensure_cap(out, length + 1)) return false;
    stringbuf_reset(out);
    stringbuf_append_str_n(out, value, length);
    return out->length == length;
}

static bool format_text(StringBuf* out, const char* format, ...) {
    if (!out) return false;
    va_list args;
    va_start(args, format);
    stringbuf_reset(out);
    bool valid = stringbuf_vappend_format(out, format, args);
    va_end(args);
    return valid;
}

static bool format_number(StringBuf* out, double value, const char* unit) {
    if (!out) return false;
    return format_text(out, "%.6g%s", value, unit ? unit : "");
}

static bool format_color(StringBuf* out, Color color) {
    if (color.a == 255) {
        return format_text(out, "rgb(%u, %u, %u)",
                 (unsigned)color.r, (unsigned)color.g, (unsigned)color.b);
    } else {
        return format_text(out, "rgba(%u, %u, %u, %.3g)",
                 (unsigned)color.r, (unsigned)color.g, (unsigned)color.b,
                 css_color_legacy_alpha(color.a));
    }
}

static Color rgba_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Color color;
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

static bool format_color_text(const char* text, StringBuf* out) {
    CssColor color = {};
    return text && css_parse_color(text, &color) && color.type != CSS_COLOR_CURRENT &&
        format_color(out, rgba_color(color.r, color.g, color.b, color.a));
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
                             int pseudo_type, StringBuf* out) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const uint8_t* base = (const uint8_t*)prop_group_base(element, accessor->group_kind);
    if (!base) return false;
    const void* field = base + accessor->offset;
    switch (accessor->value_kind) {
        case CSS_PROP_VALUE_ENUM: {
            CssEnum value = *(const CssEnum*)field;
            const CssEnumInfo* info = css_enum_info(value);
            return copy_text(out,
                info && info->name ? info->name : property_initial(accessor->id));
        }
        case CSS_PROP_VALUE_PX:
            return format_number(out, *(const float*)field, "px");
        case CSS_PROP_VALUE_NUMBER:
            return format_number(out, *(const float*)field, "");
        case CSS_PROP_VALUE_INTEGER:
            return format_text(out, "%d", *(const int*)field);
        case CSS_PROP_VALUE_COLOR:
            return format_color(out, *(const Color*)field);
        case CSS_PROP_VALUE_STRING: {
            const char* value = *(char* const*)field;
            return copy_text(out, value ? value : property_initial(accessor->id));
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

static DomElement* cssom_inheritance_parent(DomElement* element) {
    // the document stub above its CSS root never supplies inherited values.
    return element->doc && element == element->doc->root ? nullptr : dom_parent_element(element);
}

static DomElement* cssom_inherited_source(DomElement* element, int* pseudo_type) {
    // a generated pseudo inherits from its originating element before walking further ancestors.
    if (*pseudo_type != 0) { *pseudo_type = 0; return element; }
    return cssom_inheritance_parent(element);
}

static ArrayList* cssom_collect_style_ancestors(DomElement* element) {
    ArrayList* ancestors = arraylist_new(8);
    if (!ancestors) return nullptr;
    for (DomElement* current = element; current; current = cssom_inheritance_parent(current)) {
        if (!arraylist_append(ancestors, current)) {
            arraylist_free(ancestors);
            return nullptr;
        }
    }
    return ancestors;
}

static bool cssom_value_inherits(const CssValue* value, CssPropertyCode id) {
    if (!value) return css_property_is_inherited(id);
    CssComputedColor color;
    // currentColor on color inherits; on another property it resolves against
    // the receiving element's live color, even before layout is committed.
    if (id == CSS_PROPERTY_COLOR && css_color_compute(value, &color) &&
        color.type == CSS_COLOR_CURRENTCOLOR) return true;
    return value->type == CSS_VALUE_TYPE_KEYWORD &&
        (value->data.keyword == CSS_VALUE_INHERIT ||
         (value->data.keyword == CSS_VALUE_UNSET && css_property_is_inherited(id)));
}

static bool cssom_value_uses_initial(const CssValue* value) {
    return value && value->type == CSS_VALUE_TYPE_KEYWORD &&
        (value->data.keyword == CSS_VALUE_INITIAL || value->data.keyword == CSS_VALUE_REVERT ||
         value->data.keyword == CSS_VALUE_UNSET);
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

static bool serialize_computed(DomElement* element, CssPropertyCode id,
                               int pseudo_type, StringBuf* out);

static bool format_decl_color(DomElement* element, const CssValue* value,
                              StringBuf* out) {
    CssComputedColor color;
    if (!css_color_compute(value, &color)) return false;
    if (color.type == CSS_COLOR_COLOR || color.missing) return false;
    if (color.type == CSS_COLOR_CURRENTCOLOR)
        return serialize_computed(element, CSS_PROPERTY_COLOR, 0, out);
    Color rgba;
    return css_color_to_rgba(&color, &rgba.r, &rgba.g, &rgba.b, &rgba.a)
        ? format_color(out, rgba) : false;
}

static bool format_keyword_set(const CssValue* value, StringBuf* out,
        const char* const* keywords, size_t count) {
    stringbuf_reset(out);
    bool written = false;
    for (size_t index = 0; index < count; index++) {
        if (!css_value_has_identifier(value, keywords[index])) continue;
        if (!stringbuf_append_format(out, "%s%s", written ? " " : "", keywords[index])) return false;
        written = true;
    }
    return written;
}

static bool format_css_value(DomElement* element, CssPropertyCode id,
                             const CssValue* value, StringBuf* out,
                             Pool* scratch = nullptr) {
    if (!value) {
        // initial color names still serialize as computed colors after a rule stops matching.
        const CssProperty* property = css_property_get_by_code(id);
        if (property && property->type == PROP_TYPE_COLOR &&
            str_icmp_cstr(property_initial(id), "currentColor") == 0)
            return serialize_computed(element, CSS_PROPERTY_COLOR, 0, out);
        if (property && property->type == PROP_TYPE_COLOR &&
            format_color_text(property_initial(id), out)) return true;
        return copy_text(out, property_initial(id));
    }
    // computed keyword sets use grammar order and canonical case, while names retain case.
    if (id == CSS_PROPERTY_CONTAIN) {
        const char* keywords[] = {"none", "strict", "content", "size", "inline-size", "layout", "style", "paint"};
        return format_keyword_set(value, out, keywords, sizeof(keywords) / sizeof(*keywords));
    }
    if (id == CSS_PROPERTY_CONTAINER_TYPE) {
        const char* keywords[] = {"normal", "size", "inline-size", "scroll-state"};
        return format_keyword_set(value, out, keywords, sizeof(keywords) / sizeof(*keywords));
    }
    if (id == CSS_PROPERTY_OPACITY) {
        // numeric opacity math needs a value context, without consuming pending geometry.
        Pool* scratch = pool_create();
        if (!scratch) return false;
        LayoutContext context = {};
        context.pool = lam::up(scratch);
        context.view = lam::up(static_cast<View*>(element));
        bool result = format_number(out, resolve_css_opacity_value(&context, value), "");
        pool_destroy(scratch);
        return result;
    }
    if (id == CSS_PROPERTY_COLOR || id == CSS_PROPERTY_BACKGROUND_COLOR ||
        id == CSS_PROPERTY_BORDER_TOP_COLOR || id == CSS_PROPERTY_BORDER_RIGHT_COLOR ||
        id == CSS_PROPERTY_BORDER_BOTTOM_COLOR || id == CSS_PROPERTY_BORDER_LEFT_COLOR) {
        // declaration colors use live CSSOM color resolution without committing geometry.
        if (format_decl_color(element, value, out)) return true;
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
    return copy_text(out, result ? result->chars : "");
}

static bool format_legacy_font_color(DomElement* element, CssPropertyCode id,
                                     int pseudo_type, StringBuf* out) {
    if (!element || id != CSS_PROPERTY_COLOR || pseudo_type != 0 ||
        element->tag() != MARKUP_NAME_FONT) {
        return false;
    }
    return format_color_text(element->get_attribute("color"), out);
}

static bool serialize_inherited_decl(DomElement* element, CssPropertyCode id,
                                     int pseudo_type, StringBuf* out,
                                     Pool* scratch, bool preserve_color_model = false) {
    if (!element) return false;
    // walk the complete CSS ancestry without consuming native stack frames.
    while (element) {
        CssDeclaration* declaration = computed_decl(element, id, pseudo_type);
        const CssValue* value = declaration ? declaration->value : nullptr;
        if (pseudo_type == 0 && (css_animation_longhand_index(id) >= 0 ||
            css_transition_longhand_index(id) >= 0)) {
            const CssValue* animation_value = css_motion_computed_value(scratch, element, id);
            return format_css_value(element, id, animation_value, out, scratch);
        }
        // Legacy presentational attributes participate before inherited color is
        // consulted, including for dynamic computed-style reads without layout.
        if (!value && format_legacy_font_color(element, id, pseudo_type, out)) {
            if (preserve_color_model) return false;
            return true;
        }
        if (!value && id == CSS_PROPERTY_DIRECTION && pseudo_type == 0 &&
            dom_element_has_directionality_hint(element)) {
            // live HTML hints apply even when CSSOM reads precede the next layout pass.
            return copy_text(out, dom_css_element_directionality(element) > 0 ? "rtl" : "ltr");
        }
        // The specified-style tree keeps shorthands intact for CSSOM mutation.
        // Resolve their winning physical component before serializing a longhand.
        const CssValue* shorthand_value = computed_box_side_value(
            element, id, pseudo_type, &declaration);
        if (shorthand_value) value = shorthand_value;
        bool container_shorthand = declaration && declaration->property_code == CSS_PROPERTY_CONTAINER;
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
                container_shorthand ? CSS_PROPERTY_CONTAINER : background_shorthand ? CSS_PROPERTY_BACKGROUND : id);
            if (!value || !css_property_validate_value(container_shorthand ? CSS_PROPERTY_CONTAINER : background_shorthand ? CSS_PROPERTY_BACKGROUND : id, value))
                value = nullptr;
        }
        if (container_shorthand) value = css_container_shorthand_longhand(value, id, scratch);
        if (cssom_value_inherits(value, id)) {
            DomElement* parent = cssom_inherited_source(element, &pseudo_type);
            if (parent) {
                element = parent;
                pseudo_type = 0;
                continue;
            }
            value = nullptr;
        } else if (cssom_value_uses_initial(value)) {
            value = nullptr;
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
            return copy_text(out, "none");
        }
        return value ? format_css_value(element, id, value, out, scratch)
                     : !preserve_color_model && format_css_value(element, id, nullptr, out, scratch);
    }
    return false;
}

static bool serialize_decl_value(DomElement* element, CssPropertyCode id,
                                 int pseudo_type, StringBuf* out,
                                 bool preserve_color_model = false) {
    Pool* scratch = pool_create();
    if (!scratch) return false;
    bool result = serialize_inherited_decl(element, id, pseudo_type, out, scratch, preserve_color_model);
    pool_destroy(scratch);
    return result;
}

static bool serialize_decl(const CssPropAccessor* accessor, DomElement* element,
                           int pseudo_type, StringBuf* out) {
    return accessor && serialize_decl_value(element, accessor->id, pseudo_type, out);
}

static const CssValue* cssom_declared_font_value(Pool* scratch, DomElement* element,
                                                CssPropertyCode id, int pseudo_type) {
    CssDeclaration* longhand = computed_decl(element, id, pseudo_type);
    CssDeclaration* shorthand = computed_decl(element, CSS_PROPERTY_FONT, pseudo_type);
    const CssProperty* property = css_property_get_by_code(id);
    bool use_shorthand = property && css_font_shorthand_contains_property(property->name) &&
        shorthand && shorthand->value && (!longhand || css_declaration_cascade_compare(shorthand, longhand) > 0);
    const CssValue* value = use_shorthand ? shorthand->value : longhand ? longhand->value : nullptr;
    if (!value) return nullptr;
    const CssValue* resolved = css_resolve_element_var_value(scratch, element, value, id);
    CssPropertyCode source = use_shorthand ? CSS_PROPERTY_FONT : id;
    if (!resolved || !css_property_validate_value(source, resolved)) return nullptr;
    // font's omitted line-height resets to normal; its components share validation and projection.
    return use_shorthand && property
        ? css_font_shorthand_longhand(resolved, property->name, scratch) : resolved;
}

struct CssomFontState {
    FontStyleDesc font;
    CssValue line_height;
    bool vertical;
    bool sideways;
    bool upright;
};

struct CssomFontMathContext {
    DomElement* element;
    float parent_size;
    float root_size;
    const CssomFontState* metrics;
    const CssomFontState* leading;
    const CssomFontState* root;
    bool viewport_vertical;
};

static FontHandle* cssom_resolve_metric_font(DomElement* element, const CssomFontState* state) {
    UiContext* host = element && element->doc ? (UiContext*)element->doc->js.host_ui_context : nullptr;
    return host && host->font_ctx && state ? font_resolve(host->font_ctx, &state->font) : nullptr;
}

static float cssom_font_state_line_height(DomElement* element, const CssomFontState* state) {
    UiContext* host = element && element->doc ? (UiContext*)element->doc->js.host_ui_context : nullptr;
    float pixels = NAN;
    return state && css_font_line_height_px(host ? host->font_ctx : nullptr,
        &state->font, &state->line_height, &pixels) ? pixels : NAN;
}

static bool cssom_resolve_font_size_value(CssomFontMathContext* context,
    const CssValue* value, float* font_size, bool math_operand = false);

static bool cssom_font_math_leaf(void* data, const CssValue* value, double* result) {
    CssomFontMathContext* context = (CssomFontMathContext*)data;
    float size = 0.0f;
    if (!cssom_resolve_font_size_value(context, value, &size, true)) return false;
    *result = size;
    return true;
}

static bool cssom_resolve_font_size_value(CssomFontMathContext* context,
    const CssValue* value, float* font_size, bool math_operand) {
    if (!context || !context->element || !value || !font_size) return false;
    float resolved = -1.0f;
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        CssMathEvaluationContext math_context = {cssom_font_math_leaf, context, 1.0, false};
        CssMathResult math = css_math_evaluate(value, &math_context);
        if (!math.resolved || (math.type != CSS_MATH_LENGTH && math.type != CSS_MATH_PERCENT &&
            math.type != CSS_MATH_LENGTH_PERCENT && !(math.type == CSS_MATH_NUMBER && math.value == 0.0)))
            return false;
        resolved = isnan(math.value) ? 0.0 : math_operand ? math.value : fmax(0.0, math.value);
    } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
        CssUnit unit = value->data.length.unit;
        double number = value->data.length.value;
        double absolute_pixels = 0.0;
        if (css_absolute_length_to_px(unit, number, &absolute_pixels)) {
            resolved = (float)absolute_pixels;
        } else {
            DomDocument* document = context->element->doc;
            UiContext* host = document ? (UiContext*)document->js.host_ui_context : nullptr;
            CssEngine* engine = document ? (CssEngine*)document->services.cached_css_engine : nullptr;
            float viewport_width = host ? host->viewport_width
                : engine ? (float)engine->context.viewport_width : 0.0f;
            float viewport_height = host ? host->viewport_height
                : engine ? (float)engine->context.viewport_height : 0.0f;
            if (document && document->viewport.width > 0) viewport_width = document->viewport.width;
            if (document && document->viewport.height > 0) viewport_height = document->viewport.height;
            double viewport_pixels = 0.0;
            if (css_container_length_to_px(engine, context->element, unit, number, &viewport_pixels) ||
                css_viewport_length_to_px(unit, number, viewport_width, viewport_height,
                    context->viewport_vertical, &viewport_pixels)) {
                resolved = (float)viewport_pixels;
            } else if (unit == CSS_UNIT_LH || unit == CSS_UNIT_RLH) {
                resolved = number * cssom_font_state_line_height(context->element,
                    unit == CSS_UNIT_LH ? context->leading : context->root);
            } else if (unit == CSS_UNIT_EM) resolved = number * context->parent_size;
            else if (unit == CSS_UNIT_REM) resolved = number * context->root_size;
            else {
                FontHandle* handle = cssom_resolve_metric_font(context->element, context->metrics);
                float pixels = 0.0f;
                bool valid = context->metrics && css_font_metric_unit_px(handle,
                    &context->metrics->font, unit, context->metrics->font.size_px,
                    context->metrics->vertical && context->metrics->upright && !context->metrics->sideways, &pixels);
                // the font cache owns its entry; this read releases only its temporary caller reference.
                font_handle_release(handle);
                if (!valid) return false;
                resolved = number * pixels;
            }
        }
    } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        resolved = value->data.percentage.value * context->parent_size / 100.0;
    } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        if (value->data.number.value == 0.0) resolved = 0.0f;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INHERIT || keyword == CSS_VALUE_UNSET) resolved = context->parent_size;
        else if (keyword == CSS_VALUE_LARGER) resolved = context->parent_size * 1.2f;
        else if (keyword == CSS_VALUE_SMALLER) resolved = context->parent_size / 1.2f;
        else if (keyword == CSS_VALUE_INITIAL || keyword == CSS_VALUE_REVERT)
            resolved = css_font_size_keyword_px(CSS_VALUE_MEDIUM);
        else resolved = css_font_size_keyword_px(keyword);
    }
    if (!isfinite(resolved) || (!math_operand && resolved < 0.0f)) return false;
    *font_size = resolved;
    return true;
}

static void cssom_compute_font_state(Pool* scratch, DomElement* element, int pseudo_type,
    const CssomFontState* parent, const CssomFontState* root, CssomFontState* state,
    const CssValue* size, bool compute_leading) {
    *state = *parent;
    const CssValue* family = cssom_declared_font_value(scratch, element, CSS_PROPERTY_FONT_FAMILY, pseudo_type);
    if (family && !cssom_value_inherits(family, CSS_PROPERTY_FONT_FAMILY)) {
        if (cssom_value_uses_initial(family)) state->font.family = property_initial(CSS_PROPERTY_FONT_FAMILY);
        else {
            CssFormatter* formatter = css_formatter_create(scratch, CSS_FORMAT_COMPACT);
            if (formatter) {
                css_format_value(formatter, (CssValue*)family);
                String* text = stringbuf_to_string(formatter->output);
                if (text) state->font.family = text->chars;
            }
        }
    }
    const CssValue* weight = cssom_declared_font_value(scratch, element, CSS_PROPERTY_FONT_WEIGHT, pseudo_type);
    if (weight && !cssom_value_inherits(weight, CSS_PROPERTY_FONT_WEIGHT)) {
        if (weight->type == CSS_VALUE_TYPE_NUMBER) state->font.weight = (FontWeight)weight->data.number.value;
        else if (weight->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum keyword = weight->data.keyword;
            if (keyword == CSS_VALUE_BOLDER) state->font.weight = parent->font.weight < 350
                ? FONT_WEIGHT_NORMAL : parent->font.weight < 550 ? FONT_WEIGHT_BOLD : FONT_WEIGHT_BLACK;
            else if (keyword == CSS_VALUE_LIGHTER) state->font.weight = parent->font.weight < 550
                ? FONT_WEIGHT_THIN : parent->font.weight < 750 ? FONT_WEIGHT_NORMAL : FONT_WEIGHT_BOLD;
            else state->font.weight = keyword == CSS_VALUE_BOLD ? FONT_WEIGHT_BOLD : FONT_WEIGHT_NORMAL;
        }
    }
    const CssValue* slant = cssom_declared_font_value(scratch, element, CSS_PROPERTY_FONT_STYLE, pseudo_type);
    if (slant && !cssom_value_inherits(slant, CSS_PROPERTY_FONT_STYLE)) {
        const char* name = css_value_identifier_name(slant);
        state->font.slant = name && str_icmp_cstr(name, "italic") == 0 ? FONT_SLANT_ITALIC
            : name && str_icmp_cstr(name, "oblique") == 0 ? FONT_SLANT_OBLIQUE : FONT_SLANT_NORMAL;
    }
    const CssValue* writing = cssom_declared_font_value(scratch, element, CSS_PROPERTY_WRITING_MODE, pseudo_type);
    if (writing && !cssom_value_inherits(writing, CSS_PROPERTY_WRITING_MODE)) {
        CssEnum mode = writing->type == CSS_VALUE_TYPE_KEYWORD
            ? writing->data.keyword : CSS_VALUE_HORIZONTAL_TB;
        // sideways boxes retain vertical viewport axes while their glyph metrics stay horizontal.
        state->sideways = mode == CSS_VALUE_SIDEWAYS_RL || mode == CSS_VALUE_SIDEWAYS_LR;
        state->vertical = layout_writing_mode_from_css(mode) != WM_HORIZONTAL_TB;
    }
    const CssValue* orientation = cssom_declared_font_value(scratch, element, CSS_PROPERTY_TEXT_ORIENTATION, pseudo_type);
    if (orientation && !cssom_value_inherits(orientation, CSS_PROPERTY_TEXT_ORIENTATION)) {
        const char* name = css_value_identifier_name(orientation);
        state->upright = name && str_icmp_cstr(name, "upright") == 0;
    }
    bool is_root = pseudo_type == 0 && element->doc && element == element->doc->root;
    CssomFontMathContext lengths = {element, parent->font.size_px, root->font.size_px,
        parent, parent, root, state->vertical};
    if (size) cssom_resolve_font_size_value(&lengths, size, &state->font.size_px);
    lengths.parent_size = state->font.size_px;
    if (is_root) lengths.root_size = state->font.size_px;
    lengths.metrics = state;
    const CssValue* leading = compute_leading ? cssom_declared_font_value(
        scratch, element, CSS_PROPERTY_LINE_HEIGHT, pseudo_type) : nullptr;
    if (leading && !cssom_value_inherits(leading, CSS_PROPERTY_LINE_HEIGHT)) {
        if (cssom_value_uses_initial(leading)) state->line_height = *css_line_height_normal_value();
        else {
            CssMathEvaluationContext context = {cssom_font_math_leaf, &lengths, 1.0, false};
            css_compute_line_height_value(leading, &context, &state->line_height);
        }
    }
}

static bool cssom_resolve_font_state(Pool* scratch, DomElement* element, int pseudo_type,
    CssomFontState* root, CssomFontState* result, bool compute_leading = false) {
    if (!scratch || !element || !root || !result) return false;
    ArrayList* ancestors = cssom_collect_style_ancestors(element);
    if (!ancestors) return false;
    size_t count = (size_t)ancestors->length + (pseudo_type != 0 ? 1 : 0);
    const CssValue** sizes = (const CssValue**)pool_calloc(scratch, count * sizeof(CssValue*));
    if (!sizes) { arraylist_free(ancestors); return false; }
    bool needs_leading = compute_leading;
    for (size_t index = 0; index < count; index++) {
        DomElement* current = index < (size_t)ancestors->length
            ? (DomElement*)ancestors->data[(size_t)ancestors->length - index - 1] : element;
        sizes[index] = cssom_declared_font_value(scratch, current, CSS_PROPERTY_FONT_SIZE,
            index < (size_t)ancestors->length ? 0 : pseudo_type);
        needs_leading |= css_value_contains_length_unit(sizes[index], CSS_UNIT_LH, CSS_UNIT_RLH);
    }
    CssomFontState state = {};
    state.font = {property_initial(CSS_PROPERTY_FONT_FAMILY), css_font_size_keyword_px(CSS_VALUE_MEDIUM),
        FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
    state.line_height = *css_line_height_normal_value();
    *root = state;
    // font sizes and line-height dependencies compute once, from the CSS root toward the target.
    for (int index = ancestors->length; index > 0; index--) {
        CssomFontState current = {};
        cssom_compute_font_state(scratch, (DomElement*)ancestors->data[index - 1], 0, &state, root, &current,
            sizes[ancestors->length - index], needs_leading && (compute_leading || index > 1 || pseudo_type != 0));
        state = current;
        if (index == ancestors->length) *root = state;
    }
    if (pseudo_type != 0) {
        CssomFontState current = {};
        cssom_compute_font_state(scratch, element, pseudo_type, &state, root, &current,
            sizes[ancestors->length], compute_leading);
        state = current;
    }
    *result = state;
    arraylist_free(ancestors);
    return true;
}

bool css_prop_compute_numeric_value(DomElement* element, const CssValue* value, CssMathResult* result) {
    if (!element || !value || !result) return false;
    Pool* scratch = mem_pool_create(nullptr, MEM_ROLE_TEMP, "css.computed_length.scratch");
    if (!scratch) return false;
    CssomFontState root = {}, state = {};
    bool valid = cssom_resolve_font_state(scratch, element, 0, &root, &state, true);
    CssomFontMathContext lengths = {element, state.font.size_px, root.font.size_px,
        &state, &state, &root, state.vertical};
    value = css_resolve_element_var_value(scratch, element, value, CSS_PROPERTY_WIDTH);
    CssMathEvaluationContext math = {cssom_font_math_leaf, &lengths, 1.0, false};
    *result = css_math_evaluate(value, &math);
    valid = valid && result->resolved;
    mem_pool_destroy(scratch);
    return valid;
}

bool css_compute_cascaded_font_size(DomElement* element, float* font_size) {
    if (!font_size) return false;
    Pool* scratch = pool_create();
    if (!scratch) return false;
    CssomFontState root = {}, state = {};
    bool valid = cssom_resolve_font_state(scratch, element, 0, &root, &state);
    if (valid) *font_size = state.font.size_px;
    pool_destroy(scratch);
    return valid;
}

static bool serialize_cssom_font_size(DomElement* element, int pseudo_type, StringBuf* out) {
    dom_ensure_computed(element, false);
    Pool* scratch = pool_create();
    if (!scratch) return false;
    CssomFontState root = {}, state = {};
    bool success = cssom_resolve_font_state(scratch, element, pseudo_type, &root, &state) &&
        format_number(out, state.font.size_px, "px");
    pool_destroy(scratch);
    return success;
}

static bool serialize_line_height(const CssPropAccessor* accessor, DomElement* element,
    int pseudo_type, StringBuf* out) {
    if (!accessor || !element) return false;
    Pool* scratch = pool_create();
    if (!scratch) return false;
    CssomFontState root = {}, state = {};
    bool success = false;
    if (cssom_resolve_font_state(scratch, element, pseudo_type, &root, &state, true)) {
        if (state.line_height.type == CSS_VALUE_TYPE_KEYWORD) success = copy_text(out, "normal");
        else {
            float pixels = cssom_font_state_line_height(element, &state);
            if (!isnan(pixels)) success = format_number(out, layout_clamp_dimension(pixels), "px");
        }
    }
    pool_destroy(scratch);
    return success || serialize_decl(accessor, element, pseudo_type, out);
}

struct CssomSvgLengthContext {SvgLengthContext svg; CssomFontMathContext font;};

static bool cssom_svg_length_leaf(void* data, const CssValue* value, double* result) {
    CssomSvgLengthContext* context = (CssomSvgLengthContext*)data;
    if (!value || value->type != CSS_VALUE_TYPE_LENGTH) return false;
    float pixels = svg_resolve_length_unit((float)value->data.length.value,
        value->data.length.unit, &context->svg, SVG_LENGTH_DIAGONAL, NAN);
    if (isfinite(pixels)) {*result = pixels; return true;}
    return cssom_font_math_leaf(&context->font, value, result);
}

struct CssomSvgScalarWriter {
    CssFormatter* formatter;
    CssomSvgLengthContext lengths;
    bool nonnegative;
    bool comma;
    bool written;
};

static bool cssom_append_svg_length(const CssValue* input, void* data) {
    CssomSvgScalarWriter* writer = (CssomSvgScalarWriter*)data;
    // normalization writes only to the caller-owned projection, never retained declaration values (D4.5.1v4).
    CssValue* value = css_value_clone_owned(input, writer->formatter->pool);
    if (!value || !svg_normalize_length_value(value, &writer->lengths.svg, SVG_LENGTH_DIAGONAL,
        true, cssom_svg_length_leaf, &writer->lengths)) return false;
    CssMathEvaluationContext context = {cssom_svg_length_leaf, &writer->lengths, 1.0, true};
    CssMathResult math = css_math_evaluate(value, &context);
    if (writer->comma && writer->written) stringbuf_append_str(writer->formatter->output, ", ");
    writer->written = true;
    double scalar = isnan(math.value) ? 0.0 : math.value;
    if (writer->nonnegative) scalar = fmax(0.0, scalar);
    if (math.resolved && (math.type == CSS_MATH_NUMBER || math.type == CSS_MATH_LENGTH))
        stringbuf_append_format(writer->formatter->output, "%.6gpx", scalar);
    else if (math.resolved && math.type == CSS_MATH_PERCENT)
        stringbuf_append_format(writer->formatter->output, "%.6g%%",
            writer->nonnegative ? fmax(0.0, math.percentage) : math.percentage);
    else if (math.resolved && math.type == CSS_MATH_LENGTH_PERCENT)
        stringbuf_append_format(writer->formatter->output, "calc(%.6g%% %c %.6gpx)",
            math.percentage, math.value < 0.0 ? '-' : '+', fabs(math.value));
    else css_format_value(writer->formatter, value);
    return true;
}

String* css_prop_serialize_svg_value(Pool* pool, DomElement* declaring,
    CssPropertyCode id, const CssValue* value) {
    if (!pool || !declaring || !value) return nullptr;
    CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!formatter) return nullptr;
    formatter->options.computed_colors = true;
    formatter->options.quote_urls = true;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        css_format_value(formatter, (CssValue*)value);
    } else if (css_property_is_svg_length(id)) {
        CssomSvgScalarWriter writer = {formatter,
            {dom_svg_length_context(declaring), {declaring, 16.0f, 16.0f}},
            id != CSS_PROPERTY_STROKE_DASHOFFSET, id == CSS_PROPERTY_STROKE_DASHARRAY, false};
        CssomFontState root = {}, state = {};
        if (cssom_resolve_font_state(pool, declaring, 0, &root, &state, true)) {
            if (!dom_element_is_svg(declaring)) writer.lengths.svg.font_size = state.font.size_px;
            else state.font.size_px = writer.lengths.svg.font_size;
            writer.lengths.font = {declaring, state.font.size_px, root.font.size_px,
                &state, &state, &root, state.vertical};
        }
        if (id == CSS_PROPERTY_STROKE_DASHARRAY) {
            if (!css_value_visit_list_items(value, cssom_append_svg_length, &writer)) return nullptr;
        } else if (!cssom_append_svg_length(value, &writer)) return nullptr;
    } else if (css_property_is_svg_opacity(id)) {
        LayoutContext context = {};
        context.pool = lam::up(pool);
        context.view = lam::up(static_cast<View*>(declaring));
        stringbuf_append_format(formatter->output, "%.6g", resolve_css_opacity_value(&context, value));
    } else if (id == CSS_PROPERTY_STROKE_MITERLIMIT) {
        CssMathEvaluationContext context = {};
        CssMathResult math = css_math_evaluate(value, &context);
        if (!math.resolved || math.type != CSS_MATH_NUMBER) return nullptr;
        stringbuf_append_format(formatter->output, "%.6g", isnan(math.value) ? 0.0 : fmax(0.0, math.value));
    } else css_format_value(formatter, (CssValue*)value);
    return stringbuf_to_string(formatter->output);
}

static String* serialize_svg_paint_value(Pool* pool, DomElement* element, CssPropertyCode id) {
    const CssProperty* property = css_property_get_by_code(id);
    if (!pool || !element || !property) return nullptr;
    if (property->identity_shorthand) {
        // computed shorthands exist only when every member has the same complete value.
        String* selected = nullptr;
        for (int i = 0; i < property->longhand_count; i++) {
            String* member = css_prop_serialize_computed_value(pool, element, property->longhand_props[i], 0);
            if (!member) return nullptr;
            if (selected && (selected->len != member->len || memcmp(selected->chars, member->chars, member->len)))
                return create_string(pool, "");
            selected = member;
        }
        return selected;
    }
    char* owned_text = nullptr;
    DomElement* owner = nullptr;
    const char* text = svg_get_dom_presentation_property(element, property->name,
        property->inheritance == PROP_INHERIT_YES, nullptr, 0, nullptr, &owned_text, &owner);
    lam::Temp<char> text_owner(owned_text);
    CssValue* value = svg_parse_property_value(pool, text ? text : property->initial_value, property->name);
    if (!value) return nullptr;
    // only paint server fallbacks form a two-item color list; dash lists are length projections.
    CssValue* color = (id == CSS_PROPERTY_FILL || id == CSS_PROPERTY_STROKE) &&
        value->type == CSS_VALUE_TYPE_LIST ? value->data.list.values[1] : value;
    const char* keyword = css_value_identifier_name(value);
    if (keyword && (str_icmp_cstr(keyword, "context-fill") == 0 ||
        str_icmp_cstr(keyword, "context-stroke") == 0))
        return create_string(pool, str_icmp_cstr(keyword, "context-fill") == 0 ? "context-fill" : "context-stroke");
    CssComputedColor computed;
    if (css_color_compute(color, &computed) && computed.type == CSS_COLOR_CURRENTCOLOR) {
        char* owned_color = nullptr;
        const char* current = svg_get_dom_presentation_property(element, "color", true,
            nullptr, 0, nullptr, &owned_color);
        lam::Temp<char> color_owner(owned_color);
        CssValue* resolved = svg_parse_property_value(pool, current ? current : "black", "color");
        if (!resolved) return nullptr;
        // inherited currentColor resolves against the receiving element's color.
        if (color == value) value = resolved;
        else value->data.list.values[1] = resolved;
    }
    return css_prop_serialize_svg_value(pool, owner ? owner : element, id, value);
}

static bool serialize_svg_paint(const CssPropAccessor* accessor, DomElement* element,
    int pseudo_type, StringBuf* out) {
    if (pseudo_type != 0)
        return serialize_decl(accessor, element, pseudo_type, out);
    String* value = serialize_svg_paint_value(out->pool, element, accessor->id);
    return value && copy_text(out, value->chars);
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
        DomElement* element, int pseudo_type, StringBuf* out,
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
        return serialize_decl_value(element, shorthand_id, 0, out);
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
    if (self_alignment_equal(align, justify)) return copy_text(out, align_text);
    return format_text(out, "%s %s", align_text, justify_text);
}

static bool serialize_place_self(const CssPropAccessor*, DomElement* element,
                                 int pseudo_type, StringBuf* out) {
    return serialize_place_alignment(
        element, pseudo_type, out, CSS_PROPERTY_PLACE_SELF,
        CSS_PROPERTY_ALIGN_SELF, CSS_PROPERTY_JUSTIFY_SELF, CSS_VALUE_AUTO,
        css_parse_place_self_alignment, css_parse_self_alignment_value);
}

static bool serialize_place_content(const CssPropAccessor*, DomElement* element,
                                    int pseudo_type, StringBuf* out) {
    return serialize_place_alignment(
        element, pseudo_type, out, CSS_PROPERTY_PLACE_CONTENT,
        CSS_PROPERTY_ALIGN_CONTENT, CSS_PROPERTY_JUSTIFY_CONTENT, CSS_VALUE_NORMAL,
        css_parse_place_content_alignment, css_parse_content_alignment_value);
}

static bool serialize_display(const CssPropAccessor*, DomElement* element, int pseudo_type,
                              StringBuf* out) {
    if (!element || pseudo_type != 0) return false;
    if (element->display.outer == CSS_VALUE_NONE) return copy_text(out, "none");
    if (element->display.inner == CSS_VALUE_FLEX) {
        return copy_text(out,
            element->display.outer == CSS_VALUE_INLINE ? "inline-flex" : "flex");
    }
    if (element->display.inner == CSS_VALUE_GRID) {
        return copy_text(out,
            element->display.outer == CSS_VALUE_INLINE ? "inline-grid" : "grid");
    }
    if (element->display.inner == CSS_VALUE_TABLE) {
        return copy_text(out,
            element->display.outer == CSS_VALUE_INLINE ? "inline-table" : "table");
    }
    const CssEnumInfo* info = css_enum_info(element->display.outer);
    return copy_text(out, info && info->name ? info->name : "block");
}

static bool serialize_visibility(const CssPropAccessor*, DomElement* element,
                                 int pseudo_type, StringBuf* out) {
    if (!element || pseudo_type != 0) return false;
    const InlineProp* in_line = element->inl();
    Visibility visibility = in_line
        ? (Visibility)in_line->visibility : VIS_VISIBLE;
    // Visibility is a compact render enum, not a CssEnum; indexing the CSS
    // keyword table with it serialized VIS_HIDDEN as the unrelated "_length".
    switch (visibility) {
        case VIS_HIDDEN: return copy_text(out, "hidden");
        case VIS_COLLAPSE: return copy_text(out, "collapse");
        case VIS_VISIBLE:
        default: return copy_text(out, "visible");
    }
}

static bool serialize_transform(const CssPropAccessor* accessor, DomElement* element,
                                 int pseudo_type, StringBuf* out) {
    if (!accessor || !element || pseudo_type != 0) return false;
    TransformFunction* functions = element->transform ? element->transform->functions.get() : nullptr;
    if (!functions) return copy_text(out, "none");
    // CSS Transforms 1 §4.2 serializes the function product without transform-origin.
    RdtMatrix4 matrix = radiant::compute_transform_matrix_3d(functions,
        element->width, element->height, 0.0f, 0.0f, 0.0f);
    const float* values = matrix.values;
    bool is_2d = rdt_matrix4_is_2d(&matrix);
    const int planar_indices[] = {0, 4, 1, 5, 3, 7};
    int count = is_2d ? 6 : 16;
    if (!copy_text(out, is_2d ? "matrix(" : "matrix3d(")) return false;
    for (int index = 0; index < count; index++) {
        int component = is_2d ? planar_indices[index] : (index % 4) * 4 + index / 4;
        char number[64];
        int length = snprintf(number, sizeof(number), "%s%.6g", index ? ", " : "", values[component]);
        if (length < 0 || (size_t)length >= sizeof(number) ||
            !stringbuf_ensure_cap(out, out->length + (size_t)length + 2)) return false;
        stringbuf_append_str_n(out, number, (size_t)length);
    }
    stringbuf_append_char(out, ')');
    return true;
}

static bool serialize_used_size(const CssPropAccessor* accessor, DomElement* element,
                                int pseudo_type, StringBuf* out) {
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
    return format_number(out, value, "px");
}

static bool append_box_sides(StringBuf* output, String* values[4]) {
    unsigned count = 4;
    if (strcmp(values[3]->chars, values[1]->chars) == 0) {
        count = 3;
        if (strcmp(values[2]->chars, values[0]->chars) == 0) {
            count = strcmp(values[1]->chars, values[0]->chars) == 0 ? 1 : 2;
        }
    }
    for (unsigned i = 0; i < count; i++) {
        if (!stringbuf_append_format(output, "%s%s", i ? " " : "", values[i]->chars)) return false;
    }
    return true;
}

static bool serialize_corner_radius(const CssPropAccessor* accessor, DomElement* element,
                                    int pseudo_type, StringBuf* out) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const BoundaryProp* boundary = element->boundary();
    const Corner* radius = boundary && boundary->border ? &boundary->border->radius : nullptr;
    const CornerExpressions* expressions = radius ? radius->expressions.get() : nullptr;
    if (expressions) radius = &expressions->computed;
    // axis strings share the serializer's temporary pool without imposing a size limit (D4.5.1v4).
    String* axes[2][4];
    for (unsigned axis = 0; axis < 2; axis++) {
        for (unsigned corner = 0; corner < 4; corner++) {
            const CssValue* expression = expressions ? (axis ? expressions->vertical[corner].get()
                : expressions->horizontal[corner].get()) : nullptr;
            float value = radius ? (axis ? radius->vertical[corner] : radius->horizontal[corner]) : 0.0f;
            bool percent = radius && (axis ? radius->vertical_percent[corner] : radius->horizontal_percent[corner]);
            StringBuf* text = stringbuf_new(out->pool);
            if (!text || !(expression
                ? format_css_value(element, accessor->id, expression, text, out->pool)
                : format_number(text, value, percent ? "%" : "px"))) return false;
            axes[axis][corner] = stringbuf_to_string(text);
            if (!axes[axis][corner]) return false;
        }
    }
    stringbuf_reset(out);
    if (accessor->id == CSS_PROPERTY_BORDER_RADIUS) {
        if (!append_box_sides(out, axes[0])) return false;
        bool same = true;
        for (unsigned corner = 0; corner < 4; corner++)
            same = same && strcmp(axes[0][corner]->chars, axes[1][corner]->chars) == 0;
        if (!same) {
            if (!stringbuf_append_format(out, " / ") || !append_box_sides(out, axes[1])) return false;
        }
    } else {
        const CssPropertyCode physical[] = {CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS,
            CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS, CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS,
            CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS};
        int corner = -1;
        for (unsigned i = 0; i < 4; i++) if (accessor->id == physical[i]) corner = i;
        if (corner < 0) corner = css_logical_corner_index(accessor->id, element);
        if (corner < 0 || !copy_text(out, axes[0][corner]->chars)) return false;
        if (strcmp(axes[0][corner]->chars, axes[1][corner]->chars) != 0 &&
            !stringbuf_append_format(out, " %s", axes[1][corner]->chars)) return false;
    }
    return true;
}

static bool serialize_edge(const CssPropAccessor* accessor, DomElement* element,
                           int pseudo_type, StringBuf* out) {
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
    if (type == CSS_VALUE_AUTO) return copy_text(out, "auto");
    return format_number(out, value, "px");
}

static bool serialize_border_component(const CssPropAccessor* accessor,
                                       DomElement* element, int pseudo_type,
                                       StringBuf* out) {
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
            return format_number(out, side.width ? *side.width : 0.0f, "px");
        case CSS_PROPERTY_BORDER_TOP_STYLE:
        case CSS_PROPERTY_BORDER_RIGHT_STYLE:
        case CSS_PROPERTY_BORDER_BOTTOM_STYLE:
        case CSS_PROPERTY_BORDER_LEFT_STYLE: {
            const CssEnumInfo* info = side.style ? css_enum_info(*side.style) : nullptr;
            return copy_text(out, info && info->name ? info->name : "none");
        }
        default:
            // colors come from the computed side, including winning shorthands.
            return format_color(out, side.color ? *side.color : element->inl()->color);
    }
}

static bool serialize_border_colors(const CssPropAccessor* accessor, DomElement* element,
                                     int pseudo_type, StringBuf* out) {
    const CssProperty* property = accessor ? css_property_get_by_code(accessor->id) : nullptr;
    if (!property || property->longhand_count != 4) return false;
    String* values[4];
    for (unsigned side = 0; side < 4; side++) {
        // delegate computed colors so currentColor, cascade and animation
        // sampling follow the same path as each physical longhand.
        StringBuf* text = stringbuf_new(out->pool);
        if (!text || !serialize_computed(element, property->longhand_props[side], pseudo_type, text)) return false;
        values[side] = stringbuf_to_string(text);
        if (!values[side]) return false;
    }
    stringbuf_reset(out);
    return append_box_sides(out, values);
}

static bool serialize_inset(const CssPropAccessor* accessor, DomElement* element,
                            int pseudo_type, StringBuf* out) {
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
    return present ? format_number(out, value, "px")
                   : copy_text(out, "auto");
}

static bool serialize_font_weight(const CssPropAccessor*, DomElement* element, int pseudo_type,
                                  StringBuf* out) {
    if (!element || pseudo_type != 0) return false;
    const FontProp* font = element->fontp();
    if (font->font_weight_numeric > 0) {
        return format_text(out, "%d", (int)font->font_weight_numeric);
    }
    const CssEnumInfo* info = css_enum_info(font->font_weight);
    return copy_text(out, info && info->name ? info->name : "normal");
}

static bool serialize_color_prop(const CssPropAccessor*, DomElement* element, int pseudo_type,
                                 StringBuf* out) {
    if (!element || pseudo_type != 0) return false;
    const InlineProp* inl = element->inl();
    Color color = inl->color;
    if (!inl->has_color) { color.r = color.g = color.b = 0; color.a = 255; }
    return format_color(out, color);
}

static bool serialize_background_color(const CssPropAccessor*, DomElement* element,
                                       int pseudo_type, StringBuf* out) {
    if (!element || pseudo_type != 0) return false;
    const BoundaryProp* boundary = element->boundary();
    // recascade can precede event-turn layout, leaving the old background paint cache intact.
    bool dirty_cascade = element->doc && element->doc->js.mutation_count > 0;
    if (boundary && boundary->background &&
        (!dirty_cascade || css_animation_needs_computed_sample(element, CSS_PROPERTY_BACKGROUND_COLOR))) {
        return format_color(out, boundary->background->color);
    }

    // use the common shorthand/substitution/inheritance path before paint has caught up.
    return serialize_decl_value(element, CSS_PROPERTY_BACKGROUND_COLOR, pseudo_type, out);
}

static bool serialize_minmax(const CssPropAccessor* accessor, DomElement* element,
                             int pseudo_type, StringBuf* out) {
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
    if (value < 0.0f) return copy_text(out, maximum ? "none" : "0px");
    return format_number(out, value, "px");
}

static bool serialize_z_index(const CssPropAccessor*, DomElement* element, int pseudo_type,
                              StringBuf* out) {
    if (!element || pseudo_type != 0) return false;
    const PositionProp* position = element->positionp();
    if (position->position == CSS_VALUE_STATIC && position->z_index == 0) {
        return copy_text(out, "auto");
    }
    return format_text(out, "%d", position->z_index);
}

static bool serialize_outline(const CssPropAccessor* accessor, DomElement* element,
                              int pseudo_type, StringBuf* out) {
    if (!accessor || !element || pseudo_type != 0) return false;
    OutlineProp* outline = element->boundary() ? element->boundary()->outline : nullptr;
    if (!outline) {
        if (accessor->id == CSS_PROPERTY_OUTLINE_STYLE) return copy_text(out, "none");
        if (accessor->id == CSS_PROPERTY_OUTLINE_WIDTH) return copy_text(out, "0px");
        return copy_text(out, "rgba(0, 0, 0, 0)");
    }
    if (accessor->id == CSS_PROPERTY_OUTLINE_STYLE) {
        const CssEnumInfo* info = css_enum_info(outline->style);
        return copy_text(out, info && info->name ? info->name : "none");
    }
    if (accessor->id == CSS_PROPERTY_OUTLINE_WIDTH) {
        return format_number(out, outline->width, "px");
    }
    if (accessor->id == CSS_PROPERTY_OUTLINE_COLOR) {
        return format_color(out, outline->color);
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
    DomElement* element, int, StringBuf* out) {
    int index = css_individual_transform_index(accessor->id);
    const TransformFunction* function = element && element->transform
        ? &element->transformp()->individual[index] : nullptr;
    if (!function || function->type == TRANSFORM_NONE) return copy_text(out, "none");
    if (accessor->id == CSS_PROPERTY_TRANSLATE) {
        bool three_d = function->type == TRANSFORM_TRANSLATE3D;
        float x = three_d ? function->params.translate3d.x : function->params.translate.x;
        float y = three_d ? function->params.translate3d.y : function->params.translate.y;
        float z = three_d ? function->params.translate3d.z : 0.0f;
        char components[3][64];
        bool x_percent = !isnan(function->translate_x_percent);
        bool y_percent = !isnan(function->translate_y_percent);
        snprintf(components[0], sizeof(components[0]), "%.6g%s",
            x_percent ? function->translate_x_percent : x, x_percent ? "%" : "px");
        snprintf(components[1], sizeof(components[1]), "%.6g%s",
            y_percent ? function->translate_y_percent : y, y_percent ? "%" : "px");
        snprintf(components[2], sizeof(components[2]), "%.6gpx", z);
        if (z != 0.0f) return format_text(out, "%s %s %s", components[0], components[1], components[2]);
        else if (y != 0.0f || y_percent) return format_text(out, "%s %s", components[0], components[1]);
        else return copy_text(out, components[0]);
    } else if (accessor->id == CSS_PROPERTY_SCALE) {
        bool three_d = function->type == TRANSFORM_SCALE3D;
        float x = three_d ? function->params.scale3d.x : function->params.scale.x;
        float y = three_d ? function->params.scale3d.y : function->params.scale.y;
        float z = three_d ? function->params.scale3d.z : 1.0f;
        if (z != 1.0f) return format_text(out, "%.6g %.6g %.6g", x, y, z);
        else if (x != y) return format_text(out, "%.6g %.6g", x, y);
        else return format_number(out, x, "");
    } else {
        bool three_d = function->type == TRANSFORM_ROTATE3D;
        float angle = (float)math_radians_to_degrees_d(three_d
            ? function->params.rotate3d.angle : function->params.angle);
        if (!three_d || (function->params.rotate3d.x == 0.0f &&
            function->params.rotate3d.y == 0.0f && function->params.rotate3d.z > 0.0f)) {
            return format_number(out, angle, "deg");
        }
        if (function->params.rotate3d.z == 0.0f &&
            ((function->params.rotate3d.x != 0.0f && function->params.rotate3d.y == 0.0f) ||
             (function->params.rotate3d.y != 0.0f && function->params.rotate3d.x == 0.0f))) {
            bool x_axis = function->params.rotate3d.x != 0.0f;
            float axis = x_axis ? function->params.rotate3d.x : function->params.rotate3d.y;
            return format_text(out, "%s %.6gdeg", x_axis ? "x" : "y", axis < 0.0f ? -angle : angle);
        }
        return format_text(out, "%.6g %.6g %.6g %.6gdeg", function->params.rotate3d.x,
            function->params.rotate3d.y, function->params.rotate3d.z, angle);
    }
    return true;
}

static bool serialize_scroll_snap(const CssPropAccessor* accessor,
                                  DomElement* element, int pseudo_type,
                                  StringBuf* out) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const ScrollProp* scroll = element->scroll();
    if (accessor->id == CSS_PROPERTY_SCROLL_SNAP_TYPE) {
        const char* axis = scroll_snap_axis_name(scroll->snap_axis);
        if (scroll->snap_axis == SCROLL_SNAP_AXIS_NONE ||
            !scroll->snap_strictness_explicit) {
            return copy_text(out, axis);
        }
        return format_text(out, "%s %s", axis,
                 scroll->snap_mandatory ? "mandatory" : "proximity");
    }
    const CssEnumInfo* block = css_enum_info(scroll->snap_align_block);
    const CssEnumInfo* inline_axis = css_enum_info(scroll->snap_align_inline);
    return format_text(out, "%s %s",
             block && block->name ? block->name : "none",
             inline_axis && inline_axis->name ? inline_axis->name : "none");
}

static bool serialize_overscroll_behavior(const CssPropAccessor* accessor,
                                          DomElement* element, int pseudo_type,
                                          StringBuf* out) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const ScrollProp* scroll = element->scroll();
    const CssEnumInfo* x = css_enum_info(scroll->overscroll_x);
    const CssEnumInfo* y = css_enum_info(scroll->overscroll_y);
    const char* x_name = x && x->name ? x->name : "auto";
    const char* y_name = y && y->name ? y->name : "auto";
    if (strcmp(x_name, y_name) == 0) return copy_text(out, x_name);
    return format_text(out, "%s %s", x_name, y_name);
}

#define DIRECT_ROW(prop_id, group, type, field, kind, row_flags) \
    {prop_id, group, (uint16_t)offsetof(type, field), kind, row_flags, serialize_direct, nullptr}
#define DERIVED_ROW(prop_id, fn, row_flags) \
    {prop_id, PROP_GROUP_NONE, 0, CSS_PROP_VALUE_SPECIAL, row_flags, fn, fn}
#define DECL_ROW(prop_id) DERIVED_ROW(prop_id, serialize_decl, 0)

static const CssPropAccessor CSS_PROP_ROWS[] = {
    DERIVED_ROW(CSS_PROPERTY_FILL, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STROKE, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STROKE_WIDTH, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_FILL_OPACITY, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STROKE_OPACITY, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STROKE_DASHARRAY, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STROKE_DASHOFFSET, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STROKE_LINECAP, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STROKE_LINEJOIN, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STROKE_MITERLIMIT, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_FILL_RULE, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_CLIP_RULE, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_PAINT_ORDER, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STOP_COLOR, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_STOP_OPACITY, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_FLOOD_COLOR, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_FLOOD_OPACITY, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_LIGHTING_COLOR, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_MARKER_START, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_MARKER_MID, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_MARKER_END, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_MARKER, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_VECTOR_EFFECT, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_COLOR_INTERPOLATION, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_COLOR_INTERPOLATION_FILTERS, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_TEXT_ANCHOR, serialize_svg_paint, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
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
    // hit testing reads the cascade directly; CSSOM must expose that same inherited value.
    DERIVED_ROW(CSS_PROPERTY_POINTER_EVENTS, serialize_decl, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
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
    // serialize computed corners, preserving percentages and ignoring paint overlap constraints.
    DERIVED_ROW(CSS_PROPERTY_BORDER_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_START_START_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_START_END_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_END_START_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_END_END_RADIUS, serialize_corner_radius, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BORDER_RIGHT_WIDTH, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_BOTTOM_WIDTH, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_LEFT_WIDTH, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_TOP_STYLE, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_RIGHT_STYLE, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_BOTTOM_STYLE, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_LEFT_STYLE, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_TOP_COLOR, serialize_border_component, 0),
    DERIVED_ROW(CSS_PROPERTY_BORDER_COLOR, serialize_border_colors, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
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
    DECL_ROW(CSS_PROPERTY_CONTAIN),
    DECL_ROW(CSS_PROPERTY_CONTENT_VISIBILITY),
    DECL_ROW(CSS_PROPERTY_CONTAINER_NAME),
    DECL_ROW(CSS_PROPERTY_CONTAINER_TYPE),
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

static void cssom_refresh_cascade_chain(DomElement* element) {
    ArrayList* ancestors = cssom_collect_style_ancestors(element);
    if (!ancestors) return;
    // inheritance and declaration-site variables need live ancestors before pointers are borrowed.
    SelectorMatcher matcher;
    selector_matcher_init(&matcher, element->doc->document_pool);
    for (int index = ancestors->length; index > 0; index--) {
        radiant_cascade_styles_for_element_with_matcher(
            (DomElement*)ancestors->data[index - 1], &matcher);
    }
    arraylist_free(ancestors);
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
    }

    // host-driven handlers retain committed geometry while declaration reads refresh inheritance.
    cssom_refresh_cascade_chain(element);
    return false;
}

static bool serialize_computed(DomElement* element, CssPropertyCode id,
                                 int pseudo_type, StringBuf* out) {
    if (!element || !out) return false;
    stringbuf_reset(out);
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
        return serialize_cssom_font_size(element, pseudo_type, out);
    }
    if (css_property_is_svg_presentation(id) && pseudo_type == 0) {
        dom_ensure_computed(element, false);
        return accessor->serialize(accessor, element, pseudo_type, out);
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
        serialize_decl_value(element, id, pseudo_type, out, true)) return true;

    if (!computed) {
        // Before the first UiContext exists, loader scripts can only observe
        // the already-cascaded declaration tree; keep this compatibility seam
        // inside the table instead of reviving a second JS serializer.
        if ((accessor->flags & CSS_PROP_ACCESSOR_CASCADE_RESOLVED) ||
            (id >= CSS_PROPERTY_BORDER_TOP_WIDTH &&
             id <= CSS_PROPERTY_BORDER_LEFT_STYLE)) {
            return accessor->serialize && accessor->serialize(
                accessor, element, pseudo_type, out);
        }
        return serialize_decl(accessor, element, pseudo_type, out);
    }
    if (pseudo_type != 0) return serialize_decl(accessor, element, pseudo_type, out);
    return accessor->serialize && accessor->serialize(accessor, element, pseudo_type,
                                                      out);
}

String* css_prop_serialize_computed_value(Pool* pool, DomElement* element,
    CssPropertyCode id, int pseudo_type) {
    if (!pool || !element) return nullptr;
    // intermediate formatter trees never accumulate in a retained caller pool (D4.5.1v4).
    Pool* scratch = pool_create();
    if (!scratch) return nullptr;
    StringBuf* output = stringbuf_new(scratch);
    String* value = output && serialize_computed(element, id, pseudo_type, output)
        ? stringbuf_to_string(output) : nullptr;
    String* result = value ? create_string(pool, value->chars) : nullptr;
    pool_destroy(scratch);
    return result;
}

bool css_prop_serialize_computed(DomElement* element, CssPropertyCode id,
    int pseudo_type, char* out, size_t out_size) {
    if (!out || !out_size) return false;
    out[0] = '\0';
    Pool* scratch = pool_create();
    if (!scratch) return false;
    StringBuf* output = stringbuf_new(scratch);
    String* value = output && serialize_computed(element, id, pseudo_type, output)
        ? stringbuf_to_string(output) : nullptr;
    // a bounded projection succeeds only when the complete computed value fits.
    bool success = value && value->len < out_size;
    if (success) str_copy(out, out_size, value->chars, value->len);
    pool_destroy(scratch);
    return success;
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
        ? css_match_property_syntax(registration, css_value_unwrap(value)) : nullptr;
    CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!formatter) {pool_destroy(scratch); return nullptr;}
    formatter->options.computed_colors = matched && matched->type == CSS_SYNTAX_COLOR;
    formatter->options.preserve_tokens = !registration || registration->universal;
    if (value && text.str) {
        // authored custom tokens preserve spelling; typed registrations use computed serialization.
        stringbuf_append_str_n(formatter->output, text.str, text.length);
    } else if (value) css_format_value(formatter, (CssValue*)value);
    String* result = stringbuf_to_string(formatter->output);
    pool_destroy(scratch);
    return result;
}
