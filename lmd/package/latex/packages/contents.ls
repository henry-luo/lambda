// latex/packages/contents.ls — tocloft's entry and title styles and minitoc's
// per-chapter contents (vibe/impl/Lambda_Impl_Latex_Packages.md §10). tocloft is
// configured by redefining its macros, which the binding tex/binding/tocloft.sty
// declares; the document's definitions arrive as macros (macros.last_macro). The
// continuous contents have no page numbers, so the page hooks, leaders and dot
// separations are accepted without effect.

import util: ~~.util
import macros: ~~.macros
import font_decl: ~~.elements.font_decl

// tocloft's entry names by heading level (analysis levels: part 0 … subparagraph 6)
let ENTRY_NAMES = ["part", "chap", "sec", "subsec", "subsubsec", "para", "subpara"]
pub fn entry_name(level) => if (level >= 0 and level < len(ENTRY_NAMES)) ENTRY_NAMES[level] else null

pub let TOCLOFT_COMMANDS = ["cftsetindents", "cftsetpnumwidth", "cftsetrmarg", "cftpagenumbersoff",
    "cftpagenumberson", "newlistof"]
// the options place titles and page styles
pub let TOCLOFT_OPTIONS = ["titles", "subfigure", "subfig", "float", "memoir", "rmarg",
    "tocloftpagestyle", "notoc", "notocbasic", "nolof", "nolot", "nofloats", "fixtitles"]

// a hook the document redefined, as source text, or null
fn hook(info, name) {
    let found = macros.last_macro(info.macros, name)
    if (found == null) null else found.source
}

// The style of a contents entry of tocloft entry `entry`, or null when nothing
// changes it: {css (the entry), number_css, before, after (text around the number),
// after_box (text after the number's box)}
pub fn entry_style(info, entry) {
    let font = hook(info, "cft" ++ entry ++ "font")
    let presnum = hook(info, "cft" ++ entry ++ "presnum")
    let aftersnum = hook(info, "cft" ++ entry ++ "aftersnum")
    let aftersnumb = hook(info, "cft" ++ entry ++ "aftersnumb")
    let indents = util.setting_at(info.settings, "cftindents:" ++ entry, null)
    let skip = util.setting_at(info.settings, "length:cftbefore" ++ entry ++ "skip", null)
    if (font == null and presnum == null and aftersnum == null and aftersnumb == null and
        indents == null and skip == null) null
    else {
        let decls = font_decl.declarations(font, info.custom_colors)
        {css: decls.css ++ (if (indents == null or indents.indent == null) "" else "padding-left:" ++ indents.indent ++ ";") ++
            (if (skip == null) "" else "margin-top:" ++ skip ++ ";"),
         number_css: if (indents == null or indents.width == null) null
            else "display:inline-block;min-width:" ++ indents.width,
         before: font_decl.declarations(presnum, info.custom_colors).text,
         after: font_decl.declarations(aftersnum, info.custom_colors).text,
         after_box: font_decl.declarations(aftersnumb, info.custom_colors).text}
    }
}

// a list's title: the renamed name macro (\contentsname, \listfigurename, …) or the
// default, and tocloft's title font and after-title text for that list's file
pub fn title(info, name_macro, fallback, file) {
    let renamed = macros.last_macro(info.macros, name_macro)
    let font = font_decl.declarations(hook(info, "cft" ++ file ++ "titlefont"), info.custom_colors)
    {body: if (renamed == null or renamed.body == null) null else renamed.body, text: fallback,
     css: if (font.css == "" and font.align == null) null
        else font.css ++ (if (font.align == null) "" else "text-align:" ++ font.align ++ ";"),
     after: font_decl.declarations(hook(info, "cftafter" ++ file ++ "title"), info.custom_colors).text}
}

// \cftsetindents{entry}{indent}{numwidth}: {key, value} for the settings list
pub fn indents(el) {
    let entry = util.raw_argument(el, "required", 0)
    if (entry == null) null
    else {key: "cftindents:" ++ trim(entry),
        value: {indent: util.css_dimension(trim(util.raw_argument(el, "required", 1) or "")),
                width: util.css_dimension(trim(util.raw_argument(el, "required", 2) or ""))}}
}

// ---- minitoc ---------------------------------------------------------------------

pub let MINITOC_COMMANDS = ["dominitoc", "minitoc", "dosecttoc", "secttoc", "doparttoc", "parttoc",
    "faketableofcontents", "mtcaddchapter", "mtcaddsection", "mtcaddpart", "mtcsettitle", "adjustmtc",
    "adjuststc", "adjustptc"]
pub let MINITOC_OPTIONS = ["tight", "k-tight", "loose", "k-loose", "insection", "notoc", "notlof", "notlot",
    "notoccc", "hints", "nohints", "undotted", "dotted", "delayprint", "listfiles", "nolistfiles",
    "checkfiles", "nocheckfiles", "english", "vietnam", "french", "german", "spanish", "italian",
    "russian", "greek"]

// the mini contents a command prints: its heading level (part 0, chapter 1,
// section 2), title macro and depth counter
let MINI = {minitoc: {level: 1, title: "mtctitle", depth: "minitocdepth", key: "minitoc"},
    secttoc: {level: 2, title: "stctitle", depth: "secttocdepth", key: "secttoc"},
    parttoc: {level: 0, title: "ptctitle", depth: "parttocdepth", key: "parttoc"}}

pub fn mini_kind(command) => MINI[command]

// The headings a mini contents at `offset` lists: those after the last heading of
// its level before it, down to the depth counter (LaTeX levels, so 2 means down to
// subsections under a chapter), until the next heading of that level or above
pub fn mini_entries(headings, kind, depth, offset) {
    let owners = [for (i, h in headings where h.offset != null and h.offset < offset and h.level <= kind.level) i]
    if (len(owners) == 0) []
    else {
        let after = slice(headings, owners[len(owners) - 1] + 1, len(headings))
        let stop = [for (i, h in after where h.level <= kind.level) i]
        let span = if (len(stop) == 0) after else slice(after, 0, stop[0]);
        [for (h in span where h.level <= depth + 1) h]
    }
}
