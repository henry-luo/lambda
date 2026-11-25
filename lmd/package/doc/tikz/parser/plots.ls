import scan: lambda.io.source_scan
import lex: .lex
import expr: lambda.doc.tikz.parse_expression
import coords: .coordinates
import styles: .styles
import nodes: .nodes

pub fn add_points(st, count: int) map^ =>
    if ((st.points + count > 10000)^) lex.fail(st, st.pos, "picture exceeds the 10000 point limit")
    else {*:st, points:st.points + count}

fn points(s, initial, span) map^ {
    var pos = span.first
    let limit = span.stop
    var entries = null
    var st = initial
    while ((pos < limit)^) {
        pos = lex.skip(s, pos, limit)^
        if ((pos == limit)^) { break }
        st = add_points(st, 1)^
        if ((len(st.errors) > 0)^) { return {cursor:st, children:[]} }
        let literal = coords.cartesian(s, pos, limit, false)^
        let alt = if (literal == null) lex.balanced(s, pos, limit, 40, 41)^ else null
        let pair = if (alt == null) null else coords.pair(s, alt.first, alt.stop)^
        if (literal == null and pair == null) {
            return {cursor:lex.fail(st, st.pos, "expected finite Cartesian coordinate"), children:[]}
        }
        let attrs = if (literal == null) {x_source:lex.value(s, pair.x)^, y_source:lex.value(s, pair.y)^}
            else {x:literal.x, y:literal.y}
        pos = lex.skip(s, if (literal == null) alt.next else literal.next, limit)^
        let op = scan.text(s, pos, min(pos + 2, limit))^
        let has_error = op in ["+-","+=","-="]
        let errors = if (has_error) coords.cartesian(s, pos + 2, limit, false)^ else null
        if (has_error and errors == null) {
            return {cursor:lex.fail(st, st.pos, "error bar offsets require a numeric (x,y) pair"), children:[]}
        }
        if (errors != null) { pos = errors.next }
        entries = scan.add(entries, <point *:attrs,
            *:if (op in ["+-","+="]) {error_plus_x:errors.x, error_plus_y:errors.y} else {},
            *:if (op in ["+-","-="]) {error_minus_x:errors.x, error_minus_y:errors.y} else {}>)
    }
    if (entries == null) { return {cursor:lex.fail(st, st.pos, "empty coordinate list"), children:[]} }
    return {cursor:st, children:scan.finish(entries)^}
}

fn piece(s, st, span) map^ {
    let bytes = if (span.stop == span.first) null else slice(s.bytes, span.first, span.stop)
    let parsed = expr.read(bytes)^
    let errors = [for (problem in parsed.errors)
        scan.problem(lex.located(st, span.first + problem.offset), "tikz: " ++ problem.message)];
    {cursor:{*:st, errors:[*st.errors, *errors]}, value:parsed.root}
}

fn unwrap(s, span) map^ =>
    if (span.stop > span.first + 1 and scan.byte(s, span.first)^ == 123 and scan.byte(s, span.stop - 1)^ == 125)
        {first:span.first + 1, stop:span.stop - 1}
    else span

fn plot_shape(s, initial) map^ {
    let pos = lex.skip(s, initial.pos, s.length)^
    let st = scan.set_cursor(initial, pos)
    let coordinates = lex.word(s, pos, s.length, "coordinates")^
    if (coordinates != null) {
        let group = lex.expected_group(s, scan.set_cursor(st, coordinates), 123, 125)^
        let values = if (group.value == null) {cursor:group.cursor, children:[]} else points(s, group.cursor, group.value)^
        return {*:values, attrs:{input_kind:"coordinates"}}
    }
    let table = lex.word(s, pos, s.length, "table")^
    if (table != null) {
        let opt = styles.read(s, scan.set_cursor(st, table), false, false, false)^
        let group = lex.expected_group(s, opt.cursor, 123, 125)^
        if (group.value == null) { return {cursor:group.cursor, children:[], attrs:{}} }
        let entries = opt.children
        return {cursor:group.cursor, attrs:{input_kind:"table", table_source:lex.value(s, group.value)^,
            table_offset:lex.located(st, group.value.first)}, children:[<table_options *entries>]}
    }
    let fill = lex.word(s, pos, s.length, "fill")^
    if (fill != null) {
        let before = lex.skip(s, fill, s.length)^
        let between = lex.word(s, before, s.length, "between")^
        if (between == null) {
            return {cursor:lex.fail(st, before, "expected fill between"), children:[], attrs:{}}
        }
        let opt = styles.read(s, scan.set_cursor(st, between), false, false, false)^
        let entries = opt.children
        return {cursor:opt.cursor, attrs:{input_kind:"fill_between"}, children:[<fill_between *entries>]}
    }
    if (scan.byte(s, pos)^ == 40) {
        let group = lex.expected_group(s, st, 40, 41)^
        if (group.value == null) { return {cursor:group.cursor, attrs:{}, children:[]} }
        let pair = coords.pair(s, group.value.first, group.value.stop, true)^
        if (pair == null) {
            return {cursor:lex.fail(group.cursor, group.cursor.pos, "parametric plot requires x and y expressions"),
                attrs:{}, children:[]}
        }
        let x = piece(s, group.cursor, unwrap(s, pair.x)^)^
        if (x.value == null) { return {cursor:x.cursor, attrs:{}, children:[]} }
        let y = piece(s, x.cursor, unwrap(s, pair.y)^)^
        if (y.value == null) { return {cursor:y.cursor, attrs:{}, children:[]} }
        return {cursor:y.cursor, attrs:{input_kind:"parametric"},
            children:[<x_expression x.value>,<y_expression y.value>]}
    }
    let group = lex.expected_group(s, st, 123, 125)^
    if (group.value == null) { return {cursor:group.cursor, attrs:{}, children:[]} }
    let parsed = piece(s, group.cursor, group.value)^
    return {cursor:parsed.cursor, attrs:{input_kind:"expression"},
        children:if (parsed.value == null) [] else [parsed.value]}
}

pub fn input(s, st) map^ {
    let shape = plot_shape(s, st)^
    if (len(shape.cursor.errors)^ > 0) { return shape }
    let pos = lex.skip(s, shape.cursor.pos, s.length)^
    let inline = if (scan.starts(s, pos, binary("node")^)^) nodes.inline_node(s, shape.cursor, pos, s.length)^
        else {cursor:shape.cursor, next:pos, value:null}
    if (len(inline.cursor.errors)^ > 0) { return {cursor:inline.cursor, children:[], attrs:{}} }
    let after = lex.skip(s, inline.next, s.length)^
    let cycle = lex.word(s, after, s.length, "\\closedcycle")^
    let stop = if (cycle == null) after else cycle
    let result = nodes.terminator(s, scan.set_cursor(inline.cursor, stop), "plot")^;
    {cursor:result, attrs:{*:shape.attrs, *:if (cycle == null) {} else {closed_cycle:true}},
        children:[*shape.children, if (inline.value == null) null else inline.value]}
}

pub fn read(s, st) map^ {
    let append_cycle = scan.byte(s, st.pos)^ == 43
    let inherited = styles.inherited(scan.set_cursor(st, if (append_cycle) st.pos + 1 else st.pos), "every plot")^
    let opt = styles.read(s, inherited.cursor, false, true)^
    let data = input(s, opt.cursor)^
    let children = [*inherited.children, *opt.children, *data.children];
    {cursor:data.cursor, children:[<plot append_cycle:append_cycle, *:opt.attrs, *:data.attrs, *children>]}
}
