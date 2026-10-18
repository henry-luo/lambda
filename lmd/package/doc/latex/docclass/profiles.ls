// latex/docclass/profiles.ls — class profiles beyond the standard classes
// (vibe/impl/Lambda_Impl_Latex_Packages.md §14): option schemas, the commands each
// class family owns, built-in theorems and the helpers their handlers share.
// registry.CLASSES assembles the profiles; analysis and render dispatch a family's
// commands through their handler tables, as packages' are (M0.1).

import util: ~~.util

// ---- options ------------------------------------------------------------------------

let PAPERS = ["a4paper", "a5paper", "b5paper", "letterpaper", "legalpaper", "executivepaper"]
pub let STANDARD_OPTIONS = ["10pt", "11pt", "12pt", "oneside", "twoside", "onecolumn", "twocolumn",
    "draft", "final", "titlepage", "notitlepage", "leqno", "fleqn", "openbib", "landscape", "portrait"] ++ PAPERS
pub let CHAPTER_OPTIONS = STANDARD_OPTIONS ++ ["openright", "openany"]
// the extsizes classes' sizes; the stylesheet keeps one screen size for every size
// option, as it does for the standard classes' three
pub let EXT_OPTIONS = STANDARD_OPTIONS ++ ["8pt", "9pt", "14pt", "17pt", "20pt"]
pub let STANDALONE_OPTIONS = ["varwidth", "border", "preview", "crop", "tikz", "class", "convert", "multi",
    "float", "beamer", "png", "ignorerest", "11pt", "12pt", "10pt"]
pub let AMS_OPTIONS = STANDARD_OPTIONS ++ ["8pt", "9pt", "reqno", "tbtags", "centertags", "nomath",
    "noamsfonts", "psamsfonts", "makeidx", "openright", "openany"]
// KOMA-Script's key=value options; parskip is read through the parskip profile
pub let KOMA_OPTIONS = CHAPTER_OPTIONS ++ ["fontsize", "paper", "DIV", "BCOR", "parskip", "headings",
    "titlepage", "abstract", "toc", "listof", "bibliography", "index", "numbers", "captions", "open",
    "chapterprefix", "appendixprefix", "headinclude", "footinclude", "headsepline", "footsepline",
    "cleardoublepage", "version", "twoside", "twocolumn", "headlines", "pagesize", "footnotes",
    "egregdoesnotlikesansseriftitles", "DIVcalc", "DIVclassic"]
pub let MEMOIR_OPTIONS = EXT_OPTIONS ++ ["openright", "openany", "openleft", "article", "ms", "showtrims",
    "oldfontcommands", "extrafontsizes", "ebook", "statementpaper", "dbillpaper", "mcrownvopaper",
    "royalvopaper", "smalldemyvopaper", "demyvopaper", "largecrownvopaper", "pottvopaper",
    "foolscapvopaper", "crownvopaper", "postvopaper", "largepostvopaper", "mdemyvopaper"]
pub let IEEE_OPTIONS = STANDARD_OPTIONS ++ ["9pt", "conference", "journal", "technote", "peerreview",
    "peerreviewca", "compsoc", "comsoc", "transmag", "draftcls", "draftclsnofoot", "romanappendices",
    "captionsoff", "nofonttune", "invited", "nocompsoc"]
pub let LLNCS_OPTIONS = STANDARD_OPTIONS ++ ["envcountsame", "envcountreset", "envcountsect", "runningheads",
    "orivec", "citeauthoryear", "oribibl", "vecphys"]
pub let ELSARTICLE_OPTIONS = STANDARD_OPTIONS ++ ["preprint", "review", "1p", "3p", "5p", "authoryear",
    "number", "sort&compress", "longtitle", "times", "reversenotenum", "lefttitle", "centertitle", "nopreprintline"]
pub let REVTEX_OPTIONS = STANDARD_OPTIONS ++ ["aps", "aip", "sor", "prl", "pra", "prb", "prc", "prd", "pre",
    "prx", "prapplied", "prfluids", "prmaterials", "prab", "prper", "physrev", "rmp", "reprint", "preprint",
    "superscriptaddress", "groupedaddress", "showpacs", "noshowpacs", "showkeys", "noshowkeys",
    "amsmath", "amssymb", "amsfonts", "nofootinbib", "footinbib", "longbibliography", "floatfix",
    "nobibnotes", "bibnotes", "linenumbers", "galley", "byrevtex", "tightenlines", "nopreprintnumbers",
    "preprintnumbers", "eprint", "noeprint", "floats", "endfloats", "endfloats*"]
pub let ACMART_OPTIONS = STANDARD_OPTIONS ++ ["manuscript", "acmsmall", "acmlarge", "acmtog", "sigconf",
    "sigplan", "sigchi", "sigchi-a", "acmengage", "acmcp", "review", "screen", "natbib", "anonymous",
    "authorversion", "nonacm", "timestamp", "authordraft", "acmthm", "balance", "pbalance", "urlbreakonhyphens",
    "printccs", "printacmref", "printfolios"]
pub let LETTER_OPTIONS = STANDARD_OPTIONS
pub let EXAM_OPTIONS = STANDARD_OPTIONS ++ ["answers", "addpoints", "noaddpoints", "cancelspace"]

// ---- commands -----------------------------------------------------------------------

// title-block fields: a field given before \maketitle (or an elsarticle frontmatter)
// joins the title block; after it, the command renders where it stands
pub let KOMA_FIELDS = ["subtitle", "subject", "publishers", "dedication", "titlehead", "extratitle",
    "uppertitleback", "lowertitleback"]
pub let KOMA_HEADINGS = ["addchap", "addsec", "addpart", "minisec"]
pub let KOMA_SETTINGS = ["KOMAoptions", "KOMAoption", "setkomafont", "addtokomafont"]
pub let KOMA_COMMANDS = KOMA_FIELDS ++ KOMA_HEADINGS ++ KOMA_SETTINGS ++ ["captionabove", "captionbelow"]

// the AMS classes' authors, addresses and classification
pub let AMS_FIELDS = ["dedicatory", "subjclass", "keywords", "translator", "abstract"]
pub let AMS_ADDRESSES = ["address", "curraddr", "email", "urladdr"]
pub let AMS_COMMANDS = ["author"] ++ AMS_ADDRESSES ++ AMS_FIELDS

// memoir: layout commands set a printed page and are reported
pub let MEMOIR_LAYOUT = ["setlrmarginsandblock", "setulmarginsandblock", "checkandfixthelayout",
    "settypeblocksize", "setstocksize", "settrimmedsize", "settrims", "setheadfoot", "setheaderspaces",
    "setmarginnotes", "setlrmargins", "setulmargins", "setbinding", "setcolsepandrule", "fixthelayout",
    "semiisopage", "isopage", "medievalpage"]
pub let MEMOIR_SETTINGS = ["chapterstyle", "newfloat", "setsecnumdepth", "maxsecnumdepth"]
pub let MEMOIR_COMMANDS = MEMOIR_SETTINGS ++ ["epigraph"] ++ MEMOIR_LAYOUT

pub let IEEE_COMMANDS = ["IEEEauthorblockN", "IEEEauthorblockA", "IEEEPARstart", "IEEEkeywords",
    "IEEEmembership", "IEEEproof", "IEEEoverridecommandlockouts", "IEEEpeerreviewmaketitle", "IEEEspecialpapernotice"]
pub let IEEE_PREAMBLE = ["IEEEoverridecommandlockouts"]

// llncs's running heads have no page to run on
pub let LLNCS_COMMANDS = ["institute", "inst", "email", "orcidID", "titlerunning", "authorrunning",
    "keywords", "subtitle"]
pub let LLNCS_PREAMBLE = ["titlerunning", "authorrunning"]

pub let ELSARTICLE_FIELDS = ["abstract", "keyword", "highlights", "graphicalabstract"]
pub let ELSARTICLE_COMMANDS = ["frontmatter", "author", "address", "affiliation", "ead", "cortext", "corref",
    "fntext", "fnref", "tnotetext", "tnoteref", "sep", "journal", "MSC", "PACS"] ++ ELSARTICLE_FIELDS
pub let ELSARTICLE_PREAMBLE = ["journal"]

pub let REVTEX_FIELDS = ["abstract", "keywords", "pacs", "preprint"]
pub let REVTEX_COMMANDS = ["author", "affiliation", "altaffiliation", "email", "homepage", "collaboration",
    "noaffiliation"] ++ REVTEX_FIELDS

// acmart: rights and publication metadata set the printed rights block, which is
// not rendered, so they are reported
pub let ACMART_METADATA = ["setcopyright", "copyrightyear", "acmYear", "acmDOI", "acmConference",
    "acmBooktitle", "acmPrice", "acmISBN", "acmJournal", "acmVolume", "acmNumber", "acmArticle",
    "acmMonth", "acmSubmissionID", "received", "settopmatter", "authorsaddresses", "acmArticleType",
    "acmCodeLink", "acmDataLink", "acmContributions", "citestyle"]
pub let ACMART_FIELDS = ["abstract", "keywords", "ccsdesc", "teaserfigure", "subtitle"]
// the parts of an \affiliation, in print order
pub let ACMART_PLACES = ["position", "department", "institution", "streetaddress", "city", "state",
    "postcode", "country"]
let ACMART_AUTHORS = ["author", "affiliation", "additionalaffiliation", "email", "orcid", "CCSXML", "shortauthors"]
pub let ACMART_COMMANDS = ACMART_AUTHORS ++ ACMART_FIELDS ++ ACMART_PLACES ++ ACMART_METADATA
pub let ACMART_PREAMBLE = ACMART_AUTHORS ++ ACMART_FIELDS ++ ACMART_PLACES

pub let LETTER_FIELDS = ["address", "signature", "name", "location", "telephone"]
pub let LETTER_COMMANDS = ["letter", "opening", "closing", "ps", "encl", "cc", "makelabels"] ++ LETTER_FIELDS

// exam: the formats of labels, points and solutions, and the header and footer
// commands, which set printed pages, are reported
pub let EXAM_LISTS = {questions: "question", parts: "part", subparts: "subpart", subsubparts: "subsubpart",
    choices: "choice", oneparchoices: "choice", checkboxes: "choice", oneparcheckboxes: "choice"}
pub let EXAM_SETTINGS = ["printanswers", "noprintanswers", "pointname", "bracketedpoints"]
let EXAM_FORMATS = ["pointsinmargin", "pointsinrightmargin", "nopointsinmargin", "pointformat", "qformat",
    "unframedsolutions", "framedsolutions", "shadedsolutions", "SolutionEmphasis", "CorrectChoiceEmphasis",
    "checkboxchar", "checkedchar"]
let EXAM_PAGES = ["header", "footer", "firstpageheader", "runningheader", "firstpagefooter",
    "runningfooter", "headrule", "footrule", "firstpageheadrule", "runningheadrule", "firstpagefootrule",
    "runningfootrule", "extraheadheight", "extrafootheight"]
pub let EXAM_COMMANDS = [for (k, v at EXAM_LISTS) string(k)] ++ ["question", "part", "subpart", "subsubpart",
    "choice", "CorrectChoice", "solution", "numpoints", "numquestions", "points"] ++ EXAM_SETTINGS ++
    EXAM_FORMATS ++ EXAM_PAGES

// ---- the title block ----------------------------------------------------------------

// where a field prints in the title block; the others follow it in source order,
// the teaser first
pub let FIELD_SLOTS = {extratitle: "top", titlehead: "top", subject: "top", preprint: "top",
    subtitle: "below_title", institute: "below_author", dedicatory: "below_author", publishers: "below_date",
    teaserfigure: "teaser", cortext: "notes", fntext: "notes", tnotetext: "notes", journal: "end"}

// the head a field prints before its text, by class family where they differ
pub fn field_head(tag, family) {
    if (tag == "keywords" and family == "ams") "Key words and phrases."
    else if (tag == "keywords" or tag == "keyword") "Keywords:"
    else if (tag == "IEEEkeywords") "Index Terms—"
    else if (tag == "subjclass") "Mathematics Subject Classification."
    else if (tag == "pacs") "PACS numbers:"
    else if (tag == "ccsdesc") "CCS Concepts:"
    else if (tag == "journal") "Preprint submitted to"
    else if (tag == "translator") "Translated by"
    else if (tag == "highlights") "Highlights"
    else if (tag == "graphicalabstract") "Graphical abstract"
    else null
}

// elsarticle's note marks: corresponding authors ∗, footnotes 1, title notes ☆
pub fn note_mark(kind, i) {
    if (kind == "fntext") string(i + 1)
    else util.str_repeat(if (kind == "tnotetext") "☆" else "∗", i + 1)
}

// the note kind a reference command names
pub let NOTE_REFS = {corref: "cortext", fnref: "fntext", tnoteref: "tnotetext"}

// ---- built-in theorems --------------------------------------------------------------

// as \newtheorem would define them: {name, label, style}; each has its own counter
fn theorems(style, names) => [for (pair in names) {name: pair[0], label: pair[1], style: style}]

// llncs's \spnewtheorem environments: bold heads, the plain ones with italic bodies
pub let LLNCS_THEOREMS = theorems("plain", [["theorem", "Theorem"], ["lemma", "Lemma"],
    ["corollary", "Corollary"], ["proposition", "Proposition"], ["conjecture", "Conjecture"]]) ++
    theorems("definition", [["definition", "Definition"], ["example", "Example"], ["exercise", "Exercise"],
    ["problem", "Problem"], ["property", "Property"], ["question", "Question"], ["solution", "Solution"]]) ++
    theorems("remark", [["remark", "Remark"], ["note", "Note"], ["claim", "Claim"], ["case", "Case"]])
// acmart's acmthm theorems
pub let ACMART_THEOREMS = theorems("plain", [["theorem", "Theorem"], ["conjecture", "Conjecture"],
    ["proposition", "Proposition"], ["lemma", "Lemma"], ["corollary", "Corollary"]]) ++
    theorems("definition", [["example", "Example"], ["definition", "Definition"]])

// ---- KOMA-Script --------------------------------------------------------------------

// the font elements \setkomafont and \addtokomafont style: heading levels (the
// disposition is all of them) and title-block parts
pub let KOMA_HEADING_ELEMENTS = {disposition: ["part", "chapter", "section", "subsection", "subsubsection",
    "paragraph", "subparagraph", "minisec"], part: ["part"], partnumber: ["part"], chapter: ["chapter"],
    section: ["section"], subsection: ["subsection"], subsubsection: ["subsubsection"],
    paragraph: ["paragraph"], subparagraph: ["subparagraph"], minisec: ["minisec"]}
pub let KOMA_TITLE_ELEMENTS = ["title", "subtitle", "author", "date", "publishers", "subject", "dedication",
    "titlehead"]

// the setting keys of the levels or title part an element names: heading levels
// as sectsty records them; null for an element outside the bounded subset
pub fn font_keys(target) {
    let levels = KOMA_HEADING_ELEMENTS[target]
    if (levels != null) [for (level in levels) "sectsty:" ++ level]
    else if (contains(KOMA_TITLE_ELEMENTS, target) or false) ["koma-font:" ++ target]
    else null
}

// \setkomafont replaces an element's font; the class's own for headings and the
// title is sans-serif bold, so a replacement starts from the serif family
pub let KOMA_FONT_RESET = "\\rmfamily\\mdseries "

// ---- memoir -------------------------------------------------------------------------

// memoir's named chapter styles as titlesec formats ({shape, format, label, sep, before});
// other styles render as the default and are reported
pub let CHAPTER_STYLES = {
    default: {shape: "display", format: "\\huge\\bfseries", label: "\\chaptername\\ \\thechapter", sep: "20pt", before: "\\Huge"},
    section: {shape: "hang", format: "\\Huge\\bfseries", label: "\\thechapter", sep: "1em", before: ""},
    hangnum: {shape: "hang", format: "\\Huge\\bfseries", label: "\\thechapter", sep: "1em", before: ""},
    article: {shape: "hang", format: "\\Large\\bfseries", label: "\\thechapter", sep: "1em", before: ""},
    komalike: {shape: "hang", format: "\\sffamily\\Huge\\bfseries", label: "\\thechapter", sep: "1em", before: ""},
    dash: {shape: "display", format: "\\centering\\Large", label: "--- \\thechapter\\ ---", sep: "10pt", before: "\\Huge"}
}

// \setsecnumdepth{level}: the numbering depth a level name gives
pub let SECNUM_DEPTHS = {part: -1, chapter: 0, section: 1, subsection: 2, subsubsection: 3, paragraph: 4,
    subparagraph: 5, all: 5, none: -10}

// ---- journals -----------------------------------------------------------------------

// llncs's \institute{A \and B}: one affiliation per \and-separated part
pub fn split_at_and(nodes) => split_on(nodes, 0, [], [])

fn split_on(nodes, i, current, acc) {
    if (i >= len(nodes)) acc ++ [current]
    else if (nodes[i] is element and string(name(nodes[i])) == "and") split_on(nodes, i + 1, [], acc ++ [current])
    else split_on(nodes, i + 1, current ++ [nodes[i]], acc)
}

// acmart's \ccsdesc[weight]{Concept~Sub~Leaf}: the concept path and its weight
pub fn ccs_concept(el) {
    let raw = util.raw_argument(el, "required", 0) or ""
    {path: [for (part in split(raw, "~") where trim(part) != "") trim(part)],
     weight: int(trim(util.optional_raw(el) or "100")) ^ { 100 }}
}

// revtex's superscriptaddress: affiliations numbered in order of first use, and each
// author marked with the numbers of the affiliations that follow it
pub fn superscript_marks(entries) {
    let affils = [for (e in entries where is_affil(e)) util.text_of(e.el)]
    let unique = [for (i, text in affils where not any([for (j, other in affils where j < i) other == text])) text]
    let numbered = [for (e in entries) {*:e, number: if (is_affil(e))
        string(position(unique, util.text_of(e.el)) + 1) else null}];
    let marked = [for (i, e in numbered) {*:e, marks: if (e.kind == "affil") (if (e.number == null) [] else [e.number])
        else [for (j, f in numbered where j > i and f.number != null and
            not any([for (k, g in numbered where k > i and k < j and g.kind == "author") true])) f.number]}];
    // each affiliation is listed once, under its number
    [for (i, e in marked where not (e.number != null and any([for (j, f in marked where j < i) f.number == e.number]))) e]
}

// an affiliation entry, not an email or a homepage
fn is_affil(entry) => entry.kind == "affil" and entry.email != true

fn position(items, item) => [for (k, x in items where x == item) k][0]

// a run of nodes without the space at its edges (llncs's \institute parts)
pub fn trim_edges(nodes) {
    let n = len(nodes);
    [for (i, node in nodes) if (not (node is string)) node
        else (let text = (if (i == 0) trim_start(node) else node), if (i == n - 1) trim_end(text) else text)]
}

// ---- exam ---------------------------------------------------------------------------

// a question's or part's points, from \question[2] or \part[2]
pub fn points_of(el) {
    let raw = if (el.toc != null) util.text_of(el.toc) else util.optional_raw(el)
    if (raw == null) null else float(trim(raw)) ^ { null }
}

// exam's point label: (2 points), [2 points] with \bracketedpoints; one point is
// singular, and \pointname names both
pub fn points_label(points, bracketed, point_name) {
    let word = if (point_name != null) point_name else if (points == 1.0) " point" else " points"
    let text = whole(points) ++ word
    if (bracketed) "[" ++ text ++ "]" else "(" ++ text ++ ")"
}

// a point count as exam prints it: 2, 2.5
pub fn whole(points) => if ((points == float(int(points))) ^ { false }) string(int(points)) else string(points)

// ---- stylesheets ----------------------------------------------------------------------

// the title-block fields every profile with them shares
let FIELDS_CSS = ".latex-title .latex-subtitle{font-size:1.25em;margin-bottom:0.4em;}
.latex-title .latex-subject,.latex-title .latex-titlehead,.latex-title .latex-extratitle{font-size:0.95em;margin-bottom:0.6em;}
.latex-title .latex-preprint{text-align:right;font-size:0.9em;}
.latex-title .latex-publishers{margin-top:0.6em;}
.latex-dedication,.latex-dedicatory{text-align:center;font-style:italic;margin:1.5em 0;}
.latex-authblocks{display:flex;flex-wrap:wrap;justify-content:center;gap:0.5em 2.5em;}
.latex-affil{font-size:0.9em;}
.latex-email{font-family:var(--latex-mono,monospace);font-size:0.85em;}
.latex-institutes{margin:0.5em 0;}
.latex-keywords,.latex-subjclass,.latex-ccsdesc,.latex-pacs,.latex-translator{margin:0.8em 3em;font-size:0.9em;}
.latex-field-head{font-weight:bold;}
.latex-highlights,.latex-graphicalabstract{margin:1em 0;}
.latex-journal{font-size:0.85em;font-style:italic;margin-top:1em;}
"

// each family's own; KOMA-Script sets headings and the title sans-serif bold
let STYLES = {
    koma: ".latex-koma h1,.latex-koma h2,.latex-koma h3,.latex-koma h4,.latex-koma h5,.latex-koma h6,.latex-koma .latex-title .title,.latex-koma .latex-minisec{font-family:var(--latex-sans,sans-serif);font-weight:bold;}
.latex-minisec{margin:0.8em 0 0.2em;}
",
    IEEEtran: ".latex-IEEEtran h2{text-align:center;font-variant:small-caps;font-size:1.1em;}
.latex-IEEEtran h3{font-style:italic;font-size:1em;}
.latex-parstart .latex-dropcap{float:left;font-size:3.2em;line-height:0.85;margin:0.05em 0.08em 0 0;}
.latex-parstart-rest{text-transform:uppercase;}
.latex-membership{font-style:italic;font-size:0.9em;}
.latex-paper-notice{font-style:italic;text-align:center;}
",
    memoir: ".latex-epigraph{margin:1em 0 1.5em auto;width:60%;font-size:0.95em;}
.latex-epigraph-source{border-top:0.4pt solid;padding-top:0.2em;text-align:right;}
",
    letter: ".latex-letter p{text-indent:0;margin:0 0 0.7em;}
.latex-letter-from{margin-left:55%;}
.latex-letter-date{margin:1em 0 1em 55%;}
.latex-letter-to{margin-bottom:1.5em;}
.latex-letter-opening{margin:1em 0;}
.latex-letter-closing{margin:1.5em 0 0 55%;}
.latex-signature{margin-top:3em;}
.latex-letter-ps,.latex-letter-cc,.latex-letter-encl{margin-top:1em;}
",
    exam: ".latex-exam-list>li{margin:0.6em 0;}
.latex-parts{list-style:none;counter-reset:latex-part;}
.latex-parts>li::before{counter-increment:latex-part;content:\"(\" counter(latex-part, lower-alpha) \") \";}
.latex-subparts{list-style-type:lower-roman;}
.latex-subsubparts{list-style-type:lower-greek;}
.latex-choices,.latex-oneparchoices{list-style-type:upper-alpha;}
.latex-oneparchoices,.latex-oneparcheckboxes{display:flex;flex-wrap:wrap;gap:0 2em;list-style-position:inside;padding-left:0;}
.latex-checkboxes,.latex-oneparcheckboxes{list-style:none;}
.latex-checkboxes>li::before,.latex-oneparcheckboxes>li::before{content:\"\u2610 \";}
.latex-checkboxes>li.latex-correct::before,.latex-oneparcheckboxes>li.latex-correct::before{content:\"\u2612 \";}
.latex-correct{font-weight:bold;}
.latex-points{font-size:0.9em;}
.latex-solution{border:0.4pt solid;padding:0.5em 0.8em;margin:0.5em 0;}
"
}

// the stylesheet of a class family (registry.class_family), empty for the standard classes
let FIELD_FAMILIES = ["koma", "ams", "IEEEtran", "llncs", "elsarticle", "revtex", "acmart"]

pub fn stylesheet(family) =>
    (if (contains(FIELD_FAMILIES, family) or false) FIELDS_CSS else "") ++
    (if (family == null) "" else (STYLES[family] or ""))
