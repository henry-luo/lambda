#include "layout.hpp"
#include "../lambda/input/css/css_style.hpp"

static bool view_computed_percentage(void* context, const CssValue* value, double* result) {
    if (value->type != CSS_VALUE_TYPE_PERCENTAGE) return false;
    *result = value->data.percentage.value * *(const float*)context / 100.0;
    return true;
}

float radiant::resolve_computed_length_percentage(const CssValue* value, float reference_size) {
    if (!value) return 0.0f;
    // computed lengths already own their font/viewport conversion; paint supplies only the final percentage basis.
    CssMathEvaluationContext context = {view_computed_percentage, &reference_size, 1.0, false};
    CssMathResult result = css_math_evaluate(value, &context);
    if (!result.resolved || (result.type != CSS_MATH_NUMBER && result.type != CSS_MATH_LENGTH &&
        result.type != CSS_MATH_PERCENT && result.type != CSS_MATH_LENGTH_PERCENT)) return NAN;
    float pixels = isnan(result.value) && value->type == CSS_VALUE_TYPE_FUNCTION ? 0.0f : result.value;
    return layout_clamp_dimension(pixels);
}
