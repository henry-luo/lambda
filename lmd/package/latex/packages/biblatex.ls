// BibLaTeX's bounded local profile: BibTeX data, citation styles and HTML output.
import util: ~~.util
import data: .bib_data
import model: .bib_model
import style: .bib_style
import registry: .registry
import csl: ~~.citeproc.adapter

pub fn is_citation_command(tag) => model.is_citation(tag) or tag == "nocite"

pub fn prepare(ast, base_uri, opts, language) {
    if (opts == null) {entries: [], context: null, diagnostics: []}
    else {
        let loaded = data.load(ast, base_uri)
        let settings = {*:style.settings(opts, language),
            option_valid: registry.bib_profile_valid(opts)}
        let prepared = model.prepare(ast, loaded.entries, settings)
        // Invalid print options cannot provide a citation destination.
        let context = {*:prepared, prints: [for (record in prepared.prints)
            {*:record, valid: len(registry.bib_print_issues(record.node)) == 0}]}
        let language_issues = [for (entry in loaded.entries
            where entry.fields.langid != null and
                not (style.supported_language(entry.fields.langid) ^ { false }))
            {*:util.diagnostic("unsupported-bib-language", "biblatex", entry.fields.langid,
                "Unsupported bibliography language " ++ entry.fields.langid,
                entry.resource_offset), file: entry.resource, line: entry.resource_line}]
        {entries: loaded.entries, context: context,
            diagnostics: loaded.diagnostics ++ context.diagnostics ++ language_issues}
    }
}

fn valid_profile(settings) => (style.supported_style(settings.citestyle) ^ { false }) and
    (style.supported_style(settings.bibstyle) ^ { false }) and
    (style.supported_sorting(settings.sorting) ^ { false }) and
    (style.supported_language(settings.language) ^ { false }) and
    settings.backend == "biber" and settings.option_valid

fn link(context, request, entry, label) {
    if (entry == null) util.unsupported_element("biblatex",
        "Unresolved citation", request.offset)
    else {
        let target = model.target_for(context, request, entry.key)
        if (target == null) <span class: "latex-cite", label>
        else <a class: "latex-cite", href: target, label>
    }
}

fn linked_one(context, request, key, form) {
    let entry = model.labelled_entry(context, request.section, key)
    if (entry == null) link(context, request, null, "?")
    else if (form == "fullcite")
        <span class: "latex-fullcite",
            link(context, request, entry,
                style.bibliography_text(entry, context.settings));
            entry_links(entry, context.settings)
        >
    else {
        let settings = context.settings
        let author = style.author_text(entry, settings, true)
        let label = if (form == "citeauthor") author
            else if (form == "citetitle")
                style.citation_title(entry)
            else if (form == "citeyear") entry.year_label
            else if (form == "textcite")
                if (settings.citestyle == "numeric" or settings.citestyle == "numeric-comp" or
                    settings.citestyle == "alphabetic") author ++ " " ++
                        (if (settings.citation_open == null) "[" else settings.citation_open) ++
                        entry.label ++ (if (settings.citation_close == null) "]" else settings.citation_close)
                else if (settings.citestyle == "authortitle") author ++ " (" ++
                    style.citation_title(entry) ++ ")"
                else author ++ " (" ++ entry.year_label ++ ")"
            else if (settings.citestyle == "numeric" or settings.citestyle == "numeric-comp" or
                settings.citestyle == "alphabetic") entry.label
            else if (settings.citestyle == "authortitle") author ++ ", " ++
                style.citation_title(entry)
            else author ++ (if (settings.author_year_sep == null) " "
                else settings.author_year_sep) ++ entry.year_label
        link(context, request, entry, label)
    }
}

fn compact_form(form) => form == "cite" or form == "parencite" or
    form == "autocite" or form == "footcite" or form == "cites" or
    form == "parencites" or form == "autocites"

fn citation_number(context, request, key) {
    let entry = model.labelled_entry(context, request.section, key)
    if (entry == null) 999999 else entry.number
}

fn unique_keys(keys, index, seen) {
    if (index >= len(keys)) seen
    else unique_keys(keys, index + 1,
        if (any([for (key in seen) key == keys[index]])) seen
        else seen ++ [keys[index]])
}

fn run_end(keys, context, request, index) {
    if (index + 1 >= len(keys) or
        citation_number(context, request, keys[index + 1]) !=
            citation_number(context, request, keys[index]) + 1) index + 1
    else run_end(keys, context, request, index + 1)
}

fn numeric_runs(keys, context, request, index, runs) {
    if (index >= len(keys)) runs
    else {
        let finish = run_end(keys, context, request, index)
        numeric_runs(keys, context, request, finish,
            runs ++ [{start: keys[index], final: keys[finish - 1],
                length: finish - index}])
    }
}

fn compact_numeric_keys(context, request, keys) {
    let known = [for (key in unique_keys(keys, 0, [])
        where model.labelled_entry(context, request.section, key) != null) key]
    let ordered = sort(known, (key) => citation_number(context, request, key))
    let runs = numeric_runs(ordered, context, request, 0, []);
    <span class: "latex-citation-keys",
        for (i, run in runs) {
            if (i > 0) { ", " }
            linked_one(context, request, run.start, "cite")
            if (run.length == 2) { ", " linked_one(context, request, run.final, "cite") }
            else if (run.length >= 3) { "–" linked_one(context, request, run.final, "cite") }
        }
        for (key in keys where model.labelled_entry(context, request.section, key) == null)
            linked_one(context, request, key, "cite")
    >
}

fn compact_year_link(context, request, entry, prior) {
    let base = style.year_text(entry, context.settings.language)
    let display = if (prior != null and
        style.year_text(prior, context.settings.language) == base)
            slice(entry.year_label, len(base), len(entry.year_label))
        else entry.year_label
    link(context, request, entry, display)
}

fn compact_author_year_keys(context, request, keys) {
    <span class: "latex-citation-keys",
        for (i, key in unique_keys(keys, 0, [])) {
            let entry = model.labelled_entry(context, request.section, key)
            let prior = if (i == 0) null else
                model.labelled_entry(context, request.section, keys[i - 1])
            let same = entry != null and prior != null and
                style.same_author(entry, prior)
            if (i > 0) { if (same) { ", " } else { "; " } }
            if (same) { compact_year_link(context, request, entry, prior) }
            else { linked_one(context, request, key, "cite") }
        }
    >
}

fn item_piece(context, request, item, form) {
    let settings = context.settings
    let single_form = if (form == "cites") "cite"
        else if (form == "parencites") "parencite"
        else if (form == "textcites") "textcite"
        else if (form == "autocites") "autocite" else form
    let numeric = settings.citestyle == "numeric" or
        settings.citestyle == "numeric-comp" or settings.citestyle == "alphabetic"
    let parens = single_form == "parencite" or single_form == "autocite" or
        single_form == "footcite"
    let bracket = numeric and single_form != "textcite" and single_form != "citeauthor" and
        single_form != "citetitle" and single_form != "citeyear" and single_form != "fullcite"
    let open_text = if ((bracket or parens) and settings.citation_open != null)
        settings.citation_open else if (bracket) "[" else if (parens) "(" else ""
    let close_text = if ((bracket or parens) and settings.citation_close != null)
        settings.citation_close else if (bracket) "]" else if (parens) ")" else "";
    <span class: "latex-citation-item",
        open_text
        if (item.prenote != "") { item.prenote ++ " " }
        if (settings.citestyle == "numeric-comp" and compact_form(single_form)) {
            compact_numeric_keys(context, request, item.keys)
        } else if (settings.citestyle == "authoryear-comp" and compact_form(single_form)) {
            compact_author_year_keys(context, request, item.keys)
        } else {
            for (i, key in unique_keys(item.keys, 0, [])) {
                if (i > 0) {
                    if (settings.citestyle == "alphabetic" and compact_form(single_form)) { "; " }
                    else if (numeric and compact_form(single_form)) { ", " }
                    else { "; " }
                }
                linked_one(context, request, key, single_form)
            }
        }
        if (item.postnote != "") { ", " ++ item.postnote }
        close_text
    >
}

fn footcite_number(footnotes, offset) {
    let found = [for (entry in footnotes
        where entry.node.source_offset == offset) entry.number]
    if (len(found) == 0) 0 else found[0]
}

pub fn render_citation(node, context, footnotes) {
    if (context.processor == "csl") csl.render_citation(node, context, footnotes)
    else {
    let request = model.request_at(context, node.source_offset)
    if (request == null) util.unsupported_element("biblatex",
        "Citation request was not collected", node.source_offset)
    else if (not valid_profile(context.settings)) util.unsupported_element("biblatex",
        "Unsupported biblatex profile", node.source_offset)
    else if (not request.valid) util.unsupported_element("biblatex",
        "Invalid citation arguments", node.source_offset)
    else if (request.starred) util.unsupported_element("biblatex",
        "Starred citation variant is unsupported", node.source_offset)
    else if ((request.original_tag == "citet" or request.original_tag == "citep") and
        not context.settings.natbib) util.unsupported_element("biblatex",
        "Citation alias requires natbib=true", node.source_offset)
    else if (request.tag == "footcite") {
        let footnote_index = footcite_number(footnotes, request.offset);
        <sup class: "latex-footnote-ref",
            <a href: "#fn-" ++ string(footnote_index),
                id: "fnref-" ++ string(footnote_index), string(footnote_index)>
        >
    } else if (request.tag == "nocite") null
    else <span class: "latex-cites", id: "cite-" ++ string(request.offset),
        for (i, item in request.items) {
            if (i > 0) { "; " }
            item_piece(context, request, item, request.tag)
        }
    >
    }
}

fn external_url(kind, value) {
    if (value == null or trim(value) == "") null
    else if (kind == "doi") "https://doi.org/" ++ trim(value)
    else if (starts_with(value, "https://") or starts_with(value, "http://")) value
    else null
}

fn entry_links(entry, settings) {
    let f = entry.fields
    let doi = if (settings.doi) external_url("doi", f.doi) else null
    let url = if (settings.url) external_url("url", f.url) else null
    let isbn = if (settings.isbn) f.isbn else null;
    <span class: "latex-bib-links",
        if (doi != null) { " " <a href: doi, "DOI: " ++ f.doi> }
        if (url != null) { " " <a href: url, "URL"> }
        if (isbn != null) { " ISBN: " ++ isbn }
        if (f.eprint != null) { " " ++ f.eprint }
    >
}

fn bibliography_label(entry, settings) {
    if (settings.bibstyle == "numeric" or settings.bibstyle == "numeric-comp")
        "[" ++ string(entry.number) ++ "] "
    else if (settings.bibstyle == "alphabetic") "[" ++ entry.label ++ "] "
    else ""
}

fn backref_links(entry, record, requests) {
    let citations = [for (request in requests
        where request.section == record.scope_section and request.tag != "nocite" and
            (record.scope_segment == null or request.segment == record.scope_segment) and
            any([for (item in request.items, key in item.keys) key == entry.key])) request]
    if (len(citations) == 0) null
    else <span class: "latex-bib-backrefs", " Cited: "
        for (index, request in citations) {
            if (index > 0) { ", " }
            <a href: "#cite-" ++ string(request.offset), string(index + 1)>
        }
    >
}

fn bibliography_item(entry, record, settings, requests) {
    <li id: model.target_for_print(record, entry.key),
        <span class: "latex-bib-label", bibliography_label(entry, settings)>
        style.bibliography_text(entry, settings);
        entry_links(entry, settings)
        if (settings.backref) { backref_links(entry, record, requests) }
    >
}

fn bibliography_heading(record, settings) {
    let opts = record.options
    let heading = if (opts.heading == null) "bibliography" else opts.heading
    let title = if (opts.title != null) opts.title
        else style.locale(settings.language).references
    if (heading == "none") null
    else if (heading == "subbibliography") <h3 title>
    else <h2 title>
}

pub fn render_bibliography(node, context) {
    if (context.processor == "csl") csl.render_bibliography(node, context)
    else {
    let record = model.print_at(context, node.source_offset)
    if (record == null) util.unsupported_element("biblatex",
        "Bibliography request was not collected", node.source_offset)
    else if (not record.valid)
        util.unsupported_element("biblatex",
            "Unsupported bibliography options", node.source_offset)
    else if (not valid_profile(context.settings)) util.unsupported_element("biblatex",
        "Unsupported biblatex profile", node.source_offset)
    else <section class: "latex-bibliography",
        id: "bibliography-" ++ string(record.index),
        bibliography_heading(record, context.settings);
        <ol class: "latex-bib-list",
            for (entry in record.entries)
                bibliography_item(entry, record, context.settings, context.requests)
        >
    >
    }
}

pub fn render_footcite_content(node, context) {
    if (context.processor == "csl") csl.render_content(node, context)
    else {
    let request = model.request_at(context, node.source_offset)
    if (request == null) util.unsupported_element("biblatex",
        "Footnote citation was not collected", node.source_offset)
    else if (not request.valid or request.starred or not valid_profile(context.settings))
        util.unsupported_element("biblatex", "Unsupported footnote citation", node.source_offset)
    else <span class: "latex-cites",
        for (i, item in request.items) {
            if (i > 0) { "; " }
            item_piece(context, request, item, "footcite")
        }
    >
    }
}
