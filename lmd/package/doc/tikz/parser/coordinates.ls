import scan: lambda.io.source_scan
import lex: .lex
import expression: lambda.doc.tikz.parse_expression

pub fn numeric(s, start: int, limit: int, physical: bool) map?^ {
    let pos = lex.skip(s, start, limit)^
    let b = scan.byte(s, pos)^
    let first = if (b in [43,45]) pos + 1 else pos
    let stop = expression.number_end(s, first)^
    if (stop == first or stop > limit) { return null }
    let magnitude = expression.number_value(scan.text(s, pos, stop)^)
    if (magnitude == null) { return null }
    let unit = if (physical and stop + 2 <= limit) scan.text(s, stop, stop + 2)^ else ""
    let scale = if (unit == "mm") 0.1 else if (unit == "cm") 1.0
        else if (unit == "pt") 2.54 / 72.27 else if (unit == "bp") 2.54 / 72.0 else null;
    {value:if (unit == "mm") magnitude / 10.0 else if (scale == null) magnitude else magnitude * scale,
        next:if (scale == null) stop else stop + 2, explicit:scale != null}
}

fn accept(s, start: int, limit: int, value: int) int?^ {
    let pos = lex.skip(s, start, limit)^
    return if (pos < limit and scan.byte(s, pos)^ == value) pos + 1 else null
}

pub fn cartesian(s, start: int, limit: int, physical: bool) map?^ {
    let opening = accept(s, start, limit, 40)^
    if (opening == null) { return null }
    let x = numeric(s, opening, limit, physical)^
    if (x == null) { return null }
    let comma = accept(s, x.next, limit, 44)^
    if (comma == null) { return null }
    let y = numeric(s, comma, limit, physical)^
    if (y == null) { return null }
    let stop = accept(s, y.next, limit, 41)^
    if (stop == null) { return null }
    return {next:stop, x:x.value, y:y.value, x_explicit:x.explicit, y_explicit:y.explicit}
}

fn axis(s, first: int, stop: int) map?^ {
    let raw = lex.trimmed(s, first, stop)^
    let prefixes = [{text:"axis description cs:", system:"axis-description"}, {text:"axis cs:", system:"axis"}]
    let match_prefix = [for (item in prefixes where ((raw.first + len(item.text) < raw.stop and
        scan.starts(s, raw.first, binary(item.text)^)^)^)) item][0]
    if (match_prefix == null) { return null }
    let x = numeric(s, raw.first + len(match_prefix.text), raw.stop, false)^
    if (x == null) { return null }
    let comma = accept(s, x.next, raw.stop, 44)^
    if (comma == null) { return null }
    let y = numeric(s, comma, raw.stop, false)^
    if (y == null or lex.skip(s, y.next, raw.stop)^ != raw.stop) { return null }
    return {x:x.value, y:y.value, coord_system:match_prefix.system}
}

pub fn pair(s, first: int, stop: int, allow_empty: bool = false) map?^ {
    let comma = lex.separator(s, first, stop, 44, false)^
    if (comma == stop) { return null }
    let x = lex.trimmed(s, first, comma)^
    let y = lex.trimmed(s, comma + 1, stop)^
    if (not allow_empty and (x.first == x.stop or y.first == y.stop)) { return null }
    return {x:x, y:y}
}

fn polar(s, first: int, stop: int) map?^ {
    let angle = numeric(s, first, stop, false)^
    if (angle == null) { return null }
    let colon = accept(s, angle.next, stop, 58)^
    if (colon == null) { return null }
    let radius = numeric(s, colon, stop, true)^
    if (radius == null or radius.value < 0.0 or lex.skip(s, radius.next, stop)^ != stop) { return null }
    return {polar_angle:angle.value, polar_radius:radius.value, radius_explicit:radius.explicit}
}

fn form(s, start: int, limit: int, node: bool) map?^ {
    let span = lex.trimmed(s, start, limit)^
    let first = span.first
    let stop = span.stop
    if (first >= stop) { return null }
    if (stop - first >= 2 and scan.byte(s, first)^ == 36 and scan.byte(s, stop - 1)^ == 36) {
        let raw = lex.trimmed(s, first + 1, stop - 1)^
        let value = lex.value(s, raw)^
        if (value == "") { return null }
        return if (node) {at_calc:value} else {calc:value}
    }
    let polar_position = polar(s, first, stop)^
    if (polar_position != null) { return polar_position }
    let found = pair(s, first, stop, true)^
    if (found != null) {
        if (found.x.first == found.x.stop or found.y.first == found.y.stop) { return null }
        return {x_source:lex.value(s, found.x)^, y_source:lex.value(s, found.y)^}
    }
    var dot = first
    while ((dot < stop and scan.byte(s, dot)^ != 46)^) { dot = dot + 1 }
    let base = lex.trimmed(s, first, dot)^
    let anchor = lex.trimmed(s, min(dot + 1, stop), stop)^
    if (not lex.literal_name(s, base.first, base.stop)^ or (dot < stop and anchor.first == anchor.stop)) { return null }
    let name = lex.value(s, base)^
    let anchor_attr = if (dot == stop) {} else if (node) {at_anchor:lex.value(s, anchor)^} else {anchor:lex.value(s, anchor)^};
    {*:if (node) {at_ref:name} else {ref:name}, *:anchor_attr}
}

pub fn alternate(s, start: int, limit: int, node: bool) map?^ {
    let group = lex.balanced(s, start, limit, 40, 41)^
    if (group == null) { return null }
    let axis_pos = axis(s, group.first, group.stop)^
    let attrs = if (axis_pos == null) form(s, group.first, group.stop, node)^ else axis_pos
    if (attrs == null) { return null }
    return {attrs:attrs, next:group.next}
}

pub fn position(s, start: int, limit: int) map?^ {
    let pos = lex.skip(s, start, limit)^
    let numeric = cartesian(s, pos, limit, true)^
    if (numeric != null) {
        return {attrs:{x:numeric.x, y:numeric.y, coord_system:"cartesian"}, next:numeric.next}
    }
    return alternate(s, pos, limit, true)^
}

pub fn point(x, y, x_explicit: bool = false, y_explicit: bool = false,
             moving: bool = false, via: string? = null) element^ =>
    <point x:x, y:y, x_explicit:x_explicit, y_explicit:y_explicit, move:moving,
        *:if (via == null) {} else {via:via}>

pub fn relative(s, start: int, limit: int) map^ {
    let pos = lex.skip(s, start, limit)^
    let rel = pos < limit and scan.byte(s, pos)^ == 43
    let advancing = rel and pos + 1 < limit and scan.byte(s, pos + 1)^ == 43;
    {first:pos, next:if (advancing) pos + 2 else if (rel) pos + 1 else pos,
     relative:rel, advance:not rel or advancing}
}

pub fn path_literal(s, start: int, limit: int, reference) map?^ {
    let prefix = relative(s, start, limit)^
    let numeric = cartesian(s, prefix.next, limit, true)^
    if (numeric == null) { return null }
    return {*:numeric, x:if (prefix.relative) numeric.x + reference.x else numeric.x,
        y:if (prefix.relative) numeric.y + reference.y else numeric.y, advance:prefix.advance}
}

pub fn vertex(s, start: int, limit: int, reference, moving: bool = false,
              via: string? = null) map?^ {
    let pos = lex.skip(s, start, limit)^
    let literal = path_literal(s, pos, limit, reference)^
    if (literal != null) {
        return {*:literal, cartesian:true, value:point(literal.x, literal.y,
            literal.x_explicit, literal.y_explicit, moving, via)^}
    }
    let computed = alternate(s, pos, limit, false)^
    if (computed == null) { return null }
    return {next:computed.next, x:0.0, y:0.0, advance:true, cartesian:false,
        value:<point *:computed.attrs, move:moving, *:if (via == null) {} else {via:via}>}
}
