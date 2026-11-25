import scan: lambda.io.source_scan
import mm: .common

let root_scope = ""
let visibility = {["+"]:"public", ["-"]:"private", ["#"]:"protected", ["~"]:"package"}

fn emit(st, entry, scope: string) map =>
    {*:st, events:{*:st.events, [scope]:scan.add(st.events[scope], entry)}}

fn display(value: string) string^ =>
    join([for (i,ch in value) {
        let following = value[i + 1];
        if (i == 0 or ch != "~") ch
        else if (following == null or following in ["~"," ","\t","\r","\n",";",",",")"]) ">" else "<"
    }], "")

fn read_ref(s, start: int) map^ {
    let id = mm.identifier(s, start)^
    if (id.value == null or scan.byte(s, id.next)^ != 126) {
        return {id:id.value, label:id.value, generic:null, next:id.next}
    }
    var stop = id.next
    while (true) {
        let b = scan.byte(s, stop)^
        if (b in [-1,91,123,58,59,44] or mm.ascii_space(b)) { break }
        stop = stop + 1
    }
    let generic = scan.text(s, id.next, stop)^;
    {id:id.value, label:display(id.value ++ generic)^, generic:generic, next:stop}
}

fn put_node(st, ref, scope: string, authored: bool, label, label_format) map^ {
    let local = st.scope_ids[scope] or {}
    let index = local[ref.id] or st.global_ids[ref.id]
    if (index != null and not authored) {
        if (ref.generic == null) { return {parsed_state:st, index:index} }
        let old = st.nodes[index]
        let attrs = {*:old.attrs, generic:ref.generic, label:ref.label};
        return {parsed_state:{*:st, nodes:{*:st.nodes, [index]:{*:old, attrs:attrs}}}, index:index}
    }
    let key = string(st.next_node)
    let attrs = {id:ref.id, label:label, shape:"box",
        *:if (label_format == null) {} else {'label-format':label_format},
        'mermaid-family':"class", *:if (ref.generic == null) {} else {generic:ref.generic}}
    let updated = emit(st, {node:key}, scope)
    let scope_ids = {*:st.scope_ids, [scope]:{*:local, [ref.id]:local[ref.id] or key}}
    let global_ids = {*:st.global_ids, [ref.id]:st.global_ids[ref.id] or key};
    {parsed_state:{*:updated, scope_ids:scope_ids, global_ids:global_ids,
        nodes:{*:st.nodes, [key]:{attrs:attrs, members:null}}, next_node:st.next_node + 1}, index:key}
}

fn attach_member(st, index: string, value) map {
    if (value == null) { return st }
    let node = st.nodes[index]
    return {*:st, nodes:{*:st.nodes, [index]:{*:node, members:scan.add(node.members, value)}}}
}

fn span_node(s, st, index: string, start: int, stop: int) map^ {
    let node = st.nodes[index]
    if (node.attrs["source-start"] != null) { return st }
    let attrs = {*:node.attrs, *:scan.span(s, start, stop)^};
    {*:st, nodes:{*:st.nodes, [index]:{*:node, attrs:attrs}}}
}

fn annotation(s, start: int) map?^ {
    if (not scan.starts(s, start, binary("<<")^)^) { return null }
    var stop = start + 2
    let end_tag = binary(">>")
    while (not (scan.byte(s, stop)^ in [-1,10]) and not scan.starts(s, stop, end_tag)^) {
        stop = stop + 1
    }
    if (scan.starts(s, stop, end_tag)^) { stop = stop + 2 }
    return {value:scan.text(s, start, stop)^, next:stop}
}

fn stereotype(s, value: string, start: int, stop: int) element^ =>
    <'class-member' kind:"stereotype", value:value, display:value, *:scan.span(s, start, stop)^>

fn class_member(s, source_start: int, initial: int) map^ {
    let start = scan.inline_space(s, initial)^
    let stop = mm.raw_until(s, start, [-1,10,125])^
    let raw = mm.strip_semicolons(s, start, stop)^
    if (raw == "") { return {next:stop, value:null} }
    let kind = if (starts_with(raw, "<<")) "stereotype" else if (contains(raw, "(")) "method" else "field"
    let end_char = raw[len(raw) - 1]
    let classifier = if (end_char == "$") "static" else if (kind == "method" and end_char == "*") "abstract" else null
    let visible = visibility[raw[0]]
    let value = if (classifier == null) raw else slice(raw, 0, len(raw) - 1) or "";
    {next:stop, value:<'class-member' kind:kind, value:raw, display:display(value)^,
        *:if (visible == null) {} else {visibility:visible},
        *:if (classifier == null) {} else {classifier:classifier},
        *:scan.span(s, source_start, stop)^>}
}

fn note_text(s, initial: int) map^ {
    let start = scan.inline_space(s, initial)^
    let b = scan.byte(s, start)^
    if (b in [34,39]) { return mm.delimited(s, start + 1, binary(chr(b))^, 10, true)^ }
    let stop = scan.line_end(s, start)^;
    {value:mm.strip_semicolons(s, start, stop)^, next:stop}
}

fn emit_note(s, st, owner_kind: string, owner_id: string, start: int, text) map^ =>
    emit(st, <annotation 'owner-kind':owner_kind, 'owner-id':owner_id, kind:"note-right",
        label:text.value, 'label-format':"text", *:scan.span(s, start, text.next)^>, root_scope)

fn read_note(s, st, start: int, initial: int, scope: string) map^ {
    let pos = scan.inline_space(s, initial)^
    let of_class = mm.keyword(s, pos, "for")^
    let target = if (of_class) read_ref(s, pos + 3)^ else null
    if (of_class and target.id == null) {
        return mm.fail(s, st, start, target.next, "mermaid.class.note-target", "Expected a class identifier after 'note for'")^
    }
    let referenced = if (of_class) put_node(st, target, scope, false, target.label, null)^.parsed_state else st
    let text = note_text(s, if (of_class) target.next else pos)^
    let result = emit_note(s, referenced, if (of_class) "node" else "graph",
        if (of_class) target.id else "__graph__", start, text)^
    return scan.set_cursor(result, scan.line_end(s, text.next)^)
}

fn body(s, initial, start: int, ref, index: string) map^ {
    var pos = start
    var st = initial
    while ((pos < s.length)^) {
        let first = mm.skip(s, pos)^
        if (scan.byte(s, first)^ == 125) { return scan.set_cursor(st, first + 1) }
        if (mm.keyword(s, first, "note")^) {
            let text = note_text(s, first + 4)^
            st = emit_note(s, st, "node", ref.id, first, text)^
            pos = text.next
        } else {
            let member = class_member(s, first, first)^
            st = attach_member(st, index, member.value)
            pos = member.next
        }
        if (scan.byte(s, pos)^ == 10) { pos = pos + 1 }
    }
    return scan.set_cursor(st, pos)
}

fn merge_events(st, scope: string) map^ {
    let entries = scan.finish(st.children)^
    return reduce([{*:st, children:null}, *entries], (current, item) map^ => emit(current, item, scope)^)^
}

fn declaration(s, initial, start: int, after: int, scope: string) map^ {
    var pos = after
    var st = initial
    while ((pos < s.length)^) {
        let ref = read_ref(s, pos)^
        if (ref.id == null) {
            return mm.fail(s, st, start, ref.next, "mermaid.class.missing-id", "Expected class identifier after 'class'")^
        }
        let shape_at = scan.inline_space(s, ref.next)^
        let shape = if (scan.byte(s, shape_at)^ == 91) mm.node_shape(s, shape_at)^ else null
        let placed = put_node(st, ref, scope, true,
            if (shape == null) ref.label else shape.label, if (shape == null) "text" else shape.format)^
        let annotation_at = scan.inline_space(s, if (shape == null) shape_at else shape.next)^
        let marking = annotation(s, annotation_at)^
        let marked = if (marking == null) placed.parsed_state
            else attach_member(placed.parsed_state, placed.index, stereotype(s, marking.value, start, marking.next)^)
        let assignment = mm.class_suffix(s, marked, ref.id, if (marking == null) annotation_at else marking.next)^
        st = merge_events(assignment, scope)^
        let block_at = scan.inline_space(s, st.pos)^
        if (scan.byte(s, block_at)^ == 123) {
            let parsed = body(s, st, block_at + 1, ref, placed.index)^
            return span_node(s, parsed, placed.index, start, parsed.pos)^
        }
        st = span_node(s, st, placed.index, start, block_at)^
        if (scan.byte(s, block_at)^ != 44) { return scan.set_cursor(st, scan.line_end(s, block_at)^) }
        pos = block_at + 1
    }
    return scan.set_cursor(st, pos)
}

fn separate_stereotype(s, st, start: int, scope: string) map^ {
    let marking = annotation(s, start)^
    let ref = read_ref(s, marking.next)^
    if (ref.id == null) {
        return mm.fail(s, st, start, ref.next, "mermaid.class.missing-id", "Expected class identifier after stereotype")^
    }
    let placed = put_node(st, ref, scope, false, ref.label, null)^
    let marked = attach_member(placed.parsed_state, placed.index, stereotype(s, marking.value, start, ref.next)^)
    let located = span_node(s, marked, placed.index, start, ref.next)^
    return scan.set_cursor(located, scan.line_end(s, ref.next)^)
}

fn relation_marker(s, pos: int, source: bool) map^ {
    let enclosed = if (source) binary("<|")^ else binary("|>")^
    if (scan.starts(s, pos, enclosed)^) { return {kind:"empty", next:pos + 2} }
    if (scan.starts(s, pos, binary("()")^)^) { return {kind:"circle", next:pos + 2} }
    let b = scan.byte(s, pos)^
    let kind = if (b == 42) "diamond" else if (b == 111) "odiamond"
        else if ((source and b == 60) or (not source and b == 62)) "normal" else "none";
    {kind:kind, next:if (kind == "none") pos else pos + 1}
}

fn quoted_cardinality(s, start: int) map^ =>
    if (scan.byte(s, start)^ == 34) mm.delimited(s, start + 1, binary("\"")^)^
    else {value:null, next:start}

fn relation(s, initial, start: int, scope: string) map^ {
    let from = read_ref(s, start)^
    if (from.id == null) {
        return mm.fail(s, initial, start, from.next, "mermaid.class.statement",
            "Expected class declaration or relationship")^
    }
    let pos = scan.inline_space(s, from.next)^
    if (scan.byte(s, pos)^ == 58) {
        let placed = put_node(initial, from, scope, false, from.label, null)^
        let parsed = class_member(s, start, pos + 1)^
        let member = attach_member(placed.parsed_state, placed.index, parsed.value)
        let spanned = span_node(s, member, placed.index, start, parsed.next)^
        return scan.set_cursor(spanned, parsed.next)
    }
    let from_card = quoted_cardinality(s, pos)^
    let relation_at = scan.inline_space(s, from_card.next)^
    let tail = relation_marker(s, relation_at, true)^
    let connector = scan.text(s, tail.next, min(tail.next + 2, s.length))^
    if (not (connector in ["--",".."])) {
        return mm.fail(s, initial, start, tail.next, "mermaid.class.relationship",
            "Expected a supported Mermaid class relationship after '" ++ from.id ++ "'")^
    }
    let head = relation_marker(s, tail.next + 2, false)^
    let spelling = scan.text(s, relation_at, head.next)^
    let to_card_at = scan.inline_space(s, head.next)^
    let to_card = quoted_cardinality(s, to_card_at)^
    let to = read_ref(s, to_card.next)^
    if (to.id == null) {
        return mm.fail(s, initial, start, to.next, "mermaid.class.missing-endpoint",
            "Expected class identifier after relationship '" ++ spelling ++ "'")^
    }
    let colon = scan.inline_space(s, to.next)^
    let has_label = scan.byte(s, colon)^ == 58
    let stop = if (has_label) scan.line_end(s, colon + 1)^ else colon
    let label = if (has_label) mm.normalize_label(mm.strip_semicolons(s, colon + 1, stop)^)^ else null
    let first = put_node(initial, from, scope, false, from.label, null)^
    let second = put_node(first.parsed_state, to, scope, false, to.label, null)^
    let value = <edge from:from.id, to:to.id, *:if (label == null) {} else {label:label.value},
        style:(if (connector == "--") "solid" else "dotted"),
        'arrow-start':(if (tail.kind == "none") "false" else "true"),
        'arrow-end':(if (head.kind == "none") "false" else "true"), relation:spelling,
        'arrow-tail':tail.kind, 'arrow-head':head.kind,
        *:if (label == null) {} else {'label-format':label.format},
        *:if (from_card.value == null) {} else {'from-cardinality':from_card.value},
        *:if (to_card.value == null) {} else {'to-cardinality':to_card.value},
        *:scan.span(s, start, stop)^>
    return scan.set_cursor(emit(second.parsed_state, value, scope), stop)
}

fn scope_path(s, initial, start: int, parent: string) map^ {
    let first = scan.inline_space(s, start)^
    var pos = first
    while (true) {
        let b = scan.byte(s, pos)^
        if (b in [-1,91,123,59] or mm.ascii_space(b)) { break }
        pos = pos + 1
    }
    if (pos == first) { return {parsed_state:initial, path:null, next:pos} }
    let raw = scan.text(s, first, pos)^
    let segments = split(raw, ".")
    var scope = parent
    var st = initial
    var idx = 0
    while (idx < len(segments)) {
        let segment = segments[idx]
        let path = if (scope == root_scope) segment else scope ++ "." ++ segment
        if ((st.namespaces[path] == null)^) {
            st = emit(st, {namespace:path}, scope)
            st = {*:st, namespaces:{*:st.namespaces, [path]:{id:path, label:segment,
                namespace:"true", 'mermaid-family':"class"}}}
        }
        scope = path
        idx = idx + 1
    }
    return {parsed_state:st, path:scope, next:pos}
}

fn read_namespace(s, initial, start: int, after: int, scope: string, depth: int) map^ {
    let info = scope_path(s, initial, after, scope)^
    if (info.path == null) {
        return mm.fail(s, initial, start, info.next, "mermaid.class.namespace-id", "Expected a namespace identifier")^
    }
    let pos = scan.inline_space(s, info.next)^
    let label = if (scan.byte(s, pos)^ == 91) mm.node_shape(s, pos)^ else null
    let block_at = scan.inline_space(s, if (label == null) pos else label.next)^
    let st = info.parsed_state
    let attrs = st.namespaces[info.path]
    let labeled = if (label == null) st else {*:st, namespaces:{*:st.namespaces,
        [info.path]:{*:attrs, label:label.label}}}
    if (scan.byte(s, block_at)^ != 123) {
        return mm.fail(s, labeled, start, block_at, "mermaid.class.namespace-open", "Expected '{' after namespace identifier")^
    }
    if (depth >= 256) {
        let limits = scan.issue(s, labeled, start, "mermaid.class.depth-limit", "Mermaid namespace nesting limit exceeded")^
        return scan.set_cursor(limits, scan.brace_end(s, block_at + 1)^)
    }
    let parsed = statements(s, scan.set_cursor(labeled, block_at + 1), info.path, depth + 1)^
    let namespace_attrs = parsed.namespaces[info.path]
    if (namespace_attrs["source-start"] != null) { return parsed }
    return {*:parsed, namespaces:{*:parsed.namespaces,
        [info.path]:{*:namespace_attrs, *:scan.span(s, start, parsed.pos)^}}}
}

fn class_statement(s, st, start: int, scope: string, depth: int) map^ {
    let head = mm.keyword_at(s, start, ["direction","namespace","class","note","accTitle","accDescr",
        "classDef","cssClass","style","click","link","callback"])^
    let after = start + len(head)
    if (head == "namespace") { return read_namespace(s, st, start, after, scope, depth)^ }
    if (head == "class") { return declaration(s, st, start, after, scope)^ }
    if (head == "note") { return read_note(s, st, start, after, scope)^ }
    if (head in ["click","link","callback"]) {
        let action = if (head == "click") null else head
        return merge_events(mm.interaction(s, st, start, after, action)^, scope)^
    }
    if (head == "cssClass") { return merge_events(mm.class_assignment(s, st, start, after, true)^, scope)^ }
    if (head != null) {
        let reader = mm.family_directive(s, st, start, head, "class", "class")^
        if (reader != null) { return merge_events(reader, if (head in ["accTitle","accDescr"]) root_scope else scope)^ }
    }
    if (scan.starts(s, start, binary("<<")^)^) { return separate_stereotype(s, st, start, scope)^ }
    return relation(s, st, start, scope)^
}

fn statements(s, initial, scope: string, depth: int) map^ {
    var st = initial
    while ((st.pos < s.length)^) {
        let pos = mm.skip(s, st.pos)^
        st = scan.set_cursor(st, pos)
        if (scan.byte(s, pos)^ == -1) { break }
        if (depth > 0 and scan.byte(s, pos)^ == 125) { return scan.set_cursor(st, pos + 1) }
        let parsed = class_statement(s, st, pos, scope, depth)^
        let stop = parsed.pos
        st = scan.set_cursor(parsed, if (scan.byte(s, stop)^ == 10) stop + 1
            else if (stop == pos) mm.next_line(s, pos)^ else stop)
    }
    return if (depth == 0) st else scan.issue(s, st, st.pos, "mermaid.class.namespace-close",
        "Expected '}' to close namespace")^
}

fn scope_children(st, scope: string, meta: array) array^ =>
    [for (item in scan.finish(st.events[scope])^) {
        if (item is element) item
        else if (item.node != null) {
            let spec = st.nodes[item.node]
            let members = scan.finish(spec.members)^;
            <node *:spec.attrs, *members>
        } else if (item.namespace != null) {
            let children = scope_children(st, item.namespace, meta)^;
            <subgraph *:st.namespaces[item.namespace], *children>
        } else <meta *meta>
    }]

pub fn read(s, initial) map^ {
    let st = statements(s, {*:initial, events:{}, nodes:{}, namespaces:{}, global_ids:{}, scope_ids:{}, next_node:0},
        root_scope, 0)^;
    {*:st, built_children:scope_children(st, root_scope, scan.finish(st.meta)^)^}
}
