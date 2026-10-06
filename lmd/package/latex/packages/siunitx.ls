// Bounded siunitx number and SI-unit formatter; no TeX macro execution.
import util: ~~.util

fn raw_arg(el, index) {
    let raw = util.raw_argument(el, "required", index)
    if (raw != null) raw else util.text_of_child(el, index)
}

fn group_integer(digits) {
    if (len(digits) <= 3) digits
    else group_integer(slice(digits, 0, len(digits) - 3)) ++ "\u202F" ++
        slice(digits, len(digits) - 3, len(digits))
}

fn digits_only(s, i) {
    if (i >= len(s)) true
    else if (index_of("0123456789", slice(s, i, i + 1)) == null) false
    else digits_only(s, i + 1)
}

fn format_decimal(source) {
    let signed = starts_with(source, "-") or starts_with(source, "+")
    let sign = if (signed) slice(source, 0, 1) else ""
    let rest = if (signed) slice(source, 1, len(source)) else source
    let parts = split(rest, ".")
    let whole = parts[0]
    let valid = whole != "" and digits_only(whole, 0) and len(parts) <= 2 and
        (len(parts) == 1 or (parts[1] != "" and digits_only(parts[1], 0)))
    if (not valid) null
    else sign ++ group_integer(whole) ++
        (if (len(parts) > 1) "." ++ join(slice(parts, 1, len(parts)), ".") else "")
}

fn superscript_digit(ch) {
    let source = "0123456789+-"
    let raised = ["⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹", "⁺", "⁻"]
    let i = index_of(source, ch)
    if (i == null) null else raised[i]
}

fn superscript(source, i, result) {
    if (i >= len(source)) result
    else {
        let raised = superscript_digit(slice(source, i, i + 1))
        if (raised == null) null else superscript(source, i + 1, result ++ raised)
    }
}

fn format_uncertainty(source) {
    let mark = index_of(source, "(")
    if (mark == null) format_decimal(source)
    else if (not ends_with(source, ")")) null
    else {
        let base = format_decimal(slice(source, 0, mark))
        let digits = slice(source, mark + 1, len(source) - 1)
        if (base == null or digits == "" or not digits_only(digits, 0)) null
        else base ++ "(" ++ digits ++ ")"
    }
}

pub fn format_number(source) {
    let value = trim(source)
    let lower_e = index_of(value, "e")
    let upper_e = index_of(value, "E")
    let exponent_at = if (lower_e != null) lower_e else upper_e
    if (exponent_at == null) format_uncertainty(value)
    else {
        let mantissa = format_uncertainty(slice(value, 0, exponent_at))
        let raised = superscript(slice(value, exponent_at + 1, len(value)), 0, "")
        if (mantissa == null or raised == "" or raised == null) null
        else mantissa ++ " × 10" ++ raised
    }
}

let UNITS = [
    ["\\kilogram", "kg"], ["\\gram", "g"], ["\\kilometer", "km"],
    ["\\metre", "m"], ["\\meter", "m"], ["\\second", "s"],
    ["\\minute", "min"], ["\\hour", "h"], ["\\kelvin", "K"],
    ["\\ampere", "A"], ["\\mole", "mol"], ["\\candela", "cd"],
    ["\\newton", "N"], ["\\joule", "J"], ["\\watt", "W"],
    ["\\pascal", "Pa"], ["\\hertz", "Hz"], ["\\degreeCelsius", "°C"],
    ["\\ohm", "Ω"], ["\\litre", "L"], ["\\liter", "L"],
    ["\\degree", "°"], ["\\percent", "%"], ["\\per", "/"], ["\\squared", "²"],
    ["\\cubed", "³"],
    // long unit commands are replaced first; separate prefix tokens compose afterward.
    ["\\giga", "G"], ["\\mega", "M"], ["\\kilo", "k"],
    ["\\centi", "c"], ["\\milli", "m"], ["\\micro", "µ"]
]

fn replace_units(source, i) {
    if (i >= len(UNITS)) source
    else replace_units(replace(source, UNITS[i][0], UNITS[i][1]), i + 1)
}

fn unit_text(source) {
    let result = replace_units(source, 0)
    if (index_of(result, "\\") != null) null else result
}

// S columns use one measured grid width per table, keeping decimal points aligned.
pub fn column_descriptor(raw, offset) => {kind: "si-decimal", raw: raw, offset: offset}

fn numeric_parts(source) {
    let text = trim(source)
    let formatted = format_number(text)
    let exponent_at = index_of(text, "e")
    let upper_at = index_of(text, "E")
    let exp_at = if (exponent_at != null) exponent_at else upper_at
    let mantissa = if (exp_at != null) slice(text, 0, exp_at) else text
    let uncertain_at = index_of(mantissa, "(")
    let core = if (uncertain_at != null) slice(mantissa, 0, uncertain_at) else mantissa
    let formatted_core = format_decimal(core)
    if (formatted == null or formatted_core == null) null
    else {
        let halves = split(formatted_core, ".")
        {whole: halves[0], fraction: if (len(halves) > 1) halves[1] else "",
         point: len(halves) > 1,
         suffix: slice(formatted, len(formatted_core), len(formatted))}
    }
}

fn table_cells(node, i, column, current, acc) {
    if (i >= len(node)) acc ++ [{column: column, text: trim(current)}]
    else {
        let child = node[i]
        let boundary = if (child is symbol) string(child) else ""
        if (boundary == "alignment_tab")
            table_cells(node, i + 1, column + 1, "",
                acc ++ [{column: column, text: trim(current)}])
        else if (boundary == "row_sep")
            table_cells(node, i + 1, 0, "",
                acc ++ [{column: column, text: trim(current)}])
        else table_cells(node, i + 1, column,
            current ++ (if (child is symbol) "" else util.text_of(child)), acc)
    }
}

fn table_format(raw) {
    if (raw == null) {whole: 0, fraction: 0}
    else {
        let parts = split(trim(raw), ".")
        let valid = len(parts) <= 2 and digits_only(parts[0], 0) and parts[0] != "" and
            (len(parts) == 1 or (parts[1] != "" and digits_only(parts[1], 0)))
        if (not valid) null
        else {whole: int(parts[0]), fraction: if (len(parts) == 2) int(parts[1]) else 0}
    }
}

fn measured_profile(desc, cells, column) {
    let opts = util.parse_kv_options(desc.raw)
    let keys = [for (key, value at opts where string(key) != "table-format") string(key)]
    let requested = table_format(opts["table-format"])
    let shapes = [for (cell in cells
        where cell.column == column and cell.text != "" and numeric_parts(cell.text) != null)
        numeric_parts(cell.text)]
    let whole = max([0] ++ [for (shape in shapes) len(shape.whole)])
    let fraction = max([0] ++ [for (shape in shapes) len(shape.fraction)])
    let issue = if (len(keys) > 0) "Unsupported siunitx S option: " ++ join(keys, ", ")
        else if (requested == null) "Invalid siunitx table-format" else null
    {*:desc, whole_width: max([whole, if (requested != null) requested.whole else 0]),
        fraction_width: max([fraction, if (requested != null) requested.fraction else 0]),
        error: issue}
}

pub fn measure_columns(content, columns) {
    let cells = table_cells(content, 0, 0, "", []);
    [for (i, column in columns)
        if (column is map and column.kind == "si-decimal") measured_profile(column, cells, i)
        else column]
}

pub fn render_table_cell(cell, profile) {
    let parts = numeric_parts(util.text_of(cell))
    if (profile.error != null) <td util.unsupported_element("siunitx", profile.error, profile.offset)>
    else if (parts == null)
        <td util.unsupported_element("siunitx", "S column requires a number", profile.offset)>
    else {
        let grid = "display:inline-grid;grid-template-columns:" ++
            string(max([1, profile.whole_width])) ++ "ch 1ch " ++
            string(max([1, profile.fraction_width])) ++ "ch;";
        <td class: "latex-si-column",
            <span class: "latex-si-number", style: grid,
                <span class: "latex-si-whole", parts.whole>
                <span class: "latex-si-point", if (parts.point) "." else " ">
                <span class: "latex-si-fraction", parts.fraction>>
            if (parts.suffix != "") { <span class: "latex-si-suffix", parts.suffix> }
        >
    }
}

fn unsupported(message, el) {
    let offset = if (el is element) el.source_offset else null
    util.unsupported_element("siunitx", message, offset)
}

pub fn render(el) {
    let command = string(name(el))
    if (command == "num") {
        let formatted = format_number(raw_arg(el, 0))
        if (formatted == null) unsupported("Unsupported siunitx number: " ++ raw_arg(el, 0), el)
        else <span class: "latex-number", formatted>
    } else if (command == "si" or command == "unit") {
        let unit = unit_text(raw_arg(el, 0))
        if (unit == null) unsupported("Unsupported siunitx unit: " ++ raw_arg(el, 0), el)
        else <span class: "latex-unit", unit>
    } else if (command == "SI" or command == "qty") {
        let formatted = format_number(raw_arg(el, 0))
        let unit = unit_text(raw_arg(el, 1))
        if (formatted == null or unit == null) unsupported("Unsupported siunitx quantity", el)
        else <span class: "latex-quantity", formatted ++ "\u202F" ++ unit>
    } else unsupported("Unsupported siunitx command: " ++ command, el)
}
