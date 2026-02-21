// math/render.ls — Core renderer: AST node → box tree dispatch
// This is the heart of the math rendering pipeline.
// It dispatches on AST element tag names and delegates to atom-specific renderers.

import box: .lambda.package.math.box
import ctx: .lambda.package.math.context
import css: .lambda.package.math.css
import met: .lambda.package.math.metrics
import sym: .lambda.package.math.symbols
import sp_table: .lambda.package.math.spacing_table
import fraction: .lambda.package.math.atoms.fraction
import scripts: .lambda.package.math.atoms.scripts
import spacing: .lambda.package.math.atoms.spacing

// ============================================================
// Main render dispatch
// ============================================================

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
        case 'command':         render_command(node, context)
        case 'text_command':    render_text_command(node, context)
        case 'style_command':   render_style_command(node, context)
        case 'space_command':   spacing.render(node, context, render_node)
        case 'hspace_command':  spacing.render(node, context, render_node)
        case 'skip_command':    spacing.render(node, context, render_node)
        case 'color_command':   render_color_command(node, context)
        case 'genfrac':         fraction.render(node, context, render_node)
        case 'infix_frac':      fraction.render(node, context, render_node)
        case 'overunder_command': render_overunder(node, context)
        case 'big_operator':    render_big_op(node, context)
        case 'accent':          render_accent(node, context)
        case 'delimiter_group': render_delimiter_group(node, context)
        case 'sized_delimiter': render_sized_delim(node, context)
        case 'environment':     render_environment(node, context)
        case 'phantom_command': render_phantom(node, context)
        case 'box_command':     render_box_cmd(node, context)
        case 'rule_command':    render_rule(node, context)
        case 'radical':         render_radical(node, context)
        case 'middle_delim':    render_middle_delim(node, context)
        case 'limits_modifier': box.text_box("", null, "ord")
        default:                render_default(node, context)
    }
}

// render an AST node into a box
// node: a Lambda element from the tree-sitter-latex-math parser
// context: rendering context (style, font, color, etc.)
pub fn render_node(node, context) {
    if (node is string) render_text(node, context)
    else if (node == null) box.text_box("", null, "ord")
    else if (not (node is element)) box.text_box(string(node), null, "ord")
    else dispatch_element(node, context)
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
    box.hbox(children)
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
    let cls = css.font_class(context.font)
    box.text_box(display_text, cls, atom_type)
}

fn render_number(node, context) {
    let text = get_text(node)
    box.text_box(text, css.CMR, "mord")
}

fn render_operator(node, context) {
    let text = get_text(node)
    box.text_box(text, css.CMR, "mbin")
}

fn render_relation(node, context) {
    let text = get_text(node)
    box.text_box(text, css.CMR, "mrel")
}

fn render_punct(node, context) {
    let text = get_text(node)
    box.text_box(text, css.CMR, "mpunct")
}

fn render_text(text, context) {
    box.text_box(text, css.font_class(context.font), "mord")
}

// ============================================================
// Commands (generic \cmd)
// ============================================================

fn render_command(node, context) {
    let cmd_text = get_text(node)
    let unicode = sym.lookup_symbol(cmd_text)
    if (unicode != null) {
        let atom_type = sym.classify_symbol(cmd_text)
        box.text_box(unicode, css.font_class(context.font), atom_type)
    } else {
        let name_str = if (len(cmd_text) > 0 and slice(cmd_text, 0, 1) == "\\")
             slice(cmd_text, 1, len(cmd_text)) else cmd_text
        let op_name = sym.get_operator_name(name_str)
        if (op_name != null) {
            box.text_box(op_name, css.CMR, "mop")
        } else {
            box.text_box(cmd_text, css.ERROR, "mord")
        }
    }
}

// ============================================================
// Text command (\text{...})
// ============================================================

fn render_text_command(node, context) {
    let content = if (node.content != null) get_text(node.content) else ""
    box.text_box(content, css.TEXT, "mord")
}

// ============================================================
// Style commands (\mathbf, \mathrm, etc.)
// ============================================================

fn render_style_command(node, context) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""
    let font_name = match cmd {
        case "\\mathbf": "mathbf"
        case "\\mathrm": "cmr"
        case "\\mathit": "mathit"
        case "\\mathbb": "bb"
        case "\\mathcal": "cal"
        case "\\mathfrak": "frak"
        case "\\mathtt": "tt"
        case "\\mathscr": "script"
        case "\\mathsf": "sans"
        case "\\displaystyle": null
        case "\\textstyle": null
        case "\\scriptstyle": null
        case "\\scriptscriptstyle": null
        case "\\operatorname": "cmr"
        default: null
    }

    let style_override = match cmd {
        case "\\displaystyle": "display"
        case "\\textstyle": "text"
        case "\\scriptstyle": "script"
        case "\\scriptscriptstyle": "scriptscript"
        default: null
    }

    let new_ctx = if (font_name != null) ctx.derive(context, {font: font_name})
        else if (style_override != null) ctx.derive(context, {style: style_override})
        else context

    if (node.content != null) render_node(node.content, new_ctx)
    else (let children = render_children(node, new_ctx),
          box.hbox(children))
}

// ============================================================
// Color commands (\textcolor, \color, \colorbox)
// ============================================================

fn render_color_command(node, context) {
    let color = if (node.color != null) string(node.color) else "black"
    let new_ctx = ctx.derive(context, {color: color})
    if (node.content != null) {
        let content_box = render_node(node.content, new_ctx)
        box.with_color(content_box, color)
    } else {
        box.text_box("", null, "ord")
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
    let base_box = if (node.base != null) render_node(node.base, context)
        else box.text_box("", null, "ord")

    let accent_key = if (len(cmd) > 0 and slice(cmd, 0, 1) == "\\")
        slice(cmd, 1, len(cmd)) else cmd
    let accent_char = sym.get_accent(accent_key)
    let accent_text = if (accent_char != null) accent_char else "^"
    let accent_box = box.text_box(accent_text, css.ACCENT_BODY, "ord")

    box.vbox([
        {box: accent_box, shift: 0.0 - base_box.height - 0.05},
        {box: base_box, shift: 0.0}
    ])
}

// ============================================================
// Delimiter groups (\left...\right)
// ============================================================

fn render_delimiter_group(node, context) {
    let left_text = if (node.left != null) string(node.left) else ""
    let right_text = if (node.right != null) string(node.right) else ""

    let children = render_children(node, context)
    let content = box.hbox(children)

    let left_box = if (left_text != "" and left_text != ".")
        box.text_box(left_text, css.SMALL_DELIM, "mopen")
    else box.null_delim()

    let right_box = if (right_text != "" and right_text != ".")
        box.text_box(right_text, css.SMALL_DELIM, "mclose")
    else box.null_delim()

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
    let delim_box = box.text_box(delim_text, css.SMALL_DELIM, "mord")
    box.with_scale(delim_box, scale)
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
    let sqrt_line = box.box_cls(css.SQRT_LINE, 0.04, 0.0,
                                 body_box.width, "ord")

    let sqrt_body = box.hbox([sqrt_sign, body_box])

    if (index_box != null)
        box.hbox([index_box, box.with_class(sqrt_body, css.SQRT)])
    else
        box.with_class(sqrt_body, css.SQRT)
}

// ============================================================
// Environments (matrix, cases, etc.)
// ============================================================

fn render_environment(node, context) {
    let children = render_children(node, context)
    box.hbox(children)
}

// ============================================================
// Phantom (\phantom, \vphantom, \hphantom)
// ============================================================

fn render_phantom(node, context) {
    let content_box = if (node.content != null)
        render_node(node.content, ctx.derive(context, {phantom: true}))
    else box.text_box("", null, "ord")
    {
        element: <span style: "visibility:hidden;display:inline-block";
            content_box.element
        >,
        height: content_box.height, depth: content_box.depth,
        width: content_box.width, type: "ord",
        italic: 0.0, skew: 0.0
    }
}

// ============================================================
// Box commands (\boxed, \fbox)
// ============================================================

fn render_box_cmd(node, context) {
    let content_box = if (node.content != null) render_node(node.content, context)
        else box.text_box("", null, "ord")
    {
        element: <span style: "border:1px solid;padding:0.1em";
            content_box.element
        >,
        height: content_box.height + 0.15, depth: content_box.depth + 0.15,
        width: content_box.width + 0.3, type: "ord",
        italic: 0.0, skew: 0.0
    }
}

// ============================================================
// Rule command (\rule)
// ============================================================

fn render_rule(node, context) {
    let w = if (node.width != null) float(string(node.width)) else 1.0
    let h = if (node.height != null) float(string(node.height)) else 0.4
    let rule_style = "width:" ++ string(w) ++ "em;height:" ++ string(h) ++ "em;background:currentColor"
    box.box_styled(css.RULE, rule_style,
        h, 0.0, w, "ord"
    )
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
// Helpers
// ============================================================

// render all children of a node
fn render_children(node, context) {
    let n = len(node)
    if (n == 0) {
        []
    } else {
        (for (i in 0 to (n - 1),
              let child = node[i]
              where child != null)
         render_node(child, context))
    }
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
    else ""
}

// ============================================================
// Inter-atom spacing
// ============================================================

// recursive helper for building spaced box list
fn build_spaced(filtered, i, prev_type, acc, context) {
    if (i >= len(filtered)) acc
    else
        (let current = filtered[i],
         let space = sp_table.get_spacing(prev_type, current.type, context.style),
         let space_cls = sp_table.spacing_class(space),
         let with_space = if (space_cls != null)
             concat(acc, [box.box_cls(space_cls, 0.0, 0.0, space, "skip"), current])
         else
             concat(acc, [current]),
         build_spaced(filtered, i + 1, current.type, with_space, context))
}

// apply inter-atom spacing between boxes
fn apply_spacing(boxes, context) {
    let filtered = (for (b in boxes where b != null) b)
    if (len(filtered) <= 1) filtered
    else build_spaced(filtered, 1, filtered[0].type, [filtered[0]], context)
}
