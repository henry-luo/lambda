// TeX companion metrics for explicitly matched bundled font resources only.
// Decode the original TFM files; provenance and hashes live beside the resources.
fn halfword(data, offset) => int(data[offset]) * 256 + int(data[offset + 1])

fn fix_word(data, offset) {
    let first = int(data[offset]);
    ((if (first >= 128) first - 256 else first) * 16777216.0 +
        int(data[offset + 1]) * 65536.0 + int(data[offset + 2]) * 256.0 + int(data[offset + 3])) / 1048576.0
}

fn tfm_data(file) binary | error {
    let data = input(sys.lambda.home# ++ "/package/math/fonts/tex/" ++ file, 'binary')^
    if (len(data) < 24 or len(data) != halfword(data, 0) * 4)
        error("math: invalid TeX TFM: " ++ file)
    else data
}

fn parameters(file, required) array | error {
    let data = tfm_data(file)^
    let words = halfword(data, 0)
    let count = halfword(data, 22);
    if (count < required or count > words - 6) error("math: invalid Computer Modern TFM parameter table: " ++ file)
    else [for (i in 0 to (count - 1)) fix_word(data, (words - count + i) * 4)]
}

fn table_metric(data, file, start, count, index) number | error {
    if (index >= count or (start + index + 1) * 4 > len(data))
        error("math: invalid TeX TFM character metric: " ++ file)
    else fix_word(data, (start + index) * 4) * 1000.0
}

fn character_tables(data) => 6 + halfword(data,2) + halfword(data,6) - halfword(data,4) + 1

fn extension_recipe(data, file, index) array | error {
    let start = character_tables(data) + sum([for (offset in [8,10,12,14,16,18]) halfword(data,offset)]);
    if (index >= halfword(data,20) or (start + index + 1) * 4 > len(data))
        error("math: invalid TeX TFM extension recipe: " ++ file)
    else [for (i in 0 to 3) int(data[(start + index) * 4 + i])]
}

fn character_metrics(data, file, slot) map | error {
    let first_slot = halfword(data, 4)
    let last_slot = halfword(data, 6)
    let info = 6 + halfword(data, 2) + slot - first_slot
    let tables = character_tables(data)
    let counts = [for (offset in [8,10,12,14]) halfword(data, offset)];
    if (slot < first_slot or slot > last_slot or info * 4 + 3 >= len(data))
        error("math: invalid TeX TFM character: " ++ file)
    else {
        let hd = int(data[info * 4 + 1])
        let it = int(data[info * 4 + 2])
        let indices = [int(data[info * 4]), int(floor(hd / 16.0)), hd % 16, int(floor(it / 4.0))]
        let values = [for (i,index in indices)
            table_metric(data, file, tables + sum(slice(counts,0,i)), counts[i], index)^];
        {width:values[0], height:values[1], depth:values[2], italic:values[3],
            next:if (it % 4 == 2) int(data[info * 4 + 3]) else null,
            extension:if (it % 4 == 3) extension_recipe(data,file,int(data[info * 4 + 3]))^ else null}
    }
}

// mathtools uses the logical TFM height of family-3 slot 0x7a (\braceld).
fn character_height(file, slot) => character_metrics(tfm_data(file)^, file, slot)^.height

pub fn vertical_arrows() map | error {
    let file = "cmex10.tfm"
    let extension = tfm_data(file)^
    let symbols = [for (file in ["cmsy10.tfm","cmsy7.tfm","cmsy5.tfm"]) tfm_data(file)^]
    // plain.tex/fontmath.ltx delimiter codes; KaTeX_Size1 re-encodes CMEX's pieces.
    let definitions = [{ch:"↑",small:0x22,large:0x78}, {ch:"↓",small:0x23,large:0x79},
        {ch:"↕",small:0x6C,large:0x3F}, {ch:"⇑",small:0x2A,large:0x7E},
        {ch:"⇓",small:0x2B,large:0x7F}, {ch:"⇕",small:0x6D,large:0x77}]
    let pieces = [{ch:"↑",slot:0x78}, {ch:"↓",slot:0x79}, {ch:"⇑",slot:0x7E},
        {ch:"⇓",slot:0x7F}, {ch:"⏐",slot:0x3F}, {ch:"‖",slot:0x77}];
    {recipes:[for (d in definitions) {codepoint:ord(d.ch),
        small:[for (data in symbols) character_metrics(data,"cmsy",d.small)^],
        extension:character_metrics(extension,file,d.large)^.extension}],
        pieces:[for (p in pieces) {*:p, codepoint:ord(p.ch), metrics:character_metrics(extension,file,p.slot)^}]}
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
    let extension = parameters("cmex10.tfm", 13)^
    let paren = character_metrics(tfm_data("cmr10.tfm")^,"cmr10.tfm",0x28)^;
    {bracket_rule: character_height("cmex10.tfm", 0x7A)^,
        math_strut_extent:paren.height + paren.depth,
        scales: {script: 0.7, scriptscript: 0.5}, styles: {
        text: constants(parameters("cmsy10.tfm", 22)^, extension, 1.0),
        script: constants(parameters("cmsy7.tfm", 22)^, extension, 0.7),
        scriptscript: constants(parameters("cmsy5.tfm", 22)^, extension, 0.5)}}
}
