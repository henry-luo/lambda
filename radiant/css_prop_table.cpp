#include "view.hpp"
#include "layout.hpp"
#include "render.hpp"
#include "view_tree_model.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/math_utils.h"
#include "../lib/mem_factory.h"
#include "../lib/arraylist.h"
#include "../lib/tagged.hpp"
#include "../lambda/dom/dom.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RadiantCssomUsedValueSync s_cssom_used_value_sync = nullptr;

static LayoutContext cssom_value_context(DomElement* element, Pool* scratch) {
    LayoutContext context = {};
    context.pool = lam::up(scratch);
    context.css_value_scratch = lam::up(scratch);
    context.view = lam::up(static_cast<View*>(element));
    context.elmt = lam::up(element);
    context.doc = element ? element->doc : nullptr;
    context.css_query_probe = true;
    return context;
}

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
        CSS_PROPERTY_HYPHENATE_LIMIT_CHARS,
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

static const void* prop_group_base(const DomElement* element, PropGroupKind group,
        bool include_item_defaults = true) {
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
            return element->flex_item() ? element->flex_item()
                : include_item_defaults ? &FLEX_ITEM_PROP_DEFAULT : nullptr;
        case PROP_GROUP_GRID_ITEM:
            return element->grid_item() ? element->grid_item()
                : include_item_defaults ? &GRID_ITEM_PROP_DEFAULT : nullptr;
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
    unsigned grid = css_grid_property_mask(id);
    if (grid > 15 && !(grid & (grid - 1))) {
        if (pseudo_type != 0) {
            // grid component winners are axis-independent and include retained pseudo shorthands.
            return layout_cascaded_style_declaration(
                dom_element_get_pseudo_style(element, pseudo_type), id, {});
        }
        return layout_cascaded_physical_declaration(element, id);
    }
    if (pseudo_type != 0) {
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

static bool format_decl_color(const CssValue* value, StringBuf* out) {
    CssComputedColor color;
    if (!css_color_compute(value, &color)) return false;
    if (color.type == CSS_COLOR_COLOR || color.type == CSS_COLOR_CURRENTCOLOR || color.missing) return false;
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

static bool format_grid_computed_value(DomElement* element, const CssValue* value, StringBuf* output,
    CssPropertyCode property, int pseudo_type);
static bool format_overflow_clip_margin_computed_value(DomElement* element,
    const CssValue* value, StringBuf* output, int pseudo_type);
static bool format_clamp_computed_value(DomElement* element, const CssValue* value,
    StringBuf* output, int pseudo_type);
static bool cssom_compute_numeric_value(DomElement* element, const CssValue* value,
    CssMathResult* result, int pseudo_type, bool preserve_percentages = false);
static bool cssom_compute_integer_value(DomElement* element, const CssValue* value,
    int pseudo_type, double minimum, double maximum, double* result);
typedef bool (*CssomScalarPolicy)(const CssMathResult*, float*);
// native sinks consume a transient stack target and return an independently owned value.
struct CssomValueTarget {
    bool (*consume)(DomElement*, const CssValue*, int, void*);
    void* result;
};
static bool cssom_compute_scalar_value(DomElement* element, const CssValue* value,
    int pseudo_type, CssomScalarPolicy compute, float* result);
static bool format_cssom_scalar_value(DomElement* element, const CssValue* value,
    StringBuf* output, int pseudo_type, CssomScalarPolicy compute);
static bool format_cssom_length_value(DomElement* element, const CssValue* value,
    StringBuf* output, int pseudo_type);

static bool format_css_value(DomElement* element, CssPropertyCode id,
                             const CssValue* value, StringBuf* out,
                             Pool* scratch = nullptr, int pseudo_type = 0) {
    // word-spacing normal computes to zero even before a pending style obtains a font snapshot.
    if (id == CSS_PROPERTY_WORD_SPACING && (!value || css_value_has_identifier(value, "normal")))
        return copy_text(out, "0px");
    if (!value) {
        // initial color names still serialize as computed colors after a rule stops matching.
        const CssProperty* property = css_property_get_by_code(id);
        if (property && property->type == PROP_TYPE_COLOR &&
            format_color_text(property_initial(id), out)) return true;
        return copy_text(out, property_initial(id));
    }
    if (id == CSS_PROPERTY_GRID_TEMPLATE_COLUMNS || id == CSS_PROPERTY_GRID_TEMPLATE_ROWS ||
        id == CSS_PROPERTY_GRID_AUTO_COLUMNS || id == CSS_PROPERTY_GRID_AUTO_ROWS)
        return format_grid_computed_value(element, value, out, id, pseudo_type);
    if (id == CSS_PROPERTY_OVERFLOW_CLIP_MARGIN)
        return format_overflow_clip_margin_computed_value(element, value, out, pseudo_type);
    if (id == CSS_PROPERTY_LINE_CLAMP || id == CSS_PROPERTY_WEBKIT_LINE_CLAMP)
        return format_clamp_computed_value(element, value, out, pseudo_type);
    if ((id == CSS_PROPERTY_Z_INDEX || id == CSS_PROPERTY_ORDER) && value->type != CSS_VALUE_TYPE_KEYWORD) {
        double level = 0.0;
        if (!cssom_compute_integer_value(element, value, pseudo_type,
                (double)INT_MIN, (double)INT_MAX, &level)) return false;
        return format_text(out, "%d", (int)level); // INT_CAST_OK: bounded integer stacking/item order
    }
    if (id == CSS_PROPERTY_HYPHENATE_LIMIT_CHARS) {
        const CssValue* components[3];
        if (!css_hyphenate_limit_components(value, components)) return false;
        int limits[3] = {-1, -1, -1};
        for (size_t i = 0; i < 3; i++) {
            if (!components[i] || css_value_keyword_equals(components[i], CSS_VALUE_AUTO)) continue;
            double count = 0.0;
            if (!cssom_compute_integer_value(element, components[i], pseudo_type,
                    0.0, (double)INT_MAX, &count)) return false;
            limits[i] = (int)count; // INT_CAST_OK: computed character-count limit
        }
        // CSSOM omits a repeated suffix and a remaining auto prefix.
        size_t count = limits[2] == limits[1] ? 2 : 3;
        if (count == 2 && limits[1] < 0) count = 1;
        stringbuf_reset(out);
        for (size_t i = 0; i < count; i++) {
            bool written = limits[i] < 0
                ? stringbuf_append_format(out, "%sauto", i ? " " : "")
                : stringbuf_append_format(out, "%s%d", i ? " " : "", limits[i]);
            if (!written) return false;
        }
        return true;
    }
    if (id == CSS_PROPERTY_WEBKIT_BOX_ORIENT) {
        return copy_text(out, css_value_has_identifier(value, "vertical") ||
            css_value_has_identifier(value, "block-axis") ? "vertical" : "horizontal");
    }
    if (id == CSS_PROPERTY_GRID_TEMPLATE_AREAS) {
        String* text = css_grid_serialize_template_areas(out->pool, value);
        return text && copy_text(out, text->chars);
    }
    if (id == CSS_PROPERTY_GRID_COLUMN_START || id == CSS_PROPERTY_GRID_COLUMN_END ||
        id == CSS_PROPERTY_GRID_ROW_START || id == CSS_PROPERTY_GRID_ROW_END) {
        return format_grid_computed_value(element, value, out, id, pseudo_type);
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
    if (id == CSS_PROPERTY_OPACITY)
        return format_cssom_scalar_value(element, value, out, pseudo_type, css_compute_opacity_result);
    if (id == CSS_PROPERTY_FLEX_GROW || id == CSS_PROPERTY_FLEX_SHRINK)
        return format_cssom_scalar_value(element, value, out, pseudo_type, css_compute_nonnegative_number_result);
    if (id == CSS_PROPERTY_FLEX_BASIS && value->type != CSS_VALUE_TYPE_KEYWORD)
        return format_cssom_length_value(element, value, out, pseudo_type);
    if (id == CSS_PROPERTY_COLOR || id == CSS_PROPERTY_BACKGROUND_COLOR ||
        id == CSS_PROPERTY_BORDER_TOP_COLOR || id == CSS_PROPERTY_BORDER_RIGHT_COLOR ||
        id == CSS_PROPERTY_BORDER_BOTTOM_COLOR || id == CSS_PROPERTY_BORDER_LEFT_COLOR) {
        // declaration colors use live CSSOM color resolution without committing geometry.
        if (format_decl_color(value, out)) return true;
    }
    Pool* pool = scratch ? scratch : element && element->doc ? element->doc->document_pool : nullptr;
    if (!pool) return false;
    CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!formatter) return false;
    CssComputedColor color;
    const CssProperty* property = css_property_get_by_code(id);
    formatter->options.computed_colors = property && property->type == PROP_TYPE_COLOR && css_color_compute(value, &color);
    if (id == CSS_PROPERTY_GRID_AUTO_FLOW) css_format_value_with_property(formatter, (CssValue*)value, id);
    else css_format_value(formatter, (CssValue*)value);
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

static bool consume_inherited_decl(DomElement* element, CssPropertyCode id,
                                     int pseudo_type, StringBuf* out,
                                     Pool* scratch, bool preserve_color_model,
                                     const CssomValueTarget* native_value) {
    if (!element) return false;
    DomElement* receiver = element;
    int receiver_pseudo = pseudo_type;
    // walk the complete CSS ancestry without consuming native stack frames.
    while (element) {
        CssDeclaration* declaration = computed_decl(element, id, pseudo_type);
        unsigned grid_mask = css_grid_property_mask(id);
        if (grid_mask && grid_mask < 16 && !(grid_mask & (grid_mask - 1))) {
            const CssPropertyCode shorthands[] = {CSS_PROPERTY_GRID_AREA, CSS_PROPERTY_GRID_ROW, CSS_PROPERTY_GRID_COLUMN};
            for (CssPropertyCode shorthand : shorthands) {
                if (!(grid_mask & css_grid_placement_mask(shorthand))) continue;
                CssDeclaration* candidate = computed_decl(element, shorthand, pseudo_type);
                if (candidate && (!declaration || css_declaration_cascade_compare(candidate, declaration) > 0))
                    declaration = candidate;
            }
        }
        const CssValue* value = declaration ? declaration->value : nullptr;
        bool grid_shorthand = grid_mask && declaration && declaration->property_code != id;
        if (pseudo_type == 0 && (css_animation_longhand_index(id) >= 0 ||
            css_transition_longhand_index(id) >= 0)) {
            if (native_value) return false;
            const CssValue* animation_value = css_motion_computed_value(scratch, element, id);
            return format_css_value(element, id, animation_value, out, scratch);
        }
        // Legacy presentational attributes participate before inherited color is
        // consulted, including for dynamic computed-style reads without layout.
        if (!native_value && !value && format_legacy_font_color(element, id, pseudo_type, out)) {
            if (preserve_color_model) return false;
            return true;
        }
        if (!native_value && !value && id == CSS_PROPERTY_DIRECTION && pseudo_type == 0 &&
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
        bool flex_shorthand = declaration && declaration->property_code == CSS_PROPERTY_FLEX && id != CSS_PROPERTY_FLEX;
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
            CssPropertyCode value_property = declaration && declaration->pending_shorthand_code > 0
                ? declaration->pending_shorthand_code : grid_shorthand ? declaration->property_code
                : container_shorthand || flex_shorthand ? declaration->property_code
                : background_shorthand ? CSS_PROPERTY_BACKGROUND : id;
            value = css_resolve_element_var_value(scratch, element, value,
                value_property);
            if (!value || !css_property_validate_value(value_property, value))
                value = nullptr;
        }
        CssPropertyCode projection = declaration && declaration->pending_shorthand_code > 0
            ? declaration->pending_shorthand_code
            : grid_shorthand || container_shorthand || flex_shorthand ? declaration->property_code : id;
        value = css_project_shorthand_longhand(scratch, value, projection, id);
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
        if (!native_value && preserve_color_model) {
            CssComputedColor color;
            // A byte paint snapshot cannot serialize a predefined color space or missing channels.
            if (!css_color_compute(value, &color) || (color.type != CSS_COLOR_COLOR && !color.missing)) return false;
        }
        const CssProperty* property = css_property_get_by_code(id);
        if (!native_value && id != CSS_PROPERTY_COLOR && property && property->type == PROP_TYPE_COLOR) {
            CssComputedColor color;
            bool current = value ? css_color_compute(value, &color) && color.type == CSS_COLOR_CURRENTCOLOR
                : str_icmp_cstr(property_initial(id), "currentColor") == 0;
            // inherited currentColor stays a keyword until it uses the receiving element or pseudo's color.
            if (current) return serialize_computed(receiver, CSS_PROPERTY_COLOR, receiver_pseudo, out);
        }
        if (!native_value && id == CSS_PROPERTY_CONTENT && pseudo_type != 0 &&
            (!value || (value->type == CSS_VALUE_TYPE_KEYWORD &&
                        value->data.keyword == CSS_VALUE_NORMAL))) {
            return copy_text(out, "none");
        }
        if (native_value) {
            if (!value && property) {
                CssDeclaration* initial = css_parse_property_value_declaration(property->name,
                    strlen(property->name), property->initial_value, strlen(property->initial_value), scratch);
                value = initial ? initial->value : nullptr;
            }
            return native_value->consume(element, value, pseudo_type, native_value->result);
        }
        return value ? format_css_value(element, id, value, out, scratch, pseudo_type)
                     : !preserve_color_model && format_css_value(element, id, nullptr, out, scratch, pseudo_type);
    }
    return false;
}

static bool consume_cascaded_value(DomElement* element, CssPropertyCode id,
                                 int pseudo_type, StringBuf* out,
                                 bool preserve_color_model, const CssomValueTarget* native_value) {
    Pool* scratch = pool_create();
    if (!scratch) return false;
    bool result = consume_inherited_decl(element, id, pseudo_type, out, scratch, preserve_color_model, native_value);
    pool_destroy(scratch);
    return result;
}

bool css_prop_serialize_cascaded_value(DomElement* element, CssPropertyCode id,
        int pseudo_type, StringBuf* out, bool preserve_color_model) {
    return consume_cascaded_value(element, id, pseudo_type, out, preserve_color_model, nullptr);
}

static bool cssom_consume_flex_factor(DomElement* element, const CssValue* value,
        int pseudo_type, void* result) {
    return cssom_compute_scalar_value(element, value, pseudo_type,
        css_compute_nonnegative_number_result, (float*)result);
}

static bool cssom_consume_flex_basis(DomElement* element, const CssValue* value,
        int pseudo_type, void* result) {
    CssMathResult math = {};
    if (value && value->type != CSS_VALUE_TYPE_KEYWORD &&
        !cssom_compute_numeric_value(element, value, &math, pseudo_type, true)) return false;
    return css_compute_flex_basis_result(value, &math, (CssFlexBasisValue*)result);
}

bool css_prop_compute_declared_flex_factor(DomElement* element, CssPropertyCode id, float* result) {
    if (!element || !result || (id != CSS_PROPERTY_FLEX_GROW && id != CSS_PROPERTY_FLEX_SHRINK)) return false;
    CssomValueTarget target = {cssom_consume_flex_factor, result};
    return consume_cascaded_value(element, id, 0, nullptr, false, &target);
}

bool css_prop_compute_declared_flex_basis(DomElement* element, CssFlexBasisValue* result) {
    if (!element || !result) return false;
    CssomValueTarget target = {cssom_consume_flex_basis, result};
    return consume_cascaded_value(element, CSS_PROPERTY_FLEX_BASIS, 0, nullptr, false, &target);
}

static bool serialize_decl(const CssPropAccessor* accessor, DomElement* element,
                           int pseudo_type, StringBuf* out) {
    return accessor && css_prop_serialize_cascaded_value(element, accessor->id, pseudo_type, out);
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

static void cssom_apply_writing_mode(const CssValue* writing, CssomFontState* state) {
    if (!writing || cssom_value_inherits(writing, CSS_PROPERTY_WRITING_MODE)) return;
    CssEnum mode = writing->type == CSS_VALUE_TYPE_KEYWORD
        ? writing->data.keyword : CSS_VALUE_HORIZONTAL_TB;
    // sideways boxes retain vertical viewport axes while their glyph metrics stay horizontal.
    state->sideways = mode == CSS_VALUE_SIDEWAYS_RL || mode == CSS_VALUE_SIDEWAYS_LR;
    state->vertical = layout_writing_mode_from_css(mode) != WM_HORIZONTAL_TB;
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
    cssom_apply_writing_mode(writing, state);
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

static bool cssom_compute_numeric_value(DomElement* element, const CssValue* value,
        CssMathResult* result, int pseudo_type, bool preserve_percentages) {
    if (!element || !value || !result) return false;
    Pool* scratch = mem_pool_create(nullptr, MEM_ROLE_TEMP, "css.computed_length.scratch");
    if (!scratch) return false;
    CssomFontState root = {}, state = {};
    bool valid = cssom_resolve_font_state(scratch, element, pseudo_type, &root, &state, true);
    CssomFontMathContext lengths = {element, state.font.size_px, root.font.size_px,
        &state, &state, &root, state.vertical};
    value = css_resolve_element_var_value(scratch, element, value, CSS_PROPERTY_WIDTH);
    CssMathEvaluationContext math = {cssom_font_math_leaf, &lengths, 1.0, preserve_percentages};
    *result = css_math_evaluate(value, &math);
    valid = valid && result->resolved;
    mem_pool_destroy(scratch);
    return valid;
}

bool css_prop_compute_numeric_value(DomElement* element, const CssValue* value, CssMathResult* result) {
    return cssom_compute_numeric_value(element, value, result, 0);
}

static bool cssom_compute_integer_value(DomElement* element, const CssValue* value,
        int pseudo_type, double minimum, double maximum, double* result) {
    CssMathResult math;
    return cssom_compute_numeric_value(element, value, &math, pseudo_type) &&
        css_compute_integer_result(value, &math, minimum, maximum, result);
}

static bool cssom_compute_scalar_value(DomElement* element, const CssValue* value,
        int pseudo_type, CssomScalarPolicy compute, float* result) {
    CssMathResult math;
    return cssom_compute_numeric_value(element, value, &math, pseudo_type, true) &&
        compute(&math, result);
}

static bool format_cssom_scalar_value(DomElement* element, const CssValue* value,
        StringBuf* output, int pseudo_type, CssomScalarPolicy compute) {
    float number = 0.0f;
    // dirty and pseudo reads use declaration font state, with the same range policy as layout.
    return cssom_compute_scalar_value(element, value, pseudo_type, compute, &number) &&
        format_number(output, number, "");
}

static bool format_overflow_clip_margin_computed_value(DomElement* element,
        const CssValue* value, StringBuf* output, int pseudo_type) {
    CssEnum box;
    const CssValue* component;
    if (!css_overflow_clip_margin_components(value, &box, &component)) return copy_text(output, "0px");
    double length = 0.0;
    if (component) {
        CssMathResult math;
        if (!cssom_compute_numeric_value(element, component, &math, pseudo_type)) return false;
        if (math.type != CSS_MATH_LENGTH && !(math.type == CSS_MATH_NUMBER && math.value == 0.0))
            return copy_text(output, "0px");
        length = isnan(math.value) || math.value == 0.0 ? 0.0 : math.value;
        if (!isfinite(length) || fabs(length) > FLT_MAX) return copy_text(output, "0px");
    }
    // omit default components while preserving the declaration owner's computed length.
    if (box == CSS_VALUE_PADDING_BOX) return format_number(output, length, "px");
    const char* name = css_enum_info(box)->name;
    return length == 0.0 ? copy_text(output, name) : format_text(output, "%s %.6gpx", name, length);
}

static bool format_clamp_computed_value(DomElement* element, const CssValue* value,
        StringBuf* output, int pseudo_type) {
    if (!value || value->type == CSS_VALUE_TYPE_KEYWORD) return copy_text(output, "none");
    double count;
    bool computed = cssom_compute_integer_value(element, value, pseudo_type, 1.0, (double)INT_MAX, &count);
    return computed ? stringbuf_append_format(output, "%.0f", count) : copy_text(output, "none");
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

static bool cssom_append_math_result(CssFormatter* formatter, const CssValue* value,
        const CssMathResult& math, bool nonnegative) {
    double scalar = isnan(math.value) ? 0.0 : math.value;
    if (nonnegative) scalar = fmax(0.0, scalar);
    if (math.resolved && (math.type == CSS_MATH_NUMBER || math.type == CSS_MATH_LENGTH))
        return stringbuf_append_format(formatter->output, "%.6gpx", scalar);
    if (math.resolved && math.type == CSS_MATH_PERCENT)
        return stringbuf_append_format(formatter->output, "%.6g%%",
            nonnegative ? fmax(0.0, math.percentage) : math.percentage);
    if (math.resolved && math.type == CSS_MATH_LENGTH_PERCENT)
        return stringbuf_append_format(formatter->output, "calc(%.6g%% %c %.6gpx)",
            math.percentage, math.value < 0.0 ? '-' : '+', fabs(math.value));
    css_format_value(formatter, (CssValue*)value);
    return true;
}

static bool cssom_append_computed_math(CssFormatter* formatter, const CssValue* value,
        const CssMathEvaluationContext* context, bool nonnegative) {
    return cssom_append_math_result(formatter, value, css_math_evaluate(value, context), nonnegative);
}

static bool format_cssom_length_value(DomElement* element, const CssValue* value,
        StringBuf* output, int pseudo_type) {
    CssMathResult math;
    if (!cssom_compute_numeric_value(element, value, &math, pseudo_type, true)) return false;
    CssFormatter* formatter = css_formatter_create(output->pool, CSS_FORMAT_COMPACT);
    return formatter && cssom_append_math_result(formatter, value, math, true) &&
        copy_text(output, formatter->output->str->chars);
}

static CssValue* cssom_clone_computed_length(CssFormatter* formatter, const CssValue* input,
        const SvgLengthContext* svg, CssMathLeafResolver leaf, void* lengths) {
    CssValue* value = css_value_clone_owned(input, formatter->pool);
    SvgLengthContext unused_svg = {};
    return value && svg_normalize_length_value(value, svg ? svg : &unused_svg, SVG_LENGTH_DIAGONAL,
        true, leaf, lengths) ? value : nullptr;
}

struct CssomBackgroundWriter {
    CssBackgroundWinner winner;
    DomElement* owner;
    int pseudo_type;
    CssomFontState root, font;
    CssomFontMathContext lengths;
};

static CssomBackgroundWriter cssom_background_source(Pool* scratch, DomElement* element,
        CssPropertyCode property, int pseudo_type) {
    CssomBackgroundWriter result = {};
    while (element) {
        result.winner = background_component_winner(element, scratch, property, pseudo_type);
        result.owner = element; result.pseudo_type = pseudo_type;
        if (!css_value_keyword_equals(result.winner.value, CSS_VALUE_INHERIT)) break;
        element = cssom_inherited_source(element, &pseudo_type);
    }
    return result;
}

static bool cssom_background_lengths(Pool* scratch, CssomBackgroundWriter* writer) {
    if (!cssom_resolve_font_state(scratch, writer->owner, writer->pseudo_type, &writer->root, &writer->font, true)) return false;
    writer->lengths = {writer->owner, writer->font.font.size_px, writer->root.font.size_px,
        &writer->font, &writer->font, &writer->root, writer->font.vertical};
    return true;
}

static const CssValue* cssom_background_item(CssFormatter* formatter, CssomBackgroundWriter& writer,
        CssPropertyCode property, int index) {
    const CssValue* value = background_winner_component(formatter->pool, writer.winner, property, index);
    return css_value_is_global_keyword(value) ? nullptr : value;
}

static bool cssom_append_background_size(CssFormatter* formatter, CssomBackgroundWriter& writer, int index) {
    const CssValue* value = cssom_background_item(formatter, writer, CSS_PROPERTY_BACKGROUND_SIZE, index);
    const CssValue* axes[2] = {}; CssEnum mode;
    if (!value) { stringbuf_append_str(formatter->output, property_initial(CSS_PROPERTY_BACKGROUND_SIZE)); return true; }
    if (!css_background_size_components(value, axes, &mode)) return false;
    if (mode) { stringbuf_append_str(formatter->output, css_enum_info(mode)->name); return true; }
    bool automatic[2] = {!axes[0] || css_value_keyword_equals(axes[0], CSS_VALUE_AUTO),
        !axes[1] || css_value_keyword_equals(axes[1], CSS_VALUE_AUTO)};
    CssMathEvaluationContext math = {cssom_font_math_leaf, &writer.lengths, 1.0, true};
    for (size_t axis = 0; axis < (automatic[0] && automatic[1] ? 1u : 2u); axis++) {
        if (axis) stringbuf_append_str(formatter->output, " ");
        if (automatic[axis]) stringbuf_append_str(formatter->output, "auto");
        else {
            CssValue* computed = cssom_clone_computed_length(formatter, axes[axis], nullptr, cssom_font_math_leaf, &writer.lengths);
            if (!computed || !cssom_append_computed_math(formatter, computed, &math, true)) return false;
        }
    }
    return true;
}

static bool cssom_append_background_axis(CssFormatter* formatter, CssomBackgroundWriter& writer,
        int index, bool horizontal) {
    CssPropertyCode property = horizontal ? CSS_PROPERTY_BACKGROUND_POSITION_X : CSS_PROPERTY_BACKGROUND_POSITION_Y;
    const CssValue* value = cssom_background_item(formatter, writer, property, index);
    CssPositionAxis axes[2] = {{CSS_VALUE_LEFT, nullptr}, {CSS_VALUE_TOP, nullptr}};
    CssDeclaration* declaration = writer.winner.declaration;
    bool shorthand = declaration && (declaration->property_code == CSS_PROPERTY_BACKGROUND ||
        declaration->property_code == CSS_PROPERTY_BACKGROUND_POSITION);
    bool quirky = declaration && declaration->property_code == CSS_PROPERTY_BACKGROUND_POSITION &&
        dom_element_uses_quirks_css(writer.owner);
    if (value && !css_position_components(value, axes, true, shorthand ? -1 : horizontal ? 0 : 1, quirky)) return false;
    const CssPositionAxis& axis = axes[horizontal ? 0 : 1];
    double origin = background_position_keyword(axis.origin, horizontal);
    if (!axis.offset) return stringbuf_append_format(formatter->output, "%.6g%%", origin);
    CssValue* offset = cssom_clone_computed_length(formatter, axis.offset, nullptr, cssom_font_math_leaf, &writer.lengths);
    if (!offset) return false;
    CssMathEvaluationContext context = {cssom_font_math_leaf, &writer.lengths, 1.0, true};
    CssMathResult math = css_math_evaluate(offset, &context);
    double sign = axis.origin == CSS_VALUE_RIGHT || axis.origin == CSS_VALUE_BOTTOM ? -1.0 : 1.0;
    if (math.resolved) {
        double percent = origin + sign * math.percentage, length = sign * math.value;
        if (math.type == CSS_MATH_PERCENT || (math.type == CSS_MATH_LENGTH_PERCENT && length == 0.0))
            return stringbuf_append_format(formatter->output, "%.6g%%", percent);
        if (percent == 0.0 && math.type != CSS_MATH_LENGTH_PERCENT)
            return stringbuf_append_format(formatter->output, "%.6gpx", length);
        if (length == 0.0) return stringbuf_append_format(formatter->output, "%.6g%%", percent);
        return stringbuf_append_format(formatter->output, "calc(%.6g%% %c %.6gpx)", percent, length < 0.0 ? '-' : '+', fabs(length));
    }
    if (origin == 0.0 && sign > 0.0) { css_format_value(formatter, offset); return true; }
    stringbuf_append_format(formatter->output, "calc(%.6g%% %c ", origin, sign < 0.0 ? '-' : '+');
    css_format_value(formatter, offset);
    stringbuf_append_str(formatter->output, ")");
    return true;
}

static bool cssom_append_background_keyword(CssFormatter* formatter, CssomBackgroundWriter& writer,
        CssPropertyCode property, int index) {
    const CssValue* value = cssom_background_item(formatter, writer, property, index);
    if (property == CSS_PROPERTY_BACKGROUND_REPEAT) {
        CssEnum axes[2] = {CSS_VALUE_REPEAT, CSS_VALUE_REPEAT};
        if (value && !css_background_repeat_components(value, axes)) return false;
        if (axes[0] == CSS_VALUE_REPEAT && axes[1] == CSS_VALUE_NO_REPEAT) { stringbuf_append_str(formatter->output, "repeat-x"); return true; }
        if (axes[1] == CSS_VALUE_REPEAT && axes[0] == CSS_VALUE_NO_REPEAT) { stringbuf_append_str(formatter->output, "repeat-y"); return true; }
        stringbuf_append_str(formatter->output, css_enum_info(axes[0])->name);
        if (axes[0] != axes[1]) stringbuf_append_all(formatter->output, 2, " ", css_enum_info(axes[1])->name);
        return true;
    }
    if (value) { css_format_value(formatter, (CssValue*)value); return true; }
    stringbuf_append_str(formatter->output, property_initial(property));
    return true;
}

static bool serialize_background_components(const CssPropAccessor* accessor, DomElement* element,
        int pseudo_type, StringBuf* output) {
    Pool* scratch = output->pool;
    CssFormatter* formatter = css_formatter_create(scratch, CSS_FORMAT_COMPACT);
    if (!formatter) return false;
    CssomBackgroundWriter images = cssom_background_source(scratch, element, CSS_PROPERTY_BACKGROUND_IMAGE, pseudo_type);
    int count = background_list_count(images.winner.value);
    bool position = accessor->id == CSS_PROPERTY_BACKGROUND_POSITION;
    bool axis = position || accessor->id == CSS_PROPERTY_BACKGROUND_POSITION_X || accessor->id == CSS_PROPERTY_BACKGROUND_POSITION_Y;
    CssomBackgroundWriter writers[2] = {};
    for (size_t component = 0; component < (position ? 2u : 1u); component++) {
        CssPropertyCode property = position ? component == 0 ? CSS_PROPERTY_BACKGROUND_POSITION_X : CSS_PROPERTY_BACKGROUND_POSITION_Y : accessor->id;
        writers[component] = cssom_background_source(scratch, element, property, pseudo_type);
        if ((axis || accessor->id == CSS_PROPERTY_BACKGROUND_SIZE) && !cssom_background_lengths(scratch, &writers[component])) return false;
    }
    // project original independent periods only at the receiving element; inheritance keeps its declaration owner's units (CI-18).
    for (int index = 0; index < count; index++) {
        if (index) stringbuf_append_str(formatter->output, ", ");
        bool valid = axis ? cssom_append_background_axis(formatter, writers[0], index,
            accessor->id != CSS_PROPERTY_BACKGROUND_POSITION_Y)
            : accessor->id == CSS_PROPERTY_BACKGROUND_SIZE ? cssom_append_background_size(formatter, writers[0], index)
            : cssom_append_background_keyword(formatter, writers[0], accessor->id, index);
        if (!valid) return false;
        if (position) {
            stringbuf_append_str(formatter->output, " ");
            if (!cssom_append_background_axis(formatter, writers[1], index, false)) return false;
        }
    }
    String* value = stringbuf_to_string(formatter->output);
    return value && copy_text(output, value->chars);
}


static bool cssom_append_svg_length(const CssValue* input, void* data) {
    CssomSvgScalarWriter* writer = (CssomSvgScalarWriter*)data;
    // normalization writes only to the caller-owned projection, never retained declaration values (D4.5.1v4).
    CssValue* value = cssom_clone_computed_length(writer->formatter, input, &writer->lengths.svg,
        cssom_svg_length_leaf, &writer->lengths);
    if (!value) return false;
    CssMathEvaluationContext context = {cssom_svg_length_leaf, &writer->lengths, 1.0, true};
    if (writer->comma && writer->written) stringbuf_append_str(writer->formatter->output, ", ");
    writer->written = true;
    return cssom_append_computed_math(writer->formatter, value, &context, writer->nonnegative);
}

static bool cssom_append_grid_leaf(CssFormatter* formatter, CssValue* value,
        bool repeat_count, void* context) {
    CssomFontMathContext* lengths = (CssomFontMathContext*)context;
    bool function = value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function;
    if (repeat_count && function) {
        CssMathEvaluationContext math = {cssom_font_math_leaf, lengths, 1.0, false};
        double count;
        if (!css_compute_integer_value(value, &math, 1.0, (double)INT_MAX, &count)) return false;
        return stringbuf_append_format(formatter->output, "%.0f", count);
    }
    bool length = value->type == CSS_VALUE_TYPE_LENGTH && value->data.length.unit != CSS_UNIT_FR;
    bool zero = !repeat_count && value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value == 0.0;
    if (length || zero || (function && !repeat_count)) {
        // shared math normalization preserves percentages while computing lengths at their declaration owner.
        value = cssom_clone_computed_length(formatter, value, nullptr, cssom_font_math_leaf, lengths);
        if (!value) return false;
        CssMathEvaluationContext math = {cssom_font_math_leaf, lengths, 1.0, true};
        return cssom_append_computed_math(formatter, value, &math, false);
    }
    css_format_value(formatter, value);
    return true;
}

static bool format_grid_computed_value(DomElement* element, const CssValue* value, StringBuf* output,
    CssPropertyCode property, int pseudo_type) {
    Pool* scratch = pool_create();
    if (!scratch) return false;
    CssomFontState root = {}, state = {};
    CssFormatter* formatter = css_formatter_create(scratch, CSS_FORMAT_COMPACT);
    CssValue* copy = css_value_clone_owned(value, scratch);
    // generated tracks compute relative lengths in their own font before inheritance changes the owner.
    bool valid = formatter && copy && cssom_resolve_font_state(scratch, element, pseudo_type, &root, &state, true);
    if (valid) {
        CssomFontMathContext lengths = {element, state.font.size_px, root.font.size_px,
            &state, &state, &root, state.vertical};
        String* text = nullptr;
        if (css_grid_placement_mask(property)) {
            CssMathEvaluationContext math = {cssom_font_math_leaf,&lengths,1.0,false};
            CssGridLineReference lines[4] = {};
            if (!css_grid_parse_placement(copy,property,lines,&math)) memset(lines,0,sizeof(lines));
            text = css_grid_serialize_placement(scratch,lines,property);
        } else text = css_format_grid_track_value(formatter, copy, cssom_append_grid_leaf, &lengths)
            ? stringbuf_to_string(formatter->output) : nullptr;
        valid = text && copy_text(output, text->chars);
    }
    pool_destroy(scratch);
    return valid;
}

static const char* cssom_grid_used_track_text(DomElement* element,
        CssPropertyCode property, int pseudo_type) {
    ViewTree* tree = element && element->doc ? element->doc->view_tree.get() : nullptr;
    const ViewNodeState* state = tree && pseudo_type == 0 &&
        element->display.outer != CSS_VALUE_NONE && element->display.inner == CSS_VALUE_GRID
        ? view_tree_node_state(tree, element, false) : nullptr;
    const char* used = state ? property == CSS_PROPERTY_GRID_TEMPLATE_COLUMNS
        ? state->grid_used_columns.get() : state->grid_used_rows.get() : nullptr;
    if (!used && tree && !tree->model && pseudo_type == 0 &&
        element->display.outer != CSS_VALUE_NONE && element->display.inner == CSS_VALUE_GRID &&
        element->embed && element->embedp()->grid && element->embedp()->grid->used_track_text) {
        const GridProp* grid = element->embedp()->grid;
        used = grid->used_track_text.get() + (property == CSS_PROPERTY_GRID_TEMPLATE_COLUMNS
            ? grid->used_columns_offset : 0);
    }
    return used;
}

static bool serialize_grid_tracks(const CssPropAccessor* accessor, DomElement* element,
        int pseudo_type, StringBuf* output) {
    const char* used = cssom_grid_used_track_text(element, accessor->id, pseudo_type);
    return used ? copy_text(output, used) : serialize_decl(accessor, element, pseudo_type, output);
}

static bool serialize_grid_shorthand(const CssPropAccessor* accessor, DomElement* element,
        int pseudo_type, StringBuf* output) {
    unsigned mask = css_grid_property_mask(accessor->id);
    const CssValue* values[CSS_GRID_PROPERTY_MEMBER_COUNT] = {};
    String* texts[CSS_GRID_PROPERTY_MEMBER_COUNT] = {};
    for (unsigned i = 0; i < CSS_GRID_PROPERTY_MEMBER_COUNT; i++) if (mask & (1u << i)) {
        CssPropertyCode member = css_grid_property_member(i);
        String* value = nullptr;
        if (accessor->id == CSS_PROPERTY_GRID_TEMPLATE && (i == 4 || i == 5)) {
            StringBuf* computed = stringbuf_new(output->pool);
            if (!computed || !css_prop_serialize_cascaded_value(element, member, pseudo_type, computed)) return false;
            value = stringbuf_to_string(computed);
            // grid-template retains computed track functions; an entirely implicit axis uses its resolved list.
            if (value && strcmp(value->chars, "none") == 0)
                value = css_prop_serialize_computed_value(output->pool, element, member, pseudo_type);
        } else value = css_prop_serialize_computed_value(output->pool, element, member, pseudo_type);
        if (!value) return false;
        texts[i] = value;
        values[i] = css_parse_component_value_text(value->chars, value->len, output->pool);
        if (!values[i]) return false;
        if (accessor->id == CSS_PROPERTY_GRID_TEMPLATE && (i == 4 || i == 5) &&
            cssom_grid_used_track_text(element, member, pseudo_type)) {
            values[i] = css_grid_project_resolved_template_tracks(output->pool, values[i]);
            if (!values[i]) return false;
        }
    }
    if (accessor->id == CSS_PROPERTY_GRID) {
        // resolved grid preserves the browser's legacy slash-separated longhand order, including auto defaults.
        const unsigned order[] = {4, 5, 6, 9, 7, 8};
        for (unsigned i = 0; i < sizeof(order) / sizeof(*order); i++) {
            if (!stringbuf_append_format(output, "%s%s", i ? " / " : "", texts[order[i]]->chars)) return false;
        }
        return true;
    }
    String* text = css_grid_serialize_shorthand(output->pool, values, accessor->id);
    return text && copy_text(output, text->chars);
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
        if (!format_cssom_scalar_value(declaring, value, formatter->output, 0, css_compute_opacity_result)) return nullptr;
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
        return css_prop_serialize_cascaded_value(element, shorthand_id, 0, out);
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

static bool serialize_cursor(const CssPropAccessor*, DomElement* element, int pseudo_type,
                             StringBuf* out) {
    if (!element || pseudo_type != 0 || !out) return false;
    const InlineProp* style = element->inl();
    stringbuf_reset(out);
    for (const CursorImageSpec* image = style->cursor_images; image; image = image->next) {
        stringbuf_append_str(out, "url(\"");
        for (const unsigned char* p = (const unsigned char*)image->url; *p; p++) {
            if (*p == '"' || *p == '\\') stringbuf_append_char(out, '\\');
            if (*p < 0x20 || *p == 0x7f)
                stringbuf_append_format(out, "\\%x ", (unsigned)*p);
            else stringbuf_append_char(out, (char)*p);
        }
        stringbuf_append_str(out, "\")");
        if (image->has_hotspot)
            stringbuf_append_format(out, " %.6g %.6g", image->hotspot_x, image->hotspot_y);
        stringbuf_append_str(out, ", ");
    }
    const CssEnumInfo* fallback = css_enum_info(style->cursor);
    stringbuf_append_str(out, fallback && fallback->name ? fallback->name : "auto");
    return true;
}

static bool serialize_display(const CssPropAccessor*, DomElement* element, int pseudo_type,
                              StringBuf* out) {
    if (!element || pseudo_type != 0) return false;
    DisplayValue display = resolve_display_value(element);
    const char* special = css_display_special_name(display);
    if (special) return copy_text(out, special);
    if (display.outer == CSS_VALUE_NONE) return copy_text(out, "none");
    bool inline_outer = display.outer == CSS_VALUE_INLINE || display.outer == CSS_VALUE_INLINE_BLOCK;
    if (display.inner == CSS_VALUE_FLEX) {
        return copy_text(out,
            inline_outer ? "inline-flex" : "flex");
    }
    if (display.inner == CSS_VALUE_GRID) {
        return copy_text(out,
            inline_outer ? "inline-grid" : "grid");
    }
    if (display.inner == CSS_VALUE_TABLE) {
        return copy_text(out,
            inline_outer ? "inline-table" : "table");
    }
    const CssEnumInfo* info = css_enum_info(display.outer);
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
    // normal-flow collapse consumes vertical margins after their used lengths
    // are resolved; CSSOM reflects the owned value captured before that step.
    const Margin& flow_margin = *view_boundary_flow_margin(boundary);
    float value = 0.0f;
    CssEnum type = CSS_VALUE__UNDEF;
    switch (accessor->id) {
        case CSS_PROPERTY_MARGIN_TOP: value = flow_margin.top; type = flow_margin.top_type; break;
        case CSS_PROPERTY_MARGIN_RIGHT: value = boundary->margin.right; type = boundary->margin.right_type; break;
        case CSS_PROPERTY_MARGIN_BOTTOM: value = flow_margin.bottom; type = flow_margin.bottom_type; break;
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
    return css_prop_serialize_cascaded_value(element, CSS_PROPERTY_BACKGROUND_COLOR, pseudo_type, out);
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

static bool serialize_scroll_axes(const CssPropAccessor* accessor, DomElement* element,
        int pseudo_type, StringBuf* out) {
    if (!accessor || !element) return false;
    bool overscroll = accessor->id == CSS_PROPERTY_OVERSCROLL_BEHAVIOR ||
        accessor->id == CSS_PROPERTY_OVERSCROLL_BEHAVIOR_X ||
        accessor->id == CSS_PROPERTY_OVERSCROLL_BEHAVIOR_Y;
    ArrayList* ancestors = cssom_collect_style_ancestors(element);
    if (!ancestors) return false;
    CssEnum x = overscroll ? CSS_VALUE_AUTO : CSS_VALUE_VISIBLE, y = x;
    CssomFontState axes = {};
    // compute ancestors before their child so dirty reads inherit current axes without layout snapshots.
    for (int index = ancestors->length - 1; index >= (pseudo_type ? -1 : 0); index--) {
        DomElement* owner = index < 0 ? element : (DomElement*)ancestors->data[index];
        int pseudo = index < 0 ? pseudo_type : 0;
        cssom_apply_writing_mode(cssom_declared_font_value(out->pool, owner,
            CSS_PROPERTY_WRITING_MODE, pseudo), &axes);
        StyleTree* style = pseudo ? dom_element_get_pseudo_style(owner, pseudo)
            : owner->specified_style.get();
        LayoutContext context = cssom_value_context(owner, out->pool);
        CssEnum inherited_x = x, inherited_y = y;
        x = y = overscroll ? CSS_VALUE_AUTO : CSS_VALUE_VISIBLE;
        if (!overscroll && !pseudo) {
            x = owner->scroll()->default_overflow_x;
            y = owner->scroll()->default_overflow_y;
        }
        layout_compute_scroll_keyword_axes(&context, style, axes.vertical, overscroll,
            inherited_x, inherited_y, &x, &y);
    }
    arraylist_free(ancestors);
    CssEnum value = x;
    if (accessor->id == CSS_PROPERTY_OVERFLOW_Y || accessor->id == CSS_PROPERTY_OVERSCROLL_BEHAVIOR_Y ||
        (accessor->id == CSS_PROPERTY_OVERFLOW_INLINE && axes.vertical) ||
        (accessor->id == CSS_PROPERTY_OVERFLOW_BLOCK && !axes.vertical)) value = y;
    const CssEnumInfo* first = css_enum_info(value);
    if (!first || !first->name) return false;
    bool shorthand = accessor->id == CSS_PROPERTY_OVERFLOW || accessor->id == CSS_PROPERTY_OVERSCROLL_BEHAVIOR;
    if (!shorthand || x == y) return copy_text(out, first->name);
    const CssEnumInfo* second = css_enum_info(y);
    return second && second->name && format_text(out, "%s %s", first->name, second->name);
}

static bool serialize_line_clamp(const CssPropAccessor* accessor, DomElement* element,
                                  int pseudo_type, StringBuf* out) {
    // used clamp propagation is not CSS inheritance; declarations also remain observable before layout.
    return accessor && element && css_prop_serialize_cascaded_value(element, accessor->id, pseudo_type, out);
}

static bool serialize_contain_intrinsic_axis(DomElement* element, CssPropertyCode property,
        size_t axis, int pseudo_type, StringBuf* out) {
    while (element) {
        CssomFontState root = {}, font = {};
        if (!cssom_resolve_font_state(out->pool, element, pseudo_type, &root, &font, true)) return false;
        uint8_t axes = css_intrinsic_size_axes(property, font.vertical);
        if (axes != 3) axis = axes == 1 ? 0 : 1;
        CssPropertyCode physical = axis == 0 ? CSS_PROPERTY_CONTAIN_INTRINSIC_WIDTH
            : CSS_PROPERTY_CONTAIN_INTRINSIC_HEIGHT;
        StyleTree* style = pseudo_type ? dom_element_get_pseudo_style(element, pseudo_type)
            : element->specified_style.get();
        CssDeclaration* declaration = layout_cascaded_style_declaration(style, physical,
            layout_logical_sides(font.vertical, false));
        CssPropertyCode source = declaration && declaration->property_code != CSS_PROPERTY_ALL
            ? declaration->property_code : physical;
        const CssValue* value = declaration ? css_resolve_element_var_value(out->pool,
            element, declaration->value, source) : nullptr;
        if (value && !css_property_validate_value(source, value)) value = nullptr;
        if (css_value_keyword_equals(value, CSS_VALUE_INHERIT)) {
            // logical inheritance follows the parent's axis; lengths retain their declaration owner.
            property = source == CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE ? physical : source;
            element = cssom_inherited_source(element, &pseudo_type);
            continue;
        }
        if (!value || css_value_is_global_keyword(value)) return copy_text(out, "none");
        CssIntrinsicSizeAxis components[2] = {};
        if (!css_intrinsic_size_components(value, source == CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE,
                components)) return false;
        const CssIntrinsicSizeAxis& component = components[source == CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE ? axis : 0];
        const char* mode = component.automatic ? "auto " : "";
        if (css_value_keyword_equals(component.size, CSS_VALUE_NONE)) return format_text(out, "%snone", mode);
        CssomFontMathContext lengths = {element, font.font.size_px, root.font.size_px,
            &font, &font, &root, font.vertical};
        CssMathEvaluationContext context = {cssom_font_math_leaf, &lengths, 1.0, false};
        CssMathResult math = css_math_evaluate(component.size, &context);
        if (!math.resolved || (math.type != CSS_MATH_LENGTH &&
            !(math.type == CSS_MATH_NUMBER && math.value == 0.0))) return copy_text(out, "none");
        return format_text(out, "%s%.6gpx", mode, layout_clamp_dimension(isnan(math.value)
            ? 0.0f : (float)fmax(0.0, math.value)));
    }
    return copy_text(out, "none");
}

static bool serialize_contain_intrinsic(const CssPropAccessor* accessor, DomElement* element,
        int pseudo_type, StringBuf* out) {
    if (!accessor || !element) return false;
    if (accessor->id != CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE)
        return serialize_contain_intrinsic_axis(element, accessor->id, 0, pseudo_type, out);
    StringBuf* height = stringbuf_new(out->pool);
    if (!height || !serialize_contain_intrinsic_axis(element, accessor->id, 0, pseudo_type, out) ||
        !serialize_contain_intrinsic_axis(element, accessor->id, 1, pseudo_type, height)) return false;
    // shorthand compression compares complete computed axes, preserving independent auto modes.
    if (strcmp(out->str->chars, height->str->chars) == 0) return true;
    return stringbuf_append_format(out, " %s", height->str->chars);
}

#define DIRECT_ROW_DERIVE(prop_id, group, type, field, kind, row_flags, derived) \
    {prop_id, group, (uint16_t)offsetof(type, field), kind, row_flags, serialize_direct, derived}
#define DIRECT_ROW(prop_id, group, type, field, kind, row_flags) \
    DIRECT_ROW_DERIVE(prop_id, group, type, field, kind, row_flags, nullptr)
#define DERIVED_ROW(prop_id, fn, row_flags) \
    {prop_id, PROP_GROUP_NONE, 0, CSS_PROP_VALUE_SPECIAL, row_flags, fn, fn}
#define DECL_ROW(prop_id) DERIVED_ROW(prop_id, serialize_decl, 0)

static const CssPropAccessor CSS_PROP_ROWS[] = {
    DERIVED_ROW(CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE, serialize_contain_intrinsic, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_CONTAIN_INTRINSIC_WIDTH, serialize_contain_intrinsic, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_CONTAIN_INTRINSIC_HEIGHT, serialize_contain_intrinsic, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_CONTAIN_INTRINSIC_INLINE_SIZE, serialize_contain_intrinsic, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_CONTAIN_INTRINSIC_BLOCK_SIZE, serialize_contain_intrinsic, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DECL_ROW(CSS_PROPERTY_OVERFLOW_CLIP_MARGIN),
    DERIVED_ROW(CSS_PROPERTY_GRID_TEMPLATE_COLUMNS, serialize_grid_tracks, CSS_PROP_ACCESSOR_USED_VALUE | CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_GRID_TEMPLATE_ROWS, serialize_grid_tracks, CSS_PROP_ACCESSOR_USED_VALUE | CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DECL_ROW(CSS_PROPERTY_GRID_AUTO_COLUMNS),
    DECL_ROW(CSS_PROPERTY_GRID_AUTO_ROWS),
    // shorthand serializers already resolve pseudo members; declaration fallback loses their computed values.
    DERIVED_ROW(CSS_PROPERTY_GRID_TEMPLATE, serialize_grid_shorthand, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_GRID, serialize_grid_shorthand, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DECL_ROW(CSS_PROPERTY_GRID_TEMPLATE_AREAS),
    DECL_ROW(CSS_PROPERTY_GRID_AUTO_FLOW),
    DECL_ROW(CSS_PROPERTY_GRID_COLUMN_START),
    DECL_ROW(CSS_PROPERTY_GRID_COLUMN_END),
    DECL_ROW(CSS_PROPERTY_GRID_ROW_START),
    DECL_ROW(CSS_PROPERTY_GRID_ROW_END),
    DERIVED_ROW(CSS_PROPERTY_GRID_COLUMN, serialize_grid_shorthand, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_GRID_ROW, serialize_grid_shorthand, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_GRID_AREA, serialize_grid_shorthand, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
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
    DERIVED_ROW(CSS_PROPERTY_DISPLAY, serialize_display, CSS_PROP_ACCESSOR_CASCADE_RESOLVED),
    DIRECT_ROW(CSS_PROPERTY_POSITION, PROP_GROUP_POSITION, PositionProp, position, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_TOP, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_RIGHT, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BOTTOM, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_LEFT, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_Z_INDEX, serialize_decl,
        CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DIRECT_ROW(CSS_PROPERTY_FLOAT, PROP_GROUP_POSITION, PositionProp, float_prop, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_CLEAR, PROP_GROUP_POSITION, PositionProp, clear, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_OVERFLOW, serialize_scroll_axes, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_OVERFLOW_X, serialize_scroll_axes, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_OVERFLOW_Y, serialize_scroll_axes, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_OVERFLOW_INLINE, serialize_scroll_axes, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_OVERFLOW_BLOCK, serialize_scroll_axes, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DIRECT_ROW(CSS_PROPERTY_SCROLL_BEHAVIOR, PROP_GROUP_SCROLL, ScrollProp, scroll_behavior, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_OVERSCROLL_BEHAVIOR_X, serialize_scroll_axes, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_OVERSCROLL_BEHAVIOR_Y, serialize_scroll_axes, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_OVERSCROLL_BEHAVIOR, serialize_scroll_axes, CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
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
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DIRECT_ROW(CSS_PROPERTY_LETTER_SPACING, PROP_GROUP_FONT, FontProp, letter_spacing, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_WORD_SPACING, PROP_GROUP_FONT, FontProp, word_spacing, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_TEXT_ALIGN, PROP_GROUP_BLOCK, BlockProp, text_align, CSS_PROP_VALUE_ENUM, 0),
    // the layout slot is lazy and cannot represent a combined transform declaration.
    DECL_ROW(CSS_PROPERTY_TEXT_TRANSFORM),
    DIRECT_ROW(CSS_PROPERTY_TEXT_OVERFLOW, PROP_GROUP_BLOCK, BlockProp, text_overflow, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_LINE_CLAMP, serialize_line_clamp, 0),
    DERIVED_ROW(CSS_PROPERTY_WEBKIT_LINE_CLAMP, serialize_line_clamp, 0),
    DECL_ROW(CSS_PROPERTY_HYPHENATE_LIMIT_CHARS),
    DECL_ROW(CSS_PROPERTY_WEBKIT_BOX_ORIENT),
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
    DIRECT_ROW(CSS_PROPERTY_ISOLATION, PROP_GROUP_INLINE, InlineProp, isolation, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_CURSOR, serialize_cursor, 0),
    DERIVED_ROW(CSS_PROPERTY_IMAGE_RENDERING, serialize_decl,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DECL_ROW(CSS_PROPERTY_ALIGN_SELF),
    // CSS Align modifiers such as safe/unsafe are retained in the declaration tree.
    DECL_ROW(CSS_PROPERTY_JUSTIFY_SELF),
    DERIVED_ROW(CSS_PROPERTY_PLACE_SELF, serialize_place_self, 0),
    DIRECT_ROW_DERIVE(CSS_PROPERTY_FLEX_GROW, PROP_GROUP_FLEX_ITEM, FlexItemProp, flex_grow, CSS_PROP_VALUE_NUMBER, 0, serialize_decl),
    DIRECT_ROW_DERIVE(CSS_PROPERTY_FLEX_SHRINK, PROP_GROUP_FLEX_ITEM, FlexItemProp, flex_shrink, CSS_PROP_VALUE_NUMBER, 0, serialize_decl),
    DERIVED_ROW(CSS_PROPERTY_FLEX_BASIS, serialize_decl,
        CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_ORDER, serialize_decl,
        CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
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
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_POSITION, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_POSITION_X, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_POSITION_Y, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_SIZE, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_REPEAT, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_ATTACHMENT, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_ORIGIN, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_CLIP, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_BLEND_MODE, serialize_background_components,
                CSS_PROP_ACCESSOR_CASCADE_RESOLVED | CSS_PROP_ACCESSOR_PSEUDO_RESOLVED),
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
#undef DIRECT_ROW_DERIVE

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

static bool cssom_sample_skipped_motion(DomElement* element, Pool* scratch) {
    CssomFontState root = {}, state = {};
    if (!cssom_resolve_font_state(scratch, element, 0, &root, &state, true)) return false;
    LayoutContext context = cssom_value_context(element, scratch);
    DomDocument* document = element->doc;
    UiContext* host = (UiContext*)document->js.host_ui_context;
    context.ui_context = lam::up(host);
    CssEngine* engine = (CssEngine*)document->services.cached_css_engine;
    context.width = engine ? (float)engine->context.viewport_width : host ? (float)host->viewport_width : NAN;
    context.height = engine ? (float)engine->context.viewport_height : host ? (float)host->viewport_height : NAN;
    context.root_font_size = root.font.size_px;
    FontProp font = {};
    font.family = lam::up(const_cast<char*>(state.font.family));
    font.font_size = state.font.size_px;
    font.used_zoom = element->font ? element->font->used_zoom : 1.0f;
    font.font_weight_numeric = (int16_t)state.font.weight;
    font.font_style = state.font.slant == FONT_SLANT_ITALIC ? CSS_VALUE_ITALIC
        : state.font.slant == FONT_SLANT_OBLIQUE ? CSS_VALUE_OBLIQUE : CSS_VALUE_NORMAL;
    context.font.style = lam::up(&font);
    context.font.current_font_size = font_prop_used_size(&font);
    if (host && host->font_ctx) setup_font(host, &context.font, &font);
    ViewBlock* parent = lam::view_as_block(dom_flat_tree_parent(element));
    LayoutContainingBlock containing = parent ? layout_containing_block_for_view(parent) : LayoutContainingBlock{};
    LayoutContainingBlockScope containing_scope(&context, containing.content_width,
        containing.content_height, containing.has_definite_height, parent != nullptr);
    // endpoint computation borrows current declarations and committed geometry, never a layout trial.
    bool sampled = css_motion_sample_existing(element, &context);
    font_prop_release_handle(&font);
    return sampled;
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
    if (pseudo_type == 0 && metadata && metadata->animation_type != ANIM_VAL_NONE &&
        view_content_visibility_hides(element, CONTENT_VISIBILITY_PAINT) &&
        css_animation_needs_computed_sample(element, id)) {
        // explicit reads refresh owner values without advancing skipped effect events.
        computed = cssom_sample_skipped_motion(element, out->pool) || computed;
    }
    const CssProperty* property = css_property_get_by_code(id);
    if (property && property->type == PROP_TYPE_COLOR &&
        !css_animation_needs_computed_sample(element, id) &&
        css_prop_serialize_cascaded_value(element, id, pseudo_type, out, true)) return true;

    // dirty and committed pseudo reads must choose the same declaration fallback.
    if (pseudo_type != 0 && !(accessor->flags & CSS_PROP_ACCESSOR_PSEUDO_RESOLVED))
        return serialize_decl(accessor, element, pseudo_type, out);
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
    // optional item groups derive their computed value when no applied record exists.
    if (accessor->derive && accessor->group_kind != PROP_GROUP_NONE &&
        !prop_group_base(element, accessor->group_kind, false))
        return accessor->derive(accessor, element, pseudo_type, out);
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
