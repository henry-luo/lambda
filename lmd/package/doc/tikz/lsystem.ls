// TikZ lindenmayersystems: option parsing here; rewriting and the turtle are
// lambda.procgen.lsystem (Lambda_Pkg_Proc_Gen.md PG9).
import util: lambda.doc.latex.util
import opts: .options
import lsys: lambda.procgen.lsystem

fn rule_pair(raw) map^ {
    let arrow = index_of(raw, "->")
    if (arrow == null) raise error("L-system rule needs ->")
    else {
        let head = trim(slice(raw, 0, arrow))
        let body = trim(slice(raw, arrow + 2, len(raw)))
        if (len(head) != 1 or body == "")
            raise error("L-system rule needs a one-character head and body")
        else {head: head, body: body}
    }
}

fn rules_from(raw) any^ => [for (part in util.split_top_level(raw, ","))
    rule_pair(part)^]

// TikZ draws one path, so moving without drawing and branching stay unsupported.
fn first_unsupported(expanded) {
    let hits = [for (ch in ["f", "[", "]"], let at = index_of(expanded, ch) where at != null) [at, ch]];
    if (len(hits) == 0) null else sort(hits, (h) => h)[0][1]
}

pub fn points(path) any^ {
    let outer = util.parse_kv_options(path.system_source)
    let unsupported_outer = [for (key, value at outer
        where string(key) != "l-system" and
              string(key) != "lindenmayer system") string(key)]
    if (len(unsupported_outer) > 0)
        raise error("unsupported L-system option: " ++ unsupported_outer[0])
    else {
        let inner_source = if (outer["l-system"] != null) outer["l-system"]
            else outer["lindenmayer system"]
        if (inner_source == null) raise error("L-system specification is missing")
        else {
            let spec = util.parse_kv_options(inner_source)
            let unsupported = [for (key, value at spec
                where string(key) != "rule set" and string(key) != "axiom" and
                    string(key) != "angle" and string(key) != "order" and
                    string(key) != "step") string(key)]
            if (len(unsupported) > 0 or spec["rule set"] == null or
                spec.axiom == null or spec.order == null or spec.step == null)
                raise error("unsupported or incomplete L-system specification")
            else {
                let parsed_order = int(trim(spec.order)) ^ { null }
                if (parsed_order == null or parsed_order < 0 or parsed_order > 12)
                    raise error("L-system order must be from 0 through 12")
                else {
                    let turn = if (spec.angle == null) 90.0
                        else opts.numeric_value(spec.angle)^
                    let step = opts.dimension_px(spec.step)^ * 2.54 / 96.0
                    // a later rule for the same head replaces an earlier one
                    let rules = map([for (rule in rules_from(spec["rule set"])^) for (x in [rule.head, rule.body]) x])
                    let expanded = lsys.expand(trim(spec.axiom), rules, parsed_order)^
                    let unsupported_action = first_unsupported(expanded)
                    let rotation = opts.value(path, "rotate", null)
                    let heading = if (rotation == null) 0.0
                        else opts.numeric_value(rotation)^;
                    if (unsupported_action != null) raise error("unsupported L-system turtle action: " ++ unsupported_action)
                    else (let drawn = lsys.turtle(expanded, {step: step, angle: turn, heading: heading, draw: "F", move: "", reverse: ""})^,
                        [{x: 0.0, y: 0.0}, *[for (s in drawn.segments) {x: s[2], y: s[3]}]])
                }
            }
        }
    }
}
