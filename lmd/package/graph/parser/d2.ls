import scan: lambda.io.source_scan

let comments = [binary("#")]

fn trivia(s, pos: int, newlines: bool = true) int^ =>
    scan.trivia(s, pos, comments, false, newlines)^

fn connector(s, pos: int) map?^ {
    let first = scan.byte(s, pos)^
    let second = scan.byte(s, pos + 1)^
    if (first == 60 and second == 45) {
        return if (scan.byte(s, pos + 2)^ == 62) {value:"<->", next:pos + 3}
            else {value:"<-", next:pos + 2}
    }
    if (first == 45 and second == 62) { return {value:"->", next:pos + 2} }
    if (first == 45 and second == 45) { return {value:"--", next:pos + 2} }
    return null
}

fn identifier(s, start: int) map^ {
    var pos = start
    var stop = start
    while (true) {
        let b = scan.byte(s, pos)^
        if (b in [45,60] and connector(s, pos)^ != null) { break }
        if (scan.ascii_alpha(b) or scan.digit(b) or b >= 128 or b in [39,45,46,47,95]) {
            pos = pos + 1
            stop = pos
        } else if (pos > start and b in [9,32]) {
            pos = pos + 1
        } else { break }
    }
    return {value: scan.text(s, start, stop)^, next: stop}
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
    let first = trivia(s, start, false)^
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

fn edge_chain(s, id, first: int, initial) map^ {
    var pos = initial.next
    var link = initial
    var left = id
    var segments = null
    while (link != null) {
        let target_at = trivia(s, link.next, false)^
        let target = identifier(s, target_at)^
        if (target.value == "") {
            return {nodes: [], next: target_at,
                issues: [scan.problem(target_at, "Expected target node after '" ++ link.value ++ "'")]}
        }
        segments = scan.add(segments, {from:left, to:target.value, at:target_at, operator:link.value})
        left = target.value
        pos = trivia(s, target.next, false)^
        link = connector(s, pos)^
    }
    let value = if (scan.byte(s, pos)^ == 58) label(s, pos + 1)^ else {value: null, next: pos, issue: null}
    let attrs = attribute({}, "label", value.value);
    // A label after the last endpoint applies to every connection in the chain.
    {nodes: [node(s, id, first, value.next)^,
        for (segment in scan.finish(segments)^) (
            node(s, segment.to, segment.at, value.next)^,
            <edge from:if (segment.operator == "<-") segment.to else segment.from,
                to:if (segment.operator == "<-") segment.from else segment.to,
                *:attrs, *:scan.span(s, first, value.next)^,
                *:if (segment.operator in ["--", "<->"]) {
                    'arrow-tail':if (segment.operator == "<->") "normal" else "none",
                    'arrow-head':if (segment.operator == "<->") "normal" else "none"
                } else {}>
        )],
     next: value.next, issues: if (value.issue == null) [] else [value.issue]}
}

let directions = {up:"BT", down:"TB", left:"RL", right:"LR"}

fn statement(s, first: int) map^ {
    let id = identifier(s, first)^
    if (id.value == "") {
        return {nodes: [], next: first, issues: [scan.problem(first, "Expected identifier")]}
    }
    let pos = trivia(s, id.next, false)^
    let b = scan.byte(s, pos)^
    if (b == 58 and id.value == "direction") {
        let value = label(s, pos + 1)^
        let direction = if (value.value == null) null else directions[lower(value.value)]
        return {nodes: [], next: value.next, direction:direction,
            issues: if (direction != null) [] else [scan.problem(pos + 1, "Expected D2 direction up, down, left, or right")]}
    }
    if (b == 58) { return node_value(s, id.value, property_separator(id.value), first, pos)^ }
    let op = connector(s, pos)^
    if (op != null) { return edge_chain(s, id.value, first, op)^ }
    return {nodes: [node(s, id.value, first, pos)^], next: pos, issues: []}
}

pub fn read(bytes: binary?) map^ {
    let length = len(bytes)
    let s = scan.source(bytes)^
    if (length == 0) { return {root: null, errors: []} }
    var pos = 0
    var statements = null
    var issues = []
    var direction = null
    while ((pos < length and len(issues) < 100) ^) {
        pos = trivia(s, pos)^
        if ((pos == length)^) { break }
        let parsed = statement(s, pos)^
        if (len(parsed.nodes)^ > 0) { statements = scan.add(statements, parsed.nodes) }
        if (len(parsed.issues)^ > 0) { issues = [*issues, *parsed.issues] }
        if (parsed.direction != null) { direction = parsed.direction }
        let next = parsed.next
        pos = if (scan.byte(s, next)^ == 59) next + 1 else scan.line_end(s, next)^ + 1
    }
    let children = [for (nodes in scan.finish(statements)^, value in nodes) value];
    {root: <graph type:"directed", layout:"hierarchical", flavor:"d2", 'ir-stage':"source",
        *:if (direction == null) {} else {'rank-dir':direction}, *children>,
     errors: issues}
}
