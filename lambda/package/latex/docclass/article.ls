// docclass/article.ls — Article document class configuration
// LaTeX article class: no chapters, sections start at h2.

// ============================================================
// Section hierarchy
// ============================================================

// article has no chapter; section is the top level
pub SECTION_LEVELS = {
    section: 2,
    subsection: 3,
    subsubsection: 4,
    paragraph: 5,
    subparagraph: 6
}

pub TOP_LEVEL = 2  // <h2> for \section

// ============================================================
// Section numbering
// ============================================================

// format section number from counters
// article: "1", "1.1", "1.1.1", paragraphs unnumbered
pub fn format_section_number(counters, counter_name) {
    match counter_name {
        case "section":
            string(counters.section)
        case "subsection":
            string(counters.section) ++ "." ++ string(counters.subsection)
        case "subsubsection":
            string(counters.section) ++ "." ++ string(counters.subsection) ++ "." ++ string(counters.subsubsection)
        default: null
    }
}

// which counters reset when a parent increments
pub fn step_counter(counters, counter_name) {
    match counter_name {
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

pub fn figure_label(num) {
    "Figure " ++ string(num)
}

pub fn table_label(num) {
    "Table " ++ string(num)
}

pub fn equation_label(num) {
    "(" ++ string(num) ++ ")"
}

// ============================================================
// Document structure
// ============================================================

// article has no front/back matter distinction
pub HAS_CHAPTERS = false
pub HAS_PARTS = false

// abstract appears before \maketitle or after, depending on class options
pub ABSTRACT_POSITION = "before_body"

// ============================================================
// Theorem numbering — article uses single counter per env type
// ============================================================

pub fn theorem_label(env_display_name, num) {
    env_display_name ++ " " ++ string(num)
}
