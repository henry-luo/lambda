// docclass/book.ls — Book document class numbering
// LaTeX book class: parts and chapters; \chapter is the top numbered level.

import util: lambda.latex.util

// format section number from counters
// book: "1" (chapter), "1.1" (section), "1.1.1" (subsection)
// in appendix: "A" (chapter), "A.1" (section)
pub fn format_section_number(counters, counter_name, in_appendix) {
    let chap = if (in_appendix) util.upper_alph(counters.chapter) else string(counters.chapter)
    match counter_name {
        case "part":
            roman_numeral(counters.part)
        case "chapter":
            chap
        case "section":
            chap ++ "." ++ (counters.section)
        case "subsection":
            chap ++ "." ++ (counters.section) ++ "." ++ (counters.subsection)
        case "subsubsection":
            chap ++ "." ++ (counters.section) ++ "." ++ (counters.subsection) ++ "." ++ (counters.subsubsection)
        default: null
    }
}

// ============================================================
// Roman numeral helper (for \part numbering)
// ============================================================

fn roman_numeral(n) {
    if (n <= 0) string(n)
    else roman_rec(n, "")
}

fn roman_rec(n, acc) {
    if (n >= 1000) roman_rec(n - 1000, acc ++ "M")
    else if (n >= 900) roman_rec(n - 900, acc ++ "CM")
    else if (n >= 500) roman_rec(n - 500, acc ++ "D")
    else if (n >= 400) roman_rec(n - 400, acc ++ "CD")
    else if (n >= 100) roman_rec(n - 100, acc ++ "C")
    else if (n >= 90) roman_rec(n - 90, acc ++ "XC")
    else if (n >= 50) roman_rec(n - 50, acc ++ "L")
    else if (n >= 40) roman_rec(n - 40, acc ++ "XL")
    else if (n >= 10) roman_rec(n - 10, acc ++ "X")
    else if (n >= 9) roman_rec(n - 9, acc ++ "IX")
    else if (n >= 5) roman_rec(n - 5, acc ++ "V")
    else if (n >= 4) roman_rec(n - 4, acc ++ "IV")
    else if (n >= 1) roman_rec(n - 1, acc ++ "I")
    else acc
}
