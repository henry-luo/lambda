// latex/latex.ls — Main entry point for the LaTeX-to-HTML package
// Usage:
//   import latex: .lambda.package.latex.latex
//   let html = latex.render(ast)
//   let html = latex.render(ast, {standalone: true, numbering: true})
//   let html = latex.render_file("paper.tex")
//   let html = latex.render_string("\\section{Hello}")

import ctx_mod: .lambda.package.latex.context
import normalize: .lambda.package.latex.normalize
import dispatcher: .lambda.package.latex.render
import css: .lambda.package.latex.css

// ============================================================
// Public API
// ============================================================

// render a LaTeX AST (already parsed) to HTML elements
// options: {docclass, standalone, numbering, toc}
pub fn render(ast, options) {
    let ctx = ctx_mod.make_context(options)
    let normalized = normalize.normalize(ast)
    let result = dispatcher.render_node(normalized, ctx)

    if (options != null and options.standalone == true)
        wrap_standalone(result.result, result.ctx)
    else
        postprocess(result.result, result.ctx)
}

// render with default options
pub fn render_default(ast) {
    render(ast, null)
}

// parse and render a LaTeX file
pub fn render_file(file_path) {
    let ast^err = input(file_path, {type: "latex"})
    render(ast, null)
}

// parse and render a LaTeX string
pub fn render_string(latex_source) {
    let ast^err = input(latex_source, {type: "latex", source: true})
    render(ast, null)
}

// ============================================================
// Post-processing — append footnotes
// ============================================================

fn postprocess(html, ctx) {
    let footnotes_el = render_footnotes_section(ctx.footnotes)
    if (footnotes_el != null) {
        <div class: "latex-output";
            html
            footnotes_el
        >
    } else {
        html
    }
}

fn render_footnotes_section(footnotes) {
    if (len(footnotes) == 0) {
        null
    } else {
        <section class: "latex-footnotes";
            <hr>
            <ol;
                for (fn_entry in footnotes)
                    <li id: "fn-" ++ string(fn_entry.number);
                        fn_entry.content
                        <a class: "footnote-backref", href: "#fnref-" ++ string(fn_entry.number); "\u21A9">
                    >
            >
        >
    }
}

// ============================================================
// Standalone HTML wrapper
// ============================================================

fn wrap_standalone(html, ctx) {
    let footnotes_el = render_footnotes_section(ctx.footnotes)
    let stylesheet = css.get_stylesheet()
    let title_text = if (ctx.title != null) ctx.title else "LaTeX Document"
    let body_content = if (footnotes_el != null) [html, footnotes_el] else [html]

    <html lang: "en";
        <head;
            <meta charset: "utf-8">
            <meta name: "viewport", content: "width=device-width, initial-scale=1">
            <title; title_text>
            <style; stylesheet>
        >
        <body;
            for (c in body_content) c
        >
    >
}
