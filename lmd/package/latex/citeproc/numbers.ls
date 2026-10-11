// Numeric and ordinal formatting uses locale terms supplied as data.
import c: .common
import locale: .locale

fn roman_parts(value, values, labels, index, acc) {
    if (value <= 0 or index >= len(values)) acc
    else if (value >= values[index]) roman_parts(value - values[index], values, labels, index, acc ++ labels[index])
    else roman_parts(value, values, labels, index + 1, acc)
}

pub fn roman(value) => roman_parts(value,
    [1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1],
    ["m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"], 0, "")

pub fn ordinal(value, locales, gender = null) {
    let terms = locale.ordinal_terms(locales)
    let matched = [for (term in terms where term["name"] != "ordinal") {
        let n = c.as_int(slice(term["name"], 8, len(term["name"])), -1)
        let rule = c.get(term, "match", if (n < 10) "last-digit" else "last-two-digits")
        let match_value = if (rule == "whole-number") value
            else if (rule == "last-two-digits") value % 100 else value % 10;
        {term: term, n: n, matched: n == match_value}
    }]
    let generic = [for (term in terms where term["name"] == "ordinal") term]
    let legacy = len(generic) == 0
    let candidates = [for (part in matched where part.matched and
        (part.term["gender-form"] == gender or part.term["gender-form"] == null) and
        not (legacy and value % 100 >= 11 and value % 100 <= 13)) part]
    let preferred = [for (part in candidates where part.term["gender-form"] == gender) part]
    let chosen = if (len(preferred) > 0) preferred else candidates
    let suffixes = sort(chosen, {dir: 'desc', by: (part) => part.n})
    let fallback = if (len(generic) > 0) c.text(generic[0])
        else locale.term(locales, "ordinal-04")
    string(value) ++ (if (len(suffixes) > 0) c.text(suffixes[0].term) else fallback)
}

pub fn render(value, form, locales, gender = null) {
    let numeric = int(value) ^ { null }
    if (numeric == null) c.text(value)
    else if (form == "roman") roman(numeric)
    else if (form == "ordinal") ordinal(numeric, locales, gender)
    else if (form == "long-ordinal") {
        let term = locale.term(locales, "long-ordinal-" ++ (if (numeric < 10) "0" else "") ++ string(numeric))
        if (term == "") ordinal(numeric, locales, gender) else term
    } else string(numeric)
}
