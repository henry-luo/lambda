// docclass/article.ls — Article document class numbering
// LaTeX article class: no chapters; \section is the top numbered level.

import util: lambda.doc.latex.util

// format section number from counters
// article: "1", "1.1", "1.1.1", paragraphs unnumbered
// in appendix: "A", "A.1", "A.1.1"
pub fn format_section_number(counters, counter_name, in_appendix) {
    let sec = if (in_appendix) util.upper_alph(counters.section) else string(counters.section)
    match counter_name {
        case "section":
            sec
        case "subsection":
            sec ++ "." ++ (counters.subsection)
        case "subsubsection":
            sec ++ "." ++ (counters.subsection) ++ "." ++ (counters.subsubsection)
        default: null
    }
}
