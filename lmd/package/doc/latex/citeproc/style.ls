// Validate and retain an immutable CSL graph, never generated Lambda source.
import c: .common

let ELEMENTS = ["style", "info", "citation", "bibliography", "macro", "locale",
    "layout", "text", "number", "label", "names", "name", "name-part", "et-al",
    "substitute", "date", "date-part", "group", "choose", "if", "else-if", "else",
    "sort", "key", "terms", "term", "single", "multiple", "style-options"]

let FORMATTING = ["font-style", "font-weight", "font-variant", "text-decoration",
    "vertical-align", "prefix", "suffix", "delimiter", "text-case", "quotes",
    "strip-periods", "display"]
let NAME_OPTIONS = ["and", "delimiter-precedes-last", "delimiter-precedes-et-al",
    "et-al-min", "et-al-use-first", "et-al-use-last", "et-al-subsequent-min",
    "et-al-subsequent-use-first", "initialize", "initialize-with",
    "name-as-sort-order", "sort-separator", "name-delimiter", "names-delimiter", "name-form"]
let FORMAT_TAGS = ["layout", "text", "number", "label", "names", "name", "name-part", "et-al", "date", "date-part", "group"]
let OPTION_TAGS = ["style", "citation", "bibliography", "names", "name"]
let ENUMS = {'font-style': ["normal", "italic", "oblique"], 'font-weight': ["normal", "bold", "light"],
    'font-variant': ["normal", "small-caps"], 'text-decoration': ["none", "underline"],
    'vertical-align': ["baseline", "sup", "sub"], display: ["block", "left-margin", "right-inline", "indent"],
    'text-case': ["lowercase", "uppercase", "capitalize-first", "sentence", "title", "capitalize-all"],
    match: ["all", "any", "none"], 'and': ["text", "symbol"], sort: ["ascending", "descending"],
    'name-as-sort-order': ["first", "all"], 'demote-non-dropping-particle': ["never", "sort-only", "display-and-sort"],
    'date-parts': ["year", "year-month", "year-month-day"],
    'page-range-format': ["expanded", "minimal", "minimal-two", "chicago", "chicago-15", "chicago-16"]}
let ATTRIBUTES = {
    style: ["xmlns", "version", "class", "default-locale", "demote-non-dropping-particle",
        "initialize-with-hyphen", "page-range-format", "page-range-delimiter"],
    citation: ["disambiguate-add-names", "disambiguate-add-givenname",
        "disambiguate-add-year-suffix", "givenname-disambiguation-rule", "collapse",
        "cite-group-delimiter", "year-suffix-delimiter", "after-collapse-delimiter",
        "near-note-distance"],
    bibliography: ["hanging-indent", "second-field-align", "line-spacing", "entry-spacing",
        "subsequent-author-substitute", "subsequent-author-substitute-rule"],
    macro: ["name"], locale: ["xml:lang"], text: ["variable", "macro", "term", "value", "form", "plural"],
    number: ["variable", "form"], label: ["variable", "form", "plural"],
    names: ["variable"], name: ["form"], 'name-part': ["name"],
    date: ["variable", "form", "date-parts"], 'date-part': ["name", "form", "range-delimiter"],
    'if': ["type", "variable", "is-numeric", "is-uncertain-date", "locator", "position", "disambiguate", "match"],
    'else-if': ["type", "variable", "is-numeric", "is-uncertain-date", "locator", "position", "disambiguate", "match"],
    key: ["variable", "macro", "sort", "names-min", "names-use-first", "names-use-last"],
    term: ["name", "form", "gender", "gender-form", "match"],
    'style-options': ["punctuation-in-quote", "limit-day-ordinals-to-day-1"]
}

fn namespace_for(node, inherited) => {*:inherited,
    *:c.dictionary([for (key, value in c.attrs(node)
        where string(key) == "xmlns" or starts_with(string(key), "xmlns:"))
        {key: string(key), value: value}])}

fn check(node, inherited, source, macros, macro_stack, depth) {
    let tag = c.tag(node)
    let bindings = namespace_for(node, inherited)
    let allowed = c.get(ATTRIBUTES, tag, []) ++ (if (c.has(FORMAT_TAGS, tag)) FORMATTING else []) ++
        (if (c.has(OPTION_TAGS, tag)) NAME_OPTIONS else [])
    let unknown = [for (key, value in c.attrs(node)
        where not c.has(allowed, string(key)) and
            not starts_with(string(key), "xmlns:") and string(key) != "xmlns")
        c.issue("unsupported-csl-attribute", "Unsupported CSL attribute " ++ string(key), source, tag)]
    let invalid = [for (key, value in c.attrs(node)
        where ENUMS[string(key)] != null and not c.has(ENUMS[string(key)], value))
        c.issue("invalid-csl-attribute", "Invalid CSL value for " ++ string(key), source, tag)]
    let own = if (depth > 128)
        [c.issue("csl-depth", "CSL graph exceeds 128 levels", source, tag)]
        else if (c.namespace_uri(node, bindings) != c.NS)
        [c.issue("invalid-csl-namespace", "Expected the CSL namespace", source, tag)]
        else if (not c.has(ELEMENTS, tag))
        [c.issue("unsupported-csl-element", "Unsupported CSL element " ++ tag, source, tag)]
        else if (tag == "info") [] else unknown ++ (if (tag == "term") [] else invalid)
    let called = node.macro
    let macro_issues = if (called == null) []
        else if (macros[called] == null)
            [c.issue("missing-csl-macro", "Undefined CSL macro " ++ called, source, called)]
        else []
    let limited = if ((node.collapse != null and not c.has(["citation-number", "year"], node.collapse)) or
        (node["givenname-disambiguation-rule"] != null and not c.has(["by-cite", "primary-name",
            "primary-name-with-initials"], node["givenname-disambiguation-rule"])) or
        (node["subsequent-author-substitute-rule"] != null and node["subsequent-author-substitute-rule"] != "complete-all"))
        [c.issue("unsupported-csl-feature", "Unsupported CSL collapse or name-disambiguation policy", source, tag)] else []
    own ++ macro_issues ++ limited ++ (if (tag == "info" or depth > 128) [] else
        [for (child in c.children(node), issue in check(child, bindings, source, macros, macro_stack, depth + 1)) issue])
}

fn dependencies(node) => unique((if (node.macro == null) [] else [node.macro]) ++
    [for (child in c.children(node), dep in dependencies(child)) dep])

fn uses_variable(node, key) => c.has(c.words(node.variable), key) or
    any([for (child in c.children(node)) uses_variable(child, key)])

fn visit(deps, key, trail, done, source) {
    if (c.has(trail, key)) {done: done,
        issues: [c.issue("cyclic-csl-macro", "Recursive CSL macro " ++ key, source, key)]}
    else if (len(trail) > 128) {done: done,
        issues: [c.issue("csl-depth", "CSL macro chain exceeds 128 levels", source, key)]}
    else if (c.has(done, key) or deps[key] == null) {done: done, issues: []}
    else {
        let visited = visit_many(deps, deps[key], 0, trail ++ [key], done, source)
        {*:visited, done: visited.done ++ [key]}
    }
}

fn visit_many(deps, keys, index, trail, done, source) {
    if (index >= len(keys)) {done: done, issues: []}
    else {
        let current = visit(deps, keys[index], trail, done, source)
        let rest = visit_many(deps, keys, index + 1, trail, current.done, source)
        {done: rest.done, issues: current.issues ++ rest.issues}
    }
}

fn graph(root, source) {
    let macro_nodes = c.children(root, "macro")
    let macros = c.dictionary([for (node in macro_nodes) {key: node["name"], value: node}])
    let duplicates = [for (i, node in macro_nodes
        where node["name"] == null or any([for (j, prior in macro_nodes
            where j < i and prior["name"] == node["name"]) true]))
        c.issue("duplicate-csl-macro", "Missing or duplicate macro name", source, node["name"])]
    let citation = c.child(root, "citation")
    let bibliography = c.child(root, "bibliography")
    let layout = c.child(citation, "layout")
    let deps = c.dictionary([for (node in macro_nodes)
        {key: node["name"], value: dependencies(node)}])
    let cycles = visit_many(deps, [for (node in macro_nodes) node["name"]], 0, [], [], source)
    let issues = duplicates ++ cycles.issues ++ check(root, {}, source, macros, [], 0) ++
        (if (citation == null or layout == null)
            [c.issue("invalid-csl-layout", "CSL citation requires a layout", source)] else []) ++
        (if (root.class != "in-text" and root.class != "note")
            [c.issue("invalid-csl-class", "CSL class must be in-text or note", source)] else [])
    {root: root, macros: macros, citation: citation, bibliography: bibliography,
        locales: c.children(root, "locale"), source: source,
        id: c.text(c.child(c.child(root, "info"), "id")), class: root.class,
        default_locale: root["default-locale"], explicit_year_suffix: uses_variable(root, "year-suffix"), diagnostics: issues,
        valid: len(issues) == 0}
}

pub fn compile(xml, parents = {}, options = null) map^ {
    compile_at(xml, parents, c.get(options, "source", "<style>"), [], 0)^
}

fn compile_at(xml, parents, source, seen, depth) map^ {
    if (depth > 32 or c.has(seen, source))
        raise c.failure("cyclic-csl-parent", "Dependent style cycle or depth limit", source)
    else compile_root(xml, parents, source, seen, depth)^
}

fn compile_root(xml, parents, source, seen, depth) map^ {
    let root = c.root(if (xml is string) parse(xml, "xml")^ else xml)
    if (c.tag(root) != "style" or root.version != "1.0" or c.namespace_uri(root) != c.NS)
        raise c.failure("invalid-csl-style", "Expected a CSL 1.0 style", source)
    else if (c.child(root, "citation") == null) {
        let links = [for (node in c.children(c.child(root, "info"), "link")
            where node.rel == "independent-parent") node.href]
        if (len(links) != 1 or parents[links[0]] == null)
            raise c.failure("missing-csl-parent", "Dependent style requires a supplied independent parent", source)
        else {
            let parent = compile_at(parents[links[0]], parents, links[0], seen ++ [source], depth + 1)^;
            {*:parent, dependent_source: source,
                default_locale: c.get(root, "default-locale", parent.default_locale)}
        }
    } else {
        let result = graph(root, source)
        if (not result.valid) raise c.failure(result.diagnostics[0].code,
            result.diagnostics[0].message ++ " (" ++ string(result.diagnostics[0].item) ++ ")", source)
        else result
    }
}
