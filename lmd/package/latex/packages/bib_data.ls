// Local BibTeX data parsing and provenance for the Lambda-script biblatex profile.
import util: ~~.util
import paths: lambda.edit.session

pub let ENTRY_TYPES = ["article", "book", "inbook", "incollection",
    "inproceedings", "thesis", "report", "online", "misc", "unpublished"]

pub fn supported_entry_type(kind) => any([for (entry_type in ENTRY_TYPES)
    entry_type == kind])

fn char_at(source, index) => slice(source, index, index + 1)

fn source_line(source, index, line) {
    if (index >= len(source)) line
    else source_line(source, index + 1,
        line + (if (char_at(source, index) == "\n") 1 else 0))
}

fn file_issue(code, path, position, source, message) {
    let line = source_line(slice(source, 0, position), 0, 1)
    {*:util.diagnostic(code, "biblatex", path, message, position),
        file: path, line: line}
}

fn entry_issue(code, entry, item, message) {
    {*:util.diagnostic(code, "biblatex", item, message, entry.resource_offset),
        file: entry.resource, line: entry.resource_line}
}

// Replace comment bytes with spaces so field parsing keeps original offsets.
fn without_comments(source, index, braces, quoted, escaped, comment, acc) {
    if (index >= len(source)) acc
    else {
        let ch = char_at(source, index)
        if (comment)
            without_comments(source, index + 1, braces, quoted, false,
                ch != "\n", acc ++ (if (ch == "\n") "\n" else " "))
        else if (escaped)
            without_comments(source, index + 1, braces, quoted, false, false, acc ++ ch)
        else if (ch == "\\")
            without_comments(source, index + 1, braces, quoted, true, false, acc ++ ch)
        else if (ch == "\"" and braces == 0)
            without_comments(source, index + 1, braces, not quoted, false, false, acc ++ ch)
        else if (not quoted and ch == "{")
            without_comments(source, index + 1, braces + 1, false, false, false, acc ++ ch)
        else if (not quoted and ch == "}")
            without_comments(source, index + 1, braces - 1, false, false, false, acc ++ ch)
        else if (not quoted and braces == 0 and ch == "%")
            without_comments(source, index + 1, braces, false, false, true, acc ++ " ")
        else without_comments(source, index + 1, braces, quoted, false, false, acc ++ ch)
    }
}

fn skip_comment(source, index) {
    if (index >= len(source) or char_at(source, index) == "\n") index
    else skip_comment(source, index + 1)
}

fn find_at(source, index) {
    if (index >= len(source)) null
    else if (char_at(source, index) == "%")
        find_at(source, skip_comment(source, index + 1))
    else if (char_at(source, index) == "@") index
    else find_at(source, index + 1)
}

fn find_open(source, index) {
    if (index >= len(source)) null
    else if (char_at(source, index) == "{" or char_at(source, index) == "(") index
    else if (char_at(source, index) == "@") null
    else find_open(source, index + 1)
}

// Track quotes and braces so a closing delimiter inside a field is not a record end.
fn find_close(source, index, close, depth, braces, quoted, escaped) {
    if (index >= len(source)) null
    else {
        let ch = char_at(source, index)
        if (escaped) find_close(source, index + 1, close, depth, braces, quoted, false)
        else if (ch == "\\") find_close(source, index + 1, close, depth, braces, quoted, true)
        else if (ch == "\"" and braces == 0)
            find_close(source, index + 1, close, depth, braces, not quoted, false)
        else if (quoted) find_close(source, index + 1, close, depth, braces, quoted, false)
        else if (ch == "%" and braces == 0)
            find_close(source, skip_comment(source, index + 1), close,
                depth, braces, quoted, false)
        else if (ch == "{")
            find_close(source, index + 1, close,
                if (close == "}") depth + 1 else depth, braces + 1, false, false)
        else if (ch == "}")
            if (close == "}" and depth == 1) index
            else find_close(source, index + 1, close,
                if (close == "}") depth - 1 else depth, braces - 1, false, false)
        else if (ch == "(" and close == ")" and braces == 0)
            find_close(source, index + 1, close, depth + 1, braces, false, false)
        else if (ch == ")" and close == ")" and braces == 0)
            if (depth == 1) index
            else find_close(source, index + 1, close, depth - 1, braces, false, false)
        else find_close(source, index + 1, close, depth, braces, false, false)
    }
}

let MONTHS = [
    {key: "jan", val: "January"}, {key: "feb", val: "February"},
    {key: "mar", val: "March"}, {key: "apr", val: "April"},
    {key: "may", val: "May"}, {key: "jun", val: "June"},
    {key: "jul", val: "July"}, {key: "aug", val: "August"},
    {key: "sep", val: "September"}, {key: "oct", val: "October"},
    {key: "nov", val: "November"}, {key: "dec", val: "December"}
]

fn value_atom(source, macros, path, position, file_text) {
    let atom = trim(source)
    if (len(atom) >= 2 and starts_with(atom, "{") and ends_with(atom, "}"))
        {text: util.unwrap_braces(atom), diagnostics: []}
    else if (len(atom) >= 2 and starts_with(atom, "\"") and ends_with(atom, "\""))
        {text: slice(atom, 1, len(atom) - 1), diagnostics: []}
    else if (atom == "") {text: "", diagnostics: []}
    else {
        let named = util.lookup(macros, lower(atom))
        let month = util.lookup(MONTHS, lower(atom))
        if (named != null) {text: named, diagnostics: []}
        else if (month != null) {text: month, diagnostics: []}
        else if ((int(atom) ^ { null }) != null) {text: atom, diagnostics: []}
        else {text: atom, diagnostics: [file_issue("undefined-bib-string", path,
            position, file_text, "Undefined BibTeX string " ++ atom)]}
    }
}

fn value_parts(parts, index, macros, path, position, file_text, text, issues) {
    if (index >= len(parts)) {raw: text, text: replace(replace(text, "{", ""), "}", ""),
        diagnostics: issues}
    else {
        let atom = value_atom(parts[index], macros, path, position, file_text)
        value_parts(parts, index + 1, macros, path, position, file_text,
            text ++ atom.text, issues ++ atom.diagnostics)
    }
}

fn value_of(source, macros, path, position, file_text) {
    value_parts(util.split_top_level(source, "#"), 0, macros, path, position, file_text, "", [])
}

fn parse_fields(parts, index, macros, path, position, file_text, values, raw_values, issues) {
    if (index >= len(parts))
        {fields: map(values), raw_fields: map(raw_values), diagnostics: issues}
    else {
        let part = trim(parts[index])
        let eq = util.top_level_separator(part, "=")
        if (part == "") parse_fields(parts, index + 1, macros, path, position,
            file_text, values, raw_values, issues)
        else if (eq == null) parse_fields(parts, index + 1, macros, path, position,
            file_text, values, raw_values, issues ++
                [file_issue("malformed-bib-field", path, position, file_text,
                    "Expected name=value in bibliography field")])
        else {
            let key = lower(trim(slice(part, 0, eq)))
            let value = value_of(slice(part, eq + 1, len(part)), macros,
                path, position, file_text)
            parse_fields(parts, index + 1, macros, path, position, file_text,
                values ++ [key, value.text], raw_values ++ [key, value.raw],
                issues ++ value.diagnostics)
        }
    }
}

fn parse_entry(body, kind, macros, path, position, file_text, entry_types) {
    let parts = util.split_top_level(without_comments(body, 0, 0, false, false, false, ""), ",")
    let key = trim(parts[0])
    let parsed = parse_fields(parts, 1, macros, path, position, file_text, [], [], [])
    let issues = if (key == "")
        [file_issue("missing-bib-key", path, position, file_text,
            "Bibliography entry has no key")] else []
    let type_issues = if (kind == "xdata" or any([for (item in entry_types) item == kind])) []
        else [file_issue("unsupported-bib-entry-type", path, position, file_text,
            "Unsupported bibliography entry type " ++ kind)]
    {entry: if (key == "" or len(type_issues) > 0) null else
        {key: key, kind: kind, fields: parsed.fields, raw_fields: parsed.raw_fields,
            resource: path, resource_offset: position,
            resource_line: source_line(slice(file_text, 0, position), 0, 1)},
        diagnostics: issues ++ type_issues ++ parsed.diagnostics}
}

fn parse_string(body, macros, path, position, file_text) {
    let clean = without_comments(body, 0, 0, false, false, false, "")
    let eq = util.top_level_separator(clean, "=")
    if (eq == null) {macros: macros,
        diagnostics: [file_issue("malformed-bib-string", path, position, file_text,
            "Expected name=value in @string")]}
    else {
        let key = lower(trim(slice(clean, 0, eq)))
        let value = value_of(slice(clean, eq + 1, len(clean)), macros,
            path, position, file_text)
        {macros: macros ++ [{key: key, val: value.text}],
            diagnostics: value.diagnostics}
    }
}

fn parse_records(source, path, index, macros, entries, issues, entry_types) {
    let at = find_at(source, index)
    if (at == null) {macros: macros, entries: entries, diagnostics: issues}
    else {
        let opening = find_open(source, at + 1)
        if (opening == null) {macros: macros, entries: entries,
            diagnostics: issues ++ [file_issue("malformed-bib-record", path, at,
                source, "Bibliography record has no opening delimiter")]}
        else {
            let kind = lower(trim(slice(source, at + 1, opening)))
            let closing = if (char_at(source, opening) == "{") "}" else ")"
            let end = find_close(source, opening + 1, closing, 1, 0, false, false)
            if (end == null) {macros: macros, entries: entries,
                diagnostics: issues ++ [file_issue("malformed-bib-record", path, at,
                    source, "Bibliography record is not closed")]}
            else {
                let body = slice(source, opening + 1, end)
                if (kind == "comment" or kind == "preamble")
                    parse_records(source, path, end + 1, macros, entries, issues, entry_types)
                else if (kind == "string") {
                    let string_value = parse_string(body, macros, path, at, source)
                    parse_records(source, path, end + 1, string_value.macros, entries,
                        issues ++ string_value.diagnostics, entry_types)
                } else {
                    let parsed = parse_entry(body, kind, macros, path, at, source, entry_types)
                    parse_records(source, path, end + 1, macros,
                        if (parsed.entry == null) entries else entries ++ [parsed.entry],
                        issues ++ parsed.diagnostics, entry_types)
                }
            }
        }
    }
}

pub fn resources(node) {
    if (not (node is element)) []
    else if (string(name(node)) == "addbibresource")
        [{source: trim(util.text_of_skip_brack(node)), offset: node.source_offset}]
    else if (string(name(node)) == "bibliography")
        [for (raw in util.split_top_level(util.text_of(node), ","))
            {source: if (ends_with(trim(raw), ".bib")) trim(raw)
                else trim(raw) ++ ".bib", offset: node.source_offset}]
    else [for (child in node, resource in resources(child)) resource]
}

// filecontents is a document-local resource; its raw body never reaches the filesystem.
pub fn inline_resources(node) {
    if (not (node is element)) []
    else if (string(name(node)) == "filecontents" or
        string(name(node)) == "filecontents*")
        [{key: node.filename, source: node.source,
            offset: node.body_offset}]
    else [for (child in node, resource in inline_resources(child)) resource]
}

pub fn inline_source(inline, name) {
    let matches = [for (resource in inline where resource.key == name) resource]
    if (len(matches) == 0) null else matches[0]
}

pub fn resource_path(source, base_uri) =>
    if (base_uri == null or starts_with(source, "/")) source
    else paths.resolve_path(base_uri, source)

pub fn resource_assets(ast, base_uri) {
    let inline = inline_resources(ast);
    [for (resource in resources(ast)) resource_asset(resource, base_uri, inline)]
}

fn resource_asset(resource, base_uri, inline) {
    let local = inline_source(inline, resource.source)
    let path = resource_path(resource.source, base_uri)
    {kind: "bibliography", source: if (local != null) "filecontents:" ++ resource.source else path,
     origin: if (local != null) "inline" else "local-file",
     available: local != null or exists(path), offset: resource.offset}
}

fn load_resources(names, index, base_uri, inline, macros, entries, issues) {
    if (index >= len(names)) {entries: entries, diagnostics: issues}
    else {
        let resource = names[index]
        let local = inline_source(inline, resource.source)
        let path = resource_path(resource.source, base_uri)
        let source = if (local != null) local.source else input(path, "text") ^ { null }
        let provenance = if (local != null) "filecontents:" ++ resource.source else path
        if (source == null) load_resources(names, index + 1, base_uri, inline, macros, entries,
            issues ++ [util.diagnostic("missing-bib-resource", "biblatex",
                resource.source, "Cannot read bibliography resource " ++ path, resource.offset)])
        else {
            let parsed = parse_records(source, provenance, 0, macros, [], [], ENTRY_TYPES)
            load_resources(names, index + 1, base_uri, inline, parsed.macros,
                entries ++ parsed.entries, issues ++ parsed.diagnostics)
        }
    }
}

fn dedupe(entries, index, seen, unique, issues) {
    if (index >= len(entries)) {entries: unique, diagnostics: issues}
    else {
        let entry = entries[index]
        let duplicate = any([for (key in seen) key == entry.key])
        let next_issues = if (duplicate) issues ++
            [entry_issue("duplicate-bib-key", entry, entry.key,
                "Duplicate bibliography key " ++ entry.key ++ " in " ++ entry.resource)]
            else issues
        dedupe(entries, index + 1, seen ++ [entry.key],
            if (duplicate) unique else unique ++ [entry], next_issues)
    }
}

fn entry_by_key(entries, key) {
    let matches = [for (entry in entries where entry.key == key) entry]
    if (len(matches) == 0) null else matches[0]
}

fn parent_keys(entry) {
    let xdata = if (entry.fields.xdata == null) [] else
        [for (part in util.split_top_level(entry.fields.xdata, ",")
            where trim(part) != "") trim(part)]
    if (entry.fields.crossref == null) xdata
    else xdata ++ [trim(entry.fields.crossref)]
}

fn inherit_entry(entry, entries, stack) {
    if (any([for (key in stack) key == entry.key]))
        {fields: entry.fields, raw_fields: entry.raw_fields,
            diagnostics: [entry_issue("cyclic-bib-inheritance", entry, entry.key,
                "Cyclic bibliography inheritance at " ++ entry.key)]}
    else inherit_parents(parent_keys(entry), 0, entries, stack ++ [entry.key],
        entry, entry.fields, entry.raw_fields, [])
}

fn inherit_parents(keys, index, entries, stack, child, fields, raw_fields, issues) {
    if (index >= len(keys))
        {fields: fields, raw_fields: raw_fields, diagnostics: issues}
    else {
        let key = keys[index]
        let parent = entry_by_key(entries, key)
        if (parent == null) inherit_parents(keys, index + 1, entries, stack, child,
            fields, raw_fields, issues ++
                [entry_issue("missing-bib-parent", child, key,
                    "Missing bibliography parent " ++ key)])
        else {
            let resolved = inherit_entry(parent, entries, stack)
            // Child fields win over inherited fields from either source.
            inherit_parents(keys, index + 1, entries, stack, child,
                {*:resolved.fields, *:fields},
                {*:resolved.raw_fields, *:raw_fields},
                issues ++ resolved.diagnostics)
        }
    }
}

fn inherited_entries(entries, index, resolved, issues) {
    if (index >= len(entries)) {entries: resolved, diagnostics: issues}
    else {
        let entry = entries[index]
        let inherited = inherit_entry(entry, entries, [])
        inherited_entries(entries, index + 1,
            resolved ++ [{*:entry, fields: inherited.fields,
                raw_fields: inherited.raw_fields}],
            issues ++ inherited.diagnostics)
    }
}

fn unique_issues(issues, index, seen, unique) {
    if (index >= len(issues)) unique
    else {
        let issue = issues[index]
        let key = issue.code ++ ":" ++ string(issue.item) ++ ":" ++ string(issue.offset)
        unique_issues(issues, index + 1, seen ++ [key],
            if (any([for (prior in seen) prior == key])) unique else unique ++ [issue])
    }
}

pub fn load(ast, base_uri) {
    let loaded = load_resources(resources(ast), 0, base_uri,
        inline_resources(ast), [], [], [])
    finalize(loaded.entries, loaded.diagnostics)
}

// Shared parsing and inheritance preserve the legacy profile while CSL accepts more types.
pub fn parse_source(source, path = "<bibliography>", macros = [], entry_types = ENTRY_TYPES) =>
    parse_records(source, path, 0, macros, [], [], entry_types)

pub fn finalize(entries, issues = []) {
    let unique = dedupe(entries, 0, [], [], [])
    let inherited = inherited_entries(unique.entries, 0, [], [])
    {entries: inherited.entries,
        diagnostics: unique_issues(
            issues ++ unique.diagnostics ++ inherited.diagnostics, 0, [], [])}
}
