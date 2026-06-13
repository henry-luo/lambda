// math/render.ls — Core renderer: AST node → box tree dispatch
// This is the heart of the math rendering pipeline.
// It dispatches on AST element tag names and delegates to atom-specific renderers.

import box: .box
import ctx: .context
import css: .css
import met: .metrics
import sym: .symbols
import sp_table: .spacing_table
import fraction: .atoms.fraction
import scripts: .atoms.scripts
import spacing: .atoms.spacing
import style: .atoms.style
import color: .atoms.color
import arr_mod: .atoms.array
import enclose: .atoms.enclose
import delims: .atoms.delimiters

// ============================================================
// Main render dispatch
// ============================================================

// render an AST node into a box
// node: a Lambda element from the tree-sitter-latex-math parser
// context: rendering context (style, font, color, etc.)
pub fn render_node(node, context) {
    if (node is string) render_text(node, context)
    else if (node == null) box.text_box("", null, "ord")
    else if (not (node is element)) box.text_box(string(node), null, "ord")
    else dispatch_element(node, context)
}

// dispatch on element tag name — called when node is known to be an element
fn dispatch_element(node, context) {
    let tag = name(node)
    match tag {
        case 'math':            render_math_root(node, context)
        case 'group':           render_group(node, context)
        case 'brack_group':     render_group(node, context)
        case 'symbol':          render_symbol(node, context)
        case 'symbol_command':  render_symbol_command(node, context)
        case 'number':          render_number(node, context)
        case 'digit':           render_number(node, context)
        case 'operator':        render_operator(node, context)
        case 'relation':        render_relation(node, context)
        case 'punctuation':     render_punct(node, context)
        case 'subsup':          scripts.render(node, context, render_node)
        case 'fraction':        fraction.render(node, context, render_node)
        case 'binomial':        fraction.render(node, context, render_node)
        case 'frac_like':       render_frac_like(node, context)
        case 'command':         render_command(node, context)
        case 'text_command':    render_text_command(node, context)
        case 'style_command':   style.render(node, context, render_node)
        case 'textstyle_command': render_textstyle_command(node, context)
        case 'space_command':   spacing.render(node, context, render_node)
        case 'hspace_command':  spacing.render(node, context, render_node)
        case 'skip_command':    spacing.render(node, context, render_node)
        case 'spacing_command': spacing.render(node, context, render_node)
        case 'color_command':   color.render(node, context, render_node)
        case 'genfrac':         fraction.render(node, context, render_node)
        case 'infix_frac':      fraction.render(node, context, render_node)
        case 'overunder_command': render_overunder(node, context)
        case 'big_operator':    render_big_op(node, context)
        case 'accent':          render_accent(node, context)
        case 'delimiter_group': render_delimiter_group(node, context)
        case 'sized_delimiter': render_sized_delim(node, context)
        case 'environment':     arr_mod.render_env(node, context, render_node)
        case 'matrix_command':  arr_mod.render_matrix(node, context, render_node)
        case 'env_body':        render_default(node, context)
        case 'matrix_body':     render_default(node, context)
        case 'phantom_command': enclose.render_phantom(node, context, render_node)
        case 'box_command':     enclose.render_box(node, context, render_node)
        case 'rule_command':    enclose.render_rule(node, context, render_node)
        case 'radical':         render_radical(node, context)
        case 'middle_delim':    render_middle_delim(node, context)
        case 'limits_modifier': box.text_box("", null, "ord")
        default:                render_default(node, context)
    }
}

// ============================================================
// Math root
// ============================================================

fn render_math_root(node, context) {
    let children = render_children(node, context)
    let spaced = apply_spacing(children, context)
    box.hbox(spaced)
}

// ============================================================
// Groups
// ============================================================

fn render_group(node, context) {
    let children = render_children(node, context)
    transparent_hbox(children)
}

// ============================================================
// Leaf nodes: symbols, numbers, operators
// ============================================================

fn render_symbol(node, context) {
    let text = get_text(node)
    let cls = css.font_class(context.font)
    box.text_box(text, cls, "mord")
}

fn render_symbol_command(node, context) {
    let cmd_text = get_text(node)
    let unicode = sym.lookup_symbol(cmd_text)
    let display_text = if (unicode != null) unicode else cmd_text
    let atom_type = sym.classify_symbol(cmd_text)
    let cls = symbol_font_class(cmd_text, context)
    box.text_box(display_text, cls, atom_type)
}

fn render_number(node, context) {
    let text = get_text(node)
    box.text_box(text, css.CMR, "mord")
}

fn render_operator(node, context) {
    let text = get_text(node)
    let atom_type = if (text == "(" or text == "[" or text == "{") "mopen"
        else if (text == ")" or text == "]" or text == "}") "mclose"
        else "mbin"
    box.text_box(operator_display_text(text), css.CMR, atom_type)
}

fn render_relation(node, context) {
    let text = get_text(node)
    box.text_box(text, css.CMR, "mrel")
}

fn render_punct(node, context) {
    let text = get_text(node)
    let atom_type = if (text == "(" or text == "[" or text == "{") "mopen"
        else if (text == ")" or text == "]" or text == "}") "mclose"
        else "mpunct"
    box.text_box(text, css.CMR, atom_type)
}

fn operator_display_text(text) {
    if (text == "-") "−" else text
}

fn render_text(text, context) {
    let cls = if (is_plain_number_text(text)) css.CMR else css.font_class(context.font)
    box.text_box(text, cls, "mord")
}

// ============================================================
// Commands (generic \cmd)
// ============================================================

fn render_command(node, context) {
    let cmd_text = get_text(node)
    let unicode = sym.lookup_symbol(cmd_text)
    if (unicode != null) {
        let atom_type = sym.classify_symbol(cmd_text)
        box.text_box(unicode, symbol_font_class(cmd_text, context), atom_type)
    } else {
        let name_str = if (len(cmd_text) > 0 and slice(cmd_text, 0, 1) == "\\")
             slice(cmd_text, 1, len(cmd_text)) else cmd_text
        if (name_str == "pdiff") {
            render_pdiff(node, context)
        } else if (name_str == "colorbox") {
            render_colorbox_command(node, context)
        } else if (name_str == "placeholder") {
            render_placeholder(context)
        } else if (name_str == "rule") {
            render_generic_rule_command(node, context)
        } else if (is_generic_box_command(name_str)) {
            render_generic_box_command(node, context, name_str)
        } else {
            let op_name = sym.get_operator_name(name_str)
            if (op_name != null) {
            box.with_class(box.text_box(op_name, css.CMR, "mop"), css.OP_GROUP)
            } else {
                box.text_box(cmd_text, css.ERROR, "mord")
            }
        }
    }
}

fn render_generic_rule_command(node, context) {
    let n = len(node)
    let has_shift = n > 0 and node[0] is element and name(node[0]) == 'brack_group'
    let shift = if (has_shift) plain_text(node[0]) else null
    let width_idx = if (has_shift) 1 else 0
    let height_idx = width_idx + 1
    let width = if (width_idx < n) plain_text(node[width_idx]) else null
    let height = if (height_idx < n) plain_text(node[height_idx]) else width
    enclose.render_rule({width: width, height: height, shift: shift}, context, render_node)
}

fn render_placeholder(context) => {
    element: <span style: "height:0;display:inline-block;font-size: 50%"; "\u00A0">,
    height: 0.0,
    depth: 0.0,
    width: 0.0,
    type: "mord",
    italic: 0.0,
    skew: 0.0
}

fn is_generic_box_command(name_str) {
    name_str == "bbox" or name_str == "boxed" or name_str == "fbox" or
    name_str == "llap" or name_str == "rlap" or name_str == "clap" or
    name_str == "mathllap" or name_str == "mathrlap" or name_str == "mathclap"
}

fn render_generic_box_command(node, context, name_str) {
    let content = if (len(node) > 0) generic_box_content_arg(node, name_str) else null
    let cmd = if (starts_with_math_lap(name_str))
        "\\" ++ slice(name_str, 4, len(name_str))
    else "\\" ++ name_str
    let synth = {cmd: cmd, content: content}
    enclose.render_box(synth, context, render_node)
}

fn generic_box_content_arg(node, name_str) {
    if (name_str == "bbox" and len(node) > 1) node[1]
    else node[len(node) - 1]
}

fn starts_with_math_lap(name_str) {
    name_str == "mathllap" or name_str == "mathrlap" or name_str == "mathclap"
}

fn render_pdiff(node, context) {
    let n = len(node)
    let func_node = if (n > 0) node[0] else null
    let var_node = if (n > 1) node[1] else null
    let numer_box = box.hbox([partial_box(), render_node(func_node, context)])
    let denom_box = box.hbox([partial_box(), render_node(var_node, context)])
    fraction.render_boxes(numer_box, denom_box, context)
}

fn render_colorbox_command(node, context) {
    let n = len(node)
    let color_arg = if (n > 0) node[0] else null
    let content_arg = if (n > 1) node[1] else null
    let bg = color.resolve_background_raw(plain_text(color_arg))
    let content_box = if (content_arg != null)
        render_colorbox_content(content_arg, context)
        else box.text_box("", css.TEXT, "mord")
    color.with_background(content_box, bg)
}

fn symbol_font_class(cmd_text, context) {
    let name_str = if (len(cmd_text) > 0 and slice(cmd_text, 0, 1) == "\\")
        slice(cmd_text, 1, len(cmd_text)) else cmd_text
    if (name_str == "alpha") "lcGreek lm_mathit"
    else if (name_str == "Gamma" or name_str == "Delta" or name_str == "Theta" or
             name_str == "Lambda" or name_str == "Pi" or name_str == "Sigma" or
             name_str == "Upsilon" or name_str == "Phi" or name_str == "Psi" or
             name_str == "Omega") css.CMR
    else if (name_str == "blacksquare" or name_str == "blacktriangle") css.AMS
    else css.font_class(context.font)
}

fn render_colorbox_content(content_arg, context) {
    if (content_arg is string) box.text_box(string(content_arg), css.TEXT, "mord")
    else
        (let children = render_children(content_arg, context),
         let spaced = apply_spacing(children, context),
         transparent_hbox(spaced))
}

fn partial_box() => {
    element: "∂",
    height: 0.7,
    depth: 0.08,
    width: 0.45,
    type: "mord",
    italic: 0.0,
    skew: 0.0
}

// ============================================================
// Text command (\text{...})
// ============================================================

fn render_text_command(node, context) {
    let content = if (node.content != null) get_text(node.content) else ""
    box.text_box(content, css.TEXT, "mord")
}

// textstyle_command fallback (for CST nodes not yet converted by C++)
fn render_textstyle_command(node, context) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""
    let is_text = (contains(cmd, "text") or contains(cmd, "mbox") or contains(cmd, "hbox"))
    if (is_text) {
        let content = if (node.content != null) get_text(node.content)
            else if (node.arg != null) get_text(node.arg) else ""
        box.text_box(content, css.TEXT, "mord")
    } else {
        style.render(node, context, render_node)
    }
}

// ============================================================
// Overunder commands (\overset, \underset)
// ============================================================

fn render_overunder(node, context) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""
    let base_box = if (node.base != null) render_node(node.base, context)
        else box.text_box("", null, "ord")
    let anno_box = if (node.annotation != null) render_node(node.annotation, ctx.sup_context(context))
        else box.text_box("", null, "ord")

    if (cmd == "\\overset" or cmd == "\\overline" or cmd == "\\overbrace")
        box.vbox([
            {box: anno_box, shift: 0.0 - base_box.height - anno_box.depth - 0.1},
            {box: base_box, shift: 0.0}
        ])
    else
        box.vbox([
            {box: base_box, shift: 0.0},
            {box: anno_box, shift: base_box.depth + anno_box.height + 0.1}
        ])
}

// ============================================================
// Big operators (\sum, \prod, \int)
// ============================================================

fn render_big_op(node, context) {
    let cmd = if (node.op != null) string(node.op) else ""
    let unicode = sym.lookup_symbol(cmd)
    let display_text = if (unicode != null) unicode else cmd
    let op_scale = if (ctx.is_display(context)) 1.5 else 1.0
    let op_box = box.text_box(display_text, css.CMR, "mop")
    let scaled_op = box.with_scale(op_box, op_scale)

    let has_lower = node.lower != null
    let has_upper = node.upper != null

    if (not has_lower and not has_upper) scaled_op
    else render_big_op_with_limits(node, context, scaled_op, has_lower, has_upper)
}

fn render_big_op_with_limits(node, context, scaled_op, has_lower, has_upper) {
    let lower_box = if (has_lower)
        render_node(node.lower, ctx.sub_context(context)) else null
    let upper_box = if (has_upper)
        render_node(node.upper, ctx.sup_context(context)) else null

    if (ctx.is_display(context))
        render_big_op_display(scaled_op, lower_box, upper_box, has_lower, has_upper)
    else
        render_big_op_inline(scaled_op, lower_box, upper_box, has_lower, has_upper)
}

fn render_big_op_display(scaled_op, lower_box, upper_box, has_lower, has_upper) {
    let parts = [{box: scaled_op, shift: 0.0}]
    let parts2 = if (has_upper)
        [{box: upper_box, shift: 0.0 - scaled_op.height - 0.1}] ++ parts
    else parts
    let parts3 = if (has_lower)
        parts2 ++ [{box: lower_box, shift: scaled_op.depth + 0.1}]
    else parts2
    box.vbox(parts3)
}

fn render_big_op_inline(scaled_op, lower_box, upper_box, has_lower, has_upper) {
    let sub_parts = if (has_lower)
        [box.with_class(lower_box, css.MSUBSUP)] else []
    let sup_parts = if (has_upper)
        [box.with_class(upper_box, css.MSUBSUP)] else []
    box.hbox([scaled_op] ++ sub_parts ++ sup_parts)
}

// ============================================================
// Accents (\hat, \vec, etc.)
// ============================================================

fn render_accent(node, context) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""
    let accent_key = if (len(cmd) > 0 and slice(cmd, 0, 1) == "\\")
        slice(cmd, 1, len(cmd)) else cmd
    if (accent_key == "overline" or accent_key == "underline")
        render_line_accent(node, context, accent_key)
    else
        render_glyph_accent(node, context, accent_key)
}

fn render_glyph_accent(node, context, accent_key) {
    let accent_text = accent_display_text(accent_key)
    let accent_height = accent_body_height(accent_key)
    let accent_cls = if (accent_key == "vec")
        css.classes([css.ACCENT_BODY, "lm_accent-combining-char"])
        else css.ACCENT_BODY

    if (node.base == null)
        render_missing_base_accent(accent_key, accent_text, accent_cls, accent_height)
    else
        (let base_box = render_accent_base(node.base, context),
         if (base_box.width <= 0.8)
            render_simple_accent(accent_key, base_box, accent_text, accent_cls, accent_height)
         else
            render_wide_accent(accent_key, base_box, accent_text, accent_cls, accent_height))
}

fn render_line_accent(node, context, accent_key) {
    let base_box = if (node.base != null) render_accent_base(node.base, context)
        else box.text_box("□", css.CMR, "mord")
    let tall = line_accent_is_tall(base_box)
    if (accent_key == "underline") {
        if (tall) render_underline_tall(base_box)
        else render_underline_simple(base_box)
    } else {
        if (tall) render_overline_tall(base_box)
        else if (base_box.width <= 0.8) render_overline_simple(base_box)
        else render_overline_wide(base_box)
    }
}

fn render_overline_simple(base_box) {
    let base_elements = box.elements_of(base_box)
    let el = <span class: "overline";
        <span class: css.VLIST_T;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:0.64em";
                    <span style: "top:-3em";
                        <span class: css.PSTRUT, style: "height:3em">
                        <span style: "height:0.44em;display:inline-block";
                            for (el in base_elements) el
                        >
                    >
                    <span style: "top:-3.55em";
                        <span class: css.PSTRUT, style: "height:3em">
                        <span class: "overline-line", style: "height:0.04em;display:inline-block">
                    >
                >
            >
        >
    >
    line_accent_box(el, 0.64, 0.0, base_box.width, 0.0, 0.64, true)
}

fn render_overline_wide(base_box) {
    let base_elements = box.elements_of(base_box)
    let el = <span class: "overline";
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:0.64em";
                    <span style: "top:-3em";
                        <span class: css.PSTRUT, style: "height:3em">
                        <span style: "height:0.63em;display:inline-block";
                            for (el in base_elements) el
                        >
                    >
                    <span style: "top:-3.55em";
                        <span class: css.PSTRUT, style: "height:3em">
                        <span class: "overline-line", style: "height:0.04em;display:inline-block">
                    >
                >
                <span class: css.VLIST_S; "\u200B">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:0.2em">
            >
        >
    >
    line_accent_box(el, 0.64, 0.2, base_box.width, 0.2, 0.84, false)
}

fn render_overline_tall(base_box) {
    let base_elements = box.elements_of(base_box)
    let el = <span class: "overline";
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:1.35em";
                    <span style: "top:-3.14em";
                        <span class: css.PSTRUT, style: "height:3.15em">
                        <span style: "height:1.84em;display:inline-block";
                            for (el in base_elements) el
                        >
                    >
                    <span style: "top:-4.4em";
                        <span class: css.PSTRUT, style: "height:3.15em">
                        <span class: "overline-line", style: "height:0.04em;display:inline-block">
                    >
                >
                <span class: css.VLIST_S; "\u200B">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:0.69em">
            >
        >
    >
    line_accent_box(el, 1.35, 0.69, base_box.width, 0.69, 2.04, false)
}

fn render_underline_simple(base_box) {
    let base_elements = box.elements_of(base_box)
    let el = <span class: "underline";
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:0.7em";
                    <span style: "top:-2.84em";
                        <span class: css.PSTRUT, style: "height:3em">
                        <span class: "underline-line", style: "height:0.04em;display:inline-block">
                    >
                    <span style: "top:-3em";
                        <span class: css.PSTRUT, style: "height:3em">
                        <span style: "height:0.7em;display:inline-block";
                            for (el in base_elements) el
                        >
                    >
                >
                <span class: css.VLIST_S; "\u200B">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:0.21em">
            >
        >
    >
    line_accent_box(el, 0.7, 0.21, base_box.width, 0.21, 0.91, false)
}

fn render_underline_tall(base_box) {
    let base_elements = box.elements_of(base_box)
    let el = <span class: "underline";
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:1.15em";
                    <span style: "top:-2.29em";
                        <span class: css.PSTRUT, style: "height:3.15em">
                        <span class: "underline-line", style: "height:0.04em;display:inline-block">
                    >
                    <span style: "top:-3.14em";
                        <span class: css.PSTRUT, style: "height:3.15em">
                        <span style: "height:1.84em;display:inline-block";
                            for (el in base_elements) el
                        >
                    >
                >
                <span class: css.VLIST_S; "\u200B">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:0.89em">
            >
        >
    >
    line_accent_box(el, 1.15, 0.89, base_box.width, 0.88, 2.24, false)
}

fn line_accent_box(el, h, d, w, render_d, render_total_value, suppress_text_depth) => {
    element: el,
    height: h,
    depth: d,
    render_height: h,
    render_depth: render_d,
    render_total: render_total_value,
    width: w,
    type: "mord",
    italic: 0.0,
    skew: 0.0,
    suppress_hbox_text_depth: suppress_text_depth
}

fn line_accent_is_tall(base_box) {
    let total = if (base_box.render_total != null) base_box.render_total
        else ((if (base_box.render_height != null) base_box.render_height else base_box.height) +
              (if (base_box.render_depth != null) base_box.render_depth else base_box.depth))
    (total > 1.0)
}

fn render_accent_base(base_node, context) {
    if (base_node is element and (name(base_node) == 'group' or name(base_node) == 'brack_group')) {
        let children = render_children(base_node, context)
        transparent_hbox(apply_spacing(children, context))
    } else {
        render_node(base_node, context)
    }
}

fn render_simple_accent(accent_key, base_box, accent_text, accent_cls, accent_height) {
    let base_elements = box.elements_of(base_box)
    let accent_top = if (accent_key == "tilde") -3.35 else -3.0
    let margin_left = if (accent_key == "dot") 0.15 else 0.04
    let el = <span class: css.VLIST_T;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ fmt_accent_em(accent_height);
                <span style: "top:-3em";
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: "height:0.44em;display:inline-block";
                        for (el in base_elements) el
                    >
                >
                <span class: css.CENTER, style: "top:" ++ fmt_accent_em(accent_top) ++ ";margin-left:" ++ fmt_accent_em(margin_left);
                    <span class: css.PSTRUT, style: "height:3em">
                    <span class: accent_cls, style: "height:" ++ fmt_accent_em(accent_height) ++ ";display:inline-block"; accent_text>
                >
            >
        >
    >
    accent_box(el, accent_height, 0.0, base_box.width)
}

fn render_wide_accent(accent_key, base_box, accent_text, accent_cls, accent_height) {
    let base_elements = box.elements_of(base_box)
    let vheight = accent_height + if (accent_height < 0.7) 0.22 else 0.21
    let accent_top = if (accent_key == "tilde") -3.56 else -3.21
    let margin_left = if (accent_key == "dot") 0.79 else 0.68
    let el = <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ fmt_accent_em(vheight);
                <span style: "top:-3em";
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: "height:0.73em;display:inline-block";
                        for (el in base_elements) el
                    >
                >
                <span class: css.CENTER, style: "top:" ++ fmt_accent_em(accent_top) ++ ";margin-left:" ++ fmt_accent_em(margin_left);
                    <span class: css.PSTRUT, style: "height:3em">
                    <span class: accent_cls, style: "height:" ++ fmt_accent_em(accent_height) ++ ";display:inline-block"; accent_text>
                >
            >
            <span class: css.VLIST_S; "\u200B">
        >
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:0.09em">
        >
    >
    accent_box(el, vheight, 0.09, base_box.width)
}

fn render_missing_base_accent(accent_key, accent_text, accent_cls, accent_height) {
    let vheight = accent_height + 0.27
    let pstrut = if (accent_key == "vec") 2.72 else 2.7
    let base_top = if (accent_height > 0.7) 0.0 - pstrut + 0.01 else 0.0 - pstrut
    let accent_top = if (accent_key == "tilde") 0.0 - pstrut - 0.61 else 0.0 - pstrut - 0.26
    let margin_left = if (accent_key == "dot") 0.27 else 0.16
    let el = <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ fmt_accent_em(vheight);
                <span style: "top:" ++ fmt_accent_em(base_top);
                    <span class: css.PSTRUT, style: "height:" ++ fmt_accent_em(pstrut)>
                    <span style: "height:0.9em;display:inline-block";
                        <span class: css.CMR; "□">
                    >
                >
                <span class: css.CENTER, style: "top:" ++ fmt_accent_em(accent_top) ++ ";margin-left:" ++ fmt_accent_em(margin_left);
                    <span class: css.PSTRUT, style: "height:" ++ fmt_accent_em(pstrut)>
                    <span class: accent_cls, style: "height:" ++ fmt_accent_em(accent_height) ++ ";display:inline-block"; accent_text>
                >
            >
            <span class: css.VLIST_S; "\u200B">
        >
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:0.2em">
        >
    >
    accent_box(el, vheight, 0.2, 0.8)
}

fn accent_box(el, h, d, w) => {
    element: el,
    height: h,
    depth: d,
    render_height: h,
    render_depth: d,
    render_total: h + d,
    width: w,
    type: "mord",
    italic: 0.0,
    skew: 0.0
}

fn accent_display_text(key) {
    match key {
        case "vec": "⃗"
        case "acute": "ˊ"
        case "grave": "ˋ"
        case "dot": "˙"
        case "ddot": "¨"
        case "tilde": "~"
        case "bar": "ˉ"
        case "breve": "˘"
        case "check": "ˇ"
        case "hat": "^"
        default: (let accent_char = sym.get_accent(key),
                  if (accent_char != null) accent_char else "^")
    }
}

fn accent_body_height(key) {
    if (key == "vec") 0.72
    else if (key == "dot") 0.67
    else if (key == "ddot") 0.67
    else if (key == "tilde") 0.67
    else if (key == "bar") 0.57
    else if (key == "check") 0.63
    else 0.7
}

fn fmt_accent_em(v) {
    let rounded = round(v * 100.0) / 100.0
    string(rounded) ++ "em"
}

// ============================================================
// Delimiter groups (\left...\right)
// ============================================================

fn render_delimiter_group(node, context) {
    let left_text = if (node.left != null) string(node.left) else ""
    let right_text = if (node.right != null) string(node.right) else ""

    let children = render_children(node, context)
    let spaced = apply_spacing(children, context)
    let content = box.hbox(spaced)

    if (content.height + content.depth <= 1.2)
        render_small_delimiter_group(left_text, right_text, spaced, content)
    else
        render_stretchy_delimiter_group(left_text, right_text, content)
}

fn render_small_delimiter_group(left_text, right_text, spaced, content) {
    let left_char = delims.resolve_char(left_text)
    let right_char = delims.resolve_char(right_text)
    let left_el = <span class: css.classes([css.SMALL_DELIM, css.OPEN]); left_char>
    let right_el = <span class: css.classes([css.SMALL_DELIM, css.CLOSE]); right_char>
    let content_elements = box.child_elements(spaced)
    {
        element: <span class: css.LEFT_RIGHT, style: "margin-top:-0.08333em;height:0.72777em";
            left_el
            for (el in content_elements) el
            right_el
        >,
        height: 0.75,
        depth: 0.25,
        width: content.width + 0.8,
        type: "minner",
        italic: 0.0,
        skew: 0.0
    }
}

fn render_stretchy_delimiter_group(left_text, right_text, content) {
    let content_height = content.height + content.depth
    let left_box = delims.render_stretchy(left_text, content_height, "mopen")
    let right_box = delims.render_stretchy(right_text, content_height, "mclose")
    box.with_class(box.hbox([left_box, content, right_box]), css.LEFT_RIGHT)
}

// ============================================================
// Sized delimiters (\big, \Big, etc.)
// ============================================================

fn render_sized_delim(node, context) {
    let delim_text = if (node.delim != null) string(node.delim) else ""
    let size_cmd = if (node.size != null) string(node.size) else ""
    let size_name = if (len(size_cmd) > 0 and slice(size_cmd, 0, 1) == "\\")
        slice(size_cmd, 1, len(size_cmd)) else size_cmd
    let scale = if (sym.get_delim_size(size_name) != null) sym.get_delim_size(size_name) else 1.0
    delims.render_at_scale(delim_text, scale, "mord")
}

// ============================================================
// Middle delimiter (\middle|)
// ============================================================

fn render_middle_delim(node, context) {
    let delim_text = if (node.delim != null) string(node.delim) else "|"
    box.text_box(delim_text, css.SMALL_DELIM, "mrel")
}

// ============================================================
// Radical (\sqrt)
// ============================================================

fn render_radical(node, context) {
    let body_box = if (node.radicand != null) render_node(node.radicand, context)
        else box.text_box("", null, "ord")

    let index_box = if (node.index != null)
        (let idx = render_node(node.index, ctx.sup_context(context)),
         box.with_class(idx, css.SQRT_INDEX))
    else null

    let sqrt_sign = box.text_box("\u221A", css.SQRT_SIGN, "ord")

    // wrap radicand with vinculum (overline bar) using border-top
    let body_with_vinculum = box.with_style(body_box,
        "display:inline-block;border-top:0.04em solid currentColor;padding-top:1px")

    let sqrt_body = box.hbox([sqrt_sign, body_with_vinculum])

    if (index_box != null)
        box.hbox([index_box, box.with_class(sqrt_body, css.SQRT)])
    else
        box.with_class(sqrt_body, css.SQRT)
}

// ============================================================
// Default fallback
// ============================================================

fn render_default(node, context) {
    let children = render_children(node, context)
    if (len(children) > 0) box.hbox(children)
    else box.text_box(get_text(node), css.font_class(context.font), "mord")
}

// ============================================================
// frac_like fallback (for CST nodes not yet converted by C++)
// ============================================================

fn render_frac_like(node, context) {
    // if the C++ converter already set numer/denom, delegate to fraction renderer
    if (node.numer != null or node.denom != null) {
        fraction.render(node, context, render_node)
    } else {
        // fallback: treat children[0] as numer, children[1] as denom
        let n = len(node)
        let numer_node = if (n > 0) node[0] else null
        let denom_node = if (n > 1) node[1] else null
        let cmd_text = if (node.cmd != null) string(node.cmd) else "\\frac"
        // build a synthetic node map for fraction.render
        let synth = {cmd: cmd_text, numer: numer_node, denom: denom_node}
        fraction.render(synth, context, render_node)
    }
}

// ============================================================
// Helpers
// ============================================================

// render all children of a node
fn render_children(node, context) {
    let n = len(node)
    if (n == 0) {
        []
    } else {
        render_children_scan(node, context, 0, [])
    }
}

fn transparent_hbox(children) {
    let hb = box.hbox(children)
    let filtered = (for (b in children where b != null) b)
    if (len(filtered) == 0) hb
    else
        (let last_idx = len(filtered) - 1,
         let last = filtered[last_idx],
         box_with_type(hb, last.type))
}

fn render_children_scan(node, context, i, acc) {
    if (i >= len(node)) acc
    else if (is_colorbox_sibling_sequence(node, i))
        (let rendered = render_colorbox_sibling_sequence(node, context, i),
         let rest = colorbox_sibling_rest(node, i),
         let rest_boxes = if (rest == "") [] else render_text_atoms(rest, context),
         render_children_scan(node, context, i + 2, acc ++ [rendered] ++ rest_boxes))
    else if (is_color_switch(node, i))
        (let rendered = render_color_switch_tail(node, context, i),
         acc ++ [rendered])
    else if (is_textcolor_sequence(node, i))
        (let rendered = render_textcolor_sequence(node, context, i),
         render_children_scan(node, context, i + 8, acc ++ [rendered]))
    else if (is_scriptstyle_switch(node, i))
        (let rendered = render_scriptstyle_sibling(node[i + 1], context),
         render_children_scan(node, context, i + 2, acc ++ [rendered]))
    else
        (let child = node[i],
         let next_acc = if (child == null) acc
             else if (is_dollar_error(child)) acc
             else if (child is string) acc ++ render_text_atoms(string(child), context)
             else acc ++ [render_node(child, context)],
         render_children_scan(node, context, i + 1, next_acc))
}

fn is_dollar_error(child) {
    child is element and name(child) == 'ERROR' and plain_text(child) == "$"
}

fn is_colorbox_command_node(child) {
    if (child is element and name(child) == 'command') {
        let cmd_text = get_text(child)
        let name_str = if (len(cmd_text) > 0 and slice(cmd_text, 0, 1) == "\\")
            slice(cmd_text, 1, len(cmd_text)) else cmd_text
        name_str == "colorbox"
    } else false
}

fn is_color_command_node(child) {
    if (child is element and name(child) == 'command') {
        let cmd_text = get_text(child)
        let name_str = if (len(cmd_text) > 0 and slice(cmd_text, 0, 1) == "\\")
            slice(cmd_text, 1, len(cmd_text)) else cmd_text
        name_str == "color"
    } else false
}

fn is_color_switch(node, i) {
    let child = if (i < len(node)) node[i] else null
    is_color_command_node(child) and len(child) == 1
}

fn render_color_switch_tail(node, context, i) {
    let cmd = node[i]
    let color_value = color.resolve_raw(plain_text(cmd[0]))
    let children = render_children_scan(node, context, i + 1, [])
    let spaced = apply_spacing(children, context)
    let hb = transparent_hbox(spaced)
    let elements = (for (b in spaced) b.element)
    box_with_suppress_depth({
        element: <span style: "color:" ++ color_value;
            for (el in elements) el
        >,
        height: hb.height,
        depth: hb.depth,
        render_height: hb.render_height,
        render_depth: hb.render_depth,
        render_total: hb.render_total,
        width: hb.width,
        type: hb.type,
        italic: hb.italic,
        skew: hb.skew
    })
}

fn is_colorbox_sibling_sequence(node, i) {
    if (i + 1 >= len(node)) false
    else
        (let cmd = node[i],
         let next = node[i + 1],
         is_colorbox_command_node(cmd) and len(cmd) == 1 and next is string and len(next) > 0)
}

fn render_colorbox_sibling_sequence(node, context, i) {
    let cmd = node[i]
    let next = string(node[i + 1])
    let bg = color.resolve_background_raw(plain_text(cmd[0]))
    let first = slice(next, 0, 1)
    let content_box = box.text_box(first, css.TEXT, "mord")
    let wrapped = color.with_background(content_box, bg)
    box_with_suppress_depth(wrapped)
}

fn colorbox_sibling_rest(node, i) {
    let next = string(node[i + 1])
    if (len(next) <= 1) "" else slice(next, 1, len(next))
}

fn is_scriptstyle_switch(node, i) {
    if (i + 1 >= len(node)) false
    else
        (let child = node[i],
         child is element and name(child) == 'style_command' and
         child.cmd != null and string(child.cmd) == "\\scriptstyle" and
         child.arg == null and len(child) == 0)
}

fn render_scriptstyle_sibling(child, context) {
    let bx = render_node(child, context)
    style_wrap_box(bx, "font-size: 70%")
}

fn style_wrap_box(bx, style_text) {
    let rt = if (bx.render_height != null) bx.render_height else bx.height
    {
        element: <span style: style_text; bx.element>,
        height: bx.height,
        depth: 0.0,
        render_height: bx.render_height,
        render_depth: 0.0,
        render_total: rt,
        width: bx.width,
        type: bx.type,
        italic: bx.italic,
        skew: bx.skew,
        suppress_hbox_text_depth: true
    }
}

fn render_text_atoms(text, context) {
    if (text == "+-") {
        [
            box.text_box("+", css.CMR, "mbin"),
            box.text_box("−", css.CMR, "mbin")
        ]
    } else {
        [render_text(text, context)]
    }
}

fn child_is_text(node, i, text) {
    if (i >= len(node)) false
    else
        (let child = node[i],
         if (child is string) string(child) == text else false)
}

fn is_text_command_node(child) {
    if (child is element and name(child) == 'text_command') {
        child.cmd != null and string(child.cmd) == "\\text"
    } else false
}

fn is_textcolor_sequence(node, i) {
    if (i + 7 >= len(node)) false
    else
        (let cmd = node[i],
         let color_arg = node[i + 6],
         let content_arg = node[i + 7],
         is_text_command_node(cmd) and
         child_is_text(node, i + 1, "c") and
         child_is_text(node, i + 2, "o") and
         child_is_text(node, i + 3, "l") and
         child_is_text(node, i + 4, "o") and
         child_is_text(node, i + 5, "r") and
         color_arg is element and name(color_arg) == 'group' and
         content_arg is element and name(content_arg) == 'group')
}

fn render_textcolor_sequence(node, context, i) {
    let color_arg = node[i + 6]
    let content_arg = node[i + 7]
    let color_value = color.resolve_raw(plain_text(color_arg))
    let children = render_children(content_arg, context)
    let spaced = apply_spacing(children, context)
    let hb = transparent_hbox(spaced)
    let elements = (for (b in spaced) b.element)
    box_with_suppress_depth({
        element: <span style: "color:" ++ color_value;
            for (el in elements) el
        >,
        height: hb.height,
        depth: hb.depth,
        width: hb.width,
        type: hb.type,
        italic: hb.italic,
        skew: hb.skew
    })
}

fn box_with_suppress_depth(bx) {
    {
        element: bx.element,
        height: bx.height,
        depth: 0.0,
        render_height: bx.render_height,
        render_depth: 0.0,
        render_total: if (bx.render_height != null) bx.render_height else bx.height,
        width: bx.width,
        type: bx.type,
        italic: bx.italic,
        skew: bx.skew,
        suppress_hbox_text_depth: true
    }
}

fn plain_text(node) {
    if (node is string) string(node)
    else if (node is symbol) string(node)
    else if (node is element) plain_text_element(node, 0, "")
    else string(node)
}

fn plain_text_element(node, i, acc) {
    if (i >= len(node))
        if (acc != "") acc else plain_text_value(node)
    else plain_text_element(node, i + 1, acc ++ plain_text(node[i]))
}

fn plain_text_value(node) {
    if (node.value != null) string(node.value)
    else if (node.name != null) string(node.name)
    else if (node.cmd != null) string(node.cmd)
    else ""
}

// get text content of a node
fn get_text(node) {
    if (node is string) node
    else if (node is symbol) string(node)
    else if (not (node is element)) string(node)
    else get_text_from_element(node)
}

fn get_text_from_element(node) {
    if (len(node) > 0 and node[0] is string) string(node[0])
    else if (node.value != null) string(node.value)
    else if (node.text != null) string(node.text)
    else if (node.name != null) string(node.name)
    else ""
}

fn is_plain_number_text(text) {
    (len(text) > 0) and is_digit_text(text, 0)
}

fn is_digit_text(text, i) {
    if (i >= len(text)) true
    else if (contains("0123456789", slice(text, i, i + 1))) is_digit_text(text, i + 1)
    else false
}

// ============================================================
// Inter-atom spacing
// ============================================================

fn bin_as_ord_after(prev_type) {
    prev_type == null or prev_type == "mbin" or prev_type == "mopen" or
    prev_type == "mrel" or prev_type == "mpunct" or prev_type == "mop"
}

fn box_with_type(bx, atom_type) => {
    element: bx.element,
    height: bx.height,
    depth: bx.depth,
    width: bx.width,
    type: atom_type,
    italic: bx.italic,
    skew: bx.skew
}

fn normalize_bin_atom(bx, prev_type) {
    if (bx.type == "mbin" and bin_as_ord_after(prev_type)) box_with_type(bx, "mord")
    else bx
}

fn build_normalized(filtered, i, prev_type, acc) {
    if (i >= len(filtered)) acc
    else
        (let current = normalize_bin_atom(filtered[i], prev_type),
         build_normalized(filtered, i + 1, current.type, acc ++ [current]))
}

fn normalize_atom_types(filtered) {
    build_normalized(filtered, 0, null, [])
}

// recursive helper for building spaced box list
fn build_spaced(normalized, i, prev_type, acc, context) {
    if (i >= len(normalized)) acc
    else
        (let current = normalized[i],
         let space = sp_table.get_spacing(prev_type, current.type, context.style),
         let with_space = if (space != 0.0)
             acc ++ [box.skip_box(space), current]
         else
             acc ++ [current],
         build_spaced(normalized, i + 1, current.type, with_space, context))
}

// apply inter-atom spacing between boxes
fn apply_spacing(boxes, context) {
    let filtered = (for (b in boxes where b != null) b)
    if (len(filtered) <= 1) filtered
    else
        (let normalized = normalize_atom_types(filtered),
         build_spaced(normalized, 1, normalized[0].type, [normalized[0]], context))
}
