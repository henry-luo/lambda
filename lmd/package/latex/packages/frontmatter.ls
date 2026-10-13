// latex/packages/frontmatter.ls — the title block's packages: titling's pre/post
// code, \droptitle and \maketitle hooks, and authblk's authors and affiliations
// (vibe/impl/Lambda_Impl_Latex_Packages.md §10). The macros a document redefines
// (\Authfont, \maketitlehooka, …) are declared by the bindings
// tex/binding/{titling,authblk}.sty and read here as the document's definitions.

import util: ~~.util
import macros: ~~.macros
import font_decl: ~~.elements.font_decl

// ---- titling ------------------------------------------------------------------------

pub let TITLING_PARTS = ["title", "author", "date"]
pub let TITLING_COMMANDS = ["pretitle", "posttitle", "preauthor", "postauthor", "predate", "postdate",
    "thetitle", "theauthor", "thedate"]
pub let TITLING_OPTIONS = ["titlepage", "notitlepage", "pagestyles", "placement"]

// The style a part (title, author, date) gets from its \pre… and \post… code, read
// as one declaration list since an environment opened in one closes in the other:
// {css, before, after} with the code's own text before and after the part, or null
pub fn part_style(settings, part, colors) {
    let pre = util.setting_at(settings, "titling:pre" ++ part, null)
    let post = util.setting_at(settings, "titling:post" ++ part, null)
    if (pre == null and post == null) null
    else {
        let opening = font_decl.declarations(pre, colors)
        let closing = font_decl.declarations(post, colors)
        let decls = font_decl.declarations((pre or "") ++ (post or ""), colors)
        {css: decls.css ++ (if (decls.align == null) "" else "text-align:" ++ decls.align ++ ";") ++
            (if (opening.space == null) "" else "margin-top:" ++ opening.space ++ ";") ++
            (if (closing.space == null) "" else "margin-bottom:" ++ closing.space ++ ";"),
         before: trim(opening.text), after: trim(closing.text), unknown: decls.unknown}
    }
}

// the hook macro's body for slot a–d, or null
pub fn hook(info, slot) {
    let found = macros.last_macro(info.macros, "maketitlehook" ++ slot)
    if (found == null) null else found.body
}

// ---- authblk ------------------------------------------------------------------------

pub let AUTHBLK_COMMANDS = ["author", "affil"]
pub let AUTHBLK_OPTIONS = ["auth-sc", "auth-sc-lg", "auth-lg", "affil-sl", "affil-it", "noblocks", "blocks"]

fn source(info, name, fallback) {
    let found = macros.last_macro(info.macros, name)
    if (found == null) fallback else found.source
}

// authblk's separators: \Authsep between names, \Authand between two and \Authands
// before the last of more
pub fn separators(info) => {sep: font_decl.declarations(source(info, "Authsep", ", "), null).text,
    and_two: font_decl.declarations(source(info, "Authand", " and "), null).text,
    and_many: font_decl.declarations(source(info, "Authands", ", and "), null).text}

// the CSS of the author names and of the affiliations: the options, then \Authfont
// and \Affilfont
pub fn fonts(info, options) {
    let author = font_decl.declarations(source(info, "Authfont", ""), info.custom_colors)
    let affil = font_decl.declarations(source(info, "Affilfont", ""), info.custom_colors)
    {author: (if (util.option_on(options, "auth-sc") or util.option_on(options, "auth-sc-lg")) "font-variant:small-caps;" else "") ++
            (if (util.option_on(options, "auth-lg") or util.option_on(options, "auth-sc-lg")) "font-size:1.1em;" else "") ++ author.css,
     affil: (if (util.option_on(options, "affil-sl")) "font-style:oblique;"
            else if (util.option_on(options, "affil-it")) "font-style:italic;" else "") ++ affil.css}
}


// the marks of an \author or \affil: [1,2] → ["1", "2"]; none for a plain one
pub fn marks(el) {
    let raw = util.optional_raw(el)
    if (raw == null) [] else [for (part in split(raw, ",") where trim(part) != "") trim(part)]
}

// names joined as authblk joins them; each name is a list of items
pub fn joined(names, seps) {
    let n = len(names);
    [for (i, items in names, item in [*(if (i == 0) [] else if (i == n - 1) [if (n == 2) seps.and_two else seps.and_many]
        else [seps.sep]), *items]) item]
}

// blocks: without marks, the authors before each run of affiliations share them
pub fn blocks(entries) {
    let starts = [for (i, e in entries where e.kind == "author" and (i == 0 or entries[i - 1].kind == "affil")) i];
    [for (k, first in starts, let stop = if (k + 1 < len(starts)) starts[k + 1] else len(entries),
        let group = slice(entries, first, stop))
        {authors: [for (e in group where e.kind == "author") e], affils: [for (e in group where e.kind == "affil") e]}]
}
