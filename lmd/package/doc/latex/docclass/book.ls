// docclass/book.ls — Book document class numbering
// LaTeX book class: parts and chapters; \chapter is the top numbered level.

import util: lambda.doc.latex.util

// format section number from counters
// book: "1" (chapter), "1.1" (section), "1.1.1" (subsection)
// in appendix: "A" (chapter), "A.1" (section)
pub fn format_section_number(counters, counter_name, in_appendix) {
    let chap = if (in_appendix) util.upper_alph(counters.chapter) else string(counters.chapter)
    match counter_name {
        case "part":
            upper(util.roman(counters.part))
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

