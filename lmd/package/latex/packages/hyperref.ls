// HTML link presentation and document metadata; PDF annotations remain a host export gap.
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
    } else ""
}

pub fn output_diagnostics(settings, target, offset) {
    if (settings != null and target == "pdf")
        [util.diagnostic("unsupported-pdf-links", "hyperref", "annotations",
          "PDF link annotations, outlines and metadata are unavailable", offset)]
    else []
}
