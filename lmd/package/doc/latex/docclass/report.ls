// docclass/report.ls — Report document class numbering
// LaTeX report class: like book but no \part, chapters don't force new page.
// Chapters and sections are numbered like book.

import util: lambda.doc.latex.util

// format section number: same as book (chapter.section.subsection)
// in appendix: "A", "A.1" etc.
pub fn format_section_number(counters, counter_name, in_appendix) {
    let chap = if (in_appendix) util.upper_alph(counters.chapter) else string(counters.chapter)
    match counter_name {
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
