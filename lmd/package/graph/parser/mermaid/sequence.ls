import scan: lambda.io.source_scan
import shared: .common

fn participant(s, st, start: int, pos: int, kind: string) map^ {
    let id = shared.identifier(s, pos)^
    if (id.value == null) {
        return shared.fail(s, st, start, id.next, "mermaid.sequence.participant", "Expected a sequence participant name")^
    }
    let next_pos = scan.inline_space(s, id.next)^
    let label = if (shared.keyword(s, next_pos, "as")^) shared.line_text(s, next_pos + 2)^
        else {value:id.value, next:next_pos}
    let declared = shared.declare(s, st, id.value, label.value, start, label.next,
        {'label-format':"text", 'participant-kind':kind})^
    return scan.set_cursor(declared, scan.line_end(s, label.next)^)
}

fn note(s, st, start: int, initial: int) map^ {
    let pos = scan.inline_space(s, initial)^
    let side = shared.keyword_at(s, pos, ["right", "left", "over"])^
    let after = scan.inline_space(s, pos + len(side))^
    if (side == null or (side != "over" and not shared.keyword(s, after, "of")^)) {
        return shared.fail(s, st, start, after, "mermaid.sequence.note",
            "Expected Note left of, right of, or over participants")^
    }
    let id = shared.identifier(s, if (side == "over") after else after + 2)^
    let comma = scan.inline_space(s, id.next)^
    let peer = if (id.value != null and side == "over" and scan.byte(s, comma)^ == 44)
        shared.identifier(s, comma + 1)^ else null
    if (peer != null and peer.value == null) {
        return shared.fail(s, st, start, peer.next, "mermaid.sequence.note", "Expected second participant after comma")^
    }
    let colon = if (peer == null) comma else scan.inline_space(s, peer.next)^
    if (id.value == null or scan.byte(s, colon)^ != 58) {
        return shared.fail(s, st, start, colon, "mermaid.sequence.note", "Expected a participant and note text")^
    }
    let line = shared.line_text(s, colon + 1)^
    let label = shared.normalize_label(line.value)^
    let first_node = shared.ensure(s, st, id.value, start, line.next)^
    let endpoints = if (peer == null) first_node else shared.ensure(s, first_node, peer.value, start, line.next)^
    let extra = if (peer == null) {} else {'to-id':peer.value}
    let annotation = <annotation 'owner-kind':"node", 'owner-id':id.value, kind:("note-" ++ side),
        label:label.value, 'label-format':label.format, *:scan.span(s, start, line.next)^, *:extra>
    return scan.set_cursor(shared.append(endpoints, annotation), line.next)
}

let messages = ["-->>", "--)", "->>", "-->", "-)", "->"]

fn message(s, initial, start: int) map^ {
    let st = {*:initial, message_index:initial.message_index + 1}
    let from = shared.identifier(s, start)^
    let op = [for (relation in messages where scan.starts(s, from.next, binary(relation)^)^) relation][0]
    if (from.value == null or op == null) {
        return shared.fail(s, st, start, from.next, "mermaid.sequence.message",
            "Expected a sequence message such as Alice->>Bob: text")^
    }
    let after = from.next + len(op)
    let suffix = scan.byte(s, after)^
    let to = shared.identifier(s, if (suffix in [43, 45]) after + 1 else after)^
    let colon = scan.inline_space(s, to.next)^
    if (to.value == null or scan.byte(s, colon)^ != 58) {
        return shared.fail(s, st, start, colon, "mermaid.sequence.message", "Expected a target participant and message text")^
    }
    let line = shared.line_text(s, colon + 1)^
    let label = shared.normalize_label(line.value)^
    let first_node = shared.ensure(s, st, from.value, start, line.next)^
    let endpoints = shared.ensure(s, first_node, to.value, start, line.next)^
    let style = if (starts_with(op, "--")) "dotted" else "solid"
    let activation = if (suffix == 43) {activation:"start"} else if (suffix == 45) {activation:"end"} else {}
    let value = <edge from:from.value, to:to.value, label:label.value, style:style,
        'arrow-start':"false", 'arrow-end':"true", id:("message-" ++ string(initial.message_index)),
        relation:op, *:activation, 'label-format':label.format, 'arrow-head':"normal",
        *:scan.span(s, start, line.next)^>
    return scan.set_cursor(shared.append(endpoints, value), line.next)
}

fn block_event(s, st, start: int, pos: int, kind: string, phase: string) map^ {
    let line = if (phase == "end") {value:"", next:pos} else shared.line_text(s, pos)^
    let value = <'sequence-block' kind:kind, phase:phase, label:line.value, *:scan.span(s, start, line.next)^>
    return scan.set_cursor(shared.append(st, value), scan.line_end(s, line.next)^)
}

pub fn read_line(s, st, start: int) map^ {
    let head = shared.keyword_at(s, start, ["loop", "alt", "par", "else", "and", "participant",
        "actor", "end", "Note", "accTitle", "accDescr"])^
    if (head == null) { return message(s, st, start)^ }
    let after = start + len(head)
    if (head in ["loop", "alt", "par"]) {
        return block_event(s, {*:st, blocks:scan.add(st.blocks, head)}, start, after, head, "start")^
    }
    if (head in ["else", "and"]) {
        let kind = if (head == "else") "alt" else "par"
        if (st.blocks.value == kind) { return block_event(s, st, start, after, kind, "branch")^ }
        return shared.fail(s, st, start, after, "mermaid.sequence.branch", "Sequence branch does not match its opening block")^
    }
    if (head == "end") {
        if (st.blocks == null) {
            return shared.fail(s, st, start, after, "mermaid.sequence.unmatched-end", "Sequence block has no opening statement")^
        }
        return block_event(s, {*:st, blocks:st.blocks.previous}, start, after, st.blocks.value, "end")^
    }
    if (head in ["participant","actor"]) { return participant(s, st, start, after, head)^ }
    if (head == "Note") { return note(s, st, start, after)^ }
    return shared.accessibility(s, st, start, after, if (head == "accTitle") "title" else "description")^
}
