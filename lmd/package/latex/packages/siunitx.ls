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

pub fn format_number(source) {
    let number = trim(source)
    let signed = starts_with(number, "-") or starts_with(number, "+")
    let sign = if (signed) slice(number, 0, 1) else ""
    let rest = if (signed) slice(number, 1, len(number)) else number
    let parts = split(rest, ".")
    let integer = parts[0]
    let valid = integer != "" and digits_only(integer, 0) and len(parts) <= 2 and
        (len(parts) == 1 or (parts[1] != "" and digits_only(parts[1], 0)))
    if (not valid) null
    else sign ++ group_integer(integer) ++
        (if (len(parts) > 1) "." ++ join(slice(parts, 1, len(parts)), ".") else "")
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

fn unsupported(message, el) => util.unsupported_element("siunitx", message, el.source_offset)

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
        let number = format_number(raw_arg(el, 0))
        let unit = unit_text(raw_arg(el, 1))
        if (number == null or unit == null) unsupported("Unsupported siunitx quantity", el)
        else <span class: "latex-quantity", number ++ "\u202F" ++ unit>
    } else unsupported("Unsupported siunitx command: " ++ command, el)
}
