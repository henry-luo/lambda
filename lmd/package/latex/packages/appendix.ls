// latex/packages/appendix.ls — appendix: the appendices and subappendices environments,
// \appendixpage and \addappheadtotoc, on \appendix's lettered counters (analyze.ls).
// The header option names appendices in running heads and is not supported.

import util: ~~.util

pub let OPTIONS = ["toc", "page", "title", "titletoc"]
pub let COMMANDS = ["appendices", "subappendices", "appendixpage", "addappheadtotoc"]

// \appendixname, and \appendixtocname = \appendixpagename
pub let NAME = "Appendix"
pub let HEAD = "Appendices"

pub fn chaptered(docclass) => docclass == "book" or docclass == "report"

// the counter the environment letters: the class's top level in appendices, the
// level below it in subappendices (1.A in book and report, 1.A for article's
// subsections)
pub fn lettered_counter(chaptered, sub) {
    if (chaptered) (if (sub) "section" else "chapter")
    else (if (sub) "subsection" else "section")
}

// title names each appendix heading "Appendix A" and titletoc names its contents
// entry; subappendices name neither
pub fn names(options) {
    if (options == null) null
    else {title: util.option_enabled(options.title), toc: util.option_enabled(options.titletoc)}
}
