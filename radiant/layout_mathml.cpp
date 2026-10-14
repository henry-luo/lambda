// MathML package layout and intrinsic measurement preserve the semantic DOM (D7.2.8).
#include "layout.hpp"
#include "../lib/tagged.hpp"
#include <string.h>

bool layout_is_mathml_root(DomElement* element) {
    if (!element || !element->tag_name || strcmp(element->tag_name, "math") != 0) return false;
    const char* uri = dom_element_namespace_uri(element);
    return uri && strcmp(uri, "http://www.w3.org/1998/Math/MathML") == 0;
}

static bool mathml_package_layout(const CustomLayoutContext* context, CustomLayoutResult* result) {
    if (!context || !context->parent || !context->lycon || !result) return false;
    DomElement* element = context->parent->as_element();
    uint32_t generation = element->doc && element->doc->view_tree
        ? element->doc->view_tree->measurement_cache_generation : 0;
    float font_size = element->font ? element->fontp()->font_size : 16.0f;
    CustomLayoutPaintState* paint = element->custom_layout_paint_prop();
    if (paint && paint->has_intrinsic_measurement && context->child_count == 0 &&
            paint->intrinsic_generation == generation && paint->intrinsic_font_size == font_size) {
        result->width = paint->intrinsic_width;
        result->height = paint->intrinsic_height;
        result->baseline = paint->intrinsic_baseline;
        result->has_width = result->has_height = result->has_baseline = true;
        return true;
    }
    if (!radiant_package_layout_callback(context, result, "lambda.doc.math.mathml_layout")) return false;
    paint = element->custom_layout_paint_prop();
    if (paint && result->has_width && result->has_height && result->has_baseline) {
        paint->intrinsic_generation = generation;
        paint->intrinsic_font_size = font_size;
        paint->intrinsic_width = result->width;
        paint->intrinsic_height = result->height;
        paint->intrinsic_baseline = result->baseline;
        paint->has_intrinsic_measurement = true;
    }
    return true;
}

bool layout_measure_mathml(LayoutContext* lycon, DomElement* element,
                           float* width, float* height, float* baseline) {
    if (!lycon || !layout_is_mathml_root(element) ||
            resolve_display_value(element).inner != CSS_VALUE_MATH) return false;
    CustomLayoutContext context = {};
    context.lycon = lam::up(lycon);
    context.parent = lam::up(lam::unsafe_view_block_element_storage(element));
    context.layout_name = lam::up("lambda-mathml");
    context.available_width = lycon->block.content_width;
    context.available_height = lycon->block.content_height;
    context.css_width = context.css_height = -1.0f;
    CustomLayoutResult result = {};
    if (!mathml_package_layout(&context, &result) || !result.has_width || !result.has_height) return false;
    if (width) *width = result.width;
    if (height) *height = result.height;
    if (baseline) *baseline = result.has_baseline ? result.baseline : result.height;
    return true;
}

bool layout_apply_mathml(LayoutContext* lycon, ViewBlock* block) {
    return layout_custom_apply(lycon, block, "lambda-mathml", mathml_package_layout);
}
