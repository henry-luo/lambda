// docclass/report.ls — Report document class configuration
// LaTeX report class: like book but no \part, chapters don't force new page.
// Chapters and sections are numbered like book.

// ============================================================
// Section hierarchy
// ============================================================

// report has chapters but no parts; similar to book
pub SECTION_LEVELS = {
    chapter: 1,
    section: 2,
    subsection: 3,
    subsubsection: 4,
    paragraph: 5,
    subparagraph: 6
}

pub TOP_LEVEL = 1  // <h1> for \chapter

// ============================================================
// Section numbering
// ============================================================

// format section number: same as book (chapter.section.subsection)
pub fn format_section_number(counters, counter_name) {
    match counter_name {
        case "chapter":
            string(counters.chapter)
        case "section":
            string(counters.chapter) ++ "." ++ string(counters.section)
        case "subsection":
            string(counters.chapter) ++ "." ++ string(counters.section) ++ "." ++ string(counters.subsection)
        case "subsubsection":
            string(counters.chapter) ++ "." ++ string(counters.section) ++ "." ++ string(counters.subsection) ++ "." ++ string(counters.subsubsection)
        default: null
    }
}

// which counters reset when a parent increments
pub fn step_counter(counters, counter_name) {
    match counter_name {
        case "chapter":
            ({chapter: counters.chapter + 1, section: 0, subsection: 0, subsubsection: 0, counters})
        case "section":
            ({section: counters.section + 1, subsection: 0, subsubsection: 0, counters})
        case "subsection":
            ({subsection: counters.subsection + 1, subsubsection: 0, counters})
        case "subsubsection":
            ({subsubsection: counters.subsubsection + 1, counters})
        default: counters
    }
}

// ============================================================
// Caption labels
// ============================================================

// report: figures/tables numbered by chapter like book
pub fn figure_label(counters, num) {
    "Figure " ++ string(counters.chapter) ++ "." ++ string(num)
}

pub fn table_label(counters, num) {
    "Table " ++ string(counters.chapter) ++ "." ++ string(num)
}

pub fn equation_label(counters, num) {
    "(" ++ string(counters.chapter) ++ "." ++ string(num) ++ ")"
}

// ============================================================
// Document structure
// ============================================================

pub HAS_CHAPTERS = true
pub HAS_PARTS = false

pub ABSTRACT_POSITION = "separate_page"  // report puts abstract on its own page

// ============================================================
// Theorem numbering — report numbers by chapter like book
// ============================================================

pub fn theorem_label(env_display_name, counters, num) {
    env_display_name ++ " " ++ string(counters.chapter) ++ "." ++ string(num)
}
