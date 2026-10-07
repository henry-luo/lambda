// BibTeX styles select the shared bibliography model; no BST code is executed.
import util: ~~.util
import data: .bib_data
import model: .bib_model
import style: .bib_style

let STYLES = ["plain", "unsrt", "abbrv", "plainnat", "unsrtnat", "abbrvnat"]
let OPTIONS = ["numbers", "authoryear", "round", "square", "sort&compress"]

fn style_nodes(node) {
    if (not (node is element)) []
    else if (string(name(node)) == "bibliographystyle") [node]
    else [for (child in node, item in style_nodes(child)) item]
}

pub fn prepare(ast, base_uri, opts, language) {
    let bibliography = util.find_descendant(ast, "bibliography")
    if (opts == null and bibliography == null)
        {entries: [], context: null, diagnostics: []}
    else {
        let declarations = style_nodes(ast)
        let chosen = if (len(declarations) == 0) "plain"
            else trim(util.text_of(declarations[len(declarations) - 1]))
        let offset = if (len(declarations) == 0) bibliography.source_offset
            else declarations[len(declarations) - 1].source_offset
        let numeric = opts == null or opts.numbers == "true" or
            not ends_with(chosen, "nat")
        let selected = if (numeric)
            if (opts != null and opts["sort&compress"] == "true") "numeric-comp"
            else "numeric"
            else "authoryear"
        let unknown = if (opts == null) [] else [for (key, value at opts
            where not any([for (allowed in OPTIONS) string(key) == allowed])) key]
        let invalid_flags = if (opts == null) [] else [for (key, value at opts
            where value != "true" and value != "false") key]
        let valid = any([for (name in STYLES) name == chosen]) and
            len(unknown) == 0 and len(invalid_flags) == 0 and not (opts != null and
                opts.numbers == "true" and opts.authoryear == "true") and
            not (opts != null and opts.round == "true" and opts.square == "true") and
            not (opts != null and opts.authoryear == "true" and
                not ends_with(chosen, "nat"))
        let loaded = data.load(ast, base_uri)
        let settings = {*:style.settings({style: selected,
            sorting: if (starts_with(chosen, "unsrt")) "none" else "nty",
            giveninits: if (starts_with(chosen, "abbrv")) "true" else "false",
            natbib: "true"}, language), option_valid: valid, bibtex: true,
            author_year_sep: ", ",
            citation_open: if (opts != null and opts.square == "true") "["
                else if (numeric and (opts == null or opts.round != "true")) "[" else "(",
            citation_close: if (opts != null and opts.square == "true") "]"
                else if (numeric and (opts == null or opts.round != "true")) "]" else ")"}
        let prepared = model.prepare(ast, loaded.entries, settings)
        {entries: loaded.entries,
         context: {*:prepared, prints: [for (record in prepared.prints)
            {*:record, valid: valid}]},
         diagnostics: [for (issue in loaded.diagnostics ++ prepared.diagnostics)
            {*:issue, package: "natbib"}] ++
            (if (valid) [] else [util.diagnostic("unsupported-natbib-profile", "natbib",
                chosen, "Unsupported or conflicting BibTeX/natbib style or options", offset)]) ++
            [util.diagnostic("bibtex-style-approximation", "natbib", chosen,
                "Built-in bibliography punctuation differs from the named BST; sorting and citation mode are preserved",
                offset)]}
    }
}
