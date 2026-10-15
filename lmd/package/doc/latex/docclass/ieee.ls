// docclass/ieee.ls — IEEEtran numbering
// Sections are Roman (I), subsections lettered after them (II-A), subsubsections
// numbered after those (II-A1); a reference prints that number, a heading only its
// own part: "II.", "A.", "1)", "a)".

import util: ~~.util

// format section number from counters, as \thesection and the others print it
pub fn format_section_number(counters, counter_name, in_appendix) {
    let sec = if (in_appendix) util.upper_alph(counters.section) else upper(util.roman(counters.section))
    let sub = sec ++ "-" ++ util.upper_alph(counters.subsection)
    match counter_name {
        case "section": sec
        case "subsection": sub
        case "subsubsection": sub ++ string(counters.subsubsection)
        default: null
    }
}

// the heading's own label for a number format_section_number gave
pub fn heading_label(level, num) {
    let parts = split(num, "-")
    let own = parts[len(parts) - 1]
    match level {
        case "section": num ++ "."
        case "subsection": own ++ "."
        case "subsubsection": slice(own, 1, len(own)) ++ ")"
        default: num
    }
}
