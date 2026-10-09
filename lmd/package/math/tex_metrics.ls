// Computer Modern family 2/3 parameters for the bundled CMU profile only.
// Decode the original TFM files; provenance and hashes live beside the resources.
fn halfword(data, offset) => int(data[offset]) * 256 + int(data[offset + 1])

fn fix_word(data, offset) {
    let first = int(data[offset]);
    ((if (first >= 128) first - 256 else first) * 16777216.0 +
        int(data[offset + 1]) * 65536.0 + int(data[offset + 2]) * 256.0 + int(data[offset + 3])) / 1048576.0
}

fn parameters(file, required) array | error {
    let data = input(sys.lambda.home# ++ "/package/math/fonts/tex/" ++ file, 'binary')^
    let words = halfword(data, 0)
    let count = halfword(data, 22)
    if (len(data) < 24 or len(data) != words * 4 or count < required or count > words - 6)
        error("math: invalid Computer Modern TFM parameter table: " ++ file)
    else [for (i in 0 to (count - 1)) fix_word(data, (words - count + i) * 4)]
}

fn constants(symbols, extension, size) {
    let s = [for (p in symbols) p * 1000.0 * size]
    // Plain TeX uses tenex at all three sizes; extension parameters do not shrink.
    let e = [for (p in extension) p * 1000.0]
    let x = s[4]
    let rule = e[7];
    {
        math_quad: s[5], math_x_height: x, axis_height: s[21], accent_base_height: x,
        superscript_shift_up_display: s[12], superscript_shift_up: s[13], superscript_shift_up_cramped: s[14],
        superscript_baseline_drop_max: s[17], superscript_bottom_min: x / 4.0,
        superscript_bottom_max_with_subscript: 0.8 * x,
        subscript_shift_down: s[15], subscript_shift_down_with_superscript: s[16],
        subscript_baseline_drop_min: s[18], subscript_top_max: 0.8 * x, sub_superscript_gap_min: 4.0 * rule,
        fraction_numerator_display_style_shift_up: s[7], fraction_numerator_shift_up: s[8],
        fraction_denominator_display_style_shift_down: s[10], fraction_denominator_shift_down: s[11],
        stack_top_display_style_shift_up: s[7], stack_top_shift_up: s[9],
        stack_bottom_display_style_shift_down: s[10], stack_bottom_shift_down: s[11],
        stack_gap_min: 3.0 * rule, stack_display_style_gap_min: 7.0 * rule,
        fraction_rule_thickness: rule, delimiter_size_display: s[19], delimiter_size: s[20],
        upper_limit_gap_min: e[8], lower_limit_gap_min: e[9],
        upper_limit_baseline_rise_min: e[10], lower_limit_baseline_drop_min: e[11], limit_extra_padding: e[12],
        overbar_vertical_gap: 3.0 * rule, overbar_rule_thickness: rule, overbar_extra_ascender: rule,
        underbar_vertical_gap: 3.0 * rule, underbar_rule_thickness: rule, underbar_extra_descender: rule,
        radical_vertical_gap: 1.25 * rule, radical_display_style_vertical_gap: rule + x / 4.0,
        radical_rule_thickness: rule, radical_extra_ascender: rule
    }
}

pub fn load() map | error {
    let extension = parameters("cmex10.tfm", 13)^;
    {scales: {script: 0.7, scriptscript: 0.5}, styles: {
        text: constants(parameters("cmsy10.tfm", 22)^, extension, 1.0),
        script: constants(parameters("cmsy7.tfm", 22)^, extension, 0.7),
        scriptscript: constants(parameters("cmsy5.tfm", 22)^, extension, 0.5)}}
}
