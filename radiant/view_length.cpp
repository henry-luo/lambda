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

static float background_used_length(float value, bool percent, float length,
        const CssValue* expression, bool from_end, float available, float raster_scale) {
    if (expression) {
        // the owner computed lengths; each consumer supplies its percentage basis in logical units.
        float offset = radiant::resolve_computed_length_percentage(expression, available / raster_scale);
        length += from_end ? -offset : offset;
    }
    return (percent ? available * value / 100.0f : value * raster_scale) + length * raster_scale;
}

float background_position_offset(const BackgroundGeometry* background, bool horizontal,
                                 float available, float raster_scale) {
    return background_used_length(horizontal ? background->bg_position_x : background->bg_position_y,
        horizontal ? background->bg_position_x_is_percent : background->bg_position_y_is_percent,
        horizontal ? background->bg_position_x_length : background->bg_position_y_length,
        horizontal ? background->bg_position_x_expression : background->bg_position_y_expression,
        horizontal ? background->bg_position_x_from_end : background->bg_position_y_from_end, available, raster_scale);
}

float background_size_dimension(const BackgroundGeometry* background, bool horizontal,
                                float available, float raster_scale) {
    float used = background_used_length(horizontal ? background->bg_size_width : background->bg_size_height,
        horizontal ? background->bg_size_width_is_percent : background->bg_size_height_is_percent,
        horizontal ? background->bg_size_width_length : background->bg_size_height_length,
        horizontal ? background->bg_size_width_expression : background->bg_size_height_expression,
        false, available, raster_scale);
    // nonnegative size ranges clamp calculations after the positioning area's percentages are known.
    return fmaxf(0.0f, used);
}
