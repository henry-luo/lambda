import scan: lambda.io.source_scan

let comments = [binary("//"), binary("#")]
let max_depth = 256
let max_chain = 4096
let max_token = 1024 * 1024

fn skip(s, pos) int^ => scan.trivia(s, pos, comments, true)^
fn id_start(b: int) bool => scan.ascii_alpha(b) or b == 95 or b >= 128
fn id_byte(b: int) bool => id_start(b) or scan.digit(b)

fn at_keyword(s, pos, word: string) bool^ {
    let stop = pos + len(word)
    return stop <= s.length and not id_byte(scan.byte(s, stop)^) and
        lower(scan.text(s, pos, stop)^) == word
}

fn token(s, st, first_at, kind, value) map^ {
    if (st.pos - first_at > max_token) {
        return {parsed_state:scan.issue(s, st, first_at, "dot.token-limit", "DOT ID exceeds the parser token limit")^,
            value:null, kind:kind, start:first_at}
    }
    return {parsed_state:st, value:value, kind:kind, start:first_at}
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
            let failed = scan.issue(s, scan.set_cursor(parsed_state, item.next), start, "dot.unterminated-string",
                "Unterminated quoted DOT ID")^;
            return {parsed_state:failed, value:null, start:start}
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
    return token(s, scan.set_cursor(parsed_state, pos), start, "quoted", join(scan.finish(parts)^, ""))^
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
    let parsed_state = scan.set_cursor(st, pos);
    if (depth > 0) {
        return {parsed_state:scan.issue(s, parsed_state, start, "dot.unterminated-html-id", "Unterminated HTML-like DOT ID")^,
            value:null, start:start}
    }
    return token(s, parsed_state, start, "html", scan.text(s, start, pos)^)^
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

fn id(s, st) map^ {
    let pos = skip(s, st.pos)^
    let b = scan.byte(s, pos)^
    if (b == 34) { return quoted_id(s, st, pos)^ }
    if (b == 60) { return html_id(s, st, pos)^ }
    if (numeric_start(s, pos)^ or (b == 45 and numeric_start(s, pos + 1)^)) {
        let integer_stop = digit_end(s, if (b == 45) pos + 1 else pos)^
        let stop = if (scan.byte(s, integer_stop)^ == 46) digit_end(s, integer_stop + 1)^ else integer_stop
        return token(s, scan.set_cursor(st, stop), pos, "numeric", scan.text(s, pos, stop)^)^
    }
    if (id_start(b)) {
        var stop = pos + 1
        while (id_byte(scan.byte(s, stop)^)) { stop = stop + 1 }
        return token(s, scan.set_cursor(st, stop), pos, "bare", scan.text(s, pos, stop)^)^
    }
    return {parsed_state:scan.set_cursor(st, pos), value:null, start:pos}
}

fn port(s, st) map^ {
    let pos = skip(s, st.pos)^
    if (scan.byte(s, pos)^ != 58) { return {parsed_state:scan.set_cursor(st, pos), attrs:{}} }
    let name = id(s, scan.set_cursor(st, pos + 1))^
    if (name.value == null) {
        return {parsed_state:scan.issue(s, name.parsed_state, name.parsed_state.pos, "dot.port", "Expected port ID after ':'")^, attrs:{}}
    }
    let after = skip(s, name.parsed_state.pos)^
    if (scan.byte(s, after)^ != 58) { return {parsed_state:scan.set_cursor(name.parsed_state, after), attrs:{port:name.value}} }
    let compass = id(s, scan.set_cursor(name.parsed_state, after + 1))^
    if (compass.value != null) { return {parsed_state:compass.parsed_state, attrs:{port:name.value, compass:compass.value}} }
    return {parsed_state:scan.issue(s, compass.parsed_state, compass.parsed_state.pos, "dot.compass", "Expected compass point after ':'")^,
        attrs:{port:name.value}}
}

fn node_endpoint(s, name, attrs, stop, operator) element^ =>
    <'dot-endpoint' kind:"node", id:name.value, 'source-kind':name.kind, *:attrs,
        *:scan.span(s, name.start, stop)^, *:if (operator == null) {} else {operator:operator}>

fn group_endpoint(s, value, start, stop, operator) element^ =>
    <'dot-endpoint' kind:"subgraph", id:value.id, *:scan.span(s, start, stop)^,
        *:if (operator == null) {} else {operator:operator}, value>

fn edge_operator(s, pos: int) string?^ =>
    if (scan.byte(s, pos)^ != 45) null
    else if (scan.byte(s, pos + 1)^ == 62) "->"
    else if (scan.byte(s, pos + 1)^ == 45) "--" else null

fn recover(s, start: int, close_pos: int = 125) int^ {
    var pos = start
    while ((pos < s.length and scan.byte(s, pos)^ != 59 and scan.byte(s, pos)^ != close_pos)^) {
        pos = pos + 1
    }
    return pos
}

fn read_property(s, st, start: int) map^ {
    let name = id(s, st)^
    if (name.value == null) {
        return {parsed_state:scan.issue(s, name.parsed_state, start, "dot.attribute-name", "Expected DOT attribute name")^,
            value:null}
    }
    let equal = skip(s, name.parsed_state.pos)^
    if (scan.byte(s, equal)^ != 61) {
        return {parsed_state:scan.issue(s, scan.set_cursor(name.parsed_state, equal), equal, "dot.attribute-equals",
            "Expected '=' after DOT attribute name")^, value:null}
    }
    let value = id(s, scan.set_cursor(name.parsed_state, equal + 1))^
    if (value.value == null) {
        return {parsed_state:scan.issue(s, value.parsed_state, value.parsed_state.pos, "dot.attribute-value",
            "Expected DOT attribute value")^, value:null}
    }
    return {parsed_state:value.parsed_state,
        value:<property name:name.value, value:value.value, 'name-source-kind':name.kind,
            'value-source-kind':value.kind, *:scan.span(s, start, value.parsed_state.pos)^>}
}

fn attributes(s, initial) map^ {
    var st = initial
    var lists = null
    var list_index = 0
    while (true) {
        let list_start = skip(s, st.pos)^
        st = scan.set_cursor(st, list_start)
        if (scan.byte(s, list_start)^ != 91) { break }
        st = scan.set_cursor(st, list_start + 1)
        var properties = null
        while ((st.pos < s.length)^) {
            let pos = skip(s, st.pos)^
            if (scan.byte(s, pos)^ == 93) {
                st = scan.set_cursor(st, pos + 1)
                break
            }
            let parsed = read_property(s, scan.set_cursor(st, pos), pos)^
            st = parsed.parsed_state
            if (parsed.value == null) {
                let stop = skip(s, recover(s, st.pos, 93)^)^
                st = scan.set_cursor(st, if (scan.byte(s, stop)^ == 93) stop + 1 else stop)
                break
            }
            properties = scan.add(properties, parsed.value)
            let stop = skip(s, st.pos)^
            st = scan.set_cursor(st, if (scan.byte(s, stop)^ in [44, 59]) stop + 1 else stop)
        }
        let props = scan.finish(properties)^
        lists = scan.add(lists, <properties namespace:"graphviz", 'source-list-index':list_index,
            *:scan.span(s, list_start, st.pos)^, *props>)
        list_index = list_index + 1
    }
    return {parsed_state:st, children:scan.finish(lists)^}
}

fn group(s, initial, depth: int) map^ {
    let start = initial.pos
    let named = at_keyword(s, start, "subgraph")^
    let pos = skip(s, if (named) start + 8 else start)^
    let name = if (named and scan.byte(s, pos)^ != 123) id(s, scan.set_cursor(initial, pos))^
        else {parsed_state:scan.set_cursor(initial, pos), value:null}
    let open_pos = skip(s, name.parsed_state.pos)^
    if (scan.byte(s, open_pos)^ != 123) {
        return {parsed_state:scan.issue(s, scan.set_cursor(name.parsed_state, open_pos), open_pos, "dot.subgraph-open",
            "Expected '{' to start DOT subgraph")^, value:null}
    }
    var st = scan.set_cursor(name.parsed_state, open_pos + 1)
    let stable_id = if (name.value == null) "dot-anonymous-" ++ string(st.anonymous_index) else name.value
    if (name.value == null) { st = {*:st, anonymous_index:st.anonymous_index + 1} }
    var children = []
    if (depth < max_depth) {
        let parsed = statements(s, st, depth + 1)^
        st = parsed.parsed_state
        children = parsed.children
    } else {
        st = scan.issue(s, st, start, "dot.depth-limit", "DOT subgraph nesting limit exceeded")^
        st = scan.set_cursor(st, recover(s, st.pos)^)
    }
    let close_pos = skip(s, st.pos)^
    st = if (scan.byte(s, close_pos)^ == 125) scan.set_cursor(st, close_pos + 1) else
        scan.issue(s, scan.set_cursor(st, close_pos), close_pos, "dot.subgraph-close", "Expected '}' to close DOT subgraph")^
    return {parsed_state:st, value:<subgraph id:stable_id, role:"scope",
        'source-kind':name.kind or "anonymous", *:scan.span(s, start, st.pos)^, *children>}
}

fn next_endpoint(s, initial, depth: int, operator: string) map^ {
    let pos = skip(s, initial.pos)^
    let st = scan.set_cursor(initial, pos)
    if (scan.byte(s, pos)^ == 123 or at_keyword(s, pos, "subgraph")^) {
        let parsed = group(s, st, depth)^;
        return {parsed_state:parsed.parsed_state,
            value:if (parsed.value == null) null else group_endpoint(s, parsed.value, pos, parsed.parsed_state.pos, operator)^}
    }
    let name = id(s, st)^
    if (name.value == null) {
        return {parsed_state:scan.issue(s, name.parsed_state, pos, "dot.endpoint", "Expected node or subgraph edge endpoint")^,
            value:null}
    }
    let parsed = port(s, name.parsed_state)^;
    {parsed_state:parsed.parsed_state, value:node_endpoint(s, name, parsed.attrs, parsed.parsed_state.pos, operator)^}
}

fn edge(s, initial, endpoint, start: int, depth: int) map^ {
    let edge_id = "dot-stmt-" ++ string(initial.statement_index)
    var st = {*:initial, statement_index:initial.statement_index + 1}
    var endpoints = scan.add(null, endpoint)
    while (true) {
        let pos = skip(s, st.pos)^
        st = scan.set_cursor(st, pos)
        let op = edge_operator(s, pos)^
        if (op == null) { break }
        let parsed = next_endpoint(s, scan.set_cursor(st, pos + 2), depth, op)^
        st = parsed.parsed_state
        if (parsed.value == null) { break }
        endpoints = scan.add(endpoints, parsed.value)
        if ((endpoints.count > max_chain)^) {
            st = scan.issue(s, st, start, "dot.chain-limit", "DOT edge chain limit exceeded")^
            break
        }
    }
    let attrs = attributes(s, st)^
    let children = [*scan.finish(endpoints)^, *attrs.children];
    {parsed_state:attrs.parsed_state, value:<'dot-edge-statement' id:edge_id,
        *:scan.span(s, start, attrs.parsed_state.pos)^, *children>}
}

fn statement(s, st, depth: int) map^ {
    let start = st.pos
    if (scan.byte(s, start)^ == 123 or at_keyword(s, start, "subgraph")^) {
        let parsed = group(s, st, depth)^
        if (parsed.value == null) { return parsed }
        let pos = skip(s, parsed.parsed_state.pos)^
        let next_state = scan.set_cursor(parsed.parsed_state, pos)
        if (edge_operator(s, pos)^ == null) { return {parsed_state:next_state, value:parsed.value} }
        return edge(s, next_state, group_endpoint(s, parsed.value, start, pos, null)^, start, depth)^
    }
    let name = id(s, st)^
    if (name.value == null) {
        let pos = recover(s, name.parsed_state.pos)^
        let failed = scan.issue(s, name.parsed_state, start, "dot.statement", "Expected DOT statement")^;
        return {parsed_state:scan.set_cursor(failed, pos), value:null}
    }
    let pos = skip(s, name.parsed_state.pos)^
    let head = scan.set_cursor(name.parsed_state, pos)
    let b = scan.byte(s, pos)^
    let target = if (name.kind == "bare") lower(name.value) else null
    if (b == 91 and target in ["graph", "node", "edge"]) {
        let attrs = attributes(s, head)^
        let children = attrs.children;
        return {parsed_state:attrs.parsed_state, value:<'dot-attr-statement' 'target-kind':target,
            *:scan.span(s, start, attrs.parsed_state.pos)^, *children>}
    }
    if (b == 61) {
        let parsed = id(s, scan.set_cursor(head, pos + 1))^
        let parsed_state = if (parsed.value != null) parsed.parsed_state else scan.issue(s, parsed.parsed_state,
            parsed.parsed_state.pos, "dot.assignment-value", "Expected DOT assignment value")^
        let rhs = if (parsed.value == null) {} else {value:parsed.value, 'value-source-kind':parsed.kind};
        return {parsed_state:parsed_state, value:<'dot-assignment' name:name.value, 'name-source-kind':name.kind,
            *:rhs, *:scan.span(s, start, parsed_state.pos)^>}
    }
    let parsed = port(s, head)^
    let after = skip(s, parsed.parsed_state.pos)^
    let next_state = scan.set_cursor(parsed.parsed_state, after)
    if (edge_operator(s, after)^ != null) {
        return edge(s, next_state, node_endpoint(s, name, parsed.attrs, parsed.parsed_state.pos, null)^, start, depth)^
    }
    let attrs = attributes(s, next_state)^
    let children = attrs.children;
    {parsed_state:attrs.parsed_state, value:<node id:name.value, 'source-kind':name.kind, *:parsed.attrs,
        *:scan.span(s, start, attrs.parsed_state.pos)^, *children>}
}

fn statements(s, initial, depth: int) map^ {
    var st = initial
    var children = null
    while ((st.pos < s.length)^) {
        var pos = skip(s, st.pos)^
        while (scan.byte(s, pos)^ == 59) { pos = skip(s, pos + 1)^ }
        st = scan.set_cursor(st, pos)
        if (scan.byte(s, pos)^ in [-1, 125]) { break }
        let parsed = statement(s, st, depth)^
        if (parsed.value != null) { children = scan.add(children, parsed.value) }
        let next_pos = skip(s, parsed.parsed_state.pos)^
        let stop = if (scan.byte(s, next_pos)^ == 59) next_pos + 1 else next_pos
        st = if (pos != stop) scan.set_cursor(parsed.parsed_state, stop) else scan.set_cursor(
            scan.issue(s, parsed.parsed_state, pos, "dot.no-progress", "Could not advance past DOT statement")^, pos + 1)
        if (((st.errors.count or 0) >= 100)^) { break }
    }
    return {parsed_state:st, children:scan.finish(children)^}
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
    var st = {pos:skip(s, kind_end)^, statement_index:0, anonymous_index:0, errors:null}
    if (kind_end == pos) {
        st = scan.issue(s, st, pos, "dot.graph-kind", "Expected 'graph' or 'digraph'")^
    }
    let name = if (scan.byte(s, st.pos)^ == 123) {parsed_state:st, value:null} else id(s, st)^
    let open_pos = skip(s, name.parsed_state.pos)^
    st = scan.set_cursor(name.parsed_state, open_pos)
    var children = []
    if (scan.byte(s, open_pos)^ == 123) {
        let parsed = statements(s, scan.set_cursor(st, open_pos + 1), 0)^
        st = parsed.parsed_state
        children = parsed.children
    } else { st = scan.issue(s, st, open_pos, "dot.graph-open", "Expected '{' to start DOT graph")^ }
    let close_pos = skip(s, st.pos)^
    st = if (scan.byte(s, close_pos)^ == 125) scan.set_cursor(st, close_pos + 1) else
        scan.issue(s, scan.set_cursor(st, close_pos), close_pos, "dot.graph-close", "Expected '}' to close DOT graph")^
    let diagnostics = scan.finish(st.errors)^
    let entries = if (len(diagnostics) == 0) children else [*children, <diagnostics *diagnostics>]
    let name_attrs = if (name.value == null) {} else {id:name.value, 'source-kind':name.kind};
    {root:<graph type:(if (directed) "directed" else "undirected"), layout:"dot", flavor:"dot",
         version:"1", 'ir-stage':"source", directed:directed, strict:strict, *:name_attrs,
         *:scan.span(s, start, st.pos)^, *entries>, errors:[]}
}
