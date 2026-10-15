// latex/packages/hhline.ls — \hhline{…}: a horizontal rule drawn column by column
// (vibe/impl/Lambda_Impl_Latex_Packages.md §9). `=` is a double rule over a column,
// `-` a single one and `~` none; the vertical-rule and crossing marks (`|`, `:`,
// `#`, `t`, `b`) change nothing here, since the columns already draw their own
// vertical rules. *{n}{…} repeats a pattern.

// the column segments of a spec: "double", "single" or "none"
fn segments(spec, i, acc) {
    if (i >= len(spec)) acc
    else {
        let ch = slice(spec, i, i + 1)
        if (ch == "=") segments(spec, i + 1, acc ++ ["double"])
        else if (ch == "-") segments(spec, i + 1, acc ++ ["single"])
        else if (ch == "~") segments(spec, i + 1, acc ++ ["none"])
        else if (ch == "*" and slice(spec, i + 1, i + 2) == "{") {
            let count_end = index_of(slice(spec, i, len(spec)), "}")
            let copies = if (count_end == null) 0 else int(trim(slice(spec, i + 2, i + count_end))) ^ { 0 }
            let rest = if (count_end == null) "" else slice(spec, i + count_end + 1, len(spec))
            let close = if (starts_with(rest, "{")) index_of(rest, "}") else null
            if (close == null) segments(spec, i + 1, acc)
            else {
                let pattern = segments(slice(rest, 1, close), 0, [])
                segments(spec, i + count_end + 1 + close + 1,
                    acc ++ (if (copies <= 0) [] else [for (k in 1 to copies, segment in pattern) segment]))
            }
        }
        else segments(spec, i + 1, acc)
    }
}

let RULES = {double: "3px double ", single: "1px solid "}

// the rule row: one cell per column, each drawing its segment in the rule color;
// columns past the spec get no rule
pub fn rule_row(spec, columns, rule_color) {
    let parts = segments(trim(spec), 0, [])
    let count = max([1, columns, len(parts)]);
    <tr class: "latex-hhline",
        for (k in 0 to count - 1, let part = if (k < len(parts)) parts[k] else "none")
            <td style: if (part == "none") null else "border-top:" ++ RULES[part] ++ (rule_color or "black")>>
}
