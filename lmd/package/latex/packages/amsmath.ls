// The document adapter reuses the script math renderer's bounded AST profile.
import util: ~~.util
import math: lambda.doc.math.math

let ENVIRONMENTS = ["equation*", "align", "align*", "alignat", "alignat*",
    "aligned", "gather", "gather*", "multline", "multline*", "split",
    "IEEEeqnarray", "IEEEeqnarray*"]

pub fn is_environment(tag) => any([for (name in ENVIRONMENTS) tag == name])

fn collect_operators(node, scoped) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let nested_scope = scoped or tag == "curly_group" or tag == "group" or tag == "text_group"
        let own = if (tag == "DeclareMathOperator" or tag == "DeclareMathOperator*") {
            let raw_name = util.raw_argument(node, "required", 0)
            let raw_text = util.raw_argument(node, "required", 1);
            [{name: if (raw_name != null) trim(raw_name) else "",
              text: if (raw_text != null) trim(raw_text) else "",
              starred: tag == "DeclareMathOperator*" or node.starred == true, scoped: scoped,
              offset: node.source_offset}]
        } else [];
        own ++ [for (child in node, entry in collect_operators(child, nested_scope)) entry]
    }
}

pub fn operators(ast) {
    fold_operators(collect_operators(ast, false), 0, [], [])
}

fn fold_operators(declared, i, definitions, issues) {
    if (i >= len(declared)) {definitions: definitions, diagnostics: issues}
    else {
        let entry = declared[i]
        let valid_name = starts_with(entry.name, "\\") and len(entry.name) > 1 and
            command_end(entry.name, 1) == len(entry.name)
        let prior = util.lookup(definitions, entry.name)
        let code = if (entry.starred) "unsupported-operator-limits"
            else if (entry.scoped) "unsupported-operator-scope"
            else if (not valid_name or entry.text == "") "invalid-operator"
            else if (prior != null) "duplicate-operator" else null
        let next_defs = if (code == null) definitions ++
            [{key: entry.name, val: entry.text, offset: entry.offset}] else definitions
        let next_issues = if (code == null) issues else issues ++
            [util.diagnostic(code, "amsmath", entry.name,
                "Cannot declare math operator " ++ entry.name ++ ": " ++ code,
                entry.offset)]
        fold_operators(declared, i + 1, next_defs, next_issues)
    }
}

fn command_letter(ch) {
    index_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", ch) != null
}

fn command_end(source, i) {
    if (i >= len(source) or not command_letter(slice(source, i, i + 1))) i
    else command_end(source, i + 1)
}

fn expand_operators(source, definitions, i, result) {
    if (i >= len(source)) result
    else if (slice(source, i, i + 1) != "\\")
        expand_operators(source, definitions, i + 1,
            result ++ slice(source, i, i + 1))
    else {
        let finish = command_end(source, i + 1)
        let command = slice(source, i, finish)
        let declared = util.lookup(definitions, command)
        let replacement = if (declared != null) "\\operatorname{" ++ declared ++ "}" else command
        expand_operators(source, definitions, finish, result ++ replacement)
    }
}

fn split_tex(source, rows, i, depth, escaped, start, acc) {
    if (i >= len(source)) acc ++ [slice(source, start, len(source))]
    else {
        let ch = slice(source, i, i + 1)
        let pair = slice(source, i, i + 2)
        if (escaped) split_tex(source, rows, i + 1, depth, false, start, acc)
        else if (rows and depth == 0 and pair == "\\\\")
            split_tex(source, rows, i + 2, depth, false, i + 2,
                acc ++ [slice(source, start, i)])
        else if (ch == "\\") split_tex(source, rows, i + 1, depth, true, start, acc)
        else if (ch == "{") split_tex(source, rows, i + 1, depth + 1, false, start, acc)
        else if (ch == "}") split_tex(source, rows, i + 1, depth - 1, false, start, acc)
        else if (not rows and depth == 0 and ch == "&")
            split_tex(source, rows, i + 1, depth, false, i + 1,
                acc ++ [slice(source, start, i)])
        else split_tex(source, rows, i + 1, depth, false, start, acc)
    }
}

fn math_cell(source, display, operators, offset) {
    let expression = expand_operators(trim(source), operators, 0, "")
    let ast = if (expression == "") null else parse(expression, {type: "math", flavor: "latex"}) ^ { null }
    if (expression == "") ""
    else if (ast == null) util.unsupported_element("amsmath",
        "Cannot parse aligned math cell: " ++ expression, offset)
    else if (display) math.render_display(ast)
    else math.render_inline(ast)
}

fn alignment_row(source, gathered, multline_align, anchor, display, bare_tag, operators, offset) {
    let cells = if (gathered) [source] else split_tex(source, false, 0, 0, false, 0, []);
    <div class: "latex-align-row", id: anchor,
        for (i, cell in cells)
            <span class: "latex-align-cell",
                style: if (multline_align != null) "text-align:" ++ multline_align
                    else if (gathered) "text-align:center" else if (i % 2 == 0)
                    "text-align:right" else "text-align:left",
                math_cell(cell, false, operators, offset)>
        if (display != null) {
            <span class: "latex-align-number",
                if (bare_tag) display else "(" ++ display ++ ")">
        }
    >
}

let ANNOTATIONS = [
    {prefix: "\\tag*{", kind: "bare-tag", value: true},
    {prefix: "\\tag{", kind: "tag", value: true},
    {prefix: "\\label{", kind: "label", value: true},
    {prefix: "\\notag", kind: "suppress", value: false},
    {prefix: "\\nonumber", kind: "suppress", value: false}
]

fn annotation_at(source, i, candidate) {
    if (candidate >= len(ANNOTATIONS)) null
    else {
        let annotation = ANNOTATIONS[candidate]
        if (starts_with(slice(source, i, len(source)), annotation.prefix)) annotation
        else annotation_at(source, i, candidate + 1)
    }
}

fn brace_close(source, i, depth, escaped) {
    if (i >= len(source)) null
    else {
        let ch = slice(source, i, i + 1)
        if (escaped) brace_close(source, i + 1, depth, false)
        else if (ch == "\\") brace_close(source, i + 1, depth, true)
        else if (ch == "{") brace_close(source, i + 1, depth + 1, false)
        else if (ch == "}") {
            if (depth == 1) i else brace_close(source, i + 1, depth - 1, false)
        } else brace_close(source, i + 1, depth, false)
    }
}

fn strip_annotations(source, i, depth, st) {
    if (i >= len(source)) {*:st, math: trim(st.math)}
    else {
        let ch = slice(source, i, i + 1)
        let annotation = if (depth == 0) annotation_at(source, i, 0) else null
        let escaped_brace = ch == "\\" and i + 1 < len(source) and
            (slice(source, i + 1, i + 2) == "{" or slice(source, i + 1, i + 2) == "}")
        if (annotation == null and escaped_brace)
            strip_annotations(source, i + 2, depth,
                {*:st, math: st.math ++ slice(source, i, i + 2)})
        else if (annotation == null) {
            let next_depth = if (ch == "{") depth + 1
                else if (ch == "}" and depth > 0) depth - 1 else depth
            strip_annotations(source, i + 1, next_depth,
                {*:st, math: st.math ++ ch})
        }
        else if (not annotation.value)
            strip_annotations(source, i + len(annotation.prefix), depth, {*:st, suppress: true})
        else {
            let value_begin = i + len(annotation.prefix)
            let closing = brace_close(source, value_begin, 1, false)
            if (closing == null)
                {*:st, error: "Unterminated amsmath annotation"}
            else {
                let value = trim(slice(source, value_begin, closing))
                let duplicate = if (annotation.kind == "label") st.label != null else st.tag != null
                let next = if (annotation.kind == "label") {*:st, label: value}
                    else {*:st, tag: value, bare_tag: annotation.kind == "bare-tag"}
                strip_annotations(source, closing + 1, depth,
                    if (duplicate or value == "") {*:next, error: "Duplicate or empty amsmath annotation"}
                    else next)
            }
        }
    }
}

pub fn rows_for(el) {
    let tag = string(name(el))
    let parsed = alignat_body(if (el.source != null) el.source else "", tag)
    if (parsed.error != null)
        [{math: "", label: null, tag: null, bare_tag: false,
          suppress: false, error: parsed.error}]
    else {
        let sources = if (starts_with(tag, "equation")) [parsed.source]
            else [for (row in split_tex(parsed.source, true, 0, 0, false, 0, [])
                where trim(row) != "") row];
        [for (source in sources)
            strip_annotations(source, 0, 0, {math: "", label: null, tag: null,
                bare_tag: false, suppress: false, error: null})]
    }
}

fn alignat_body(source, tag) {
    if (starts_with(tag, "IEEEeqnarray")) {
        let trimmed = trim(source)
        let close = index_of(trimmed, "}")
        let columns = if (close == null) "" else slice(trimmed, 1, close)
        if (not starts_with(trimmed, "{") or close == null or
            not valid_ieee_columns(columns, 0))
            {source: source, error: "Unsupported IEEEeqnarray column specification"}
        else {source: slice(trimmed, close + 1, len(trimmed)), error: null}
    }
    else if (not starts_with(tag, "alignat")) {source: source, error: null}
    else {
        let trimmed = trim(source)
        let close = index_of(trimmed, "}")
        if (not starts_with(trimmed, "{") or close == null) {
            {source: source, error: "alignat requires a column-pair count"}
        } else {
            let count = int(slice(trimmed, 1, close)) ^ { null }
            if (count == null or count < 1)
                {source: source, error: "Invalid alignat column-pair count"}
            else {source: slice(trimmed, close + 1, len(trimmed)), error: null}
        }
    }
}

fn valid_ieee_columns(columns, index) {
    if (index >= len(columns)) index > 0
    else {
        let ch = slice(columns, index, index + 1)
        if (ch == "l" or ch == "c" or ch == "r")
            valid_ieee_columns(columns, index + 1)
        else false
    }
}

fn render_alignment(el, tag, rows, numbers, operators) {
    if (el.source != null and index_of(el.source, "\\intertext") != null)
        util.unsupported_element("amsmath", "Alignment intertext is not supported", el.source_offset)
    else {
        let gathered = starts_with(tag, "gather") or starts_with(tag, "multline")
        let multline = starts_with(tag, "multline");
        <div class: if (multline) "latex-align latex-align-multline" else "latex-align",
            for (i, row in rows)
                if (row.error != null)
                    util.unsupported_element("amsmath", row.error, el.source_offset)
                else alignment_row(row.math, gathered,
                    if (multline and i == 0) "left"
                    else if (multline and i == len(rows) - 1) "right"
                    else null,
                    if (row.label != null) util.slugify(row.label) else null,
                    if (numbers != null and i < len(numbers)) numbers[i].display else row.tag,
                    row.bare_tag, operators, el.source_offset)>
    }
}

pub fn render_environment(el, numbers, operators) {
    let tag = string(name(el))
    let rows = rows_for(el)
    let active_operators = [for (entry in operators
        where entry.offset == null or el.source_offset == null or entry.offset < el.source_offset) entry]
    if (starts_with(tag, "equation")) {
        let row = rows[0]
        let display = if (numbers != null and len(numbers) > 0) numbers[0].display else row.tag
        if (row.error != null) util.unsupported_element("amsmath", row.error, el.source_offset)
        else <div class: "latex-equation",
            id: if (row.label != null) util.slugify(row.label) else null,
            math_cell(row.math, true, active_operators, el.source_offset);
            if (display != null) {
                <span class: "latex-eq-number",
                    if (row.bare_tag) display else "(" ++ display ++ ")">
            }>
    } else render_alignment(el, tag, rows, numbers, active_operators)
}
