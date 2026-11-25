import scan: lambda.io.source_scan
import lex: .lex
import styles: .styles
import coordinates: .coordinates

pub fn inline_node(s, cursor, start: int, limit: int, segment: bool = false) map^ {
    let directive = lex.word(s, start, limit, "node")^
    if (directive == null) { return {cursor:cursor, next:start, value:null} }
    let inherited = styles.inherited(cursor, "every node")^
    let pos = lex.skip(s, directive, limit)^
    let has_options = scan.byte(s, pos)^ == 91
    let group = if (has_options) lex.balanced(s, pos, limit, 91, 93)^ else null
    if (has_options and group == null) {
        return {cursor:lex.fail(inherited.cursor, cursor.pos, "unclosed inline node options"), next:pos, value:null}
    }
    let opts = if (has_options) styles.options(s, inherited.cursor, group.first, group.stop, false, true)^
        else {cursor:inherited.cursor, children:[]}
    let after = lex.skip(s, if (has_options) group.next else pos, limit)^
    let text = lex.balanced(s, after, limit, 123, 125)^
    if (text == null) {
        return {cursor:lex.fail(opts.cursor, cursor.pos, "inline node requires text"), next:after, value:null}
    }
    let entries = [*inherited.children, *opts.children];
    {cursor:opts.cursor, next:text.next, value:<node inline:true,
        *:if (segment) {segment:true} else {}, source:lex.value(s, text)^, *entries>}
}

pub fn terminator(s, cursor, name: string) map^ {
    let pos = lex.skip(s, cursor.pos, s.length)^
    let current = scan.byte(s, pos)^
    if (current == 59) { return scan.set_cursor(cursor, pos + 1) }
    return lex.fail(cursor, if (current == -1) pos else pos + 1, "expected " ++ name ++ " semicolon")
}

pub fn name_at(s, cursor, label: string) map^ {
    let group = lex.expected_group(s, cursor, 40, 41)^
    if (group.value == null) { return {cursor:group.cursor, id:null} }
    let span = lex.trimmed(s, group.value.first, group.value.stop)^
    if (not lex.literal_name(s, span.first, span.stop)^) {
        return {cursor:lex.fail(group.cursor, group.cursor.pos, "invalid " ++ label ++ " name"), id:null}
    }
    return {cursor:group.cursor, id:lex.value(s, span)^}
}

pub fn optional_position(s, cursor, message: string, required: bool = false) map^ {
    let pos = lex.skip(s, cursor.pos, s.length)^
    let at_word = lex.word(s, pos, s.length, "at")^
    if (at_word == null) {
        return if (required) {cursor:lex.fail(cursor, pos, "named coordinate requires at"), attrs:{}}
            else {cursor:scan.set_cursor(cursor, pos), attrs:{x:0.0, y:0.0, coord_system:null}}
    }
    let location = coordinates.position(s, at_word, s.length)^
    if (location == null) { return {cursor:lex.fail(cursor, at_word, message), attrs:{}} }
    return {cursor:scan.set_cursor(cursor, location.next), attrs:location.attrs}
}

pub fn read(s, cursor, named_coordinate: bool) map^ {
    let label = if (named_coordinate) "coordinate" else "node"
    let inherited = if (named_coordinate) {cursor:cursor,children:[]}
        else styles.inherited(cursor, "every node")^
    let opt = styles.read(s, inherited.cursor, false, true)^
    let pos = lex.skip(s, opt.cursor.pos, s.length)^
    let has_name = named_coordinate or scan.byte(s, pos)^ == 40
    let named = if (has_name) name_at(s, scan.set_cursor(opt.cursor, pos), label)^
        else {cursor:scan.set_cursor(opt.cursor, pos), id:null}
    if (len(named.cursor.errors)^ > 0) { return {cursor:named.cursor, children:[]} }
    let after = if (has_name and not named_coordinate) styles.read(s, named.cursor, false, true, false)^
        else {cursor:named.cursor, children:[]}
    let position = optional_position(s, after.cursor, if (named_coordinate)
        "expected named coordinate position" else "expected node coordinate", named_coordinate)^
    if (len(position.cursor.errors)^ > 0) { return {cursor:position.cursor, children:[]} }
    let text = if (named_coordinate) {cursor:position.cursor, value:null}
        else lex.expected_group(s, position.cursor, 123, 125)^
    if (len(text.cursor.errors)^ > 0) { return {cursor:text.cursor, children:[]} }
    let done = terminator(s, text.cursor, label)^
    let attrs = {*:opt.attrs, *:if (named.id == null) {} else {id:named.id},
        *:position.attrs, *:if (text.value == null) {} else {source:lex.value(s, text.value)^}}
    let options = [*inherited.children, *opt.children, *after.children];
    {cursor:done, children:[<[label] *:attrs, *options>]}
}
