// TikZ coordinates beyond literals: named anchors, calc expressions and
// positioning-library placement. Values are picture units (cm, y up).
import opts: .options
import util: lambda.latex.util
import pgfmath: .pgfmath
import shapes: .shapes

let PI = 3.141592653589793

// A coordinate component: a unit dimension is absolute cm; a plain number scales by the basis.
pub fn component(raw, basis) any^ {
    let source = trim(raw)
    let units = if (ends_with(source, "cm")) "cm"
        else if (ends_with(source, "mm")) "mm"
        else if (ends_with(source, "pt")) "pt"
        else if (ends_with(source, "bp")) "bp"
        else if (ends_with(source, "in")) "in" else null
    if (units == null) opts.numeric_value(source)^ * basis
    else {
        let magnitude = opts.numeric_value(slice(source, 0, len(source) - len(units)))^
        let factor = if (units == "cm") 1.0
            else if (units == "mm") 0.1
            else if (units == "pt") 2.54 / 72.27
            else if (units == "bp") 2.54 / 72.0
            else 2.54
        magnitude * factor
    }
}

pub fn has_unit(raw) {
    let source = trim(raw)
    ends_with(source, "cm") or ends_with(source, "mm") or ends_with(source, "pt") or
        ends_with(source, "bp") or ends_with(source, "in")
}

// Cartesian `x,y` or polar `angle:radius` text as a vector in cm.
pub fn literal_vector(body, basis_x, basis_y) any^ {
    let cartesian = util.split_top_level(body, ",")
    let polar = util.split_top_level(body, ":")
    if (len(cartesian) == 2)
        [component(cartesian[0], basis_x)^, component(cartesian[1], basis_y)^]
    else if (len(polar) == 2) {
        let angle = opts.numeric_value(polar[0])^ * PI / 180.0
        let radius = component(polar[1], basis_x)^;
        [radius * math.cos(angle), radius * math.sin(angle)]
    } else raise error("unsupported TikZ coordinate: (" ++ body ++ ")")
}

// A parsed point element (literal, polar or computed sources) in cm, before shifts.
pub fn literal_point(point, basis_x, basis_y, program_data = null) any^ {
    if (point.polar_angle != null) {
        let angle = float(point.polar_angle) * PI / 180.0
        let radius = float(point.polar_radius)
        let x_basis = if (point.radius_explicit == true) 1.0 else basis_x
        let y_basis = if (point.radius_explicit == true) 1.0 else basis_y;
        {x: radius * math.cos(angle) * x_basis, y: radius * math.sin(angle) * y_basis}
    } else {
        let source_x = if (point.x_source == null) float(point.x)
            else pgfmath.evaluate_source(point.x_source, program_data)^
        let source_y = if (point.y_source == null) float(point.y)
            else pgfmath.evaluate_source(point.y_source, program_data)^;
        {x: source_x * (if (point.x_explicit == true) 1.0 else basis_x),
         y: source_y * (if (point.y_explicit == true) 1.0 else basis_y)}
    }
}

fn skip_space(source, at) =>
    if (at < len(source) and index_of(" \n\t\r", slice(source, at, at + 1)) != null)
        skip_space(source, at + 1)
    else at

fn identifier_body(body) {
    let raw = trim(body)
    let dot = index_of(raw, ".")
    let named = if (dot == null) raw else trim(slice(raw, 0, dot))
    let first = slice(named, 0, 1)
    if (named == "" or index_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_", first) == null)
        null
    else {ref: named, anchor: if (dot == null) null else trim(slice(raw, dot + 1, len(raw)))}
}

// Literal coordinates are transformed by the enclosing frame (basis, scale and
// scope shift) before calc arithmetic, as TikZ evaluates calc on canvas points.
pub fn calc_frame(basis_x = 1.0, basis_y = 1.0, scale = 1.0, shift_x = 0.0, shift_y = 0.0,
             program_data = null) =>
    {basis_x: basis_x, basis_y: basis_y, scale: scale, shift_x: shift_x,
     shift_y: shift_y, program: program_data}

// A coordinate body inside a calc expression: a node name, nested calc, or literal.
pub fn point_ref(body, frame) any^ {
    let raw = trim(body)
    let named = identifier_body(raw)
    if (starts_with(raw, "$") and ends_with(raw, "$") and len(raw) >= 2)
        {calc: parse_calc(slice(raw, 1, len(raw) - 1), frame)^}
    else if (named != null and not contains(raw, ",") and not contains(raw, ":")) named
    else {
        let vector = literal_vector(raw, frame.basis_x, frame.basis_y)^;
        {x: vector[0] * frame.scale + frame.shift_x, y: vector[1] * frame.scale + frame.shift_y}
    }
}

fn factor_value(raw, program_data) any^ {
    let source = trim(util.unwrap_braces(trim(raw)))
    let numeric = opts.numeric_value(source) ^ { null }
    if (numeric != null) numeric else pgfmath.evaluate_source(source, program_data)^
}

// Read a `!...!` modifier and its target coordinate.
fn modifier(source, at, frame) any^ {
    let close = index_of(slice(source, at + 1, len(source)), "!")
    let valid_close = if (close == null) raise error("unclosed TikZ calc modifier") else true
    let text = trim(slice(source, at + 1, at + 1 + close))
    let after = skip_space(source, at + 2 + close)
    let open_at = index_of(slice(source, after, len(source)), "(")
    let valid_open = if (open_at == null) raise error("TikZ calc modifier needs a target coordinate")
        else true
    let prefix = trim(slice(source, after, after + open_at))
    let target = util.read_balanced(source, after + open_at, "(", ")")^
    let target_ref = point_ref(target.raw, frame)^
    let angle = if (prefix == "") 0.0
        else if (ends_with(prefix, ":")) opts.numeric_value(slice(prefix, 0, len(prefix) - 1))^
        else raise error("unsupported TikZ calc modifier: " ++ prefix)
    let step = if (starts_with(text, "("))
        {kind: "projection", through: point_ref(util.read_balanced(text, 0, "(", ")")^.raw,
            frame)^, target: target_ref, angle: angle}
        else if (has_unit(text))
            {kind: "distance", amount: component(text, 1.0)^, target: target_ref, angle: angle}
        else {kind: "partway", amount: factor_value(text, frame.program)^,
            target: target_ref, angle: angle};
    {next: target.next, step: step}
}

fn modifiers(source, at, frame, acc) any^ {
    let cursor = skip_space(source, at)
    if (slice(source, cursor, cursor + 1) != "!") {next: cursor, steps: acc}
    else {
        let read = modifier(source, cursor, frame)^
        modifiers(source, read.next, frame, [*acc, read.step])^
    }
}

fn term(source, at, direction, frame) any^ {
    let cursor = skip_space(source, at)
    let star = index_of(slice(source, cursor, len(source)), "*")
    let open_at = index_of(slice(source, cursor, len(source)), "(")
    let valid_open = if (open_at == null) raise error("TikZ calc term needs a coordinate")
        else true
    // A factor `n*` or `{expr}*` precedes the coordinate when `*` comes first.
    let factored = star != null and star < open_at
    let factor = if (factored) factor_value(slice(source, cursor, cursor + star), frame.program)^
        else 1.0
    let coordinate_at = if (factored) skip_space(source, cursor + star + 1) else cursor + open_at
    let valid_coordinate = if (not factored and trim(slice(source, cursor, cursor + open_at)) != "")
        raise error("unsupported TikZ calc term: " ++ trim(slice(source, cursor, cursor + open_at)))
        else true
    let base = util.read_balanced(source, coordinate_at, "(", ")")^
    let steps = modifiers(source, base.next, frame, [])^;
    {next: steps.next, term: {scale: direction * factor,
        base: point_ref(base.raw, frame)^, steps: steps.steps}}
}

fn terms(source, at, direction, frame, acc) any^ {
    let read = term(source, at, direction, frame)^
    let cursor = skip_space(source, read.next)
    let operator = slice(source, cursor, cursor + 1)
    if (cursor >= len(source)) [*acc, read.term]
    else if (operator == "+" or operator == "-")
        terms(source, cursor + 1, if (operator == "+") 1.0 else -1.0,
            frame, [*acc, read.term])^
    else raise error("unsupported TikZ calc operator: " ++ operator)
}

// Parse the text between `($` and `$)` into terms of scaled, modified coordinates.
pub fn parse_calc(source, frame) any^ {
    let cursor = skip_space(source, 0)
    let leading = slice(source, cursor, cursor + 1)
    let direction = if (leading == "-") -1.0 else 1.0
    let first_at = if (leading == "-" or leading == "+") cursor + 1 else cursor
    if (trim(source) == "") raise error("empty TikZ calc expression")
    else terms(source, first_at, direction, frame, [])^
}

fn ref_names(ref) {
    if (ref == null) []
    else if (ref.calc != null) refs(ref.calc)
    else if (ref.ref != null) [ref.ref]
    else []
}

// Node and coordinate names a parsed calc expression refers to.
pub fn refs(parsed) => [for (item in parsed, found in [*ref_names(item.base),
    *[for (step in item.steps, step_ref in [*ref_names(step.target), *ref_names(step.through)])
        step_ref]]) found]

fn find_named(named, id) any^ {
    let matches = [for (entry in named where entry.id == id) entry]
    if (len(matches) == 0) raise error("unknown TikZ node: " ++ id)
    else matches[len(matches) - 1]
}

// A named node's anchor, or its center when no anchor is given.
pub fn named_point(named, id, anchor) any^ {
    let entry = find_named(named, id)^
    if (anchor == null or anchor == "center") [entry.x, entry.y]
    else {
        let offset = shapes.anchor_offset(entry.geom, anchor)^;
        [entry.x + offset[0], entry.y + offset[1]]
    }
}

pub fn evaluate_ref(ref, named) any^ {
    if (ref.calc != null) evaluate_calc(ref.calc, named)^
    else if (ref.ref != null) named_point(named, ref.ref, ref.anchor)^
    else [float(ref.x), float(ref.y)]
}

fn rotated(vector, degrees) {
    let angle = degrees * PI / 180.0;
    [vector[0] * math.cos(angle) - vector[1] * math.sin(angle),
     vector[0] * math.sin(angle) + vector[1] * math.cos(angle)]
}

fn apply_step(origin, step, named) any^ {
    let target = evaluate_ref(step.target, named)^
    let direction = rotated([target[0] - origin[0], target[1] - origin[1]], float(step.angle))
    let length = math.sqrt(direction[0] * direction[0] + direction[1] * direction[1])
    if (step.kind == "partway")
        [origin[0] + float(step.amount) * direction[0],
         origin[1] + float(step.amount) * direction[1]]
    else if (length == 0.0) raise error("TikZ calc modifier needs distinct coordinates")
    else if (step.kind == "distance")
        [origin[0] + float(step.amount) * direction[0] / length,
         origin[1] + float(step.amount) * direction[1] / length]
    else {
        // Orthogonal projection of `through` onto the line origin -> target.
        let through = evaluate_ref(step.through, named)^
        let t = ((through[0] - origin[0]) * direction[0] +
            (through[1] - origin[1]) * direction[1]) / (length * length);
        [origin[0] + t * direction[0], origin[1] + t * direction[1]]
    }
}

fn apply_steps(point, steps, index, named) any^ {
    if (index >= len(steps)) point
    else apply_steps(apply_step(point, steps[index], named)^, steps, index + 1, named)^
}

pub fn evaluate_calc(parsed, named) any^ {
    let vectors = [for (item in parsed)
        (let base = apply_steps(evaluate_ref(item.base, named)^, item.steps, 0, named)^,
         [float(item.scale) * base[0], float(item.scale) * base[1]])];
    [sum([for (vector in vectors) vector[0]]), sum([for (vector in vectors) vector[1]])]
}

// Positioning directions: own anchor, reference anchor, unit shift, diagonal flag.
let PLACEMENTS = [
    {key: "above", own: "south", target: "north", x: 0.0, y: 1.0},
    {key: "below", own: "north", target: "south", x: 0.0, y: -1.0},
    {key: "left", own: "east", target: "west", x: -1.0, y: 0.0},
    {key: "right", own: "west", target: "east", x: 1.0, y: 0.0},
    {key: "above left", own: "south east", target: "north west", x: -1.0, y: 1.0},
    {key: "above right", own: "south west", target: "north east", x: 1.0, y: 1.0},
    {key: "below left", own: "north east", target: "south west", x: -1.0, y: -1.0},
    {key: "below right", own: "north west", target: "south east", x: 1.0, y: -1.0}]

pub let PLACEMENT_KEYS = [for (entry in PLACEMENTS) entry.key]

fn word_of(value) {
    // The `of` keyword splits the shift from the reference node.
    let padded = " " ++ trim(value) ++ " "
    let found = index_of(padded, " of ")
    if (found == null) null
    else {shift: trim(slice(padded, 0, found)), target: trim(slice(padded, found + 4, len(padded)))}
}

// `y and x` or a single distance; the positioning library scales a single
// diagonal distance by 1/sqrt(2) on each axis, with or without `of`.
fn shift_pair(raw, diagonal) any^ {
    let parts = [for (part in split(" " ++ trim(raw) ++ " ", " and ")) trim(part)]
    if (len(parts) == 2)
        {y: component(parts[0], 1.0)^, x: component(parts[1], 1.0)^}
    else if (len(parts) == 1) {
        let single = component(parts[0], 1.0)^
        let factor = if (diagonal) 0.707106781 else 1.0;
        {x: single * factor, y: single * factor}
    } else raise error("unsupported TikZ positioning distance: " ++ raw)
}

// Option keys with their source position, for key-order rules.
fn ordered_options(node) => [for (index, child in node
    where child is element and string(name(child)) == "option")
    {index: index, key: opts.normalize_key(child.key), value: child.value}]

// A node's positioning request, or null. `node_distance` is the inherited default.
// Keys apply in order, as in TikZ: the last placement key wins, and a node's own
// `on grid` counts only when it precedes that key.
pub fn placement(node, node_distance, on_grid) any^ {
    let options = ordered_options(node)
    let used = [for (option in options
        where any([for (key in PLACEMENT_KEYS) key == option.key])) option]
    if (len(used) == 0) null
    else {
        let chosen = used[len(used) - 1]
        let entry = [for (candidate in PLACEMENTS where candidate.key == chosen.key) candidate][0]
        let value = chosen.value
        let of = word_of(value)
        let diagonal = entry.x != 0.0 and entry.y != 0.0
        let grid = on_grid or any([for (option in options
            where option.key == "on grid" and option.index < chosen.index) true])
        let shift_source = if (of == null) value
            else if (of.shift == "") node_distance else of.shift
        let shift = if (shift_source == "") {x: 0.0, y: 0.0}
            else shift_pair(shift_source, diagonal)^
        let target = if (of == null) null else identifier_body(of.target)
        let valid_target = if (of != null and target == null)
            raise error("unsupported TikZ positioning reference: " ++ of.target) else true
        // `on grid` aligns centers only against a whole node, not an explicit anchor.
        let centered = grid and target != null and target.anchor == null;
        {own: if (centered) "center" else entry.own,
         target: target, target_anchor: if (target == null) null
            else if (target.anchor != null) target.anchor
            else if (centered) "center" else entry.target,
         x: entry.x * shift.x, y: entry.y * shift.y}
    }
}
