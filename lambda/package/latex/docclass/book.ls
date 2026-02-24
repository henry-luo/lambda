// docclass/book.ls — Book document class configuration
// LaTeX book class: has chapters, parts, front/back matter.

// ============================================================
// Section hierarchy
// ============================================================

// book has chapters; chapter is the top level
pub SECTION_LEVELS = {
    part: 0,
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

// format section number from counters
// book: "1" (chapter), "1.1" (section), "1.1.1" (subsection)
pub fn format_section_number(counters, counter_name) {
    match counter_name {
        case "part":
            roman_numeral(counters.part)
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
        case "part":
            ({part: counters.part + 1, counters})
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

// book: figures/tables numbered by chapter: "Figure 1.3"
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
pub HAS_PARTS = true

// book can have \frontmatter, \mainmatter, \backmatter
pub ABSTRACT_POSITION = "after_title"

// ============================================================
// Theorem numbering — book numbers by chapter: "Theorem 2.1"
// ============================================================

pub fn theorem_label(env_display_name, counters, num) {
    env_display_name ++ " " ++ string(counters.chapter) ++ "." ++ string(num)
}

// ============================================================
// Part display — "Part I", "Part II", etc.
// ============================================================

pub fn part_label(num) {
    "Part " ++ roman_numeral(num)
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
