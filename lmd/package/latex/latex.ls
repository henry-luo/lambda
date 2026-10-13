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
import math: lambda.doc.math.math
import math_bridge: .math_bridge
import html_ser: .to_html
import registry: .packages.registry
import geometry: .packages.geometry
import microtype: .packages.microtype
import biblatex: .packages.biblatex
import natbib: .packages.natbib
import bib_style: .packages.bib_style
import bib_data: .packages.bib_data
import csl_resources: .citeproc.resources
import csl: .citeproc.adapter
import csl_aliases: .citeproc.aliases
import hyperref: .packages.hyperref
import amsmath: .packages.amsmath
import paths: lambda.edit.session
import util: .util
import language_profile: .packages.language
import tikz: .packages.tikz
import page_style: .packages.page_style
import listings: .packages.listings
import caption: .packages.caption
import font_decl: .elements.font_decl
import paragraph: .packages.paragraph
import footmisc: .packages.footmisc
import fontspec: .packages.fontspec

// ============================================================
// Public API — HTML string output
// ============================================================

// D7.2.7: every LaTeX parse expands with the TeX engine before the direct parser
// builds the AST; the package supplies its adapter registry so local files never
// shadow an adapter and adapter commands keep raw arguments
pub fn parse_source(latex_source, base) =>
    parse(latex_source, {type: "latex", base: base,
        packages: registry.adapter_names(), raw: registry.RAW_ARGUMENT_COMMANDS}) ^ { null }

pub fn parse_file(file_path) {
    let source = input(file_path, "text") ^ { null }
    if (source == null) null else parse_source(source, file_path)
}

// parse and render a LaTeX file to HTML string
pub fn render_file_to_html(file_path) {
    let ast = parse_file(file_path)
    render_to_html(ast, {base_uri: paths.dirname(file_path)})
}

// parse and render a LaTeX string to HTML string
pub fn render_string_to_html(latex_source) {
    let ast = parse_source(latex_source, null)
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
// options: {standalone, base_uri, source_path, target, paged, citeproc, language}
pub fn render(ast, options) {
    render_result(ast, options).elements
}

// Structured result keeps diagnostics and package metadata beside the element tree.
pub fn render_result(ast, options) {
    let base_uri = resource_base(options)
    // extract macro definitions from AST (does not modify tree)
    let macro_defs = macros.get_defs(ast)
    let loaded = registry.collect(ast)
    let polyglossia = if (registry.active(loaded.packages, "polyglossia"))
        language_profile.polyglossia_profile(ast) else null
    let math_operators = if (registry.active(loaded.packages, "amsmath"))
        amsmath.operators(ast) else {definitions: [], diagnostics: []}
    // package activation precedes counter and label analysis.
    let language = document_language(options, loaded.packages, polyglossia)
    let biblatex_options = registry.options_for(loaded.packages, "biblatex")
    let explicit_citeproc = if (options == null) null else options.citeproc
    // a guide-level biblatex style selects its CSL style unless the caller chose one
    // (Lambda_Design_BibTeX_CSL §7.4)
    let style_alias = if (explicit_citeproc != null) null
        else csl_aliases.select(biblatex_options, ast,
            registry.offset_for(loaded.packages, "biblatex"))
    let citeproc_request = if (explicit_citeproc != null) explicit_citeproc
        else if (style_alias != null) style_alias.citeproc else null
    let csl_capture = if (citeproc_request != null)
        csl_resources.load(ast, base_uri, citeproc_request, language) else null
    let base_info = analyzer.analyze_with_language(ast, loaded.packages, language,
        {citeproc: csl_capture != null, note_style: csl_capture.compiled.style.class == "note"})
    let bibliography = if (csl_capture != null)
        csl.prepare(ast, csl_capture, base_info.footnotes,
            // under an alias the biblatex style is the request, not a conflict;
            // the options CSL cannot follow are reported by the alias itself
            {*:citeproc_request, legacy_options: if (style_alias != null) {} else biblatex_options})
        else if (registry.active(loaded.packages, "biblatex"))
        biblatex.prepare(ast, base_uri,
            registry.options_for(loaded.packages, "biblatex"), language)
        else natbib.prepare(ast, base_uri,
            registry.options_for(loaded.packages, "natbib"), language)
    let language_issues = if (csl_capture == null and registry.active(loaded.packages, "biblatex") and
        not (bib_style.supported_language(language) ^ { false }))
        [util.diagnostic("unsupported-bib-language", "biblatex", language,
            "Unsupported bibliography language " ++ language,
            registry.offset_for(loaded.packages, "biblatex"))] else []
    let cleveref_issues = if (registry.active(loaded.packages, "cleveref") and
        language != "english" and language != "en")
        [util.diagnostic("unsupported-cref-language", "cleveref", language,
            "Cleveref names currently support English", registry.offset_for(
                loaded.packages, "cleveref"))] else []
    let link_settings = hyperref.settings(ast, registry.options_for(loaded.packages, "hyperref"))
    let running_style = page_style.prepare(ast,
        registry.active(loaded.packages, "fancyhdr"))
    // add macro definitions to info for render-time expansion
    let info = {macros: macro_defs, *:base_info, packages: loaded.packages,
                tikz_declarations: tikz.preamble_declarations(ast),
                tikz_math_declarations: tikz.math_declarations(ast),
                running_style: running_style,
                language: language, text_language: language,
                polyglossia: polyglossia,
                // fontspec's and polyglossia's family declarations (fontspec.ls)
                font_families: if (registry.active(loaded.packages, "fontspec")) fontspec.declarations(ast) else [],
                listing_options: listings.preamble_options(ast),
                // fancyvrb's \fvset and minted's \setminted defaults
                code_defaults: {fancyvrb: if (registry.active(loaded.packages, "fancyvrb"))
                    listings.preamble_settings(ast, "fvset") else {},
                    minted: if (registry.active(loaded.packages, "minted"))
                    listings.preamble_settings(ast, "setminted") else {}},
                listing_nums: base_info.listing_nums,
                caption_options: caption.initial(registry.options_for(loaded.packages, "caption"),
                    registry.options_for(loaded.packages, "subcaption")),
                caption_type: null, caption_number: null,
                math_bold: false,
                greek_polytonic: language_profile.greek_polytonic(ast),
                font_size: null, line_height: null, font_groups: [],
                base_uri: base_uri,
                bibitems: base_info.bibitems,
                biblatex_context: bibliography.context, hyperref_settings: link_settings,
                math_operators: math_operators.definitions,
                cellspace_css: cellspace_stylesheet(ast, loaded.packages)}
    // pass 2: render AST using pre-computed info
    let html = dispatcher.render_node(ast, info)
    let completed = postprocess(html, info)
    let elements = if (is_standalone(options)) {
        wrap_standalone(completed, info, options)
    } else {
        completed
    }
    // an aliased style is supported through CSL, so its unsupported-style report is dropped
    let package_issues = if (style_alias == null) loaded.diagnostics
        else [for (issue in loaded.diagnostics where not (issue.code == "unsupported-bib-style" and
            issue.item == style_alias.biblatex_style)) issue] ++ style_alias.diagnostics
    let diagnostics = engine_diagnostics(ast) ++ package_issues ++ math_operators.diagnostics ++
        bibliography.diagnostics ++ language_issues ++ cleveref_issues ++
        registry.font_issues(loaded.packages) ++
        (if (registry.active(loaded.packages, "fontspec"))
            fontspec.issues(fontspec.declarations(ast), base_info.custom_colors) else []) ++
        math_bridge.issues(ast, math_operators.definitions, loaded.packages) ++
        hyperref.issues(link_settings, registry.offset_for(loaded.packages, "hyperref")) ++
        (if (registry.active(loaded.packages, "tikzpeople"))
            [util.diagnostic("tikzpeople-shape-approximation", "tikzpeople", null,
                "Named people use reusable vector silhouettes; CTAN artwork detail differs",
                registry.offset_for(loaded.packages, "tikzpeople"))] else []) ++
        (if (bold_math_request(ast) == null) [] else
            [util.diagnostic("math-bold-approximation", "latex", string(name(bold_math_request(ast))),
                "Bold math uses CSS font weight until bold math fonts are available",
                bold_math_request(ast).source_offset)]) ++
        font_decl.substitution_issues(ast) ++
        (if (polyglossia == null) [] else language_profile.polyglossia_issues(
            polyglossia, registry.offset_for(loaded.packages, "polyglossia"))) ++
        language_profile.target_issues(polyglossia, if (options == null) null else options.target,
            registry.offset_for(loaded.packages, "polyglossia")) ++
        running_style.diagnostics ++
        dispatcher.running_diagnostics(info, if (options == null) null else options.target) ++
        (if (options != null and options.target == "pdf" and options.paged != true and
             (running_style.style == "fancy" or
              running_style.style == "fancyplain"))
            [util.diagnostic("unsupported-running-header", "fancyhdr", null,
                "Running headers require the shared paged PDF export",
                running_style.offset)] else []) ++
        microtype.unsupported(registry.options_for(loaded.packages, "microtype"),
            registry.offset_for(loaded.packages, "microtype")) ++
        registry.reference_diagnostics(ast, info.labels, info.bibitems, info.packages,
            bibliography.context != null) ++
        registry.target_diagnostics(ast, if (options == null) null else options.target,
            options != null and options.paged == true) ++
        info.diagnostics ++
        output_diagnostics(elements)
    {body: html, elements: elements,
     stylesheet: css.get_stylesheet() ++ math.stylesheet(options) ++ package_stylesheet(info),
     metadata: if (csl_capture == null) hyperref.metadata(link_settings, info.title, info.author)
         else {*:hyperref.metadata(link_settings, info.title, info.author),
             bibliography: if (style_alias == null) bibliography.metadata
                 else {*:bibliography.metadata, biblatex_style: style_alias.biblatex_style}},
     packages: loaded.packages, diagnostics: diagnostics,
     assets: registry.assets(ast, info.base_uri) ++ (if (csl_capture == null)
         bib_data.resource_assets(ast, info.base_uri) else bibliography.assets)}
}

fn document_language(options, packages, polyglossia) {
    if (options != null and options.language != null) options.language
    else if (polyglossia != null) polyglossia.default
    else if (registry.active(packages, "vietnam")) "vietnamese"
    else language_profile.babel_default(registry.options_for(packages, "babel"))
}

// Lambda_Design_TeX_Engine §8: TeX engine errors are located diagnostics of the `tex` package
fn engine_diagnostics(ast) {
    let issues = if (ast == null or ast.tex_diagnostics == null) [] else ast.tex_diagnostics;
    [for (issue in issues) util.diagnostic(issue.code, "tex", issue.file, issue.message, issue.offset)]
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

// fontspec's features for the sans and mono families, on the elements set in them
fn family_features(info) {
    let sans = dispatcher.role_features(info, "sans")
    let mono = dispatcher.role_features(info, "mono");
    (if (sans == "") "" else ".latex-document .latex-sf,.latex-document .latex-sffamily{" ++ sans ++ "}\n") ++
    (if (mono == "") "" else ".latex-document code.latex-code,.latex-document pre.latex-verbatim," ++
        ".latex-document .latex-ttfamily,.latex-document .latex-listing pre{" ++ mono ++ "}\n")
}

fn package_stylesheet(info) {
    family_features(info) ++
    (if (info.biblatex_context.processor == "csl") csl.stylesheet() else "") ++
    geometry.stylesheet(registry.options_for(info.packages, "geometry")) ++
    (if (registry.active(info.packages, "fullpage") and
        not registry.active(info.packages, "geometry"))
        geometry.stylesheet({margin: "1in"}) else "") ++
    microtype.stylesheet(registry.options_for(info.packages, "microtype")) ++
    hyperref.stylesheet(info.hyperref_settings) ++ dispatcher.running_stylesheet(info) ++
    info.cellspace_css ++
    (if (registry.active(info.packages, "parskip"))
        paragraph.parskip_stylesheet(registry.options_for(info.packages, "parskip")) else "") ++
    // indentfirst indents the first paragraph after a heading, which LaTeX otherwise does not
    (if (registry.active(info.packages, "indentfirst"))
        ".latex-document h1 + p,.latex-document h2 + p,.latex-document h3 + p," ++
        ".latex-document h4 + p{text-indent:1.5em;}\n" else "")
}

// The parser preserves both braced and control-sequence forms of \setlength.
fn declared_length(node, key) {
    if (not (node is element)) null
    else if (string(name(node)) == "setlength" and node.length_name == key)
        util.css_dimension(node.length_value)
    else declared_length_child(node, key, 0)
}

fn declared_length_child(node, key, index) {
    if (index >= len(node)) null
    else {
        let value = declared_length(node[index], key)
        if (value != null) value else declared_length_child(node, key, index + 1)
    }
}

fn cellspace_stylesheet(ast, packages) {
    if (not registry.active(packages, "cellspace")) ""
    else {
        let top = declared_length(ast, "cellspacetoplimit")
        let bottom = declared_length(ast, "cellspacebottomlimit")
        if (top == null and bottom == null) ""
        else ".latex-tabular td{" ++
            (if (top == null) "" else "padding-top:" ++ top ++ ";") ++
            (if (bottom == null) "" else "padding-bottom:" ++ bottom ++ ";") ++ "}\n"
    }
}

// Native document-loader entry point keeps standalone output as the view default.
pub fn render_document(ast, options) {
    let effective = if (options == null) {standalone: true}
        else if (options.standalone == null) {*:options, standalone: true}
        else options
    render(ast, effective)
}

// Path-source entry for the native document transform (CLI convert, Radiant):
// parsing here, not natively, gives the engine the adapter registry (D7.2.7).
pub fn render_document_file(file_path, options) any^ {
    let source = input(file_path, "text") ^ { raise error("could not read " ++ file_path, ^) }
    let ast = parse_source(source, file_path)
    if (ast == null) raise error("could not parse LaTeX document " ++ file_path)
    else render_document(ast, if (options == null) {source_path: file_path}
        else {*:options, source_path: file_path})
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
    let ast = parse_file(file_path)
    render(ast, {base_uri: paths.dirname(file_path)})
}

// parse and render a LaTeX string
pub fn render_string(latex_source) {
    let ast = parse_source(latex_source, null)
    render(ast, null)
}

// ============================================================
// Post-processing — append footnotes
// ============================================================

fn postprocess(html, info) {
    let fn_section = render_footnotes_section(info)
    if (fn_section != null) wrap_with_footnotes(html, fn_section) else html
}

// the first \boldmath or \mathversion{bold}
fn bold_math_request(ast) {
    let boldmath = util.find_descendant(ast, "boldmath")
    let version = util.find_descendant(ast, "mathversion")
    if (boldmath != null) boldmath
    else if (version != null and trim(util.text_of(version)) == "bold") version
    else null
}

fn wrap_with_footnotes(body, footnotes_el) {
    <div class: "latex-output",
        body
        footnotes_el
    >
}

fn render_footnotes_section(info) {
    let footnotes = info.footnotes
    let options = registry.options_for(info.packages, "footmisc")
    // symbol marks replace the list's own numbers
    let symbolic = footmisc.mark(options, 1) != "1"
    let ruled = footmisc.ruled(options)
    let rule = if (ruled) <hr> else null
    if (len(footnotes) == 0) null
    else (
        <section class: "latex-footnotes", style: if (ruled) null else "border-top:none",
            rule;
            <ol style: if (symbolic) "list-style:none" else null,
                for (i, fn_entry in footnotes)
                    render_footnote_item(fn_entry, i, info,
                        if (symbolic) footmisc.mark(options, fn_entry.number) else null)
            >
        >
    )
}

fn render_footnote_item(fn_entry, i, info, mark) {
    let fn_num = fn_entry.number
    let content = if (info.biblatex_context != null and
        (string(name(fn_entry.node)) == "footcite" or
        (info.biblatex_context.processor == "csl" and
         (string(name(fn_entry.node)) == "autocite" or string(name(fn_entry.node)) == "autocites"))))
        [biblatex.render_footcite_content(fn_entry.node, info.biblatex_context)]
        else dispatcher.render_note_body(fn_entry.node, info);
    // explicit or \footnotetext numbers can leave the list's own sequence
    <li id: "fn-" ++ (fn_num), value: if (mark == null and fn_num != i + 1) fn_num else null,
        if (mark != null) (<sup class: "latex-footnote-mark", mark>, " ") else null
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
    let math_stylesheet = math.stylesheet(options)
    let meta = hyperref.metadata(info.hyperref_settings, info.title, info.author)
    let title_text = get_title_or_default(meta.title);

    <html lang: language_profile.code(info.language),
        <head
            <meta charset: "utf-8">
            <meta name: "viewport", content: "width=device-width, initial-scale=1">
            <title title_text>
            if (meta.author != null) { <meta name: "author", content: meta.author> }
            if (meta.subject != null) { <meta name: "description", content: meta.subject> }
            if (meta.keywords != null) { <meta name: "keywords", content: meta.keywords> }
            // Document text uses CMU; math SVG carries the selected glyph outlines.
            font_stylesheet();
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

// Document text needs CMU CSS even when math is emitted as outlined SVG.
pub fn font_stylesheet() {
    let home = paths.resolve_path(sys.proc.self.cwd#, sys.lambda.home#);
    <link rel: "stylesheet", href: home ++ "/package/latex/fonts/cmu-combined.css">
}
