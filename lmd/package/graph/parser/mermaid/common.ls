import scan: lambda.io.source_scan

let comment = binary("%%")
let init_open = binary("%%{")
let init_close = binary("}%%")
let front_marker = binary("---")

pub fn ascii_space(b: int) bool => scan.space(b) or b == 11 or b == 12

pub fn skip(s, start: int) int^ {
    var pos = start
    while (true) {
        let b = scan.byte(s, pos)^
        if (ascii_space(b) or b == 59) { pos = pos + 1 }
        else if (b == 37 and scan.starts(s, pos, comment)^ and not scan.starts(s, pos, init_open)^) {
            pos = scan.line_end(s, pos)^
        } else { break }
    }
    return pos
}

fn word_byte(b: int) bool => scan.ascii_alpha(b) or scan.digit(b) or b == 95

pub fn keyword(s, pos: int, word: string) bool^ {
    let next_byte = scan.byte(s, pos + len(word))^
    return not (word_byte(next_byte) or next_byte == 45) and scan.starts(s, pos, binary(word)^)^
}

pub fn keyword_at(s, pos: int, words: string[]) string?^ =>
    [for (word in words where keyword(s, pos, word)^) word][0]

pub fn trim_text(s, start: int, stop: int) string^ {
    var left = start
    var right = stop
    while (left < right and ascii_space(scan.byte(s, left)^)) { left = left + 1 }
    while (right > left and ascii_space(scan.byte(s, right - 1)^)) { right = right - 1 }
    return scan.text(s, left, right)^
}

pub fn line_text(s, start: int) map^ {
    let stop = scan.line_end(s, start)^;
    {value:trim_text(s, start, stop)^, next:stop}
}

pub fn next_line(s, pos: int) int^ {
    let stop = scan.line_end(s, pos)^
    return if (scan.byte(s, stop)^ == 10) stop + 1 else stop
}

pub fn raw_until(s, start: int, delims: int[]) int^ {
    var pos = start
    while (not (scan.byte(s, pos)^ in delims)) { pos = pos + 1 }
    return pos
}

pub fn front_matter(s, start: int) map^ {
    if (not scan.starts(s, start, front_marker)^ or not (scan.byte(s, start + 3)^ in [10, 13])) {
        return {next:start, child:null, warning:null}
    }
    let body = next_line(s, start)^
    var pos = body
    while ((pos < s.length)^) {
        if (scan.starts(s, pos, front_marker)^ and scan.byte(s, pos + 3)^ in [-1, 10, 13]) {
            let stop = next_line(s, pos + 3)^
            let value = trim_text(s, body, pos)^;
            return {next:stop, child:<'front-matter' value:value, *:scan.span(s, start, stop)^>, warning:null}
        }
        pos = next_line(s, pos)^
    }
    let value = trim_text(s, body, pos)^;
    {next:pos, child:<'front-matter' value:value, *:scan.span(s, start, pos)^>,
     warning:{offset:pos, message:"Unterminated Mermaid front matter"}}
}

pub fn initialization(s, start: int) map^ {
    if (not scan.starts(s, start, init_open)^) { return {next:start, child:null, warning:null} }
    var pos = start + 3
    while ((pos < s.length and not scan.starts(s, pos, init_close)^)^) { pos = pos + 1 }
    let value = trim_text(s, start + 3, pos)^
    let closed = scan.starts(s, pos, init_close)^
    let stop = if (closed) pos + 3 else pos;
    {next:stop, child:<init value:value, *:scan.span(s, start, stop)^>,
     warning:if (closed) null else {offset:pos, message:"Unterminated Mermaid initialization directive"}}
}

pub fn identifier(s, initial: int) map^ {
    let start = scan.inline_space(s, initial)^
    if (scan.byte(s, start)^ == 96) {
        let stop = raw_until(s, start + 1, [-1, 10, 96])^
        let next_pos = if (scan.byte(s, stop)^ == 96) stop + 1 else stop;
        return {value:scan.text(s, start + 1, stop)^, next:next_pos}
    }
    var pos = start
    while (true) {
        let b = scan.byte(s, pos)^
        if (word_byte(b) or b >= 128 or
                (pos > start and b == 45 and not (scan.byte(s, pos + 1)^ in [45, 46, 61, 62]))) {
            pos = pos + 1
        } else { break }
    }
    return {value:if (pos == start) null else scan.text(s, start, pos)^, next:pos}
}

type letter = ("A" to "Z") | ("a" to "z")
type html_open = \("<" "/"? letter !("<" | "\n" | ">")* ">")

pub fn normalize_label(raw: string) map^ {
    let value = if (len(raw) >= 2 and starts_with(raw, "\"") and ends_with(raw, "\""))
        slice(raw, 1, len(raw) - 1) or "" else raw
    if (len(value) >= 2 and starts_with(value, "`") and ends_with(value, "`")) {
        return {value:slice(value, 1, len(value) - 1) or "", format:"markdown"}
    }
    return {value:value, format:if (len(find(value, html_open)) > 0) "html" else "text"}
}

pub fn append(st, child) map => {*:st, children:scan.add(st.children, child)}

fn node(s, id, label, first, stop, attrs: map = {}) element^ =>
    <node id:id, label:label, shape:"box", *:attrs, *:scan.span(s, first, stop)^>

pub fn declare(s, st, id: string, label: string, start, stop, attrs: map) map^ {
    let recorded = append(st, node(s, id, label, start, stop, attrs)^)
    return {*:recorded, ids:{*:recorded.ids, [id]:true}}
}

pub fn ensure(s, st, id: string, start, stop) map^ =>
    if (st.ids[id] == true) st else declare(s, st, id, id, start, stop, {})^

pub fn fail(s, st, start: int, pos: int, code: string, message: string) map^ =>
    scan.set_cursor(scan.issue(s, st, start, code, message)^, scan.line_end(s, pos)^)

fn add_meta(s, st, tag: string, value: string, start, stop) map^ {
    // The first directive fixes the position of the single metadata container.
    let entry = <[tag] value:value, *:scan.span(s, start, stop)^>
    let body = if (st.meta == null) append(st, {metadata:true}) else st;
    {*:body, meta:scan.add(st.meta, entry)}
}

pub fn accessibility(s, st, start: int, initial: int, tag: string) map^ {
    let pos = scan.inline_space(s, initial)^
    let b = scan.byte(s, pos)^
    if (b == 58) {
        let line = line_text(s, pos + 1)^
        return scan.set_cursor(add_meta(s, st, tag, line.value, start, line.next)^, line.next)
    }
    if (b == 123 and tag == "description") {
        let stop = raw_until(s, pos + 1, [-1, 125])^
        let value = trim_text(s, pos + 1, stop)^
        let closed = scan.byte(s, stop)^ == 125
        let next_pos = if (closed) stop + 1 else stop
        let next_state = if (closed) st else
            scan.issue(s, st, start, st.syntax_code, "Unterminated Mermaid accessibility description", "warning")^
        return scan.set_cursor(add_meta(s, next_state, tag, value, start, next_pos)^, next_pos)
    }
    let expected = if (tag == "description") " or '{...}'" else ""
    let warning = scan.issue(s, st, start, st.syntax_code,
        "Expected ':'" ++ expected ++ " after Mermaid accessibility directive", "warning")^
    return scan.set_cursor(warning, scan.line_end(s, pos)^)
}

pub fn parse_body(s, initial, reader) map^ {
    var st = initial
    while ((st.pos < s.length)^) {
        let pos = skip(s, st.pos)^
        st = scan.set_cursor(st, pos)
        if (((pos >= s.length))^) { break }
        let parsed = reader(s, st, pos)^
        let stop = parsed.pos
        st = scan.set_cursor(parsed, if (scan.byte(s, stop)^ == 10) stop + 1 else stop)
    }
    return st
}

pub fn with_warning(s, st, warning) map^ =>
    if (warning == null) st else scan.issue(s, st, warning.offset, st.syntax_code, warning.message, "warning")^


pub fn strip_semicolons(s, start: int, stop: int) string^ {
    var pos = stop
    while (pos > start) {
        let b = scan.byte(s, pos - 1)^
        if (ascii_space(b) or b == 59) { pos = pos - 1 } else { break }
    }
    return trim_text(s, start, pos)^
}

pub fn delimited(s, start: int, delimiter: binary, line_stop: int = -1, note_escapes: bool = false) map^ {
    var pos = start
    var first = start
    var pieces = null
    while ((pos < s.length and scan.byte(s, pos)^ != line_stop and not scan.starts(s, pos, delimiter)^)^) {
        if (scan.byte(s, pos)^ == 92) {
            pieces = scan.add(pieces, scan.text(s, first, pos)^)
            pos = pos + 1
            first = pos
            if (note_escapes and scan.byte(s, pos)^ == 110) {
                pieces = scan.add(pieces, "\n")
                first = pos + 1
            }
            if ((pos < s.length)^) { pos = pos + 1 }
        } else { pos = pos + 1 }
    }
    let tail = scan.text(s, first, pos)^
    let stop = if (scan.starts(s, pos, delimiter)^) pos + len(delimiter) else pos;
    {value:if (pieces == null) tail else join([*scan.finish(pieces)^, tail], ""), next:stop}
}

let shape_syntax = [
    {opening:"(((", ending:")))", shape:"doublecircle"}, {opening:"((", ending:"))", shape:"circle"},
    {opening:"([", ending:"])", shape:"stadium"}, {opening:"[(", ending:")]", shape:"cylinder"},
    {opening:"[[", ending:"]]", shape:"subroutine"}, {opening:"{{", ending:"}}", shape:"hexagon"},
    {opening:"[/", ending:"\\]", shape:"trapezoid"}, {opening:"[\\", ending:"/]", shape:"trapezoid-alt"},
    {opening:">", ending:"]", shape:"asymmetric"}, {opening:"(", ending:")", shape:"rounded"},
    {opening:"[", ending:"]", shape:"box"}, {opening:"{", ending:"}", shape:"diamond"}
]

pub fn node_shape(s, pos: int) map?^ {
    let syntax = [for (entry in shape_syntax where scan.starts(s, pos, binary(entry.opening)^)^) entry][0]
    if (syntax == null) { return null }
    let parsed = delimited(s, pos + len(syntax.opening), binary(syntax.ending)^)^
    let label = normalize_label(parsed.value)^;
    {next:parsed.next, shape:syntax.shape, label:label.value, format:label.format}
}

pub fn direction(s, initial: int) map^ {
    let pos = scan.inline_space(s, initial)^
    let value = scan.text(s, pos, min(pos + 2, s.length))^;
    {next:if (value in ["LR","RL","TB","TD","BT"]) pos + 2 else pos,
        value:if (value == "TD") "TB" else if (value in ["LR","RL","TB","BT"]) value else null}
}

fn family_direction(s, st, start: int, pos: int, family: string, display: string) map^ {
    let read = direction(s, pos)^
    let advanced = scan.set_cursor(st, read.next)
    if (read.value != null) { return {*:advanced, direction:read.value} }
    return scan.issue(s, advanced, start, "mermaid." ++ family ++ ".direction",
        "Expected " ++ display ++ " diagram direction LR, RL, TB, TD, or BT")^
}

fn class_ids(s, initial: int) map^ {
    var pos = scan.inline_space(s, initial)^
    var ids = null
    while (true) {
        let id = identifier(s, pos)^
        pos = id.next
        if (id.value == null) { break }
        ids = scan.add(ids, id.value)
        pos = scan.inline_space(s, pos)^
        if (scan.byte(s, pos)^ != 44) { break }
        pos = scan.inline_space(s, pos + 1)^
    }
    return {values:scan.finish(ids)^, next:pos}
}

fn style_rule(s, st, start: int, pos: int) map^ {
    let classes = class_ids(s, pos)^
    let stop = scan.line_end(s, classes.next)^
    if (len(classes.values)^ == 0) {
        let warning = scan.issue(s, st, classes.next, st.syntax_code, "Expected class name after classDef", "warning")^
        return scan.set_cursor(warning, stop)
    }
    var output = st
    let declarations = strip_semicolons(s, classes.next, stop)^
    var idx = 0
    while ((idx < len(classes.values))^) {
        output = append(output, <'style-rule' class:classes.values[idx], declarations:declarations,
            *:scan.span(s, start, stop)^>)
        idx = idx + 1
    }
    return scan.set_cursor(output, stop)
}

pub fn class_assignment(s, st, start: int, initial: int, force_nodes: bool = false) map^ {
    let pos = skip(s, initial)^
    let b = scan.byte(s, pos)^
    let quoted = b == 34 or b == 39
    let targets = if (quoted) delimited(s, pos + 1, binary(chr(b))^)^ else class_ids(s, pos)^
    let class_id = identifier(s, targets.next)^
    if (class_id.value == null) {
        let warning = scan.issue(s, st, class_id.next, st.syntax_code, "Expected class name in class definition", "warning")^
        return scan.set_cursor(warning, class_id.next)
    }
    let all_edges = not force_nodes and not quoted and all([for (value in targets.values) st.edge_ids[value] == true])
    let value = <'class-assignment' 'target-kind':(if (all_edges) "edge" else "node"),
        targets:(if (quoted) targets.value else join(targets.values, ",")), class:class_id.value,
        *:scan.span(s, start, class_id.next)^>
    return scan.set_cursor(append(st, value), class_id.next)
}

pub fn style_assignment(s, st, start: int, initial: int, edge_assignment: bool = false) map^ {
    let pos = scan.inline_space(s, initial)^
    let stop = scan.line_end(s, pos)^
    let colon = raw_until(s, pos, [-1, 10, 58])^
    let keyword = if (edge_assignment) "linkStyle" else "style"
    if (colon == stop) {
        let warning = scan.issue(s, st, start, st.syntax_code,
            "Expected CSS declarations in Mermaid " ++ keyword ++ " statement", "warning")^
        return scan.set_cursor(warning, stop)
    }
    var decl = colon
    while (decl > pos and not ascii_space(scan.byte(s, decl - 1)^)) { decl = decl - 1 }
    let target_text = scan.text(s, pos, decl)^
    let targets = join([for (ch in target_text where not (ch in [" ", "\t", "\r", "\n", "\f", "\u000b"])) ch], "")
    let declarations = strip_semicolons(s, decl, stop)^
    if (targets == "" or declarations == "") {
        let warning = scan.issue(s, st, start, st.syntax_code,
            "Expected targets and declarations in Mermaid " ++ keyword ++ " statement", "warning")^
        return scan.set_cursor(warning, stop)
    }
    let result = <'style-assignment' 'target-kind':(if (edge_assignment) "edge" else "node"),
        targets:targets, declarations:declarations, *:scan.span(s, start, stop)^>
    return scan.set_cursor(append(st, result), stop)
}

pub fn class_suffix(s, st, target: string, initial: int) map^ {
    let pos = scan.inline_space(s, initial)^
    let length = if (scan.starts(s, pos, binary(":::")^)^) 3
        else if (scan.byte(s, pos)^ == 46) 1 else 0
    if (length == 0) { return scan.set_cursor(st, pos) }
    let first = pos + length
    let b = scan.byte(s, first)^
    if (not (scan.ascii_alpha(b) or b == 95)) {
        let warning = scan.issue(s, st, first, st.syntax_code, "Expected class name after ':::'", "warning")^
        return scan.set_cursor(warning, first)
    }
    var stop = first + 1
    while (word_byte(scan.byte(s, stop)^) or scan.byte(s, stop)^ == 45) { stop = stop + 1 }
    let value = <'class-assignment' 'target-kind':"node", targets:target,
        class:scan.text(s, first, stop)^, *:scan.span(s, pos, stop)^>
    return scan.set_cursor(append(st, value), stop)
}

pub let common_directives = ["direction", "accTitle", "accDescr", "classDef", "class", "style"]

pub fn family_directive(s, st, start: int, head, family: string, display: string) map?^ {
    let after = start + len(head)
    if (head == "direction") { return family_direction(s, st, start, after, family, display)^ }
    if (head == "accTitle" or head == "accDescr") {
        return accessibility(s, st, start, after, if (head == "accTitle") "title" else "description")^
    }
    if (head == "classDef") { return style_rule(s, st, start, after)^ }
    if (head == "class") { return class_assignment(s, st, start, after)^ }
    if (head == "style") { return style_assignment(s, st, start, after)^ }
    return null
}

pub fn unescaped(s, start: int, closing: int, line_stop: int = -1) map^ {
    let stop = raw_until(s, start, [-1, closing, line_stop])^;
    {value:scan.text(s, start, stop)^, next:if (scan.byte(s, stop)^ == closing) stop + 1 else stop}
}

fn interaction_token(s, initial: int) map^ {
    let start = scan.inline_space(s, initial)^
    let b = scan.byte(s, start)^
    if (b in [-1,10,59]) { return {value:null, next:start} }
    if (b in [34,39]) { return delimited(s, start + 1, binary(chr(b))^)^ }
    var pos = start
    var first = start
    var depth = 0
    var pieces = null
    while ((pos < s.length)^) {
        let current = scan.byte(s, pos)^
        if (depth == 0 and (ascii_space(current) or current == 59)) { break }
        if (current == 40) { depth = depth + 1 }
        else if (current == 41 and depth > 0) { depth = depth - 1 }
        if (current == 92 and scan.byte(s, pos + 1)^ != -1) {
            pieces = scan.add(pieces, scan.text(s, first, pos)^)
            pos = pos + 1
            first = pos
        }
        pos = pos + 1
    }
    let tail = scan.text(s, first, pos)^;
    {value:if (pieces == null) tail else join([*scan.finish(pieces)^, tail], ""), next:pos}
}

pub fn interaction(s, st, start: int, initial: int, forced_action: string? = null) map^ {
    let target = interaction_token(s, initial)^
    if (target.value == null or target.value == "") {
        let warning = scan.issue(s, st, start, st.syntax_code, "Expected target after Mermaid click directive", "warning")^
        return scan.set_cursor(warning, scan.line_end(s, target.next)^)
    }
    let pos = scan.inline_space(s, target.next)^
    let directive = if (forced_action == null) keyword_at(s, pos, ["href","call"])^ else null
    let quoted = scan.byte(s, pos)^ in [34,39]
    let action = forced_action or (if (directive == "href" or (directive == null and quoted)) "link" else "callback")
    let value = interaction_token(s, pos + len(directive))^
    let hint = interaction_token(s, value.next)^
    let window = interaction_token(s, hint.next)^
    let advanced = scan.set_cursor(st, window.next)
    if (value.value == null or value.value == "") {
        return scan.issue(s, advanced, start, st.syntax_code, "Expected link or callback after Mermaid click target", "warning")^
    }
    let window_alias = window.value == null and hint.value in ["_blank", "_self", "_parent", "_top"]
    let target_window = if (window_alias) hint.value else window.value
    let tooltip = if (window_alias) null else hint.value
    let entry = <interaction target:target.value, action:action,
        *:if (action == "link") {href:value.value} else {callback:value.value},
        *:if (tooltip == null or tooltip == "") {} else {tooltip:tooltip},
        *:if (target_window == null or target_window == "") {} else {'target-window':target_window},
        *:scan.span(s, start, window.next)^>
    return append(advanced, entry)
}
