import scan: lambda.io.source_scan
import shared: .common

fn state_endpoint(s, initial: int, source: bool) map^ {
    let pos = scan.inline_space(s, initial)^
    if (scan.starts(s, pos, binary("[*]")^)^) {
        return {id:if (source) "__state_start" else "__state_end",
            kind:if (source) "start" else "end", next:pos + 3}
    }
    let id = shared.identifier(s, pos)^;
    {id:id.value, kind:"state", next:id.next}
}

fn state_node(st, id: string, label: string, shape: string, kind: string, attrs: map,
              children: array = []) map^ {
    let result = shared.append(st, <node id:id, label:label, shape:shape,
        *:if (attrs["label-format"] == null) {} else {'label-format':attrs["label-format"]},
        'mermaid-family':"state", 'state-kind':kind, *:attrs, *children>);
    {*:result, ids:{*:result.ids, [id]:true}}
}

fn put_state_span(s, st, id: string, start: int, stop: int) map^ {
    if (st.state_spans[id] != null) { return st }
    let span = scan.span(s, start, stop)^;
    {*:st, state_spans:{*:st.state_spans, [id]:span}}
}

fn ensure_state(st, ref) map^ {
    if (st.ids[ref.id] == true) { return st }
    let shape = if (ref.kind == "start") "circle" else if (ref.kind == "end") "doublecircle" else "rounded"
    let label = if (ref.kind == "state") ref.id else "";
    state_node(st, ref.id, label, shape, ref.kind, {})^
}

fn state_declaration(s, initial, start: int, after: int) map^ {
    let pos = scan.inline_space(s, after)^
    let quoted = scan.byte(s, pos)^ == 34
    let label = if (quoted) shared.delimited(s, pos + 1, binary("\"")^)^ else null
    let alias_at = if (quoted) scan.inline_space(s, label.next)^ else pos
    let as_present = quoted and shared.keyword(s, alias_at, "as")^
    let st = if (quoted and not as_present) scan.issue(s, initial, start, "mermaid.state.missing-as",
        "Expected 'as' after a quoted state description")^ else initial
    let id = shared.identifier(s, if (as_present) alias_at + 2 else alias_at)^
    if (id.value == null) {
        return shared.fail(s, st, start, id.next, "mermaid.state.missing-id", "Expected a state identifier")^
    }
    let assigned = shared.class_suffix(s, st, id.value, id.next)^
    let scope_at = scan.inline_space(s, assigned.pos)^
    let annotation = if (scan.starts(s, scope_at, binary("<<")^)^) shared.identifier(s, scope_at + 2)^ else null
    let next_pos = if (annotation == null) scope_at
        else if (scan.starts(s, annotation.next, binary(">>")^)^) annotation.next + 2 else annotation.next
    let kind = if (annotation.value in ["choice","fork","join"]) annotation.value else "state"
    let checked = if (annotation != null and kind == "state") scan.issue(s, assigned, start,
        "mermaid.state.annotation", "Expected state annotation choice, fork, or join")^ else assigned
    let shape = if (kind == "choice") "diamond" else if (kind == "state") "rounded" else "box"
    let body_at = scan.inline_space(s, next_pos)^
    let composite = scan.byte(s, body_at)^ == 123
    let stop = if (composite) scan.brace_end(s, body_at + 1)^ else scan.line_end(s, body_at)^
    let marked = if (composite) scan.issue(s, checked, start, "mermaid.state.composite-unsupported",
        "Composite Mermaid states require the cluster-aware state tranche")^ else checked
    let attrs = {'label-format':if (quoted) "markdown" else "text",
        *:scan.span(s, start, stop)^, *:if (composite) {composite:"true"} else {}}
    let result = state_node(marked, id.value,
        if (kind != "state") "" else if (quoted) label.value else id.value, shape, kind, attrs)^
    return scan.set_cursor(result, stop)
}

fn state_transition(s, initial, start: int) map^ {
    let from = state_endpoint(s, start, true)^
    if (from.id == null) {
        return shared.fail(s, initial, start, from.next, "mermaid.state.statement",
            "Expected a state declaration or transition")^
    }
    let st = if (from.kind == "state") shared.class_suffix(s, initial, from.id, from.next)^
        else scan.set_cursor(initial, from.next)
    let pos = scan.inline_space(s, st.pos)^
    let b = scan.byte(s, pos)^
    if (b in [-1, 10, 59, 58]) {
        let stop = scan.line_end(s, pos)^
        let span = scan.span(s, start, if (b == 58) stop else pos)^
        let label = if (b == 58) shared.normalize_label(shared.strip_semicolons(s, pos + 1, stop)^)^ else null
        let descriptions = if (label == null) [] else
            [<'state-description' value:label.value, 'label-format':label.format, *:span>]
        let result = state_node(st, from.id, from.id, "rounded", "state",
            {'label-format':"text", *:span}, descriptions)^
        return scan.set_cursor(result, stop)
    }
    if (not scan.starts(s, pos, binary("-->")^)^) {
        return shared.fail(s, st, start, pos, "mermaid.state.transition", "Expected '-->' after state '" ++ from.id ++ "'")^
    }
    let to = state_endpoint(s, pos + 3, false)^
    if (to.id == null) {
        return shared.fail(s, st, start, to.next, "mermaid.state.missing-endpoint", "Expected a state after '-->'")^
    }
    let assigned = if (to.kind == "state") shared.class_suffix(s, st, to.id, to.next)^
        else scan.set_cursor(st, to.next)
    let colon = scan.inline_space(s, assigned.pos)^
    let stop = scan.line_end(s, colon)^
    let label = if (scan.byte(s, colon)^ == 58)
        shared.normalize_label(shared.strip_semicolons(s, colon + 1, stop)^)^ else null
    let first_node = ensure_state(assigned, from)^
    let endpoints = ensure_state(first_node, to)^
    let first_spanned = put_state_span(s, endpoints, from.id, start, stop)^
    let spanned = put_state_span(s, first_spanned, to.id, start, stop)^
    let value = <edge from:from.id, to:to.id, *:if (label == null) {} else {label:label.value},
        style:"solid", 'arrow-start':"false", 'arrow-end':"true", relation:"-->",
        'arrow-tail':"none", 'arrow-head':"normal", *:if (label == null) {} else {'label-format':label.format},
        *:scan.span(s, start, stop)^>
    return scan.set_cursor(shared.append(spanned, value), stop)
}

fn state_note_text(s, initial: int) map^ {
    let pos = scan.inline_space(s, initial)^
    let b = scan.byte(s, pos)^
    if (b == 58) {
        let stop = scan.line_end(s, pos + 1)^;
        return {value:shared.strip_semicolons(s, pos + 1, stop)^, next:stop}
    }
    var next_pos = if (b == 10) pos + 1 else pos
    var lines = null
    while ((next_pos < s.length)^) {
        let start = scan.inline_space(s, next_pos)^
        if (scan.starts(s, start, binary("end note")^)^) {
            next_pos = start + 8
            break
        }
        let line = shared.line_text(s, start)^
        if (line.value != "") { lines = scan.add(lines, line.value) }
        next_pos = shared.next_line(s, line.next)^
    }
    return {value:join(scan.finish(lines)^, "\n"), next:next_pos}
}

fn state_note(s, st, start: int, initial: int) map^ {
    let pos = scan.inline_space(s, initial)^
    let side = shared.keyword_at(s, pos, ["left","right"])^
    let after = scan.inline_space(s, pos + len(side))^
    if (side == null or not shared.keyword(s, after, "of")^) {
        return shared.fail(s, st, start, after, "mermaid.state.note-position",
            "Expected 'note left of' or 'note right of'")^
    }
    let target = shared.identifier(s, after + 2)^
    if (target.value == null) {
        return shared.fail(s, st, start, target.next, "mermaid.state.note-target", "Expected a note target state")^
    }
    let text = state_note_text(s, target.next)^
    let label = shared.normalize_label(text.value)^
    let result = ensure_state(st, {id:target.value, kind:"state"})^
    let value = <annotation 'owner-kind':"node", 'owner-id':target.value,
        kind:("note-" ++ side), label:label.value, 'label-format':label.format,
        *:scan.span(s, start, text.next)^>
    return scan.set_cursor(shared.append(result, value), text.next)
}

pub fn read_line(s, st, start: int) map^ {
    let head = shared.keyword_at(s, start, [*shared.common_directives, "state", "note", "hide"])^
    let directive = shared.family_directive(s, st, start, head, "state", "state")^
    if (directive != null) { return directive }
    let after = start + len(head)
    if (head == "state") { return state_declaration(s, st, start, after)^ }
    if (head == "note") { return state_note(s, st, start, after)^ }
    if (head == "hide") { return shared.fail(s, st, start, after, "mermaid.state.hide-unsupported",
        "State description hiding is not implemented")^ }
    return state_transition(s, st, start)^
}

pub fn body_child(entry, meta, state_spans) element^ {
    if (not (entry is element)) { return <meta *meta> }
    let span = if (name(entry) == 'node' and entry["source-start"] == null) state_spans[entry.id] else null
    if (span == null) { return entry }
    let children = content(entry)^
    return <node *:map(entry), *:span, *children>
}
