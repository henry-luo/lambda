import scan: lambda.io.source_scan

let max_depth = 256
let max_chain = 4096
let max_token = 1024 * 1024

fn skip(s, pos) int^ => scan.trivia(s, pos, null, true)^
fn id_start(b: int) bool => scan.ascii_alpha(b) or b == 95 or b >= 128
fn id_byte(b: int) bool => id_start(b) or scan.digit(b)

fn at_keyword(s, pos, word: string) bool^ {
    let stop = pos + len(word)
    return stop <= s.length and not id_byte(scan.byte(s, stop)^) and
        lower(scan.text(s, pos, stop)^) == word
}

fn token(s, st, first_at, stop: int, kind, value) map^ {
    if (stop - first_at > max_token) {
        return {parsed_state:scan.issue(s, st, first_at, "dot.token-limit", "DOT ID exceeds the parser token limit")^,
            next:stop, value:null, kind:kind, start:first_at}
    }
    return {parsed_state:st, next:stop, value:value, kind:kind, start:first_at}
}

fn quoted_escape(s, pos: int) map^ {
    let b = scan.byte(s, pos)^
    let after = pos + 1;
    {value: if (b == 10 or b == 13) "" else if (b == 34) "\"" else scan.text(s, pos - 1, after)^,
     next: if (b == 13 and scan.byte(s, after)^ == 10) after + 1 else after}
}

fn quoted_id(s, st, start: int) map^ {
    var pos = start
    var parts = null
    var parsed_state = st
    while (true) {
        let item = scan.quoted(s, pos, quoted_escape)^
        if (item.issue != null) {
            let failed = scan.issue(s, parsed_state, start, "dot.unterminated-string",
                "Unterminated quoted DOT ID")^;
            return {parsed_state:failed, next:item.next, value:null, start:start}
        }
        parts = scan.add(parts, item.value)
        pos = skip(s, item.next)^
        if (scan.byte(s, pos)^ != 43) { break }
        pos = skip(s, pos + 1)^
        if (scan.byte(s, pos)^ != 34) {
            parsed_state = scan.issue(s, parsed_state, pos, "dot.string-concat", "Expected quoted ID after '+'")^
            break
        }
    }
    return token(s, parsed_state, start, pos, "quoted", join(scan.finish(parts)^, ""))^
}

fn html_id(s, st, start: int) map^ {
    var pos = start + 1
    var depth = 1
    var quote = 0
    while ((pos < s.length and depth > 0 and pos - start <= max_token)^) {
        let b = scan.byte(s, pos)^
        if (quote != 0) {
            if (b == quote) { quote = 0 }
        } else if (b == 34 or b == 39) { quote = b }
        else if (b == 60) { depth = depth + 1 }
        else if (b == 62) { depth = depth - 1 }
        pos = pos + 1
    }
    if (depth > 0) {
        return {parsed_state:scan.issue(s, st, start, "dot.unterminated-html-id", "Unterminated HTML-like DOT ID")^,
            next:pos, value:null, start:start}
    }
    return token(s, st, start, pos, "html", scan.text(s, start, pos)^)^
}

fn numeric_start(s, pos: int) bool^ {
    let b = scan.byte(s, pos)^
    return scan.digit(b) or (b == 46 and scan.digit(scan.byte(s, pos + 1)^))
}

fn digit_end(s, start: int) int^ {
    var pos = start
    while (scan.digit(scan.byte(s, pos)^)) { pos = pos + 1 }
    return pos
}

// The caller skips trivia and passes the cursor separately from diagnostics.
fn id(s, st, pos: int) map^ {
    let b = scan.byte(s, pos)^
    if (b == 34) { return quoted_id(s, st, pos)^ }
    if (b == 60) { return html_id(s, st, pos)^ }
    if (numeric_start(s, pos)^ or (b == 45 and numeric_start(s, pos + 1)^)) {
        let integer_stop = digit_end(s, if (b == 45) pos + 1 else pos)^
        let stop = if (scan.byte(s, integer_stop)^ == 46) digit_end(s, integer_stop + 1)^ else integer_stop
        return token(s, st, pos, stop, "numeric", scan.text(s, pos, stop)^)^
    }
    if (id_start(b)) {
        var stop = pos + 1
        while (id_byte(scan.byte(s, stop)^)) { stop = stop + 1 }
        return token(s, st, pos, stop, "bare", scan.text(s, pos, stop)^)^
    }
    return {parsed_state:st, next:pos, value:null, start:pos}
}

fn port(s, st, pos: int) map^ {
    if (scan.byte(s, pos)^ != 58) { return {parsed_state:st, next:pos, attrs:{}} }
    let name = id(s, st, skip(s, pos + 1)^)^
    if (name.value == null) {
        return {parsed_state:scan.issue(s, name.parsed_state, name.next, "dot.port", "Expected port ID after ':'")^,
            next:name.next, attrs:{}}
    }
    let after = skip(s, name.next)^
    if (scan.byte(s, after)^ != 58) { return {parsed_state:name.parsed_state, next:after, attrs:{port:name.value}} }
    let compass = id(s, name.parsed_state, skip(s, after + 1)^)^
    if (compass.value != null) {
        return {parsed_state:compass.parsed_state, next:compass.next, attrs:{port:name.value, compass:compass.value}}
    }
    return {parsed_state:scan.issue(s, compass.parsed_state, compass.next, "dot.compass", "Expected compass point after ':'")^,
        next:compass.next, attrs:{port:name.value}}
}

fn node_endpoint(s, name, attrs, stop, operator) element^ =>
    <'dot-endpoint' kind:"node", id:name.value, 'source-kind':name.kind, *:attrs,
        *:scan.span(s, name.start, stop)^, *:if (operator == null) {} else {operator:operator}>

fn group_endpoint(s, value, start, stop, operator) element^ =>
    <'dot-endpoint' kind:"subgraph", id:value.id, *:scan.span(s, start, stop)^,
        *:if (operator == null) {} else {operator:operator}, value>

fn edge_operator(s, pos: int) string?^ =>
    if (scan.byte(s, pos)^ != 45) null else {
        let next = scan.byte(s, pos + 1)^;
        if (next == 62) "->" else if (next == 45) "--" else null
    }

fn recover(s, start: int, close_pos: int = 125) int^ {
    var pos = start
    while ((pos < s.length)^) {
        let b = scan.byte(s, pos)^
        if (b == 59 or b == close_pos) { break }
        pos = pos + 1
    }
    return pos
}

fn read_property(s, st, start: int) map^ {
    let name = id(s, st, start)^
    if (name.value == null) {
        return {parsed_state:scan.issue(s, name.parsed_state, start, "dot.attribute-name", "Expected DOT attribute name")^,
            next:name.next, value:null}
    }
    let equal = skip(s, name.next)^
    if (scan.byte(s, equal)^ != 61) {
        return {parsed_state:scan.issue(s, name.parsed_state, equal, "dot.attribute-equals",
            "Expected '=' after DOT attribute name")^, next:equal, value:null}
    }
    let value = id(s, name.parsed_state, skip(s, equal + 1)^)^
    if (value.value == null) {
        return {parsed_state:scan.issue(s, value.parsed_state, value.next, "dot.attribute-value",
            "Expected DOT attribute value")^, next:value.next, value:null}
    }
    return {parsed_state:value.parsed_state, next:value.next,
        value:<property name:name.value, value:value.value, 'name-source-kind':name.kind,
            'value-source-kind':value.kind, *:scan.span(s, start, value.next)^>}
}

fn attributes(s, initial, start: int) map^ {
    var st = initial
    var pos = start
    var lists = null
    var list_index = 0
    while (true) {
        let list_start = pos
        if (scan.byte(s, list_start)^ != 91) { break }
        pos = list_start + 1
        var properties = null
        while ((pos < s.length)^) {
            pos = skip(s, pos)^
            if (scan.byte(s, pos)^ == 93) {
                pos = pos + 1
                break
            }
            let parsed = read_property(s, st, pos)^
            st = parsed.parsed_state
            pos = parsed.next
            if (parsed.value == null) {
                let stop = skip(s, recover(s, pos, 93)^)^
                pos = if (scan.byte(s, stop)^ == 93) stop + 1 else stop
                break
            }
            properties = scan.add(properties, parsed.value)
            let stop = skip(s, pos)^
            let sep = scan.byte(s, stop)^
            pos = if (sep == 44 or sep == 59) stop + 1 else stop
        }
        let props = scan.finish(properties)^
        lists = scan.add(lists, <properties namespace:"graphviz", 'source-list-index':list_index,
            *:scan.span(s, list_start, pos)^, *props>)
        list_index = list_index + 1
        pos = skip(s, pos)^
    }
    return {parsed_state:st, next:pos, children:scan.finish(lists)^}
}

fn group(s, initial, start: int, depth: int) map^ {
    let named = at_keyword(s, start, "subgraph")^
    let pos = if (named) skip(s, start + 8)^ else start
    let name = if (named and scan.byte(s, pos)^ != 123) id(s, initial, pos)^
        else {parsed_state:initial, next:pos, value:null}
    let open_pos = skip(s, name.next)^
    if (scan.byte(s, open_pos)^ != 123) {
        return {parsed_state:scan.issue(s, name.parsed_state, open_pos, "dot.subgraph-open",
            "Expected '{' to start DOT subgraph")^, next:open_pos, value:null}
    }
    var st = name.parsed_state
    let stable_id = if (name.value == null) "dot-anonymous-" ++ string(st.anonymous_index) else name.value
    if (name.value == null) { st = {*:st, anonymous_index:st.anonymous_index + 1} }
    var children = []
    var stop = open_pos + 1
    if (depth < max_depth) {
        let parsed = statements(s, st, stop, depth + 1)^
        st = parsed.parsed_state
        stop = parsed.next
        children = parsed.children
    } else {
        st = scan.issue(s, st, start, "dot.depth-limit", "DOT subgraph nesting limit exceeded")^
        stop = recover(s, stop)^
    }
    let close_pos = skip(s, stop)^
    if (scan.byte(s, close_pos)^ == 125) { stop = close_pos + 1 }
    else {
        st = scan.issue(s, st, close_pos, "dot.subgraph-close", "Expected '}' to close DOT subgraph")^
        stop = close_pos
    }
    return {parsed_state:st, next:stop, value:<subgraph id:stable_id, role:"scope",
        'source-kind':name.kind or "anonymous", *:scan.span(s, start, stop)^, *children>}
}

fn next_endpoint(s, st, pos: int, depth: int, operator: string) map^ {
    if (scan.byte(s, pos)^ == 123 or at_keyword(s, pos, "subgraph")^) {
        let parsed = group(s, st, pos, depth)^;
        return {parsed_state:parsed.parsed_state, next:parsed.next,
            value:if (parsed.value == null) null else group_endpoint(s, parsed.value, pos, parsed.next, operator)^}
    }
    let name = id(s, st, pos)^
    if (name.value == null) {
        return {parsed_state:scan.issue(s, name.parsed_state, pos, "dot.endpoint", "Expected node or subgraph edge endpoint")^,
            next:name.next, value:null}
    }
    let parsed = port(s, name.parsed_state, skip(s, name.next)^)^;
    {parsed_state:parsed.parsed_state, next:parsed.next,
        value:node_endpoint(s, name, parsed.attrs, parsed.next, operator)^}
}

fn edge(s, initial, endpoint, start: int, first: int, depth: int) map^ {
    let edge_id = "dot-stmt-" ++ string(initial.statement_index)
    var st = {*:initial, statement_index:initial.statement_index + 1}
    var pos = first
    var endpoints = scan.add(null, endpoint)
    while (true) {
        let op = edge_operator(s, pos)^
        if (op == null) { break }
        let parsed = next_endpoint(s, st, skip(s, pos + 2)^, depth, op)^
        st = parsed.parsed_state
        pos = skip(s, parsed.next)^
        if (parsed.value == null) { break }
        endpoints = scan.add(endpoints, parsed.value)
        if ((endpoints.count > max_chain)^) {
            st = scan.issue(s, st, start, "dot.chain-limit", "DOT edge chain limit exceeded")^
            break
        }
    }
    let attrs = attributes(s, st, pos)^
    let children = [*scan.finish(endpoints)^, *attrs.children];
    {parsed_state:attrs.parsed_state, next:attrs.next, value:<'dot-edge-statement' id:edge_id,
        *:scan.span(s, start, attrs.next)^, *children>}
}

fn statement(s, st, start: int, depth: int) map^ {
    if (scan.byte(s, start)^ == 123 or at_keyword(s, start, "subgraph")^) {
        let parsed = group(s, st, start, depth)^
        if (parsed.value == null) { return parsed }
        let pos = skip(s, parsed.next)^
        if (edge_operator(s, pos)^ == null) { return {parsed_state:parsed.parsed_state, next:pos, value:parsed.value} }
        return edge(s, parsed.parsed_state, group_endpoint(s, parsed.value, start, pos, null)^, start, pos, depth)^
    }
    let name = id(s, st, start)^
    if (name.value == null) {
        let pos = recover(s, name.next)^
        let failed = scan.issue(s, name.parsed_state, start, "dot.statement", "Expected DOT statement")^;
        return {parsed_state:failed, next:pos, value:null}
    }
    let pos = skip(s, name.next)^
    let b = scan.byte(s, pos)^
    let target = if (name.kind == "bare") lower(name.value) else null
    if (b == 91 and target in ["graph", "node", "edge"]) {
        let attrs = attributes(s, name.parsed_state, pos)^
        let children = attrs.children;
        return {parsed_state:attrs.parsed_state, next:attrs.next, value:<'dot-attr-statement' 'target-kind':target,
            *:scan.span(s, start, attrs.next)^, *children>}
    }
    if (b == 61) {
        let parsed = id(s, name.parsed_state, skip(s, pos + 1)^)^
        let parsed_state = if (parsed.value != null) parsed.parsed_state else scan.issue(s, parsed.parsed_state,
            parsed.next, "dot.assignment-value", "Expected DOT assignment value")^
        let rhs = if (parsed.value == null) {} else {value:parsed.value, 'value-source-kind':parsed.kind};
        return {parsed_state:parsed_state, next:parsed.next, value:<'dot-assignment' name:name.value, 'name-source-kind':name.kind,
            *:rhs, *:scan.span(s, start, parsed.next)^>}
    }
    let parsed = port(s, name.parsed_state, pos)^
    let after = skip(s, parsed.next)^
    if (edge_operator(s, after)^ != null) {
        return edge(s, parsed.parsed_state, node_endpoint(s, name, parsed.attrs, parsed.next, null)^, start, after, depth)^
    }
    let attrs = attributes(s, parsed.parsed_state, after)^
    let children = attrs.children;
    {parsed_state:attrs.parsed_state, next:attrs.next, value:<node id:name.value, 'source-kind':name.kind, *:parsed.attrs,
        *:scan.span(s, start, attrs.next)^, *children>}
}

fn statements(s, initial, start: int, depth: int) map^ {
    var st = initial
    var pos = start
    var children = null
    while ((pos < s.length)^) {
        pos = skip(s, pos)^
        while (scan.byte(s, pos)^ == 59) { pos = skip(s, pos + 1)^ }
        let b = scan.byte(s, pos)^
        if (b == -1 or b == 125) { break }
        let parsed = statement(s, st, pos, depth)^
        if (parsed.value != null) { children = scan.add(children, parsed.value) }
        st = parsed.parsed_state
        let next_pos = skip(s, parsed.next)^
        let stop = if (scan.byte(s, next_pos)^ == 59) next_pos + 1 else next_pos
        if (pos != stop) { pos = stop }
        else {
            st = scan.issue(s, st, pos, "dot.no-progress", "Could not advance past DOT statement")^
            pos = pos + 1
        }
        if (((st.errors.count or 0) >= 100)^) { break }
    }
    return {parsed_state:st, next:pos, children:scan.finish(children)^}
}

pub fn read(bytes: binary?) map^ {
    let s = scan.source(bytes)^
    if (s.length == 0) { return {root:null, errors:[]} }
    let start = skip(s, 0)^
    let strict = at_keyword(s, start, "strict")^
    let pos = if (strict) skip(s, start + 6)^ else start
    let directed = not at_keyword(s, pos, "graph")^
    let kind_end = if (directed) (if (at_keyword(s, pos, "digraph")^) pos + 7 else pos)
        else pos + 5
    let name_start = skip(s, kind_end)^
    var st = {statement_index:0, anonymous_index:0, errors:null}
    if (kind_end == pos) {
        st = scan.issue(s, st, pos, "dot.graph-kind", "Expected 'graph' or 'digraph'")^
    }
    let name = if (scan.byte(s, name_start)^ == 123) {parsed_state:st, next:name_start, value:null}
        else id(s, st, name_start)^
    let open_pos = skip(s, name.next)^
    st = name.parsed_state
    var stop = open_pos
    var children = []
    if (scan.byte(s, open_pos)^ == 123) {
        let parsed = statements(s, st, open_pos + 1, 0)^
        st = parsed.parsed_state
        stop = parsed.next
        children = parsed.children
    } else { st = scan.issue(s, st, open_pos, "dot.graph-open", "Expected '{' to start DOT graph")^ }
    let close_pos = skip(s, stop)^
    if (scan.byte(s, close_pos)^ == 125) { stop = close_pos + 1 }
    else {
        st = scan.issue(s, st, close_pos, "dot.graph-close", "Expected '}' to close DOT graph")^
        stop = close_pos
    }
    let diagnostics = scan.finish(st.errors)^
    let entries = if (len(diagnostics) == 0) children else [*children, <diagnostics *diagnostics>]
    let name_attrs = if (name.value == null) {} else {id:name.value, 'source-kind':name.kind};
    {root:<graph type:(if (directed) "directed" else "undirected"), layout:"dot", flavor:"dot",
         version:"1", 'ir-stage':"source", directed:directed, strict:strict, *:name_attrs,
         *:scan.span(s, start, stop)^, *entries>, errors:[]}
}
