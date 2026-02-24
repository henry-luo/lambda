// latex/math_bridge.ls - Bridge to the math package
// Delegates math AST rendering to lambda/package/math/math.ls

import math: .lambda.package.math.math
import ctx_mod: .lambda.package.latex.context

// ============================================================
// Inline math
// ============================================================

pub fn render_inline(node, ctx) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let result = if (math_ast != null) math.render_inline(math_ast) else <span class: "math-inline"; src>
    {result: result, ctx: ctx}
}

// ============================================================
// Display math
// ============================================================

pub fn render_display(node, ctx) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    let result = <div class: "math-display-container"; math_html>
    {result: result, ctx: ctx}
}

// ============================================================
// Numbered equation environment
// ============================================================

pub fn render_equation(node, ctx) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    let new_ctx = ctx_mod.step_counter(ctx, "equation")
    let eq_num = new_ctx.counters.equation
    let num_el = if (ctx.numbering) { <span class: "latex-eq-number"; "(" ++ string(eq_num) ++ ")"> } else { null }
    let result = <div class: "latex-equation";
        math_html
        if num_el != null { num_el }
    >
    {result: result, ctx: new_ctx}
}

// ============================================================
// Align/gather/multline environments
// ============================================================

pub fn render_math_environment(node, ctx, env_name) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    let is_starred = ends_with(env_name, "*")
    math_env_result(math_html, ctx, is_starred)
}

fn math_env_result(math_html, ctx, is_starred) {
    if is_starred {
        let result = <div class: "math-display-container"; math_html>
        {result: result, ctx: ctx}
    } else {
        let new_ctx = ctx_mod.step_counter(ctx, "equation")
        let eq_num = new_ctx.counters.equation
        let num_el = if (ctx.numbering) { <span class: "latex-eq-number"; "(" ++ string(eq_num) ++ ")"> } else { null }
        let result = <div class: "latex-equation";
            math_html
            if num_el != null { num_el }
        >
        {result: result, ctx: new_ctx}
    }
}

// ============================================================
// Element-returning versions for two-pass rendering
// ============================================================

pub fn render_inline_el(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    if (math_ast != null) math.render_inline(math_ast)
    else <span class: "math-inline"; src>
}

pub fn render_display_el(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    <div class: "math-display-container"; math_html>
}

pub fn render_equation_el(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    <div class: "latex-equation"; math_html>
}

pub fn render_math_env_el(node, env_name) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    <div class: "math-display-container"; math_html>
}