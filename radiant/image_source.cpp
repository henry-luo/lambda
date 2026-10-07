#include "layout.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lib/str.h"
#include "../lib/strbuf.h"
#include <math.h>

static bool image_source_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool layout_image_type_supported(const char* type) {
    if (!type) return true;
    while (image_source_space(*type)) type++;
    const char* end = type;
    while (*end && *end != ';') end++;
    while (end > type && image_source_space(end[-1])) end--;
    const char* supported[] = {"image/png", "image/jpeg", "image/gif", "image/webp", "image/svg+xml"};
    for (const char* mime : supported)
        if (strlen(mime) == (size_t)(end - type) && strncasecmp(type, mime, end - type) == 0) return true;
    return false;
}

struct ImageSourceCandidate {
    const char* url;
    size_t length;
    double density;
};

struct ImageSourceChoice {
    ImageSourceCandidate selected;
    double desired;
    bool has_width, has_one;
};

static void image_source_consider(ImageSourceChoice* choice, ImageSourceCandidate candidate) {
    const ImageSourceCandidate& old = choice->selected;
    // pick the smallest sufficient density, or the largest available; ties preserve source order.
    if (!old.url || (old.density < choice->desired
            ? candidate.density > old.density
            : candidate.density >= choice->desired && candidate.density < old.density))
        choice->selected = candidate;
}

static bool image_source_descriptor(const char* text, const char* end, bool integer, double* value) {
    const char* cursor = text;
    if (!integer && cursor < end && *cursor == '-') cursor++;
    bool digits = false;
    while (cursor < end && *cursor >= '0' && *cursor <= '9') { cursor++; digits = true; }
    if (!integer && cursor < end && *cursor == '.') {
        cursor++;
        const char* fraction = cursor;
        while (cursor < end && *cursor >= '0' && *cursor <= '9') cursor++;
        if (cursor == fraction) return false;
        digits = true;
    }
    if (!digits) return false;
    if (!integer && cursor < end && (*cursor == 'e' || *cursor == 'E')) {
        cursor++;
        if (cursor < end && (*cursor == '+' || *cursor == '-')) cursor++;
        const char* exponent = cursor;
        while (cursor < end && *cursor >= '0' && *cursor <= '9') cursor++;
        if (cursor == exponent) return false;
    }
    return cursor == end && str_to_double(text, end - text, value, nullptr) &&
        isfinite(*value) && *value >= 0.0 && (!integer || *value > 0.0);
}

static ImageSourceChoice image_source_parse(const char* srcset, double size, double desired) {
    ImageSourceChoice choice = {}; choice.desired = desired;
    const char* cursor = srcset ? srcset : "";
    while (*cursor) {
        while (image_source_space(*cursor) || *cursor == ',') cursor++;
        if (!*cursor) break;
        const char* url = cursor;
        // the HTML URL token ends at whitespace, retaining commas inside data URLs.
        while (*cursor && !image_source_space(*cursor)) cursor++;
        const char* url_end = cursor;
        bool trailing_comma = url_end > url && url_end[-1] == ',';
        while (url_end > url && url_end[-1] == ',') url_end--;
        bool invalid = url_end == url, width_set = false, density_set = false, height_set = false;
        double width = 0.0, density = 1.0;
        if (!trailing_comma) while (*cursor) {
            while (image_source_space(*cursor)) cursor++;
            if (!*cursor || *cursor == ',') { if (*cursor) cursor++; break; }
            const char* token = cursor;
            size_t parens = 0;
            while (*cursor && (parens || (!image_source_space(*cursor) && *cursor != ','))) {
                if (*cursor == '(') parens++;
                else if (*cursor == ')' && parens) parens--;
                cursor++;
            }
            char suffix = cursor[-1]; double value = 0.0;
            bool integer = suffix == 'w' || suffix == 'h';
            if (cursor - token < 2 || !image_source_descriptor(token, cursor - 1, integer, &value)) invalid = true;
            else if (suffix == 'w' && !width_set && !density_set) { width = value; width_set = true; }
            else if (suffix == 'h' && !height_set && !density_set) height_set = true;
            else if (suffix == 'x' && !width_set && !density_set && !height_set) { density = value; density_set = true; }
            else invalid = true;
        }
        if (invalid || (height_set && !width_set)) continue;
        choice.has_width |= width_set;
        choice.has_one |= !width_set && density == 1.0;
        if (width_set) density = size > 0.0 ? width / size : INFINITY;
        image_source_consider(&choice, {url, (size_t)(url_end - url), density});
    }
    return choice;
}

static double image_source_size(CssEngine* engine, const char* sizes) {
    if (!engine) return 0.0;
    if (!sizes || !*sizes) return engine->context.viewport_width;
    Pool* pool = pool_create();
    if (!pool) return NAN;
    LayoutContext layout = {};
    layout.width = (float)engine->context.viewport_width;
    layout.height = (float)engine->context.viewport_height;
    // source-size em/rem units use the initial font, independently of the image's style.
    layout.root_font_size = layout.font.current_font_size = 16.0f;
    double result = engine->context.viewport_width;
    const char* cursor = sizes;
    while (*cursor) {
        const char* first = cursor;
        size_t depth = 0;
        while (*cursor && (*cursor != ',' || depth)) {
            if (*cursor == '(') depth++;
            else if (*cursor == ')' && depth) depth--;
            cursor++;
        }
        const char* end = cursor;
        if (*cursor) cursor++;
        while (end > first && image_source_space(end[-1])) end--;
        // the final top-level component is the length; parentheses may contain spaces/commas.
        const char* length = end; depth = 0;
        while (length > first) {
            char c = length[-1];
            if (c == ')') depth++;
            else if (c == '(' && depth) depth--;
            if (!depth && image_source_space(c)) break;
            length--;
        }
        if (length == end) continue;
        StrBuf* declaration = strbuf_new();
        if (!declaration) { result = NAN; break; }
        strbuf_append_str(declaration, "width:"); strbuf_append_str_n(declaration, length, end - length);
        CssDeclaration* parsed = css_parse_declaration_text(declaration->str, declaration->length, pool);
        strbuf_free(declaration);
        if (!parsed || !parsed->value) continue;
        CssMathType type = css_math_value_type(parsed->value);
        if (type != CSS_MATH_LENGTH && !(type == CSS_MATH_NUMBER &&
                parsed->value->type == CSS_VALUE_TYPE_NUMBER && parsed->value->data.number.value == 0.0)) continue;
        float pixels = resolve_length_value(&layout, CSS_PROPERTY_WIDTH, parsed->value);
        if (!isfinite(pixels) || pixels < 0.0f) continue;
        while (length > first && image_source_space(length[-1])) length--;
        if (length > first) {
            char* media = (char*)pool_alloc(pool, length - first + 1);
            if (!media) { result = NAN; break; }
            memcpy(media, first, length - first); media[length - first] = '\0';
            if (!css_evaluate_media_query(engine, media)) continue;
        }
        result = pixels; break;
    }
    pool_destroy(pool);
    return result;
}

char* layout_resolve_replaced_image_source(DomElement* element, CssEngine* engine,
        float* density, DomElement** dimension_source, bool (*supports_type)(const char*)) {
    if (density) *density = 1.0f;
    if (dimension_source) *dimension_source = element;
    if (!element || element->tag() != MARKUP_NAME_IMG) return nullptr;
    if (!engine && element->doc) engine = (CssEngine*)element->doc->services.cached_css_engine;
    double desired = engine && engine->context.device_pixel_ratio > 0.0
        ? engine->context.device_pixel_ratio : 1.0;
    if (!supports_type) supports_type = layout_image_type_supported;
    DomElement* parent = element->parent_element();
    if (parent && parent->tag() == MARKUP_NAME_PICTURE) {
        for (DomNode* node = parent->first_child; node && node != element; node = node->next_sibling) {
            if (!node->is_element() || node->as_element()->tag() != MARKUP_NAME_SOURCE) continue;
            DomElement* source = node->as_element();
            const char* media = source->get_attribute("media");
            const char* type = source->get_attribute("type");
            if ((media && *media && (!engine || !css_evaluate_media_query(engine, media))) ||
                (type && !supports_type(type))) continue;
            ImageSourceChoice choice = image_source_parse(source->get_attribute("srcset"),
                image_source_size(engine, source->get_attribute("sizes")), desired);
            if (!choice.selected.url) continue;
            if (density) *density = (float)choice.selected.density;
            if (dimension_source && (source->get_attribute("width") || source->get_attribute("height")))
                *dimension_source = source;
            return mem_dup_n(choice.selected.url, choice.selected.length, MEM_CAT_LAYOUT);
        }
    }
    ImageSourceChoice choice = image_source_parse(element->get_attribute("srcset"),
        image_source_size(engine, element->get_attribute("sizes")), desired);
    const char* src = element->get_attribute("src");
    if (src && *src && !choice.has_width && !choice.has_one)
        image_source_consider(&choice, {src, strlen(src), 1.0});
    if (!choice.selected.url) return nullptr;
    if (density) *density = (float)choice.selected.density;
    return mem_dup_n(choice.selected.url, choice.selected.length, MEM_CAT_LAYOUT);
}
