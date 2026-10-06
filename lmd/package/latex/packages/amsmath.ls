// The document adapter reuses the script math renderer's bounded AST profile.
import bridge: ~~.math_bridge
import util: ~~.util
import math: lambda.doc.math.math

let ENVIRONMENTS = ["equation*", "align", "align*", "alignat", "alignat*",
    "aligned", "gather", "gather*", "multline", "multline*", "split"]

pub fn is_environment(tag) => any([for (name in ENVIRONMENTS) tag == name])

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

fn math_cell(source) {
    let expression = trim(source)
    let ast = if (expression == "") null else parse(expression, {type: "math", flavor: "latex"}) ^ { null }
    if (expression == "") ""
    else if (ast == null) util.unsupported_element("amsmath",
        "Cannot parse aligned math cell: " ++ expression, null)
    else math.render_inline(ast)
}

fn alignment_row(source, gathered, multline_align) {
    let cells = if (gathered) [source] else split_tex(source, false, 0, 0, false, 0, [])
    <div class: "latex-align-row",
        for (i, cell in cells)
            <span class: "latex-align-cell",
                style: if (multline_align != null) "text-align:" ++ multline_align
                    else if (gathered) "text-align:center" else if (i % 2 == 0)
                    "text-align:right" else "text-align:left",
                math_cell(cell)>
    >
}

fn alignat_body(source, tag) {
    if (not starts_with(tag, "alignat")) {source: source, error: null}
    else {
        let trimmed = trim(source)
        let close = index_of(trimmed, "}")
        if (not starts_with(trimmed, "{") or close == null) {
            {source: source, error: "alignat requires a column-pair count"}
        } else {
            let count = int(slice(trimmed, 1, close))
            if (count == null or count < 1)
                {source: source, error: "Invalid alignat column-pair count"}
            else {source: slice(trimmed, close + 1, len(trimmed)), error: null}
        }
    }
}

fn render_alignment(el, tag) {
    let parsed = alignat_body(if (el.source != null) el.source else "", tag)
    let source = parsed.source
    if (parsed.error != null) util.unsupported_element("amsmath", parsed.error, el.source_offset)
    else if (index_of(source, "\\tag") != null or index_of(source, "\\notag") != null or
        index_of(source, "\\nonumber") != null or index_of(source, "\\label") != null or
        index_of(source, "\\intertext") != null)
        util.unsupported_element("amsmath", "Alignment annotations are not supported", el.source_offset)
    else {
        let rows = [for (row in split_tex(source, true, 0, 0, false, 0, [])
                    where trim(row) != "") row]
        let gathered = starts_with(tag, "gather") or starts_with(tag, "multline")
        let multline = starts_with(tag, "multline")
        <div class: if (multline) "latex-align latex-align-multline" else "latex-align",
            for (i, row in rows)
                alignment_row(row, gathered,
                    if (multline and i == 0) "left"
                    else if (multline and i == len(rows) - 1) "right"
                    else null)>
    }
}

pub fn render_environment(el) {
    let tag = string(name(el))
    if (tag == "equation*") bridge.render_math_env_el(el, tag)
    else render_alignment(el, tag)
}
