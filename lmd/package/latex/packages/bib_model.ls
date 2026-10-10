// Source-order citation requests, reference scopes, labels and print selections.
import util: ~~.util
import style: .bib_style

pub let CITATIONS = ["cite", "parencite", "textcite", "autocite", "footcite",
    "citeauthor", "citetitle", "citeyear", "fullcite", "cites", "parencites",
    "textcites", "autocites", "citet", "citep"]

pub fn is_citation(tag) => any([for (name in CITATIONS) name == tag])

fn cite_items(groups, index, notes, items, issues, offset) {
    if (index >= len(groups)) {items: items, diagnostics: issues}
    else {
        let group = groups[index]
        if (group.kind == "optional")
            // TeX's tie keeps adjacent note words together in HTML/PDF text.
            cite_items(groups, index + 1, notes ++ [replace(group.raw, "~", " ")],
                items, issues, offset)
        else if (group.kind == "required") {
            let keys = [for (key in util.split_top_level(group.raw, ",")
                where trim(key) != "") trim(key)]
            let pre = if (len(notes) > 1) notes[0] else ""
            let post = if (len(notes) > 1) notes[1]
                else if (len(notes) == 1) notes[0] else ""
            let invalid = if (len(notes) > 2 or len(keys) == 0)
                [util.diagnostic("invalid-citation-arguments", "biblatex", null,
                    "Citation needs one or two notes and at least one key", offset)] else []
            cite_items(groups, index + 1, [], items ++
                [{keys: keys, prenote: pre, postnote: post}], issues ++ invalid, offset)
        } else cite_items(groups, index + 1, notes, items,
            issues ++ [util.diagnostic("unsupported-citation-argument", "biblatex",
                group.kind, "Unsupported citation argument " ++ group.kind, offset)], offset)
    }
}

fn citation(node, tag, section, segment) {
    let groups = node.argument_groups
    let parsed = if (groups == null) {items: [{keys: [trim(util.text_of_skip_brack(node))],
        prenote: "", postnote: ""}], diagnostics: []}
        else cite_items(groups, 0, [], [], [], node.source_offset)
    let effective = if (tag == "citet") "textcite"
        else if (tag == "citep") "parencite" else tag
    let missing = if (len(parsed.items) == 0)
        [util.diagnostic("invalid-citation-arguments", "biblatex", tag,
            "Citation has no key group", node.source_offset)] else []
    {request: {offset: node.source_offset, tag: effective, original_tag: tag, items: parsed.items,
        valid: len(parsed.diagnostics) == 0 and len(missing) == 0,
        section: section, segment: segment, starred: node.starred == true},
     diagnostics: parsed.diagnostics ++ missing}
}

fn walk_children(node, index, cursor, section, segment) {
    if (index >= len(node)) cursor
    else walk_children(node, index + 1,
        walk(node[index], cursor, section, segment), section, segment)
}

fn walk(node, cursor, section, segment) {
    if (not (node is element)) cursor
    else {
        let tag = string(name(node))
        if (tag == "refsection") {
            let next = cursor.next_section + 1
            walk_children(node, 0, {*:cursor, next_section: next}, next, 0)
        } else if (tag == "refsegment") {
            let prior = util.lookup(cursor.segment_counts, string(section))
            let next = if (prior == null) 1 else prior + 1
            let counts = cursor.segment_counts ++ [{key: string(section), val: next}]
            walk_children(node, 0, {*:cursor, segment_counts: counts}, section, next)
        } else if (is_citation(tag) or tag == "nocite") {
            let parsed = citation(node, tag, section, segment)
            let next = {*:cursor, requests: cursor.requests ++ [parsed.request],
                diagnostics: cursor.diagnostics ++ parsed.diagnostics}
            walk_children(node, 0, next, section, segment)
        } else if (tag == "printbibliography" or tag == "bibliography") {
            let opts = if (tag == "bibliography") {}
                else util.parse_kv_options(util.optional_raw(node))
            let record = {offset: node.source_offset, node: node, options: opts,
                section: section, segment: segment, index: len(cursor.prints) + 1}
            walk_children(node, 0, {*:cursor, prints: cursor.prints ++ [record]},
                section, segment)
        } else walk_children(node, 0, cursor, section, segment)
    }
}

fn key_in(keys, key) => any([for (item in keys) item == key])

fn add_keys(keys, additions) {
    if (len(additions) == 0) keys
    else {
        let key = additions[0]
        add_keys(if (key_in(keys, key)) keys else keys ++ [key],
            slice(additions, 1, len(additions)))
    }
}

fn request_keys(request) => [for (item in request.items, key in item.keys) key]

pub fn selected_keys(requests, entries, section, index, keys) {
    if (index >= len(requests)) keys
    else {
        let request = requests[index]
        let additions = if (request.section != section) []
            else if (request.tag == "nocite" and key_in(request_keys(request), "*"))
                [for (entry in entries where entry.kind != "xdata") entry.key]
            else request_keys(request)
        selected_keys(requests, entries, section, index + 1,
            add_keys(keys, additions))
    }
}

fn source_entry_for(entries, key) {
    let found = [for (entry in entries where entry.key == key) entry]
    if (len(found) == 0) null else found[0]
}

fn entry_for(entries, key) {
    let found = source_entry_for(entries, key)
    // xdata supplies inherited fields but is not a printable citation entry.
    if (found != null and found.kind == "xdata") null else found
}

fn labels_for_section(entries, requests, section, settings) {
    let keys = selected_keys(requests, entries, section, 0, [])
    let selected = [for (key in keys where entry_for(entries, key) != null)
        entry_for(entries, key)]
    let ordered = style.sort_entries(selected, settings.sorting)
    {section: section,
     entries: style.assign_labels(ordered, settings.citestyle, settings.language),
     bib_entries: style.assign_labels(ordered, settings.bibstyle, settings.language)}
}

fn all_sections(entries, requests, count, settings) {
    [for (section in 0 to count)
        labels_for_section(entries, requests, section, settings)]
}

fn section_entries(sections, section) {
    let found = [for (part in sections where part.section == section) part.entries]
    if (len(found) == 0) [] else found[0]
}

fn section_bib_entries(sections, section) {
    let found = [for (part in sections where part.section == section) part.bib_entries]
    if (len(found) == 0) [] else found[0]
}

pub fn labelled_entry(context, section, key) =>
    entry_for(section_entries(context.sections, section), key)

fn keyword_match(entry, wanted) {
    let words = if (entry.fields.keywords == null) []
        else [for (word in util.split_top_level(entry.fields.keywords, ","))
            lower(trim(word))]
    key_in(words, lower(trim(wanted)))
}

fn passes_filters(entry, opts) {
    (opts.type == null or entry.kind == opts.type) and
    (opts.nottype == null or entry.kind != opts.nottype) and
    (opts.keyword == null or keyword_match(entry, opts.keyword)) and
    (opts.notkeyword == null or not keyword_match(entry, opts.notkeyword))
}

fn print_section(record) {
    let specified = record.options.section
    if (specified == null) record.section else int(specified) ^ { record.section }
}

fn print_segment(record) {
    let specified = record.options.segment
    if (specified == null) null else int(specified) ^ { null }
}

fn entry_in_segment(requests, section, segment, key) {
    any([for (request in requests
        where request.section == section and request.segment == segment and
            key_in(request_keys(request), key)) true])
}

fn print_entries(record, sections, requests) {
    let section = print_section(record)
    let segment = print_segment(record);
    [for (entry in section_bib_entries(sections, section)
        where passes_filters(entry, record.options) and
            (segment == null or entry_in_segment(requests, section, segment, entry.key)))
        entry]
}

fn target_id(record, key) {
    if (record.index == 1 and print_section(record) == 0) "bib-" ++ key
    else "bib-s" ++ string(print_section(record)) ++ "-p" ++
        string(record.index) ++ "-" ++ key
}

pub fn prepare_prints(prints, sections, requests) {
    [for (record in prints)
        {*:record, scope_section: print_section(record),
            scope_segment: print_segment(record),
            entries: print_entries(record, sections, requests)}]
}

fn request_issue(request, entries, settings) {
    let missing = [for (key in request_keys(request)
        where (key != "*" or request.tag != "nocite") and
            source_entry_for(entries, key) == null)
        util.diagnostic("unresolved-citation", "biblatex", key,
            "Unresolved citation " ++ key, request.offset)]
    let data_only = [for (key in request_keys(request)
        where source_entry_for(entries, key) != null and
            source_entry_for(entries, key).kind == "xdata")
        util.diagnostic("data-only-bib-citation", "biblatex", key,
            "xdata cannot be cited or printed directly: " ++ key, request.offset)]
    let star = if (request.starred)
        [util.diagnostic("unsupported-citation-star", "biblatex", request.tag,
            "Starred citation variant is unsupported", request.offset)] else []
    let alias = if ((request.original_tag == "citet" or request.original_tag == "citep") and
        not settings.natbib)
        [util.diagnostic("natbib-alias-disabled", "biblatex", request.original_tag,
            "Citation alias requires natbib=true", request.offset)] else []
    missing ++ data_only ++ star ++ alias
}

fn request_issues(requests, entries, settings) {
    [for (request in requests, issue in request_issue(request, entries, settings)) issue]
}

// Collection is independent of the selected bibliography processor.
pub fn collect(ast) {
    walk(ast,
        {requests: [], prints: [], diagnostics: [], next_section: 0,
            segment_counts: []},
        0, 0)
}

pub fn prepare(ast, entries, settings) {
    let walked = collect(ast)
    let found = {*:walked, requests: [for (request in walked.requests)
        if (settings.bibtex == true and request.original_tag == "cite")
            {*:request, tag: if (settings.citestyle == "authoryear" and
                all([for (item in request.items) item.prenote == "" and
                    item.postnote == ""])) "textcite" else "parencite"}
        else request]}
    let sections = all_sections(entries, found.requests, found.next_section, settings)
    let prints = prepare_prints(found.prints, sections, found.requests)
    {requests: found.requests, prints: prints, sections: sections, settings: settings,
        diagnostics: found.diagnostics ++ request_issues(found.requests, entries, settings)}
}

pub fn request_at(context, offset) {
    let found = [for (request in context.requests where request.offset == offset) request]
    if (len(found) == 0) null else found[0]
}

pub fn print_at(context, offset) {
    let found = [for (record in context.prints where record.offset == offset) record]
    if (len(found) == 0) null else found[0]
}

pub fn target_for(context, request, key) {
    let candidates = [for (record in context.prints
        where record.valid != false and record.scope_section == request.section and
            any([for (entry in record.entries) entry.key == key])) record]
    let exact = [for (record in candidates
        where record.scope_segment == request.segment and request.segment != 0) record]
    let broad = [for (record in candidates where record.scope_segment == null) record]
    let chosen = if (len(exact) > 0) exact[0]
        else if (len(broad) > 0) broad[0]
        else if (len(candidates) > 0) candidates[0] else null
    if (chosen == null) null else "#" ++ target_id(chosen, key)
}

pub fn target_for_print(record, key) => target_id(record, key)
