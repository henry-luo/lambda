import scan: lambda.io.source_scan
import shared: .mermaid.common
import sequence: .mermaid.sequence
import er: .mermaid.er
import class_reader: .mermaid.class
import flowchart: .mermaid.flowchart
import state_reader: .mermaid.state

let families = {
    graph:"flowchart", flowchart:"flowchart", classDiagram:"class",
    sequenceDiagram:"sequence", erDiagram:"er", 'stateDiagram-v2':"state", stateDiagram:"state", gantt:"gantt", pie:"pie", 'sankey-beta':"sankey",
    timeline:"timeline", 'xychart-beta':"xychart", xychart:"xychart"
}
let family_names = [for (word, kind in families) string(word)]

pub fn read(bytes: binary?) map^ {
    let s = scan.source(bytes)^
    if (s.length == 0) { return {root:null, errors:[]} }
    let start = shared.skip(s, 0)^
    let front = shared.front_matter(s, start)^
    let init_start = shared.skip(s, front.next)^
    let init = shared.initialization(s, init_start)^
    let head = shared.skip(s, init.next)^
    let family = shared.keyword_at(s, head, family_names)^
    let kind = families[family] or "flowchart"
    let after = head + len(family)
    let direction = if (family != null and kind == "flowchart") shared.direction(s, after)^
        else {value:null, next:after}
    let initial = {pos:direction.next, children:null, errors:null, blocks:null,
        ids:{}, edge_ids:{}, state_spans:{}, message_index:0, meta:null, direction:(direction.value or "TB"),
        syntax_code:if (kind in ["sequence","er","state","class"]) "mermaid." ++ kind ++ ".syntax" else "mermaid.syntax"}
    let warnings = shared.with_warning(s, shared.with_warning(s, initial, front.warning)^, init.warning)^
    let st = if (kind == "sequence") {
        let parsed = shared.parse_body(s, warnings, sequence.read_line)^;
        if (parsed.blocks == null) parsed else scan.issue(s, parsed, parsed.pos,
            "mermaid.sequence.unclosed-block", "Sequence block is missing end")^
    } else if (kind == "er") shared.parse_body(s, warnings, er.read_line)^
    else if (kind == "state") shared.parse_body(s, warnings, state_reader.read_line)^
    else if (kind == "flowchart") flowchart.read(s, warnings)^
    else if (kind == "class") class_reader.read(s, warnings)^ else
        scan.issue(s, warnings, start, "mermaid.chart-family", "Mermaid " ++ kind ++ " diagrams belong to lambda.chart")^
    let meta_children = scan.finish(st.meta)^
    let children = if (kind in ["flowchart","class"]) st.built_children else [for (entry in scan.finish(st.children)^)
        state_reader.body_child(entry, meta_children, st.state_spans)^]
    let diagnostics = scan.finish(st.errors)^
    let entries = [front.child, init.child, *children,
        if (len(diagnostics) > 0) <diagnostics *diagnostics> else null]
    let attrs = if (kind in ["sequence","er","state","flowchart","class"]) {} else {status:"unsupported"};
    {root:<graph type:"directed", layout:"mermaid", flavor:"mermaid", version:"1",
        'ir-stage':"source", kind:kind, 'diagram-type':kind, directed:"true", direction:st.direction, 'rank-dir':st.direction,
        *:attrs, *:scan.span(s, start, st.pos)^, *entries>, errors:[]}
}
