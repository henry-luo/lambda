import util: ~~.util

fn unsupported_rule(message, el, columns) {
    <tr class: "latex-unsupported", 'data-latex-error': message,
        'data-latex-offset': el.source_offset, 'data-latex-package': "booktabs",
        <td colspan: string(if (columns > 0) columns else 1), message>>
}

pub fn cmidrule(el, columns) {
    let required = util.raw_argument(el, "required", 0)
    let raw = trim(if (required != null) required else util.text_of_skip_brack(el))
    let trim_spec = util.raw_argument(el, "parenthesized", 0)
    let ends = split(raw, "-")
    let first = if (len(ends) == 2) int(trim(ends[0])) else null
    let last = if (len(ends) == 2) int(trim(ends[1])) else null
    let valid_trim = trim_spec == null or trim_spec == "l" or trim_spec == "r" or trim_spec == "lr"
    if (first == null or last == null or first < 1 or last < first or last > columns or not valid_trim) {
        let message = "Invalid booktabs cmidrule range: " ++ raw
        unsupported_rule(message, el, columns)
    } else {
        let before = first - 1
        let span = last - first + 1
        let after = columns - last
        let trim_style = (if (trim_spec == "l" or trim_spec == "lr") "margin-left:0.5em;" else "") ++
            (if (trim_spec == "r" or trim_spec == "lr") "margin-right:0.5em;" else "")
        <tr class: "latex-cmidrule",
            if (before > 0) <td colspan: string(before)>
            <td class: "latex-cmidrule-segment", colspan: string(span),
                <div class: "latex-cmidrule-line", style: trim_style>>
            if (after > 0) <td colspan: string(after)>
        >
    }
}

pub fn addlinespace(el, columns) {
    let raw = util.optional_raw(el)
    let height = if (raw == null) "0.5em" else util.css_dimension(raw)
    if (height == null) {
        let message = "Invalid booktabs addlinespace length: " ++ raw
        unsupported_rule(message, el, columns)
    } else {
        <tr class: "latex-addlinespace",
            <td colspan: string(if (columns > 0) columns else 1), style: "height:" ++ height>>
    }
}
