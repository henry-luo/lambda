// Font-driven math layout. All geometry is in a 1000-unit em; only emission
// converts to CSS em. OpenType MATH enhances ordinary-font layout when present.
import font: .font
import bx: .svg_box
import stretch: .stretch
import sym: .symbols
import spaces: .spacing_table
import util: .util

pub fn render(ast, options) map | error {
    if (options.font_size != null and (not (options.font_size is number) or options.font_size <= 0 or
        options.font_size - options.font_size != 0))
        error("math: font_size must be finite positive CSS pixels")
    else {
    let profile = font.prepare(ast, options)^
    let context = {profile: profile, style: if (options.display == true) "display" else "text",
        variant: "auto", cramped: false, size: 1.0, pixels_per_em: options.font_size or 16.0}
    let result = node(ast, context)^
    // The title retains searchable, accessible math when painting font-local glyphs.
    let label = if (ast is string) ast else format(ast, {type: "math", flavor: "latex"})^
    let output_el = bx.emit(result, font.UNITS, options.color, options.font_size, label,
        font.stylesheet(profile, result.body));
    {element: output_el, width: result.width / font.UNITS, height: result.height / font.UNITS,
        depth: result.depth / font.UNITS, type: result.type, italic: result.italic / font.UNITS,
        skew: 0.0, max_font_size: scale(context), font_family: profile.family}
    }
}

fn scale(c) => font.scale(c.profile, c.style) * c.size
fn metric(c, key, fallback_value = 0.0) {
    let companion = c.profile.tex.styles[if (c.style == "display") "text" else c.style][key];
    if (companion != null) companion * c.size
    else (c.profile.facts.constants[key] or fallback_value) * scale(c)
}
fn math_quad(c) => metric(c, "math_quad", font.UNITS)
fn with_style(c, style) => {*:c, style: style, cramped: false}
fn script(c) => {*:c, style: if (c.style == "display" or c.style == "text") "script" else "scriptscript"}
fn fraction_child(c) => if (c.style == "display") {*:c, style: "text"} else script(c)
fn command_name(text) => if (slice(text, 0, 1) == "\\") slice(text, 1, len(text)) else text
fn value(n) => string(n.value or n.text or util.text_of(n))

fn atom_type(ch) {
    if (contains("+-−±∓×÷⋅∗∪∩", ch)) "mbin"
    else if (contains("=<>≤≥≠≈∈", ch)) "mrel"
    else if (contains("([{", ch)) "mopen"
    else if (contains(")]}", ch)) "mclose"
    else if (contains(",;:", ch)) "mpunct"
    else "mord"
}

fn character(ch, c, atom, literal = false) {
    let actual = if (ch == "-" and not literal) "−" else ch
    bx.glyph(font.character(c.profile, actual, c.variant)^, scale(c), atom)
}

fn text(text_value, c, literal = false) {
    let chars = split(if (literal) replace(replace(replace(text_value, "\n", " "), "\r", " "), "\t", " ") else text_value, "")
    let boxes = [for (ch in chars where literal or (ch != " " and ch != "\n" and ch != "\t"))
        character(ch, if (literal) {*:c, variant: c.text_variant or "normal"} else c, if (literal) "mord" else atom_type(ch), literal)^]
    // Classification belongs to the enclosing list; a lone '+' here may be infix.
    if (literal) bx.row(boxes) else if (len(boxes) == 1) boxes[0] else spaced(boxes, c)
}

// TeX's first pass normalizes binary atoms sequentially, ignoring explicit glue.
fn normalize_atoms(boxes, i, previous) {
    if (i >= len(boxes)) []
    else {
        let item = boxes[i]
        let next = [for (j in (i + 1) to (len(boxes) - 1) where boxes[j].type != "skip") boxes[j].type][0]
        let kind = if (item.type == "mbin" and (previous == null or
            contains(["mbin", "mop", "mrel", "mopen", "mpunct"], previous) or next == null or
            contains(["mrel", "mclose", "mpunct"], next))) "mord" else item.type;
        [{*:item, type:kind}, *normalize_atoms(boxes, i + 1, if (kind == "skip") previous else kind)]
    }
}

fn spaced(boxes, c) {
    let normalized = normalize_atoms(boxes, 0, null)
    let with_spaces = [for (i, item in normalized) (
        let previous = [for (j in 0 to (i - 1) where normalized[j].type != "skip") normalized[j]],
        let left = previous[len(previous) - 1],
        [if (left != null and item.type != "skip") bx.empty(spaces.get_spacing(left.type, item.type,
            item.spacing_style or c.style) * (item.spacing_quad or math_quad(c))) else bx.empty(), item])];
    if (len(normalized) == 1) normalized[0] else bx.row([for (pair in with_spaces, item in pair) item])
}

fn group(items, c) {
    spaced(group_boxes(items, c, 0)^, c)
}

fn group_boxes(items, c, i) {
    if (i >= len(items)) []
    else {
        let item = items[i]
        let command = if (item is element) string(item.cmd or item.name or "") else ""
        let style = style_name(command)
        if (style != null and item.arg == null)
            group_boxes(items, with_style(c, style), i + 1)^
        else if (item is element and name(item) == 'color_switch') {
            let tail = group(slice(items, i + 1, len(items)), c)^;
            [{*:tail, body: <g fill: (item.color_raw or util.text_of(item.color)), tail.body>}]
        } else [{*:node(item, c)^, spacing_style:c.style, spacing_quad:math_quad(c)}, *group_boxes(items, c, i + 1)^]
    }
}

fn node(n, c) {
    if (n == null) bx.empty()
    else if (n is string) text(n, c)^
    else if (n is array) group(n, c)^
    else if (not (n is element)) bx.empty()
    else match name(n) {
        case 'subsup': scripts(n, c)^
        case 'fraction': fraction(n, c)^
        case 'binomial': fraction(n, c)^
        case 'genfrac': fraction(n, c)^
        case 'infix_frac': fraction(n, c)^
        case 'radical': radical(n, c)^
        case 'accent': accent(n, c)^
        case 'delimiter_group': delimited(n, c)^
        case 'sized_delimiter': sized_delimiter(n, c)^
        case 'middle_delim': delimiter(value(n), font.UNITS * scale(c), c, "mrel")^
        case 'command': command_node(n, c)^
        // parsed symbol commands store their spelling in name, like command nodes.
        case 'symbol_command': command_node(n, c)^
        case 'big_operator': command(value(n), c)^
        case 'operator': text(value(n), {*:c, variant: "normal"})^
        case 'relation': text(value(n), {*:c, variant: "normal"})^
        case 'punctuation': text(value(n), {*:c, variant: "normal"})^
        case 'escaped_symbol': text(value(n), {*:c, variant: "normal"})^
        case 'symbol': text(value(n), c)^
        case 'number': text(value(n), {*:c, variant: "normal"})^
        case 'digit': text(value(n), {*:c, variant: "normal"})^
        case 'unicode_text': text(value(n), c, true)^
        case 'raw_math_text': text(value(n), c, true)^
        case 'text_command': text_command(n, c)^
        case 'text_group': text(util.text_of(n), c, true)^
        case 'style_command': styled(n, c)^
        case 'textstyle_command': styled(n, c)^
        case 'mathop': {*:node(n.body, {*:c, variant: "normal"})^, type: "mop", character:false, limits:true}
        case 'overunder_command': overunder(n, c)^
        case 'extended_arrow': arrow(n, c)^
        case 'environment': matrix(n, c)^
        case 'matrix_command': matrix(n, c)^
        case 'phantom_command': phantom(n, c)^
        case 'box_command': enclosed(n, c)^
        case 'color_command': colored(n, c)^
        case 'space_command': space(n, c)
        case 'hspace_command': space(n, c)
        case 'skip_command': space(n, c)
        case 'spacing_command': space(n, c)
        case 'rule_command': rule(n, c)
        case 'not_overlay': negated(n, c)^
        case 'not_empty': character("/", {*:c, variant: "normal"}, "mrel")^
        case 'limits_modifier': bx.empty()
        case 'group': {*:group(util.content_items(n), c)^, type: "mord"}
        default: group(util.content_items(n), c)^
    }
}

fn command(raw, c) {
    let key = command_name(raw)
    let unicode = sym.lookup_symbol(key)
    let op = sym.get_operator_name(key)
    if (op != null) {*:text(op, c, true)^, type: "mop", limits: sym.is_limit_op(key)}
    else if (unicode != null) {
        let atom = sym.classify_symbol(key)
        let is_large = atom == "mop"
        let result = if (is_large and c.style == "display")
            stretch.glyph(font.large_operator(c.profile, ord(unicode))^, metric(c, "display_operator_min_height"), true, scale(c), atom)^
            else text(unicode, if (is_large) {*:c, variant: "normal"} else c)^
        let centered = if (is_large) center_axis(result, c) else result;
        {*:centered, type: atom, limits: is_large and not contains(key, "int")}
    } else if (contains([",", ":", ";", "!", "quad", "qquad", "enspace", "thinspace"], key))
        space(<space_command cmd: "\\" ++ key>, c)
    else text(raw, {*:c, variant: "normal"}, true)^
}

fn text_command(n, c) {
    let variant = if (n.cmd == "\\textbf") "bold" else if (n.cmd == "\\textit" or n.cmd == "\\emph") "italic"
        else if (n.cmd == "\\texttt") "mono" else if (n.cmd == "\\textsf") "sans" else "normal"
    text(util.text_of(n.content), {*:c, text_variant: variant}, true)^
}

fn command_node(n, c) {
    let key = command_name(string(n.name or n.cmd or ""))
    let items = util.content_items(n)
    if (key == "rule") rule(<rule_command width: util.text_of(items[0]), height: util.text_of(items[1])>, c)
    else if (key == "genfrac" and len(items) == 6) {
        let style = ["display", "text", "script", "scriptscript"][int(util.text_of(items[3]))]
        fraction(<fraction cmd: "\\genfrac", numer: items[4], denom: items[5],
            left:util.text_of(items[0]), right:util.text_of(items[1]), thickness: util.text_of(items[2])>,
            if (style != null) with_style(c, style) else c)^
    } else if (len(items) > 0) spaced([command(key, c)^, *[for (item in items) node(item, c)^]], c)
    else command(key, c)^
}

fn style_name(cmd) {
    match command_name(cmd) {
        case "displaystyle": "display"
        case "textstyle": "text"
        case "scriptstyle": "script"
        case "scriptscriptstyle": "scriptscript"
        default: null
    }
}

fn styled(n, c) {
    let variant = font.command_variant(string(n.cmd))
    let style = style_name(string(n.cmd))
    let child = {*:if (style != null) with_style(c, style) else c, variant: variant or c.variant}
    node(n.arg or content(n)^, child)^
}

fn kern(b, corner, height) {
    let entries = b.glyph.kerns[corner]
    let matches = [for (entry in entries where entry.height == null or entry.height * b.glyph_scale >= height) entry.kern]
    if (len(matches) > 0) matches[0] * b.glyph_scale else 0.0
}

fn scripts(n, c) {
    if (n.base is element and name(n.base) == 'accent' and
        not contains(["\\overline", "\\underline", "\\overbrace", "\\underbrace"], n.base.cmd))
        accent(n.base, c, n)^
    else side_scripts(n, c, node(n.base, c)^)^
}

fn side_scripts(n, c, base) {
    let sup = node(n.sup, script(c))^
    let sub = node(n.sub, {*:script(c), cramped: true})^
    let limits = n.modifier == "limits" or (n.modifier != "nolimits" and c.style == "display" and base.limits == true)
    if (limits) limits_box(base, if (n.sub != null) sub else null, if (n.sup != null) sup else null, c)
    else {
        // TeX Rule 18 exempts a character nucleus; compound drops use script-size parameters.
        let is_character = base.character == true and base.type != "mop"
        let up_initial = if (is_character) 0.0 else base.height - metric(script(c), "superscript_baseline_drop_max")
        let down_initial = if (is_character) 0.0 else base.depth + metric(script(c), "subscript_baseline_drop_min")
        let up0 = max([up_initial, metric(c, if (c.cramped) "superscript_shift_up_cramped"
            else if (c.style == "display" and c.profile.tex != null) "superscript_shift_up_display" else "superscript_shift_up"),
            sup.depth + metric(c, "superscript_bottom_min")])
        let both = n.sup != null and n.sub != null
        let down0 = if (both) max(down_initial, metric(c, "subscript_shift_down_with_superscript",
            c.profile.facts.constants.subscript_shift_down))
            else max([down_initial, metric(c, "subscript_shift_down"), sub.height - metric(c, "subscript_top_max")])
        let missing_gap = if (both) max(0.0, metric(c, "sub_superscript_gap_min") -
            (up0 - sup.depth + down0 - sub.height)) else 0.0
        // After opening the gap, TeX raises both scripts by psi, even if psi exceeds that gap.
        let lift = if (missing_gap > 0.0) max(0.0, metric(c, "superscript_bottom_max_with_subscript") -
            (up0 - sup.depth)) else 0.0
        let up = up0 + lift
        let down = down0 + missing_gap - lift
        let sup_kern = min(kern(base, "top_right", up - sup.depth) + kern(sup, "bottom_left", base.height - up),
            kern(base, "top_right", base.height) + kern(sup, "bottom_left", 0.0 - sup.depth))
        let sub_kern = min(kern(base, "bottom_right", sub.height - down) + kern(sub, "top_left", down - base.depth),
            kern(base, "bottom_right", 0.0 - base.depth) + kern(sub, "top_left", sub.height))
        let x_sup = base.width + base.italic + sup_kern
        let x_sub = base.width + sub_kern
        let width = max([base.width, if (n.sup != null) x_sup + sup.width else 0.0,
            if (n.sub != null) x_sub + sub.width else 0.0]) +
            (if (c.profile.tex != null) dimension("0.5pt", c) else metric(c, "space_after_script"))
        let entries = [{box: base, x: 0.0, y: 0.0},
            *if (n.sup != null) [{box: sup, x: x_sup, y: 0.0 - up}] else [],
            *if (n.sub != null) [{box: sub, x: x_sub, y: down}] else []];
        bx.compose(entries, width, base.type)
    }
}

fn limits_box(base, lower, upper, c) {
    let width = max([base.width, upper.width or 0.0, lower.width or 0.0])
    let up = if (upper != null) max(metric(c, "upper_limit_baseline_rise_min"), metric(c, "upper_limit_gap_min") + upper.depth) + base.height else 0.0
    let down = if (lower != null) max(metric(c, "lower_limit_baseline_drop_min"), metric(c, "lower_limit_gap_min") + lower.height) + base.depth else 0.0
    let entries = [{box: base, x: (width - base.width) / 2.0, y: 0.0},
        *if (upper != null) [{box: upper, x: (width - upper.width + base.italic) / 2.0, y: 0.0 - up}] else [],
        *if (lower != null) [{box: lower, x: (width - lower.width - base.italic) / 2.0, y: down}] else []];
    let result = bx.compose(entries, width, base.type);
    {*:result, height:result.height + (if (upper != null) metric(c, "limit_extra_padding") else 0.0),
        depth:result.depth + (if (lower != null) metric(c, "limit_extra_padding") else 0.0)}
}

fn fraction(n, c) {
    let key = command_name(string(n.cmd or "frac"))
    let context = if (contains(["dfrac", "dbinom", "cfrac"], key)) with_style(c, "display")
        else if (contains(["tfrac", "tbinom"], key)) with_style(c, "text") else c
    let child = fraction_child(context)
    let numer = node(n.numer, child)^
    let denom = node(n.denom, {*:child, cramped: true})^
    let explicit_thickness = if (n.thickness != null and string(n.thickness) != "") dimension(string(n.thickness), context) else null
    let bar = if (explicit_thickness != null) explicit_thickness > 0.0 else not contains(["binom", "dbinom", "tbinom", "choose", "atop", "brace", "brack"], key)
    let display = context.style == "display"
    let thickness = if (bar) explicit_thickness or metric(context, "fraction_rule_thickness") else 0.0
    let axis = metric(context, "axis_height")
    let up0 = metric(context, if (bar) (if (display) "fraction_numerator_display_style_shift_up" else "fraction_numerator_shift_up")
        else (if (display) "stack_top_display_style_shift_up" else "stack_top_shift_up"))
    let down0 = metric(context, if (bar) (if (display) "fraction_denominator_display_style_shift_down" else "fraction_denominator_shift_down")
        else (if (display) "stack_bottom_display_style_shift_down" else "stack_bottom_shift_down"))
    let gap = if (bar) 0.0 else max(0.0, metric(context, if (display) "stack_display_style_gap_min" else "stack_gap_min") - (up0 + down0 - numer.depth - denom.height))
    // An authored TeX bar uses its own thickness; native MATH gaps remain font-owned.
    let tex_bar = context.profile.tex != null or explicit_thickness != null
    let num_gap = if (tex_bar) (if (display) 3.0 else 1.0) * thickness
        else metric(context, if (display) "fraction_num_display_style_gap_min" else "fraction_numerator_gap_min")
    let denom_gap = if (tex_bar) (if (display) 3.0 else 1.0) * thickness
        else metric(context, if (display) "fraction_denom_display_style_gap_min" else "fraction_denominator_gap_min")
    let up = if (bar) max(up0, axis + thickness / 2.0 + numer.depth + num_gap) else up0 + gap / 2.0
    let down = if (bar) max(down0, thickness / 2.0 + denom.height - axis + denom_gap) else down0 + gap / 2.0
    let width = max(numer.width, denom.width)
    let result = bx.compose([{box: numer, x: (width - numer.width) / 2.0, y: 0.0 - up},
        {box: denom, x: (width - denom.width) / 2.0, y: down},
        *if (bar) [{box: bx.rule(width, thickness, 0.0 - axis - thickness / 2.0), x: 0.0, y: 0.0}] else []], width, "mord")
    let fences = if (contains(["binom", "dbinom", "tbinom", "choose"], key)) ["(", ")"]
        else if (key == "brace") ["{", "}"] else if (key == "brack") ["[", "]"]
        else [string(n.left or "."), string(n.right or ".")]
    let target = metric(context, if (display) "delimiter_size_display" else "delimiter_size",
        c.profile.facts.constants.delimited_sub_formula_min_height);
    bx.row([delimiter(fences[0], target, context, "mopen")^, result,
        delimiter(fences[1], target, context, "mclose")^], "mord")
}

fn center_axis(b, c) => bx.shifted(b, 0.0, (b.height - b.depth) / 2.0 - metric(c, "axis_height"), b.width)

fn delimiter(raw, target, c, atom) {
    let ch = sym.lookup_symbol(raw) or raw
    if (ch == "." or ch == "") bx.empty(dimension("1.2pt", c))
    else center_axis(stretch.glyph(font.glyph(c.profile, ord(ch))^, target, true, scale(c), atom)^, c)
}

fn fence_box(body, left, right, c) {
    let axis = metric(c, "axis_height")
    let extent = 2.0 * max(body.height - axis, body.depth + axis)
    let target = max(extent * 901.0 / 1000.0, extent - dimension("5pt", c))
    bx.row([delimiter(left, target, c, "mopen")^, body, delimiter(right, target, c, "mclose")^], "minner")
}

fn delimited(n, c) => fence_box(group(util.content_items(n), c)^, string(n.left or "."), string(n.right or "."), c)^

fn sized_delimiter(n, c) {
    // Explicit TeX sizes are author requests in em, not font size-face indices.
    let level = sym.get_delim_size(command_name(string(n.size or n.cmd or "big"))) or 1.0
    delimiter(string(n.delim or n.value or util.text_of(n)), font.UNITS * scale(c) * level, c, "mord")^
}

fn radical(n, c) {
    let body = node(n.radicand, {*:c, cramped: true})^
    let thickness = metric(c, "radical_rule_thickness")
    let gap = metric(c, if (c.style == "display") "radical_display_style_vertical_gap" else "radical_vertical_gap")
    let sign = stretch.glyph(font.glyph(c.profile, ord("√"))^, body.height + body.depth + gap + thickness, true, scale(c), "mord")^
    let adjusted_gap = gap + max(0.0, sign.height + sign.depth - (body.height + body.depth + gap + thickness)) / 2.0
    let top = 0.0 - body.height - adjusted_gap - thickness
    let index = node(n.index, {*:c, style: "scriptscript"})^
    let before = if (n.index != null) max(0.0, metric(c, "radical_kern_before_degree")) else 0.0
    let offset = if (n.index != null) before + index.width + max(0.0 - index.width, metric(c, "radical_kern_after_degree")) else 0.0
    let index_y = top + sign.height + sign.depth - (sign.height + sign.depth) * c.profile.facts.constants.radical_degree_bottom_raise_percent / 100.0 - index.depth
    let entries = [{box: sign, x: offset, y: top + sign.height}, {box: body, x: offset + sign.width, y: 0.0},
        {box: bx.rule(body.width, thickness, top), x: offset + sign.width, y: 0.0},
        *if (n.index != null) [{box: index, x: before, y: index_y}] else []]
    let result = bx.compose(entries, offset + sign.width + body.width);
    {*:result, height: result.height + metric(c, "radical_extra_ascender")}
}

fn accent(n, c, attached = null) {
    let key = command_name(string(n.cmd))
    let base = node(n.base, if (key == "underline") c else {*:c, cramped: true})^
    if (key == "overline" or key == "underline") {
        let above = key == "overline"
        let thickness = metric(c, if (above) "overbar_rule_thickness" else "underbar_rule_thickness")
        let gap = metric(c, if (above) "overbar_vertical_gap" else "underbar_vertical_gap")
        let y = if (above) 0.0 - base.height - gap - thickness else base.depth + gap
        let result = bx.compose([{box: base, x: 0.0, y: 0.0}, {box: bx.rule(base.width, thickness, y), x: 0.0, y: 0.0}], base.width);
        {*:result, height: result.height + (if (above) metric(c, "overbar_extra_ascender") else 0.0),
            depth: result.depth + (if (above) 0.0 else metric(c, "underbar_extra_descender"))}
    } else if (key == "overbrace" or key == "underbrace") {
        let above = key == "overbrace"
        let g = font.glyph(c.profile, ord(if (above) "⏞" else "⏟"))^
        let mark = stretch.glyph(g, base.width, false, scale(c), "mord")^
        let y = if (above) 0.0 - base.height - metric(c, "stretch_stack_gap_above_min") - mark.depth
            else base.depth + metric(c, "stretch_stack_gap_below_min") + mark.height
        let result = bx.compose([{box: base, x: 0.0, y: 0.0}, {box: mark, x: (base.width - mark.width) / 2.0, y: y}], base.width);
        {*:result, limits: true}
    } else {
        let ch = sym.get_accent(key)
        if (ch == null) error("math: unsupported accent " ++ key)
        else {
        let g = font.glyph(c.profile, ord(ch))^
        let wide = starts_with(key, "wide") or contains(key, "arrow")
        let mark = if (contains(key, "arrow")) stretch.glyph(g, base.width, false, scale(c), "mord")^
            else if (wide) stretch.accent(g, base.width, scale(c))^ else bx.glyph(g, scale(c))
        let x = base.accent - mark.accent
        // below-arrow accents must clear the base's descent instead of its top.
        let y = if (starts_with(key, "under")) base.depth + metric(c, "underbar_vertical_gap") + mark.height
            else 0.0 - max(0.0, base.height - metric(c, "accent_base_height"));
        let scripted = if (attached != null and base.character == true) side_scripts(attached, c, base)^ else base
        let result = bx.compose([{box: scripted, x: 0.0, y: 0.0}, {box: mark, x: x, y: y}], scripted.width, base.type);
        if (attached != null and base.character != true) side_scripts(attached, c, result)^ else result
        }
    }
}

fn overunder(n, c) {
    let key = command_name(string(n.cmd))
    let base = node(n.base or n.body, c)^
    let label = node(n.annotation or n.label or n.over or n.under, script(c))^
    limits_box(base, if (key == "underset") label else null, if (key == "underset") null else label, c)
}

fn arrow(n, c) {
    let key = command_name(string(n.cmd))
    let ch = if (contains(key, "leftright")) "↔" else if (contains(key, "left")) "←" else "→"
    let upper = node(n.upper or n.label or n.above or n.over, script(c))^
    let lower = node(n.lower or n.below or n.under, script(c))^
    let width = max(upper.width, lower.width) + 2.0 * metric(c, "space_after_script")
    let base = stretch.glyph(font.glyph(c.profile, ord(ch))^, width, false, scale(c), "mrel")^
    limits_box(base, lower, upper, c)
}

fn matrix(n, c) {
    let key = command_name(string(n.name or n.cmd or "matrix"))
    let child = with_style(c, if (key == "smallmatrix") "script" else "text")
    let items = util.content_items(n.body)
    let rows = util.parse_rows(items, 0, len(items), [], [], [])
    let cells = [for (row in rows) [for (cell in row.cells) group(cell.items, child)^]]
    let count = max([0, *[for (row in cells) len(row)]])
    let widths = [for (col in 0 to (count - 1)) max([0.0, *[for (row in cells) row[col].width or 0.0]])]
    let heights = [for (row in cells) max([0.0, *[for (cell in row) cell.height]])]
    let depths = [for (row in cells) max([0.0, *[for (cell in row) cell.depth]])]
    let aligned = contains(["aligned", "align", "align*", "split"], key)
    let gap_x = font.UNITS * scale(child)
    let gaps = [for (col in 0 to (count - 1)) if (col == 0) 0.0
        else if (aligned) (if (col % 2 == 1) 0.0 else 2.0 * gap_x) else gap_x]
    let declared = [for (ch in split(string(n.columns or ""), "") where contains("lcr", ch)) ch]
    let aligns = [for (col in 0 to (count - 1)) declared[col] or
        (if (aligned) (if (col % 2 == 0) "r" else "l") else if (key == "cases" or key == "rcases") "l" else "c")]
    let gap_y = metric(child, "stack_gap_min")
    let total = sum(heights) + sum(depths) + gap_y * max(0, len(rows) - 1)
    let top = 0.0 - total / 2.0 - metric(c, "axis_height")
    let entries = [for (r, row in cells, col, cell in row) {
        box: {*:cell, body: <g 'data-column-align': aligns[col], cell.body>},
        x: sum(slice(widths, 0, col)) + sum(slice(gaps, 0, col + 1)) +
            (if (aligns[col] == "r") widths[col] - cell.width else if (aligns[col] == "l") 0.0 else (widths[col] - cell.width) / 2.0),
        y: top + sum(slice(heights, 0, r)) + sum(slice(depths, 0, r)) + float(r) * gap_y + heights[r]}]
    let table = bx.compose(entries, sum(widths) + sum(gaps), "minner")
    let result = {*:table, body: <g 'data-math-kind': "matrix", table.body>}
    let fences = match key {
        case "pmatrix": ["(", ")"]
        case "bmatrix": ["[", "]"]
        case "Bmatrix": ["{", "}"]
        case "vmatrix": ["|", "|"]
        case "Vmatrix": ["‖", "‖"]
        case "cases": ["{", "."]
        case "rcases": [".", "}"]
        default: null
    }
    if (fences != null) fence_box(result, fences[0], fences[1], c)^ else result
}

fn phantom(n, c) {
    let base = node(n.content, c)^
    let key = command_name(string(n.cmd));
    {*:base, character:false, body: if (key == "smash") base.body else <g>,
        width: if (key == "vphantom") 0.0 else base.width,
        height: if (key == "hphantom" or key == "smash") 0.0 else base.height,
        depth: if (key == "hphantom" or key == "smash") 0.0 else base.depth}
}

fn enclosed(n, c) {
    let base = node(n.content, c)^
    let key = command_name(string(n.cmd))
    if (contains(key, "lap")) bx.shifted(base, if (contains(key, "llap")) 0.0 - base.width else if (contains(key, "clap")) 0.0 - base.width / 2.0 else 0.0, 0.0, 0.0)
    else {
        let rule = metric(c, "fraction_rule_thickness")
        let pad = metric(c, "overbar_vertical_gap") + rule
        let result = bx.shifted(base, pad, 0.0, base.width + 2.0 * pad);
        {*:result, height: base.height + pad, depth: base.depth + pad,
            body: <g <rect x: rule / 2.0, y: 0.0 - base.height - pad + rule / 2.0,
                width: result.width - rule, height: base.height + base.depth + 2.0 * pad - rule,
                fill: "none", stroke: "currentColor", 'stroke-width': rule> result.body>}
    }
}

fn colored(n, c) {
    let base = node(n.content, c)^
    let color = n.color_raw or util.text_of(n.color)
    if (n.cmd == "\\colorbox") {*:base, body: <g <rect x: 0, y: 0.0 - base.height,
        width: base.width, height: base.height + base.depth, fill: color> base.body>}
    else {*:base, body: <g fill: color, style: "color:" ++ color, base.body>}
}

fn space(n, c) {
    let key = command_name(string(n.cmd or n.value or ""))
    let em = match key {
        case ",": 3.0 / 18.0
        case "thinspace": 3.0 / 18.0
        case ":": 4.0 / 18.0
        case ";": 5.0 / 18.0
        case "!": -3.0 / 18.0
        case "enspace": 0.5
        case "quad": 1.0
        case "qquad": 2.0
        default: null
    }
    {*:bx.empty(if (em != null) em * (if (contains(["quad", "qquad", "enspace"], key)) font.UNITS * c.size else math_quad(c)) else dimension(string(n.value or "0em"), c)), type: "skip"}
}

fn dimension(raw, c) {
    let dim = util.dimension_from_string(raw)
    let units = match dim.unit {
        case "em": font.UNITS * c.size
        case "ex": c.profile.font_metrics.x_height * c.size
        case "mu": math_quad(c) / 18.0
        case "pt": font.UNITS * 96.0 / 72.27 / c.pixels_per_em
        case "pc": font.UNITS * 96.0 / 72.27 * 12.0 / c.pixels_per_em
        case "in": font.UNITS * 96.0 / c.pixels_per_em
        case "cm": font.UNITS * 96.0 / 2.54 / c.pixels_per_em
        case "mm": font.UNITS * 96.0 / 25.4 / c.pixels_per_em
        case "px": font.UNITS / c.pixels_per_em
        default: font.UNITS * scale(c)
    }
    dim.sign * dim.value * units
}

fn rule(n, c) {
    let width = dimension(string(n.width or "0em"), c)
    let height = dimension(string(n.height or "0em"), c)
    bx.rule(width, height, 0.0 - height)
}

fn negated(n, c) {
    let base = node(n.base or n.target or content(n)^, c)^
    let slash = character("/", {*:c, variant: "normal"}, "mrel")^
    bx.compose([{box: base, x: 0.0, y: 0.0}, {box: slash, x: (base.width - slash.width) / 2.0, y: 0.0}], base.width, base.type)
}
