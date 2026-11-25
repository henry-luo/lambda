import scan: lambda.io.source_scan
import lex: .lex

let handlers = [
    {suffix:"/.style 2 args", kind:"two"}, {suffix:"/.append style", kind:"append"},
    {suffix:"/.style", kind:"one"}, {suffix:"/.default", kind:"default"},
    {suffix:"/.pic", kind:"pic"}
]

pub fn find_handler(st, expected: string, kind: string) map?^ {
    let matches = [for (handler in st.styles where ((handler.name == expected and handler.kind == kind)^)) handler];
    matches[len(matches) - 1]
}

fn style_definitions(st, expected: string) array^ =>
    [for (handler in st.styles where ((handler.name == expected and handler.kind in ["one","two","append"])^)) handler]

fn add_handler(st, key: string, value: string) map^ {
    let handler = [for (entry in handlers where ((len(key) > len(entry.suffix)^ and ends_with(key, entry.suffix))^))
        entry][0]
    if (handler == null) { return st }
    let name = trim(slice(key, 0, len(key) - len(handler.suffix)) or "")
    if (name == "") { return st }
    if (((len(st.styles) >= 64))^) { return lex.fail(st, st.pos, "too many TikZ styles") }
    return {*:st, styles:[*st.styles, {name:name, kind:handler.kind, value:value}]}
}

pub fn substitute(st, value: string, args: array, arity: int) map^ {
    let s = scan.source(binary(value)^)^
    let limit = len(s.bytes)^
    var pos = 0
    var first = 0
    var pieces = null
    while (pos < limit) {
        let b = scan.byte(s, pos)^
        let following = if (b == 35) scan.byte(s, pos + 1)^ else -1
        if (following == 35 or (following >= 49 and following <= 57)) {
            let index = following - 49
            if (index >= arity) {
                return {cursor:lex.fail(st, st.pos, "TikZ style parameter exceeds its argument count"), value:null}
            }
            pieces = scan.add(pieces, scan.text(s, first, pos)^)
            let replacement = if (following == 35) "#" else args[index] or ""
            pieces = scan.add(pieces, replacement)
            pos = pos + 2
            first = pos
        } else { pos = pos + 1 }
    }
    let tail = scan.text(s, first, pos)^;
    {cursor:st, value:if (pieces == null) tail else join([*scan.finish(pieces)^, tail], "")}
}

fn two_arguments(value: string?) array?^ {
    if (value == null) { return null }
    let bytes = binary(value)^
    let s = scan.source(bytes)^
    let limit = len(bytes)
    let first = lex.balanced(s, lex.skip(s, 0, limit, false)^, limit, 123, 125)^
    if (first == null) { return null }
    let second = lex.balanced(s, lex.skip(s, first.next, limit, false)^, limit, 123, 125)^
    if (second == null or lex.skip(s, second.next, limit, false)^ != limit) { return null }
    return [lex.value(s, first)^, lex.value(s, second)^]
}

fn expand(st, definitions: array, name: string, arg: string?, depth: int) map^ {
    if (depth > 8) { return {cursor:lex.fail(st, st.pos, "TikZ style expansion exceeds 8 levels"), children:[]} }
    let bases = [for (i,entry in definitions where entry.kind != "append") i]
    let base = if (len(bases) == 0) 0 else bases[len(bases) - 1]
    let arity = if (definitions[base].kind == "two") 2 else 1
    let fallback = if (arg == null) find_handler(st, name, "default")^ else null
    let argument = arg or fallback.value
    let args = if (arity == 2) two_arguments(argument)^ else if (argument == null) [] else [argument]
    if (args == null) {
        return {cursor:lex.fail(st, st.pos, "two-argument TikZ style needs {first}{second}"), children:[]}
    }
    var idx = base
    var result = null
    var cursor = st
    while (idx < len(definitions)) {
        let body = substitute(cursor, definitions[idx].value, args, arity)^
        if (body.value == null) { return {cursor:body.cursor, children:[]} }
        let bytes = binary(body.value)^
        let s = scan.source(bytes)^
        let parsed = options(s, cursor, 0, len(bytes), false, true, false, depth + 1)^
        cursor = parsed.cursor
        if ((len(cursor.errors) > 0)^) { return {cursor:cursor, children:[]} }
        result = scan.add(result, parsed.children)
        idx = idx + 1
    }
    let batches = scan.finish(result)^;
    {cursor:cursor, children:[for (batch in batches, entry in batch) entry]}
}

fn option_parts(s, start: int, stop: int) map^ {
    let equal = lex.separator(s, start, stop, 61, true, true)^
    let name = lex.trimmed(s, start, equal, true)^
    let single = lex.trimmed(s, if (equal == stop) stop else equal + 1, stop, true)^
    let grouped = lex.balanced(s, single.first, single.stop, 123, 125)^
    let unwrapped = if (grouped != null and grouped.next == single.stop) grouped else single;
    {key:lex.value(s, name)^, value:lex.value(s, unwrapped)^, supplied:equal < stop}
}

pub fn options(s, st, first: int, stop: int, define: bool = false, use_styles: bool = false,
               defaults: bool = false, depth: int = 0) map^ {
    var cursor = st
    var entries = null
    var pos = first
    while (pos < stop) {
        let end_pos = lex.separator(s, pos, stop, 44, true, true, true)^
        let entry = option_parts(s, pos, end_pos)^
        pos = end_pos + 1
        if (entry.key == "") { continue }
        if (define) {
            cursor = add_handler(cursor, entry.key, entry.value)^
            if ((len(cursor.errors) > 0)^) { break }
        }
        let reference = if (entry.key == "style") entry.value else entry.key
        let definitions = if (use_styles) style_definitions(cursor, reference)^ else []
        if (len(definitions) > 0) {
            let argument = if (entry.supplied and entry.key != "style") entry.value else null
            let expanded = expand(cursor, definitions, reference, argument, depth)^
            cursor = expanded.cursor
            entries = scan.add(entries, expanded.children)
            if ((len(cursor.errors) > 0)^) { break }
            continue
        }
        if (defaults) {
            if (not (entry.key in ["every axis/.style", "every axis/.append style"])) {
                cursor = lex.fail(cursor, cursor.pos, "unsupported pgfplotsset key")
                break
            }
            let inherited = if (entry.key == "every axis/.style") [] else cursor.defaults
            if ((len(inherited) >= 16)^) {
                cursor = lex.fail(cursor, cursor.pos, "too many PGFPlots axis style declarations")
                break
            }
            cursor = {*:cursor, defaults:[*inherited, entry.value]}
        }
        entries = scan.add(entries, [<option key:entry.key, value:entry.value>])
    }
    let batches = scan.finish(entries)^;
    {cursor:cursor, children:[for (batch in batches, entry in batch) entry]}
}

pub fn read(s, st, define: bool = false, use_styles: bool = false, record_source: bool = true) map^ {
    let pos = lex.skip(s, st.pos, s.length)^
    let cursor = scan.set_cursor(st, pos)
    if (scan.byte(s, pos)^ != 91) { return {cursor:cursor, attrs:{}, children:[]} }
    let group = lex.expected_group(s, cursor, 91, 93)^
    if (group.value == null) { return {cursor:group.cursor, attrs:{}, children:[]} }
    let parsed = options(s, group.cursor, group.value.first, group.value.stop, define, use_styles)^
    return {*:parsed, attrs:if (record_source) {options_source:lex.value(s, group.value)^} else {}}
}

pub fn inherited(st, name: string) map^ {
    let definitions = style_definitions(st, name)^
    return if (len(definitions) == 0) {cursor:st, children:[]} else expand(st, definitions, name, null, 0)^
}
