// Bounded BibTeX reader and numeric bibliography in Lambda script.
import util: ~~.util
import paths: lambda.edit.session

fn find_entry_start(text, from) {
    let next = index_of(slice(text, from, len(text)), "@")
    if (next == null) null else from + next
}

fn matching_brace(text, i, depth, escaped) {
    if (i >= len(text)) null
    else {
        let ch = slice(text, i, i + 1)
        if (escaped) matching_brace(text, i + 1, depth, false)
        else if (ch == "\\") matching_brace(text, i + 1, depth, true)
        else if (ch == "{") matching_brace(text, i + 1, depth + 1, false)
        else if (ch == "}") {
            if (depth == 1) i else matching_brace(text, i + 1, depth - 1, false)
        } else matching_brace(text, i + 1, depth, false)
    }
}

fn field_value(source) {
    let trimmed = trim(source)
    let unquoted = if (len(trimmed) >= 2 and starts_with(trimmed, "\"") and ends_with(trimmed, "\""))
        slice(trimmed, 1, len(trimmed) - 1)
        else util.unwrap_braces(trimmed)
    replace(replace(unquoted, "{", ""), "}", "")
}

fn fields(parts, i, pairs) {
    if (i >= len(parts)) map(pairs)
    else {
        let part = trim(parts[i])
        let eq = util.top_level_separator(part, "=")
        if (eq == null) fields(parts, i + 1, pairs)
        else fields(parts, i + 1, pairs ++
            [lower(trim(slice(part, 0, eq))), field_value(slice(part, eq + 1, len(part)))])
    }
}

fn parse_entry(body, kind) {
    let parts = util.split_top_level(body, ",")
    let key = trim(parts[0])
    if (key == "" or kind == "comment" or kind == "preamble" or kind == "string") null
    else {key: key, kind: kind, fields: fields(parts, 1, [])}
}

fn parse_entries(text, from, acc) {
    let at = find_entry_start(text, from)
    if (at == null) acc
    else {
        let tail = slice(text, at + 1, len(text))
        let brace = index_of(tail, "{")
        if (brace == null) acc
        else {
            let open = at + 1 + brace
            let close = matching_brace(text, open, 0, false)
            if (close == null) acc
            else {
                let kind = lower(trim(slice(text, at + 1, open)))
                let entry = parse_entry(slice(text, open + 1, close), kind)
                parse_entries(text, close + 1, if (entry != null) acc ++ [entry] else acc)
            }
        }
    }
}

fn resources(node) {
    if (not (node is element)) []
    else if (string(name(node)) == "addbibresource")
        [trim(util.text_of_skip_brack(node))]
    else [for (child in node, resource in resources(child)) resource]
}

fn load_one(resource, base_uri) {
    let path = if (base_uri == null or starts_with(resource, "/")) resource
        else paths.resolve_path(base_uri, resource)
    let source = input(path, "text") ^ { null }
    if (source == null)
        {entries: [], diagnostics: [util.diagnostic("missing-bib-resource", "biblatex",
            resource, "Cannot read bibliography resource " ++ path, null)]}
    else {entries: parse_entries(source, 0, []), diagnostics: []}
}

fn load_resources(names, i, base_uri, entries, diagnostics) {
    if (i >= len(names)) {entries: entries, diagnostics: diagnostics}
    else {
        let loaded = load_one(names[i], base_uri)
        load_resources(names, i + 1, base_uri,
            entries ++ loaded.entries, diagnostics ++ loaded.diagnostics)
    }
}

fn sort_key(entry) {
    let f = entry.fields
    lower((if (f.author != null) f.author else entry.key) ++ "|" ++
          (if (f.title != null) f.title else "") ++ "|" ++
          (if (f.year != null) f.year else ""))
}

fn dedupe_entries(entries, i, seen, unique, issues) {
    if (i >= len(entries)) {entries: unique, diagnostics: issues}
    else {
        let key = entries[i].key
        let repeated = any([for (known in seen) known == key])
        let next = if (repeated) issues ++
            [util.diagnostic("duplicate-bib-key", "biblatex", key,
              "Duplicate bibliography key " ++ key, null)]
            else issues
        dedupe_entries(entries, i + 1, seen ++ [key],
            if (repeated) unique else unique ++ [entries[i]], next)
    }
}

pub fn load(ast, base_uri, opts) {
    if (opts == null) {entries: [], diagnostics: []}
    else {
        let style = if (opts.style != null) opts.style else "numeric"
        let sorting = if (opts.sorting != null) opts.sorting else "nty"
        let names = resources(ast)
        let loaded = load_resources(names, 0, base_uri, [], [])
        let style_issue = if (style != "numeric")
            [util.diagnostic("unsupported-bib-style", "biblatex", style,
              "Unsupported biblatex style " ++ style, null)]
            else []
        let sorting_issue = if (sorting != "nty" and sorting != "none")
            [util.diagnostic("unsupported-bib-sorting", "biblatex", sorting,
              "Unsupported biblatex sorting " ++ sorting, null)]
            else []
        let ordered = if (sorting == "nty") sort(loaded.entries, sort_key) else loaded.entries
        let unique = dedupe_entries(ordered, 0, [], [], [])
        {entries: unique.entries,
         diagnostics: loaded.diagnostics ++ style_issue ++ sorting_issue ++ unique.diagnostics}
    }
}

pub fn numbered_entries(entries, offset) {
    [for (i, entry in entries) {*:entry, number: offset + i + 1}]
}

fn entry_text(entry) {
    let f = entry.fields
    (if (f.author != null) f.author ++ ". " else "") ++
    (if (f.title != null) f.title ++ ". " else "") ++
    (if (f.journal != null) f.journal ++ ". " else "") ++
    (if (f.publisher != null) f.publisher ++ ". " else "") ++
    (if (f.year != null) f.year ++ "." else "")
}

pub fn render_bibliography(entries) {
    <section class: "latex-bibliography",
        <h2 "References">
        <ol class: "latex-bib-list",
            start: if (len(entries) > 0) string(entries[0].number) else null,
            for (entry in entries)
                <li id: "bib-" ++ entry.key, entry_text(entry)>
        >
    >
}
