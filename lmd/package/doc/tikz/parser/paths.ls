import scan: lambda.io.source_scan
import lex: .lex
import styles: .styles
import coords: .coordinates
import nodes: .nodes
import plots: .plots

fn inline(s, current, pos: int, stop: int, segment: bool = false) map^ {
    let parsed = nodes.inline_node(s, current.cursor, pos, stop, segment)^
    return {*:current, cursor:parsed.cursor, pos:parsed.next,
        children:if (parsed.value == null) current.children else scan.add(current.children, parsed.value)}
}

fn failed(current, message: string) map^ => {*:current, cursor:lex.fail(current.cursor, current.cursor.pos, message)}

fn at_vertex(current, vertex) map^ {
    let cursor = plots.add_points(current.cursor, 1)^;
    {*:current, cursor:cursor, pos:vertex.next, point:vertex, cartesian:vertex.cartesian,
        reference:if (vertex.advance) {x:vertex.x,y:vertex.y} else current.reference,
        children:scan.add(current.children, vertex.value)}
}

fn marker(s, current, pos: int, stop: int, keyword: string) map^ {
    let start = lex.skip(s, pos, stop)^
    if (scan.byte(s, start)^ != 40) { return failed(current, keyword ++ " requires a positive radius")^ }
    let x = coords.numeric(s, start + 1, stop, true)^
    let separator = if (keyword == "ellipse" and x != null) lex.word(s, x.next, stop, "and", false)^ else null
    let y = if (keyword == "circle") x else if (separator == null) null else coords.numeric(s, int(separator), stop, true)^
    let end_pos = if (y == null) stop else lex.skip(s, y.next, stop)^
    if (x == null or y == null or x.value <= 0.0 or y.value <= 0.0 or scan.byte(s, end_pos)^ != 41) {
        return failed(current, if (keyword == "circle") "circle requires a positive radius"
            else "ellipse requires positive x and y radii")^
    }
    return {*:current, pos:end_pos + 1, attrs:{*:current.attrs, shape:"ellipse",
        rx:x.value, ry:y.value, rx_explicit:x.explicit, ry_explicit:y.explicit}}
}

fn control(s, pos: int, stop: int) map?^ {
    let prefix = coords.relative(s, pos, stop)^
    let p = coords.cartesian(s, prefix.next, stop, true)^
    return if (p == null) null else {*:p, relative:prefix.relative}
}

fn cubic_value(t: float, u: float, a, b, c, d) float^ =>
    u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d

fn curve(s, current, pos: int, stop: int) map^ {
    let keyword = lex.word(s, pos, stop, "controls")^
    if (not current.cartesian or keyword == null) { return failed(current, "TikZ curve requires control points")^ }
    let a = control(s, keyword, stop)^
    let next_control = if (a == null) null else lex.word(s, a.next, stop, "and")^
    let b = if (next_control == null) null else control(s, int(next_control), stop)^
    let end_at = if (b == null) null else lex.word(s, b.next, stop, "..")^
    let finish = if (end_at == null) null else coords.cartesian(s, int(end_at), stop, true)^
    if (finish == null) { return failed(current, "TikZ cubic curve requires two controls and an endpoint")^ }
    let cursor = plots.add_points(current.cursor, 32)^
    let start = current.point
    let x1 = if (a.relative) a.x + start.x else a.x
    let y1 = if (a.relative) a.y + start.y else a.y
    let x2 = if (b.relative) b.x + finish.x else b.x
    let y2 = if (b.relative) b.y + finish.y else b.y
    // The source Mark stores the same bounded 32-piece path approximation.
    let points = [for (i in 1 to 32) {
        let t = float(i) / 32.0
        let u = 1.0 - t;
        coords.point(cubic_value(t,u,start.x,x1,x2,finish.x)^,
            cubic_value(t,u,start.y,y1,y2,finish.y)^)^
    }]
    let entries = reduce([current.children,*points], scan.add)^;
    {*:current, cursor:cursor, children:entries, point:finish,
        reference:{x:finish.x,y:finish.y}, cartesian:true, pos:finish.next}
}

fn shaped_segment(s, current, keyword: string, after: int, stop: int) map^ {
    let rectangle = keyword == "rectangle"
    let vertex = if (rectangle) {
        let numeric = if (current.cartesian) coords.path_literal(s, after, stop, current.reference)^ else null;
        if (numeric == null) null else {*:numeric, cartesian:true,
            value:coords.point(numeric.x, numeric.y, numeric.x_explicit, numeric.y_explicit)^}
    } else coords.vertex(s, after, stop, current.reference)^
    if (vertex == null) {
        let message = if (rectangle) "rectangle requires a second coordinate" else "grid requires a second coordinate"
        let failed_vertex = if (rectangle) current else failed(current, "expected finite Cartesian coordinate or named node")^
        return failed(failed_vertex, message)^
    }
    let appended = at_vertex(current, vertex)^
    let result = {*:appended, attrs:{*:appended.attrs, shape:keyword}}
    let pos = lex.skip(s, vertex.next, stop)^
    if (pos == stop) { return {*:result, pos:stop} }
    if (not rectangle) { return failed(result, "grid must end after its second coordinate")^ }
    let label = inline(s, result, pos, stop)^
    if (label.pos == pos) { return label }
    if (lex.skip(s, label.pos, stop)^ != stop) { return failed(label, "rectangle accepts only a following node")^ }
    return {*:label, pos:stop}
}

fn line_segment(s, current, pos: int, stop: int, connector: string) map^ {
    let via = if (connector == "--") null else connector
    let attrs = if (via == null) current.attrs else {*:current.attrs,
        line_mode:if (via == "|-") "vertical-horizontal" else "horizontal-vertical"}
    let first = lex.skip(s, pos + 2, stop)^
    let with_label = if (scan.starts(s, first, binary("node")^)^) inline(s, current, first, stop, true)^
        else {*:current, pos:first}
    if (len(with_label.cursor.errors)^ > 0) { return with_label }
    let vertex = coords.vertex(s, with_label.pos, stop, current.reference, false, via)^
    if (vertex == null) {
        let bad = failed(with_label, "expected finite Cartesian coordinate or named node")^
        return failed(bad, "line segment requires a second coordinate")^
    }
    return at_vertex({*:with_label, attrs:attrs}, vertex)^
}

fn step(s, current, pos: int, stop: int) map^ {
    let keywords = ["node","rectangle","grid","circle","ellipse"]
    let found = [for (word in keywords, after in [lex.word(s, pos, stop, word)^] where after != null)
        {word:word, after:after}][0]
    if (found.word == "node") { return inline(s, current, pos, stop)^ }
    if (found.word in ["rectangle","grid"]) { return shaped_segment(s, current, found.word, found.after, stop)^ }
    if (found.word in ["circle","ellipse"]) { return marker(s, current, found.after, stop, found.word)^ }
    let connector = scan.text(s, pos, min(pos + 2, stop))^
    if (connector in ["--","-|","|-"]) { return line_segment(s, current, pos, stop, connector)^ }
    if (connector == "..") { return curve(s, current, pos + 2, stop)^ }
    if (scan.byte(s, pos)^ in [40,43]) {
        let vertex = coords.vertex(s, pos, stop, current.reference, true)^
        if (vertex == null) { return failed(current, "expected finite Cartesian coordinate or named node")^ }
        return at_vertex(current, vertex)^
    }
    return failed(current, "unsupported path operator in TikZ geometry")^
}

fn geometry(s, cursor, begin: int, stop: int) map^ {
    let origin = {x:0.0,y:0.0}
    let initial = {cursor:cursor, pos:begin, point:origin, reference:origin, cartesian:false, children:null, attrs:{}}
    let uses_origin = lex.word(s, begin, stop, "circle")^ != null or lex.word(s, begin, stop, "ellipse")^ != null
    let vertex = if (uses_origin) {x:0.0, y:0.0, cartesian:true, advance:true, next:begin,
        value:coords.point(0.0, 0.0)^} else coords.vertex(s, begin, stop, origin)^
    if (vertex == null) {
        let issue = failed(initial, "expected finite Cartesian coordinate or named node")^
        return failed(issue, "expected TikZ path coordinate")^
    }
    var current = at_vertex(initial, vertex)^
    while ((len(current.cursor.errors) == 0)^) {
        let pos = lex.skip(s, current.pos, stop)^
        if (pos == stop) { break }
        let next_step = step(s, current, pos, stop)^
        if ((next_step.pos <= pos and len(next_step.cursor.errors) == 0)^) {
            return failed(next_step, "unsupported path operator in TikZ geometry")^
        }
        current = next_step
    }
    return current
}

fn has_arc(s, first: int, stop: int) bool^ {
    var pos = first
    let arc = binary("arc")
    while (pos + 3 <= stop) {
        if (scan.starts(s, pos, arc)^ and (pos == first or not scan.ascii_alpha(scan.byte(s, pos - 1)^))
                and (pos + 3 == stop or not scan.ascii_alpha(scan.byte(s, pos + 3)^))) { return true }
        pos = pos + 1
    }
    return false
}

fn path_data(s, cursor, first: int, stop: int) map^ {
    let plot = lex.word(s, first, stop, "plot")^
    if (plot != null) {
        let opt = styles.read(s, scan.set_cursor(cursor, plot), false, true)^
        let data = plots.input(s, opt.cursor)^;
        return {cursor:data.cursor, attrs:{shape:"plot", *:opt.attrs, *:data.attrs}, children:[*opt.children,*data.children]}
    }
    let system = lex.word(s, first, stop, "l-system")^ or lex.word(s, first, stop, "lindenmayer system")^
    if (system != null) {
        let pos = lex.skip(s, system, stop)^
        let group = lex.balanced(s, pos, stop, 91, 93)^
        if (group == null) { return {cursor:lex.fail(cursor, cursor.pos, "l-system requires a bracketed specification"),
            attrs:{}, children:[]} }
        if (lex.skip(s, group.next, stop)^ != stop) {
            return {cursor:lex.fail(cursor, cursor.pos, "unexpected content after l-system"), attrs:{}, children:[]}
        }
        return {cursor:cursor, attrs:{shape:"l-system", system_source:lex.value(s, group)^}, children:[]}
    }
    if (has_arc(s, first, stop)^) { return {cursor:cursor, attrs:{shape:"arc-chain"}, children:[]} }
    let path = geometry(s, cursor, first, stop)^
    return {cursor:path.cursor, attrs:path.attrs, children:scan.finish(path.children)^}
}

pub fn read(s, cursor, action: string) map^ {
    let inherited = styles.inherited(cursor, "every path")^
    let opt = styles.read(s, inherited.cursor, false, true)^
    let first = opt.cursor.pos
    let stop = lex.statement_end(s, first, s.length)^
    if (stop == null) { return {cursor:lex.fail(opt.cursor, first, "missing command semicolon"), children:[]} }
    let data = path_data(s, scan.set_cursor(opt.cursor, stop + 1), first, stop)^
    let children = [*inherited.children, *opt.children, *data.children];
    {cursor:data.cursor, children:[<path action:action, *:opt.attrs, source:scan.text(s, first, stop)^,
        *:data.attrs, *children>]}
}
