// latex/math_bridge.ls - Bridge to the math package
// Delegates math AST rendering to lambda/package/math/math.ls

import math: lambda.package.math.math

// ============================================================
// Inline math
// ============================================================

pub fn render_inline_el(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    if (math_ast != null) math.render_inline(math_ast)
    else <span class: "math-inline"; src>
}

// ============================================================
// Display math
// ============================================================

pub fn render_display_el(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    <div class: "math-display-container"; math_html>
}

// ============================================================
// Numbered equation environment
// ============================================================

pub fn render_equation_el(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    <div class: "latex-equation"; math_html>
}

// ============================================================
// Align/gather/multline environments
// ============================================================

pub fn render_math_env_el(node, env_name) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display"; src>
    <div class: "math-display-container"; math_html>
}