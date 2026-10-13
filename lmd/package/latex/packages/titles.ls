// latex/packages/titles.ls — heading styles from titlesec and sectsty
// (vibe/impl/Lambda_Impl_Latex_Packages.md §10). Analysis records \titleformat,
// \titlespacing, \titlelabel and the sectsty font commands as source-order settings;
// the heading renderer asks heading_style for the style in effect at a heading.
// Formats are declaration lists (font_decl.declarations); format_issues reports what
// they cannot express when analysis records them.

import util: ~~.util
import font_decl: ~~.elements.font_decl

pub let TITLESEC_COMMANDS = ["titleformat", "titlespacing", "titlelabel"]
// the font, alignment, size and spacing options; the others shape pages, which
// continuous output does not have
pub let TITLESEC_OPTIONS = ["rm", "sf", "tt", "md", "bf", "up", "it", "sl", "sc", "big", "medium",
    "small", "tiny", "raggedleft", "center", "raggedright", "compact", "explicit", "pagestyles",
    "clearempty", "toctitles", "newparttoc", "oldparttoc", "rigidchapters", "rubberchapters",
    "bottomtitles", "nobottomtitles", "nobottomtitles*", "indentafter", "noindentafter", "calcwidth",
    "nostruts", "aftersep", "largestsep", "loadonly"]

// sectsty's commands and the heading levels they style; chapter title and number
// have their own
pub let SECTSTY_FONTS = {partfont: ["part"], chapterfont: ["chapter"], sectionfont: ["section"],
    subsectionfont: ["subsection"], subsubsectionfont: ["subsubsection"], paragraphfont: ["paragraph"],
    subparagraphfont: ["subparagraph"], minisecfont: ["minisec"],
    chaptertitlefont: ["chapter-title"], chapternumberfont: ["chapter-number"],
    allsectionsfont: ["part", "chapter", "section", "subsection", "subsubsection", "paragraph", "subparagraph"]}
pub let SECTSTY_COMMANDS = [for (k, v at SECTSTY_FONTS) string(k)]

// titlesec's size options: the LaTeX size of each level (big is the classes' own)
let SIZES = {
    big: {part: "Huge", chapter: "Huge", section: "Large", subsection: "large"},
    medium: {part: "huge", chapter: "huge", section: "large", subsection: "normalsize"},
    small: {part: "LARGE", chapter: "LARGE", section: "normalsize", subsection: "normalsize"},
    tiny: {part: "normalsize", chapter: "normalsize", section: "normalsize", subsection: "normalsize"}}
let FONT_OPTIONS = {rm: "\\rmfamily", sf: "\\sffamily", tt: "\\ttfamily", md: "\\mdseries",
    bf: "\\bfseries", up: "\\upshape", it: "\\itshape", sl: "\\slshape", sc: "\\scshape"}
let ALIGN_OPTIONS = {raggedleft: "right", center: "center", raggedright: "left"}

// shapes CSS cannot reproduce render as hang and are reported
let SHAPES = ["hang", "block", "display"]

// the level a heading command names: \section → section
pub fn level_of(raw) {
    let plain = trim(util.strip_backslash(trim(raw or "")))
    if (plain == "paragraph_command") "paragraph" else plain
}

// ---- analysis ---------------------------------------------------------------------

// \titleformat{\cmd}[shape]{format}{label}{sep}{before}[after], or \titleformat*{\cmd}{format}
// which keeps the rest: {key, value} for the settings list, or null
pub fn titleformat(el, prior) {
    let level = level_of(util.raw_argument(el, "required", 0))
    if (level == "") null
    else if (el.starred == true)
        {key: "titleformat:" ++ level, value: {*:(prior or {}), format: util.raw_argument(el, "required", 1) or ""}}
    else {key: "titleformat:" ++ level,
        value: {shape: trim(util.raw_argument(el, "optional", 0) or "hang"),
            format: util.raw_argument(el, "required", 1) or "", label: util.raw_argument(el, "required", 2),
            sep: util.raw_argument(el, "required", 3), before: util.raw_argument(el, "required", 4) or "",
            after: util.raw_argument(el, "optional", 1) or ""}}
}

// \titlespacing*{\cmd}{left}{before}{after}[right]
pub fn titlespacing(el) {
    let level = level_of(util.raw_argument(el, "required", 0))
    if (level == "") null
    else {key: "titlespacing:" ++ level,
        value: {left: spacing(util.raw_argument(el, "required", 1)), before: spacing(util.raw_argument(el, "required", 2)),
            after: spacing(util.raw_argument(el, "required", 3)), right: spacing(util.raw_argument(el, "optional", 0)),
            indent_after: el.starred != true}}
}

// a titlesec space: *n is n ex, a rubber length its natural size
fn spacing(raw) {
    let text = trim(raw or "")
    if (text == "") null
    else if (starts_with(text, "*")) util.css_dimension(slice(text, 1, len(text)) ++ "ex")
    else util.css_dimension(split(text, " ")[0])
}

// the commands a recorded format cannot express, and a shape CSS cannot draw, as
// {code, item}
pub fn format_issues(value, colors) {
    let parts = [for (part in [value.format, value.before, value.after] where part != null) part]
    // a label's number and name commands are filled in, not declarations
    let label_unknown = if (value.label == null) [] else label_parts(value.label, "1", "", false, colors).unknown;
    [*[for (part in parts, command in font_decl.declarations(part, colors).unknown)
        {code: "unsupported-title-format", item: command}],
     *[for (command in label_unknown) {code: "unsupported-title-format", item: command}],
     *(if (value.shape == null or contains(SHAPES, value.shape) or value.shape == "frame" or false) []
       else [{code: "title-shape-approximation", item: value.shape}])]
}

// ---- rendering ----------------------------------------------------------------------

fn setting(info, key, offset) => util.setting_at(info.settings, key, offset)

// the label: the template with the number put in, as text and the template's own CSS
fn label_parts(template, num, level, appendix, colors) {
    let names = [["\\thesubsubsection", num], ["\\thesubsection", num], ["\\thesection", num],
        ["\\thesubparagraph", num], ["\\theparagraph", num], ["\\thechapter", num],
        ["\\thepart", num], ["\\thetitle", num],
        ["\\chaptertitlename", if (appendix) "Appendix" else "Chapter"], ["\\chaptername", "Chapter"],
        ["\\appendixname", "Appendix"], ["\\partname", "Part"]]
    let filled = substitute(template, names, 0)
    font_decl.declarations(filled, colors)
}

fn substitute(text, pairs, k) {
    if (k >= len(pairs)) text
    else substitute(replace(text, pairs[k][0], pairs[k][1]), pairs, k + 1)
}

fn css_of(decls) => decls.css ++ (if (decls.align == null) "" else "text-align:" ++ decls.align ++ ";")

// The style of a heading, or null when neither package changes it. `titlesec` is
// the package's options when it is loaded, `sectsty` whether that is.
//   css          the heading element's CSS (a \titlerule draws its bottom border)
//   label        {text, css, block, sep} or null for the class default
//   title_css    CSS for the title text
//   indent_after the paragraph after it is indented (\titlespacing without *)
pub fn heading_style(info, titlesec, sectsty, level, num, appendix, offset) {
    let fmt = if (titlesec == null) null else setting(info, "titleformat:" ++ level, offset)
    let space = if (titlesec == null) null else setting(info, "titlespacing:" ++ level, offset)
    let label_template = if (titlesec == null) null
        else if (fmt != null and fmt.label != null) fmt.label
        else setting(info, "titlelabel", offset)
    let fonts = if (not sectsty) null else setting(info, "sectsty:" ++ level, offset)
    let package_css = if (titlesec == null) "" else option_css(titlesec, level)
    if (fmt == null and space == null and label_template == null and fonts == null and package_css == "") null
    else {
        let colors = info.custom_colors
        let format_decls = font_decl.declarations(if (fmt == null) "" else fmt.format, colors)
        let before = font_decl.declarations(if (fmt == null) "" else fmt.before, colors)
        let after = font_decl.declarations(if (fmt == null) "" else fmt.after, colors)
        let font_decls = font_decl.declarations(fonts, colors)
        let chapter_title = if (not sectsty or level != "chapter") null
            else setting(info, "sectsty:chapter-title", offset)
        let chapter_number = if (not sectsty or level != "chapter") null
            else setting(info, "sectsty:chapter-number", offset)
        let heading_shape = if (fmt == null) "hang" else fmt.shape
        let label_decls = if (num == null or label_template == null) null
            else label_parts(label_template, num, level, appendix, colors)
        let sep = if (fmt == null or fmt.sep == null) null else util.css_dimension(trim(fmt.sep))
        let margins = if (space == null) ""
            else (if (space.left == null) "" else "margin-left:" ++ space.left ++ ";") ++
                (if (space.before == null) "" else "margin-top:" ++ space.before ++ ";") ++
                (if (space.after == null) "" else "margin-bottom:" ++ space.after ++ ";") ++
                (if (space.right == null) "" else "margin-right:" ++ space.right ++ ";")
        {css: package_css ++ css_of(format_decls) ++ css_of(font_decls) ++ margins ++
            (if (after.rule or before.rule) "border-bottom:0.4pt solid;padding-bottom:0.2em;" else "") ++
            (if (heading_shape == "frame") "border:0.4pt solid;padding:0.3em;" else ""),
         label: if (label_decls == null) null
            else {text: label_decls.text, css: css_of(label_decls) ++ css_of(font_decl.declarations(chapter_number, colors)),
                block: heading_shape == "display", sep: sep},
         title_css: css_of(before) ++ css_of(font_decl.declarations(chapter_title, colors)) ++
            (if (before.transform == null) "" else "text-transform:" ++ before.transform ++ ";"),
         indent_after: space != null and space.indent_after}
    }
}

// the CSS the titlesec options give every heading of a level
fn option_css(options, level) {
    let sizes = [for (k, table at SIZES where util.option_enabled(options[k]) and table[level] != null)
        table[level]]
    let fonts = join([for (k, decl at FONT_OPTIONS where util.option_enabled(options[k])) decl], "")
    let aligns = [for (k, side at ALIGN_OPTIONS where util.option_enabled(options[k])) side];
    css_of(font_decl.declarations((if (len(sizes) == 0) "" else "\\" ++ sizes[0]) ++ fonts, null)) ++
        (if (len(aligns) == 0) "" else "text-align:" ++ aligns[0] ++ ";") ++
        (if (util.option_enabled(options.compact)) "margin-top:0.8em;margin-bottom:0.4em;" else "")
}
