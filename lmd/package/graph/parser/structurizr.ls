import scan: lambda.io.source_scan

let comments = [binary("//"), binary("#")]
let max_depth = 256
let max_statements = 100000
let max_token = 1024 * 1024

fn skip(s, pos) int^ => scan.trivia(s, pos, comments, true)^

fn bare_ident(b: int) bool => scan.ascii_alpha(b) or b == 95

fn archetype_length(s, pos: int) int^ {
    if (scan.byte(s, pos)^ != 45 or scan.byte(s, pos + 1)^ != 45) { return 0 }
    if (not bare_ident(scan.byte(s, pos + 2)^)) { return 0 }
    var stop = pos + 3
    while (true) {
        let b = scan.byte(s, stop)^
        if (not (bare_ident(b) or scan.digit(b))) { break }
        stop = stop + 1
    }
    return if (scan.byte(s, stop)^ == 45 and scan.byte(s, stop + 1)^ == 62) stop - pos - 2 else 0
}

fn arrow(s, pos: int) bool^ => scan.byte(s, pos)^ == 45 and scan.byte(s, pos + 1)^ == 62

fn token(kind: string, value: string, start: int, stop: int) map^ =>
    {kind:kind, value:scan.c_string(value), start:start, stop:stop}

fn quoted_token(s, start: int) map^ {
    let parsed = scan.quoted(s, start, scan.standard_escape)^
    if (parsed.issue != null) {
        let problem = scan.problem(parsed.issue.offset, parsed.issue.message);
        return {value:null, next:parsed.next, problem:problem}
    }
    return {value:token("quoted", parsed.value, start, parsed.next)^, next:parsed.next}
}

fn bare_token(s, start: int) map^ {
    var pos = start
    while (pos - start < max_token) {
        let b = scan.byte(s, pos)^
        if (b == -1 or scan.space(b) or b in [123, 125, 59, 34] or arrow(s, pos)^ or
                (b == 45 and archetype_length(s, pos)^ > 0)) { break }
        if (b == 61 and scan.byte(s, pos + 1)^ != 61 and
                (pos == start or scan.byte(s, pos - 1)^ != 61)) { break }
        pos = pos + 1
    }
    return {value:if (pos == start) null else token("bare", scan.text(s, start, pos)^, start, pos)^,
        next:pos, problem:if (pos - start == max_token)
            {offset:start, code:"structurizr.token-limit", message:"Structurizr token exceeds the maximum length"}
            else null}
}

fn read_token(s, pos: int) map^ {
    let b = scan.byte(s, pos)^
    if (b == 34) { return quoted_token(s, pos)^ }
    if (arrow(s, pos)^) { return {value:token("operator", "->", pos, pos + 2)^, next:pos + 2} }
    let length = if (b == 45) archetype_length(s, pos)^ else 0
    if (length > 0) {
        let stop = pos + length + 4
        let value = scan.text(s, pos + 2, stop - 2)^;
        return {value:token("relationship-archetype", value, pos, stop)^, next:stop}
    }
    if (b == 61 and scan.byte(s, pos + 1)^ != 61) {
        return {value:token("operator", "=", pos, pos + 1)^, next:pos + 1}
    }
    return bare_token(s, pos)^
}

fn line_tokens(s, initial) map^ {
    var parsed_state = initial
    var pos = initial.pos
    var tokens = null
    var block = false
    while ((pos < s.length)^) {
        pos = scan.inline_space(s, pos)^
        let b = scan.byte(s, pos)^
        if (((b == 47 and scan.byte(s, pos + 1)^ == 47) or (b == 35 and tokens == null))^) {
            pos = scan.line_end(s, pos)^
            break
        }
        if (b in [10, 59, 123]) {
            block = b == 123
            pos = pos + 1
            break
        }
        if (b == 125) { break }
        let parsed = read_token(s, pos)^
        pos = parsed.next
        if (parsed.problem != null) {
            let problem = parsed.problem
            parsed_state = scan.issue(s, parsed_state, problem.offset, problem.code or "structurizr.syntax",
                problem.message)^
        }
        if (parsed.value == null) {
            parsed_state = scan.issue(s, parsed_state, pos, "structurizr.syntax",
                "Unable to read Structurizr statement token")^
            pos = scan.line_end(s, pos)^
            break
        }
        tokens = scan.add(tokens, parsed.value)
    }
    return {parsed_state:scan.set_cursor(parsed_state, pos), tokens:scan.finish(tokens)^, block:block}
}

let declaration_keywords = ["person", "softwareSystem", "container", "component", "element",
    "group", "deploymentEnvironment", "deploymentGroup", "deploymentNode", "infrastructureNode",
    "softwareSystemInstance", "containerInstance", "instanceOf", "archetype", "relationship"]
let view_keywords = ["systemLandscape", "systemContext", "container", "component", "filtered",
    "dynamic", "deployment", "custom", "image"]
let required_blocks = ["workspace", "model", "views", "styles", "archetypes"]
let regular_tags = ["include", "exclude", "properties", "perspectives", "animation"]

fn tag_for(parent, keyword, relationship) string^ {
    if (parent == "archetypes") "archetype"
    else if (relationship) "relationship"
    else if (keyword in required_blocks or keyword in regular_tags) keyword
    else if (parent == "views" and keyword in view_keywords) "view"
    else if (parent == "styles" and keyword in ["element", "relationship"]) "style-rule"
    else if (keyword in declaration_keywords) "declaration"
    else if (keyword == "autoLayout") "auto-layout"
    else "statement"
}

fn child_context(tag: string, keyword: string) string^ => if (tag == "style-rule") "style"
    else if (tag == "declaration") {
        if (contains(keyword, "deployment") or contains(keyword, "Instance")) "deployment" else "element"
    } else if (tag == "view" or tag in required_blocks) tag else "generic"

fn block(s, parsed_state, parent, depth: int, start: int) map^ {
    if (depth >= max_depth) {
        let failed = scan.issue(s, parsed_state, start, "structurizr.depth-limit",
            "Structurizr block nesting limit exceeded")^;
        return {parsed_state:scan.set_cursor(failed, scan.brace_end(s, failed.pos)^), children:[]}
    }
    let body = statements(s, parsed_state, parent, depth + 1)^
    let pos = skip(s, body.parsed_state.pos)^
    let next_state = if (scan.byte(s, pos)^ == 125) scan.set_cursor(body.parsed_state, pos + 1)
        else scan.issue(s, scan.set_cursor(body.parsed_state, pos), pos,
            "structurizr.missing-close", "Expected '}' to close Structurizr block")^;
    {parsed_state:next_state, children:body.children}
}

fn argument_list(s, tokens, start: int) array^ {
    let trailing = slice(tokens, start, len(tokens)) or []
    let values = [for (tok in trailing where tok.value != "=") tok];
    [for (i,tok in values)
        <argument index:i, kind:tok.kind, value:tok.value, *:scan.span(s, tok.start, tok.stop)^>]
}

fn relation_fields(tokens, first: int, arrow_idx: int) map^ {
    let operator = tokens[arrow_idx]
    let order = if (arrow_idx > first + 1) tokens[first].value else ""
    let preceding = if (len(order)^ > 1 and ends_with(order, ":")) {order:slice(order, 0, len(order)^ - 1)} else {};
    {*:if (operator.kind == "relationship-archetype") {archetype:operator.value} else {},
     *:preceding, from:if (arrow_idx > first) tokens[arrow_idx - 1].value else "this",
     to:tokens[arrow_idx + 1].value or ""}
}

fn classify(s, tokens, parent) map^ {
    let assigned = len(tokens) > 2 and tokens[1].value == "="
    let first = if (assigned) 2 else 0
    let arrows = [for (i,tok in tokens where ((i >= first and
        (tok.value == "->" or tok.kind == "relationship-archetype"))^)) i]
    let arrow_idx = arrows[0]
    let relationship = arrow_idx != null
    let keyword = if (relationship) "relationship" else tokens[first].value
    let tag = tag_for(parent, keyword, relationship)^
    let base = if (relationship) tokens[arrow_idx].value else tokens[first].value
    let specific = if (tag == "workspace") {flavor:"structurizr", 'ir-stage':"source"}
        else if (tag == "view") {kind:keyword}
        else if (tag == "style-rule") {'target-kind':keyword}
        else if (tag == "archetype") {'target-kind':if (relationship) "relationship" else "element",
            base:if (base == "->") "relationship" else base}
        else if (relationship) relation_fields(tokens, first, arrow_idx)^ else {}
    let arg_at = if (relationship) arrow_idx + 2 else first + 1;
    {tag:tag, keyword:keyword, attrs:{*:if (assigned) {identifier:tokens[0].value} else {}, *:specific},
     arguments:argument_list(s, tokens, arg_at)^, context:child_context(tag, keyword)^}
}

fn statement(s, initial, parent, depth: int) map^ {
    let start = initial.pos
    let line = line_tokens(s, initial)^
    let no_tokens = len(line.tokens)^ == 0
    if (no_tokens and not line.block) { return {parsed_state:line.parsed_state, value:null} }
    let idx = line.parsed_state.statement_index
    if ((idx >= max_statements)^) {
        return {parsed_state:scan.issue(s, line.parsed_state, start, "structurizr.statement-limit",
            "Structurizr statement limit exceeded")^, value:null}
    }
    let anonymous = if (parent == "view") "parallel" else "block"
    let info = if (no_tokens) {tag:anonymous, keyword:anonymous, attrs:{}, arguments:[], context:parent}
        else classify(s, line.tokens, parent)^
    let next_state = {*:line.parsed_state, statement_index:idx + 1}
    let body = if (line.block) block(s, next_state, info.context, depth, start)^ else {
        let following = if (info.keyword in required_blocks) scan.issue(s, next_state, start,
            "structurizr.missing-block", "Structurizr '" ++ info.keyword ++ "' statement requires a block")^
            else next_state;
        {parsed_state:following, children:[]}
    }
    let tag = info.tag
    let children = [*info.arguments, *body.children];
    {parsed_state:body.parsed_state, value:<[tag] keyword:info.keyword, 'statement-index':idx,
        *:info.attrs, *:scan.span(s, start, body.parsed_state.pos)^, *children>}
}

fn statements(s, initial, parent, depth: int) map^ {
    var parsed_state = initial
    var children = null
    while ((parsed_state.pos < s.length and (parsed_state.errors.count or 0) < 100)^) {
        let pos = skip(s, parsed_state.pos)^
        parsed_state = scan.set_cursor(parsed_state, pos)
        if (scan.byte(s, pos)^ in [-1, 125]) { break }
        let value = statement(s, parsed_state, parent, depth)^
        parsed_state = value.parsed_state
        if (value.value != null) { children = scan.add(children, value.value) }
        if ((parsed_state.pos == pos)^) {
            let failed = scan.issue(s, parsed_state, pos, "structurizr.no-progress",
                "Unable to recover Structurizr statement")^
            parsed_state = scan.set_cursor(failed, pos + 1)
        }
    }
    return {parsed_state:parsed_state, children:scan.finish(children)^}
}

fn attach_diagnostics(root: element, issues) element^ {
    if (issues == null) { return root }
    let diagnostics = scan.finish(issues)^
    let children = [*content(root)^, <diagnostics *diagnostics>];
    <[name(root)] *:map(root), *children>
}

fn workspace(s) element^ {
    let start = skip(s, 0)^
    let parsed = statement(s, {pos:start, statement_index:0, errors:null}, "root", 0)^
    if (parsed.value != null and name(parsed.value) == 'workspace') {
        let pos = skip(s, parsed.parsed_state.pos)^
        let trailing = if (pos < s.length) scan.issue(s, parsed.parsed_state, pos,
            "structurizr.trailing-source", "Unexpected source after Structurizr workspace")^ else parsed.parsed_state
        return attach_diagnostics(parsed.value, trailing.errors)^
    }
    let failed = scan.issue(s, parsed.parsed_state, start, "structurizr.workspace",
        "Structurizr source must start with a workspace block")^
    let body = statements(s, failed, "workspace", 0)^
    let children = if (parsed.value == null) body.children else [parsed.value, *body.children]
    let root = <workspace flavor:"structurizr", 'ir-stage':"source",
        *:scan.span(s, start, body.parsed_state.pos)^, *children>
    return attach_diagnostics(root, body.parsed_state.errors)^
}

pub fn read(bytes: binary?) map^ {
    let s = scan.source(bytes)^;
    {root:if (s.length == 0) null else workspace(s)^, errors:[]}
}

pub fn fragment(bytes: binary?, parent: string) map^ {
    let s = scan.source(bytes)^
    let body = statements(s, {pos:0, statement_index:0, errors:null}, parent, 0)^
    let children = body.children
    let root = <fragment *children>;
    {root:attach_diagnostics(root, body.parsed_state.errors)^, errors:[]}
}
