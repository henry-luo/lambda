// latex/math_bridge.ls — Bridge to the math package for inline/display math rendering
// Delegates math AST rendering to lambda/package/math/math.ls

import math: .lambda.package.math.math
import ctx_mod: .lambda.package.latex.context

// ============================================================
// Inline math: $...$ or \(...\)
// ============================================================

pub fn render_inline(node, ctx) {
    let math_ast = node.ast
    let result = if (math_ast != null) {
        math.render_inline(math_ast)
    } else {
        // fallback: render source text as-is
        let src = if (node.source != null) node.source else ""
        <span class: "math-inline"; src>
    }
    {result: result, ctx: ctx}
}

// ============================================================
// Display math: $$...$$ or \[...\]
// ============================================================

pub fn render_display(node, ctx) {
    let math_ast = node.ast
    let math_html = if (math_ast != null) {
        math.render_display(math_ast)
    } else {
        let src = if (node.source != null) node.source else ""
        <span class: "math-display"; src>
    }
    let result = <div class: "math-display-container"; math_html>
    {result: result, ctx: ctx}
}

// ============================================================
// Numbered equation environment
// ============================================================

pub fn render_equation(node, ctx) {
    let math_ast = node.ast
    let math_html = if (math_ast != null) {
        math.render_display(math_ast)
    } else {
        let src = if (node.source != null) node.source else ""
        <span class: "math-display"; src>
    }

    // step equation counter
    let new_ctx = ctx_mod.step_counter(ctx, "equation")
    let eq_num = new_ctx.counters.equation

    let result = <div class: "latex-equation";
        math_html
        if ctx.numbering { <span class: "latex-eq-number"; "(" ++ string(eq_num) ++ ")"> }
    >
    {result: result, ctx: new_ctx}
}

// ============================================================
// Align/gather/multline environments
// ============================================================

pub fn render_math_environment(node, ctx, env_name) {
    // for now, treat like display math with source
    let math_ast = node.ast
    let math_html = if (math_ast != null) {
        math.render_display(math_ast)
    } else {
        let src = if (node.source != null) node.source else ""
        <span class: "math-display"; src>
    }

    // numbered if not starred
    let is_starred = ends_with(env_name, "*")
    if (is_starred) {
        let result = <div class: "math-display-container"; math_html>
        {result: result, ctx: ctx}
    } else {
        let new_ctx = ctx_mod.step_counter(ctx, "equation")
        let eq_num = new_ctx.counters.equation
        let result = <div class: "latex-equation";
            math_html
            if ctx.numbering { <span class: "latex-eq-number"; "(" ++ string(eq_num) ++ ")"> }
        >
        {result: result, ctx: new_ctx}
    }
}
