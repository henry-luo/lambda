// Font-independent MathML Core §5.1 defaults, in the queried font's CSS units.
// https://w3c.github.io/mathml-core/#layout-constants-mathconstants
// Ordinary OS/2/post measurements supply the geometry; no other face's MATH data.
pub fn constants(metrics) {
    let x = metrics.x_height
    let rule = metrics.underline_thickness
    let em = metrics.font_size;
    {
        script_percent_scale_down: 71.0, script_script_percent_scale_down: 50.41,
        delimited_sub_formula_min_height: 0.0, display_operator_min_height: 0.0,
        math_leading: 0.0, axis_height: x / 2.0,
        accent_base_height: x, flattened_accent_base_height: metrics.cap_height,
        subscript_shift_down: metrics.subscript_y_offset, subscript_top_max: 0.8 * x,
        subscript_baseline_drop_min: 0.0, superscript_shift_up: metrics.superscript_y_offset,
        superscript_shift_up_cramped: 0.0, superscript_bottom_min: x / 4.0,
        superscript_baseline_drop_max: 0.0, sub_superscript_gap_min: 4.0 * rule,
        superscript_bottom_max_with_subscript: 0.8 * x, space_after_script: em / 24.0,
        upper_limit_gap_min: 0.0, upper_limit_baseline_rise_min: 0.0,
        lower_limit_gap_min: 0.0, lower_limit_baseline_drop_min: 0.0,
        stack_top_shift_up: 0.0, stack_top_display_style_shift_up: 0.0,
        stack_bottom_shift_down: 0.0, stack_bottom_display_style_shift_down: 0.0,
        stack_gap_min: 3.0 * rule, stack_display_style_gap_min: 7.0 * rule,
        stretch_stack_top_shift_up: 0.0, stretch_stack_bottom_shift_down: 0.0,
        stretch_stack_gap_above_min: 0.0, stretch_stack_gap_below_min: 0.0,
        fraction_numerator_shift_up: 0.0, fraction_numerator_display_style_shift_up: 0.0,
        fraction_denominator_shift_down: 0.0, fraction_denominator_display_style_shift_down: 0.0,
        fraction_numerator_gap_min: rule, fraction_num_display_style_gap_min: 3.0 * rule,
        fraction_rule_thickness: rule,
        fraction_denominator_gap_min: rule, fraction_denom_display_style_gap_min: 3.0 * rule,
        skewed_fraction_horizontal_gap: 0.0, skewed_fraction_vertical_gap: 0.0,
        overbar_vertical_gap: 3.0 * rule, overbar_rule_thickness: rule, overbar_extra_ascender: rule,
        underbar_vertical_gap: 3.0 * rule, underbar_rule_thickness: rule, underbar_extra_descender: rule,
        radical_vertical_gap: 1.25 * rule, radical_display_style_vertical_gap: rule + x / 4.0,
        radical_rule_thickness: rule, radical_extra_ascender: rule,
        radical_kern_before_degree: 5.0 * em / 18.0, radical_kern_after_degree: -10.0 * em / 18.0,
        radical_degree_bottom_raise_percent: 60.0
    }
}
