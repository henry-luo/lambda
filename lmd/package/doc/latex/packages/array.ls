// latex/packages/array.ls — a tabular's column preamble (Pkg_Latex §8.4): the
// standard and array column types, \newcolumntype, vertical rules, and the column
// decorations of colortbl (\columncolor) and dcolumn (D columns).
//
// A column is "left", "center" or "right", or a map whose `kind` is "fixed" (p, m, b
// and tabularx's L, C, R), "stretch" (X), "si-decimal" (siunitx S, dcolumn D) or
// "plain" (l, c or r with decorations). Decorations: `rule_left` counts the vertical
// rules before the column, `rule_right` those after the last column, and `color`
// is its \columncolor.

import util: ~~.util
import color: ~~.elements.color
import siunitx: .siunitx

// The columns of a preamble. ctx: {si, dcolumn (packages active), colors (custom
// colors), settings (for \newcolumntype), offset (the table's)}
pub fn columns(spec, ctx) =>
    decorate(extract(expand_types(trim(spec), ctx, 0), 0, [], ctx), 0, 0, null, [])

// ---- \newcolumntype ---------------------------------------------------------------

// \newcolumntype{d}[1]{D{.}{.}{#1}}: {name, value: {count, body}} for analysis to record
pub fn column_type(el) {
    let letter = util.raw_argument(el, "required", 0)
    let body = util.raw_argument(el, "required", 1)
    let count = int(trim(util.optional_raw(el) or "0")) ^ { null }
    if (letter == null or body == null or count == null or len(trim(letter)) != 1) null
    else {name: trim(letter), value: {count: count, body: body}}
}

// letters \newcolumntype defined, replaced by their definitions until none is left;
// array lets a definition use other new types. Groups are copied as they are: they
// hold widths and TeX code, and a *{n}{…} pattern is expanded when it is read.
fn expand_types(spec, ctx, depth) {
    if (depth > 8 or ctx.settings == null) spec
    else {
        let expanded = expand_pass(spec, 0, "", ctx)
        if (expanded == spec) spec else expand_types(expanded, ctx, depth + 1)
    }
}

fn expand_pass(spec, i, out, ctx) {
    if (i >= len(spec)) out
    else {
        let ch = slice(spec, i, i + 1)
        let definition = if (ch == "{") null
            else util.setting_at(ctx.settings, "column-type:" ++ ch, ctx.offset)
        if (ch == "{") {
            let after = group_end(spec, i, 0) or len(spec)
            expand_pass(spec, after, out ++ slice(spec, i, after), ctx)
        }
        else if (definition == null) expand_pass(spec, i + 1, out ++ ch, ctx)
        else {
            let read = type_arguments(spec, i + 1, definition.count, [])
            expand_pass(spec, read.next, out ++ substitute(definition.body, read.values, 0), ctx)
        }
    }
}

// a new type's arguments: braced groups or single characters
fn type_arguments(spec, i, count, values) {
    if (count <= 0 or i >= len(spec)) {values: values, next: i}
    else if (slice(spec, i, i + 1) == " ") type_arguments(spec, i + 1, count, values)
    else if (slice(spec, i, i + 1) == "{") {
        let after = group_end(spec, i, 0) or len(spec)
        type_arguments(spec, after, count - 1, values ++ [slice(spec, i + 1, max([i + 1, after - 1]))])
    }
    else type_arguments(spec, i + 1, count - 1, values ++ [slice(spec, i, i + 1)])
}

fn substitute(body, values, k) {
    if (k >= len(values)) body
    else substitute(replace(body, "#" ++ string(k + 1), values[k]), values, k + 1)
}

// ---- reading the preamble ---------------------------------------------------------

fn extract(spec, i, acc, ctx) {
    if (i >= len(spec)) acc
    else extract_one(spec, i, acc, ctx)
}

fn extract_one(spec, i, acc, ctx) {
    let ch = slice(spec, i, i + 1)
    if (ch == "l") extract(spec, i + 1, acc ++ ["left"], ctx)
    else if (ch == "c") extract(spec, i + 1, acc ++ ["center"], ctx)
    else if (ch == "r") extract(spec, i + 1, acc ++ ["right"], ctx)
    else if (ch == "|") extract(spec, i + 1, acc ++ [{kind: "vrule"}], ctx)
    else if (ch == "L" or ch == "C" or ch == "R" or
        ch == "p" or ch == "m" or ch == "b") {
        let after = group_end(spec, i + 1, 0)
        let width = if (after == null) null
            else slice(spec, i + 2, after - 1)
        let align = if (ch == "C") "center"
            else if (ch == "R") "right" else "left"
        extract(spec, if (after == null) i + 1 else after,
            acc ++ [{kind: "fixed", align: align, width: width}], ctx)
    }
    else if (ch == "X") extract(spec, i + 1,
        acc ++ [{kind: "stretch", align: "left"}], ctx)
    else if (ch == "*" and slice(spec, i + 1, i + 2) == "{") {
        let count_end = group_end(spec, i + 1, 0)
        let pattern_end = if (count_end == null) null
            else group_end(spec, count_end, 0)
        if (count_end == null or pattern_end == null)
            extract(spec, i + 1, acc, ctx)
        else {
            let copies = int(slice(spec, i + 2, count_end - 1)) ^ { 0 }
            let pattern = slice(spec, count_end + 1, pattern_end - 1)
            let columns = extract(expand_types(pattern, ctx, 0), 0, [], ctx)
            extract(spec, pattern_end, repeat_columns(columns, copies, acc), ctx)
        }
    }
    // >{…} prefixes the next column; colortbl's \columncolor in it colors the column
    else if (ch == ">" and slice(spec, i + 1, i + 2) == "{") {
        let after = group_end(spec, i + 1, 0)
        let prefix = if (after == null) "" else slice(spec, i + 2, after - 1)
        let column_color = prefix_color(prefix, ctx.colors)
        extract(spec, if (after == null) i + 1 else after,
            if (column_color == null) acc else acc ++ [{kind: "prefix", color: column_color}], ctx)
    }
    else if ((ch == "@" or ch == "<" or ch == "!") and
        slice(spec, i + 1, i + 2) == "{") {
        let after = group_end(spec, i + 1, 0)
        extract(spec, if (after == null) i + 1 else after, acc, ctx)
    }
    else if (ch == "S") {
        let tail = slice(spec, i + 1, len(spec))
        let optioned = starts_with(tail, "[")
        let closing = if (optioned) index_of(tail, "]") else null
        let raw = if (closing != null) slice(tail, 1, closing)
            else if (optioned) "[unterminated" else null
        let next = if (closing != null) i + closing + 2
            else if (optioned) len(spec) else i + 1
        let column = if (ctx.si) siunitx.column_descriptor(raw, ctx.offset) else "right"
        extract(spec, next, acc ++ [column], ctx)
    }
    // dcolumn's D{input separator}{output separator}{places}
    else if (ch == "D" and ctx.dcolumn) {
        let read = type_arguments(spec, i + 1, 3, [])
        extract(spec, read.next, acc ++ [if (len(read.values) == 3)
            siunitx.dcolumn_descriptor(read.values[0], read.values[1], read.values[2], ctx.offset)
            else "center"], ctx)
    }
    else extract(spec, i + 1, acc, ctx)
}

// the color of a \columncolor[model]{spec} in a column prefix, or null
fn prefix_color(prefix, colors) {
    let at = index_of(prefix, "\\columncolor")
    if (at == null) null
    else {
        let rest = trim(slice(prefix, at + 12, len(prefix)))
        let modeled = starts_with(rest, "[")
        let model_end = if (modeled) index_of(rest, "]") else null
        let model = if (model_end == null) null else slice(rest, 1, model_end)
        let tail = trim(if (model_end == null) rest else slice(rest, model_end + 1, len(rest)))
        let spec_end = if (starts_with(tail, "{")) group_end(tail, 0, 0) else null
        if (spec_end == null) null
        else color.spec_color(model, slice(tail, 1, spec_end - 1), colors)
    }
}

// the index just past the group that opens at or after index
fn group_end(spec, index, depth) {
    if (index >= len(spec)) null
    else {
        let ch = slice(spec, index, index + 1)
        if (ch == "{") group_end(spec, index + 1, depth + 1)
        else if (ch == "}" and depth == 1) index + 1
        else if (ch == "}") group_end(spec, index + 1, depth - 1)
        else group_end(spec, index + 1, depth)
    }
}

fn repeat_columns(columns, copies, acc) {
    if (copies <= 0) acc
    else repeat_columns(columns, copies - 1, acc ++ columns)
}

// ---- decorations ------------------------------------------------------------------

// fold the rule and prefix markers into the columns they belong to: rules and a
// \columncolor before a column decorate it; rules after the last column are its
// rule_right
fn decorate(items, i, rules, prefix, acc) {
    if (i >= len(items)) {
        if (rules == 0 or len(acc) == 0) acc
        else [*slice(acc, 0, len(acc) - 1), {*:plain(acc[len(acc) - 1]), rule_right: rules}]
    }
    else {
        let item = items[i]
        if (item is map and item.kind == "vrule") decorate(items, i + 1, rules + 1, prefix, acc)
        else if (item is map and item.kind == "prefix") decorate(items, i + 1, rules, item.color, acc)
        else if (rules == 0 and prefix == null) decorate(items, i + 1, 0, null, acc ++ [item])
        else decorate(items, i + 1, 0, null, acc ++ [{*:plain(item),
            rule_left: if (rules > 0) rules else null, color: prefix}])
    }
}

fn plain(column) => if (column is map) column else {kind: "plain", align: column}

// a column's vertical rules and \columncolor as td CSS. One rule is a line; two or
// more are a double line. A colored row hides the column color (colortbl's order:
// cell over row over column).
pub fn decoration_css(column, rule_color, row_colored) {
    if (not (column is map)) ""
    else rule_css("left", column.rule_left, rule_color) ++ rule_css("right", column.rule_right, rule_color) ++
        (if (column.color == null or row_colored) "" else "background-color:" ++ column.color ++ ";")
}

fn rule_css(side, count, rule_color) {
    if (count == null or count <= 0) ""
    else "border-" ++ side ++ ":" ++ (if (count == 1) "1px solid " else "3px double ") ++
        (rule_color or "black") ++ ";"
}
