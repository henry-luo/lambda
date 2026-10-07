// Bounded anonymous L-system turtle; rewriting and geometry are script policy.
import util: lambda.latex.util
import opts: .options

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

fn replacement(ch, rules) {
    let matching = [for (rule in rules where rule.head == ch) rule.body]
    if (len(matching) == 0) ch else matching[len(matching) - 1]
}

fn rewrite(source, rules) => util.str_join([for (at in 0 to (len(source) - 1))
    replacement(slice(source, at, at + 1), rules)], "")

fn expand(source, rules, order) any^ {
    if (order == 0) source
    else {
        let next = rewrite(source, rules)
        if (len(next) > 100000)
            raise error("L-system expansion exceeds 100000 symbols")
        else expand(next, rules, order - 1)^
    }
}

// Divide the stream so order-11 systems do not require thousands of stack frames.
fn turtle_segment(source, first, ending, x, y, heading, step, turn) any^ {
    if (first >= ending) {x: x, y: y, heading: heading, points: []}
    else if (ending - first == 1) {
        let ch = slice(source, first, ending)
        if (ch == "F") {
            let radians = heading * 3.141592653589793 / 180.0
            let next_x = x + step * math.cos(radians)
            let next_y = y + step * math.sin(radians)
            {x: next_x, y: next_y, heading: heading,
             points: [{x: next_x, y: next_y}]}
        } else if (ch == "+")
            {x: x, y: y, heading: heading + turn, points: []}
        else if (ch == "-")
            {x: x, y: y, heading: heading - turn, points: []}
        else if (ch == "f" or ch == "[" or ch == "]")
            raise error("unsupported L-system turtle action: " ++ ch)
        else {x: x, y: y, heading: heading, points: []}
    } else {
        let middle = first + int(floor(float(ending - first) / 2.0))
        let left = turtle_segment(source, first, middle, x, y, heading,
            step, turn)^
        let right = turtle_segment(source, middle, ending,
            left.x, left.y, left.heading, step, turn)^;
        {x: right.x, y: right.y, heading: right.heading,
         points: left.points ++ right.points}
    }
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
                    let rules = rules_from(spec["rule set"])^
                    let expanded = expand(trim(spec.axiom), rules, parsed_order)^
                    let rotation = opts.value(path, "rotate", null)
                    let heading = if (rotation == null) 0.0
                        else opts.numeric_value(rotation)^
                    let drawn = turtle_segment(expanded, 0, len(expanded),
                        0.0, 0.0, heading, step, turn)^;
                    [{x: 0.0, y: 0.0}, *drawn.points]
                }
            }
        }
    }
}
