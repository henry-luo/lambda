// citeproc/aliases.ls — guide-level biblatex styles formatted by bundled CSL styles
// (vibe/latex/Lambda_Design_BibTeX_CSL.md §7.4). A document that asks for a
// contributed biblatex style such as `style=apa` gets the CSL style implementing
// the same published guide. The substitution is always reported; biblatex style
// files are never executed (D7.2.7).

import util: lambda.latex.util

// biblatex style name -> bundled CSL style. Only styles with a pinned, bundled
// CSL file belong here; a new row needs reference fixtures against biblatex.
let ALIASES = [
    {biblatex: "apa", csl: "apa"},
    {biblatex: "ieee", csl: "ieee"},
    {biblatex: "chicago-authordate", csl: "chicago-author-date"},
    {biblatex: "chicago-notes", csl: "chicago-fullnote-bibliography"}
]

// biblatex options that a CSL style fixes in its own XML
let CSL_FIXED_OPTIONS = ["sorting", "maxnames", "minnames", "maxcitenames",
    "mincitenames", "maxbibnames", "minbibnames", "giveninits", "uniquename",
    "doi", "url", "isbn"]

// TeX-level style customization that no CSL style can follow
let CUSTOMIZATION_COMMANDS = ["DeclareFieldFormat", "DeclareListFormat",
    "DeclareNameFormat", "newbibmacro", "renewbibmacro", "DeclareBibliographyDriver",
    "DeclareCiteCommand", "DeclareSortingTemplate", "DeclareLabelalphaTemplate"]

// the CSL style a biblatex style name maps to, or null
pub fn csl_style(style_name) {
    let matching = [for (row in ALIASES where row.biblatex == style_name) row.csl]
    if (len(matching) == 0) null else matching[0]
}

// the style requested for both citations and bibliography; a split request
// (citestyle and bibstyle naming different styles) has no single CSL style
fn requested_style(opts) {
    let cite = if (opts.citestyle != null) opts.citestyle else opts.style
    let bib = if (opts.bibstyle != null) opts.bibstyle else opts.style
    if (cite != null and cite == bib) cite else null
}

fn customizations(node) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let own = if (any([for (command in CUSTOMIZATION_COMMANDS) tag == command]))
            [node] else []
        own ++ [for (child in node, found in customizations(child)) found]
    }
}

// Select the CSL style for a document's biblatex options. Returns null when no
// alias applies; otherwise the citeproc request, the biblatex style it replaces,
// and the diagnostics that report the substitution and what CSL cannot follow.
pub fn select(opts, ast, offset) {
    let style_name = if (opts == null) null else requested_style(opts)
    let target = if (style_name == null) null else csl_style(style_name)
    if (target == null) null
    else ({
        citeproc: {style: target},
        biblatex_style: style_name,
        diagnostics:
            [util.diagnostic("csl-style-alias", "biblatex", style_name,
                "biblatex style " ++ style_name ++ " is formatted by the bundled CSL style " ++
                target ++ "; biblatex style files are not executed", offset)] ++
            [for (key in CSL_FIXED_OPTIONS where opts[key] != null)
                util.diagnostic("csl-alias-option-ignored", "biblatex", key,
                    "The CSL style " ++ target ++ " fixes " ++ key ++
                    "; the biblatex option is ignored", offset)] ++
            [for (node in customizations(ast))
                util.diagnostic("csl-alias-customization", "biblatex", string(name(node)),
                    "biblatex style customization has no CSL counterpart and is ignored",
                    node.source_offset)]
    })
}
