// Adapt the shared LaTeX citation model to the pure CSL batch API.
import c: .common
import cp: .citeproc
import model: ~~.packages.bib_model
import registry: ~~.packages.registry
import util: ~~.util
import locale: .locale

fn contains_offset(node, offset) => node is element and
    (node.source_offset == offset or any([for (child in content(node)) contains_offset(child, offset)]))

fn note_index(request, footnotes) {
    let found = [for (entry in footnotes where contains_offset(entry.node, request.offset)) entry.number]
    if (len(found) == 0) 0 else found[0]
}

fn mode(tag) => if (tag == "citeauthor") "author-only"
    else if (tag == "citetitle") "title-only" else if (tag == "citeyear") "year-only"
    else if (tag == "fullcite") "full" else if (tag == "textcite" or tag == "textcites") "textual"
    else "normal"

fn note(value) {
    let pieces = c.words(value)
    let labels = {'p.': "page", 'pp.': "page", 'chap.': "chapter", 'ch.': "chapter",
        'sec.': "section", '§': "section", 'vol.': "volume"}
    let label = labels[pieces[0]]
    let candidate = if (label == null) value else join(slice(pieces, 1, len(pieces)), " ")
    let first = slice(candidate, 0, 1)
    if (candidate != "" and ((first >= "0" and first <= "9") or label != null))
        {locator: candidate, label: c.get({label: label}, "label", "page"), suffix: ""}
    else {suffix: if (value == "") "" else ", " ++ value}
}

fn request_for(request, context, footnotes) =>
    {id: string(request.offset), mode: mode(request.tag), note_index: note_index(request, footnotes),
        items: [for (group in request.items, i, key in group.keys)
            {id: key, *:note(if (i + 1 == len(group.keys)) group.postnote else ""),
                prefix: if (i == 0 and group.prenote != "") group.prenote ++ " " else "",
                target: model.target_for(context, request, key)}]}

fn section_result(section, references, found, context, footnotes, compiled) {
    let requests = [for (request in found.requests where request.section == section and request.tag != "nocite")
        request_for(request, context, footnotes)]
    let nocite = [for (request in found.requests, group in request.items, key in group.keys
        where request.section == section and request.tag == "nocite") key]
    cp.process(compiled, references, requests, {nocite: nocite}) ^ {
        {citations: [], bibliography: [], references: [], diagnostics: [c.error_issue(^)]}
    }
}

pub fn prepare(ast, loaded, footnotes, options) {
    let found = model.collect(ast)
    let compiled = loaded.compiled
    let conflicts = [for (key in ["style", "citestyle", "bibstyle", "sorting"]
        where options.legacy_options[key] != null)
        c.issue("conflicting-citation-style", "CSL cannot be combined with the legacy " ++ key ++ " option", null, key)] ++
        (if (util.find_descendant(ast, "bibliographystyle") == null) [] else
            [c.issue("conflicting-citation-style", "CSL cannot be combined with bibliographystyle")])
    let entries = [for (ref in loaded.references) {*:ref.provenance, *:ref,
        kind: c.get(ref.provenance, "kind", ref.type), fields: c.get(ref.provenance, "fields", {})}]
    let sections = [for (section in 0 to found.next_section) {
        let keys = model.selected_keys(found.requests, entries, section, 0, [])
        let selected = [for (key in keys, entry in entries where entry.key == key) entry];
        {section: section, entries: selected, bib_entries: selected}
    }]
    let prints = [for (record in model.prepare_prints(found.prints, sections, found.requests))
        {*:record, valid: len(registry.bib_print_issues(record.node)) == 0}]
    let context = {processor: "csl", requests: found.requests, prints: prints,
        sections: sections, compiled: compiled, valid: compiled != null and len(conflicts) == 0}
    let processed = if (not context.valid) [] else [for (section in 0 to found.next_section)
        {section: section, result: section_result(section, loaded.references, found, context, footnotes, compiled)}]
    let diagnostics = loaded.diagnostics ++ found.diagnostics ++ conflicts ++
        [for (part in processed, issue in part.result.diagnostics) {
            let requests = [for (request in found.requests where any([for (group in request.items, key in group.keys)
                key == issue.item])) request];
            {*:issue, offset: if (len(requests) == 0) issue.offset else requests[0].offset}
        }] ++
        [for (request in found.requests where request.starred)
            c.issue("unsupported-citation-star", "Starred CSL citation variants are unsupported", null, request.tag)]
    {context: {*:context, processed: processed}, entries: loaded.entries,
        diagnostics: diagnostics, assets: loaded.assets,
        metadata: {processor: "lambda-script-citeproc", style_id: compiled.style.id,
            locale: compiled.language, csl_version: "1.0.2"}}
}

fn result_for(context, section) {
    let found = [for (part in context.processed where part.section == section) part.result]
    if (len(found) == 0) null else found[0]
}

fn citation_for(context, node) {
    let request = model.request_at(context, node.source_offset)
    let result = result_for(context, request.section)
    let found = [for (citation in result.citations where citation.id == string(node.source_offset)) citation]
    if (len(found) == 0) null else found[0]
}

pub fn render_content(node, context) {
    let request = model.request_at(context, node.source_offset)
    let citation = citation_for(context, node)
    if (not context.valid or citation == null or not request.valid or request.starred)
        util.unsupported_element("citeproc", "Citation could not be processed", node.source_offset)
    else <span class: "latex-cites csl-citation", id: "cite-" ++ string(node.source_offset), citation.content>
}

pub fn render_citation(node, context, footnotes) {
    let tag = string(name(node))
    let found = [for (entry in footnotes where entry.node.source_offset == node.source_offset) entry.number]
    if (tag == "nocite") null
    else if (len(found) > 0 and (tag == "footcite" or ((tag == "autocite" or tag == "autocites") and
        context.compiled.style.class == "note"))) {
        let footnote_number = found[0];
        <sup class: "latex-footnote-ref",
            <a href: "#fn-" ++ string(footnote_number), id: "fnref-" ++ string(footnote_number), string(footnote_number)>>
    } else render_content(node, context)
}

pub fn render_bibliography(node, context) {
    let record = model.print_at(context, node.source_offset)
    let result = result_for(context, record.scope_section)
    if (not context.valid or record == null or not record.valid)
        util.unsupported_element("citeproc", "Bibliography could not be processed", node.source_offset)
    else {
        let opts = record.options
        let loc = locale.sources(context.compiled.style, context.compiled.locales, context.compiled.language)
        let term = locale.term(loc, "reference", "long", true)
        let heading = c.get(opts, "title", upper(slice(term, 0, 1)) ++ slice(term, 1, len(term)))
        let bib = context.compiled.style.bibliography
        let css = (if (c.truth(bib["hanging-indent"])) "csl-hanging-indent " else "") ++
            (if (bib["second-field-align"] != null) "csl-align-fields" else "")
        let spacing = "line-height:" ++ c.get(bib, "line-spacing", "1") ++ ";--csl-entry-spacing:" ++
            c.get(bib, "entry-spacing", "1") ++ "em";
        <section class: "latex-bibliography csl-bibliography", id: "bibliography-" ++ string(record.index),
            if (opts.heading != "none") {
                if (opts.heading == "subbibliography") <h3 heading> else <h2 heading>
            }
            <ol class: "latex-bib-list " ++ css, style: spacing,
                for (entry in result.bibliography where any([for (selected in record.entries) selected.key == entry.id]))
                    <li class: "csl-entry", id: model.target_for_print(record, entry.id), entry.content>
            >
        >
    }
}

pub fn stylesheet() => "
.csl-bibliography .latex-bib-list{list-style:none;padding-left:0}
.csl-entry{margin-bottom:var(--csl-entry-spacing,1em)}
.csl-hanging-indent .csl-entry{padding-left:2em;text-indent:-2em}
.csl-align-fields .csl-entry{display:flex;gap:.5em}
.csl-left-margin{min-width:2em;flex-shrink:0}
.csl-right-inline{flex:1}
.csl-block{display:block}
.csl-indent{display:block;margin-left:2em}
"
