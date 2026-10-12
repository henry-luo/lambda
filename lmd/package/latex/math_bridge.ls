// latex/math_bridge.ls - Bridge to the math package
// Delegates math AST rendering to lmd/package/math/math.ls

import math: lambda.doc.math.math
import util: .util
import registry: .packages.registry
import amsmath: .packages.amsmath
import semantic: .packages.semantic

// ============================================================
// Unmapped math commands
// ============================================================

// The math typesetter paints a command it cannot map as its name, which reads as
// success, so each one is reported (Lambda_Pkg_Math §3.1, §10.2). Declared
// operators are rendered by name on purpose, and Lua is reported as unsupported-lua.
pub fn issues(node, operators, packages) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        // amsmath renders its environments from row sources, not the pre-parsed ast
        let asts = if (adapter_math(node, tag, packages)) []
            else if (amsmath.handles(tag, packages)) amsmath.cell_asts(node, operators)
            else if (node.ast != null) [node.ast]
            else null
        if (asts == null) [for (child in node, issue in issues(child, operators, packages)) issue]
        else [for (ast in asts, key in math.unmapped_commands(ast)
                where key != "directlua" and util.lookup(operators, "\\" ++ key) == null)
            util.diagnostic("unsupported-math-command", "math", key,
                "Unsupported math command \\" ++ key, node.source_offset)]
    }
}

// math an adapter typesets itself: bussproofs sequents and semantic inference rules
fn adapter_math(node, tag, packages) =>
    (tag == "prooftree" and registry.active(packages, "bussproofs")) or
    (tag == "display_math" and registry.active(packages, "semantic") and
        semantic.contains_inference(node))

// ============================================================
// Inline math
// ============================================================

pub fn render_inline_el(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    if (math_ast != null) math.render_inline(math_ast)
    else <span class: "math-inline", src>
}

// ============================================================
// Display math
// ============================================================

pub fn render_display_el(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display", src>;
    <div class: "math-display-container", math_html>
}

// ============================================================
// Numbered equation environment
// ============================================================

pub fn render_equation_el(node) {
    <div class: "latex-equation", render_equation_math(node)>
}

pub fn render_equation_math(node) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    if (math_ast != null) math.render_display(math_ast) else <span class: "math-display", src>
}

// ============================================================
// Align/gather/multline environments
// ============================================================

pub fn render_math_env_el(node, env_name) {
    let math_ast = node.ast
    let src = if (node.source != null) node.source else ""
    let math_html = if (math_ast != null) math.render_display(math_ast) else <span class: "math-display", src>;
    <div class: "math-display-container", math_html>
}
