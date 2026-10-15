// Link presentation and document metadata are carried by ordinary HTML semantics.
import util: ~~.util

fn collect_setup(node, settings) {
    if (not (node is element)) settings
    else if (string(name(node)) == "hypersetup") {
        let stored = util.raw_argument(node, "required", 0)
        let raw = if (stored != null) stored else util.text_of(node)
        {*:settings, *:util.parse_kv_options(raw)}
    } else collect_children(node, 0, settings)
}

fn collect_children(node, i, settings) {
    if (i >= len(node)) settings
    else collect_children(node, i + 1, collect_setup(node[i], settings))
}

pub fn settings(ast, initial) {
    if (initial == null) null else collect_setup(ast, initial)
}

pub fn metadata(settings, title, author) {
    if (settings == null) {title: title, author: author}
    else {title: if (settings.pdftitle != null) settings.pdftitle else title,
          author: if (settings.pdfauthor != null) settings.pdfauthor else author,
          subject: settings.pdfsubject, keywords: settings.pdfkeywords}
}

pub fn outlines_enabled(settings) => settings != null and settings.bookmarks != "false"

pub fn issues(settings, offset) =>
    if (settings == null) [] else [for (key in ["bookmarks", "bookmarksnumbered"]
        where settings[key] != null and settings[key] != "true" and settings[key] != "false")
        util.diagnostic("invalid-bookmark-option", "hyperref", key,
            "Bookmark flags must be true or false", offset)]

pub fn explicit_bookmark(node, settings) {
    let title = util.raw_argument(node, "required", 0)
    let target = util.raw_argument(node, "required", 1)
    let level_raw = util.optional_raw(node)
    let level = if (level_raw == null) 0 else int(level_raw) ^ { -1 }
    if (settings == null or title == null or trim(title) == "" or target == null or
        trim(target) == "" or level < 0 or level > 32 or
        contains(title, "\\") or contains(title, "$"))
        util.unsupported_element("hyperref", "pdfbookmark needs text, target, and level 0–32", node.source_offset)
    else outline_anchor(title, target, level, settings)
}

// sectioning names the bookmark package accepts for its level key
let BOOKMARK_LEVELS = {part: 0, chapter: 0, section: 1, subsection: 2, subsubsection: 3,
    paragraph: 4, subparagraph: 5}

// \bookmark[level=…]{title}: an outline entry at its own position. An outline
// entry marks the element that carries it, so dest (an anchor elsewhere) and the
// other keys (page, uri, color, style, open, …) are reported.
pub fn bookmark_command(node, settings) {
    let title = util.raw_argument(node, "required", 0)
    let opts = util.parse_kv_options(util.optional_raw(node))
    let unknown = [for (key, value at opts where string(key) != "level") string(key)]
    let level = if (opts.level == null) 0
        else if (BOOKMARK_LEVELS[trim(opts.level)] != null) BOOKMARK_LEVELS[trim(opts.level)]
        else int(trim(opts.level)) ^ { -1 }
    if (settings == null or title == null or trim(title) == "" or level < 0 or level > 32 or
        contains(title, "\\") or contains(title, "$"))
        util.unsupported_element("bookmark", "bookmark needs plain text and level 0–32", node.source_offset)
    else if (len(unknown) > 0)
        util.unsupported_element("bookmark", "Unsupported bookmark keys: " ++ join(unknown, ", "),
            node.source_offset)
    else outline_anchor(title, title, level, settings)
}

fn outline_anchor(title, target, level, settings) {
    if (not outlines_enabled(settings)) null
    else <span id: "pdfbookmark-" ++ util.slugify(target),
        'data-pdf-outline-title': title, 'data-pdf-outline-level': level,
        style: "font-size:0;line-height:0;", "​">
}

pub fn stylesheet(settings) {
    if (settings == null) ""
    else if (util.option_enabled(settings.hidelinks)) ".latex-document a{color:inherit;text-decoration:none;}\n"
    else if (util.option_enabled(settings.colorlinks)) {
        let link = if (settings.linkcolor != null) settings.linkcolor else "blue"
        let url = if (settings.urlcolor != null) settings.urlcolor else link
        let cite = if (settings.citecolor != null) settings.citecolor else link
        ".latex-document a.latex-ref{color:" ++ link ++ ";}\n" ++
        ".latex-document a.latex-url{color:" ++ url ++ ";}\n" ++
        ".latex-document a.latex-cite{color:" ++ cite ++ ";}\n"
    } else if (util.option_enabled(settings.ocgcolorlinks))
        // HTML approximates on-screen color and print-neutral links; PDF OCGs need a writer feature.
        ".latex-document a{color:#005d9f;}\n@media print{.latex-document a{color:inherit;}}\n"
    else ""
}
