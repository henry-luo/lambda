// Font-driven math layout. All geometry is in a 1000-unit em; only emission
// converts to CSS em. OpenType MATH enhances ordinary-font layout when present.
import font: .font
import bx: .svg_box
import stretch: .stretch
import sym: .symbols
import spaces: .spacing_table
import util: .util
import graphics: .graphics

pub fn render(ast, options) map | error {
    if (ast.error != null) error("math: " ++ string(ast.error))
    else if (options.font_size != null and (not (options.font_size is number) or options.font_size <= 0 or
        options.font_size - options.font_size != 0))
        error("math: font_size must be finite positive CSS pixels")
    else {
    let profile = font.prepare(ast, options)^
    let context = {profile: profile, style: if (options.display == true) "display" else "text",
        display_mode:options.display == true, variant: "auto", cramped: false, size: 1.0,
        pixels_per_em: options.font_size or 16.0,
        base_uri: options.base_uri}
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

// LaTeX's 10pt size ladder has distinct script sizes; size declarations are absolute.
let size_multipliers = [0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.2, 1.44, 1.728, 2.074, 2.488]
// classes.dtx/size10.clo text baselines, in units of the initial 10pt em.
let text_baselines = [0.6, null, 0.8, 0.95, 1.1, 1.2, 1.4, 1.8, 2.2, 2.5, 3.0]
let size_styles = [[0,0,0], [1,0,0], [2,0,0], [3,1,0], [4,1,0], [5,2,0],
    [6,3,1], [7,5,2], [8,6,5], [9,7,6], [10,9,8]]
let size_commands = ["tiny", "sixptsize", "scriptsize", "footnotesize", "small", "normalsize",
    "large", "Large", "LARGE", "huge", "Huge"]

fn text_scale(c) => (if (c.size_index != null) size_multipliers[c.size_index] else 1.0) * c.size
fn scale(c) => if (c.size_index != null and c.profile.tex != null)
    size_multipliers[size_styles[c.size_index][if (c.style == "script") 1 else if (c.style == "scriptscript") 2 else 0]] * c.size
    else font.scale(c.profile, c.style) * text_scale(c)
fn metric(c, key, fallback_value = 0.0) {
    let companion = c.profile.tex.styles[if (c.style == "display") "text" else c.style][key];
    if (companion != null) companion * (scale(c) / font.scale(c.profile, c.style))
    else (c.profile.facts.constants[key] or fallback_value) * scale(c)
}
fn math_quad(c) => metric(c, "math_quad", font.UNITS)
fn with_style(c, style) => {*:c, style: style, cramped: false}
fn script(c) => {*:c, style: if (c.style == "display" or c.style == "text") "script" else "scriptscript"}
fn fraction_child(c) => if (c.style == "display") {*:c, style: "text"} else script(c)
fn command_name(text) => if (len(text) > 1 and slice(text, 0, 1) == "\\") slice(text, 1, len(text)) else text
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
    if (not literal and contains(c.profile.closed_composites,actual)) closed_integral(actual,c)^
    else bx.glyph(font.character(c.profile, actual, c.variant, literal)^, scale(c), atom)
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
        [{*:item, type:kind}, *normalize_atoms(boxes, i + 1,
            if (kind == "skip") previous else item.after_type or kind)]
    }
}

fn spaced(boxes, c) {
    let normalized = normalize_atoms(boxes, 0, null)
    let with_spaces = [for (i, item in normalized) (
        let previous = [for (j in 0 to (i - 1) where normalized[j].type != "skip") normalized[j]],
        let left = previous[len(previous) - 1],
        [if (left != null and item.type != "skip") bx.empty(spaces.get_spacing(left.after_type or left.type, item.type,
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
        let size_index = index_of(size_commands, command_name(command))
        if (item is element and name(item) == 'middle_delim') {
            // e-TeX closes the preceding math group and reopens it in the enclosing style/scope.
            let restored = {*:(c.delimiter_context or c),delimiter_context:c.delimiter_context,
                delimiter_target:c.delimiter_target,measuring_delimiters:c.measuring_delimiters};
            [middle_delimiter(item,restored)^,*group_boxes(items,restored,i + 1)^]
        } else if (item is element and name(item) == 'font_switch') {
            let variant = {rm:"normal", it:"italic", bf:"bold", sf:"sans", tt:"mono"}[command_name(command)];
            group_boxes(items, {*:c, variant:variant, text_variant:variant}, i + 1)^
        } else if (style != null and item.arg == null)
            group_boxes(items, with_style(c, style), i + 1)^
        else if (size_index != null and name(item) == 'size_command')
            group_boxes(items, {*:c, size_index:size_index,
                style:if (c.style == "display") "display" else "text"}, i + 1)^
        else if (item is element and name(item) == 'mod_command')
            // Expand into the enclosing list so declarations in macro arguments retain TeX scope.
            group_boxes([*modulo_items(item,c),*slice(items,i + 1,len(items))],c,0)^
        else if (item is element and name(item) == 'color_switch') {
            let tail = group(slice(items, i + 1, len(items)), c)^;
            let color = item.color_raw or util.text_of(item.color);
            [{*:tail, body: <g fill:color, style:"color:" ++ color, tail.body>}]
        } else [{*:node(item, c)^, spacing_style:c.style, spacing_quad:math_quad(c)}, *group_boxes(items, c, i + 1)^]
    }
}

fn node(n, c) {
    if (n == null) bx.empty()
    else if (n is string) text(n, c, c.text_mode == true)^
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
        case 'middle_delim': middle_delimiter(n,c)^
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
        // Contiguous Unicode operators still need math-list atom spacing and script policy.
        case 'unicode_text': if (c.text_mode != true and len([for (ch in split(value(n),"")
            where contains(c.profile.closed_composites,ch)) ch]) > 0) text(value(n),c)^
            else text(value(n),c,true)^
        case 'raw_math_text': text(value(n), c, true)^
        case 'text_command': text_command(n, c)^
        case 'text_group': group(util.content_items(n), {*:c, text_mode:true, variant:c.text_variant or "normal"})^
        case 'embedded_math': group(util.content_items(n), {*:c, text_mode:false, variant:"auto", style:"text"})^
        case 'verbatim': text(if (n.starred) replace(string(n.value), " ", "␣") else string(n.value), {*:c, text_variant:"mono"}, true)^
        case 'box_transform': transformed(n, c)^
        case 'equation_tag': equation_tag(n, c)^
        case 'mod_command': group(modulo_items(n,c),c)^
        case 'cd_arrow': diagram_arrow(n, c)^
        case 'layout_control': bx.empty()
        case 'array_rule': bx.empty()
        case 'style_command': styled(n, c)^
        case 'textstyle_command': styled(n, c)^
        case 'mathop': {*:node(n.body, {*:c, variant: "normal"})^, type: "mop", character:false, limits:true}
        case 'math_atom': {*:node(n.body, c)^, type:string(n.atom), character:false, limits:false}
        case 'mathchoice': if (n[c.style] is element and name(n[c.style]) == 'group')
            group(util.content_items(n[c.style]), c)^ else node(n[c.style], c)^
        case 'overunder_command': overunder(n, c)^
        case 'extended_arrow': arrow(n, c)^
        case 'image_command': graphics.render(n, c.base_uri, (raw) => dimension(raw, c))^
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
    else if (contains(c.profile.closed_composites,unicode)) closed_integral(unicode,c)^
    else if (unicode != null) {
        let atom = sym.classify_symbol(key)
        let is_large = atom == "mop"
        let result = if (is_large and c.style == "display")
            stretch.glyph(font.large_operator(c.profile, ord(unicode))^, metric(c, "display_operator_min_height"), true, scale(c), atom)^
            else text(unicode, if (is_large) {*:c, variant: "normal"} else c)^
        let centered = if (is_large) center_axis(result, c) else result;
        {*:centered, type: atom, limits: is_large and (key == "intop" or not contains(key, "int"))}
    } else if (key == "mathstrut") phantom(<phantom_command cmd:"\\vphantom", content:"(">,c)^
    else if (key == "KaTeX") katex_logo(c)^
    else if (contains(["TeX", "LaTeX"], key)) text(key, {*:c, text_variant:"normal"}, true)^
    else if (contains([",", ":", ";", "!", "quad", "qquad", "enspace", "thinspace"], key))
        space(<space_command cmd: "\\" ++ key>, c)
    else text(raw, {*:c, variant: "normal"}, true)^
}

fn closed_integral(ch, c) {
    // tex_bundled_integrals.tex: mathop/vcenter, centered ooalign rows and shared vphantom extents.
    let base = command(if (sym.closed_integral_base(ch) == "∬") "iint" else "iiint",c)^
    let circle = character("◯",{*:c,variant:"normal"},"mord")^
    let width = max(base.width,circle.width)
    let overlay = bx.compose([{box:base,x:(width - base.width) / 2.0,y:0.0},
        {box:circle,x:(width - circle.width) / 2.0,y:0.0}],width,"mop");
    {*:center_axis(overlay,c),limits:false}
}

fn katex_logo(c) {
    // screenshotter/test.tex uses an mbox; math alphabet and script styles do not select its font.
    let context = with_style(c,"text")
    let variant = c.text_variant or "normal"
    let letters = [for (ch in ["K","T","E","X"]) text(ch,context,true)^]
    let a = text("A",{*:context,size:c.size * 0.75},true)^
    let em = font.UNITS * text_scale(c)
    // Its vbox aligns A's top to T; ltlogos.dtx supplies the lowered E and TeX kerns.
    let a_x = letters[0].width - 0.17 * em
    let t_x = a_x + a.width - 0.15 * em
    let e_x = t_x + letters[1].width - 0.1667 * em
    let x_x = e_x + letters[2].width - 0.125 * em;
    bx.compose([{box:letters[0],x:0.0,y:0.0}, {box:a,x:a_x,y:a.height - letters[1].height},
        {box:letters[1],x:t_x,y:0.0},
        {box:letters[2],x:e_x,y:0.5 * font.text_metrics(c.profile,variant).x_height * text_scale(c)},
        {box:letters[3],x:x_x,y:0.0}],x_x + letters[3].width)
}

fn text_command(n, c) {
    let variant = if (n.cmd == "\\textbf") "bold" else if (n.cmd == "\\textit" or n.cmd == "\\emph") "italic"
        else if (n.cmd == "\\texttt") "mono" else if (n.cmd == "\\textsf") "sans" else "normal"
    if (n.body != null) node(n.body, {*:c, text_variant:variant})^
    else text(util.text_of(n.content), {*:c, text_variant: variant}, true)^
}

fn command_node(n, c) {
    let key = command_name(string(n.name or n.cmd or ""))
    let items = util.content_items(n)
    if (key == "rule") {
        // LaTeX's optional raise precedes the two mandatory dimensions; it is not the width.
        let start = if (name(items[0]) == 'brack_group') 1 else 0;
        rule(<rule_command width:util.text_of(items[start],true),height:util.text_of(items[start + 1],true),
            raise:if (start == 1) util.text_of(items[0],true) else "0pt">,c)
    }
    else if (key == "genfrac" and len(items) == 6) {
        let style = ["display", "text", "script", "scriptscript"][int(util.text_of(items[3]))]
        fraction(<fraction cmd: "\\genfrac", numer: items[4], denom: items[5],
            left:util.text_of(items[0]), right:util.text_of(items[1]), thickness: util.text_of(items[2])>,
            if (style != null) with_style(c, style) else c)^
    } else if (len(items) > 0) spaced([command(key, c)^, *[for (item in items) node(item, c)^]], c)
    else command(if (c.text_mode == true and sym.lookup_symbol(key) == null and
        sym.get_operator_name(key) == null and not contains(["TeX","LaTeX","KaTeX","mathstrut"],key)) "\\" ++ key else key, c)^
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
    let result = node(n.arg or content(n)^, child)^;
    if (n.cmd == "\\operatorname") {*:result, type:"mop", limits:n.limits == true, character:false} else result
}

fn kern(b, corner, height) {
    let entries = b.glyph.kerns[corner]
    let matches = [for (entry in entries where entry.height == null or entry.height * b.glyph_scale >= height) entry.kern]
    if (len(matches) > 0) matches[0] * b.glyph_scale else 0.0
}

fn scripts(n, c) {
    if (n.base is element and name(n.base) == 'accent' and
        not contains(["\\overline", "\\underline", "\\underbar", "\\overbrace", "\\underbrace",
            "\\overbracket", "\\underbracket", "\\overgroup", "\\undergroup", "\\overlinesegment", "\\underlinesegment"], n.base.cmd))
        accent(n.base, c, n)^
    else side_scripts(n, c, node(n.base, c)^)^
}

fn side_scripts(n, c, base) {
    let sup = node(n.sup, script(c))^
    let sub = node(n.sub, {*:script(c), cramped: true})^
    // Bracket and brace annotations stack in every style; operators default to display only.
    let limits = n.modifier == "limits" or (n.modifier != "nolimits" and
        (base.limits == "always" or (c.style == "display" and base.limits == true)))
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

fn text_baseline(c) number | error {
    let amount = text_baselines[c.size_index or 5];
    // sixptsize is a math extension, not a size10.clo text-baseline definition.
    if (amount == null) error("math: no text-strut baseline defined for \\sixptsize")
    else amount * font.UNITS * c.size
}

fn fraction(n, c) {
    let key = command_name(string(n.cmd or "frac"))
    let chosen = if (n.style != null and n.style != "") ["display", "text", "script", "scriptscript"][int(n.style)] else null
    let context = if (chosen != null) with_style(c, chosen)
        else if (contains(["dfrac", "dbinom", "cfrac"], key)) with_style(c, "display")
        else if (contains(["tfrac", "tbinom"], key)) with_style(c, "text") else c
    let child = fraction_child(context)
    let raw_numer = node(n.numer, child)^
    // amsmath cfrac uses the surrounding text strut even in script math; ltfsstrc.dtx defines 70/30.
    let baseline = if (key == "cfrac") text_baseline(c)^ else 0.0
    let numer = if (key == "cfrac") {*:raw_numer, height:max(raw_numer.height,0.7 * baseline),
        depth:max(raw_numer.depth,0.3 * baseline)} else raw_numer
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
    let alignment = if (key == "cfrac") util.text_of(n.options) else "c"
    let num_x = if (alignment == "l") 0.0 else if (alignment == "r") width - numer.width else (width - numer.width) / 2.0
    let result = bx.compose([{box: numer, x:num_x, y: 0.0 - up},
        {box: denom, x: (width - denom.width) / 2.0, y: down},
        *if (bar) [{box: bx.rule(width, thickness, 0.0 - axis - thickness / 2.0), x: 0.0, y: 0.0}] else []], width, "mord")
    let fences = if (contains(["binom", "dbinom", "tbinom", "choose"], key)) ["(", ")"]
        else if (key == "brace") ["{", "}"] else if (key == "brack") ["[", "]"]
        else [string(n.left_delim or n.left or "."), string(n.right_delim or n.right or ".")]
    let target = metric(context, if (display) "delimiter_size_display" else "delimiter_size",
        c.profile.facts.constants.delimited_sub_formula_min_height);
    bx.row([delimiter(fences[0], target, context, "mopen")^, result,
        delimiter(fences[1], target, context, "mclose")^,
        // cfrac removes the right null-delimiter space so nested rules end together.
        *if (key == "cfrac") [bx.empty(0.0 - dimension("1.2pt",context))] else []], "mord")
}

fn center_axis(b, c) => bx.shifted(b, 0.0, (b.height - b.depth) / 2.0 - metric(c, "axis_height"), b.width)

// TeX make_left_right uses delimiterfactor=901 and delimitershortfall=5pt.
fn delimiter_target(extent, c) => max(extent * 901.0 / 1000.0, extent - dimension("5pt", c))

fn tex_delimiter(recipe, target, c, atom) {
    let size = if (c.style == "scriptscript") 2 else if (c.style == "script") 1 else 0
    // var_delimiter searches the current symbol size, then successively larger sizes.
    let small = [for (i in 0 to size where recipe.small[size - i] != null) (
        let index = size - i,
        let amount = font.scale(c.profile,["text","script","scriptscript"][index]) * text_scale(c),
        {glyph:recipe.small[index], scale:amount})]
    // CMEX is the same text-sized extension font in every style; search its TFM character list.
    let variants = [*small,*[for (g in recipe.variants) {glyph:g,scale:text_scale(c)}]]
    let adequate = [for (v in variants where (v.glyph.height + v.glyph.depth) * v.scale >= target) v];
    if (len(adequate) > 0) bx.glyph(adequate[0].glyph,adequate[0].scale,atom)
    else if (recipe.repeat != null) stretch.tex_assembly(recipe,target,text_scale(c),atom)
    // TeX keeps the largest finite variant when the character list has no extension recipe.
    else bx.glyph(variants[len(variants) - 1].glyph,variants[len(variants) - 1].scale,atom)
}

fn delimiter(raw, target, c, atom) {
    let key = command_name(raw)
    let ch = if (raw == "\\|") "‖" else sym.lookup_symbol(key) or key
    let recipe = font.tex_delimiter(c.profile,ord(ch))^
    // var_delimiter centers even the empty null-delimiter box on the math axis.
    if (ch == "." or ch == "") {*:bx.empty(dimension("1.2pt",c)),
        height:metric(c,"axis_height"),depth:0.0 - metric(c,"axis_height")}
    else if (recipe != null) center_axis(tex_delimiter(recipe,target,c,atom),c)
    else center_axis(stretch.glyph(font.glyph(c.profile, ord(ch))^, target, true, scale(c), atom)^, c)
}

fn fence_box(body, left, right, c, demand = null) {
    let axis = metric(c, "axis_height")
    let extent = 2.0 * max(body.height - axis, body.depth + axis)
    let target = if (demand != null) demand else delimiter_target(extent,c)
    bx.row([delimiter(left, target, c, "mopen")^, body, delimiter(right, target, c, "mclose")^], "minner")
}

fn delimited(n, c) {
    let items = if (n.body != null) util.content_items(n.body) else util.content_items(n)
    // TeX's demand ignores left/right/middle noads; all three use the same first-pass extent.
    let body = group(items,{*:c,delimiter_context:c,measuring_delimiters:true})^
    let extent = 2.0 * max(body.height - metric(c, "axis_height"), body.depth + metric(c, "axis_height"))
    let target = delimiter_target(extent,c);
    fence_box(group(items,{*:c,delimiter_context:c,delimiter_target:target,measuring_delimiters:false})^,
        string(n.left or "."),string(n.right or "."),c,target)^
}

fn middle_delimiter(n,c) {
    let result = if (c.measuring_delimiters) bx.empty() else
        delimiter(string(n.delim or "."),if (c.delimiter_target != null) c.delimiter_target
            else font.UNITS * scale(c),c,"mclose")^;
    // e-TeX's middle is close on its left and open on its right, not a relation atom.
    {*:result,type:"mclose",after_type:"mopen",spacing_style:c.style,spacing_quad:math_quad(c)}
}

fn sized_delimiter(n, c) {
    let key = command_name(string(n.size or n.cmd or "big"))
    let level = sym.get_delim_size(key) or 1.0
    // fontmath.ltx wraps bigl/bigr/bigm in mathopen/mathclose/mathrel respectively.
    let atom = if (ends_with(key,"l")) "mopen" else if (ends_with(key,"r")) "mclose"
        else if (ends_with(key,"m")) "mrel" else "mord"
    let raw = string(n.delim or n.value or util.text_of(n))
    let ch = sym.lookup_symbol(command_name(raw)) or raw
    let recipe = font.tex_delimiter(c.profile,ord(ch))^;
    if (recipe != null) {
        // amsmath bBigg@: a text-style hbox and 1.2 * math-strut extent times 1/1.5/2/2.5.
        let context = with_style(c,"text")
        let extent = c.profile.tex.math_strut_extent * level * text_scale(c);
        delimiter(raw,delimiter_target(extent,context),context,atom)^
    } else delimiter(raw,font.UNITS * scale(c) * level,c,atom)^
}

fn radical(n, c) {
    let recipe = font.tex_delimiter(c.profile,ord("√"))^;
    if (recipe != null) tex_radical(n,c,recipe)^
    else font_radical(n,c)^
}

fn tex_radical(n,c,recipe) {
    let body = node(n.radicand,{*:c,cramped:true})^
    let gap = metric(c,if (c.style == "display") "radical_display_style_vertical_gap" else "radical_vertical_gap")
    let sign = tex_delimiter(recipe,body.height + body.depth + gap + metric(c,"radical_rule_thickness"),c,"mord")
    // TeX make_radical uses the selected sign's height as the rule thickness, even in scripts.
    let clearance = gap + max(0.0,sign.depth - body.height - body.depth - gap) / 2.0
    let top = 0.0 - body.height - clearance - sign.height
    let result = bx.compose([{box:sign,x:0.0,y:0.0 - body.height - clearance},
        {box:body,x:sign.width,y:0.0},
        {box:bx.rule(body.width,sign.height,top),x:sign.width,y:0.0}],sign.width + body.width)
    let root = {*:result,height:result.height + sign.height};
    if (n.index == null) root
    else {
        // LaTeX/amsmath r@@t: scriptscript degree, 5mu, -10mu, raised .6*(height-depth).
        let degree = node(n.index,with_style(c,"scriptscript"))^
        let mu = math_quad(c) / 18.0
        let offset = degree.width - 5.0 * mu;
        bx.compose([{box:degree,x:5.0 * mu,y:0.0 - 0.6 * (root.height - root.depth)},
            {box:root,x:offset,y:0.0}],root.width + offset)
    }
}

fn font_radical(n, c) {
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
    let arrow_recipe = if (starts_with(key,"over")) sym.tex_arrow(slice(key,4,len(key)))
        else if (starts_with(key,"under")) sym.tex_arrow(slice(key,5,len(key))) else null;
    if (arrow_recipe != null and c.profile.tex != null) arrow_accent(n,c,arrow_recipe,attached)^
    else {
    let bracket = contains(["overbracket", "underbracket"], key)
    let base = node(n.base, if (bracket) with_style(c,"display")
        else if (key == "underline") c else {*:c, cramped: true})^
    if (contains(["overline", "underline", "underbar"], key)) {
        let above = key == "overline"
        let thickness = metric(c, if (above) "overbar_rule_thickness" else "underbar_rule_thickness")
        let gap = metric(c, if (above) "overbar_vertical_gap" else "underbar_vertical_gap")
        let y = if (above) 0.0 - base.height - gap - thickness else base.depth + gap
        let result = bx.compose([{box: base, x: 0.0, y: 0.0}, {box: bx.rule(base.width, thickness, y), x: 0.0, y: 0.0}], base.width);
        {*:result, height: result.height + (if (above) metric(c, "overbar_extra_ascender") else 0.0),
            depth: result.depth + (if (above) 0.0 else metric(c, "underbar_extra_descender"))}
    } else if (contains(["overbracket", "underbracket", "overlinesegment", "underlinesegment", "overgroup", "undergroup"], key)) {
        let above = starts_with(key, "over")
        let mark = bracket_mark(key, base.width, c)^
        // mathtools inserts .2 of the text symbol font's x-height at the nucleus.
        let gap = if (bracket) 0.2 * metric(with_style(c,"text"),"math_x_height")
            else metric(c,if (above) "overbar_vertical_gap" else "underbar_vertical_gap")
        let y = if (above) 0.0 - base.height - gap - mark.depth
            else base.depth + gap + mark.height
        let result = bx.compose([{box:base, x:0.0, y:0.0}, {box:mark, x:0.0, y:y}], base.width);
        {*:result, limits:if (contains(key, "bracket")) "always" else false}
    } else if (key == "overbrace" or key == "underbrace") {
        let above = key == "overbrace"
        let g = font.glyph(c.profile, ord(if (above) "⏞" else "⏟"))^
        let mark = stretch.glyph(g, base.width, false, scale(c), "mord")^
        let y = if (above) 0.0 - base.height - metric(c, "stretch_stack_gap_above_min") - mark.depth
            else base.depth + metric(c, "stretch_stack_gap_below_min") + mark.height
        let result = bx.compose([{box: base, x: 0.0, y: 0.0}, {box: mark, x: (base.width - mark.width) / 2.0, y: y}], base.width);
        {*:result, limits: "always"}
    } else {
        let ch = sym.get_accent(key)
        if (ch == null) error("math: unsupported accent " ++ key)
        else {
        let designed = font.tex_accent(c.profile,key)^
        let g = if (designed != null) null else font.glyph(c.profile, ord(ch))^
        let wide = starts_with(key, "wide") or contains(key, "arrow") or key == "utilde"
        let mark = if (designed != null) (
            let variants = [for (v in designed.variants where v.advance * text_scale(c) <= base.width) v],
            bx.glyph(if (len(variants) > 0) variants[len(variants) - 1] else designed.variants[0],text_scale(c)))
            else if (contains(key, "harpoon") or key == "Overrightarrow") arrow_shape(key, base.width, c)
            else if (contains(key, "arrow")) stretch.glyph(g, base.width, false, scale(c), "mord")^
            else if (wide) stretch.accent(g, base.width, scale(c))^ else bx.glyph(g, scale(c))
        let x = base.accent - mark.accent
        // below-arrow accents must clear the base's descent instead of its top.
        let y = if (starts_with(key, "under") or key == "utilde" or key == "cedilla") base.depth + metric(c, "underbar_vertical_gap") + mark.height
            else if (contains(key,"harpoon") or key == "Overrightarrow") 0.0 - base.height - metric(c,"overbar_vertical_gap") - mark.depth
            else 0.0 - max(0.0,base.height - (if (designed != null) designed.x_height * text_scale(c)
                else metric(c,"accent_base_height")));
        let scripted = if (attached != null and base.character == true) side_scripts(attached, c, base)^ else base
        let result = bx.compose([{box: scripted, x: 0.0, y: 0.0}, {box: mark, x: x, y: y}], scripted.width, base.type);
        if (attached != null and base.character != true) side_scripts(attached, c, result)^ else result
        }
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
    let designed = sym.tex_arrow(slice(key,1,len(key)));
    if (designed != null and c.profile.tex != null) tex_labelled_arrow(n,c,designed)^
    else {
    let ch = if (contains(key, "leftright")) "↔" else if (contains(key, "left")) "←" else "→"
    let upper_node = n.upper or n.label or n.above or n.over
    let lower_node = n.lower or n.below or n.under
    let upper = node(upper_node, script(c))^
    let lower = node(lower_node, script(c))^
    let recipe = sym.reaction_arrow(key)
    let width = max(n.min_width or 0.0, max(max(upper.width, lower.width) + (if (recipe != null) font.UNITS * scale(c) else 2.0 * metric(c, "space_after_script")),
        if (recipe != null) 1.75 * font.UNITS * scale(c) else 0.0))
    let base = if (recipe != null) paired_arrow(recipe, width, c)^
        else if (contains(["xrightarrow", "xleftarrow", "xleftrightarrow"], key))
            stretch.arrow(font.glyph(c.profile, ord(ch))^, width, scale(c), ch != "←",
                metric(c, "axis_height"), metric(c, "fraction_rule_thickness"))^
        else arrow_shape(key, max(width, 1.75 * font.UNITS * scale(c)), c);
    limits_box(base, if (lower_node != null) lower else null, if (upper_node != null) upper else null, c)
    }
}

fn tex_arrow_fill(recipe, width, c) {
    let parts = [for (ch in [recipe.left,recipe.middle,recipe.right]) (
        let b = bx.glyph(font.tex_arrow_symbol(c.profile,ch,c.style)^,scale(c)),
        // AMS relbar smashes the minus box, retaining the glyph's ink and baseline.
        if (ch == "−") {*:b,height:0.0,depth:0.0} else b)];
    stretch.tex_arrow(parts[0],parts[1],parts[2],width,math_quad(c) / 18.0,dimension("1pt",c) / 65536.0)
}

fn arrow_argument_present(n) {
    let items = if (n is element) content(n) else [];
    // TeX strips one enclosing brace group from an entire delimited optional argument.
    if (n is element and name(n) == 'brack_group' and len(items) == 1 and
        items[0] is element and name(items[0]) == 'group') arrow_argument_present(items[0])
    else n != null and (if (n is element and contains(['group','brack_group'],name(n))) len(items) > 0
        else if (n is array or n is string) len(n) > 0 else true)
}

fn arrow_label(n, c, recipe, kerns) {
    let body = node(n,c)^
    // Math control-space is text-font space, even inside script/scriptscript labels.
    let space = c.profile.delimiter_data.text_space * text_scale(c)
    let sp = dimension("1pt",c) / 65536.0
    let mu = floor(floor(math_quad(c) / sp + 0.5) / 18.0) * sp;
    bx.row([bx.empty(kerns[0] * mu + recipe.spaces[0] * space),body,
        bx.empty(kerns[1] * mu + recipe.spaces[1] * space)])
}

fn tex_labelled_arrow(n, c, recipe) {
    let upper = n.upper or n.label or n.above or n.over
    let lower = n.lower or n.below or n.under
    // ext@arrow measures in explicit scriptstyle, and builds its filler in displaystyle.
    let measure_context = with_style(c,"script")
    let width = max([n.min_width or 0.0,arrow_label(upper,measure_context,recipe,recipe.measure)^.width,
        arrow_label(lower,measure_context,recipe,recipe.measure)^.width])
    let base = tex_arrow_fill(recipe,width,with_style(c,"display"))^
    let forced = sum(recipe.spaces) > 0;
    limits_box(base,
        if (forced or arrow_argument_present(lower)) arrow_label(lower,{*:script(c),cramped:true},recipe,recipe.attach)^ else null,
        if (forced or arrow_argument_present(upper)) arrow_label(upper,script(c),recipe,recipe.attach)^ else null,c)
}

// amsgen compute@ex@: a text-size-dependent point length, not fontdimen5.
fn ams_ex_fuzz(steps, fuzz = 65536.0) => if (steps <= 0) fuzz
    // scan_dimen truncates .97 (63570/65536) times an internal dimension.
    else ams_ex_fuzz(steps - 1,floor(fuzz * 63570.0 / 65536.0))

fn ams_ex(c) {
    let pt = dimension("1pt",c)
    let size = floor(font.UNITS * text_scale(c) / pt * 65536.0 + 0.5)
    let delta = 2.0 * (10.0 * 65536.0 - size)
    let steps = max(0,int(ceil((abs(delta) - 1000.0) / 65536.0)));
    pt / 65536.0 * (if (size > 20.0 * 65536.0) 98304.0 else
        65536.0 + (if (delta > 0.0) -1.0 else 1.0) * (65536.0 - ams_ex_fuzz(steps)))
}

fn arrow_accent(n, c, recipe, attached) {
    // mathpalette selects an explicit, uncramped style for both alignment rows.
    let chosen = with_style(c,c.style)
    let body = node(n.base,chosen)^
    let mark = tex_arrow_fill(recipe,body.width,chosen)^
    let width = max(body.width,mark.width)
    let above = starts_with(command_name(string(n.cmd)),"over")
    let sp = dimension("1pt",c) / 65536.0
    // The macro's 1.3 register multiplier is 85197/65536, truncated to scaled points.
    let gap = floor(ams_ex(c) / sp * 85197.0 / 65536.0) * sp
    let y = if (above) 0.0 - body.height - mark.depth else body.depth + gap + mark.height
    let result = bx.compose([{box:body,x:(width - body.width) / 2.0,y:0.0},
        {box:mark,x:0.0,y:y}],width);
    // The macro ends in mathchoice, so unbraced following scripts get an empty noad.
    // An authored enclosing group instead receives the normal compound-nucleus scripts.
    if (attached != null) bx.row([result,side_scripts(attached,c,bx.empty())^]) else result
}

// Shafts grow with the label; hooks and arrowheads keep their em-sized geometry.
fn arrow_shape(key, width, c) {
    let s = scale(c)
    let head = 200.0 * s
    let half = 110.0 * s
    let hook = contains(key, "hook")
    // An outward cubic reaches three quarters of its control-point offset.
    let hook_inset = if (hook) 0.75 * head else 0.0
    let w = max(width, 700.0 * s) - hook_inset
    let double = contains(key, "Right") or contains(key, "Left")
    let harpoon = contains(key, "harpoon")
    let left = contains(key, "left") or contains(key, "Left")
    let right = contains(key, "right") or contains(key, "Right") or key == "xmapsto"
    let shaft = if (double) [0.0 - 60.0 * s, 60.0 * s] else [0.0]
    let path = <g fill: "none", stroke: "currentColor", 'stroke-width': metric(c, "fraction_rule_thickness"),
        for (y in shaft) <path d: "M0 " ++ string(y) ++ " H" ++ string(w)>
        if (right) <path d: "M" ++ string(w - head) ++ " " ++ string(0.0 - half) ++ " L" ++ string(w) ++ " 0" ++
            (if (harpoon) "" else " L" ++ string(w - head) ++ " " ++ string(half))>
        if (left) <path d: "M" ++ string(head) ++ " " ++ string(if (harpoon) half else 0.0 - half) ++ " L0 0" ++
            (if (harpoon) "" else " L" ++ string(head) ++ " " ++ string(half))>
        if (contains(key, "twohead")) <path d: if (right)
            "M" ++ string(w - 2.0 * head) ++ " " ++ string(0.0 - half) ++ " L" ++ string(w - head) ++ " 0 L" ++ string(w - 2.0 * head) ++ " " ++ string(half)
            else "M" ++ string(2.0 * head) ++ " " ++ string(0.0 - half) ++ " L" ++ string(head) ++ " 0 L" ++ string(2.0 * head) ++ " " ++ string(half)>
        if (hook) <path d: if (right)
            "M0 0 C" ++ string(0.0 - head) ++ " 0 " ++ string(0.0 - head) ++ " " ++ string(0.0 - 2.0 * half) ++ " 0 " ++ string(0.0 - 2.0 * half)
            else "M" ++ string(w) ++ " 0 C" ++ string(w + head) ++ " 0 " ++ string(w + head) ++ " " ++ string(0.0 - 2.0 * half) ++ " " ++ string(w) ++ " " ++ string(0.0 - 2.0 * half)>
        if (contains(key, "mapsto")) <path d: "M0 " ++ string(0.0 - half) ++ " V" ++ string(half)>
    >
    let axis = metric(c, "axis_height");
    bx.make(<g 'data-math-kind':"extensible-arrow", transform:"translate(" ++ string(if (right) hook_inset else 0.0) ++ " " ++ string(0.0 - axis) ++ ")", path>,
        w + hook_inset, axis + 2.0 * half, max(0.0, half - axis), "mrel")
}

fn bracket_mark(key, width, c) {
    if (contains(["overbracket", "underbracket"], key)) {
        // mathtools' rule is ht(braceld); end height is .7 * fontdimen5(textfont2).
        if (c.profile.tex == null) error("math: bracket requires TeX extension-font metrics")
        else {
            let text_context = with_style(c,"text")
            let rule = c.profile.tex.bracket_rule * text_scale(c)
            let tick = 0.7 * metric(text_context,"math_x_height")
            let above = key == "overbracket"
            let bar_y = if (above) 0.0 - rule else 0.0
            let tick_y = if (above) 0.0 else 0.0 - tick;
            bx.make(<g fill:"currentColor",
                <rect x:0.0, y:bar_y, width:width, height:rule>
                for (x in [0.0,width - rule]) <rect x:x, y:tick_y, width:rule, height:tick>
            >,width,if (above) rule else tick,if (above) tick else rule)
        }
    } else {
    let h = 180.0 * scale(c)
    let sign = if (starts_with(key, "under")) -1.0 else 1.0
    let tick = sign * h
    let d = if (contains(key, "group")) "M0 " ++ string(tick) ++ " Q" ++ string(width / 2.0) ++ " " ++ string(0.0 - tick) ++ " " ++ string(width) ++ " " ++ string(tick)
        else "M0 " ++ string(tick) ++ " V0 H" ++ string(width) ++ " V" ++ string(tick);
    bx.make(<path d:d, fill:"none", stroke:"currentColor", 'stroke-width':metric(c,"fraction_rule_thickness")>,
        width, if (sign < 0.0) h else 0.0, if (sign > 0.0) h else 0.0)
    }
}

fn transformed(n, c) {
    let key = command_name(string(n.cmd))
    let base = node(n.body, c)^
    if (contains(key, "reflectbox")) {*:base, character:false, body:<g 'data-math-kind':"reflection",
        transform:"translate(" ++ string(base.width) ++ " 0) scale(-1 1)", base.body>}
    else if (key == "raisebox") {
        let result = bx.shifted(base, 0.0, 0.0 - dimension(string(n.raise), c), base.width);
        {*:result, height:if (n.height != null) dimension(n.height,c) else result.height,
            depth:if (n.depth != null) dimension(n.depth,c) else result.depth}
    } else if (key == "vcenter") center_axis(base, c)
    else if (key == "pmb") bx.compose([for (dx in [0.0, 25.0, 50.0]) {box:base, x:dx * scale(c), y:0.0}], base.width)
    else if (key == "angl") {
        let pad = 100.0 * scale(c)
        let width = base.width + pad
        let top = 0.0 - base.height - pad;
        {*:base, width:width, height:base.height + pad, character:false,
            body:<g base.body; <path d:"M0 " ++ string(top) ++ " H" ++ string(width) ++ " V" ++ string(base.depth),
                fill:"none", stroke:"currentColor", 'stroke-width':metric(c,"fraction_rule_thickness")>>}
    }
    else if (key == "phase") {
        let pad = 350.0 * scale(c)
        let raised = bx.shifted(base, pad, 0.0, base.width + pad)
        let path = "M0 " ++ string(base.depth) ++ " L" ++ string(pad) ++ " " ++ string(0.0 - base.height) ++
            " M0 " ++ string(base.depth) ++ " H" ++ string(raised.width);
        {*:raised, body:<g <path d:path, fill:"none", stroke:"currentColor", 'stroke-width':metric(c,"fraction_rule_thickness")> raised.body>}
    } else {
        let x = base.width
        let top = 0.0 - base.height
        let bottom = base.depth
        let d = if (key == "sout") "M0 " ++ string(0.0 - metric(c,"axis_height")) ++ " H" ++ string(x)
            else (if (key != "bcancel") "M0 " ++ string(bottom) ++ " L" ++ string(x) ++ " " ++ string(top) else "") ++
                (if (key != "cancel") " M0 " ++ string(top) ++ " L" ++ string(x) ++ " " ++ string(bottom) else "");
        {*:base, character:false, body:<g 'data-math-kind':key, base.body;
            <path d:d, fill:"none", stroke:"currentColor", 'stroke-width':metric(c,"fraction_rule_thickness")>>}
    }
}

fn equation_tag(n, c) {
    let body = node(n.body, {*:c, style:"text"})^;
    if (n.starred) body else bx.row([character("(",c,"mord")^, body, character(")",c,"mord")^])
}

fn modulo_items(n, c) {
    let key = command_name(string(n.cmd))
    let body = if (n.body is element and name(n.body) == 'group') util.content_items(n.body)
        else if (n.body == null) [] else [n.body]
    let label = <style_command cmd:"\\mathrm",arg:"mod">
    let script_style = c.style == "script" or c.style == "scriptscript"
    // amsmath.dtx: bmod has 5mu kerns and nonscript -medmuskip; pod/mod test display mode, not style.
    let binary_gap = <skip_command cmd:"\\mkern",value:if (script_style) "5mu" else "1mu">
    let leading = <skip_command cmd:"\\mkern",value:if (c.display_mode) "18mu" else if (key == "mod") "12mu" else "8mu">;
    if (key == "bmod") [binary_gap,<math_atom atom:"mbin",body:label>,binary_gap]
    else [leading,*if (key != "mod") ["("] else [],
        *if (key != "pod") [label,<skip_command cmd:"\\mkern",value:"6mu">] else [],*body,
        *if (key != "mod") [")"] else []]
}

fn diagram_arrow(n, c) {
    let dir = string(n.direction)
    let upper = node(n.upper,script(c))^
    let lower = node(n.lower,script(c))^
    let w = max(3000.0 * scale(c), max(upper.width,lower.width) + 500.0 * scale(c))
    if (dir == "<" or dir == ">") arrow(<extended_arrow cmd:if (dir == "<") "\\xleftarrow" else "\\xrightarrow",
        upper:n.upper, lower:n.lower, min_width:w>,c)^
    else if (dir == "=") bx.make(<g stroke:"currentColor", 'stroke-width':metric(c,"fraction_rule_thickness"),
        <path d:"M0 -200 H" ++ string(w) ++ " M0 -300 H" ++ string(w)>>,w,300.0 * scale(c),0.0)
    else if (dir == ".") bx.empty(w)
    else if (not contains(["A","V","|"],dir)) error("math: unsupported CD arrow " ++ dir)
    else {
        let h = 1600.0 * scale(c)
        let x = max(upper.width,lower.width) + 200.0 * scale(c)
        let d = if (dir == "|") "M" ++ string(x - 50.0 * scale(c)) ++ " " ++ string(0.0 - h / 2.0) ++ " v" ++ string(h) ++
            " M" ++ string(x + 50.0 * scale(c)) ++ " " ++ string(0.0 - h / 2.0) ++ " v" ++ string(h)
            else "M" ++ string(x) ++ " " ++ string(0.0 - h / 2.0) ++ " v" ++ string(h) ++
                " M" ++ string(x - 120.0 * scale(c)) ++ " " ++ string(if (dir == "A") 0.0 - h / 2.0 + 200.0 * scale(c) else h / 2.0 - 200.0 * scale(c)) ++
                " L" ++ string(x) ++ " " ++ string(if (dir == "A") 0.0 - h / 2.0 else h / 2.0) ++
                " L" ++ string(x + 120.0 * scale(c)) ++ " " ++ string(if (dir == "A") 0.0 - h / 2.0 + 200.0 * scale(c) else h / 2.0 - 200.0 * scale(c))
        let mark = bx.make(<path d:d, fill:"none", stroke:"currentColor", 'stroke-width':metric(c,"fraction_rule_thickness")>,x + lower.width + 200.0 * scale(c),h / 2.0,h / 2.0);
        bx.compose([{box:mark,x:0.0,y:0.0}, {box:upper,x:x - 200.0 * scale(c) - upper.width,y:0.0},
            {box:lower,x:x + 200.0 * scale(c),y:0.0}],mark.width)
    }
}

fn paired_arrow(recipe, width, c) {
    // mhchem centers the short harpoon with a half-em inset on each side.
    let inset = 0.5 * font.UNITS * scale(c)
    let upper = center_axis(stretch.arrow(font.glyph(c.profile, ord(recipe.upper))^,
        width - 2.0 * recipe.upper_short * inset, scale(c), true,
        metric(c, "axis_height"), metric(c, "fraction_rule_thickness"))^, c)
    let lower = center_axis(stretch.arrow(font.glyph(c.profile, ord(recipe.lower))^,
        width - 2.0 * recipe.lower_short * inset, scale(c), false,
        metric(c, "axis_height"), metric(c, "fraction_rule_thickness"))^, c)
    let separation = 0.2 * font.UNITS * scale(c);
    bx.compose([{box: upper, x: (width - upper.width) / 2.0, y: 0.0 - separation},
        {box: lower, x: (width - lower.width) / 2.0, y: separation}], width, "mrel")
}

fn matrix(n, c) {
    let raw_key = command_name(string(n.name or n.cmd or "matrix"))
    let key = if (ends_with(raw_key, "*")) slice(raw_key, 0, len(raw_key) - 1) else raw_key
    let aligned = contains(["aligned", "alignedat", "align", "alignat", "split"], key)
    let display = aligned or contains(["gathered", "gather", "equation", "dcases"], key)
    let small = key == "smallmatrix" or key == "subarray"
    let child = with_style(c, if (small) "script" else if (display) "display" else "text")
    let items = util.content_items(n.body)
    let rows = util.parse_rows(items, 0, len(items), [], [], [])
    // AMS starts each right-hand cell with an empty ordinary atom for relation glue.
    let cells = [for (row in rows) [for (col, cell in row.cells)
        group([*if (aligned and col % 2 == 1) [<group>] else [],
            *[for (item in cell.items where not (item is element and name(item) == 'array_rule')) item]], child)^]]
    let count = max([0, *[for (row in cells) len(row)]])
    let widths = [for (col in 0 to (count - 1)) max([0.0, *[for (row in cells) row[col].width or 0.0]])]
    // Array struts keep simple rows apart: 12pt baseline with 70/30 height/depth at 10pt.
    let row_skip = (if (small) 0.6 else 1.2) * font.UNITS * text_scale(c)
    let heights = [for (row in cells) max([0.7 * row_skip, *[for (cell in row) cell.height]])]
    let depths = [for (row in cells) max([0.3 * row_skip, *[for (cell in row) cell.depth]])]
    let gap_x = font.UNITS * scale(child)
    let gaps = [for (col in 0 to (count - 1)) if (col == 0) 0.0
        else if (aligned) (if (col % 2 == 1 or key == "alignedat" or key == "alignat") 0.0 else 2.0 * gap_x) else gap_x]
    let declared = [for (ch in split(string(n.columns or ""), "") where contains("lcr", ch)) ch]
    let aligns = [for (col in 0 to (count - 1)) declared[col] or n.alignment or
        (if (aligned) (if (col % 2 == 0) "r" else "l") else if (key == "cases" or key == "rcases") "l" else "c")]
    let gap_y = if (aligned or contains(["gathered", "gather"], key)) 0.3 * font.UNITS * text_scale(c) else 0.0
    let extra_gaps = [for (row in rows) dimension(row.gap or "0em", child)]
    let total = sum(heights) + sum(depths) + gap_y * max(0, len(rows) - 1) + sum(extra_gaps)
    let top = 0.0 - total / 2.0 - metric(c, "axis_height")
    let entries = [for (r, row in cells, col, cell in row) {
        box: {*:cell, body: <g 'data-column-align': aligns[col], cell.body>},
        x: sum(slice(widths, 0, col)) + sum(slice(gaps, 0, col + 1)) +
            (if (aligns[col] == "r") widths[col] - cell.width else if (aligns[col] == "l") 0.0 else (widths[col] - cell.width) / 2.0),
        y: top + sum(slice(heights, 0, r)) + sum(slice(depths, 0, r)) + float(r) * gap_y +
            sum(slice(extra_gaps, 0, r)) + heights[r]}]
    let width = sum(widths) + sum(gaps)
    let thickness = metric(c,"fraction_rule_thickness")
    let column_spec = split(string(n.columns or ""), "")
    let verticals = [for (i,ch in column_spec where ch == "|" or ch == ":") (
        let col = len([for (v in slice(column_spec,0,i) where contains("lcr",v)) v]),
        let boundary = sum(slice(widths,0,col)) + sum(slice(gaps,0,col + 1)),
        let repeated = if (i > 0 and column_spec[i - 1] == "|") 150.0 * scale(c) else 0.0,
        <path d:"M" ++ string(boundary - (if (col > 0 and col < count) gaps[col] / 2.0 else 0.0) + repeated) ++ " " ++ string(top) ++ " v" ++ string(total),
            fill:"none", stroke:"currentColor", 'stroke-width':thickness, 'stroke-dasharray':if (ch == ":") "150 100" else "none">)]
    let horizontals = [for (r,row in rows, i,mark in [
        *[for (cell in row.cells, item in cell.items where item is element and name(item) == 'array_rule') {item:item,trailing:false}],
        *[for (item in row.trailing_rules) {item:item,trailing:true}]])
        <path d:"M0 " ++ string(top + sum(slice(heights,0,r)) + sum(slice(depths,0,r)) + float(r) * gap_y +
            sum(slice(extra_gaps,0,r)) + (if (mark.trailing) heights[r] + depths[r] else 0.0) + float(i) * 3.0 * thickness) ++ " H" ++ string(width),
            fill:"none", stroke:"currentColor", 'stroke-width':thickness,
            'stroke-dasharray':if (mark.item.cmd == "\\hdashline") "150 100" else "none">]
    let table0 = bx.compose(entries, width, "minner")
    let table = if (len(verticals) + len(horizontals) == 0) table0 else
        {*:table0, body:<g table0.body; for (line in verticals) line; for (line in horizontals) line>}
    let result = {*:table, body: <g 'data-math-kind': "matrix", table.body>}
    let fences = match key {
        case "pmatrix": ["(", ")"]
        case "bmatrix": ["[", "]"]
        case "Bmatrix": ["{", "}"]
        case "vmatrix": ["|", "|"]
        case "Vmatrix": ["‖", "‖"]
        case "cases": ["{", "."]
        case "dcases": ["{", "."]
        case "rcases": [".", "}"]
        default: null
    }
    if (fences != null) fence_box(result, fences[0], fences[1], c)^ else result
}

fn phantom(n, c) {
    let base = node(n.content, c)^
    let key = command_name(string(n.cmd));
    // LaTeX finph@nt/finsm@sh emits an ordinary box, without the source noad's glyph/limit metadata.
    bx.make(if (key == "smash") base.body else <g>,
        if (key == "vphantom") 0.0 else base.width,
        if (key == "hphantom" or (key == "smash" and util.text_of(n.options) != "b")) 0.0 else base.height,
        if (key == "hphantom" or (key == "smash" and util.text_of(n.options) != "t")) 0.0 else base.depth)
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
    if (n.cmd == "\\colorbox" or n.cmd == "\\fcolorbox") {
        let pad = dimension("3pt",c)
        let result = bx.shifted(base,pad,0.0,base.width + 2.0 * pad);
        {*:result, height:base.height + pad, depth:base.depth + pad,
            body: <g <rect x:0, y:0.0 - base.height - pad, width:result.width,
                height:base.height + base.depth + 2.0 * pad, fill:color,
                stroke:n.border_color or "none", 'stroke-width':metric(c,"fraction_rule_thickness")> result.body>}
    } else ({*:base, body: <g fill: color, style: "color:" ++ color, base.body>})
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
    {*:bx.empty(if (em != null) em * (if (contains(["quad", "qquad", "enspace"], key)) font.UNITS * text_scale(c) else math_quad(c)) else dimension(string(n.value or "0em"), c)), type: "skip"}
}

fn dimension(raw, c) {
    let dim = util.dimension_from_string(raw)
    let units = match dim.unit {
        case "em": font.UNITS * text_scale(c)
        case "ex": c.profile.font_metrics.x_height * text_scale(c)
        case "mu": math_quad(c) / 18.0
        case "pt": font.UNITS * 96.0 / 72.27 / c.pixels_per_em
        case "bp": font.UNITS * 96.0 / 72.0 / c.pixels_per_em
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
    bx.rule(width,height,0.0 - height - dimension(string(n.raise or "0pt"),c))
}

fn negated(n, c) {
    let base = node(n.base or n.target or content(n)^, c)^
    let slash = character("/", {*:c, variant: "normal"}, "mrel")^
    bx.compose([{box: base, x: 0.0, y: 0.0}, {box: slash, x: (base.width - slash.width) / 2.0, y: 0.0}], base.width, base.type)
}
