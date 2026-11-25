import scan: lambda.io.source_scan
import lex: .parser.lex
import styles: .parser.styles
import nodes: .parser.nodes
import plots: .parser.plots
import paths: .parser.paths

let axis_names = ["axis","semilogxaxis","semilogyaxis","loglogaxis","polaraxis"]
let draw_commands = ["draw","path","fill","filldraw","clip"]

fn failed(st, message: string, pos = null) map^ =>
    {cursor:lex.fail(st, pos or st.pos, message), children:[]}

fn raw_pair(s, st, tag: string, first_key: string, second_key: string) map^ {
    let first = lex.expected_group(s, st, 123, 125)^
    if (first.value == null) { return {cursor:first.cursor, children:[]} }
    let second = lex.expected_group(s, first.cursor, 123, 125)^
    if (second.value == null) { return {cursor:second.cursor, children:[]} }
    let node = <[tag] [first_key]:lex.value(s, first.value)^, [second_key]:lex.value(s, second.value)^>
    return {cursor:second.cursor, children:[node]}
}

fn definition(s, st, kind: string) map^ {
    let pos = lex.skip(s, st.pos, s.length)^
    let control = lex.command(s, pos, s.length)^
    if (control == null) { return failed(st, "PGF definition requires a control sequence", pos)^ }
    let group = lex.expected_group(s, scan.set_cursor(st, control.next), 123, 125)^
    if (group.value == null) { return {cursor:group.cursor, children:[]} }
    return {cursor:group.cursor, children:[<pgf_definition kind:kind,
        variable:control.name, source:lex.value(s, group.value)^>]}
}

fn setmacro(s, st) map^ {
    let variable = lex.expected_group(s, st, 123, 125)^
    if (variable.value == null) { return {cursor:variable.cursor, children:[]} }
    let group = lex.expected_group(s, variable.cursor, 123, 125)^
    if (group.value == null) { return {cursor:group.cursor, children:[]} }
    let name = lex.trimmed(s, variable.value.first, variable.value.stop)^
    if (scan.byte(s, name.first)^ != 92 or not lex.literal_name(s, name.first + 1, name.stop)^) {
        return failed(group.cursor, "PGF math macro needs a control-sequence name")^
    }
    let pos = lex.skip(s, group.cursor.pos, s.length)^
    let stop = if (scan.byte(s, pos)^ == 59) pos + 1 else pos
    return {cursor:scan.set_cursor(group.cursor, stop), children:[<pgf_definition kind:"setmacro",
        variable:scan.text(s, name.first + 1, name.stop)^, source:lex.value(s, group.value)^>]}
}

fn raw_command(s, st, tag: string) map^ {
    let group = lex.expected_group(s, st, 123, 125)^
    if (group.value == null) { return {cursor:group.cursor, children:[]} }
    return {cursor:group.cursor, children:[<[tag] source:lex.value(s, group.value)^>]}
}

fn settings(s, st, defaults: bool) map^ {
    let group = lex.expected_group(s, st, 123, 125)^
    if (group.value == null) { return {cursor:group.cursor, children:[]} }
    let parsed = styles.options(s, group.cursor, group.value.first, group.value.stop, not defaults, false, defaults)^
    let entries = parsed.children
    let node = if (defaults) <pgfplots_setting *entries> else <tikzset source:lex.value(s, group.value)^, *entries>
    return {cursor:parsed.cursor, children:[node]}
}

fn bindings_end(s, start: int) map^ {
    var pos = start
    var count = 0
    while (true) {
        let next_pos = lex.skip(s, pos, s.length)^
        if (count == 4) { return {next:next_pos, complete:false} }
        let control = lex.command(s, next_pos, s.length)^
        if (control == null) { return {next:next_pos, complete:false} }
        pos = lex.skip(s, control.next, s.length)^
        if (scan.byte(s, pos)^ != 47) { return {next:pos, complete:true} }
        pos = pos + 1
        count = count + 1
    }
}

fn foreach_command(s, st) map^ {
    let pos = lex.skip(s, st.pos, s.length)^
    let bindings = bindings_end(s, pos)^
    if (not bindings.complete) {
        return failed(st, "foreach needs one to four command bindings", bindings.next)^
    }
    let after = lex.word(s, bindings.next, s.length, "in")^
    if (after == null) { return failed(st, "foreach bindings require in", bindings.next)^ }
    let items = lex.expected_group(s, scan.set_cursor(st, after), 123, 125)^
    if (items.value == null) { return {cursor:items.cursor, children:[]} }
    let first = lex.skip(s, items.cursor.pos, s.length)^
    let grouped = scan.byte(s, first)^ == 123
    let body = if (grouped) lex.expected_group(s, items.cursor, 123, 125)^ else {
        let command = scan.byte(s, first)^ == 92
        let stop = if (command) lex.statement_end(s, first, s.length)^ else null
        let cursor = if (command and stop == null) lex.fail(items.cursor, first, "missing command semicolon")
            else scan.set_cursor(items.cursor, if (stop == null) first else stop + 1);
        {cursor:cursor, value:if (stop == null) null else {first:first, stop:stop + 1}}
    }
    if (body.value == null) {
        return if (grouped) {cursor:body.cursor, children:[]}
            else failed(body.cursor, "foreach body requires a TikZ command or group")^
    }
    return {cursor:body.cursor, children:[<foreach bindings:scan.text(s, pos, bindings.next)^,
        items:lex.value(s, items.value)^, body:lex.value(s, body.value)^>]}
}

fn legend(s, st, multiple: bool) map^ {
    let group = lex.expected_group(s, st, 123, 125)^
    if (group.value == null) { return {cursor:group.cursor, children:[]} }
    if (not multiple) {
        return {cursor:group.cursor, children:[<legend_entry source:lex.value(s, group.value)^, from_list:false>]}
    }
    var entries = null
    var pos = group.value.first
    let limit = group.value.stop
    while ((pos <= limit)^) {
        let stop = lex.separator(s, pos, limit, 44, false, false, true)^
        let label = lex.trimmed(s, pos, stop)^
        if (label.first == label.stop) { return failed(group.cursor, "empty plot legend entry")^ }
        entries = scan.add(entries, <legend_entry source:lex.value(s, label)^, from_list:true>)
        pos = stop + 1
    }
    return {cursor:group.cursor, children:scan.finish(entries)^}
}

fn pic(s, initial, depth: int, command_at: int) map^ {
    let opt = styles.read(s, initial, false, true)^
    let pos = lex.skip(s, opt.cursor.pos, s.length)^
    let name = if (scan.byte(s, pos)^ == 40) nodes.name_at(s, opt.cursor, "pic")^
        else {cursor:scan.set_cursor(opt.cursor, pos), id:null}
    if (len(name.cursor.errors)^ > 0) { return {cursor:name.cursor, children:[]} }
    let location = nodes.optional_position(s, name.cursor, "expected pic coordinate")^
    if (len(location.cursor.errors)^ > 0) { return {cursor:location.cursor, children:[]} }
    let group = lex.expected_group(s, location.cursor, 123, 125)^
    if (group.value == null) { return {cursor:group.cursor, children:[]} }
    let raw = lex.value(s, group.value)^
    let equal = index_of(raw, "=")
    let kind = trim(if (equal == null) raw else slice(raw, 0, equal) or "")
    let argument = if (equal == null) null else trim(slice(raw, equal + 1, len(raw)) or "")
    let handler = styles.find_handler(group.cursor, kind, "pic")^
    if (handler == null) { return failed(group.cursor, "unknown TikZ pic")^ }
    let end_pos = nodes.terminator(s, group.cursor, "pic")^
    if (len(end_pos.errors)^ > 0) { return {cursor:end_pos, children:[]} }
    let expanded = styles.substitute(end_pos, handler.value, if (argument == null) [] else [argument], 1)^
    if (expanded.value == null) { return {cursor:expanded.cursor, children:[]} }
    let source = scan.source(binary(expanded.value)^)^
    let anchor = end_pos.anchor or command_at
    let parsed = commands(source, {*:end_pos, pos:0, anchor:anchor}, null, depth + 1, false)^
    let cursor = {*:parsed.cursor, pos:end_pos.pos, styles:end_pos.styles, anchor:end_pos.anchor}
    let body = [*opt.children,*parsed.children];
    {cursor:cursor, children:[<pic *:opt.attrs, *:if (name.id == null) {} else {id:name.id},
        *:location.attrs, name:kind, *:if (argument == null) {} else {argument:argument}, *body>]}
}

fn axis_defaults(st) map^ {
    var entries = null
    var cursor = st
    let defaults = st.defaults
    var idx = 0
    while ((idx < len(defaults))^) {
        let bytes = binary(defaults[idx])^
        let parsed = styles.options(scan.source(bytes)^, cursor, 0, len(bytes))^
        cursor = parsed.cursor
        entries = scan.add(entries, parsed.children)
        idx = idx + 1
    }
    let batches = scan.finish(entries)^;
    {cursor:cursor, children:[for (batch in batches, entry in batch) entry]}
}

fn groupplot(s, initial, depth: int) map^ {
    let opt = styles.read(s, initial)^
    var entries = null
    var st = opt.cursor
    var group_count = 0
    while ((len(st.errors) == 0)^) {
        let pos = lex.skip(s, st.pos, s.length)^
        let control = lex.command(s, pos, s.length)^
        if (control == null or control.name != "nextgroupplot") {
            return failed(st, "groupplot requires \\nextgroupplot before its plots", pos)^
        }
        group_count = group_count + 1
        if (group_count > 64) { return failed(st, "groupplot exceeds 64 plots", control.next)^ }
        let defaults = axis_defaults(scan.set_cursor(st, control.next))^
        let axis = styles.read(s, defaults.cursor)^
        let parsed = commands(s, axis.cursor, "groupplot", depth + 1, true)^
        st = parsed.cursor
        let children = [*defaults.children,*opt.children,*axis.children,*parsed.children]
        entries = scan.add(entries, <axis *:axis.attrs, *children>)
        if (parsed.ended == true) { break }
    }
    let children = [*opt.children, *scan.finish(entries)^];
    {cursor:{*:st, styles:initial.styles}, children:[<groupplot *:opt.attrs, *children>]}
}

fn environment(s, st, tag: string, depth: int) map^ {
    if (tag == "groupplot") { return groupplot(s, st, depth)^ }
    let axis = tag in axis_names
    if (not axis and not (tag in ["tikzpicture", "scope"])) { return failed(st, "unsupported environment")^ }
    let defaults = if (axis) axis_defaults(st)^ else {cursor:st, children:[]}
    let opt = styles.read(s, defaults.cursor, not axis, tag == "scope")^
    let parsed = commands(s, opt.cursor, tag, depth + 1, false)^
    let children = [*defaults.children,*opt.children,*parsed.children];
    {cursor:{*:parsed.cursor, styles:st.styles}, children:[<[tag] *:opt.attrs, *children>]}
}

fn dispatch(s, st, keyword: string, closing: string?, depth: int, command_at: int) map^ {
    if (keyword == "addplot") { return plots.read(s, st)^ }
    if (keyword in ["addlegendentry","legend"]) { return legend(s, st, keyword == "legend")^ }
    if (keyword in draw_commands) { return paths.read(s, st, keyword)^ }
    if (keyword in ["node","coordinate"]) { return nodes.read(s, st, keyword == "coordinate")^ }
    if (keyword == "pic") { return pic(s, st, depth, command_at)^ }
    if (keyword == "tikzset") { return settings(s, st, false)^ }
    if (keyword == "foreach") { return foreach_command(s, st)^ }
    if (keyword in ["def","xdef"]) { return definition(s, st, keyword)^ }
    if (keyword == "pgfmathsetmacro") { return setmacro(s, st)^ }
    if (keyword == "tikzmath") { return raw_command(s, st, "tikzmath")^ }
    if (keyword == "pgfplotsinvokeforeach") { return raw_pair(s, st, "pgfplots_foreach", "range", "body")^ }
    if (closing == null and st.anchor == null) {
        if (keyword == "usepgfplotslibrary") { return raw_command(s, st, "pgfplots_library")^ }
        if (keyword == "pgfplotsset") { return settings(s, st, true)^ }
    }
    return failed(st, "unsupported TikZ command")^
}

fn commands(s, initial, closing: string?, depth: int, group_axis: bool) map^ {
    if (depth > 64) { return failed(initial, "picture nesting exceeds 64 scopes")^ }
    var st = initial
    var entries = null
    var ended = false
    while ((st.pos < s.length and len(st.errors) == 0)^) {
        let pos = lex.skip(s, st.pos, s.length)^
        st = scan.set_cursor(st, pos)
        if (scan.byte(s, pos)^ == -1) { break }
        st = {*:st, commands:st.commands + 1}
        if ((st.commands > 8192)^) {
            st = lex.fail(st, pos, "picture exceeds the 8192 command limit")
            break
        }
        let tok = lex.command(s, pos, s.length)^
        if (tok == null) {
            st = lex.fail(st, pos, "expected TikZ command")
            break
        }
        st = scan.set_cursor(st, tok.next)
        if (tok.name in ["begin", "end"]) {
            let group = lex.expected_group(s, st, 123, 125)^
            st = group.cursor
            if (group.value == null) { break }
            if (group.value.stop - group.value.first >= 96) {
                st = lex.fail(st, st.pos, "environment name too long")
                break
            }
            let env = lex.value(s, group.value)^
            if (tok.name == "end") {
                if (env != closing) { st = lex.fail(st, st.pos, "mismatched environment end") }
                ended = true
                break
            }
            let parsed = environment(s, st, env, depth)^
            st = parsed.cursor
            if ((len(st.errors) == 0)^) { entries = scan.add(entries, parsed.children) }
        } else if (group_axis and tok.name == "nextgroupplot") {
            let batches = scan.finish(entries)^;
            return {cursor:scan.set_cursor(st, pos), children:[for (batch in batches, node in batch) node], ended:false}
        } else {
            let parsed = dispatch(s, st, tok.name, closing, depth, pos)^
            st = parsed.cursor
            if ((len(st.errors) == 0)^) { entries = scan.add(entries, parsed.children) }
        }
    }
    if ((not ended and closing != null and len(st.errors) == 0)^) {
        st = lex.fail(st, st.pos, "missing environment end")
    }
    let batches = scan.finish(entries)^;
    {cursor:st, children:[for (batch in batches, node in batch) node], ended:ended}
}

pub fn read(bytes: binary?) map^ {
    let st = {pos:0, styles:[], defaults:[], points:0, commands:0, anchor:null, errors:[]}
    let length = len(bytes)
    let parsed = if (length > 1024 * 1024) failed(st, "source exceeds the 1 MiB picture limit")^
        else commands(scan.source(bytes)^, st, null, 0, false)^
    let children = parsed.children;
    {root:<tikz_picture *children>, errors:parsed.cursor.errors}
}
