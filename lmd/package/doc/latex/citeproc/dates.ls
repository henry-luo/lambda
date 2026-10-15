// Date precision and ranges stay structured through locale-specific rendering.
import c: .common
import locale: .locale
import numbers: .numbers
import out: .output

fn part(node, components, context) {
    let kind = node["name"]
    let index = if (kind == "year") 0 else if (kind == "month") 1 else 2
    let value = if (components[index] == null) null else c.as_int(components[index], null)
    let form = c.get(node, "form", "long")
    let text = if (value == null or (kind == "year" and value == 0)) ""
        else if (kind == "year") {
            let digits = string(abs(value))
            let rendered = if (form == "short") slice(digits, max([0, len(digits) - 2]), len(digits)) else digits
            rendered ++ (if (value < 0) locale.term(context.locales, "bc")
                else if (value < 1000) locale.term(context.locales, "ad") else "") ++
                c.get(context, "year_suffix", "")
        } else if (kind == "month" and (form == "long" or form == "short"))
            locale.term(context.locales, (if (value > 12) "season-0" ++ string(value - (if (value >= 21) 20 else 12))
                else "month-" ++ (if (value < 10) "0" else "") ++ string(value)), form)
        else if (form == "numeric-leading-zeros" and value < 10) "0" ++ string(value)
        else if (form == "ordinal" and (kind != "day" or value == 1 or
            not c.truth(locale.option(context.locales, "limit-day-ordinals-to-day-1", "false"))))
            numbers.ordinal(value, context.locales, locale.gender(context.locales,
                "month-" ++ (if (components[1] < 10) "0" else "") ++ string(components[1])))
        else string(value)
    out.decorate(node, out.result([text]), context)
}

fn configured_parts(node, context) {
    let localized = if (node.form != null) locale.date_format(context.locales, node.form) else null
    let requested = split(c.get(node, "date-parts", "year-month-day"), "-")
    let defaults = if (localized != null) c.children(localized, "date-part") else c.children(node, "date-part")
    let overrides = c.children(node, "date-part");
    [for (item in defaults where c.has(requested, item["name"])) {
        let matching = [for (override in overrides where override["name"] == item["name"]) override]
        if (len(matching) == 0) item else {*:map(item), *:map(matching[0])}
    }]
}

fn rank(node) => if (node["name"] == "year") 0 else if (node["name"] == "month") 1 else 2

fn date_parts(node, values, context) {
    let parts = [for (item in configured_parts(node, context)
        where values[0][rank(item)] != null or values[1][rank(item)] != null) item]
    let localized = if (node.form == null) null else locale.date_format(context.locales, node.form)
    let delimiter = c.get(node, "delimiter", c.get(localized, "delimiter", ""))
    if (len(values) < 2) out.combine([for (item in parts) part(item, values[0], context)], delimiter)
    else {
        let differing = [for (r in 0 to 2 where values[0][r] != values[1][r]) r]
        let changed = if (len(differing) == 0) 3 else differing[0]
        let positions = [for (i, item in parts where rank(item) >= changed) i]
        if (len(positions) == 0) out.combine([for (item in parts) part(item, values[0], context)], delimiter)
        else {
            let start_at = positions[0]
            let end_at = positions[len(positions) - 1]
            let range_nodes = [for (item in parts where rank(item) == changed) item]
            let separator = c.get(range_nodes[0], "range-delimiter", "–")
            let prefix_parts = [for (i, item in parts where i < start_at) part(item, values[0], context)]
            let first = out.combine([for (i, item in parts where i >= start_at and i <= end_at)
                part(if (i == end_at) {*:c.attrs(item), suffix: ""} else item, values[0], context)], delimiter)
            let second = out.combine([for (i, item in parts where i >= start_at and i <= end_at)
                part(if (i == start_at) {*:c.attrs(item), prefix: ""} else item, values[1], context)], delimiter)
            let suffix_parts = [for (i, item in parts where i > end_at) part(item, values[1], context)]
            let ranged = if (second.text == "") out.result([first.content, separator])
                else out.combine([first, second], separator)
            out.combine(prefix_parts ++ [ranged] ++ suffix_parts, delimiter)
        }
    }
}

pub fn render(node, value, context) {
    if (value == null) out.result([], 1, 0)
    else if (value.literal != null) out.result([value.literal], 1, 1)
    else if (len(value["date-parts"]) == 0 and value.raw != null) out.result([value.raw], 1, 1)
    else {
        let original = value["date-parts"]
        let values = if (value.season == null or len(original[0]) > 1) original
            else [[original[0][0], 12 + c.as_int(value.season)]]
        let ranged = date_parts(node, values, context)
        {*:ranged, attempted: 1, successful: if (ranged.text == "") 0 else 1}
    }
}
