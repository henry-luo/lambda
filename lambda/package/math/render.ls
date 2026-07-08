// math/render.ls — Core renderer: AST node → box tree dispatch
// This is the heart of the math rendering pipeline.
// It dispatches on AST element tag names and delegates to atom-specific renderers.

import box: .box
import ctx: .context
import css: .css
import met: .metrics
import sym: .symbols
import sp_table: .spacing_table
import util: .util
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
        case 'escaped_symbol':  render_escaped_symbol(node, context)
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
        case 'ERROR':           render_error_node(node, context)
        case 'phantom_command': enclose.render_phantom(node, context, render_node)
        case 'box_command':     enclose.render_box(node, context, render_node)
        case 'rule_command':    enclose.render_rule(node, context, render_node)
        case 'radical':         render_radical(node, context)
        case 'middle_delim':    render_middle_delim(node, context)
        case 'limits_modifier': box.text_box("", null, "ord")
        case 'text_group':      render_text_group(node, context)
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
    let spacing_context = group_spacing_context(context)
    let spaced = apply_spacing(children, spacing_context)
    transparent_hbox(spaced)
}

fn group_spacing_context(context) {
    if (context.style == "script" and context.script_container != true)
        ctx.derive(context, {style: "text"})
    else if (context.style == "scriptscript" and
        context.script_container != true and context.fraction_child != true)
        ctx.derive(context, {style: "text"})
    else context
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
    if (unicode != null and sym.is_limit_op(cmd_text))
        render_limit_operator_symbol(display_text, context)
    else
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
        else if (text == "/") "mord"
        else "mbin"
    box.text_box(operator_display_text(text), css.CMR, atom_type)
}

fn render_relation(node, context) {
    let text = get_text(node)
    // If this is a command-form relation (e.g., \uparrow, \downarrow), look
    // up its Unicode glyph. The grammar tokenizes these as relation nodes
    // rather than commands, so render_command's symbol lookup isn't invoked.
    let display0 = if (len(text) > 0 and slice(text, 0, 1) == "\\") {
        let unicode = sym.lookup_symbol(text)
        if (unicode != null) unicode else text
    } else text
    // The `<`/`>` relation glyphs are emitted RAW by MathLive inside top-level
    // math `lm_cmr` spans (unlike text-mode `<`/`>`, e.g. inside `\text{...}`,
    // which it escapes). Carry them as private-use sentinels (U+E000/U+E001)
    // that the HTML serializer maps back to raw glyphs after entity-escaping
    // everything else. Skip the sentinel for inline math embedded in text.
    let raw_lt_gt = context.text_embedded != true
    let display = if (raw_lt_gt and display0 == "<") "\u{E000}"
        else if (raw_lt_gt and display0 == ">") "\u{E001}"
        else display0
    // ASCII `!` is mclose in TeX math, not mrel — `n!` should typeset
    // without thickspace between the letter and the factorial.
    let atom_type = if (text == "!") "mclose" else "mrel"
    box.text_box(display, css.CMR, atom_type)
}

fn render_punct(node, context) {
    let text = get_text(node)
    if (text == "'") render_prime_script(context)
    else {
        let atom_type = if (text == "(" or text == "[" or text == "{") "mopen"
            else if (text == ")" or text == "]" or text == "}") "mclose"
            else if (text == "|") "mord"
            else if (text == ":") "mrel"
            else "mpunct"
        box.text_box(punct_display_text(text), css.CMR, atom_type)
    }
}

// Render ASCII apostrophe `'` as MathLive's prime structure: a lm_msubsup
// containing the Unicode prime ′ (U+2032) shifted up like a superscript.
// This produces visual parity with MathLive's `\prime` shortcut and lets
// the outer strut wrap include the prime's height.
fn render_prime_script(context) {
    // Prime stacks are compact only in table/fraction child frames. Root
    // display math still uses the taller inline prime stack in MathLive.
    let compact = context.compact_prime == true
    let cramped = context.cramped == true
    let vlist_h = if (compact and cramped) 0.68 else if (compact) 0.76 else 0.81
    let top_em = if (compact and cramped) "-3.28em" else if (compact) "-3.36em" else "-3.41em"
    let el = <span class: css.MSUBSUP;
        <span class: css.VLIST_T;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em(vlist_h);
                    <span style: "top:" ++ top_em ++ ";margin-right:0.05em";
                        <span class: css.PSTRUT, style: "height:3em">
                        <span style: "height:0.39em;display:inline-block;font-size: 70%";
                            <span class: css.CMR; "′">
                        >
                    >
                >
            >
        >
    >
    box.ml_box_full(el, vlist_h, 0.0, 0.4, "mord", 0.0, 0.0, vlist_h)
}

fn operator_display_text(text) {
    if (text == "-") "−"
    else if (text == "*") "∗"
    else text
}

fn punct_display_text(text) {
    if (text == "\\{") "{"
    else if (text == "\\}") "}"
    else if (text == "|") "∣"
    else text
}

fn render_escaped_symbol(node, context) {
    let text = get_text(node)
    box.text_box(escaped_symbol_display_text(text), css.CMR, "mord")
}

fn escaped_symbol_display_text(text) {
    if (len(text) >= 2 and slice(text, 0, 1) == "\\")
        slice(text, 1, len(text))
    else text
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
    let name_str = if (len(cmd_text) > 0 and slice(cmd_text, 0, 1) == "\\")
         slice(cmd_text, 1, len(cmd_text)) else cmd_text
    if (name_str == "ne" or name_str == "neq") {
        render_not_overlay("=")
    } else if (name_str == "not") {
        render_not_command(node)
    } else if (name_str == "class") {
        render_class_command(node, context)
    } else if (name_str == "cssId") {
        render_css_id_command(node, context)
    } else if (name_str == "htmlData") {
        render_html_data_command(node, context)
    } else if (is_math_size_name(name_str)) {
        box.text_box("", null, "mord")
    } else if (name_str == "right" and len(node) == 1) {
        render_malformed_right_command(node, context)
    } else if (name_str == "pmod") {
        render_pmod_command(node, context)
    } else if (name_str == "bmod") {
        render_bmod_command(node, context)
    } else if (name_str == "boldsymbol") {
        render_boldsymbol_command(node, context)
    } else {
        let unicode = sym.lookup_symbol(cmd_text)
        if (unicode != null) {
        let atom_type = sym.classify_symbol(cmd_text)
        if (sym.is_limit_op(cmd_text))
            render_limit_operator_symbol(unicode, context)
        else
            box.text_box(unicode, symbol_font_class(cmd_text, context), atom_type)
    } else {
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
                render_unknown_command_node(node, name_str, context)
            }
        }
    }
    }
}

// An unknown `\cmd{...}` renders the way MathLive does: an `lm_error lm_cmr`
// span carrying `\cmd`, followed by the command's arguments rendered as
// ordinary math (the braces are not consumed by an unknown macro). E.g.
// `\label{eq:x}` → error("\label") + the math `eq:x`.
fn render_unknown_command_node(node, name_str, context) {
    // The leading `\` (cmr backslash: height 0.75, depth 0.25) governs the
    // error token's vertical extent.
    let err_box = box.ml_box_full(
        <span class: css.classes([css.ERROR, css.CMR]); "\\" ++ name_str>,
        0.75,
        0.25,
        0.4 * float(len(name_str) + 1),
        "mord",
        0.0,
        0.0,
        0.75
    )
    // Render only the brace/bracket-group arguments as math (skip the bare
    // command-name string). MathLive puts a thin space (0.17em) between the
    // error token and the following argument content.
    let arg_boxes = (for (child in node
        where child is element and (name(child) == 'group' or name(child) == 'brack_group'))
        render_node(child, context))
    if (len(arg_boxes) == 0) err_box
    else {
        let all_boxes = [err_box, box.skip_box(0.17)] ++ arg_boxes
        let spaced = apply_spacing(all_boxes, context)
        box_with_type(box.hbox(spaced), "minner")
    }
}

fn render_limit_operator_symbol(text, context) {
    // MathLive consistently uses lm_large-op for top-level big operators
    // (\\sum, \\int, \\prod, etc.) regardless of inline vs display mode.
    // Only nested/script-style contexts demote to small-op.
    let use_large = ctx.is_display(context) or not ctx.is_script(context)
    let op_size = if (use_large) "lm_large-op" else "lm_small-op"
    let op_cls = css.classes(["lm_op-symbol", op_size])
    let metrics = if (use_large) large_op_metrics(text) else small_op_metrics(text)
    // Italic correction (mostly \\int with italic=0.44445 → margin-right:0.45em)
    let op_el = if (metrics.italic > 0.0)
        <span class: op_cls, style: "margin-right:" ++ util.fmt_em_ceil2(metrics.italic); text>
        else <span class: op_cls; text>
    box.ml_box_full(
        <span class: css.OP_GROUP;
            op_el
        >,
        metrics.h_raw,
        metrics.d_raw,
        metrics.width,
        "mop",
        0.0,
        0.0,
        metrics.h_raw
    )
}

// Per-symbol Size2 (large-op) metrics ported from MathLive's
// font-metrics-data.ts. Values are CEIL@2-rounded for h, FLOOR@2 for d
// (positive), italic CEIL@2. The h_raw/d_raw fields carry 5dp full
// precision so the outer strut can emit CEIL@2 of the raw sum.
fn large_op_metrics(text) {
    let is_integral = text == "∫" or text == "∮" or text == "∯" or text == "∰"
    let is_summation = text == "∑" or text == "∏" or text == "⋂" or text == "⋃" or
        text == "⨀" or text == "⨁" or text == "⨂" or text == "⨃" or
        text == "⨄" or text == "⨆" or text == "⨅" or text == "⨇" or
        text == "⨈" or text == "⨉" or text == "⨊" or text == "⨋" or
        text == "∐"
    if (is_integral) {
        {h: 1.36, d: 0.86, h_raw: 1.36, d_raw: 0.86225, italic: 0.45, width: 0.55556}
    } else if (is_summation) {
        {h: 1.05, d: 0.55, h_raw: 1.05, d_raw: 0.55001, italic: 0.0, width: 1.05}
    } else {
        {h: 1.61, d: 0.2, h_raw: 1.61, d_raw: 0.2, italic: 0.0, width: 0.6}
    }
}

fn small_op_metrics(text) {
    let is_integral = text == "∫" or text == "∮"
    let is_summation = text == "∑" or text == "∏" or text == "⋂" or text == "⋃" or
        text == "⨀" or text == "⨁" or text == "⨂" or text == "⨃" or
        text == "⨄" or text == "⨆"
    if (is_integral) {
        {h: 0.81, d: 0.3, h_raw: 0.805, d_raw: 0.30612, italic: 0.2, width: 0.47222}
    } else if (is_summation) {
        {h: 0.75, d: 0.25, h_raw: 0.75, d_raw: 0.25001, italic: 0.0, width: 0.94445}
    } else {
        {h: 0.9, d: 0.1, h_raw: 0.9, d_raw: 0.1, italic: 0.0, width: 0.6}
    }
}

fn render_class_command(node, context) {
    if (len(node) >= 2) {
        let class_name = arg_raw_text(node[0])
        let content_box = render_node(node[1], context)
        let children = box.elements_of(content_box)
        wrap_content_element(
            <span class: class_name;
                for (el in children) el
            >,
            content_box,
            content_box.type
        )
    } else box.text_box("class", css.ERROR, "mord")
}

fn render_css_id_command(node, context) {
    if (len(node) >= 2) {
        let id_name = normalize_css_id(arg_raw_text(node[0]))
        let content_box = render_node(node[1], context)
        let children = box.elements_of(content_box)
        wrap_content_element(
            <span id: id_name;
                for (el in children) el
            >,
            content_box,
            content_box.type
        )
    } else box.text_box("cssId", css.ERROR, "mord")
}

fn render_html_data_command(node, context) {
    if (len(node) >= 2) {
        let attrs = parse_html_data_attrs(arg_raw_text(node[0]))
        let content_box = render_node(node[1], context)
        let children = box.elements_of(content_box)
        wrap_content_element(
            <span math_data_attrs: attrs;
                for (el in children) el
            >,
            content_box,
            content_box.type
        )
    } else box.text_box("htmlData", css.ERROR, "mord")
}

fn parse_html_data_attrs(raw) {
    parse_html_data_attr_parts(split(raw, ","), 0, [])
}

fn wrap_content_element(el, content_box, atom_type) {
    box.ml_box_full(
        el,
        content_box.height,
        content_box.depth,
        content_box.width,
        atom_type,
        content_box.italic,
        content_box.skew,
        if (content_box.max_font_size != null)
            content_box.max_font_size else content_box.height
    )
}

fn parse_html_data_attr_parts(parts, i, acc) {
    if (i >= len(parts)) acc
    else
        (let attr = parse_html_data_attr(parts[i]),
         let next = if (attr == null) acc else acc ++ [attr],
         parse_html_data_attr_parts(parts, i + 1, next))
}

fn parse_html_data_attr(part) {
    let text = trim(part)
    if (text == "") null
    else
        (let eq_pos = index_of(text, "="),
         let raw_name = if (eq_pos >= 0) trim(slice(text, 0, eq_pos)) else text,
         let raw_value = if (eq_pos >= 0) trim(slice(text, eq_pos + 1, len(text))) else null,
         let data_name = normalize_data_attr_name(raw_name),
         if (data_name == "data-") null
         else {
            name: data_name,
            value: if (raw_value == null) null else strip_attr_quotes(raw_value),
            has_value: raw_value != null
         })
}

fn normalize_data_attr_name(text) {
    "data-" ++ normalize_data_attr_name_at(text, 0, "")
}

fn normalize_data_attr_name_at(text, i, acc) {
    if (i >= len(text)) acc
    else
        (let ch = slice(text, i, i + 1),
         let out = if (ch == " ") "-"
            else if (is_data_attr_char(ch)) ch
            else "",
         normalize_data_attr_name_at(text, i + 1, acc ++ out))
}

fn is_data_attr_char(ch) {
    contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_:", ch)
}

fn strip_attr_quotes(value) {
    if (len(value) >= 2 and
        ((slice(value, 0, 1) == "\"" and slice(value, len(value) - 1, len(value)) == "\"") or
         (slice(value, 0, 1) == "'" and slice(value, len(value) - 1, len(value)) == "'")))
        slice(value, 1, len(value) - 1)
    else value
}

fn arg_raw_text(node) {
    if (node is element and node.raw != null) string(node.raw)
    else plain_text(node)
}

fn normalize_css_id(text) {
    normalize_css_id_at(text, 0, "")
}

fn normalize_css_id_at(text, i, acc) {
    if (i >= len(text)) acc
    else
        (let ch = slice(text, i, i + 1),
         let out = if (ch == " ") "-" else ch,
         normalize_css_id_at(text, i + 1, acc ++ out))
}

fn render_not_command(node) {
    if (len(node) > 0 and not_group_text(node[0]) != "")
        render_not_overlay_group_text(not_group_text(node[0]))
    else render_not_slash()
}

fn not_group_text(child) {
    if (child is element and (name(child) == 'group' or name(child) == 'brack_group') and len(child) > 0)
        plain_text(child)
    else ""
}

fn render_not_overlay_group_text(base_text) {
    let cls = if (is_alpha_text(base_text)) css.MATHIT else css.CMR
    render_not_overlay_text(base_text, cls, "mord")
}

fn render_not_slash() {
    box.ml_box_full(<span class: css.CMR; "\uE020">,
        0.7, 0.19, 0.5, "mrel", 0.0, 0.0, 0.7)
}

fn render_not_overlay(base_text) {
    render_not_overlay_text(base_text, css.CMR, "mrel")
}

fn render_not_overlay_text(base_text, cls, atom_type) {
    let base_box = box.text_box(base_text, cls, atom_type)
    let overlay_el = <span class: css.BASE;
        <span class: "lm_rlap";
            <span class: "lm_inner";
                <span class: css.CMR; "\uE020">
            >
            <span class: "lm_fix">
        >
        <span class: cls; base_text>
    >
    box.ml_box_full(overlay_el, 0.7, 0.19, base_box.width, atom_type, 0.0, 0.0, 0.7)
}

fn is_alpha_text(text) {
    (len(text) > 0) and is_alpha_text_at(text, 0)
}

fn is_alpha_text_at(text, i) {
    if (i >= len(text)) true
    else
        (let ch = slice(text, i, i + 1),
         if (contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", ch))
            is_alpha_text_at(text, i + 1)
         else false)
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

fn render_placeholder(context) =>
    box.ml_box(<span style: "height:0;display:inline-block;font-size: 50%"; "\u00A0">,
        0.0, 0.0, 0.0, "mord")

fn is_generic_box_command(name_str) {
    name_str == "bbox" or name_str == "boxed" or name_str == "fbox" or
    name_str == "llap" or name_str == "rlap" or name_str == "clap" or
    name_str == "mathllap" or name_str == "mathrlap" or name_str == "mathclap"
}

fn render_generic_box_command(node, context, name_str) {
    let content = if (len(node) > 0) generic_box_content_arg(node, name_str) else null
    let options = if (name_str == "bbox" and len(node) > 1) plain_text(node[0]) else null
    let cmd = if (starts_with_math_lap(name_str))
        "\\" ++ slice(name_str, 4, len(name_str))
    else "\\" ++ name_str
    let synth = {cmd: cmd, content: content, options: options}
    enclose.render_box(synth, context, render_node)
}

fn generic_box_content_arg(node, name_str) {
    if (name_str == "bbox" and len(node) > 1) node[1]
    else node[len(node) - 1]
}

fn starts_with_math_lap(name_str) {
    name_str == "mathllap" or name_str == "mathrlap" or name_str == "mathclap"
}

// `\pmod{n}` expands to a quad space, parens around the literal "mod" plus
// the argument. Matches MathLive's emit:
//   <span class="lm_quad"></span><span class="lm_cmr">(</span>
//   <span class="lm_op-group"><span class="lm_cmr">mod</span></span>
//   <span class="lm_cmr"> </span><span style="...;width:0.17em"></span>
//   {arg rendered}<span class="lm_cmr">)</span>
fn render_pmod_command(node, context) {
    let quad_box = box.box_cls(css.QUAD, 0.0, 0.0, 1.0, "skip")
    let lparen = box.text_box("(", css.CMR, "mord")
    let mod_box = box.with_class(box.text_box("mod", css.CMR, "mop"), css.OP_GROUP)
    let space_box = box.text_box(" ", css.CMR, "mord")
    let thin_box = box.skip_box(0.17)
    let arg_box = if (len(node) > 0) render_node(node[0], context)
        else box.text_box("", null, "mord")
    let rparen = box.text_box(")", css.CMR, "mord")
    box.hbox([quad_box, lparen, mod_box, space_box, thin_box, arg_box, rparen])
}

// `\bmod` is the bare "mod" operator (no parens). Same op-group structure
// without surrounding parens or quad spacing.
fn render_bmod_command(node, context) {
    let mod_box = box.with_class(box.text_box("mod", css.CMR, "mop"), css.OP_GROUP)
    if (len(node) > 0) {
        let arg_box = render_node(node[0], context)
        box.hbox([mod_box, box.skip_box(0.17), arg_box])
    } else mod_box
}

// `\boldsymbol{...}` renders the body in mathbf font — handle by deriving a
// new context with font="mathbf" and rendering the argument under it. The
// font-class infrastructure already maps `mathbf` → `lm_mathbf`; letters and
// Greek pick up the right class via `font_class(context.font)`.
fn render_boldsymbol_command(node, context) {
    if (len(node) == 0) box.text_box("", null, "mord")
    else {
        let new_ctx = ctx.derive(context, {font: "mathbf"})
        render_node(node[0], new_ctx)
    }
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
    // Centralized lookup in symbols.font_class_map; falls back to the
    // current rendering context's font (typically lm_mathit) when the
    // command is not registered.
    let mapped = sym.font_class_of(name_str)
    // Bold context override: `\boldsymbol{...}` and `\mathbf{...}` should
    // swap any `lm_mathit` in a mapped class for `lm_mathbf` so Greek and
    // letters render bold under the bold scope. Preserves marker classes
    // like `lcGreek` and `lm_it`.
    if (context.font == "mathbf" and mapped != null and contains(mapped, "lm_mathit"))
        replace(mapped, "lm_mathit", "lm_mathbf")
    else (mapped or css.font_class(context.font))
}

fn render_colorbox_content(content_arg, context) {
    if (content_arg is string) box.text_box(string(content_arg), css.TEXT, "mord")
    else
        // \colorbox content is text-mode; any embedded `$...$` is inline math
        // (text-style), so reset to text-style regardless of the ambient
        // (display) style — matches MathLive's inline-math reset.
        (let content_context = ctx.derive(context, {colorbox_content: true, style: "text"}),
         let children = render_children(content_arg, content_context),
         let spaced = apply_spacing(children, content_context),
         transparent_hbox(spaced))
}

// The partial glyph is a bare element (no class span / italic margin), while
// still carrying the full cmr metrics needed by the fraction path.
fn partial_box() =>
    box.ml_box_full("∂", 0.69444, 0.0, 0.45, "mord", 0.0, 0.0, 0.69444)

// ============================================================
// Text command (\text{...})
// ============================================================

fn render_text_command(node, context) {
    let content = if (node.content != null) get_text(node.content) else ""
    render_text_content_box(content)
}

// textstyle_command fallback (for CST nodes not yet converted by C++)
fn render_textstyle_command(node, context) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""
    let is_text = (contains(cmd, "text") or contains(cmd, "mbox") or contains(cmd, "hbox"))
    if (is_text) {
        let content = if (node.content != null) get_text(node.content)
            else if (node.arg != null) get_text(node.arg) else ""
        render_text_content_box(content)
    } else {
        style.render(node, context, render_node)
    }
}

fn render_text_content_box(content) {
    let tiny_idx = index_of(content, "\\tiny")
    if (is_ensuremath_text(content)) render_ensuremath_text(content)
    else if (is_text_inline_math_content(content)) render_text_inline_math_content(content)
    else if (tiny_idx >= 0) render_tiny_text_content(content, tiny_idx)
    else if (is_text_textcolor_content(content)) render_text_textcolor_content(content)
    else box.text_box(decode_latex_text(content), css.TEXT, "mord")
}

fn is_ensuremath_text(content) =>
    util.starts_with(content, "\\ensuremath{") and
    len(content) >= 13 and
    slice(content, len(content) - 1, len(content)) == "}"

fn render_ensuremath_text(content) {
    let inner = slice(content, 12, len(content) - 1)
    let ast^err = parse(inner, {type: "math", flavor: "latex"})
    if (^err) {
        box.text_box(decode_latex_text(content), css.TEXT, "mord")
    } else {
        let inner_box = render_node(ast, ctx.text_context())
        let children = box.elements_of(inner_box)
        box.ml_box_full(
            <span class: css.TEXT;
                for (el in children) el
            >,
            inner_box.height,
            inner_box.depth,
            inner_box.width,
            "mord",
            0.0,
            0.0,
            if (inner_box.max_font_size != null) inner_box.max_font_size else inner_box.height
        )
    }
}

fn is_text_inline_math_content(content) =>
    index_of(content, "$") >= 0

fn render_text_inline_math_content(content) {
    let start = index_of(content, "$")
    let after_start = slice(content, start + 1, len(content))
    let end_rel = index_of(after_start, "$")
    if (start < 0 or end_rel < 0) {
        box.text_box(decode_latex_text(content), css.TEXT, "mord")
    } else {
        let end_pos = start + 1 + end_rel
        let before = decode_latex_text(slice(content, 0, start))
        let math_src = slice(content, start + 1, end_pos)
        let after = decode_latex_text(slice(content, end_pos + 1, len(content)))
        let before_box = if (before != "") box.text_box(before, css.TEXT, "mord") else null
        let parse_src = inline_text_math_parse_source(math_src)
        let ast^err = parse(parse_src, {type: "math", flavor: "latex"})
        // Mark inline math embedded in \text{...}: MathLive escapes `<`/`>`
        // here (text-mode semantics) rather than emitting them raw as it does
        // for top-level math relations.
        let embedded_ctx = ctx.derive(ctx.text_context(), {text_embedded: true})
        let math_box = if (^err) box.text_box("$" ++ math_src ++ "$", css.TEXT, "mord")
            else render_node(ast, embedded_ctx)
        let math_gap = if (before_box != null) box.skip_box(0.17) else null
        let after_box = if (after != "") box.text_box(after, css.TEXT, "mord") else null
        transparent_hbox([before_box, math_gap, math_box, after_box])
    }
}

fn inline_text_math_parse_source(math_src) {
    if (len(math_src) >= 3 and slice(math_src, 0, 1) == "|") {
        let second_rel = index_of(slice(math_src, 1, len(math_src)), "|")
        if (second_rel >= 0) {
            let second = second_rel + 1
            "\\lvert " ++ slice(math_src, 1, second) ++ "\\rvert " ++
            slice(math_src, second + 1, len(math_src))
        } else math_src
    } else math_src
}

fn is_text_textcolor_content(content) =>
    index_of(content, "\\textcolor{") >= 0

fn render_text_textcolor_content(content) {
    let cmd_idx = index_of(content, "\\textcolor{")
    let before = decode_latex_text(slice(content, 0, cmd_idx))
    let color_start = cmd_idx + 11
    let color_end_rel = index_of(slice(content, color_start, len(content)), "}")
    if (color_end_rel < 0) {
        box.text_box(decode_latex_text(content), css.TEXT, "mord")
    } else {
        let color_end = color_start + color_end_rel
        let color_name = slice(content, color_start, color_end)
        let body_start = color_end + 2
        let body_end = len(content) - 1
        let body = if (body_start <= body_end) decode_latex_text(slice(content, body_start, body_end)) else ""
        let before_box = if (before != "") box.text_box(before, css.TEXT, "mord") else null
        let color_box = text_color_box(body, color.resolve_raw(color_name))
        let elems = text_content_elements(before_box, color_box)
        text_content_special_box(
            <span class: css.BASE;
                for (el in elems) el
            >,
            0.65,
            0.08,
            text_content_width(before_box, color_box)
        )
    }
}

fn text_color_box(text, color_value) => {
    element: <span style: "color:" ++ color_value;
        <span class: css.TEXT; text>
    >,
    height: 0.65,
    depth: 0.08,
    width: 0.5 * float(len(text)),
    type: "mord",
    italic: 0.0,
    skew: 0.0,
    max_font_size: 0.65,
    model: "ml",
    suppress_hbox_text_depth: true,
    suppress_hbox_operator_height: true,
    no_left_bin_space: true
}

fn render_tiny_text_content(content, tiny_idx) {
    let before = decode_latex_text(slice(content, 0, tiny_idx))
    let after = decode_latex_text(trim(slice(content, tiny_idx + 5, len(content))))
    let before_box = if (before != "") box.text_box(before, css.TEXT, "mord") else null
    let after_box = if (after != "") tiny_text_box(after) else null
    let elems = text_content_elements(before_box, after_box)
    box.ml_box_full(
        <span class: css.BASE;
            for (el in elems) el
        >,
        0.7,
        0.09,
        text_content_width(before_box, after_box),
        "mord",
        0.0,
        0.0,
        0.7
    )
}

fn text_content_special_box(el, h, d, w) => {
    element: el,
    height: h,
    depth: d,
    width: w,
    type: "mord",
    italic: 0.0,
    skew: 0.0,
    max_font_size: h,
    model: "ml",
    suppress_hbox_text_depth: true,
    suppress_hbox_operator_height: true,
    no_left_bin_space: true
}

fn tiny_text_box(text) => {
    element: <span style: "font-size: 50%";
        <span class: css.TEXT; text>
    >,
    height: 0.35,
    depth: 0.04,
    width: 0.25 * float(len(text)),
    type: "mord",
    italic: 0.0,
    skew: 0.0
}

fn text_content_elements(before_box, after_box) {
    let before = if (before_box != null) [before_box.element] else []
    let after = if (after_box != null) [after_box.element] else []
    before ++ after
}

fn text_content_width(before_box, after_box) {
    (if (before_box != null) before_box.width else 0.0) +
    (if (after_box != null) after_box.width else 0.0)
}

fn decode_latex_text(text) {
    decode_latex_text_at(text, 0, "")
}

fn decode_latex_text_at(text, i, acc) {
    if (i >= len(text)) acc
    else if (is_latex_text_accent_at(text, i)) {
        let accent = slice(text, i + 1, i + 2)
        let base = slice(text, i + 3, i + 4)
        decode_latex_text_at(text, i + 5, acc ++ accented_text(accent, base))
    } else {
        decode_latex_text_at(text, i + 1, acc ++ slice(text, i, i + 1))
    }
}

fn is_latex_text_accent_at(text, i) =>
    i + 4 < len(text) and
    slice(text, i, i + 1) == "\\" and
    is_latex_text_accent(slice(text, i + 1, i + 2)) and
    slice(text, i + 2, i + 3) == "{" and
    slice(text, i + 4, i + 5) == "}"

fn is_latex_text_accent(accent) =>
    accent == "'" or accent == "\"" or accent == "." or accent == "`" or
    accent == "=" or accent == "~" or accent == "^"

fn accented_text(accent, base) {
    if (base == "a" and accent == "'") "á"
    else if (base == "a" and accent == "\"") "ä"
    else if (base == "a" and accent == ".") "ȧ"
    else if (base == "a" and accent == "`") "à"
    else if (base == "a" and accent == "=") "ā"
    else if (base == "a" and accent == "~") "ã"
    else if (base == "a" and accent == "^") "â"
    else base
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
            render_simple_accent(accent_key, base_box, accent_text, accent_cls, accent_height, node.base)
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

fn line_accent_box(el, h, d, w, visual_d, visual_total, suppress_text_depth) => {
    element: el,
    height: h,
    depth: d,
    width: w,
    type: "mord",
    italic: 0.0,
    skew: 0.0,
    max_font_size: h,
    model: "ml",
    suppress_hbox_text_depth: suppress_text_depth
}

fn line_accent_is_tall(base_box) {
    let total = base_box.height + base_box.depth
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

// Accent glyph widths from MathLive's Main-Regular font-metrics-data.ts.
// Used to derive accent margin-left: (base.width - accent.width)/2 + 2*skew.
// Accent glyph widths in Main-Regular (from MathLive font-metrics-data.ts).
// Most accents are 0.5em wide; the single dot (˙, U+02D9) is 0.27778.
fn accent_glyph_width(key) {
    if (key == "dot") 0.27778
    else if (key == "mathring") 0.75
    else 0.5
}

// Compute the accent margin-left per MathLive's accent.ts:
//   margin = (base.width - accent.width) / 2  (+ 2*skew, but skew only
//   applies to multi-atom bodies, which don't reach this path).
// Uses the REAL Math-Italic glyph width of the base char (not Lambda's
// approximate box width). Emitted via CEIL@2 to match MathLive's toString.
fn accent_margin_left(accent_key, base_node, base_box) {
    let base_text = plain_text(base_node)
    let base_w = accent_base_width(base_text, base_box)
    let acc_w = accent_glyph_width(accent_key)
    util.ceil_em2((base_w - acc_w) / 2.0)
}

// Full-precision base glyph width for accent centering. MathLive divides the
// unrounded Math-Italic width by 2, so the 2dp-rounded width can flip the
// ceil by 0.01em (e.g. v: 0.48472 -> 0 vs rounded 0.48 -> -0.01).
fn accent_base_width(base_text, base_box) {
    if (len(base_text) != 1) base_box.width
    else (let m = met.get_character_metrics(base_text, "Math-Italic"),
          if (m == null or m.default) base_box.width
          else if (m.width_raw != null) m.width_raw
          else m.width)
}

fn render_simple_accent(accent_key, base_box, accent_text, accent_cls, accent_height, base_node) {
    let base_elements = box.elements_of(base_box)
    // Use the base box's actual height for the inner wrapper. For tall
    // bases (uppercase letters with cmmi h=0.69), the wrapper needs to
    // reflect the real glyph height — Lambda previously hardcoded 0.44em
    // (short-letter height) which over-shrinks tall bases.
    let base_wrap_h = if (base_box.height > 0.5) base_box.height else 0.44
    // The accent is shifted UP by the difference between base height and
    // the short-letter reference (0.44em) so taller bases sit lower
    // relative to the accent glyph.
    let tall_extra = if (base_box.height > 0.5) base_box.height - 0.44 else 0.0
    let accent_top = if (accent_key == "tilde") (0.0 - 3.35 - tall_extra)
        else (0.0 - 3.0 - tall_extra)
    // Vlist height per MathLive's accent VBox:
    //   height = base.height - clearance + accent.height  (CEIL@2)
    //   clearance = min(base.height, X_HEIGHT=0.43056)
    // For short bases (a/x, height ≤ X_HEIGHT) this reduces to accent.height;
    // for tall bases (b/d/A/F) the base lifts the accent by its overshoot.
    let base_h = base_box.height
    let clearance = if (base_h < 0.43056) base_h else 0.43056
    let accent_h = accent_body_height_precise(accent_key)
    let vlist_h = util.ceil_em2(base_h - clearance + accent_h)
    // Centering margin per MathLive: (base.width - accent.width)/2.
    let margin_left = accent_margin_left(accent_key, base_node, base_box)
    let el = <span class: css.VLIST_T;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ fmt_accent_em(vlist_h);
                <span style: "top:-3em";
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: "height:" ++ fmt_accent_em(base_wrap_h) ++ ";display:inline-block";
                        for (el in base_elements) el
                    >
                >
                <span class: css.CENTER, style: "top:" ++ fmt_accent_em(accent_top) ++ ";margin-left:" ++ fmt_accent_margin(margin_left);
                    <span class: css.PSTRUT, style: "height:3em">
                    <span class: accent_cls, style: "height:" ++ fmt_accent_em(accent_height) ++ ";display:inline-block"; accent_text>
                >
            >
        >
    >
    accent_box_raw(el, vlist_h, base_box.width)
}

// A simple accent sits entirely above the baseline, so its vlist height can be
// used directly by the root strut.
fn accent_box_raw(el, h, w) => {
    element: el,
    height: h,
    depth: 0.0,
    width: w,
    type: "mord",
    italic: 0.0,
    skew: 0.0,
    max_font_size: h,
    model: "ml",
    no_left_bin_space: true
}

// Format an accent margin: MathLive emits a bare "0" (no unit) for zero,
// otherwise the em value.
fn fmt_accent_margin(v) {
    if (v == 0.0) "0" else fmt_accent_em(v)
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
    width: w,
    type: "mord",
    italic: 0.0,
    skew: 0.0,
    max_font_size: h,
    model: "ml",
    no_left_bin_space: true
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

// Precise accent glyph height in Main-Regular (from MathLive
// font-metrics-data.ts). Used to compute the accent VBox height.
fn accent_body_height_precise(key) {
    if (key == "vec" or key == "overrightarrow") 0.71444
    else if (key == "dot") 0.66786
    else if (key == "ddot") 0.66786
    else if (key == "tilde" or key == "widetilde") 0.66786
    else if (key == "bar" or key == "overline") 0.56778
    else if (key == "check") 0.62847
    else if (key == "breve") 0.69444
    else if (key == "acute") 0.69444
    else if (key == "grave") 0.69444
    else if (key == "hat" or key == "widehat") 0.69444
    else 0.69444
}

fn fmt_accent_em(v) {
    let rounded = round(v * 100.0) / 100.0
    (rounded) ++ "em"
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
    let left_char = small_left_right_char(left_text)
    let right_char = small_left_right_char(right_text)
    let left_el = small_delim_el(left_char, css.OPEN)
    let right_el = small_delim_el(right_char, css.CLOSE)
    let content_elements = box.child_elements(spaced)
    let style_attr = if (has_middle_delim(spaced, 0))
        "margin-top:-0.25em;height:1em"
    else if (has_suppressed_text_depth(spaced, 0))
        "margin-top:0em;height:0.69444em"
    else
        "margin-top:-0.08333em;height:0.72777em"
    let both_null = left_char == "." and right_char == "."
    let has_corner = delims.is_corner_delim(left_char) or delims.is_corner_delim(right_char)
    let has_surd = delims.is_surd_delim(left_char) or delims.is_surd_delim(right_char)
    let small_height = if (has_surd) 0.8 else if (has_corner) 0.65 else if (both_null) 0.65 else 0.75
    let small_depth = if (has_surd) 0.2
        else if (has_corner) 0.15
        else if (is_shallow_small_delim(left_char) or is_shallow_small_delim(right_char))
        0.24 else 0.25
    box.ml_box_full(
        <span class: css.LEFT_RIGHT, style: style_attr;
            left_el
            for (el in content_elements) el
            right_el
        >,
        small_height,
        if (both_null) content.depth else small_depth,
        content.width + 0.8,
        "minner",
        0.0,
        0.0,
        small_height
    )
}

fn small_left_right_char(delim_text) {
    if (is_arrow_delimiter_text(delim_text)) "\\\\"
    else delims.resolve_char(delim_text)
}

fn is_arrow_delimiter_text(delim_text) {
    delim_text == "\\uparrow" or delim_text == "\\downarrow" or
    delim_text == "\\updownarrow" or delim_text == "\\Uparrow" or
    delim_text == "\\Downarrow" or delim_text == "\\Updownarrow"
}

fn is_shallow_small_delim(ch) {
    ch == "⟮" or ch == "⟯" or ch == "⎰" or ch == "⎱"
}

fn small_delim_el(ch, side_class) {
    if (ch == ".") {
        let cls = css.classes([css.NULLDELIMITER, side_class])
        <span class: cls, style: "width:0.12em">
    } else if (delims.is_corner_delim(ch)) {
        let cls = css.classes([css.SMALL_DELIM, side_class])
        <span class: cls, style: "top:0.08em;font-size: 70%"; ch>
    } else {
        let cls = css.classes([css.SMALL_DELIM, side_class])
        <span class: cls; ch>
    }
}

fn has_suppressed_text_depth(items, i) {
    if (i >= len(items)) false
    else if (items[i].suppress_hbox_text_depth == true) true
    else has_suppressed_text_depth(items, i + 1)
}

fn has_middle_delim(items, i) {
    if (i >= len(items)) false
    else if (items[i].is_middle_delim == true) true
    else has_middle_delim(items, i + 1)
}

fn render_stretchy_delimiter_group(left_text, right_text, content) {
    let left_box = delims.render_left_right(left_text, content.height, content.depth, "mopen")
    let right_box = delims.render_left_right(right_text, content.height, content.depth, "mclose")
    let parts = [left_box, content, right_box]
    let elements = box.child_elements(parts)
    let style_attr = stretchy_left_right_style(content)
    let content_h = content.height
    let content_d = content.depth
    let box_h = max(content_h, max(delim_extent_h(left_box), delim_extent_h(right_box)))
    let box_d = max(content_d, max(delim_extent_d(left_box), delim_extent_d(right_box)))
    box.ml_box_full(
        <span class: css.LEFT_RIGHT, style: style_attr;
            for (el in elements) el
        >,
        box_h,
        box_d,
        sum((for (p in parts where p != null) p.width)),
        "minner",
        0.0,
        0.0,
        box_h
    )
}

fn stretchy_left_right_style(content) {
    let visual_depth = content.depth
    let visual_total = content.height + content.depth
    "margin-top:" ++ fmt_delim_em(0.0 - visual_depth) ++ ";height:" ++ fmt_delim_em(visual_total)
}

fn delim_extent_h(b) { b.height }
fn delim_extent_d(b) { b.depth }

// \left..\right strut height = CEIL@2 of the box's full-precision height+depth,
// computed ONCE (mirrors MathLive box.ts toString). The delimiter glyphs carry
// precise extent (delimiters.ls sized_delim_metrics) so a Size3 pair sums to 2.40003
// and rounds to 2.41 naturally — no special-casing.
fn stretchy_left_right_total(content, left_box, right_box) {
    let content_h = content.height
    let content_d = content.depth
    let box_h = max(content_h, max(delim_extent_h(left_box), delim_extent_h(right_box)))
    let box_d = max(content_d, max(delim_extent_d(left_box), delim_extent_d(right_box)))
    let box_total = util.ceil_em2(box_h + box_d)
    max(util.ceil_em2(content_h + content_d), box_total)
}

// \left..\right margin-top/height: MathLive emits these at full precision
// with trailing zeros trimmed (NOT CEIL@2) — 0.686, 1.069108, 0.08333. Build
// the 6dp fixed string with integer arithmetic (avoids float-to-string
// artifacts) then drop trailing zeros.
fn fmt_delim_em(v) {
    trim_trailing_zeros(util.fmt_fixed(v, 6)) ++ "em"
}

fn trim_trailing_zeros(s) {
    if (index_of(s, ".") < 0) s
    else trim_tz_loop(s, len(s))
}

fn trim_tz_loop(s, n) {
    if (n <= 0) s
    else (let tail = slice(s, n - 1, n),
          if (tail == "0") trim_tz_loop(s, n - 1)
          else if (tail == ".") slice(s, 0, n - 1)
          else slice(s, 0, n))
}

// ============================================================
// Sized delimiters (\big, \Big, etc.)
// ============================================================

fn render_sized_delim(node, context) {
    let delim_text = if (node.delim != null) string(node.delim) else ""
    let size_cmd = if (node.size != null) string(node.size) else ""
    let size_name = if (len(size_cmd) > 0 and slice(size_cmd, 0, 1) == "\\")
        slice(size_cmd, 1, len(size_cmd)) else size_cmd
    render_sized_delim_text(size_name, delim_text)
}

fn render_sized_delim_text(size_name, delim_text) {
    let scale = if (sym.get_delim_size(size_name) != null) sym.get_delim_size(size_name) else 1.0
    if (is_valid_sized_delim_text(delim_text))
        delims.render_at_scale(delim_text, scale, "mord")
    else
        delims.render_at_scale("", scale, "mord")
}

fn is_valid_sized_delim_text(delim_text) {
    delim_text == "(" or delim_text == ")" or
    delim_text == "[" or delim_text == "]" or
    delim_text == "{" or delim_text == "}" or
    delim_text == "\\{" or delim_text == "\\}" or
    delim_text == "|" or delim_text == "\\|" or
    delim_text == "\\vert" or delim_text == "\\Vert" or
    delim_text == "\\lvert" or delim_text == "\\rvert" or
    delim_text == "\\lVert" or delim_text == "\\rVert" or
    delim_text == "<" or delim_text == ">" or
    delim_text == "\\langle" or delim_text == "\\rangle"
}

fn is_sized_delim_pair(node, i) {
    if (i + 1 >= len(node)) false
    else
        (let child = node[i],
         child is element and name(child) == 'sized_delimiter' and child.delim == null and
         child.size != null and sized_pair_text(node[i + 1]) != null)
}

// Detect `\big` (or `Big`/`bigg`/`Bigg`) followed by 2–5 alpha letters that
// form a known big-operator command (\bigcup, \bigcap, \bigvee, \bigwedge,
// \bigodot, \bigoplus, \bigotimes, \bigsqcup, \biguplus). The grammar matches
// `\big` greedily as a sized-delim prefix and leaves the suffix as plain
// strings; this reconstructs the intended command at render time.
//
// Sub/sup attached to the last reconstructed letter (e.g., `\bigcup_{i\in I}`
// parses as `... p_{...}` with the subsup base = `p`) is also recovered by
// peeking at the trailing `subsup` and stealing its base letters.
//
// Returns {cmd_name: "bigcup", subsup: subsup_node | null, consumed: N} or null.
fn try_sized_delim_word_combine(node, i) {
    if (i + 1 >= len(node)) null
    else (let cur = node[i],
        if (not (cur is element)) null
        else if (name(cur) != 'sized_delimiter') null
        else if (cur.delim != null) null
        else if (cur.size == null) null
        else (let size_name = string(cur.size),
            if (not is_plain_sized_command(size_name)) null
            else attempt_sized_combine(node, i, size_name)))
}

fn attempt_sized_combine(node, i, size_name) {
    // Collect per-node alpha letters following the sized-delim. Each node
    // contributes a string of one or more letters; preserve the per-node
    // boundary so we can match by node count and also pull a final letter
    // out of a trailing `subsup` base.
    let chunks = collect_letter_chunks(node, i + 1, [], 0)
    // chunks = {parts: [str,...], count: nodes_consumed}
    let opt_a = try_prefix_match(size_name, chunks.parts, len(chunks.parts))
    // opt_a = {cmd_name, n_parts} or null — n_parts ≤ len(chunks.parts)
    let next_idx = i + 1 + chunks.count
    let sub_base_text = if (next_idx >= len(node)) null
        else (let nxt = node[next_idx],
            if (nxt is element and name(nxt) == 'subsup')
                bare_letter_of(nxt.base)
            else null)
    // Try B: use ALL bare chunks + subsup base
    let bare_full = concat_strings(chunks.parts, len(chunks.parts), "")
    let opt_b = if (sub_base_text != null
            and sym.lookup_symbol("\\" ++ size_name ++ bare_full ++ sub_base_text) != null) {
            {cmd_name: size_name ++ bare_full ++ sub_base_text,
             subsup: node[next_idx],
             consumed: chunks.count + 2}
        } else null
    if (opt_b != null) opt_b
    else if (opt_a != null) {
        {cmd_name: opt_a.cmd_name, subsup: null, consumed: opt_a.n_parts + 1}
    } else null
}

fn try_prefix_match(size_name, parts, n) {
    if (n <= 0) null
    else (let text = concat_strings(parts, n, ""),
        if (sym.lookup_symbol("\\" ++ size_name ++ text) != null) {
            {cmd_name: size_name ++ text, n_parts: n}
        } else try_prefix_match(size_name, parts, n - 1))
}

fn concat_strings(parts, n, acc) {
    if (n <= 0) acc
    else concat_strings(parts, n - 1, parts[n - 1] ++ acc)
}

fn collect_letter_chunks(node, j, acc, count) {
    if (j >= len(node)) {
        {parts: acc, count: count}
    } else {
        let child = node[j]
        let txt = bare_letter_of(child)
        if (txt == null) {
            {parts: acc, count: count}
        } else collect_letter_chunks(node, j + 1, acc ++ [txt], count + 1)
    }
}

fn collect_bare_letters(node, j, acc, count) {
    if (j >= len(node)) {
        {text: acc, count: count}
    } else {
        let child = node[j]
        let txt = bare_letter_of(child)
        if (txt == null) {
            {text: acc, count: count}
        } else collect_bare_letters(node, j + 1, acc ++ txt, count + 1)
    }
}

fn bare_letter_of(child) {
    if (child == null) null
    else if (child is string) (
        let s = string(child),
        if (len(s) > 0 and len(s) <= 6 and is_alpha_text(s)) s
        else null)
    else if (child is element and (name(child) == 'symbol' or name(child) == 'word')) (
        let s = get_text(child),
        if (len(s) > 0 and len(s) <= 6 and is_alpha_text(s)) s
        else null)
    else null
}

fn render_combined_big_command(combo, context) {
    let unicode = sym.lookup_symbol(combo.cmd_name)
    if (unicode == null) render_unknown_command("\\\\" ++ combo.cmd_name)
    else if (combo.subsup == null) render_limit_operator_symbol(unicode, context)
    else (let synthetic_base = <command name: combo.cmd_name>,
          let synthetic_subsup = <subsup base: synthetic_base, sub: combo.subsup.sub, sup: combo.subsup.sup>,
          scripts.render(synthetic_subsup, context, render_node))
}

// Detect an ERROR node carrying a truncated command prefix (e.g., "\left",
// "\Left", "\right") followed by alpha letters that, when concatenated, form
// a known relation/symbol/big-op command (\leftarrow, \Leftrightarrow,
// \rightharpoonup, etc.). The grammar matches `\left` as a delimiter prefix
// before letting longer command tokens win, so we recover the intended token
// at render time.
//
// Returns {cmd_name: "leftarrow", consumed: N} or null.
fn try_error_prefix_word_combine(node, i) {
    if (i + 1 >= len(node)) null
    else (let cur = node[i],
        if (not (cur is element)) null
        else if (name(cur) != 'ERROR') null
        else (let raw = plain_text(cur),
            if (len(raw) < 2 or slice(raw, 0, 1) != "\\") null
            else (let prefix = slice(raw, 1, len(raw)),
                if (not is_alpha_text(prefix)) null
                else attempt_error_prefix_combine(node, i, prefix))))
}

fn attempt_error_prefix_combine(node, i, prefix) {
    let chunks = collect_letter_chunks(node, i + 1, [], 0)
    let opt_a = try_error_prefix_match(prefix, chunks.parts, len(chunks.parts))
    if (opt_a == null) null
    else {
        {cmd_name: opt_a.cmd_name, consumed: opt_a.n_parts + 1}
    }
}

fn try_error_prefix_match(prefix, parts, n) {
    if (n <= 0) null
    else (let text = concat_strings(parts, n, ""),
        if (sym.lookup_symbol("\\" ++ prefix ++ text) != null) {
            {cmd_name: prefix ++ text, n_parts: n}
        } else try_error_prefix_match(prefix, parts, n - 1))
}

fn render_combined_error_prefix_command(combo, context) {
    let unicode = sym.lookup_symbol(combo.cmd_name)
    if (unicode == null) render_unknown_command("\\\\" ++ combo.cmd_name)
    else (let atom_type = sym.classify_symbol(combo.cmd_name),
        if (sym.is_limit_op(combo.cmd_name)) render_limit_operator_symbol(unicode, context)
        else box.text_box(unicode, symbol_font_class(combo.cmd_name, context), atom_type))
}

fn render_sized_delim_pair(cmd_node, delim_node, context) {
    let size_name = string(cmd_node.size)
    let delim_text = sized_pair_text(delim_node)
    if (is_unknown_sized_letter(size_name, delim_text))
        render_unknown_command("\\\\" ++ size_name ++ delim_text)
    else
        box_with_type(render_sized_delim_text(size_name, delim_text), sized_delim_atom_type(size_name))
}

fn sized_delim_atom_type(size_name) {
    if (sized_command_suffix(size_name) == "l" or sized_command_suffix(size_name) == "r") "mopen"
    else "mord"
}

fn sized_command_suffix(size_name) {
    if (len(size_name) == 0) ""
    else slice(size_name, len(size_name) - 1, len(size_name))
}

fn sized_pair_text(node) {
    if (node is string) slice(string(node), 0, 1)
    else if (node is element and (name(node) == 'punctuation' or name(node) == 'operator' or name(node) == 'relation'))
        get_text(node)
    else null
}

fn is_unknown_sized_letter(size_name, delim_text) {
    is_plain_sized_command(size_name) and is_single_alpha_delim(delim_text)
}

fn is_plain_sized_command(size_name) {
    size_name == "big" or size_name == "Big" or size_name == "bigg" or size_name == "Bigg"
}

fn is_single_alpha_delim(text) {
    len(text) == 1 and is_alpha_text(text)
}

fn render_unknown_command(cmd) {
    let bx = box.ml_box_full(<span class: css.classes([css.ERROR, css.CMR]); cmd>,
        0.7, 0.0, 0.4 * float(len(cmd)), "mord", 0.0, 0.0, 0.7)
    box_with_error(bx, "unknown-command")
}

fn box_with_error(bx, err) => {
    element: bx.element,
    height: bx.height,
    depth: bx.depth,
    width: bx.width,
    type: bx.type,
    italic: bx.italic,
    skew: bx.skew,
    max_font_size: bx.max_font_size,
    model: "ml",
    mathlive_error: err
}

// ============================================================
// Middle delimiter (\middle|)
// ============================================================

fn render_middle_delim(node, context) {
    if (node.delim == null) {
        {
            element: <span class: css.NULLDELIMITER, style: "width:0.12em">,
            height: 0.0,
            depth: 0.0,
            width: 0.12,
            type: "mord",
            italic: 0.0,
            skew: 0.0,
            max_font_size: 0.0,
            model: "ml",
            suppress_hbox_text_depth: true
        }
    } else {
        let delim_text = string(node.delim)
        let display_char = delims.resolve_char(delim_text)
        let bx = box.text_box(display_char, css.SMALL_DELIM, "mord")
        {
            element: bx.element,
            height: bx.height,
            depth: bx.depth,
            width: bx.width,
            type: bx.type,
            italic: bx.italic,
            skew: bx.skew,
            max_font_size: bx.max_font_size,
            model: "ml",
            is_middle_delim: true
        }
    }
}

// ============================================================
// Radical (\sqrt)
// ============================================================

fn render_radical(node, context) {
    let body_context = ctx.derive(context, {cramped: true})
    let body_box = if (node.radicand != null) render_node(node.radicand, body_context)
        else box.text_box("", null, "ord")

    let compact_index = body_box.height <= 0.5 and body_box.depth <= 0.1
    let index_box = if (node.index != null)
        render_sqrt_index(render_node(node.index, ctx.sup_context(context)), context, compact_index)
    else null

    let spec = sqrt_spec(body_box, context, node.index != null)
    let body_elements = box.elements_of(body_box)
    let child_elements = if (index_box != null)
        [index_box.element, sqrt_sign_element(spec), sqrt_vlist_element(spec, body_elements)]
    else
        [sqrt_unindexed_element(spec, body_elements)]
    let el = <span class: css.BASE;
        for (child in child_elements) child
    >
    radical_box(el, spec, body_box.width + 0.52, context)
}

fn radical_box(el, spec, width, context) => {
        element: el,
        height: if (spec.box_height != null) spec.box_height else spec.height,
        depth: if (spec.box_depth != null) spec.box_depth else spec.depth,
        width: width,
        type: "mord",
        italic: 0.0,
        skew: 0.0,
        max_font_size: if (spec.box_height != null) spec.box_height else spec.height,
        model: "ml",
        is_radical: true,
        is_script_radical: context.style == "script" or context.style == "scriptscript"
}

fn sqrt_unindexed_element(spec, body_elements) {
    <span style: "display:inline-block;height:" ++ util.fmt_em(spec.visual_total);
        sqrt_sign_element(spec)
        sqrt_vlist_element(spec, body_elements)
    >
}

fn sqrt_sign_element(spec) {
    let cls = spec.sign_class
    let sign_style = if (spec.sign_top == null) null else "top:" ++ util.fmt_em(spec.sign_top)
    if (sign_style == null) {
        <span class: css.SQRT_SIGN;
            <span class: cls; "\u221A">
        >
    } else {
        <span class: css.SQRT_SIGN, style: sign_style;
            <span class: cls; "\u221A">
        >
    }
}

fn sqrt_vlist_element(spec, body_elements) {
    if (spec.is_tall == true) sqrt_tall_vlist_element(spec, body_elements)
    else sqrt_small_vlist_element(spec, body_elements)
}

// the inner vlist height is the body stack extent (maxPos), which differs from
// the box height when the surd glyph extends past the body (tall radicals).
fn sqrt_vlist_height(spec) {
    if (spec.vlist_height != null) spec.vlist_height else spec.height
}

fn sqrt_small_vlist_element(spec, body_elements) {
    let body_style = "height:" ++ fmt_sqrt_body_height(spec.body_height) ++ ";display:inline-block"
    let pstrut_style = "height:" ++ util.fmt_em(spec.pstrut)
    <span class: css.VLIST_T;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(sqrt_vlist_height(spec));
                <span style: "top:" ++ util.fmt_em(spec.body_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span style: body_style;
                        for (child in body_elements) child
                    >
                >
                <span style: "top:" ++ util.fmt_em(spec.line_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span class: css.SQRT_LINE, style: "height:" ++ util.fmt_em(spec.line_height) ++ ";display:inline-block">
                >
            >
        >
    >
}

fn sqrt_tall_vlist_element(spec, body_elements) {
    let body_style = "height:" ++ util.fmt_em(spec.body_height) ++ ";display:inline-block"
    let pstrut_style = "height:" ++ util.fmt_em(spec.pstrut)
    <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(sqrt_vlist_height(spec));
                <span style: "top:" ++ util.fmt_em(spec.body_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span style: body_style;
                        for (child in body_elements) child
                    >
                >
                <span style: "top:" ++ util.fmt_em(spec.line_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span class: css.SQRT_LINE, style: "height:" ++ util.fmt_em(spec.line_height) ++ ";display:inline-block">
                >
            >
            <span class: css.VLIST_S; "\u200B">
        >
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(spec.depth_holder)>
        >
    >
}

fn fmt_sqrt_body_height(h) {
    if (h == 0.0) "0" else util.fmt_em(h)
}

fn render_sqrt_index(index_box, context, compact_index) {
    let is_empty = index_box.height == 0.0 and index_box.depth == 0.0
    let is_script = context.style == "script" or context.style == "scriptscript"
    let is_sized = context.size > 1.0
    let is_compact = is_sized or compact_index
    let elements = if (is_empty) ["\u00A0"] else box.elements_of(index_box)
    // The radical index renders at scriptscript scale (font-size 50%). Its
    // content-wrapper height is the index glyph's metric height × 0.5 (CEIL@2)
    // — e.g. `n` (0.43056) → 0.22, not the old hardcoded 0.33. The vlist
    // height follows as -top - 3 + child_h.
    let idx_h = index_box.height
    let derived_child = util.ceil_em2(idx_h * 0.5)
    let top = if (is_script and is_empty) -3.26
        else if (is_empty) -3.32
        else if (is_compact) -3.32
        else -3.45
    let child_h = if (is_empty) 0.0 else if (is_sized) 0.32 else derived_child
    let h = if (is_script and is_empty) 0.27
        else if (is_empty) 0.33
        else if (is_sized) 0.64
        else if (is_compact) (0.0 - top - 3.0 + child_h)
        else 0.78
    let child_font = if (is_script and is_empty) "71.43%"
        else if (is_sized) "48.24%"
        else "50%"
    {
        element: <span class: css.SQRT_INDEX;
            <span class: css.VLIST_T;
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:" ++ util.fmt_em(h);
                        <span style: "top:" ++ util.fmt_em(top);
                            <span class: css.PSTRUT, style: "height:3em">
                            <span style: "height:" ++ fmt_sqrt_body_height(child_h) ++
                                ";display:inline-block;font-size: " ++ child_font;
                                for (child in elements) child
                            >
                        >
                    >
                >
            >
        >,
        height: h,
        depth: 0.0,
        width: index_box.width * 0.5,
        type: "mord",
        italic: 0.0,
        skew: 0.0
    }
}

fn sqrt_spec(body_box, context, has_index) {
    if ((context.style == "script" or context.style == "scriptscript") and has_index)
        make_sqrt_spec(0.73, 0.27, body_box.height, -3.0, -3.62, 0.08, 3.0, 0.05, css.SMALL_DELIM)
    else if (context.style == "script" or context.style == "scriptscript")
        make_script_sqrt_spec(body_box)
    else
        sqrt_geom(body_box, context)
}

// Metric-driven radical geometry — a faithful port of TeXBook Rule 11 as
// implemented by MathLive's SurdAtom.render + makeCustomSizedDelim + VBox
// (ref/mathlive/src/atoms/surd.ts, core/delimiters.ts, core/v-box.ts).
// Covers display & text styles (scalingFactor 1.0); script/scriptscript stay
// on the legacy specs above. Every dimension derives from the body's
// full-precision height/depth plus the surd font metrics — no bucket dispatch.
fn sqrt_metric_h(b) { b.height }
fn sqrt_metric_d(b) { b.depth }

fn sqrt_geom(body_box, context) {
    let inner_h = sqrt_metric_h(body_box)
    let inner_d = sqrt_metric_d(body_box)
    let is_empty = inner_h == 0.0 and inner_d == 0.0
    let is_display = context.style == "display"
    let si = met.style_index(context.style)
    let factor = met.style_scale(context.style)
    let rule_width = met.defaultRuleThickness[si] / factor
    // φ = σ5 (x-height) in display, else the rule thickness (TeXBook p.443)
    let phi = if (is_display) met.X_HEIGHT else rule_width
    // ψ = θ + ¼|φ|, the body↔line clearance
    let lc0 = factor * (rule_width + phi / 4.0)
    let inner_total = max(factor * 2.0 * phi, inner_h + inner_d)
    let min_delim = inner_total + lc0 + rule_width
    let surd = surd_metrics(min_delim)
    let surd_total = surd.h + surd.d
    let delim_depth = surd_total - rule_width
    // re-center the body when the surd is taller than the body needs
    let lc = if (delim_depth > inner_h + inner_d + lc0)
        (lc0 + delim_depth - (inner_h + inner_d)) / 2.0
        else lc0
    // setTop on the surd sign (only applied when |t| > 0.01, per Box.setTop)
    let t = surd.h - inner_h - lc
    let apply = abs(t) > 0.01
    let body_h = inner_h + lc
    let delim_h = if (apply) body_h else surd.h
    let delim_d = if (apply) (surd_total - body_h) else surd.d
    let result_h = max(delim_h, body_h)
    let result_d = max(delim_d, inner_d)
    // pstrut = max(maxFontSize, child heights) + 2; an empty body has no
    // unscaled glyph (maxFontSize 0 → 2.04), otherwise the base sits at 1.0
    let max_font = if (is_empty) 0.0 else 1.0
    let pstrut = max(max(max_font, inner_h), rule_width) + 2.0
    {
        height: util.ceil_em2(result_h),
        // depth projection rounds toward zero so the negated vertical-align
        // matches MathLive's per-emission ceil.
        depth: 0.0 - util.ceil_em2(0.0 - result_d),
        box_height: result_h,
        box_depth: result_d,
        visual_total: util.ceil_em2(result_h + result_d),
        vlist_height: util.ceil_em2(body_h),
        body_height: util.ceil_em2(inner_h + inner_d),
        body_top: util.ceil_em2(0.0 - pstrut),
        line_top: util.ceil_em2(0.0 - pstrut - (body_h - 2.0 * rule_width)),
        sign_top: if (apply) util.ceil_em2(t) else null,
        pstrut: util.ceil_em2(pstrut),
        line_height: util.ceil_em2(rule_width),
        sign_class: surd.cls,
        is_tall: inner_d > 0.0,
        depth_holder: util.ceil_em2(inner_d)
    }
}

// Choose the smallest surd glyph whose natural height+depth exceeds the
// required delimiter height. Totals (Main-Regular small, then Size1–Size4):
// 1.0, 1.20001, 1.80002, 2.40003, 3.00003 (ref font-metrics-data U+221A).
fn surd_metrics(min_delim) {
    let h = if (1.0 > min_delim) 0.8
        else if (1.20001 > min_delim) 0.85
        else if (1.80002 > min_delim) 1.15
        else if (2.40003 > min_delim) 1.45
        else 1.75
    let d = if (1.0 > min_delim) 0.2
        else if (1.20001 > min_delim) 0.35001
        else if (1.80002 > min_delim) 0.65002
        else if (2.40003 > min_delim) 0.95003
        else 1.25003
    let cls = if (1.0 > min_delim) css.SMALL_DELIM
        else if (1.20001 > min_delim) css.DELIM_SIZE1
        else if (1.80002 > min_delim) css.DELIM_SIZE2
        else if (2.40003 > min_delim) css.DELIM_SIZE3
        else css.DELIM_SIZE4
    let r = {h: h, d: d, cls: cls}
    r
}

fn make_sqrt_spec(h, d, body_h, body_top, line_top, sign_top, pstrut, line_h, sign_class) => {
    height: h,
    depth: d,
    visual_total: if (h + d < 1.21) 1.21 else h + d,
    body_height: body_h,
    body_top: body_top,
    line_top: line_top,
    sign_top: sign_top,
    pstrut: pstrut,
    line_height: line_h,
    sign_class: sign_class
}

fn make_script_sqrt_spec(body_box) => {
    height: 0.84,
    depth: 0.09,
    visual_total: 1.0,
    body_height: max(body_box.height, 0.83),
    body_top: -3.0,
    line_top: -3.75,
    sign_top: -0.03,
    pstrut: 3.0,
    line_height: 0.04,
    sign_class: css.SMALL_DELIM,
    is_tall: true,
    depth_holder: 0.09
}

// ============================================================
// Default fallback
// ============================================================

// text_group elements wrap the brace tokens (`{`, `}`) into the child list
// alongside the actual content. Strip the literal brace strings so they don't
// appear in the rendered output (style commands like `\mathit{text}` would
// otherwise emit `{text}`).
fn render_text_group(node, context) {
    let n = len(node)
    if (n == 0) box.text_box("", null, "mord")
    else {
        let children = (for (i in 0 to (n - 1),
            let child = node[i]
            where child != null and not is_brace_string(child))
            render_node(child, context))
        if (len(children) == 0) box.text_box("", null, "mord")
        else box.hbox(children)
    }
}

fn is_brace_string(child) {
    if (not (child is string)) false
    else (let s = string(child),
        s == "{" or s == "}")
}

fn render_default(node, context) {
    let children = render_children(node, context)
    if (len(children) > 0 and name(node) == '_seq') {
        let hb = box.hbox(apply_spacing(children, context))
        if (is_malformed_right_fraction_sequence(node))
            box_with_type(hb, "minner")
        else hb
    }
    else if (len(children) > 0) box.hbox(children)
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
         let tail = filtered[last_idx],
         let typed = box_with_type(hb, tail.type),
         if (has_suppressed_text_depth(filtered, 0))
            box_with_suppress_depth(typed)
         else typed)
}

fn render_children_scan(node, context, i, acc) {
    if (i >= len(node)) acc
    else if (is_empty_not_target_sequence(node, i))
        (let collected = collect_empty_not_targets(node, i, ""),
         let rendered = render_empty_not_targets(collected[0]),
         render_children_scan(node, context, collected[1], acc ++ [rendered]))
    else if (is_not_target_sequence(node, i))
        (let target_text = not_target_text(node[i + 1]),
         let rendered = render_not_overlay(target_text),
         render_children_scan(node, context, i + 2, acc ++ [rendered]))
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
         let spacer = if (last_box_is_colorbox(acc)) [box.skip_box(0.17)] else [],
         render_children_scan(node, context, i + 8, acc ++ spacer ++ [rendered]))
    else if (is_size_switch(node, i))
        (let rendered = render_size_switch_tail(node, context, i),
         let spacer = if (has_trailing_radical(acc)) [box.skip_box(0.17)] else [],
         acc ++ spacer ++ [rendered])
    else if (is_malformed_left_sequence(node, i))
        (let rendered = render_malformed_left_sequence(node[i], node[i + 1], context),
         render_children_scan(node, context, i + 2, acc ++ [rendered]))
    else if (is_scriptstyle_switch(node, i))
        (let rendered = render_scriptstyle_sibling(node[i + 1], context),
         let spacer = if (has_trailing_radical(acc)) [box.skip_box(0.17)] else [],
         render_children_scan(node, context, i + 2, acc ++ spacer ++ [rendered]))
    else if (try_sized_delim_word_combine(node, i) != null)
        (let combo = try_sized_delim_word_combine(node, i),
         let rendered = render_combined_big_command(combo, context),
         render_children_scan(node, context, i + combo.consumed, acc ++ [rendered]))
    else if (try_error_prefix_word_combine(node, i) != null)
        (let combo = try_error_prefix_word_combine(node, i),
         let rendered = render_combined_error_prefix_command(combo, context),
         render_children_scan(node, context, i + combo.consumed, acc ++ [rendered]))
    else if (is_sized_delim_pair(node, i))
        (let rendered = render_sized_delim_pair(node[i], node[i + 1], context),
         render_children_scan(node, context, i + 2, acc ++ [rendered]))
    else if (is_null_middle_sequence(node, i))
        (let rendered = render_middle_delim(node[i], context),
         render_children_scan(node, context, i + 2, acc ++ [rendered]))
    else
        (let child = node[i],
         let leading_spacer = if (has_trailing_radical(acc) and is_fraction_like_sequence_node(child))
             [box.skip_box(0.17)] else [],
         let next_acc = if (child == null) acc
             else if (is_dollar_error(child)) acc
             else if (child is string) acc ++ render_text_atoms(string(child), context)
             else acc ++ leading_spacer ++ [render_node(child, context)],
         render_children_scan(node, context, i + 1, next_acc))
}

fn is_fraction_like_sequence_node(child) {
    if (not (child is element)) false
    else {
        let tag = name(child)
        tag == 'fraction' or tag == 'frac_like' or
            (tag == '_seq' and len(child) > 0 and is_fraction_like_sequence_node(child[0]))
    }
}

fn is_malformed_right_fraction_sequence(node) {
    if (len(node) <= 1) false
    else
        is_fraction_like_sequence_node(node[0]) and
            is_malformed_right_sequence_command(node[1])
}

fn is_malformed_right_sequence_command(child) {
    child is element and name(child) == 'command' and
        command_name(child) == "right" and len(child) == 1
}

fn is_malformed_left_sequence(node, i) {
    if (i + 1 >= len(node)) false
    else
        (let child = node[i],
         let next = node[i + 1],
         is_left_error_node(child) and next is element and name(next) == 'group')
}

fn is_left_error_node(child) {
    child is element and name(child) == 'ERROR' and plain_text(child) == "\\left"
}

fn render_malformed_left_sequence(err_node, delim_node, context) {
    let left_box = render_unknown_display_command("\\\\left")
    let delim_box = render_node(delim_node, context)
    box_with_type(box.hbox([left_box, delim_box]), "minner")
}

fn render_malformed_right_command(node, context) {
    let right_box = render_unknown_display_command("\\\\right")
    let delim_box = render_node(node[0], context)
    box_with_type(box.hbox([right_box, delim_box]), "minner")
}

fn render_error_node(node, context) {
    let text = plain_text(node)
    if (text == "\\left") render_unknown_display_command("\\\\left")
    else render_unknown_display_command(text)
}

fn render_unknown_display_command(text) {
    {
        element: <span class: css.classes([css.ERROR, css.CMR]); text>,
        height: 0.7,
        depth: 0.0,
        width: 0.4 * float(len(text)),
        type: "mord",
        italic: 0.0,
        skew: 0.0
    }
}

fn is_null_middle_sequence(node, i) {
    if (i + 1 >= len(node)) false
    else
        (let child = node[i],
         child is element and name(child) == 'middle_delim' and child.delim == null)
}

fn is_empty_not_target_sequence(node, i) {
    if (i + 1 >= len(node)) false
    else
        (let child = node[i],
         let target = node[i + 1],
         is_empty_not_command(child) and not_target_text(target) != null)
}

fn collect_empty_not_targets(node, i, text) {
    if (is_empty_not_target_sequence(node, i)) {
        let target_text = not_target_text(node[i + 1])
        collect_empty_not_targets(node, i + 2, text ++ "\uE020" ++ target_text)
    } else {
        [text, i]
    }
}

fn render_empty_not_targets(text) {
    let bx = box.text_box(text, css.CMR, "mrel")
    box.ml_box_full(bx.element, 0.7, 0.19, bx.width, "mrel", 0.0, 0.0, 0.7)
}

fn is_not_target_sequence(node, i) {
    if (i + 1 >= len(node)) false
    else
        (let child = node[i],
         let target = node[i + 1],
         is_not_command_for_sequence(child) and not_target_text(target) != null)
}

fn is_empty_not_command(child) {
    child is element and name(child) == 'command' and command_name(child) == "not" and
    len(child) == 1 and is_empty_group(child[0])
}

fn is_not_command_for_sequence(child) {
    child is element and name(child) == 'command' and command_name(child) == "not" and
    (len(child) == 0 or (len(child) == 1 and is_empty_group(child[0])))
}

fn command_name(child) {
    let cmd_text = get_text(child)
    if (len(cmd_text) > 0 and slice(cmd_text, 0, 1) == "\\")
        slice(cmd_text, 1, len(cmd_text))
    else cmd_text
}

fn is_empty_group(child) {
    child is element and (name(child) == 'group' or name(child) == 'brack_group') and len(child) == 0
}

fn not_target_text(target) {
    if (not (target is element)) null
    else
        (let tag = name(target),
         if (tag == 'relation') relation_display_text(get_text(target))
         else if (tag == 'command' or tag == 'symbol_command')
            (let cmd_text = get_text(target),
             let unicode = sym.lookup_symbol(cmd_text),
             if (unicode != null) unicode else null)
         else null)
}

fn relation_display_text(text) {
    if (text == "<") "<"
    else if (text == ">") ">"
    else text
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
    let elements = box.child_elements(spaced)
    box_with_suppress_depth({
        element: <span style: "color:" ++ color_value;
            for (el in elements) el
        >,
        height: hb.height,
        depth: hb.depth,
        width: hb.width,
        type: hb.type,
        italic: hb.italic,
        skew: hb.skew,
        max_font_size: hb.max_font_size,
        model: hb.model,
        is_middle_delim: has_middle_delim(spaced, 0)
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

fn is_size_switch(node, i) {
    let child = if (i < len(node)) node[i] else null
    child is element and name(child) == 'command' and len(child) == 0 and
        is_math_size_name(command_name(child))
}

fn render_size_switch_tail(node, context, i) {
    let scale = math_size_scale(command_name(node[i]))
    let sized_context = ctx.derive(context, {size: scale})
    let children = render_children_scan(node, sized_context, i + 1, [])
    let spaced = apply_spacing(children, sized_context)
    let hb = transparent_hbox(spaced)
    let elements = box.child_elements(spaced)
    let pct = (round(scale * 1000.0) / 10.0) ++ "%"
    let scaled_height = hb.height * scale
    let scaled_depth = hb.depth * scale
    let report_height = if (scale > 2.0) round(scaled_height * 100.0) / 100.0 else scaled_height
    let report_depth = if (scale > 2.0) round(scaled_depth * 100.0) / 100.0 else scaled_depth
    {
        element: <span style: "font-size: " ++ pct;
            for (el in elements) el
        >,
        height: report_height,
        depth: report_depth,
        width: hb.width * scale,
        type: hb.type,
        italic: hb.italic * scale,
        skew: hb.skew * scale,
        max_font_size: if (hb.max_font_size != null) hb.max_font_size * scale else report_height,
        model: "ml",
        is_middle_delim: has_middle_delim(spaced, 0)
    }
}

fn is_math_size_name(size_name) {
    math_size_scale(size_name) != 0.0
}

fn math_size_scale(size_name) {
    if (size_name == "tiny") 0.5
    else if (size_name == "scriptsize") 0.7
    else if (size_name == "footnotesize") 0.8
    else if (size_name == "small") 0.9
    else if (size_name == "normalsize") 1.0
    else if (size_name == "large") 1.2
    else if (size_name == "Large") 1.44
    else if (size_name == "LARGE") 1.728
    else if (size_name == "huge") 2.074
    else if (size_name == "Huge") 2.488
    else 0.0
}

fn render_scriptstyle_sibling(child, context) {
    let bx = render_node(child, ctx.derive(context, {style: "script"}))
    style_wrap_box(bx, "font-size: 70%")
}

fn has_trailing_radical(items) {
    if (len(items) == 0) false
    else items[len(items) - 1].is_radical == true
}

fn style_wrap_box(bx, style_text) {
    ml_style_wrap_box(bx, style_text)
}

fn ml_style_wrap_box(bx, style_text) => {
    element: <span style: style_text;
        for (el in box.elements_of(bx)) el
    >,
    height: bx.height,
    depth: 0.0,
    width: bx.width,
    type: bx.type,
    italic: bx.italic,
    skew: bx.skew,
    max_font_size: bx.max_font_size,
    model: "ml",
    suppress_hbox_text_depth: true,
    is_middle_delim: bx.is_middle_delim,
    is_colorbox: bx.is_colorbox
}

fn last_box_is_colorbox(items) {
    if (len(items) == 0) false
    else items[len(items) - 1].is_colorbox == true
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
    let content_text = plain_text(content_arg)
    let children = if (is_dollar_math_group(content_arg))
        [render_dollar_math_group(content_arg, context)]
    else if (content_text == "red")
        [
            box.make_box(<span class: css.MATHIT, style: "margin-right:0.03em"; "r">,
                0.7, 0.08, 0.5, "mord"),
            render_text("ed", context)
        ]
    else render_children(content_arg, context)
    let spaced = apply_spacing(children, context)
    let hb = transparent_hbox(spaced)
    let elements = box.child_elements(spaced)
    box_with_suppress_depth({
        element: <span style: "color:" ++ color_value;
            for (el in elements) el
        >,
        height: hb.height,
        depth: hb.depth,
        width: hb.width,
        type: hb.type,
        italic: hb.italic,
        skew: hb.skew,
        max_font_size: hb.max_font_size,
        model: hb.model
    })
}

fn is_dollar_math_group(content_arg) =>
    (let txt = plain_text(content_arg),
     len(txt) >= 2 and slice(txt, 0, 1) == "$" and slice(txt, len(txt) - 1, len(txt)) == "$")

fn render_dollar_math_group(content_arg, context) {
    let txt = plain_text(content_arg)
    let inner = trim(slice(txt, 1, len(txt) - 1))
    let ast^err = parse(inner, {type: "math", flavor: "latex"})
    if (^err) render_node(content_arg, context)
    else render_node(ast, context)
}

fn box_with_suppress_depth(bx) {
    ml_box_with_suppress_depth(bx)
}

fn ml_box_with_suppress_depth(bx) => {
    element: bx.element,
    height: bx.height,
    depth: 0.0,
    width: bx.width,
    type: bx.type,
    italic: bx.italic,
    skew: bx.skew,
    max_font_size: bx.max_font_size,
    model: "ml",
    suppress_hbox_text_depth: true,
    is_middle_delim: bx.is_middle_delim,
    is_script_radical: bx.is_script_radical
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
    else if (contains("0123456789.,", slice(text, i, i + 1))) is_digit_text(text, i + 1)
    else false
}

// ============================================================
// Inter-atom spacing
// ============================================================

fn bin_as_ord_after(prev_type) {
    prev_type == null or prev_type == "mbin" or prev_type == "mopen" or
    prev_type == "mrel" or prev_type == "mpunct" or prev_type == "mop"
}

fn box_with_type(bx, atom_type) {
    ml_box_with_type(bx, atom_type)
}

fn ml_box_with_type(bx, atom_type) => {
    element: bx.element,
    height: bx.height,
    depth: bx.depth,
    width: bx.width,
    type: atom_type,
    italic: bx.italic,
    skew: bx.skew,
    max_font_size: bx.max_font_size,
    model: "ml",
    is_fraction: bx.is_fraction,
    is_script_radical: bx.is_script_radical
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
        let space = if (current.no_left_bin_space == true and prev_type == "mbin")
             0.0 else if (prev_type == "mpunct" and current.type == "skip")
             0.0 else sp_table.get_spacing(prev_type, current.type, context.style),
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
