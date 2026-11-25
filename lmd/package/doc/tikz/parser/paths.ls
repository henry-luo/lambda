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
        subpath:if (current.subpath == null or vertex.value.move == true) vertex else current.subpath,
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
    let radii = {rx:x.value, ry:y.value, rx_explicit:x.explicit, ry_explicit:y.explicit}
    // Each ellipse starts its own subpath at the current TikZ coordinate.
    let segment = <'ellipse_segment' *:radii, current.point.value>;
    return {*:current, pos:end_pos + 1, ellipses:scan.add(current.ellipses, segment)}
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
    let vertex = coords.vertex(s, after, stop, current.reference)^
    let rectangle = keyword == "rectangle"
    if (rectangle and (not current.cartesian or vertex == null or not vertex.cartesian)) {
        return failed(current, "rectangle requires a second coordinate")^
    }
    if (vertex == null) {
        return failed(failed(current, "expected finite Cartesian coordinate or named node")^,
            "grid requires a second coordinate")^
    }
    let pos = lex.skip(s, vertex.next, stop)^
    let single = current.children.count == 1
    let label = if (rectangle and single and lex.word(s, pos, stop, "node")^ != null)
        inline(s, current, pos, stop)^ else null
    if (not rectangle or (single and (pos == stop or (label != null and lex.skip(s, label.pos, stop)^ == stop)))) {
        let appended = at_vertex(current, vertex)^
        let compact = {*:appended, attrs:{*:appended.attrs, shape:keyword}}
        if (not rectangle and pos != stop) { return failed(compact, "grid must end after its second coordinate")^ }
        return if (label == null) compact else inline(s, compact, pos, stop)^
    }
    // Close the rectangle's own subpath, then resume from the opposite corner.
    let first = current.point
    let origin = coords.point(first.x, first.y, first.x_explicit == true, first.y_explicit == true, true)^
    let points = [origin,
        coords.point(first.x, vertex.y, first.x_explicit == true, vertex.y_explicit)^,
        vertex.value,
        coords.point(vertex.x, first.y, vertex.x_explicit, first.y_explicit == true)^,
        <point *:map(origin), move:false, close:true>]
    let result = {*:vertex, value:<point *:map(vertex.value), move:true>}
    let children = reduce([current.children, *points], scan.add)^
    let cursor = plots.add_points(current.cursor, 5)^
    return at_vertex({*:current, children:children, cursor:cursor}, result)^
}

fn line_segment(s, current, pos: int, stop: int, connector: string) map^ {
    let via = if (connector == "--") null else connector
    let attrs = if (via == null) current.attrs else {*:current.attrs,
        line_mode:if (via == "|-") "vertical-horizontal" else "horizontal-vertical"}
    let first = lex.skip(s, pos + 2, stop)^
    let cycle = if (via == null) lex.word(s, first, stop, "cycle")^ else null
    if (cycle != null) {
        let origin = current.subpath
        return at_vertex(current, {*:origin, next:cycle, value:<point *:map(origin.value), move:false, close:true>})^
    }
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
    let initial = {cursor:cursor, pos:begin, point:origin, reference:origin, cartesian:false, subpath:null,
        children:null, ellipses:null, attrs:{}}
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
    let vertices = scan.finish(path.children)^
    let ellipses = scan.finish(path.ellipses)^
    if (len(ellipses) == 0) { return {cursor:path.cursor, attrs:path.attrs, children:vertices} }
    if (len(ellipses) == 1 and len([for (child in vertices where ((name(child)^ == 'point') ^ { false })) child]) == 1) {
        return {cursor:path.cursor, attrs:{*:path.attrs, shape:"ellipse", *:map(ellipses[0])}, children:vertices}
    }
    return {cursor:path.cursor, attrs:{*:path.attrs, shape:"compound-ellipse"}, children:[*vertices, *ellipses]}
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
