// Static package catalogue. Package names are data, never dynamic import paths (S1.8, D7.2.4).
import util: ~~.util
import graphicx: .graphicx
import enumitem: .enumitem
import xcolor: .xcolor
import color: ~~.elements.color
import geometry: .geometry
import microtype: .microtype
import bib_style: .bib_style
import bib_model: .bib_model
import bib_data: .bib_data

pub let CATALOG = {
    amsmath: {options: []},
    amssymb: {options: []},
    graphicx: {options: []},
    hyperref: {options: ["colorlinks", "hidelinks", "linkcolor", "urlcolor", "citecolor", "pdfauthor", "pdftitle", "pdfsubject", "pdfkeywords"]},
    geometry: {options: ["a4paper", "a5paper", "letterpaper", "legalpaper", "landscape", "portrait", "margin", "left", "right", "top", "bottom", "inner", "outer", "paperwidth", "paperheight"]},
    xcolor: {options: []},
    booktabs: {options: []},
    babel: {options: ["english", "german", "ngerman", "french"]},
    biblatex: {options: ["style", "citestyle", "bibstyle", "sorting",
        "maxnames", "minnames", "maxcitenames", "mincitenames",
        "maxbibnames", "minbibnames", "giveninits", "uniquename",
        "doi", "url", "isbn", "natbib", "backend"]},
    enumitem: {options: []},
    microtype: {options: ["kerning", "tracking", "spacing", "protrusion", "expansion"]},
    siunitx: {options: []},
    tikz: {options: []}
}

fn canonical(name) {
    if (name == "pgf") "tikz"
    else if (name == "amsfonts") "amssymb"
    else name
}

pub fn active(packages, name) {
    util.lookup(packages, canonical(name)) != null
}

pub fn options_for(packages, name) {
    util.lookup(packages, canonical(name))
}

pub fn offset_for(packages, name) {
    let matching = [for (entry in packages where entry.key == canonical(name)) entry.offset]
    if (len(matching) == 0) null else matching[0]
}

pub fn package_for_command(command, packages) {
    let owner = if (command == "DeclareMathOperator" or command == "tag" or
                    command == "tag*" or command == "numberwithin") "amsmath"
        else if (command == "colorlet") "xcolor"
        else if (command == "sisetup" or command == "ang") "siunitx"
        else if (command == "setlist") "enumitem"
        else if (command == "newgeometry" or command == "restoregeometry") "geometry"
        else if (bib_model.is_citation(command) or command == "nocite" or
            command == "printbibliography" or command == "addbibresource") "biblatex"
        else if (command == "pdfbookmark" or command == "bookmark") "hyperref"
        else if (command == "usetikzlibrary" or command == "tikzset" or command == "pgfkeys") "tikz"
        else null
    if (owner != null and active(packages, owner)) owner else "latex"
}

fn diagnostic(code, package, item, message, node) {
    let offset = if (node is element) node.source_offset else null
    util.diagnostic(code, package, item, message, offset)
}

fn check_options(package, opts, node) {
    let desc = CATALOG[package];
    [for (key, value at opts
          where not any([for (allowed in desc.options) string(key) == allowed]))
        diagnostic("unsupported-option", package, string(key),
                   "Unsupported " ++ package ++ " option: " ++ string(key), node)]
}

fn add_package(st, raw_name, opts, node) {
    let package = canonical(trim(raw_name))
    let known = CATALOG[package] != null
    let prior = util.lookup(st.packages, package)
    if (not known) {
        {*:st, diagnostics: st.diagnostics ++ [diagnostic("unknown-package", package, null,
            "Unknown LaTeX package: " ++ package, node)]}
    } else if (prior != null and prior != opts) {
        {*:st, diagnostics: st.diagnostics ++ [diagnostic("conflicting-package-options", package, null,
            "Conflicting options for LaTeX package " ++ package, node)]}
    } else if (prior != null) st
    else {
        let value_issues = if (package == "geometry")
            option_issues("geometry", geometry.invalid_options(opts), node)
            else if (package == "microtype")
                option_issues("microtype", microtype.invalid_options(opts), node)
            else if (package == "biblatex") bib_options_issues(opts, node)
            else []
        {*:st, packages: st.packages ++ [{key: package, val: opts,
            offset: if (node is element) node.source_offset else null}],
         diagnostics: st.diagnostics ++ check_options(package, opts, node) ++ value_issues}
    }
}

fn bib_options_issues(opts, node) {
    let styles = [opts.style, opts.citestyle, opts.bibstyle]
    let limits = [opts.maxnames, opts.minnames, opts.maxcitenames,
        opts.mincitenames, opts.maxbibnames, opts.minbibnames]
    let booleans = [opts.giveninits, opts.doi, opts.url, opts.isbn, opts.natbib];
    [for (value in styles where value != null and not bib_style.supported_style(value))
        diagnostic("unsupported-bib-style", "biblatex", value,
            "Unsupported biblatex style " ++ value, node)] ++
    (if (opts.sorting != null and not bib_style.supported_sorting(opts.sorting))
        [diagnostic("unsupported-bib-sorting", "biblatex", opts.sorting,
            "Unsupported biblatex sorting " ++ opts.sorting, node)] else []) ++
    (if (opts.backend != null and opts.backend != "biber")
        [diagnostic("unsupported-bib-backend", "biblatex", opts.backend,
            "Unsupported biblatex backend " ++ opts.backend, node)] else []) ++
    (if (opts.uniquename != null and opts.uniquename != "false" and
        opts.uniquename != "init" and opts.uniquename != "full")
        [diagnostic("unsupported-bib-uniquename", "biblatex", opts.uniquename,
            "Unsupported uniquename option " ++ opts.uniquename, node)] else []) ++
    [for (value in limits where value != null and
        ((int(value) ^ { null }) == null or value == "0" or starts_with(value, "-")))
        diagnostic("invalid-bib-name-limit", "biblatex", value,
            "Name limits must be positive integers", node)] ++
    [for (value in booleans where value != null and value != "true" and value != "false")
        diagnostic("invalid-bib-boolean", "biblatex", value,
            "Boolean biblatex option must be true or false", node)]
}

pub fn bib_profile_valid(opts) =>
    len(check_options("biblatex", opts, null)) == 0 and
    len(bib_options_issues(opts, null)) == 0

pub fn bib_print_issues(node) {
    let opts = util.parse_kv_options(util.optional_raw(node))
    let allowed = ["title", "heading", "type", "nottype", "keyword", "notkeyword",
        "section", "segment"]
    let unknown = [for (key, value at opts
        where not any([for (name in allowed) string(key) == name])) string(key)]
    option_issues("biblatex", unknown, node) ++
    (if (opts.heading != null and opts.heading != "bibliography" and
        opts.heading != "subbibliography" and opts.heading != "bibintoc" and
        opts.heading != "none")
        [diagnostic("unsupported-bib-heading", "biblatex", opts.heading,
            "Unsupported bibliography heading " ++ opts.heading, node)] else []) ++
    [for (value in [opts.type, opts.nottype] where value != null and
        not (bib_data.supported_entry_type(value) ^ { false }))
        diagnostic("unsupported-bib-filter-type", "biblatex", value,
            "Unsupported bibliography filter type " ++ value, node)] ++
    [for (value in [opts.section, opts.segment] where value != null and
        ((int(value) ^ { null }) == null or starts_with(value, "-")))
        diagnostic("invalid-bib-scope", "biblatex", value,
            "Bibliography section and segment must be nonnegative integers", node)]
}

fn add_names(st, names, opts, node, i) {
    if (i >= len(names)) st
    else add_names(add_package(st, names[i], opts, node), names, opts, node, i + 1)
}

fn walk(node, st, before_body) {
    if (not (node is element)) st
    else if (string(name(node)) == "usepackage") {
        if (not before_body)
            {*:st, diagnostics: st.diagnostics ++ [diagnostic("late-package", "latex", "usepackage",
                "usepackage must appear in the preamble", node)]}
        else {
            let opts = util.parse_kv_options(util.optional_raw(node))
            let names = util.split_top_level(util.text_of_skip_brack(node), ",")
            add_names(st, names, opts, node, 0)
        }
    } else walk_children(node, 0, st, before_body and string(name(node)) != "document")
}

fn walk_children(node, i, st, before_body) {
    if (i >= len(node)) st
    else {
        let child = node[i]
        let next = walk(child, st, before_body)
        // a package declaration after the document body is no longer preamble input.
        let next_before = before_body and not (child is element and string(name(child)) == "document")
        walk_children(node, i + 1, next, next_before)
    }
}

fn option_issues(package, keys, node) {
    [for (key in keys) diagnostic("unsupported-option", package, key,
        "Unsupported " ++ package ++ " option: " ++ key, node)]
}

fn scan(node, packages, outside_body) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let owner = package_for_command(tag, packages)
        let own = if (outside_body and owner != "latex" and tag != "DeclareMathOperator" and
            tag != "addbibresource")
            [diagnostic("unsupported-command", owner, tag,
                "Unsupported " ++ owner ++ " command: " ++ tag, node)]
        else if (tag == "includegraphics" and active(packages, "graphicx"))
            option_issues("graphicx", graphicx.unsupported_keys(graphicx.options(node)), node)
        else if ((tag == "enumerate" or tag == "itemize" or tag == "description")
                 and active(packages, "enumitem"))
            option_issues("enumitem", enumitem.unsupported_keys(enumitem.options(node), tag), node)
        else if (tag == "definecolor" and active(packages, "xcolor") and
                 not xcolor.supported_model(trim(util.text_of_child(node, 1))))
            [diagnostic("unsupported-color-model", "xcolor", util.text_of_child(node, 1),
                "Unsupported xcolor model: " ++ util.text_of_child(node, 1), node)]
        else if (tag == "definecolor" and active(packages, "xcolor") and
                 color.definition(node).css_color == null)
            [diagnostic("invalid-color", "xcolor", util.text_of_child(node, 2),
                "Invalid xcolor definition", node)]
        else if ((tag == "textcolor" or tag == "colorbox" or tag == "fcolorbox" or
                  tag == "color" or tag == "pagecolor") and active(packages, "xcolor") and
                 util.optional_raw(node) != null and
                 not xcolor.supported_model(trim(util.optional_raw(node))))
            [diagnostic("unsupported-color-model", "xcolor", util.optional_raw(node),
                "Unsupported xcolor model: " ++ util.optional_raw(node), node)]
        else if ((tag == "num" or tag == "si" or tag == "unit" or tag == "SI" or tag == "qty")
                 and active(packages, "siunitx") and util.optional_raw(node) != null)
            option_issues("siunitx", ["command options"], node)
        else if (tag == "printbibliography" and active(packages, "biblatex"))
            bib_print_issues(node)
        else if (tag == "addbibresource" and active(packages, "biblatex") and
                 util.optional_raw(node) != null)
            option_issues("biblatex", ["addbibresource options"], node)
        else if (tag == "hypersetup" and active(packages, "hyperref")) {
            let stored = util.raw_argument(node, "required", 0)
            let raw = if (stored != null) stored else util.text_of(node)
            check_options("hyperref", util.parse_kv_options(raw), node)
        } else []
        let next_outside = outside_body and tag != "document"
        own ++ [for (child in node, issue in scan(child, packages, next_outside)) issue]
    }
}

pub fn collect(ast) {
    let loaded = walk(ast, {packages: [], diagnostics: []}, true)
    let has_body = ast is element and util.find_child(ast, "document") != null
    {*:loaded, diagnostics: loaded.diagnostics ++ scan(ast, loaded.packages, has_body)}
}

pub fn assets(node, base_uri) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let own = if (tag == "includegraphics")
            [{kind: "image", source: graphicx.resolved_source(graphicx.source(node), base_uri)}]
        else if (tag == "addbibresource")
            [{kind: "bibliography", source: graphicx.resolved_source(
                trim(util.text_of_skip_brack(node)), base_uri)}]
        else []
        own ++ [for (child in node, asset in assets(child, base_uri)) asset]
    }
}

pub fn reference_diagnostics(node, labels, bibitems, packages) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let own = if (tag == "ref" or tag == "autoref" or tag == "nameref") {
            let key = trim(util.text_of(node))
            if (util.lookup(labels, key) == null)
                [diagnostic("unresolved-reference",
                    if (active(packages, "hyperref")) "hyperref" else "latex", key,
                    "Unresolved reference " ++ key, node)]
            else []
        } else if (tag == "cite" and not active(packages, "biblatex")) {
            let keys = util.split_top_level(trim(util.text_of_skip_brack(node)), ",");
            [for (key in keys
                  where not any([for (entry in bibitems) entry.key == trim(key)]))
                diagnostic("unresolved-citation",
                    if (active(packages, "biblatex")) "biblatex" else "latex", trim(key),
                    "Unresolved citation " ++ trim(key), node)]
        } else []
        own ++ [for (child in node, issue in reference_diagnostics(child, labels, bibitems, packages)) issue]
    }
}
