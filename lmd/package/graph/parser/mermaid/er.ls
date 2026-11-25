import scan: lambda.io.source_scan
import shared: .common

fn er_entity(s, start: int) map^ {
    let pos = scan.inline_space(s, start)^
    if (scan.byte(s, pos)^ == 34) {
        let raw = shared.delimited(s, pos + 1, binary("\"")^)^;
        return {id:raw.value, label:raw.value, format:"markdown", alias:false, next:raw.next}
    }
    let ref = shared.identifier(s, pos)^
    if (ref.value == null) { return {id:null, next:ref.next} }
    let after = scan.inline_space(s, ref.next)^
    if (scan.byte(s, after)^ == 91) {
        let parsed = shared.node_shape(s, after)^;
        return {id:ref.value, label:parsed.label, format:parsed.format, alias:true, next:parsed.next}
    }
    return {id:ref.value, label:ref.value, format:"text", alias:false, next:after}
}

fn er_node(s, st, ref, declared: bool, start: int, stop: int, children: array = []) map^ {
    if (not declared and not ref.alias and st.ids[ref.id] == true) { return st }
    let authored = if (declared or ref.alias)
        {'label-format':ref.format, 'mermaid-family':"er", *:scan.span(s, start, stop)^}
        else {'mermaid-family':"er"}
    let value = <node id:ref.id, label:(if (declared or ref.alias) ref.label else ref.id),
        shape:"box", *:authored, *children>
    let result = shared.append(st, value);
    {*:result, ids:{*:result.ids, [ref.id]:true}}
}

fn er_word(s, initial: int) map^ {
    let start = scan.inline_space(s, initial)^
    var pos = start
    while (true) {
        let b = scan.byte(s, pos)^
        if (b == -1 or shared.ascii_space(b) or b == 125) { break }
        pos = pos + 1
    }
    return {value:if (pos == start) null else scan.text(s, start, pos)^, next:pos}
}

fn er_attribute(s, st, start: int) map^ {
    let data_type = er_word(s, start)^
    let name = er_word(s, data_type.next)^
    if (data_type.value == null or name.value == null) {
        let failed = shared.fail(s, st, name.next, name.next, "mermaid.er.attribute",
            "Expected an entity attribute type and name")^;
        return {parsed_state:failed, value:null}
    }
    let first = scan.inline_space(s, name.next)^
    let stop = shared.raw_until(s, first, [-1, 34, 10, 125])^
    let key = shared.strip_semicolons(s, first, stop)^
    let comment_at = scan.inline_space(s, stop)^
    let comment = if (scan.byte(s, comment_at)^ == 34) shared.delimited(s, comment_at + 1, binary("\"")^)^
        else {value:null, next:comment_at}
    // The native ER attribute record anchors at the cursor after its final token.
    let value = <'er-attribute' type:data_type.value, name:name.value,
        *:if (key == "") {} else {key:key},
        *:if (comment.value == null) {} else {comment:comment.value},
        *:scan.span(s, comment.next, comment.next)^>;
    {parsed_state:scan.set_cursor(st, scan.line_end(s, comment.next)^), value:value}
}

fn er_attributes(s, initial, pos: int) map^ {
    var st = scan.set_cursor(initial, pos)
    var attrs = null
    while ((st.pos < s.length)^) {
        let first = shared.skip(s, st.pos)^
        let b = scan.byte(s, first)^
        if (b == 125) { return {parsed_state:scan.set_cursor(st, first + 1), values:scan.finish(attrs)^} }
        let parsed = er_attribute(s, st, first)^
        st = parsed.parsed_state
        if (parsed.value != null) { attrs = scan.add(attrs, parsed.value) }
    }
    return {parsed_state:st, values:scan.finish(attrs)^}
}

fn cardinality(a: int, b: int) map? {
    let bar = a == 124 or b == 124
    let zero = a == 111 or b == 111
    let many = a in [123, 125] or b in [123, 125];
    if (a == 124 and b == 124) {value:"exactly-one", marker:"tee tee"}
    else if (zero and bar) {value:"zero-or-one", marker:"odot tee"}
    else if (zero and many) {value:"zero-or-more", marker:"odot crow"}
    else if (many and bar) {value:"one-or-more", marker:"tee crow"} else null
}

fn er_relation(s, pos: int) map?^ {
    let connector = scan.text(s, pos + 2, min(pos + 4, s.length))^
    if (not (connector in ["--",".."])) { return null }
    let from = cardinality(scan.byte(s, pos)^, scan.byte(s, pos + 1)^)
    let to = cardinality(scan.byte(s, pos + 4)^, scan.byte(s, pos + 5)^)
    if (from == null or to == null) { return null }
    return {from:from, to:to, solid:connector == "--", spelling:scan.text(s, pos, pos + 6)^, next:pos + 6}
}

fn er_statement(s, initial, start: int) map^ {
    let from = er_entity(s, start)^
    if (from.id == null) {
        return shared.fail(s, initial, start, from.next, "mermaid.er.entity", "Expected an ER entity name")^
    }
    let st = shared.class_suffix(s, initial, from.id, from.next)^
    let pos = scan.inline_space(s, st.pos)^
    let b = scan.byte(s, pos)^
    if (b == 123) {
        let body = er_attributes(s, st, pos + 1)^
        return er_node(s, body.parsed_state, from, true, start, body.parsed_state.pos, body.values)^
    }
    if (b in [-1, 10, 59]) {
        let declared = er_node(s, st, from, true, start, pos)^
        return scan.set_cursor(declared, scan.line_end(s, pos)^)
    }
    let relation = er_relation(s, pos)^
    if (relation == null) {
        return shared.fail(s, st, start, pos, "mermaid.er.relationship",
            "Expected a symbolic ER relationship after '" ++ from.id ++ "'")^
    }
    let to = er_entity(s, relation.next)^
    if (to.id == null) {
        return shared.fail(s, st, start, to.next, "mermaid.er.missing-endpoint",
            "Expected an entity after relationship '" ++ relation.spelling ++ "'")^
    }
    let assigned = shared.class_suffix(s, st, to.id, to.next)^
    let colon = scan.inline_space(s, assigned.pos)^
    let stop = scan.line_end(s, colon)^
    let has_label = scan.byte(s, colon)^ == 58
    let label = if (has_label) shared.normalize_label(shared.strip_semicolons(s, colon + 1, stop)^)^ else null
    let checked = if (has_label) assigned else scan.issue(s, assigned, start, "mermaid.er.missing-label",
        "Expected ':' and an ER relationship label")^
    let source_node = er_node(s, checked, from, false, start, stop)^
    let endpoints = er_node(s, source_node, to, false, start, stop)^
    let value = <edge from:from.id, to:to.id, *:if (has_label) {label:label.value} else {},
        style:(if (relation.solid) "solid" else "dotted"), 'arrow-start':"true", 'arrow-end':"true",
        relation:relation.spelling, identifying:(if (relation.solid) "true" else "false"),
        'from-cardinality':relation.from.value, 'to-cardinality':relation.to.value,
        'arrow-tail':relation.from.marker, 'arrow-head':relation.to.marker,
        *:if (has_label) {'label-format':label.format} else {}, *:scan.span(s, start, stop)^>
    return scan.set_cursor(shared.append(endpoints, value), stop)
}

pub fn read_line(s, st, start: int) map^ {
    let head = shared.keyword_at(s, start, shared.common_directives)^
    let directive = shared.family_directive(s, st, start, head, "er", "ER")^
    return if (directive == null) er_statement(s, st, start)^ else directive
}
