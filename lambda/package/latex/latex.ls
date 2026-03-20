// latex/latex.ls — Main entry point for the LaTeX-to-HTML package
// Usage:
//   import latex: lambda.package.latex.latex
//   let html_string = latex.render_to_html(ast)
//   let html_string = latex.render_to_html(ast, {standalone: true})
//   let html_string = latex.render_file_to_html("paper.tex")
//   let elements = latex.render(ast)

import normalize: .normalize
import analyzer: .analyze
import macros: .macros
import dispatcher: .render
import css: .css
import math_css: lambda.package.math.css
import html_ser: .to_html

// ============================================================
// Public API — HTML string output
// ============================================================

// parse and render a LaTeX file to HTML string
pub fn render_file_to_html(file_path) {
    let ast^err = input(file_path, {type: "latex"})
    render_to_html(ast, null)
}

// parse and render a LaTeX string to HTML string
pub fn render_string_to_html(latex_source) {
    let ast^err = input(latex_source, {type: "latex", source: true})
    render_to_html(ast, null)
}

// render a LaTeX AST to HTML string
pub fn render_to_html(ast, options) {
    let elements = render(ast, options)
    html_ser.to_html(elements)
}

// ============================================================
// Public API — element output
// ============================================================

// render a LaTeX AST (already parsed) to HTML elements
// options: {docclass, standalone, numbering, toc}
pub fn render(ast, options) {
    // extract macro definitions from AST (does not modify tree)
    let macro_defs = macros.get_defs(ast)
    // pass 1: analyze AST to collect counters, headings, labels
    let base_info = analyzer.analyze(ast)
    // add macro definitions to info for render-time expansion
    let info = {macros: macro_defs, *:base_info}
    // pass 2: render AST using pre-computed info
    let html = dispatcher.render_node(ast, info)

    if (is_standalone(options)) {
        wrap_standalone(html, info)
    } else {
        postprocess(html, info)
    }
}

fn is_standalone(options) {
    if (options == null) { false }
    else if (options.standalone == true) { true }
    else { false }
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

fn postprocess(html, info) {
    let fn_section = render_footnotes_section(info)
    if (fn_section != null) wrap_with_footnotes(html, fn_section) else html
}

fn wrap_with_footnotes(body, footnotes_el) {
    <div class: "latex-output";
        body
        footnotes_el
    >
}

fn render_footnotes_section(info) {
    let footnotes = info.footnotes
    if (len(footnotes) == 0) null
    else (
        <section class: "latex-footnotes";
            <hr>
            <ol;
                for (fn_entry in footnotes)
                    render_footnote_item(fn_entry, info)
            >
        >
    )
}

fn render_footnote_item(fn_entry, info) {
    let fn_num = fn_entry.number
    let content = dispatcher.render_children_of(fn_entry.node, info)
    <li id: "fn-" ++ string(fn_num);
        for c in content { c }
        " "
        <a class: "footnote-backref", href: "#fnref-" ++ string(fn_num); "\u21A9">
    >
}

// ============================================================
// Standalone HTML wrapper
// ============================================================

fn wrap_standalone(html, info) {
    let stylesheet = css.get_stylesheet()
    let math_stylesheet = math_css.get_stylesheet()
    let title_text = get_title_or_default(info.title)

    <html lang: "en";
        <head;
            <meta charset: "utf-8">
            <meta name: "viewport", content: "width=device-width, initial-scale=1">
            <title; title_text>
            <style; stylesheet>
            <style; math_stylesheet>
        >
        <body;
            html
        >
    >
}

fn get_title_or_default(title) {
    if (title != null) { title }
    else { "LaTeX Document" }
}
