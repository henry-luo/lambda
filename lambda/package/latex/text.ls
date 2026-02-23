// latex/text.ls — Text formatting: bold, italic, code, verbatim, fonts, diacritics
// Handles inline text formatting commands from the LaTeX AST.

import util: .lambda.package.latex.util
import sym: .lambda.package.latex.symbols

// ============================================================
// Paragraph rendering
// ============================================================

// render a paragraph element — splits content at parbreaks into <p> tags
pub fn render_paragraph(node, ctx, render_fn) {
    let n = len(node)
    if (n == 0) { {result: null, ctx: ctx} }
    else {
        let split = split_at_parbreaks(node, render_fn, ctx)
        split
    }
}

// split paragraph children at parbreak symbols into separate <p> elements
// returns {result: [<p>...], ctx: updated_ctx}
fn split_at_parbreaks(node, render_fn, ctx) {
    let n = len(node)
    let collected = collect_paragraphs(node, 0, n, [], [], render_fn, ctx)
    collected
}

// recursive paragraph collector
// acc_current: children of current <p> being built
// acc_paras: completed <p> elements
fn collect_paragraphs(node, i, n, acc_current, acc_paras, render_fn, ctx) {
    if (i >= n) {
        // flush remaining content
        let final_paras = if (len(acc_current) > 0) acc_paras ++ [make_p(acc_current)]
            else acc_paras
        {result: final_paras, ctx: ctx}
    } else {
        let child = node[i]
        if (util.is_parbreak(child)) {
            // flush current paragraph, start new one
            let updated_paras = if (len(acc_current) > 0) acc_paras ++ [make_p(acc_current)]
                else acc_paras
            collect_paragraphs(node, i + 1, n, [], updated_paras, render_fn, ctx)
        } else {
            // render child and add to current paragraph
            let rendered = render_fn(child, ctx)
            let new_ctx = rendered.ctx
            let content = rendered.result
            if (content != null) {
                collect_paragraphs(node, i + 1, n, acc_current ++ [content], acc_paras, render_fn, new_ctx)
            } else {
                collect_paragraphs(node, i + 1, n, acc_current, acc_paras, render_fn, new_ctx)
            }
        }
    }
}

fn make_p(children) {
    let trimmed = util.trim_children(children)
    if (len(trimmed) == 0) null
    else <p; for c in trimmed { c }>
}

// ============================================================
// Text styling commands
// ============================================================

// render a text styling command: \textbf → <strong>, \textit → <em>, etc.
pub fn render_styled(node, ctx, html_tag, render_fn) {
    let children_result = render_child_list(node, ctx, render_fn)
    let el = match html_tag {
        case "b":  <strong; for c in children_result.items { c }>
        case "i":  <em; for c in children_result.items { c }>
        case "em": <em; for c in children_result.items { c }>
        case "u":  <u; for c in children_result.items { c }>
        default:   <span; for c in children_result.items { c }>
    }
    {result: el, ctx: children_result.ctx}
}

// render \texttt → <code>
pub fn render_code_inline(node, ctx, render_fn) {
    let children_result = render_child_list(node, ctx, render_fn)
    {result: <code class: "latex-code"; for c in children_result.items { c }>, ctx: children_result.ctx}
}

// render \verb|...| → <code>
pub fn render_verbatim_inline(node, ctx) {
    let text = util.text_of(node)
    {result: <code class: "latex-code"; text>, ctx: ctx}
}

// render font family commands: \textsf → sans, \textsc → small-caps, \textsl → oblique
pub fn render_font_family(node, ctx, css_class, render_fn) {
    let children_result = render_child_list(node, ctx, render_fn)
    {result: <span class: css_class; for c in children_result.items { c }>, ctx: children_result.ctx}
}

// render font size commands: \small, \large, etc.
pub fn render_font_size(node, ctx, cmd_name, render_fn) {
    let size = sym.get_font_size(cmd_name)
    if (size != null) {
        let children_result = render_child_list(node, ctx, render_fn)
        {result: <span style: "font-size:" ++ size; for c in children_result.items { c }>, ctx: children_result.ctx}
    } else {
        let children_result = render_child_list(node, ctx, render_fn)
        {result: <span; for c in children_result.items { c }>, ctx: children_result.ctx}
    }
}

// ============================================================
// Diacritics
// ============================================================

// render a diacritic command: \'{e} → é
pub fn render_diacritic(node, ctx, cmd_name) {
    // the first child should be the base character (inside curly_group or direct)
    let base = get_diacritic_base(node)
    let result = sym.resolve_diacritic(cmd_name, base)
    {result: result, ctx: ctx}
}

fn get_diacritic_base(node) {
    let n = len(node)
    if (n == 0) { "" }
    else {
        let first = node[0]
        if (first is element and name(first) == "curly_group")
            util.text_of(first)
        else
            util.text_of(first)
    }
}

// ============================================================
// Helper: render all children of a node
// ============================================================

fn render_child_list(node, ctx, render_fn) {
    let n = len(node)
    if (n == 0) { {items: [], ctx: ctx} }
    else { render_children_rec(node, 0, n, [], ctx, render_fn) }
}

fn render_children_rec(node, i, n, acc, ctx, render_fn) {
    if (i >= n) { {items: acc, ctx: ctx} }
    else {
        let child = node[i]
        let rendered = render_fn(child, ctx)
        let new_acc = if (rendered.result != null) acc ++ [rendered.result] else acc
        render_children_rec(node, i + 1, n, new_acc, rendered.ctx, render_fn)
    }
}
