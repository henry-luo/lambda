#include "page_document.hpp"
#include "view_tree_css.hpp"
#include "layout.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/dom/dom.h"
#include "../lib/memtrack.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/str.h"
#include "../lib/sort.h"
#include "../lib/arraylist.h"
#include "../lib/url.h"
#include <string.h>
#include <math.h>
#include <float.h>
#include <utf8proc.h>

bool radiant_page_whitespace(DomText* text) {
    for (size_t i = 0; i < text->length; i++) {
        char ch = text->text[i];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') return false;
    }
    return true;
}

bool radiant_note_resolve(ViewTree* tree, ViewCssStyle* style) {
    if (!radiant_page_element(style->source, "note")) return true;
    RadiantNoteBinding* binding = (RadiantNoteBinding*)pool_calloc(tree->model->css->pool, sizeof(RadiantNoteBinding));
    if (!binding) return false;
    style->note_binding = lam::up(binding);
    auto fail = [&]() { binding->status = VIEW_MODEL_INVALID_ARGUMENT;
        binding->reason = "note requires one note-call followed by one note-body"; return true; };
    for (DomNode* child = style->source->first_child; child; child = child->next_sibling) {
        if (child->is_text()) { if (!radiant_page_whitespace(child->as_text())) return fail(); continue; }
        if (!child->is_element()) continue;
        if (!binding->call.address && radiant_page_element(child->as_element(), "note-call")) binding->call = dom_node_ref(child);
        else if (binding->call.address && !binding->body.address && radiant_page_element(child->as_element(), "note-body")) binding->body = dom_node_ref(child);
        else return fail();
    }
    return binding->call.address && binding->body.address ? true : fail();
}

template <typename Fn>
static bool page_native_attributes(DomElement* source, Fn fn) {
    int count = 0; const char** names = source->attribute_names(&count); bool matched = false;
    for (int i = 0; i < count; i++) {
        const char* local = nullptr;
        const char* uri = dom_element_attribute_namespace_uri(source, names[i], &local);
        if (uri && !strcmp(uri, RADIANT_PAGE_NAMESPACE)) matched |= fn(local, source->get_attribute(names[i]));
    }
    return matched;
}

static bool page_native_values(DomElement* source, const char* const* names, size_t count, const char** values) {
    return page_native_attributes(source, [&](const char* local, const char* value) {
        for (size_t i = 0; i < count; i++) if (!strcmp(local, names[i])) { values[i] = value; return true; }
        return false;
    });
}

DomElement* radiant_page_style_parent(DomElement* source) {
    DomElement* parent = source ? source->parent_element() : nullptr;
    // structural wrappers may own layout boxes without becoming a new computed inheritance context.
    // the document root remains the initial font and inheritance anchor.
    while (parent && parent->parent_element()) {
        const char* transparent = dom_element_attribute_ns(parent, RADIANT_PAGE_NAMESPACE, "style-transparent");
        if (!transparent || strcmp(transparent, "true")) break;
        parent = parent->parent_element();
    }
    return parent;
}

static const char* whitespace_names[] = {"linefeed-treatment", "white-space-treatment", "white-space-collapse", "wrap-option"};

static const char* image_trait_names[] = {"content-width", "content-height", "scaling", "allowed-width-scale", "allowed-height-scale"};

static const CssValue* page_computed_value(ViewTree* tree, ViewCssStyle* style, const char* text) {
    // native traits validate their own domains after untyped CSS parsing and binding substitution.
    const char* property = "--radiant-trait";
    CssDeclaration* declaration = css_parse_property_value_declaration(property, strlen(property), text, strlen(text), tree->model->css->pool);
    return declaration ? view_css_resolve_value(tree, style, declaration->value) : nullptr;
}

static bool page_resolved_dimension(ViewTree* tree, ViewCssStyle* style, const CssValue* value,
        RadiantLengthPercentage* result) {
    if (!value) return false;
    CssMathType type = css_math_value_type(value);
    result->percentage = type == CSS_MATH_PERCENT;
    if (result->percentage) {
        CssMathEvaluationContext context = {}; context.preserve_percentages = true;
        CssMathResult evaluated = css_math_evaluate(value, &context);
        if (!evaluated.resolved) return false;
        result->value = (float)evaluated.percentage;
    }
    else {
        if (type != CSS_MATH_LENGTH &&
            !(value->type == CSS_VALUE_TYPE_NUMBER && !value->data.number.value)) return false;
        result->value = view_css_length(tree, style, value, CSS_PROPERTY_MARGIN_TOP, 0.0f, 0.0f);
    }
    return isfinite(result->value);
}

static bool page_computed_dimension(ViewTree* tree, ViewCssStyle* style, const char* text,
        RadiantLengthPercentage* result) {
    return page_resolved_dimension(tree, style, page_computed_value(tree, style, text), result);
}

bool radiant_page_boolean(const char* text, bool inherited, bool* result) {
    if (!text || !result) return false;
    if (!strcmp(text, "inherit")) *result = inherited;
    else if (!strcmp(text, "true")) *result = true;
    else if (!strcmp(text, "false")) *result = false;
    else return false;
    return true;
}

static const char* label_body_names[] = {"provisional-distance-between-starts", "provisional-label-separation", "label-body-grid", "label-body-context"};

bool radiant_label_body_trait_name(const char* name) {
    for (const char* candidate : label_body_names) if (!strcmp(name, candidate)) return true;
    return false;
}

bool radiant_label_body_resolve(ViewTree* tree, ViewCssStyle* style) {
    const char* values[4] = {};
    bool any = style->source && page_native_values(style->source, label_body_names, 4, values);
    const RadiantLabelBodySpec* parent = style->parent ? style->parent->label_body.get() : nullptr;
    if (!any && !parent) return true;
    RadiantLabelBodySpec* result = (RadiantLabelBodySpec*)pool_calloc(tree->model->css->pool, sizeof(RadiantLabelBodySpec));
    if (!result) return false;
    style->label_body = lam::up(result);
    // XSL 1.1 §7.30.11–12 initializes the inherited components to 24pt and 6pt.
    result->distance = parent ? parent->distance : RadiantLengthPercentage{32.0f, false};
    result->separation = parent ? parent->separation : RadiantLengthPercentage{8.0f, false};
    auto fail = [&](const char* reason) { result->status = VIEW_MODEL_INVALID_ARGUMENT; result->reason = reason; return true; };
    if (parent && parent->status != VIEW_MODEL_OK) return fail(parent->reason);
    RadiantLengthPercentage* outputs[] = {&result->distance, &result->separation};
    for (size_t i = 0; i < 2; i++) if (const char* value = values[i]) {
        if (strcmp(value, "inherit") && !page_computed_dimension(tree, style, value, outputs[i]))
            return fail("label/body geometry requires a finite length or percentage");
    }
    bool* flags[] = {&result->grid, &result->context};
    for (size_t i = 0; i < 2; i++) if (const char* value = values[i + 2]) {
        if (!radiant_page_boolean(value, parent && (i ? parent->context : parent->grid), flags[i]))
            return fail("label/body flags require true or false");
    }
    if (result->grid && style->display.inner != CSS_VALUE_TABLE)
        return fail("label/body grid requires a table formatting context");
    return true;
}

const RadiantLabelBodySpec* radiant_label_body_geometry(const ViewCssStyle* style) {
    // a list's property functions bind to its original containing list context, not an item's overrides.
    for (const ViewCssStyle* parent = style->parent; parent; parent = parent->parent)
        if (parent->label_body && parent->label_body->context) return parent->label_body.get();
    return style->label_body.get();
}

bool radiant_label_body_size(const RadiantLabelBodySpec* spec, float width,
        float* label, float* separation, float* body) {
    if (!spec || spec->status != VIEW_MODEL_OK || !isfinite(width) || width <= 0.0f ||
        !label || !separation || !body) return false;
    auto used = [&](RadiantLengthPercentage value) { return value.percentage ? width * value.value * 0.01f : value.value; };
    float distance = used(spec->distance);
    *separation = used(spec->separation); *label = distance - *separation; *body = width - distance;
    return isfinite(*label) && isfinite(*separation) && isfinite(*body) &&
        *label > 0.0f && *separation >= 0.0f && *body > 0.0f;
}

static Url* page_resource_base(DomElement* source, const Url* document, size_t remaining, bool* valid) {
    if (!source) return document ? url_clone(document) : nullptr;
    if (!remaining) { *valid = false; return nullptr; }
    Url* base = page_resource_base(source->parent_element(), document, remaining - 1, valid);
    if (!*valid) return nullptr;
    if (const char* value = source->get_attribute("xml:base")) {
        Url* resolved = base ? url_parse_with_base(value, base) : url_parse_path_or_url(value, nullptr);
        if (base) url_destroy(base);
        base = resolved; *valid = base && url_is_valid(base);
    }
    return base;
}

const char* radiant_page_resource_url(DomElement* source, const char* resource, Pool* pool, size_t max_depth) {
    if (!source || !resource || !pool) return nullptr;
    if (url_is_absolute_url(resource)) return pool_strdup(pool, resource);
    bool valid = true;
    Url* base = page_resource_base(source, source->doc ? source->doc->url : nullptr, max_depth, &valid);
    // XML Base uses URI references: /images/ remains on a remote origin, rather than becoming a file path.
    Url* resolved = valid ? (base ? url_parse_with_base(resource, base) : url_parse_path_or_url(resource, nullptr)) : nullptr;
    if (base) url_destroy(base);
    String* serialized = resolved && url_is_valid(resolved) ? url_serialize(resolved) : nullptr;
    const char* result = serialized ? pool_dup_n(pool, serialized->chars, serialized->len) : nullptr;
    if (serialized) mem_free(serialized);
    if (resolved) url_destroy(resolved);
    return result;
}

bool radiant_image_trait_name(const char* name) {
    for (const char* candidate : image_trait_names) if (!strcmp(name, candidate)) return true;
    return false;
}

CssValue* radiant_image_computed_trait(Pool* pool, const RadiantImageSpec* spec, const char* name) {
    if (!pool || !name || (spec && spec->status != VIEW_MODEL_OK)) return nullptr;
    RadiantImageSpec initial = {}; if (!spec) spec = &initial;
    for (size_t i = 0; i < sizeof(image_trait_names) / sizeof(*image_trait_names); i++) if (!strcmp(name, image_trait_names[i])) {
        if (i == 2) return css_value_create_keyword(pool, spec->non_uniform ? "non-uniform" : "uniform");
        if (i < 2) {
            const RadiantImageAxis& axis = spec->axes[i];
            if (axis.kind == RADIANT_IMAGE_LENGTH) return css_value_create_length(pool, axis.value, CSS_UNIT_PX);
            if (axis.kind == RADIANT_IMAGE_PERCENT) return css_value_create_percentage(pool, axis.value);
            const char* keywords[] = {"auto", "scale-to-fit", "scale-down-to-fit", "scale-up-to-fit"};
            size_t keyword = axis.kind == RADIANT_IMAGE_AUTO ? 0u : (size_t)axis.kind - RADIANT_IMAGE_FIT + 1u;
            return keyword < 4 ? css_value_create_keyword(pool, keywords[keyword]) : nullptr;
        }
        const RadiantImageScales& scales = spec->allowed[i - 3];
        if (!scales.count) return css_value_create_keyword(pool, "any");
        if (scales.count > (size_t)INT_MAX - (scales.any ? 1u : 0u) || !scales.values) return nullptr;
        ArrayList* entries = arraylist_new(8); if (!entries) return nullptr;
        bool valid = true;
        for (size_t j = 0; valid && j < scales.count + (scales.any ? 1u : 0u); j++) {
            CssValue* entry = j < scales.count ? css_value_create_percentage(pool, (double)scales.values[j] * 100.0)
                : css_value_create_keyword(pool, "any");
            valid = entry && arraylist_append(entries, entry);
        }
        CssValue* value = valid ? css_value_create_list(pool, (CssValue**)entries->data, (size_t)entries->length) : nullptr;
        arraylist_free(entries); return value;
    }
    return nullptr;
}

bool radiant_image_traits_resolve(ViewTree* tree, ViewCssStyle* style) {
    const char* values[5] = {};
    bool specified = page_native_values(style->source, image_trait_names, 5, values);
    const RadiantImageSpec* inherited = style->parent ? style->parent->image_spec.get() : nullptr;
    if (!specified && (!inherited || (!inherited->allowed[0].count && !inherited->allowed[0].any &&
        !inherited->allowed[1].count && !inherited->allowed[1].any))) return true;
    RadiantImageSpec* result = (RadiantImageSpec*)pool_calloc(tree->model->css->pool, sizeof(RadiantImageSpec));
    if (!result) return false;
    style->image_spec = lam::up(result);
    RadiantImageSpec initial = {};
    const RadiantImageSpec* parent = inherited ? inherited : &initial;
    // allowed scales inherit through ordinary blocks; content dimensions and scaling do not.
    for (size_t i = 0; i < 2; i++) result->allowed[i] = parent->allowed[i];
    auto fail = [&]() { result->status = VIEW_MODEL_INVALID_ARGUMENT; result->reason = "invalid native image content size or scaling"; return true; };
    for (size_t i = 0; i < 3; i++) {
        const char* value = values[i]; if (!value) continue;
        const CssValue* computed = page_computed_value(tree, style, value);
        if (!computed) return fail();
        const char* keyword = computed->type == CSS_VALUE_TYPE_KEYWORD || computed->type == CSS_VALUE_TYPE_CUSTOM
            ? css_value_identifier_name(computed) : nullptr;
        if (i == 2) {
            if (!keyword) return fail();
            if (!strcmp(keyword, "inherit")) result->non_uniform = parent->non_uniform;
            else if (!strcmp(keyword, "non-uniform")) result->non_uniform = true;
            else if (strcmp(keyword, "uniform")) return fail();
            continue;
        }
        RadiantImageAxis& axis = result->axes[i];
        if (keyword && !strcmp(keyword, "inherit")) { axis = parent->axes[i]; continue; }
        if (keyword && !strcmp(keyword, "auto")) continue;
        if (keyword && !strcmp(keyword, "scale-to-fit")) axis.kind = RADIANT_IMAGE_FIT;
        else if (keyword && !strcmp(keyword, "scale-down-to-fit")) axis.kind = RADIANT_IMAGE_FIT_DOWN;
        else if (keyword && !strcmp(keyword, "scale-up-to-fit")) axis.kind = RADIANT_IMAGE_FIT_UP;
        else {
            RadiantLengthPercentage size = {};
            if (!page_resolved_dimension(tree, style, computed, &size)) return fail();
            axis.kind = size.percentage ? RADIANT_IMAGE_PERCENT : RADIANT_IMAGE_LENGTH;
            axis.value = size.value;
            if (!isfinite(axis.value) || axis.value < 0.0f) return fail();
        }
    }
    for (size_t axis = 0; axis < 2; axis++) {
        const char* text = values[axis + 3];
        if (!text || !strcmp(text, "inherit")) continue;
        const CssValue* value = page_computed_value(tree, style, text);
        bool list = value && value->type == CSS_VALUE_TYPE_LIST;
        size_t count = list ? (size_t)value->data.list.count : 1u;
        if (!value || !count || (list && value->data.list.comma_separated) || count > SIZE_MAX / sizeof(float)) return fail();
        float* scales = (float*)pool_alloc(tree->model->css->pool, count * sizeof(float));
        if (!scales) return false;
        RadiantImageScales& allowed = result->allowed[axis]; allowed = {scales, 0, false};
        for (size_t i = 0; i < count; i++) {
            const CssValue* entry = list ? value->data.list.values[i] : value;
            const char* keyword = entry && (entry->type == CSS_VALUE_TYPE_KEYWORD || entry->type == CSS_VALUE_TYPE_CUSTOM)
                ? css_value_identifier_name(entry) : nullptr;
            if (keyword && !strcmp(keyword, "any")) { allowed.any = true; continue; }
            CssMathEvaluationContext context = {}; context.preserve_percentages = true;
            CssMathResult factor = css_math_evaluate(entry, &context);
            float scale = (float)(factor.percentage / 100.0);
            if (factor.type != CSS_MATH_PERCENT || !factor.resolved || !isfinite(scale) || scale < 0.0f) return fail();
            scales[allowed.count++] = scale;
        }
        qsort(scales, allowed.count, sizeof(float), sort_cmp_float_asc);
    }
    return true;
}

static bool page_image_scale_equal(float left, float right) {
    // equivalent percentage and length divisions may differ by one float rounding step.
    return left == right || (isfinite(left) && isfinite(right) &&
        fabsf(left - right) <= FLT_EPSILON * fmaxf(fabsf(left), fabsf(right)));
}

static bool page_image_scale_explicit(const RadiantImageScales& allowed, float scale) {
    size_t low = 0, high = allowed.count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (page_image_scale_equal(allowed.values[middle], scale)) return true;
        if (allowed.values[middle] < scale) low = middle + 1;
        else high = middle;
    }
    return false;
}

static bool page_image_scale_select(const RadiantImageScales* const* sets, size_t count,
        float target, bool flexible, float* selected) {
    if (!isfinite(target) || target < 0.0f) return false;
    size_t best_fallbacks = SIZE_MAX; float best = -1.0f;
    auto consider = [&](float scale) {
        if (!isfinite(scale) || scale < 0.0f ||
            (scale > target && !page_image_scale_equal(scale, target)) ||
            (!flexible && !page_image_scale_equal(scale, target))) return;
        size_t fallbacks = 0;
        for (size_t i = 0; i < count; i++) {
            const RadiantImageScales& set = *sets[i];
            if (!set.count || page_image_scale_explicit(set, scale)) continue;
            if (!set.any) return;
            fallbacks++;
        }
        if (fallbacks < best_fallbacks || (fallbacks == best_fallbacks && scale > best)) {
            best = scale; best_fallbacks = fallbacks;
        }
    };
    consider(target);
    for (size_t i = 0; i < count; i++) for (size_t j = 0; j < sets[i]->count; j++) consider(sets[i]->values[j]);
    if (best < 0.0f) return false;
    *selected = best; return true;
}

bool radiant_image_size(const RadiantImageSpec* spec, float natural_width, float natural_height,
        float* viewport_width, float* viewport_height, float* content_width, float* content_height) {
    if (!spec || spec->status != VIEW_MODEL_OK || !viewport_width || !viewport_height || !content_width || !content_height ||
        !isfinite(natural_width) || !isfinite(natural_height) || natural_width <= 0.0f || natural_height <= 0.0f) return false;
    const float natural[] = {natural_width, natural_height};
    const float viewport[] = {*viewport_width, *viewport_height};
    float scale[] = {1.0f, 1.0f}; bool specified[] = {false, false}, flexible[] = {false, false};
    for (size_t i = 0; i < 2; i++) {
        const RadiantImageAxis& axis = spec->axes[i]; specified[i] = axis.kind != RADIANT_IMAGE_AUTO;
        if (axis.kind == RADIANT_IMAGE_LENGTH) scale[i] = axis.value / natural[i];
        else if (axis.kind == RADIANT_IMAGE_PERCENT) scale[i] = axis.value / 100.0f;
        else if (axis.kind >= RADIANT_IMAGE_FIT && isfinite(viewport[i])) {
            scale[i] = viewport[i] / natural[i];
            flexible[i] = axis.kind == RADIANT_IMAGE_FIT || (axis.kind == RADIANT_IMAGE_FIT_DOWN && scale[i] < 1.0f) ||
                (axis.kind == RADIANT_IMAGE_FIT_UP && scale[i] > 1.0f);
            if (axis.kind == RADIANT_IMAGE_FIT_DOWN) scale[i] = fminf(1.0f, scale[i]);
            if (axis.kind == RADIANT_IMAGE_FIT_UP) scale[i] = fmaxf(1.0f, scale[i]);
        }
    }
    const RadiantImageScales* sets[] = {&spec->allowed[0], &spec->allowed[1]};
    bool linked = specified[0] != specified[1] || !spec->non_uniform;
    if (linked) {
        size_t axis = specified[0] ? 0u : 1u;
        float target = specified[0] && specified[1] ? fminf(scale[0], scale[1]) : scale[axis];
        bool variable = specified[0] && specified[1] ? flexible[0] && flexible[1] : flexible[axis];
        if (!page_image_scale_select(sets, 2, target, variable, &scale[0])) return false;
        scale[1] = scale[0];
    } else for (size_t i = 0; i < 2; i++)
        if (!page_image_scale_select(sets + i, 1, scale[i], flexible[i], &scale[i])) return false;
    *content_width = natural_width * scale[0]; *content_height = natural_height * scale[1];
    if (isnan(*viewport_width)) *viewport_width = *content_width;
    if (isnan(*viewport_height)) *viewport_height = *content_height;
    return isfinite(*content_width) && isfinite(*content_height) && *content_width >= 0.0f && *content_height >= 0.0f &&
        isfinite(*viewport_width) && isfinite(*viewport_height) && *viewport_width >= 0.0f && *viewport_height >= 0.0f;
}

bool radiant_whitespace_trait_name(const char* name) {
    for (const char* candidate : whitespace_names) if (!strcmp(name, candidate)) return true;
    return false;
}

RadiantWhitespaceSpec radiant_whitespace_spec(const ViewCssStyle* style) {
    if (style->whitespace) return *style->whitespace;
    CssEnum mode = style->white_space;
    bool preserve = mode == CSS_VALUE_PRE || mode == CSS_VALUE_PRE_WRAP || mode == CSS_VALUE_BREAK_SPACES;
    RadiantWhitespaceSpec result = {};
    result.linefeed = preserve || mode == CSS_VALUE_PRE_LINE ? RADIANT_LINEFEED_PRESERVE : RADIANT_LINEFEED_SPACE;
    result.collapse = result.discard_start = result.discard_end = !preserve;
    result.wrap = mode != CSS_VALUE_PRE && mode != CSS_VALUE_NOWRAP;
    return result;
}

bool radiant_whitespace_resolve(ViewTree* tree, ViewCssStyle* style) {
    const char* values[4] = {}; bool specified = false;
    if (!style->pseudo_element && style->source) specified = page_native_values(style->source, whitespace_names, 4, values);
    const RadiantWhitespaceSpec* parent = style->parent ? style->parent->whitespace.get() : nullptr;
    if (!specified && !parent) return true;
    RadiantWhitespaceSpec* result = (RadiantWhitespaceSpec*)pool_alloc(tree->model->css->pool, sizeof(RadiantWhitespaceSpec));
    if (!result) return false;
    *result = parent ? *parent : radiant_whitespace_spec(style); style->whitespace = lam::up(result);
    RadiantWhitespaceSpec inherited = {};
    inherited.collapse = inherited.wrap = inherited.discard_start = inherited.discard_end = true;
    if (style->parent) inherited = radiant_whitespace_spec(style->parent);
    auto fail = [&]() { result->status = VIEW_MODEL_INVALID_ARGUMENT; result->reason = "invalid native whitespace trait"; return true; };
    for (size_t i = 0; i < 4; i++) {
        const char* value = values[i]; if (!value) continue;
        if (!strcmp(value, "inherit")) {
            if (!i) result->linefeed = inherited.linefeed;
            else if (i == 1) { result->ignore = inherited.ignore; result->discard_start = inherited.discard_start; result->discard_end = inherited.discard_end; }
            else if (i == 2) result->collapse = inherited.collapse;
            else result->wrap = inherited.wrap;
            continue;
        }
        if (!i) {
            if (!strcmp(value, "treat-as-space")) result->linefeed = RADIANT_LINEFEED_SPACE;
            else if (!strcmp(value, "preserve")) result->linefeed = RADIANT_LINEFEED_PRESERVE;
            else if (!strcmp(value, "ignore")) result->linefeed = RADIANT_LINEFEED_IGNORE;
            else if (!strcmp(value, "treat-as-zero-width-space")) result->linefeed = RADIANT_LINEFEED_ZERO_WIDTH;
            else return fail();
        } else if (i == 1) {
            result->ignore = !strcmp(value, "ignore");
            result->discard_start = !strcmp(value, "ignore-if-after-linefeed") || !strcmp(value, "ignore-if-surrounding-linefeed");
            result->discard_end = !strcmp(value, "ignore-if-before-linefeed") || !strcmp(value, "ignore-if-surrounding-linefeed");
            if (!result->ignore && !result->discard_start && !result->discard_end && strcmp(value, "preserve")) return fail();
        } else if (i == 2) {
            if (!radiant_page_boolean(value, inherited.collapse, &result->collapse)) return fail();
        } else {
            if (strcmp(value, "wrap") && strcmp(value, "no-wrap")) return fail();
            result->wrap = !strcmp(value, "wrap");
        }
    }
    return true;
}

static const char* flow_table_omit_names[] = {"table-omit-header-at-break", "table-omit-footer-at-break"};

static const char* flow_space_names[] = {"space-before", "space-after"};
static const char* flow_space_components[] = {"minimum", "optimum", "maximum", "precedence", "conditionality"};
static const char* flow_keep_names[] = {"keep-together", "keep-with-next", "keep-with-previous"};
static const char* flow_keep_components[] = {"within-line", "within-column", "within-page"};

bool radiant_flow_trait_key(const char* name, bool* space, size_t* index, size_t* component, const char** base) {
    if (!name) return false;
    for (size_t family = 0; family < 2; family++) {
        const char* const* names = family ? flow_keep_names : flow_space_names;
        const char* const* components = family ? flow_keep_components : flow_space_components;
        size_t count = family ? 3 : 2, component_count = family ? 3 : 5;
        for (size_t i = 0; i < count; i++) {
            size_t length = strlen(names[i]);
            if (strncmp(name, names[i], length)) continue;
            if (!name[length]) {
                *space = !family; *index = i; *component = 0; if (base) *base = names[i]; return true;
            }
            if (name[length] != '.') continue;
            for (size_t j = 0; j < component_count; j++) if (!strcmp(name + length + 1, components[j])) {
                *space = !family; *index = i; *component = j + 1; if (base) *base = names[i]; return true;
            }
        }
    }
    return false;
}


static const char* flow_decoration_names[2][2] = {
    {"border-before-width.conditionality", "border-after-width.conditionality"},
    {"padding-before.conditionality", "padding-after.conditionality"}
};

static bool flow_decoration_key(const char* name, size_t* family, size_t* edge) {
    for (size_t i = 0; i < 2; i++) for (size_t j = 0; j < 2; j++)
        if (!strcmp(name, flow_decoration_names[i][j])) { *family = i; *edge = j; return true; }
    return false;
}

bool radiant_table_column_proportion(const CssValue* value, float* result) {
    if (!value || value->type != CSS_VALUE_TYPE_NUMBER || !isfinite(value->data.number.value) ||
        value->data.number.value <= 0.0 || value->data.number.value > FLT_MAX) return false;
    *result = (float)value->data.number.value;
    return *result > 0.0f;
}

bool radiant_decoration_retain(const ViewCssStyle* style, bool padding, bool after) {
    RadiantDecorationConditionality policy = style && style->flow_traits
        ? style->flow_traits->decoration[padding][after] : RADIANT_DECORATION_CSS;
    return policy == RADIANT_DECORATION_RETAIN || (policy == RADIANT_DECORATION_CSS && style && style->decoration_clone);
}

bool radiant_flow_trait_name(const char* name) {
    bool space; size_t index, component;
    return name && (!strcmp(name, "line-stacking-strategy") || !strcmp(name, "text-altitude") ||
        !strcmp(name, "text-depth") || !strcmp(name, "area-source") || !strcmp(name, "block-inline-geometry") ||
        !strcmp(name, "start-indent") || !strcmp(name, "end-indent") || !strcmp(name, "column-proportion") || !strcmp(name, "column-number") ||
        !strcmp(name, "style-transparent") ||
        !strcmp(name, flow_table_omit_names[0]) || !strcmp(name, flow_table_omit_names[1]) ||
        flow_decoration_key(name, &index, &component) || radiant_flow_trait_key(name, &space, &index, &component));
}

bool radiant_text_metrics(const ViewCssStyle* style, float* altitude, float* depth) {
    const FontMetrics* font = style && style->font.font_handle ? font_get_metrics(style->font.font_handle) : nullptr;
    if (!font) return false;
    const RadiantFlowTraits* traits = style->flow_traits.get();
    *altitude = traits && traits->text_metrics_set[0] ? traits->text_metrics[0] : font->ascender;
    *depth = traits && traits->text_metrics_set[1] ? traits->text_metrics[1] : -font->descender;
    return true;
}

int radiant_keep_compare(RadiantKeepStrength left, RadiantKeepStrength right) {
    if (left.kind != right.kind) return left.kind < right.kind ? -1 : 1;
    return left.kind != RADIANT_KEEP_NUMBER || left.value == right.value ? 0 : left.value < right.value ? -1 : 1;
}

RadiantKeepStrength radiant_keep_max(RadiantKeepStrength left, RadiantKeepStrength right) {
    return radiant_keep_compare(left, right) < 0 ? right : left;
}

RadiantSpaceSpec radiant_spaces_resolve(const RadiantSpaceSpec* spaces, size_t count, bool start, bool end) {
    size_t first = 0, last = count;
    auto zero = [](const RadiantSpaceSpec& value) { return !value.minimum && !value.optimum && !value.maximum; };
    // conditional runs stop at a retained nonzero space (XSL 1.1 §4.3.1).
    if (start) while (first < last && (!spaces[first].retain || zero(spaces[first]))) first++;
    if (end) while (last > first && (!spaces[last - 1].retain || zero(spaces[last - 1]))) last--;
    RadiantSpaceSpec result = {}; bool forced = false;
    for (size_t i = first; i < last; i++) if (spaces[i].specified) forced |= spaces[i].force;
    for (size_t i = first; i < last; i++) {
        RadiantSpaceSpec value = spaces[i];
        if (!value.specified) continue;
        value.minimum = fminf(value.minimum, value.optimum); value.maximum = fmaxf(value.maximum, value.optimum);
        if (forced) {
            if (!value.force) continue;
            result.minimum += value.minimum; result.optimum += value.optimum; result.maximum += value.maximum;
            result.specified = true; result.force = true;
        } else if (!result.specified || value.precedence > result.precedence ||
            (value.precedence == result.precedence && value.optimum > result.optimum)) result = value;
        else if (value.precedence == result.precedence && value.optimum == result.optimum) {
            result.minimum = fmaxf(result.minimum, value.minimum); result.maximum = fminf(result.maximum, value.maximum);
        }
    }
    result.retain = true;
    return result;
}

static bool flow_integer(const char* value, int32_t* result) {
    int64_t parsed = 0; const char* end = nullptr;
    if (!str_to_int64(value, strlen(value), &parsed, &end) || *end || parsed < INT32_MIN || parsed > INT32_MAX) return false;
    *result = static_cast<int32_t>(parsed);
    return true;
}

static bool flow_computed_integer(ViewTree* tree, ViewCssStyle* style, const CssValue* value, int32_t* result) {
    double number = view_css_number(value, tree, style);
    if (!isfinite(number) || number != floor(number) || number < INT32_MIN || number > INT32_MAX) return false;
    *result = static_cast<int32_t>(number); // INT_CAST_OK: validated keep/precedence counter domain
    return true;
}

static bool flow_length(ViewTree* tree, ViewCssStyle* style, const char* text, float* result) {
    RadiantLengthPercentage size = {};
    if (!page_computed_dimension(tree, style, text, &size) || size.percentage) return false;
    *result = size.value; return true;
}

bool radiant_flow_traits_resolve(ViewTree* tree, ViewCssStyle* style) {
    const char* spaces[2][6] = {}; const char* keeps[3][4] = {};
    const char* decorations[2][2] = {};
    const char* cell_alignment = nullptr; const char* line_stacking = nullptr;
    const char* area_source = nullptr;
    const char* inline_geometry = nullptr; const char* column_proportion = nullptr; const char* column_number = nullptr;
    const char* indents[2] = {}; const char* table_omit[2] = {};
    const char* nominal[2] = {};
    const char* style_transparent = nullptr;
    bool any = page_native_attributes(style->source, [&](const char* local, const char* value) {
        bool space; size_t index, component;
        if (!strcmp(local, "cell-alignment")) {
            cell_alignment = value; return true;
        }
        if (!strcmp(local, "line-stacking-strategy")) { line_stacking = value; return true; }
        if (!strcmp(local, "style-transparent")) { style_transparent = value; return true; }
        if (!strcmp(local, "column-proportion")) { column_proportion = value; return true; }
        if (!strcmp(local, "column-number")) { column_number = value; return true; }
        for (size_t i = 0; i < 2; i++) if (!strcmp(local, flow_table_omit_names[i])) { table_omit[i] = value; return true; }
        if (!strcmp(local, "area-source")) { area_source = value; return true; }
        if (!strcmp(local, "block-inline-geometry")) { inline_geometry = value; return true; }
        if (!strcmp(local, "start-indent")) { indents[0] = value; return true; }
        if (!strcmp(local, "end-indent")) { indents[1] = value; return true; }
        if (!strcmp(local, "text-altitude")) { nominal[0] = value; return true; }
        if (!strcmp(local, "text-depth")) { nominal[1] = value; return true; }
        if (flow_decoration_key(local, &index, &component)) { decorations[index][component] = value; return true; }
        if (!radiant_flow_trait_key(local, &space, &index, &component)) return false;
        (space ? spaces[index][component] : keeps[index][component]) = value; return true;
    });
    const RadiantFlowTraits* inherited_traits = style->parent ? style->parent->flow_traits.get() : nullptr;
    if (!any && (!inherited_traits || (inherited_traits->line_stacking == RADIANT_LINE_STACK_CSS &&
        inherited_traits->indents[0] == 0.0f && inherited_traits->indents[1] == 0.0f &&
        !inherited_traits->indent_expressions[0] && !inherited_traits->indent_expressions[1]))) return true;
    RadiantFlowTraits* traits = (RadiantFlowTraits*)pool_calloc(tree->model->css->pool, sizeof(RadiantFlowTraits));
    if (!traits) return false;
    style->flow_traits = lam::up(traits);
    RadiantFlowTraits empty = {};
    const RadiantFlowTraits* parent = style->parent && style->parent->flow_traits ? style->parent->flow_traits.get() : &empty;
    traits->line_stacking = parent->line_stacking;
    for (size_t i = 0; i < 2; i++) {
        traits->indents[i] = parent->indents[i];
        traits->indent_expressions[i] = parent->indent_expressions[i];
        traits->indent_owners[i] = parent->indent_owners[i];
    }
    auto fail = [&](const char* reason) { traits->status = VIEW_MODEL_INVALID_ARGUMENT; traits->reason = reason; return true; };
    if (style_transparent && strcmp(style_transparent, "true") && strcmp(style_transparent, "false"))
        return fail("style transparency requires true or false");
    for (size_t i = 0; i < 2; i++) if (table_omit[i] &&
        !radiant_page_boolean(table_omit[i], parent->table_omit[i], &traits->table_omit[i]))
        return fail("table furniture omission requires true, false or inherit");
    if (column_number && strcmp(column_number, "auto")) {
        if (!strcmp(column_number, "inherit")) traits->column_number = parent->column_number;
        else {
            int32_t number = 0;
            if (!flow_integer(column_number, &number) || number < 1)
                return fail("column number requires auto or a positive integer in the counter domain");
            traits->column_number = static_cast<uint32_t>(number);
        }
    }
    if (column_proportion && strcmp(column_proportion, "none")) {
        if (!strcmp(column_proportion, "inherit")) traits->column_proportion = parent->column_proportion;
        else {
            CssDeclaration* parsed = css_parse_property_value_declaration("flex-grow", 9, column_proportion,
                strlen(column_proportion), tree->model->css->pool);
            if (!parsed || !parsed->valid || !radiant_table_column_proportion(parsed->value, &traits->column_proportion))
                return fail("column proportion requires none or a finite positive number");
        }
    }
    for (size_t i = 0; i < 2; i++) for (size_t j = 0; j < 2; j++) if (const char* value = decorations[i][j]) {
        RadiantDecorationConditionality& policy = traits->decoration[i][j];
        if (!strcmp(value, "inherit")) policy = parent->decoration[i][j];
        else if (!strcmp(value, "retain")) policy = RADIANT_DECORATION_RETAIN;
        else if (!strcmp(value, "discard")) policy = RADIANT_DECORATION_DISCARD;
        else if (strcmp(value, "css")) return fail("decoration conditionality requires css, discard or retain");
    }
    for (size_t i = 0; i < 2; i++) if (const char* value = indents[i]) {
        if (!strcmp(value, "inherit")) continue;
        const CssValue* resolved = page_computed_value(tree, style, value);
        CssMathType type = resolved ? css_math_value_type(resolved) : CSS_MATH_INVALID;
        if (type != CSS_MATH_LENGTH && type != CSS_MATH_PERCENT && type != CSS_MATH_LENGTH_PERCENT &&
            !(resolved && resolved->type == CSS_VALUE_TYPE_NUMBER && !resolved->data.number.value))
            return fail("reference indents require a finite length or length-percentage expression");
        const CssValue* computed = view_css_compute_length(tree, style, CSS_PROPERTY_MARGIN_LEFT, resolved, nullptr);
        if (!computed) return false;
        traits->indent_expressions[i] = nullptr; traits->indent_owners[i] = nullptr;
        if (layout_css_value_has_percentage(computed)) {
            if (style->display.outer != CSS_VALUE_BLOCK || style->display.inner != CSS_VALUE_FLOW)
                return fail("percentage indents require an ordinary block declaration context");
            // font terms compute at the owner; percentages await its first selected reference area.
            traits->indents[i] = 0.0f; traits->indent_expressions[i] = lam::up(computed); traits->indent_owners[i] = lam::up(style);
        } else {
            traits->indents[i] = view_css_length(tree, style, computed, CSS_PROPERTY_MARGIN_LEFT, 0.0f, 0.0f);
            if (!isfinite(traits->indents[i])) return fail("reference indents require finite computed lengths");
        }
    }
    if (area_source) {
        if (!strcmp(area_source, "descendants")) traits->descendant_areas = true;
        else if (!strcmp(area_source, "inherit")) traits->descendant_areas = parent->descendant_areas;
        else if (strcmp(area_source, "box")) return fail("area source requires box or descendants");
    }
    if (inline_geometry) {
        if (!strcmp(inline_geometry, "reference")) traits->reference_inline = true;
        else if (!strcmp(inline_geometry, "inherit")) traits->reference_inline = parent->reference_inline;
        else if (strcmp(inline_geometry, "css")) return fail("block inline geometry requires css or reference");
        if (traits->reference_inline && (style->display.outer != CSS_VALUE_BLOCK || style->display.inner != CSS_VALUE_FLOW))
            return fail("reference inline geometry requires an ordinary block formatting context");
    }
    if (line_stacking && strcmp(line_stacking, "inherit")) {
        if (!strcmp(line_stacking, "max-height")) traits->line_stacking = RADIANT_LINE_STACK_MAX;
        else if (!strcmp(line_stacking, "font-height")) traits->line_stacking = RADIANT_LINE_STACK_FONT;
        else if (!strcmp(line_stacking, "line-height") || !strcmp(line_stacking, "css")) traits->line_stacking = RADIANT_LINE_STACK_CSS;
        else return fail("line stacking requires line-height, max-height or font-height");
    }
    for (size_t i = 0; i < 2; i++) if (const char* value = nominal[i]) {
        float& used = traits->text_metrics[i]; traits->text_metrics_set[i] = true;
        const FontMetrics* font = font_get_metrics(style->font.font_handle);
        if (!font) return fail("nominal text geometry requires resolved font metrics");
        if (!strcmp(value, "use-font-metrics")) used = i ? font->typo_descender : font->typo_ascender;
        else if (!strcmp(value, "inherit")) {
            float altitude, depth;
            if (!style->parent) { altitude = font->ascender; depth = -font->descender; }
            else if (!radiant_text_metrics(style->parent.get(), &altitude, &depth))
                return fail("nominal text geometry requires resolved parent font metrics");
            used = i ? depth : altitude;
        } else {
            RadiantLengthPercentage size = {};
            if (!page_computed_dimension(tree, style, value, &size))
                return fail("nominal text geometry requires a font metric, length or font-em percentage");
            used = size.percentage ? size.value * 0.01f * style->font.font_size : size.value;
        }
        if (!isfinite(used) || used < 0.0f) return fail("nominal text geometry requires finite nonnegative values");
    }
    if (cell_alignment) {
        if (style->display.inner != CSS_VALUE_TABLE_CELL ||
            (strcmp(cell_alignment, "css") && strcmp(cell_alignment, "relative-before")))
            return fail("cell alignment requires css or relative-before on a table cell");
        traits->relative_cell_before = !strcmp(cell_alignment, "relative-before");
    }
    RadiantSpaceSpec* outputs[] = {&traits->before, &traits->after};
    const RadiantSpaceSpec* inherited[] = {&parent->before, &parent->after};
    for (size_t i = 0; i < 2; i++) {
        RadiantSpaceSpec& output = *outputs[i]; const RadiantSpaceSpec& ancestor = *inherited[i];
        if (spaces[i][0]) {
            if (!strcmp(spaces[i][0], "inherit")) output = ancestor;
            else if (!flow_length(tree, style, spaces[i][0], &output.optimum)) return fail("space shorthand requires a length");
            else output.minimum = output.maximum = output.optimum;
            output.specified = true;
        }
        float* ranges[] = {&output.minimum, &output.optimum, &output.maximum};
        const float ancestor_ranges[] = {ancestor.minimum, ancestor.optimum, ancestor.maximum};
        for (size_t j = 0; j < 5; j++) if (const char* text = spaces[i][j + 1]) {
            bool inherit = !strcmp(text, "inherit"); output.specified = true;
            if (j < 3) {
                if (inherit) *ranges[j] = ancestor_ranges[j];
                else if (!flow_length(tree, style, text, ranges[j])) return fail("space range requires lengths without percentages");
                continue;
            }
            const CssValue* value = inherit ? nullptr : page_computed_value(tree, style, text);
            const char* keyword = css_value_identifier_name(value);
            if (j == 3) {
                if (inherit) { output.precedence = ancestor.precedence; output.force = ancestor.force; }
                else if (keyword && !strcmp(keyword, "force")) output.force = true;
                else {
                    output.force = false;
                    if (!flow_computed_integer(tree, style, value, &output.precedence)) return fail("space precedence requires force or a signed integer");
                }
            } else if (inherit) output.retain = ancestor.retain;
            else if (keyword && !strcmp(keyword, "retain")) output.retain = true;
            else if (keyword && !strcmp(keyword, "discard")) output.retain = false;
            else return fail("space conditionality requires discard or retain");
        }
        output.minimum = fminf(output.minimum, output.optimum); output.maximum = fmaxf(output.maximum, output.optimum);
    }
    RadiantKeepSpec* keep_outputs[] = {&traits->together, &traits->next, &traits->previous};
    const RadiantKeepSpec* keep_ancestors[] = {&parent->together, &parent->next, &parent->previous};
    for (size_t i = 0; i < 3; i++) for (size_t j = 0; j < 3; j++) {
        const char* text = keeps[i][j + 1] ? keeps[i][j + 1] : keeps[i][0];
        if (!text) continue;
        RadiantKeepStrength& output = keep_outputs[i]->scope[j];
        if (!strcmp(text, "inherit")) output = keep_ancestors[i]->scope[j];
        else {
            const CssValue* value = page_computed_value(tree, style, text);
            const char* keyword = css_value_identifier_name(value);
            if (keyword && !strcmp(keyword, "always")) output.kind = RADIANT_KEEP_ALWAYS;
            else if (!keyword || strcmp(keyword, "auto")) {
                output.kind = RADIANT_KEEP_NUMBER;
                if (!flow_computed_integer(tree, style, value, &output.value)) return fail("keep strength requires auto, always or a signed integer");
            }
        }
    }
    return true;
}

struct RadiantPageProvenance : DomDocumentResourceData {
    HashMap* origins;
};

struct PageOriginLookup {
    const Element* translated;
    const RadiantSourceOrigin* origin;
};
HASHMAP_DEFINE_PTRKEY(page_origins, PageOriginLookup, translated)

static void page_provenance_destroy(DomDocumentResourceData* data) {
    RadiantPageProvenance* provenance = (RadiantPageProvenance*)data;
    if (provenance->origins) hashmap_free(provenance->origins);
    mem_free(provenance);
}

static RadiantPageProvenance* page_provenance(DomDocument* document) {
    for (DomDocumentResource* resource = document->resources; resource; resource = resource->next)
        if (resource->destroy == page_provenance_destroy) return (RadiantPageProvenance*)resource->data.get();
    return nullptr;
}

bool radiant_page_set_origins(DomDocument* document, RadiantSourceOrigin* origins) {
    if (!document) return false;
    HashMap* index = page_origins_new(16);
    if (!index) return false;
    for (RadiantSourceOrigin* origin = origins; origin; origin = origin->next) {
        PageOriginLookup entry = {origin->translated, origin}; hashmap_set(index, &entry);
        if (hashmap_oom(index)) { hashmap_free(index); return false; }
    }
    RadiantPageProvenance* provenance = page_provenance(document);
    if (!provenance) {
        provenance = (RadiantPageProvenance*)mem_calloc(1, sizeof(RadiantPageProvenance), MEM_CAT_LAYOUT);
        if (!provenance) { hashmap_free(index); return false; }
        if (!dom_document_add_resource(document, provenance, page_provenance_destroy)) { mem_free(provenance); hashmap_free(index); return false; }
    }
    if (provenance->origins) hashmap_free(provenance->origins);
    provenance->origins = index;
    return true;
}

const RadiantSourceOrigin* radiant_page_source_origin(DomDocument* document, DomNode* translated) {
    if (!document || !translated) return nullptr;
    RadiantPageProvenance* provenance = page_provenance(document);
    if (!provenance) return nullptr;
    for (DomNode* node = translated; node; node = node->parent) if (node->is_element()) {
        PageOriginLookup key = {dom_element_render_source(node->as_element()), nullptr};
        const PageOriginLookup* entry = (const PageOriginLookup*)hashmap_get(provenance->origins, &key);
        // generated control children share the nearest original object's provenance.
        if (entry) return entry->origin;
    }
    return nullptr;
}

bool radiant_page_element(DomElement* element, const char* local_name) {
    if (!element || !local_name || strcmp(element->local_name(), local_name) != 0) return false;
    const char* uri = dom_element_namespace_uri(element);
    return uri && strcmp(uri, RADIANT_PAGE_NAMESPACE) == 0;
}

bool radiant_page_control_hidden(DomElement* element) {
    return radiant_page_element(element, "master-set") || radiant_page_element(element, "page-master") ||
        radiant_page_element(element, "region") || radiant_page_element(element, "column") || radiant_page_element(element, "master-rule") ||
        radiant_page_element(element, "sequence-master") || radiant_page_element(element, "master-run");
}

const RadiantPageSequence* radiant_page_sequence_for(const RadiantPageDocument* document, DomElement* element) {
    if (!document || !radiant_page_element(element, "page-sequence")) return nullptr;
    for (const RadiantPageSequence* sequence = document->sequences; sequence; sequence = sequence->next)
        if (!sequence->implicit && sequence->source.address == element) return sequence;
    return nullptr;
}

static bool page_document_fail(RadiantPageDocument* program, ViewModelStatus status,
        DomElement* source, const char* reason) {
    if (program->diagnostic.status == VIEW_MODEL_OK)
        program->diagnostic = {status, source ? dom_node_ref(source) : DomNodeRef{}, reason};
    return false;
}

static bool page_fixed_length(DomElement* source, const char* name, Pool* pool, float* result) {
    const char* text = source->get_attribute(name);
    if (!text) return false;
    CssDeclaration* declaration = css_parse_property_value_declaration(name, strlen(name), text, strlen(text), pool);
    const CssValue* value = declaration ? declaration->value : nullptr;
    double pixels = 0.0;
    if (!value || value->type != CSS_VALUE_TYPE_LENGTH ||
        !css_absolute_length_to_px(value->data.length.unit, value->data.length.value, &pixels) ||
        !isfinite(pixels) || pixels <= 0.0 || pixels > FLT_MAX) return false;
    *result = (float)pixels;
    return *result > 0.0f;
}

static bool page_fixed_box(const char* text, RdtLogicalRect* box) {
    float coordinates[4];
    for (size_t i = 0; i < 4; i++) {
        text = str_skip_ascii_space(text);
        const char* end = nullptr; double value = 0.0;
        if (!*text || !str_to_double(text, strlen(text), &value, &end) || !isfinite(value) || fabs(value) > FLT_MAX ||
            (i < 3 && (*end != ' ' && *end != '\t' && *end != '\n' && *end != '\r'))) return false;
        coordinates[i] = (float)value; text = end;
    }
    *box = {coordinates[0], coordinates[1], coordinates[2] - coordinates[0], coordinates[3] - coordinates[1]};
    return !*str_skip_ascii_space(text) && isfinite(box->width) && isfinite(box->height) && box->width > 0.0f && box->height > 0.0f;
}

RdtLogicalRect radiant_fixed_page_crop(const RadiantFixedGeometry* geometry) {
    RdtLogicalRect media = geometry->boxes[RADIANT_SOURCE_MEDIA];
    RdtLogicalRect crop = geometry->box_mask & (1u << RADIANT_SOURCE_CROP) ? geometry->boxes[RADIANT_SOURCE_CROP] : media;
    float right = fminf(media.x + media.width, crop.x + crop.width);
    float top = fminf(media.y + media.height, crop.y + crop.height);
    crop.x = fmaxf(media.x, crop.x); crop.y = fmaxf(media.y, crop.y);
    crop.width = right - crop.x; crop.height = top - crop.y;
    return crop;
}

RdtMatrix radiant_fixed_page_source_transform(const RadiantFixedGeometry* geometry) {
    RdtLogicalRect crop = radiant_fixed_page_crop(geometry);
    int32_t rotation = (geometry->rotation % 360 + 360) % 360;
    RdtMatrix matrix = rdt_matrix_identity();
    matrix.e13 = crop.x; matrix.e23 = crop.y;
    if (rotation == 90) {
        matrix.e11 = matrix.e22 = 0.0f; matrix.e12 = -1.0f; matrix.e21 = 1.0f; matrix.e13 += crop.width;
    } else if (rotation == 180) {
        matrix.e11 = matrix.e22 = -1.0f; matrix.e13 += crop.width; matrix.e23 += crop.height;
    } else if (rotation == 270) {
        matrix.e11 = matrix.e22 = 0.0f; matrix.e12 = 1.0f; matrix.e21 = -1.0f; matrix.e23 += crop.height;
    }
    matrix.e13 *= 96.0f / 72.0f; matrix.e23 *= 96.0f / 72.0f;
    return matrix;
}

static bool page_fixed_compile(RadiantPageDocument* program, DomElement* source, Pool* pool) {
    if (program->fixed_source.address)
        return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "fixed document requires one fixed-pages container");
    program->fixed_source = dom_node_ref(source);
    RadiantFixedPage* tail = nullptr;
    for (DomNode* node = source->first_child; node; node = node->next_sibling) {
        if (node->is_text()) {
            if (!radiant_page_whitespace(node->as_text()))
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "fixed-pages accepts only fixed-page children");
            continue;
        }
        if (!node->is_element()) continue;
        DomElement* element = node->as_element();
        if (!radiant_page_element(element, "fixed-page"))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, element, "fixed-pages accepts only fixed-page children");
        RadiantFixedPage* page = (RadiantFixedPage*)pool_calloc(pool, sizeof(RadiantFixedPage));
        if (!page) return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, element, "fixed page allocation failed");
        page->source = dom_node_ref(element);
        if (!page_fixed_length(element, "width", pool, &page->viewport.width) ||
            !page_fixed_length(element, "height", pool, &page->viewport.height))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, element, "fixed page viewport requires positive finite absolute lengths");
        static const char* names[] = {"source-media-box", "source-crop-box", "source-bleed-box", "source-trim-box", "source-art-box"};
        for (size_t i = 0; i < RADIANT_SOURCE_BOX_COUNT; i++) if (const char* text = element->get_attribute(names[i])) {
            if (!page_fixed_box(text, &page->geometry.boxes[i]))
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, element, "source page boxes require four finite increasing coordinates");
            page->geometry.box_mask |= 1u << i;
        }
        if (const char* text = element->get_attribute("source-rotation")) {
            if (!flow_integer(text, &page->geometry.rotation) || page->geometry.rotation % 90)
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, element, "source rotation requires a signed multiple of 90 degrees");
        }
        if ((page->geometry.box_mask || page->geometry.rotation) && !(page->geometry.box_mask & 1u))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, element, "source geometry requires a media box");
        if (page->geometry.box_mask & 1u) {
            RdtLogicalRect crop = radiant_fixed_page_crop(&page->geometry);
            int32_t rotation = (page->geometry.rotation % 360 + 360) % 360;
            bool turn = rotation == 90 || rotation == 270;
            float width = (turn ? crop.height : crop.width) * (96.0f / 72.0f);
            float height = (turn ? crop.width : crop.height) * (96.0f / 72.0f);
            float tolerance = FLT_EPSILON * 8.0f * fmaxf(width, height);
            if (crop.width <= 0.0f || crop.height <= 0.0f || !isfinite(width) || !isfinite(height) ||
                fabsf(page->viewport.width - width) > tolerance || fabsf(page->viewport.height - height) > tolerance)
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, element, "fixed viewport must match the rotated media/crop intersection");
        }
        page->geometry.source_page = static_cast<uint32_t>(program->fixed_count + 1);
        if (const char* text = element->get_attribute("source-page")) {
            int64_t ordinal = 0; const char* end = nullptr;
            if (!str_to_int64(text, strlen(text), &ordinal, &end) || *end || ordinal < 1 || ordinal > UINT32_MAX)
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, element, "source page requires a positive ordinal");
            page->geometry.source_page = static_cast<uint32_t>(ordinal);
        }
        const char* label = element->get_attribute("source-label");
        if (label && !(page->label = pool_strdup(pool, label)))
            return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, element, "fixed page label allocation failed");
        if (tail) tail->next = page; else program->fixed_pages = page;
        tail = page; program->fixed_count++;
    }
    return program->fixed_count || page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "fixed-pages requires at least one page");
}

static bool page_fixed_scan(RadiantPageDocument* program, DomElement* source, Pool* pool, size_t depth) {
    if (!source) return true;
    if (depth > 512) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "page control nesting limit exceeded");
    if (radiant_page_element(source, "fixed-pages")) return page_fixed_compile(program, source, pool);
    if (radiant_page_element(source, "fixed-page"))
        return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "fixed-page requires a fixed-pages parent");
    if (radiant_page_control_hidden(source)) return true;
    for (DomElement* child = source->first_child_element(); child; child = child->next_sibling_element())
        if (!page_fixed_scan(program, child, pool, depth + 1)) return false;
    return true;
}

static CssDeclaration** page_declarations_snapshot(CssDeclaration** declarations, size_t count, Pool* pool) {
    if (!count) return nullptr;
    CssDeclaration** result = (CssDeclaration**)pool_calloc(pool, count * sizeof(CssDeclaration*));
    if (!result) return nullptr;
    for (size_t i = 0; i < count; i++) {
        result[i] = css_declaration_snapshot(declarations[i], pool);
        if (!result[i]) return nullptr;
    }
    return result;
}

static bool page_rule_append(RadiantPageDocument* program, const CssPageRule* input,
        CssOrigin origin, RadiantPageRuleSource kind, DomElement* source, Pool* pool) {
    RadiantPageRule* rule = (RadiantPageRule*)pool_calloc(pool, sizeof(RadiantPageRule));
    if (!rule) return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, source, "page rule allocation failed");
    rule->kind = kind; rule->origin = origin;
    rule->source = source ? dom_node_ref(source) : DomNodeRef{};
    rule->selector_count = input->selector_count;
    rule->selectors = (CssPageSelector*)pool_calloc(pool, input->selector_count * sizeof(CssPageSelector));
    rule->declaration_count = input->declaration_count;
    rule->declarations = page_declarations_snapshot(input->declarations, input->declaration_count, pool);
    rule->area_count = input->area_count;
    rule->areas = input->area_count ? (CssPageAreaRule*)pool_calloc(pool, input->area_count * sizeof(CssPageAreaRule)) : nullptr;
    if (!rule->selectors || (rule->declaration_count && !rule->declarations) || (rule->area_count && !rule->areas))
        return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, source, "page rule snapshot failed");
    for (size_t i = 0; i < rule->selector_count; i++) {
        rule->selectors[i] = input->selectors[i];
        const char* name = input->selectors[i].name;
        if (name && !(rule->selectors[i].name = pool_strdup(pool, name)))
            return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, source, "page selector snapshot failed");
    }
    rule->name = rule->selector_count == 1 ? rule->selectors[0].name : nullptr;
    for (size_t i = 0; i < rule->area_count; i++) {
        rule->areas[i] = input->areas[i];
        rule->areas[i].declarations = page_declarations_snapshot(input->areas[i].declarations, input->areas[i].declaration_count, pool);
        if (rule->areas[i].declaration_count && !rule->areas[i].declarations)
            return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, source, "page area snapshot failed");
    }
    if (program->last_rule) program->last_rule->next = rule;
    else program->rules = rule;
    program->last_rule = rule; program->rule_count++;
    return true;
}

static bool page_css_rule_compile(RadiantPageDocument* program, CssRule* rule,
        CssEngine* engine, Pool* pool, size_t depth) {
    if (!rule) return true;
    if (depth > 512) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, nullptr, "page rule nesting limit exceeded");
    if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS || rule->type == CSS_RULE_LAYER) {
        bool active = rule->type == CSS_RULE_LAYER || (rule->type == CSS_RULE_MEDIA
            ? css_evaluate_media_query(engine, rule->data.conditional_rule.condition)
            : css_evaluate_supports_condition(engine, rule->data.conditional_rule.condition));
        if (active) for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++)
            if (!page_css_rule_compile(program, rule->data.conditional_rule.rules[i], engine, pool, depth + 1)) return false;
        return true;
    }
    return rule->type != CSS_RULE_PAGE || !rule->page ||
        page_rule_append(program, rule->page, rule->origin, RADIANT_PAGE_CSS, nullptr, pool);
}

static bool page_css_sheet_compile(RadiantPageDocument* program, CssStylesheet* sheet,
        CssEngine* engine, Pool* pool, size_t depth) {
    if (!sheet || sheet->disabled) return true;
    if (depth > 512) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, nullptr, "page import nesting limit exceeded");
    if (sheet->media && *sheet->media && !css_evaluate_media_query(engine, sheet->media)) return true;
    for (size_t i = 0; i < sheet->imported_count; i++)
        if (!page_css_sheet_compile(program, sheet->imported_stylesheets[i], engine, pool, depth + 1)) return false;
    for (size_t i = 0; i < sheet->rule_count; i++)
        if (!page_css_rule_compile(program, sheet->rules[i], engine, pool, 0)) return false;
    return true;
}

template<typename T, size_t N> static bool page_keyword(const char* text, const char* const (&names)[N], T* result) {
    if (!text) return true;
    for (size_t i = 0; i < N; i++) if (!strcmp(text, names[i])) { *result = static_cast<T>(i); return true; }
    return false;
}

static bool page_native_columns_compile(RadiantPageDocument* program, RadiantPageRegion* region, Pool* pool) {
    static const char* names[] = {"inline-start", "block-start", "inline-size", "block-size"};
    static const char* properties[] = {"left", "top", "width", "height"};
    RadiantPageColumn** tail = &region->columns;
    DomElement* source = region->source.address->as_element();
    for (DomNode* node = source->first_child; node; node = node->next_sibling) {
        if (node->is_text()) {
            if (!radiant_page_whitespace(node->as_text()))
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "page region cannot contain text");
            continue;
        }
        if (!node->is_element()) continue;
        DomElement* child = node->as_element();
        if (region->role != RADIANT_REGION_BODY || !radiant_page_element(child, "column"))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "body region accepts only empty column controls");
        for (DomNode* content = child->first_child; content; content = content->next_sibling)
            if (content->is_element() || (content->is_text() && !radiant_page_whitespace(content->as_text())))
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "native column control must be empty");
        if (region->column_count == UINT32_MAX)
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "native column count exceeds its domain");
        auto* column = (RadiantPageColumn*)pool_calloc(pool, sizeof(RadiantPageColumn));
        if (!column) return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, child, "native column allocation failed");
        column->source = dom_node_ref(child);
        for (size_t i = 0; i < 4; i++) {
            const char* text = child->get_attribute(names[i]);
            if (!text) text = i == 2 ? nullptr : i == 3 ? "100%" : "0";
            if (!text) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "native column requires inline-size");
            CssDeclaration* parsed = css_parse_property_value_declaration(properties[i], strlen(properties[i]), text, strlen(text), pool);
            if (!parsed || !parsed->valid || !parsed->value)
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "native column geometry requires a CSS length or percentage");
            column->geometry[i] = parsed->value;
        }
        *tail = column; tail = &column->next; region->column_count++;
    }
    return true;
}

static bool page_native_master_compile(RadiantPageDocument* program, DomElement* source, Pool* pool) {
    const char* name = source->get_attribute("name");
    if (!name || !*name) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "page master requires a name");
    for (RadiantPageRule* rule = program->rules; rule; rule = rule->next)
        if (rule->kind == RADIANT_PAGE_NATIVE && rule->name && strcmp(rule->name, name) == 0)
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "duplicate native page master name");
    CssPageRule input = {};
    // native master references are exact names; they do not carry CSS selector syntax.
    CssPageSelector selector = {name, 0, 1, 0, 0};
    input.selectors = &selector; input.selector_count = 1;
    const char* style = dom_element_get_inline_style(source);
    if (style && *style) input.declarations = css_parse_declaration_list_text(style, strlen(style), pool, &input.declaration_count);
    RadiantPageRegion* body = nullptr;
    RadiantPageRegion* edges[RADIANT_REGION_EDGE_COUNT] = {};
    static const char* roles[] = {"before", "after", "start", "end", "body"};
    for (DomElement* child = source->first_child_element(); child; child = child->next_sibling_element()) {
        if (!radiant_page_element(child, "region"))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "page master child must be a region");
        const char* role = child->get_attribute("role");
        RadiantPageRegionRole kind = RADIANT_REGION_BODY;
        if (role && page_keyword(role, roles, &kind)) {
            RadiantPageRegion*& slot = kind == RADIANT_REGION_BODY ? body : edges[kind];
            if (slot) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child,
                kind == RADIANT_REGION_BODY ? "page master has more than one body region" : "page master has duplicate logical edge regions");
            const char* region_name = child->get_attribute("name");
            if (!region_name || !*region_name) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "logical region requires a name");
            slot = (RadiantPageRegion*)pool_calloc(pool, sizeof(RadiantPageRegion));
            if (!slot || !(slot->name = pool_strdup(pool, region_name)))
                return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, child, "logical region allocation failed");
            slot->source = dom_node_ref(child); slot->role = kind;
            static const char* aligns[] = {"before", "center", "after"};
            if (!page_keyword(child->get_attribute("display-align"), aligns, &slot->align))
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "invalid logical region display alignment");
            const char* box_policy = child->get_attribute("box-policy");
            if (box_policy && strcmp(box_policy, "zero-border-padding"))
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "unknown region box policy");
            slot->zero_box = box_policy != nullptr;
            style = dom_element_get_inline_style(child);
            if (style && *style) slot->declarations = css_parse_declaration_list_text(style, strlen(style), pool, &slot->declaration_count);
            if (!page_native_columns_compile(program, slot, pool)) return false;
            if (kind != RADIANT_REGION_BODY) {
                if (const char* extent = child->get_attribute("extent")) {
                    CssDeclaration* parsed = css_parse_property_value_declaration("height", 6, extent, strlen(extent), pool);
                    const CssValue* value = parsed ? parsed->value : nullptr;
                    if (!value || (value->type != CSS_VALUE_TYPE_LENGTH && !(value->type == CSS_VALUE_TYPE_NUMBER && !value->data.number.value)))
                        return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "logical region extent requires a length");
                    slot->extent = value;
                }
                const char* precedence = child->get_attribute("precedence");
                if (precedence && (kind >= RADIANT_REGION_START || (strcmp(precedence, "true") && strcmp(precedence, "false"))))
                    return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "precedence requires true or false on before/after regions");
                slot->precedence = precedence && !strcmp(precedence, "true");
            }
        } else input.area_count++;
    }
    for (size_t i = 0; i < RADIANT_REGION_EDGE_COUNT; i++) if (edges[i]) {
        if (!strcmp(edges[i]->name, body ? body->name : "body"))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, edges[i]->source.address->as_element(), "duplicate page region name");
        for (size_t j = 0; j < i; j++) if (edges[j] && !strcmp(edges[j]->name, edges[i]->name))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, edges[i]->source.address->as_element(), "duplicate page region name");
    }
    input.areas = input.area_count ? (CssPageAreaRule*)pool_calloc(pool, input.area_count * sizeof(CssPageAreaRule)) : nullptr;
    if (input.area_count && !input.areas) return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, source, "native page region allocation failed");
    size_t index = 0;
    for (DomElement* child = source->first_child_element(); child; child = child->next_sibling_element()) {
        const char* role = child->get_attribute("role");
        RadiantPageRegionRole kind = RADIANT_REGION_BODY;
        if (role && page_keyword(role, roles, &kind)) continue;
        CssPageAreaRule& area = input.areas[index++];
        const char* box = child->get_attribute("box");
        bool found = box && strcmp(box, "footnote") == 0;
        area.kind = found ? CSS_PAGE_AREA_FOOTNOTE : CSS_PAGE_AREA_MARGIN;
        for (uint8_t i = 0; box && !found && i < CSS_PAGE_MARGIN_BOX_COUNT; i++) {
            if (strcmp(box, css_page_margin_box_name((CssPageMarginBox)i)) == 0) {
                area.box = (CssPageMarginBox)i; found = true;
            }
        }
        if (!found) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "unknown CSS margin-box region");
        style = dom_element_get_inline_style(child);
        if (style && *style) area.declarations = css_parse_declaration_list_text(style, strlen(style), pool, &area.declaration_count);
    }
    if (!page_rule_append(program, &input, CSS_ORIGIN_AUTHOR, RADIANT_PAGE_NATIVE, source, pool)) return false;
    program->last_rule->body = body;
    for (size_t i = 0; i < RADIANT_REGION_EDGE_COUNT; i++) program->last_rule->edges[i] = edges[i];
    return true;
}

static bool page_native_compile(RadiantPageDocument* program, DomElement* element, Pool* pool, size_t depth) {
    if (!element) return true;
    if (depth > 512) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, element, "page control nesting limit exceeded");
    if (radiant_page_element(element, "page-master")) return page_native_master_compile(program, element, pool);
    for (DomElement* child = element->first_child_element(); child; child = child->next_sibling_element())
        if (!page_native_compile(program, child, pool, depth + 1)) return false;
    return true;
}

static const RadiantPageRule* page_named_master(const RadiantPageDocument* program, const char* name) {
    if (name) for (const RadiantPageRule* rule = program->rules; rule; rule = rule->next)
        if (rule->kind == RADIANT_PAGE_NATIVE && rule->name && !strcmp(rule->name, name)) return rule;
    return nullptr;
}

static const char* page_inherited_attribute(DomElement* source, const char* name) {
    const char* value = source->get_attribute(name);
    while (value && !strcmp(value, "inherit")) {
        source = source->parent_element();
        value = source ? source->get_attribute(name) : nullptr;
    }
    return value;
}

bool radiant_page_rounded_count(const char* text, uint32_t minimum, uint32_t* result) {
    double value = 0.0; const char* end = nullptr;
    if (!text || !result || !str_to_double(text, strlen(text), &value, &end) || *end || !isfinite(value)) return false;
    return radiant_page_rounded_count(value, minimum, result);
}

bool radiant_page_rounded_count(double value, uint32_t minimum, uint32_t* result) {
    if (!result || !isfinite(value)) return false;
    value = fmax((double)minimum, floor(value + 0.5));
    if (value > INT32_MAX) return false;
    *result = static_cast<uint32_t>(value); return true;
}


static bool page_format_alphanumeric(uint32_t code) {
    utf8proc_category_t category = utf8proc_category(static_cast<utf8proc_int32_t>(code));
    return (category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_LO) ||
        (category >= UTF8PROC_CATEGORY_ND && category <= UTF8PROC_CATEGORY_NO);
}

static bool page_folio_format_compile(RadiantPageDocument* program, DomElement* source,
        Pool* pool, RadiantFolioFormat* format) {
    *format = {CSS_VALUE_DECIMAL, '0', 1, 0, "", "", ""};
    const char* text = page_inherited_attribute(source, "format");
    if (!text) text = "1";
    size_t length = strlen(text), first = length, end = length, trailing = 0, digits = 0;
    uint32_t first_code = 0, previous_code = 0; bool decimal = true, in_first = false;
    if (length > UINT32_MAX) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "folio format is too long");
    for (size_t offset = 0; offset < length;) {
        uint32_t code = 0; int bytes = str_utf8_decode(text + offset, length - offset, &code);
        if (bytes <= 0) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "folio format requires valid UTF-8");
        bool alpha = page_format_alphanumeric(code);
        if (alpha) {
            trailing = offset + static_cast<size_t>(bytes);
            if (first == length) { first = offset; in_first = true; }
            if (in_first) {
                // decimal tokens consist of zero digits followed by a one in the same Unicode digit set.
                if (!digits) first_code = code;
                else if (previous_code != first_code) decimal = false;
                previous_code = code; digits++;
            }
        } else if (in_first) { end = offset; in_first = false; }
        offset += static_cast<size_t>(bytes);
    }
    if (first != length) {
        size_t token_length = end - first;
        if (token_length == 1) {
            char token = text[first];
            format->style = token == 'a' ? CSS_VALUE_LOWER_ALPHA : token == 'A' ? CSS_VALUE_UPPER_ALPHA :
                token == 'i' ? CSS_VALUE_LOWER_ROMAN : token == 'I' ? CSS_VALUE_UPPER_ROMAN : CSS_VALUE_DECIMAL;
        }
        if (utf8proc_category(static_cast<utf8proc_int32_t>(previous_code)) != UTF8PROC_CATEGORY_ND) decimal = false;
        uint32_t start = previous_code;
        while (start && utf8proc_category(static_cast<utf8proc_int32_t>(start - 1)) == UTF8PROC_CATEGORY_ND) start--;
        decimal &= (previous_code - start) % 10 == 1 && (digits == 1 || first_code == previous_code - 1);
        if (decimal) { format->zero_digit = previous_code - 1; format->minimum_digits = static_cast<uint32_t>(digits); }
    }
    // unsupported numbering tokens use decimal, as required by XSLT 1.0 §7.7.1.
    format->prefix = pool_dup_n(pool, text, first);
    format->suffix = pool_strdup(pool, text + trailing);
    const char* letter = page_inherited_attribute(source, "letter-value");
    if (letter && strcmp(letter, "auto") && strcmp(letter, "alphabetic") && strcmp(letter, "traditional"))
        return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "invalid folio letter value");
    const char* separator = page_inherited_attribute(source, "grouping-separator");
    const char* group = page_inherited_attribute(source, "grouping-size");
    if (separator && group) {
        uint32_t code = 0; int bytes = str_utf8_decode(separator, strlen(separator), &code);
        int32_t size = 0;
        if (bytes <= 0 || static_cast<size_t>(bytes) != strlen(separator) || !flow_integer(group, &size) || size <= 0)
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "folio grouping requires one character and a positive integer size");
        format->grouping_separator = pool_strdup(pool, separator); format->grouping_size = static_cast<uint32_t>(size);
    }
    return (format->prefix && format->suffix && format->grouping_separator) ||
        page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, source, "folio format allocation failed");
}

bool radiant_folio_append(const RadiantFolioFormat* format, uint32_t folio, StrBuf* text) {
    if (!text || folio > INT32_MAX) return false;
    RadiantFolioFormat initial = {CSS_VALUE_DECIMAL, '0', 1, 0, "", "", ""};
    if (!format) format = &initial;
    strbuf_append_str(text, format->prefix);
    if (format->style == CSS_VALUE_DECIMAL) {
        char number[16]; size_t count = 0;
        do { number[count++] = static_cast<char>('0' + folio % 10); folio /= 10; } while (folio);
        size_t total = count > format->minimum_digits ? count : format->minimum_digits;
        for (size_t i = total; i; i--) {
            char encoded[4]; uint32_t digit = i > count ? 0 : static_cast<uint32_t>(number[i - 1] - '0');
            size_t bytes = str_utf8_encode(format->zero_digit + digit, encoded, sizeof(encoded));
            if (!bytes) return false;
            strbuf_append_str_n(text, encoded, bytes);
            if (format->grouping_size && i > 1 && (i - 1) % format->grouping_size == 0)
                strbuf_append_str(text, format->grouping_separator);
        }
    } else if (!counter_value_append(static_cast<int>(folio), format->style, text)) return false; // INT_CAST_OK: bounded folio counter.
    strbuf_append_str(text, format->suffix);
    return true;
}

bool radiant_page_query_resolve(ViewTree* tree, ViewCssStyle* style) {
    bool current = radiant_page_element(style->source, "folio");
    if (!current && !radiant_page_element(style->source, "folio-ref")) return true;
    RadiantPageQuery* query = (RadiantPageQuery*)pool_calloc(tree->model->css->pool, sizeof(RadiantPageQuery));
    if (!query) return false;
    style->page_query = lam::up(query);
    auto fail = [&](const char* reason) { query->status = VIEW_MODEL_INVALID_ARGUMENT; query->reason = reason; return true; };
    if (style->source->first_child) return fail("page query must be empty");
    if (current) query->edge = RADIANT_QUERY_CURRENT;
    else {
        query->edge = RADIANT_QUERY_FIRST;
        query->target = style->source->get_attribute("ref-id");
        if (!query->target || !*query->target) return fail("folio reference requires a target ID");
        static const char* edges[] = {"current", "first", "last"};
        if (!page_keyword(style->source->get_attribute("edge"), edges, &query->edge) || query->edge == RADIANT_QUERY_CURRENT)
            return fail("folio reference edge requires first or last");
    }
    static const char* areas[] = {"all", "normal", "non-blank"};
    return page_keyword(style->source->get_attribute("area"), areas, &query->area) || fail("invalid folio reference area strategy");
}

static bool page_master_program_compile(RadiantPageDocument* program, DomElement* source, Pool* pool) {
    const char* name = source->get_attribute("name");
    if (!name || !*name) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "sequence master requires a name");
    if (page_named_master(program, name)) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "duplicate native page master name");
    for (RadiantSequenceMaster* master = program->masters; master; master = master->next)
        if (!strcmp(master->name, name)) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "duplicate native page master name");
    RadiantSequenceMaster* master = (RadiantSequenceMaster*)pool_calloc(pool, sizeof(RadiantSequenceMaster));
    if (!master || !(master->name = pool_strdup(pool, name)))
        return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, source, "sequence master allocation failed");
    master->source = dom_node_ref(source); master->next = program->masters; program->masters = master;
    RadiantMasterRun* last_run = nullptr;
    for (DomElement* run_source = source->first_child_element(); run_source; run_source = run_source->next_sibling_element()) {
        if (!radiant_page_element(run_source, "master-run"))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, run_source, "sequence master child must be a master run");
        RadiantMasterRun* run = (RadiantMasterRun*)pool_calloc(pool, sizeof(RadiantMasterRun));
        if (!run) return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, run_source, "master run allocation failed");
        run->source = dom_node_ref(run_source); run->maximum_repeats = UINT32_MAX;
        const char* repeats = page_inherited_attribute(run_source, "maximum-repeats");
        if (repeats && strcmp(repeats, "no-limit") && !radiant_page_rounded_count(repeats, 0, &run->maximum_repeats))
            return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, run_source, "master repeat limit requires a number or no-limit");
        if (last_run) last_run->next = run; else master->runs = run;
        last_run = run;
        RadiantMasterRule* last_rule = nullptr;
        for (DomElement* choice = run_source->first_child_element(); choice; choice = choice->next_sibling_element()) {
            if (!radiant_page_element(choice, "master-rule"))
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, choice, "master run child must be a master rule");
            const RadiantPageRule* target = page_named_master(program, choice->get_attribute("master-reference"));
            if (!target) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, choice, "master rule references an unknown page master");
            RadiantMasterRule* rule = (RadiantMasterRule*)pool_calloc(pool, sizeof(RadiantMasterRule));
            if (!rule) return page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, choice, "master rule allocation failed");
            rule->source = dom_node_ref(choice); rule->master = target;
            static const char* positions[] = {"any", "first", "last", "only", "rest"};
            static const char* parities[] = {"any", "odd", "even"};
            static const char* blanks[] = {"any", "blank", "not-blank"};
            if (!page_keyword(page_inherited_attribute(choice, "page-position"), positions, &rule->position) ||
                !page_keyword(page_inherited_attribute(choice, "odd-or-even"), parities, &rule->parity) ||
                !page_keyword(page_inherited_attribute(choice, "blank-or-not-blank"), blanks, &rule->blank))
                return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, choice, "invalid conditional master predicate");
            master->terminal |= rule->position == RADIANT_MASTER_LAST || rule->position == RADIANT_MASTER_ONLY || rule->position == RADIANT_MASTER_REST;
            if (last_rule) last_rule->next = rule; else run->rules = rule;
            last_rule = rule;
        }
        if (!run->rules) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, run_source, "master run requires alternatives");
    }
    return master->runs || page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "sequence master requires runs");
}

static bool page_master_programs_compile(RadiantPageDocument* program, DomElement* source, Pool* pool, size_t depth) {
    if (depth > 512) return page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, source, "page control nesting limit exceeded");
    if (radiant_page_element(source, "sequence-master")) return page_master_program_compile(program, source, pool);
    if (radiant_page_control_hidden(source) && !radiant_page_element(source, "master-set")) return true;
    for (DomElement* child = source->first_child_element(); child; child = child->next_sibling_element())
        if (!page_master_programs_compile(program, child, pool, depth + 1)) return false;
    return true;
}

const RadiantPageRule* radiant_page_master_select(const RadiantPageSequence* sequence,
        uint32_t ordinal, uint32_t folio, bool blank, bool terminal) {
    if (!sequence || !sequence->master_program || !ordinal) return nullptr;
    uint32_t offset = ordinal - 1;
    for (const RadiantMasterRun* run = sequence->master_program->runs; run; run = run->next) {
        if (run->maximum_repeats != UINT32_MAX && offset >= run->maximum_repeats) { offset -= run->maximum_repeats; continue; }
        for (const RadiantMasterRule* rule = run->rules; rule; rule = rule->next) {
            bool first = ordinal == 1;
            bool position = rule->position == RADIANT_MASTER_ANY || (rule->position == RADIANT_MASTER_FIRST && first) ||
                (rule->position == RADIANT_MASTER_LAST && terminal) || (rule->position == RADIANT_MASTER_ONLY && first && terminal) ||
                (rule->position == RADIANT_MASTER_REST && !first && !terminal);
            bool parity = rule->parity == RADIANT_PARITY_ANY || (rule->parity == RADIANT_PARITY_ODD ? folio % 2 : !(folio % 2));
            bool blanks = rule->blank == RADIANT_BLANK_ANY || (rule->blank == RADIANT_BLANK_ONLY ? blank : !blank);
            if (position && parity && blanks) return rule->master;
        }
        return nullptr;
    }
    return nullptr;
}

RadiantPageDocument* radiant_page_document_compile(ViewTree* tree, CssEngine* engine, Pool* pool) {
    if (!tree || !tree->model || !engine || !pool) return nullptr;
    RadiantPageDocument* program = (RadiantPageDocument*)pool_calloc(pool, sizeof(RadiantPageDocument));
    if (!program) return nullptr;
    DomDocument* doc = tree->model->document;
    if (!page_fixed_scan(program, doc->root, pool, 0)) return program;
    for (int i = 0; i < doc->stylesheet_count; i++)
        if (!page_css_sheet_compile(program, doc->stylesheets.get()[i], engine, pool, 0)) return program;
    if (!page_native_compile(program, doc->root, pool, 0)) return program;
    if (!page_master_programs_compile(program, doc->root, pool, 0)) return program;
    // validate forward master references after every master has been collected.
    DomElement* node = doc->root;
    RadiantPageSequence* last_sequence = nullptr;
    while (node) {
        if (radiant_page_element(node, "page-sequence")) {
            if (program->fixed_count) {
                page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, node, "fixed pages cannot mix with flow page sequences"); return program;
            }
            const char* name = node->get_attribute("master-reference");
            bool found = false;
            const RadiantPageRegion* body = nullptr;
            const RadiantPageRule* simple_master = nullptr;
            const RadiantSequenceMaster* master_program = nullptr;
            for (RadiantPageRule* rule = program->rules; rule; rule = rule->next) {
                if (name && rule->name && strcmp(rule->name, name) == 0) {
                    found = true;
                    simple_master = rule;
                    if (rule->body) body = rule->body;
                }
            }
            for (const RadiantSequenceMaster* master = program->masters; master; master = master->next)
                if (name && !strcmp(master->name, name)) { found = true; master_program = master; }
            if (!found) { page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, node, "page sequence references an unknown master"); return program; }
            RadiantPageSequence* sequence = (RadiantPageSequence*)pool_calloc(pool, sizeof(RadiantPageSequence));
            if (!sequence || !(sequence->master_reference = pool_strdup(pool, name))) {
                page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, node, "page sequence allocation failed"); return program;
            }
            sequence->source = dom_node_ref(node);
            sequence->master_program = master_program;
            if (!page_folio_format_compile(program, node, pool, &sequence->format)) return program;
            const char* initial = page_inherited_attribute(node, "initial-page-number");
            static const char* initials[] = {"auto", "auto-odd", "auto-even"};
            if (!page_keyword(initial, initials, &sequence->initial)) {
                sequence->initial = RADIANT_FOLIO_NUMBER;
                if (!radiant_page_rounded_count(initial, 1, &sequence->initial_folio)) {
                    page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, node, "initial page number requires auto, auto-odd, auto-even or a number"); return program;
                }
            }
            static const char* endings[] = {"auto", "even", "odd", "end-on-even", "end-on-odd", "no-force"};
            if (!page_keyword(page_inherited_attribute(node, "force-page-count"), endings, &sequence->end)) {
                page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, node, "invalid sequence end-page constraint"); return program;
            }
            if (last_sequence) last_sequence->next = sequence;
            else program->sequences = sequence;
            last_sequence = sequence; program->sequence_count++;
            RadiantPageFlowBinding* last_flow = nullptr;
            RadiantPageFlowBinding* last_static = nullptr;
            for (DomElement* child = node->first_child_element(); child; child = child->next_sibling_element()) {
                bool furniture = radiant_page_element(child, "static-content");
                if (!furniture && !radiant_page_element(child, "flow")) continue;
                const char* binding = child->get_attribute("region-name");
                auto matches = [&](const RadiantPageRule* master) {
                    if (!binding || !master) return false;
                    if (!furniture) return !strcmp(binding, master->body ? master->body->name : "body");
                    for (const RadiantPageRegion* edge : master->edges) if (edge && !strcmp(binding, edge->name)) return true;
                    return false;
                };
                bool region_found = matches(simple_master);
                if (master_program) for (const RadiantMasterRun* run = master_program->runs; run; run = run->next)
                    for (const RadiantMasterRule* rule = run->rules; rule; rule = rule->next)
                        region_found |= matches(rule->master);
                if (!region_found) {
                    page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child,
                        furniture ? "static content references an unknown edge region" : "flow references an unknown body region"); return program;
                }
                RadiantPageFlowBinding*& head = furniture ? sequence->static_content : sequence->flows;
                for (RadiantPageFlowBinding* previous = head; previous; previous = previous->next)
                    if (!strcmp(previous->region_name, binding)) {
                        page_document_fail(program, VIEW_MODEL_INVALID_ARGUMENT, child, "duplicate sequence region binding"); return program;
                    }
                RadiantPageFlowBinding* flow = (RadiantPageFlowBinding*)pool_calloc(pool, sizeof(RadiantPageFlowBinding));
                if (!flow || !(flow->region_name = pool_strdup(pool, binding))) {
                    page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, child, "flow binding allocation failed"); return program;
                }
                flow->source = dom_node_ref(child);
                RadiantPageFlowBinding*& tail = furniture ? last_static : last_flow;
                if (tail) tail->next = flow;
                else head = flow;
                tail = flow;
            }
            if (!sequence->flows) {
                sequence->flows = (RadiantPageFlowBinding*)pool_calloc(pool, sizeof(RadiantPageFlowBinding));
                if (!sequence->flows || !(sequence->flows->region_name = pool_strdup(pool, body ? body->name : "body"))) {
                    page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, node, "implicit sequence flow allocation failed"); return program;
                }
                sequence->flows->source = sequence->source;
            }
        }
        if (node->first_child_element()) { node = node->first_child_element(); continue; }
        while (node != doc->root.get() && !node->next_sibling_element()) node = node->parent_element();
        if (node == doc->root.get()) break;
        node = node->next_sibling_element();
    }
    if (!program->sequences && !program->fixed_count) {
        program->sequences = (RadiantPageSequence*)pool_calloc(pool, sizeof(RadiantPageSequence));
        RadiantPageFlowBinding* flow = (RadiantPageFlowBinding*)pool_calloc(pool, sizeof(RadiantPageFlowBinding));
        if (!program->sequences || !flow) {
            page_document_fail(program, VIEW_MODEL_OUT_OF_MEMORY, doc->root, "implicit document sequence allocation failed"); return program;
        }
        program->sequences->source = dom_node_ref(doc->root); program->sequences->implicit = true;
        program->sequences->flows = flow; program->sequence_count = 1;
        flow->source = program->sequences->source; flow->region_name = "body";
    }
    return program;
}
