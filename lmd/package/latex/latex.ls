// latex/latex.ls — Main entry point for the LaTeX-to-HTML package
// Usage:
//   import latex: lambda.latex.latex
//   let html_string = latex.render_to_html(ast)
//   let html_string = latex.render_to_html(ast, {standalone: true})
//   let html_string = latex.render_file_to_html("paper.tex")
//   let elements = latex.render(ast)

import analyzer: .analyze
import macros: .macros
import dispatcher: .render
import css: .css
import math_css: lambda.doc.math.css
import html_ser: .to_html
import registry: .packages.registry
import geometry: .packages.geometry
import microtype: .packages.microtype
import biblatex: .packages.biblatex
import hyperref: .packages.hyperref
import amsmath: .packages.amsmath
import paths: lambda.edit.session
import util: .util

// ============================================================
// Public API — HTML string output
// ============================================================

// parse and render a LaTeX file to HTML string
pub fn render_file_to_html(file_path) {
    let ast = input(file_path, {type: "latex"}) ^ { null }
    render_to_html(ast, {base_uri: paths.dirname(file_path)})
}

// parse and render a LaTeX string to HTML string
pub fn render_string_to_html(latex_source) {
    let ast = parse(latex_source, {type: "latex"}) ^ { null }
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
    render_result(ast, options).elements
}

// Structured result keeps diagnostics and package metadata beside the element tree.
pub fn render_result(ast, options) {
    let base_uri = resource_base(options)
    // extract macro definitions from AST (does not modify tree)
    let macro_defs = macros.get_defs(ast)
    let loaded = registry.collect(ast)
    let math_operators = if (registry.active(loaded.packages, "amsmath"))
        amsmath.operators(ast) else {definitions: [], diagnostics: []}
    // package activation precedes counter and label analysis.
    let base_info = analyzer.analyze_with_packages(ast, loaded.packages)
    let bibliography = biblatex.load(ast, base_uri,
        registry.options_for(loaded.packages, "biblatex"))
    let numbered_bib = biblatex.numbered_entries(bibliography.entries, len(base_info.bibitems))
    let link_settings = hyperref.settings(ast, registry.options_for(loaded.packages, "hyperref"))
    // add macro definitions to info for render-time expansion
    let info = {macros: macro_defs, *:base_info, packages: loaded.packages,
                base_uri: base_uri,
                bibitems: base_info.bibitems ++ numbered_bib,
                biblatex_entries: numbered_bib, hyperref_settings: link_settings,
                math_operators: math_operators.definitions}
    // pass 2: render AST using pre-computed info
    let html = dispatcher.render_node(ast, info)
    let elements = if (is_standalone(options)) {
        wrap_standalone(html, info, options)
    } else {
        postprocess(html, info)
    }
    let diagnostics = loaded.diagnostics ++ math_operators.diagnostics ++ bibliography.diagnostics ++
        microtype.unsupported(registry.options_for(loaded.packages, "microtype"),
            registry.offset_for(loaded.packages, "microtype")) ++
        registry.reference_diagnostics(ast, info.labels, info.bibitems, info.packages) ++
        output_diagnostics(elements)
    {body: html, elements: elements,
     stylesheet: css.get_stylesheet() ++ math_css.get_stylesheet(options) ++ package_stylesheet(info),
     metadata: hyperref.metadata(link_settings, info.title, info.author),
     packages: loaded.packages, diagnostics: diagnostics,
     assets: registry.assets(ast, info.base_uri)}
}

fn resource_base(options) {
    if (options == null) null
    else if (options.base_uri != null) options.base_uri
    else if (options.source_path != null) paths.dirname(options.source_path)
    else null
}

fn output_diagnostics(node) {
    if (node is array or node is list)
        [for (child in node, issue in output_diagnostics(child)) issue]
    else if (node == null or not (node is element)) []
    else {
        let message = node["data-latex-error"]
        let own = if (message != null)
            [util.diagnostic("unsupported-output", node["data-latex-package"], null,
              message, node["data-latex-offset"])] else []
        own ++ [for (child in node, issue in output_diagnostics(child)) issue]
    }
}

fn package_stylesheet(info) {
    geometry.stylesheet(registry.options_for(info.packages, "geometry")) ++
    microtype.stylesheet(registry.options_for(info.packages, "microtype")) ++
    hyperref.stylesheet(info.hyperref_settings)
}

// Native document-loader entry point keeps standalone output as the view default.
pub fn render_document(ast, options) {
    let effective = if (options == null) {standalone: true}
        else if (options.standalone == null) {*:options, standalone: true}
        else options
    render(ast, effective)
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
    let ast = input(file_path, {type: "latex"}) ^ { null }
    render(ast, {base_uri: paths.dirname(file_path)})
}

// parse and render a LaTeX string
pub fn render_string(latex_source) {
    let ast = parse(latex_source, {type: "latex"}) ^ { null }
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
    <div class: "latex-output",
        body
        footnotes_el
    >
}

fn render_footnotes_section(info) {
    let footnotes = info.footnotes
    if (len(footnotes) == 0) null
    else (
        <section class: "latex-footnotes",
            <hr>
            <ol
                for (fn_entry in footnotes)
                    render_footnote_item(fn_entry, info)
            >
        >
    )
}

fn render_footnote_item(fn_entry, info) {
    let fn_num = fn_entry.number
    let content = dispatcher.render_children_of(fn_entry.node, info);
    <li id: "fn-" ++ (fn_num),
        for c in content { c }
        " "
        <a class: "footnote-backref", href: "#fnref-" ++ (fn_num), "\u21A9">
    >
}

// ============================================================
// Standalone HTML wrapper
// ============================================================

fn wrap_standalone(html, info, options) {
    let stylesheet = css.get_stylesheet()
    let math_stylesheet = math_css.get_stylesheet(options)
    let meta = hyperref.metadata(info.hyperref_settings, info.title, info.author)
    let title_text = get_title_or_default(meta.title);

    <html lang: "en",
        <head
            <meta charset: "utf-8">
            <meta name: "viewport", content: "width=device-width, initial-scale=1">
            <title title_text>
            if (meta.author != null) { <meta name: "author", content: meta.author> }
            if (meta.subject != null) { <meta name: "description", content: meta.subject> }
            if (meta.keywords != null) { <meta name: "keywords", content: meta.keywords> }
            <style stylesheet>
            <style math_stylesheet>
            <style package_stylesheet(info)>
        >
        <body
            html
        >
    >
}

fn get_title_or_default(title) {
    if (title != null) { title }
    else { "LaTeX Document" }
}
