import scan: lambda.io.source_scan

let comments = [binary("#")]

fn trivia(s, pos: int) int^ => scan.trivia(s, pos, comments)^

fn identifier(s, start: int) map^ {
    var pos = start
    while (true) {
        let b = scan.byte(s, pos)^
        if (scan.ascii_alpha(b) or scan.digit(b) or b == 95 or b == 45 or b == 46) {
            pos = pos + 1
        } else { break }
    }
    return {value: scan.text(s, start, pos)^, next: pos}
}

fn property_separator(id) {
    let style = index_of(id, ".style.")
    let suffix = if (style != null and style + 7 < len(id)) null else last_index_of(id, ".")
    if (style != null and style + 7 < len(id)) {node_end: style, property: style + 7}
    else if (suffix != null and slice(id, suffix + 1, len(id)) in ["shape", "label", "style"])
        {node_end: suffix, property: suffix + 1}
    else null
}

fn label(s, start: int) map^ {
    let first = trivia(s, start)^
    if (scan.byte(s, first)^ == 34) { return scan.quoted(s, first, scan.standard_escape)^ }
    var pos = first
    while (pos < s.length and not (scan.byte(s, pos)^ in [10, 13, 123, 125, 35, 59])) {
        pos = pos + 1
    }
    var stop = pos
    while (stop > first and scan.space(scan.byte(s, stop - 1)^)) { stop = stop - 1 }
    return {value: if (stop == first) null else scan.text(s, first, stop)^, next: pos, issue: null}
}

let css_keys = {fontsize: "font-size", fontcolor: "color", fontname: "font-family",
    font: "font-family", arrowhead: "arrow-head", arrowtail: "arrow-tail",
    labelpos: "label-position", rankdir: "rank-dir", 'stroke-dash': "stroke-dasharray"}

fn attribute(props: map, key: string, value) map {
    if (value == null) { return props }
    return {*:props, [css_keys[key] or key]: scan.c_string(value)}
}

fn style(s, start: int, initial: map) map^ {
    var pos = start + 1
    var depth = 1
    var props = initial
    var issues = []
    while (depth > 0) {
        pos = trivia(s, pos)^
        let b = scan.byte(s, pos)^
        if (b == 125) {
            pos = pos + 1
            depth = depth - 1
        } else if (b == -1) { break }
        else {
            let key = identifier(s, pos)^
            if (key.value == "") {
                issues = [scan.problem(pos, "Expected property name in style block")]
                break
            }
            pos = trivia(s, key.next)^
            if (scan.byte(s, pos)^ != 58) {
                issues = [scan.problem(pos, "Expected ':' after property name")]
                break
            }
            pos = trivia(s, pos + 1)^
            if (scan.byte(s, pos)^ == 123) {
                depth = depth + 1;
                pos = pos + 1
                continue
            }
            let parsed = label(s, pos)^
            props = attribute(props, key.value, parsed.value)
            if (parsed.issue != null) { issues = [parsed.issue] }
            pos = trivia(s, parsed.next)^
        }
        // A nested block consumes a separator back in its enclosing level.
        if (depth > 0 and scan.byte(s, pos)^ == 59) { pos = pos + 1 }
    }
    let close_issues = if (depth == 0) [] else
        [for (i in 1 to depth) scan.problem(pos, "Expected '}' to close style block")];
    {props: props, next: pos, issues: [*issues, *close_issues]}
}

fn node(s, id, first: int, stop: int, props: map = {}) element^ =>
    <node id:id, *:scan.span(s, first, stop)^, *:props>

fn node_value(s, id, sep, first: int, colon: int) map^ {
    let pos = trivia(s, colon + 1)^
    let node_id = if (sep == null) id else slice(id, 0, sep.node_end) or ""
    let parsed = if (scan.byte(s, pos)^ == 123) style(s, pos, {})^ else {
        let value = label(s, pos)^
        let key = if (sep == null) "label" else slice(id, sep.property, len(id))
        {props: attribute({}, key, value.value), next: value.next,
         issues: if (value.issue == null) [] else [value.issue]}
    };
    {nodes: [node(s, node_id, first, pos, parsed.props)^], next: parsed.next, issues: parsed.issues}
}

fn edge(s, from, first: int, arrow: int) map^ {
    let target_at = trivia(s, arrow + 2)^
    let target = identifier(s, target_at)^
    if (target.value == "") {
        return {nodes: [], next: target_at, issues: [scan.problem(target_at, "Expected target node after '->'")]}
    }
    let pos = trivia(s, target.next)^
    let value = if (scan.byte(s, pos)^ == 58) label(s, pos + 1)^ else {value: null, next: pos, issue: null}
    let attrs = attribute({}, "label", value.value);
    {nodes: [node(s, from, first, value.next)^, node(s, target.value, target_at, value.next)^,
        <edge from:from, to:target.value, *:attrs, *:scan.span(s, first, value.next)^>],
     next: value.next, issues: if (value.issue == null) [] else [value.issue]}
}

fn statement(s, first: int) map^ {
    let id = identifier(s, first)^
    if (id.value == "") {
        return {nodes: [], next: first, issues: [scan.problem(first, "Expected identifier")]}
    }
    let pos = trivia(s, id.next)^
    let b = scan.byte(s, pos)^
    if (b == 58) { return node_value(s, id.value, property_separator(id.value), first, pos)^ }
    if (b == 45 and scan.byte(s, pos + 1)^ == 62) { return edge(s, id.value, first, pos)^ }
    return {nodes: if (b == -1) [] else [node(s, id.value, first, pos)^], next: pos, issues: []}
}

pub fn read(bytes: binary?) map^ {
    let length = len(bytes)
    let s = scan.source(bytes)^
    if (length == 0) { return {root: null, errors: []} }
    var pos = 0
    var statements = null
    var issues = []
    while ((pos < length and len(issues) < 100) ^) {
        pos = trivia(s, pos)^
        if ((pos == length)^) { break }
        let parsed = statement(s, pos)^
        if (len(parsed.nodes)^ > 0) { statements = scan.add(statements, parsed.nodes) }
        if (len(parsed.issues)^ > 0) { issues = [*issues, *parsed.issues] }
        pos = scan.line_end(s, parsed.next)^ + 1
    }
    let children = [for (nodes in scan.finish(statements)^, value in nodes) value];
    {root: <graph type:"directed", layout:"hierarchical", flavor:"d2", 'ir-stage':"source", *children>,
     errors: issues}
}
