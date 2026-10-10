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

fn character_chain(data, file, slot, seen = []) array | error {
    if (contains(seen,slot)) error("math: cyclic TeX delimiter character list")
    else {
        let metrics = character_metrics(data,file,slot)^;
        [{slot:slot,metrics:metrics}, *if (metrics.next != null)
            character_chain(data,file,metrics.next,[*seen,slot])^ else []]
    }
}

pub fn delimiters() map | error {
    let file = "cmex10.tfm"
    let extension = tfm_data(file)^
    let symbols = [for (file in ["cmsy10.tfm","cmsy7.tfm","cmsy5.tfm"]) tfm_data(file)^]
    let roman = tfm_data("cmr10.tfm")^
    // fontmath.ltx small/large delimiter fields; family 0 uses the matched, scaled CM10 profile.
    let definitions = [
        {ch:"(",small:0x28,roman:true,large:0x00}, {ch:")",small:0x29,roman:true,large:0x01},
        {ch:"[",small:0x5B,roman:true,large:0x02}, {ch:"]",small:0x5D,roman:true,large:0x03},
        {ch:"⌊",small:0x62,large:0x04}, {ch:"⌋",small:0x63,large:0x05},
        {ch:"⌈",small:0x64,large:0x06}, {ch:"⌉",small:0x65,large:0x07},
        {ch:"{",small:0x66,large:0x08}, {ch:"}",small:0x67,large:0x09},
        {ch:"⟨",small:0x68,large:0x0A}, {ch:"⟩",small:0x69,large:0x0B},
        {ch:"∣",small:0x6A,large:0x0C}, {ch:"∥",small:0x6B,large:0x0D},
        {ch:"/",small:0x2F,roman:true,large:0x0E}, {ch:"\\",small:0x6E,large:0x0F},
        {ch:"↑",small:0x22,large:0x78}, {ch:"↓",small:0x23,large:0x79},
        {ch:"↕",small:0x6C,large:0x3F}, {ch:"⇑",small:0x2A,large:0x7E},
        {ch:"⇓",small:0x2B,large:0x7F}, {ch:"⇕",small:0x6D,large:0x77},
        {ch:"⟮",large:0x3A}, {ch:"⟯",large:0x3B},
        {ch:"⎰",large:0x40}, {ch:"⎱",large:0x41}]
    // KaTeX fonts/src/fonts/makeFF: encoding and authored translations, in 1000-unit ems.
    let variant_chars = split("()[]⌊⌋⌈⌉{}⟨⟩/\\","")
    let variant_slots = [[0,1,2,3,4,5,6,7,8,9,10,11,14,15],
        [16,17,104,105,106,107,108,109,110,111,68,69,46,47],
        [18,19,20,21,22,23,24,25,26,27,28,29,30,31],
        [32,33,34,35,36,37,38,39,40,41,42,43,44,45]]
    let encodings = [*[for (i,slots in variant_slots)
        {family:"KaTeX_Size" ++ string(i + 1),chars:[for (j,slot in slots)
            [slot,variant_chars[j],[810,1110,1410,1710][i]]]}],
        {family:"KaTeX_Size1",chars:[[12,"∣",606],[13,"∥",606],[63,"⏐",601],[119,"‖",601],
            [120,"↑",600],[121,"↓",600],[126,"⇑",600],[127,"⇓",600]]},
        {family:"KaTeX_Size4",chars:[[48,"⎛",1115],[49,"⎞",1115],[50,"⎡",1115],[51,"⎤",1115],
            [52,"⎣",1115],[53,"⎦",1115],[54,"⎢",601],[55,"⎥",601],
            [56,"⎧",900],[57,"⎫",900],[58,"⎩",0],[59,"⎭",0],
            [60,"⎨",1150],[61,"⎬",1150],[62,"⎪",300],
            [64,"⎝",1115],[65,"⎠",1115],[66,"⎜",600],[67,"⎟",600]]}]
    let encoded = [for (encoding in encodings,p in encoding.chars)
        {slot:p[0],ch:p[1],family:encoding.family,shift:p[2]}];
    {recipes:[for (d in definitions) {codepoint:ord(d.ch),
        small:if (d.small == null) [] else [for (data in symbols)
            character_metrics(if (d.roman) roman else data,if (d.roman) "cmr10" else "cmsy",d.small)^],
        chain:character_chain(extension,file,d.large)^}],
        pieces:[for (p in encoded) {*:p,codepoint:ord(p.ch),metrics:character_metrics(extension,file,p.slot)^}]}
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
