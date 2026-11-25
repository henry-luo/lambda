import scan: lambda.io.source_scan
import mm: .common

let property_open = binary("@{")
let edge_prefixes = [45, 46, 61]
let node_kinds = {rect:"box", diam:"diamond", 'dbl-circ':"doublecircle", cyl:"cylinder"}

fn bare_end(s, pos: int) int^ {
    let initial = scan.byte(s, pos)^
    if (not (scan.ascii_alpha(initial) or initial == 95)) { return pos }
    var stop = pos + 1
    while (true) {
        let b = scan.byte(s, stop)^
        if (scan.ascii_alpha(b) or scan.digit(b) or b in [95,45]) { stop = stop + 1 }
        else { break }
    }
    return stop
}

fn property_value(s, initial: int) map^ {
    let start = scan.inline_space(s, initial)^
    if (scan.byte(s, start)^ == 34) { return mm.delimited(s, start + 1, binary("\"")^, 10)^ }
    var stop = start
    while (true) {
        let b = scan.byte(s, stop)^
        if (b == -1 or mm.ascii_space(b) or b in [44,125]) { break }
        stop = stop + 1
    }
    return {value:scan.text(s, start, stop)^, next:stop}
}

fn properties(s, initial, start: int, edge: bool) map^ {
    var st = initial
    var pos = start + 2
    var entries = null
    while ((pos < s.length)^) {
        pos = scan.inline_space(s, pos)^
        if (scan.byte(s, pos)^ == 125) {
            pos = pos + 1
            break
        }
        let key_end = bare_end(s, pos)^
        if (pos == key_end) {
            st = scan.issue(s, st, pos, st.syntax_code,
                if (edge) "Expected Mermaid edge property name" else "Expected property name in Mermaid node shape",
                "warning")^
            break
        }
        let key = scan.text(s, pos, key_end)^
        pos = scan.inline_space(s, key_end)^
        if (scan.byte(s, pos)^ != 58) {
            st = scan.issue(s, st, pos, st.syntax_code,
                if (edge) "Expected ':' after Mermaid edge property" else "Expected ':' after Mermaid node property",
                "warning")^
            break
        }
        let value = property_value(s, pos + 1)^
        pos = value.next
        if (edge or key in ["label","shape"]) {
            entries = scan.add(entries, {key:key, value:value.value, stop:pos})
        } else {
            st = scan.issue(s, st, pos, st.syntax_code, "Unsupported Mermaid node property '" ++ key ++ "'", "warning")^
        }
        pos = scan.inline_space(s, pos)^
        if (scan.byte(s, pos)^ == 44) { pos = pos + 1 }
    }
    return {parsed_state:scan.set_cursor(st, pos), entries:scan.finish(entries)^}
}

fn node_ref(s, initial, start: int) map^ {
    let first = scan.inline_space(s, start)^
    let id = mm.identifier(s, first, true)^
    if (id.value == null) { return {parsed_state:scan.set_cursor(initial, id.next), id:null} }
    let pos = scan.inline_space(s, id.next)^
    let shape = mm.node_shape(s, pos)^
    let base = shape or {shape:"box",label:id.value,format:"text",next:pos}
    let has_properties = scan.starts(s, base.next, property_open)^
    let attrs = if (has_properties) properties(s, initial, base.next, false)^
        else {parsed_state:scan.set_cursor(initial, base.next), entries:[]}
    let values = reduce([{label:base.label, shape:base.shape}, *attrs.entries], (m, item) => {*:m, [item.key]:item.value})
    let authored = shape != null or has_properties
    let label = if (has_properties) mm.normalize_label(values.label)^ else {value:base.label, format:base.format}
    let kind = node_kinds[values.shape] or values.shape
    let suffix = mm.class_suffix(s, {*:attrs.parsed_state, children:null}, id.value, attrs.parsed_state.pos)^
    let st = {*:suffix, children:attrs.parsed_state.children}
    var output = st
    if (authored or st.ids[id.value] != true) {
        // A bare reference in a subgraph declares membership without
        // resetting a previously authored label or shape.
        let attrs = if (not authored and st.root_ids[id.value] == true) {} else {label:label.value, shape:kind}
        let value = <node id:id.value, *:attrs, *:if (authored) {'label-format':label.format} else {},
            *:scan.span(s, first, suffix.pos)^>
        let entry = mm.append(st, value)
        output = {*:entry, ids:{*:entry.ids, [id.value]:true}}
    }
    let additions = scan.finish(suffix.children)^
    if (len(additions) > 0) { output = mm.append(output, additions[0]) }
    return {parsed_state:scan.set_cursor(output, scan.inline_space(s, suffix.pos)^), id:id.value}
}

fn node_list(s, initial, start: int) map^ {
    let first = node_ref(s, initial, start)^
    if (first.id == null) { return {parsed_state:first.parsed_state, ids:null} }
    var refs = scan.add(null, first.id)
    var st = first.parsed_state
    while (scan.byte(s, st.pos)^ == 38) {
        let parsed = node_ref(s, st, st.pos + 1)^
        st = parsed.parsed_state
        if (parsed.id == null) {
            let failed = scan.issue(s, st, st.pos, st.syntax_code, "Expected node identifier after '&'")^;
            return {parsed_state:failed, ids:null}
        }
        refs = scan.add(refs, parsed.id)
    }
    return {parsed_state:st, ids:scan.finish(refs)^}
}

fn edge_id_end(s, pos: int) int^ {
    let stop = bare_end(s, pos)^
    let after = scan.byte(s, stop + 1)^
    return if (stop > pos and scan.byte(s, stop)^ == 64 and after in [60,111,120,45,61,46]) stop else pos
}

fn edge_start(s, pos: int) bool^ {
    let b = scan.byte(s, pos)^
    return b in [60,45,61] or ((b == 111 or b == 120) and scan.byte(s, pos + 1)^ in edge_prefixes) or
        edge_id_end(s, pos)^ > pos
}

fn inline_label_end(s, start: int, pattern: binary) int?^ {
    var pos = start
    while (not (scan.byte(s, pos)^ in [-1,10])) {
        if (scan.starts(s, pos, pattern)^) { return pos }
        pos = pos + 1
    }
    return null
}

fn marker(b: int) string => if (b in [60,62]) "normal" else if (b == 111) "circle" else if (b == 120) "cross" else "none"

fn edge_operator(s, initial) map^ {
    let start = mm.skip(s, initial.pos)^
    let id_end = edge_id_end(s, start)^
    let source_id = if (id_end == start) null else scan.text(s, start, id_end)^
    var pos = if (source_id == null) start else id_end + 1
    let b = scan.byte(s, pos)^
    let start_marker = if (b == 60 or ((b == 111 or b == 120) and scan.byte(s, pos + 1)^ in edge_prefixes))
        marker(b) else "none"
    if (start_marker != "none") { pos = pos + 1 }
    let first = scan.byte(s, pos)^
    if (not (first in edge_prefixes) or (first == 46 and scan.byte(s, pos + 1)^ != 45)) {
        let where_pos = if (first == 46) pos + 1 else pos
        let message = if (first == 46) "Invalid edge syntax after '.'" else "Invalid edge syntax, expected '-', '=', or '.'"
        let failed = scan.issue(s, scan.set_cursor(initial, where_pos), where_pos, initial.syntax_code, message)^;
        return {parsed_state:failed, op:null}
    }
    var strokes = 0
    var dots = if (first == 46) 1 else 0
    if (first == 46) { pos = pos + 1 }
    let thick = first == 61
    while (true) {
        let current = scan.byte(s, pos)^
        if (thick and current == 61) { strokes = strokes + 1 }
        else if (not thick and current in [45,46]) {
            if (current == 46) { dots = dots + 1 } else { strokes = strokes + 1 }
        } else { break }
        pos = pos + 1
    }
    var end_marker = marker(scan.byte(s, pos)^)
    if (scan.byte(s, pos)^ == 60) { end_marker = "none" }
    if (end_marker != "none") { pos = pos + 1 }
    let style = if (thick) "thick" else if (dots > 0) "dotted" else "solid"
    var label = null
    if (end_marker == "none") {
        pos = scan.inline_space(s, pos)^
        let close = binary(if (thick) "==>" else if (dots > 0) ".->" else "-->")^
        let stop = inline_label_end(s, pos, close)^
        if (stop != null and stop > pos) {
            // Repeated connector strokes before the closing marker belong to the edge.
            var label_stop = int(stop)^
            let stroke = if (thick) 61 else 45
            while (dots == 0 and label_stop > pos and scan.byte(s, label_stop - 1)^ == stroke) {
                label_stop = label_stop - 1
                strokes = strokes + 1
            }
            label = mm.normalize_label(mm.trim_text(s, pos, label_stop)^)^
            pos = stop + len(close)
            end_marker = "normal"
        }
    }
    if (scan.byte(s, pos)^ == 124) {
        let stop = mm.raw_until(s, pos + 1, [-1,124])^
        if (scan.byte(s, stop)^ == 124) {
            label = mm.normalize_label(scan.text(s, pos + 1, stop)^)^
            pos = stop + 1
        } else { pos = stop }
    }
    let min_length = max(1, if (dots > 0) dots else strokes - (if (end_marker == "none") 2 else 1));
    {parsed_state:scan.set_cursor(initial, mm.skip(s, pos)^), op:{source_id:source_id,
        start:start, style:style, marker_start:start_marker, marker_end:end_marker,
        min_length:min_length, label:label}}
}

fn provenance(s, entry, spans) element^ {
    let value = entry.edge
    let info = spans[string(entry.statement)]
    if (info == null) { return value }
    let loc = scan.location(s, entry.statement)^;
    <edge *:map(value), 'source-statement-start':entry.statement, 'source-statement-end':info.stop,
        'source-statement-line':loc.line, 'source-statement-column':loc.column,
        'source-expansion-index':entry.index, 'source-expansion-count':info.count>
}

fn edge_chain(s, initial, from_ids: array, statement: int) map^ {
    var st = initial
    var from = from_ids
    var segment = 0
    var expansion = 0
    while (edge_start(s, st.pos)^) {
        let parsed = edge_operator(s, st)^
        st = parsed.parsed_state
        if (parsed.op == null) { return st }
        let op = parsed.op
        let targets = node_list(s, st, st.pos)^
        st = targets.parsed_state
        if (targets.ids == null) {
            return scan.issue(s, st, st.pos, st.syntax_code, "Expected target node identifier")^
        }
        let to = targets.ids
        let count = len(from) * len(to)
        let span = scan.span(s, op.start, st.pos)^
        let labels = if (op.label == null) {} else {label:op.label.value}
        let label_format = if (op.label == null) {} else {'label-format':op.label.format}
        let units = if (op.min_length > 1) {'min-length':string(op.min_length)} else {}
        let rows = [for (i,a in from, j,b in to) {
            let index = i * len(to) + j
            let id = if (op.source_id == null) null else if (count == 1) op.source_id
                else op.source_id ++ ":" ++ string(index)
            let identity = if (id == null) {} else {'source-id':op.source_id, id:id}
            let value = <edge from:a, to:b, *:labels, style:op.style,
                'arrow-start':(if (op.marker_start == "none") "false" else "true"),
                'arrow-end':(if (op.marker_end == "none") "false" else "true"),
                *:label_format, *:identity, 'arrow-tail':op.marker_start, 'arrow-head':op.marker_end,
                *:units, 'source-segment-index':segment, *:span>;
            {edge:value, statement:statement, index:expansion + index}
        }]
        st = reduce([st, *rows], mm.append)^
        if (op.source_id != null) {
            let by_id = reduce([{*:st.edge_ids, [op.source_id]:true}, *rows], (ids, row) => {*:ids, [row.edge.id]:true})^
            st = {*:st, edge_ids:by_id}
        }
        expansion = expansion + count
        segment = segment + 1
        from = to
        st = scan.set_cursor(st, scan.inline_space(s, st.pos)^)
    }
    return {*:st, spans:{*:st.spans, [string(statement)]:{stop:st.pos,count:expansion}}}
}

fn property_statement(s, initial, start: int, stop: int, target: string) map^ {
    let parsed = properties(s, initial, stop, true)^
    let entries = [for (entry in parsed.entries)
        <'edge-property' target:target, key:entry.key, value:entry.value, *:scan.span(s, start, entry.stop)^>];
    reduce([parsed.parsed_state, *entries], mm.append)^
}

fn children(s, st) array^ {
    let meta = scan.finish(st.meta)^;
    [for (entry in scan.finish(st.children)^)
        if (entry is element) entry else if (entry.edge != null) provenance(s, entry, st.spans)^
        else <meta *meta>]
}

fn subgraph(s, initial, start: int, after: int, depth: int) map^ {
    let pos = mm.skip(s, after)^
    let quoted = scan.byte(s, pos)^ == 34
    let id = if (quoted) mm.unescaped(s, pos + 1, 34, 10)^ else mm.identifier(s, pos, true)^
    let generated_name = "subgraph_" ++ string(id.next)
    let name = id.value or generated_name
    let bracket = scan.inline_space(s, id.next, false)^
    let explicit = scan.byte(s, bracket)^ == 91
    let label = if (explicit)
        mm.unescaped(s, bracket + 1, 93)^
        else if (quoted) {value:name, next:bracket}
        else {
            let line = mm.line_text(s, pos)^
            let comment = index_of(line.value, "%%")
            {value:if (comment == null) line.value else trim(slice(line.value, 0, comment)), next:line.next}
        }
    let body_start = mm.next_line(s, label.next)^
    let root_ids = if (depth == 0) initial.ids else initial.root_ids
    let root_edges = if (depth == 0) initial.edge_ids else initial.root_edges
    if (depth >= 256) { return mm.fail(s, initial, start, body_start, initial.syntax_code,
        "Mermaid subgraph nesting limit exceeded")^ }
    let body = statements(s, {pos:body_start, direction:null, errors:initial.errors, meta:null, children:null,
        ids:{}, edge_ids:{}, root_ids:root_ids, root_edges:root_edges, spans:{}, syntax_code:initial.syntax_code}, depth + 1)^
    let contents = children(s, body)^
    let identity = if (quoted or explicit or label.value == name) name else generated_name;
    let value = <subgraph id:identity, label:label.value, *:if (body.direction == null) {} else {direction:body.direction},
        *:scan.span(s, start, body.pos)^, *contents>
    let st = mm.append(initial, value);
    {*:st, pos:body.pos, ids:{*:st.ids, *:body.ids}, edge_ids:{*:st.edge_ids, *:body.edge_ids}, errors:body.errors}
}

fn flow_statement(s, st, start: int, depth: int) map^ {
    let direct = mm.keyword_at(s, start, ["accTitle","accDescr","direction","subgraph","classDef","class","click","style","linkStyle"])^
    let after = start + len(direct)
    if (direct == "subgraph") { return subgraph(s, st, start, after, depth)^ }
    if (direct == "click") { return mm.interaction(s, st, start, after)^ }
    if (direct == "linkStyle") { return mm.style_assignment(s, st, start, after, true)^ }
    if (direct == "direction" and depth > 0) {
        let dir = mm.direction(s, after)^
        let output = scan.set_cursor(st, dir.next);
        return if (dir.value == null) output else {*:output, direction:dir.value}
    }
    if (direct in ["classDef","class","style"] or (depth == 0 and direct in ["accTitle","accDescr"])) {
        return mm.family_directive(s, st, start, direct, "flowchart", "flowchart")^
    }
    let name_end = bare_end(s, start)^
    if (name_end > start and scan.starts(s, name_end, property_open)^) {
        let target = scan.text(s, start, name_end)^
        let known = if (depth == 0) st.edge_ids[target] else st.root_edges[target]
        if (known == true) { return property_statement(s, st, start, name_end, target)^ }
    }
    let nodes = node_list(s, st, start)^
    if (nodes.ids == null or not edge_start(s, nodes.parsed_state.pos)^) { return nodes.parsed_state }
    return edge_chain(s, nodes.parsed_state, nodes.ids, start)^
}

fn statements(s, initial, depth: int) map^ {
    var st = initial
    while ((st.pos < s.length)^) {
        let pos = mm.skip(s, st.pos)^
        st = scan.set_cursor(st, pos)
        if ((pos >= s.length)^) { break }
        if (depth > 0 and mm.keyword(s, pos, "end")^) { return scan.set_cursor(st, pos + 3) }
        let parsed = flow_statement(s, st, pos, depth)^
        st = if (parsed.pos > pos) parsed else scan.set_cursor(parsed, mm.next_line(s, pos)^)
    }
    return st
}

pub fn read(s, initial) map^ {
    let st = statements(s, {*:initial, spans:{}, root_ids:{}, root_edges:{}}, 0)^;
    {*:st, built_children:children(s, st)^}
}
