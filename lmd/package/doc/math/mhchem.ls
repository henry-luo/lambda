// math/mhchem.ls — a bounded profile of mhchem's \ce and \pu (Pkg_Math §7).
//
// \ce covers formulas (element symbols, subscripts, groups), charges, states,
// stoichiometric coefficients, isotopes, adducts, bonds, reaction arrows with
// labels, and precipitate and gas marks; \pu covers a number with an optional
// exponent followed by units. The notation becomes TeX math for the ordinary
// parser. Notation outside the profile passes through unchanged.

// longest spelling first, so "<-->" is not read as "<-"
let ARROWS = [
    {text: "<-->", tex: "\\xrightleftarrows"}, {text: "<=>>", tex: "\\xrightequilibrium"},
    {text: "<<=>", tex: "\\xleftequilibrium"}, {text: "<->", tex: "\\xleftrightarrow"},
    {text: "<=>", tex: "\\xrightleftharpoons"}, {text: "->", tex: "\\xrightarrow"},
    {text: "<-", tex: "\\xleftarrow"}]

let STATES = ["aq", "s", "l", "g", "sol", "cr", "c", "ads", "lc", "am"]

let UPPER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
let LOWER = "abcdefghijklmnopqrstuvwxyz"
let DIGITS = "0123456789"

fn is_in(ch, set) => ch != null and ch != "" and (contains(set, ch) ^ { false })

// the parsed math of a <chemistry> node: the items of its formula
pub fn items(n) {
    let source = if (n.source == null) "" else string(n.source)
    let tex = if (string(n.cmd) == "\\pu") quantity(source) else equation(source)
    let ast = parse(tex, {type: "math", flavor: "latex"}) ^ { null }
    if (ast == null) [] else content(ast)
}

// ---- \ce ----------------------------------------------------------------------

// whitespace separates tokens, except inside braces, brackets and $...$
pub fn equation(source) => join([for (token in tokens(split(source, ""), 0, 0, false, "", [])) token_tex(token)], " ")

fn tokens(chars, i, depth, in_math, current, found) {
    if (i >= len(chars)) (if (current == "") found else found ++ [current])
    else {
        let ch = chars[i]
        if (not in_math and depth == 0 and (ch == " " or ch == "\t" or ch == "\n"))
            tokens(chars, i + 1, depth, in_math, "", if (current == "") found else found ++ [current])
        else tokens(chars, i + 1,
            if (in_math) depth else if (ch == "{" or ch == "[") depth + 1
                else if (ch == "}" or ch == "]") max(0, depth - 1) else depth,
            if (ch == "$") not in_math else in_math, current ++ ch, found)
    }
}

fn token_tex(token) {
    let arrow = [for (a in ARROWS where starts_with(token, a.text) or false) a][0]
    if (token == "+") "+"
    else if (token == "v") "\\downarrow"
    else if (token == "^") "\\uparrow"
    else if (arrow != null) arrow_tex(arrow, slice(token, len(arrow.text), len(token)))
    else species(split(token, ""), 0, "start", "")
}

// ->[above][below]: each label is itself \ce notation
fn arrow_tex(arrow, rest) {
    let labels = brackets(split(rest, ""), 0, 0, "", [])
    let below = if (len(labels) > 1) "[" ++ equation(labels[1]) ++ "]" else ""
    arrow.tex ++ below ++ "{" ++ (if (len(labels) > 0) equation(labels[0]) else "") ++ "}"
}

fn brackets(chars, i, depth, current, found) {
    if (i >= len(chars)) found
    else {
        let ch = chars[i]
        if (ch == "[" and depth == 0) brackets(chars, i + 1, 1, "", found)
        else if (ch == "]" and depth == 1) brackets(chars, i + 1, 0, "", found ++ [current])
        else brackets(chars, i + 1, depth + (if (ch == "[") 1 else if (ch == "]") -1 else 0),
            if (depth > 0) current ++ ch else current, found)
    }
}

// the index after the run of characters from set starting at i
fn run_end(chars, i, set) => if (i < len(chars) and is_in(chars[i], set)) run_end(chars, i + 1, set) else i

fn text_of(chars, from, to) => join(slice(chars, from, to), "")

// the end of a brace group opening at i (the index after its closing brace)
fn group_end(chars, i, depth) {
    if (i >= len(chars)) i
    else if (chars[i] == "{") group_end(chars, i + 1, depth + 1)
    else if (chars[i] == "}") (if (depth == 1) i + 1 else group_end(chars, i + 1, depth - 1))
    else group_end(chars, i + 1, depth)
}

fn close_index(chars, i, close) => if (i >= len(chars) or chars[i] == close) i else close_index(chars, i + 1, close)

// a script's argument: a brace group, kept as written, or a run of charge or
// numeral characters; an oxidation state's roman numeral stays upright
fn script_arg(chars, i) {
    let braced = i < len(chars) and chars[i] == "{"
    let end = if (braced) group_end(chars, i, 0) else run_end(chars, i, DIGITS ++ "+-IVX.")
    let stop = if (not braced and end == i and i < len(chars)) i + 1 else end
    let text = if (braced) text_of(chars, i + 1, end - 1) else text_of(chars, i, stop);
    {tex: if (text != "" and run_end(split(text, ""), 0, "IVX") == len(text)) "\\mathrm{" ++ text ++ "}" else text,
     next: stop}
}

// one formula, read left to right; prev says what came last ("start" admits a
// coefficient or an isotope prefix)
fn species(chars, i, prev, out) {
    if (i >= len(chars)) out
    else {
        let ch = chars[i]
        let next = if (i + 1 < len(chars)) chars[i + 1] else null
        if (prev == "start" and is_in(ch, DIGITS)) {
            let end = run_end(chars, i, DIGITS ++ "./")
            let digits = text_of(chars, i, end)
            let parts = split(digits, "/")
            species(chars, end, "coefficient", out ++
                (if (len(parts) == 2) "\\tfrac{" ++ parts[0] ++ "}{" ++ parts[1] ++ "}" else digits))
        }
        else if (is_in(ch, UPPER)) {
            let end = run_end(chars, i + 1, LOWER)
            species(chars, end, "atom", out ++ "\\mathrm{" ++ text_of(chars, i, end) ++ "}")
        }
        else if (is_in(ch, LOWER)) {
            // a word is text; a single letter is a variable, except the electron
            let end = run_end(chars, i, LOWER)
            let word = text_of(chars, i, end)
            let tex = if (len(word) > 1 or word == "e") "\\mathrm{" ++ word ++ "}" else word
            species(chars, end, "atom", out ++ tex)
        }
        else if (is_in(ch, DIGITS) and (prev == "atom" or prev == "close")) {
            let end = run_end(chars, i, DIGITS)
            species(chars, end, "script", out ++ "_{" ++ text_of(chars, i, end) ++ "}")
        }
        else if ((ch == "+" or ch == "-") and prev != "start" and charge_at(chars, i)) {
            let end = run_end(chars, i, "+-")
            species(chars, end, "charge", out ++ "^{" ++ text_of(chars, i, end) ++ "}")
        }
        else if (ch == "^" or ch == "_") {
            let arg = script_arg(chars, i + 1)
            species(chars, arg.next, "script", out ++ (if (prev == "start") "{}" else "") ++
                ch ++ "{" ++ arg.tex ++ "}")
        }
        else if (ch == "(" and state_at(chars, i)) {
            let end = close_index(chars, i, ")")
            species(chars, end + 1, "close", out ++ "\\mathrm{(" ++ text_of(chars, i + 1, end) ++ ")}")
        }
        else if (ch == "(" or ch == "[") species(chars, i + 1, "open", out ++ ch)
        else if (ch == ")" or ch == "]") species(chars, i + 1, "close", out ++ ch)
        // an adduct dot starts a new formula, which may have its own coefficient
        else if (ch == "*" or (ch == "." and (is_in(next, DIGITS) or is_in(next, UPPER))))
            species(chars, i + 1, "start", out ++ "\\cdot ")
        else if (ch == "-") species(chars, i + 1, "bond", out ++ "{-}")
        else if (ch == "=") species(chars, i + 1, "bond", out ++ "{=}")
        else if (ch == "#") species(chars, i + 1, "bond", out ++ "{\\equiv}")
        else if (ch == "{") {
            let end = group_end(chars, i, 0)
            species(chars, end, "close", out ++ "{" ++ species(slice(chars, i + 1, end - 1), 0, "start", "") ++ "}")
        }
        else if (ch == "$") {
            let end = close_index(chars, i + 1, "$")
            species(chars, end + 1, "atom", out ++ "{" ++ text_of(chars, i + 1, end) ++ "}")
        }
        else if (ch == "\\") {
            let end = if (is_in(next, UPPER ++ LOWER)) run_end(chars, i + 1, UPPER ++ LOWER) else i + 2
            species(chars, end, "atom", out ++ text_of(chars, i, end) ++ " ")
        }
        else species(chars, i + 1, "other", out ++ ch)
    }
}

// a run of + and - is a charge when nothing but a state follows it
fn charge_at(chars, i) {
    let end = run_end(chars, i, "+-");
    (end >= len(chars) or (chars[end] == "(" and state_at(chars, end)))
}

fn state_at(chars, i) {
    let end = close_index(chars, i, ")");
    (end < len(chars) and (contains(STATES, text_of(chars, i + 1, end)) or false))
}

// ---- \pu ----------------------------------------------------------------------

// a number, an optional e-exponent, then units: 1.2e3 kJ/mol -> 1.2\cdot10^{3}\,\mathrm{kJ}/\mathrm{mol}
pub fn quantity(source) {
    let chars = split(trim(source), "")
    let end = run_end(chars, 0, DIGITS ++ ".,+-")
    let exponent = if (end < len(chars) and (chars[end] == "e" or chars[end] == "E") and
        (is_in(if (end + 1 < len(chars)) chars[end + 1] else null, DIGITS ++ "+-")))
        run_end(chars, end + 1 + (if (is_in(chars[end + 1], "+-")) 1 else 0), DIGITS) else end
    let mantissa = text_of(chars, 0, end)
    let power = if (exponent > end) "\\cdot 10^{" ++ text_of(chars, end + 1, exponent) ++ "}" else ""
    let rest = trim(text_of(chars, exponent, len(chars)))
    mantissa ++ power ++ (if (rest == "") "" else (if (mantissa == "") "" else "\\,") ++
        units(split(rest, ""), 0, ""))
}

// unit names are upright; digits after a unit (mol-1, m2) are its power
fn units(chars, i, out) {
    if (i >= len(chars)) out
    else {
        let ch = chars[i]
        if (is_in(ch, UPPER ++ LOWER)) {
            let end = run_end(chars, i, UPPER ++ LOWER)
            let power_end = run_end(chars, end + (if (end < len(chars) and chars[end] == "-" and
                is_in(if (end + 1 < len(chars)) chars[end + 1] else null, DIGITS)) 1 else 0), DIGITS)
            units(chars, power_end, out ++ "\\mathrm{" ++ text_of(chars, i, end) ++ "}" ++
                (if (power_end > end) "^{" ++ text_of(chars, end, power_end) ++ "}" else ""))
        }
        else if (ch == "^") {
            let arg = script_arg(chars, i + 1)
            units(chars, arg.next, out ++ "^{" ++ arg.tex ++ "}")
        }
        else if (ch == " ") units(chars, i + 1, out ++ "\\,")
        else if (ch == "." or ch == "*") units(chars, i + 1, out ++ "\\cdot ")
        else if (ch == "\\") {
            let end = run_end(chars, i + 1, UPPER ++ LOWER)
            units(chars, end, out ++ text_of(chars, i, end) ++ " ")
        }
        else units(chars, i + 1, out ++ ch)
    }
}
