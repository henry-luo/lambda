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
import language: .language
import listings: .listings
import tcolorbox: .tcolorbox
import caption: .caption

pub let CATALOG = {
    amsmath: {options: []},
    amssymb: {options: []},
    amsthm: {options: []},
    bussproofs: {options: []},
    semantic: {options: ["inference", "tdiagram"]},
    IEEEtrantools: {options: ["retainorgcmds"]},
    graphicx: {options: []},
    hyperref: {options: ["colorlinks", "hidelinks", "linkcolor", "urlcolor", "citecolor", "pdfauthor", "pdftitle", "pdfsubject", "pdfkeywords", "pdfusetitle", "ocgcolorlinks", "bookmarks", "bookmarksnumbered"]},
    cleveref: {options: []},
    listings: {options: []},
    tcolorbox: {options: []},
    url: {options: []},
    geometry: {options: ["a4paper", "a5paper", "letterpaper", "legalpaper", "landscape", "portrait", "margin", "left", "right", "top", "bottom", "inner", "outer", "paperwidth", "paperheight"]},
    xcolor: {options: ["table", "svgnames", "dvipsnames", "usenames"]},
    booktabs: {options: []},
    array: {options: []},
    multirow: {options: []},
    makecell: {options: []},
    cellspace: {options: []},
    caption: {options: caption.OPTIONS},
    subcaption: {options: caption.OPTIONS},
    slashbox: {options: []},
    diagbox: {options: []},
    longtable: {options: []},
    tabularx: {options: []},
    xltabular: {options: []},
    babel: {options: ["english", "german", "ngerman", "french", "spanish", "russian", "greek"]},
    polyglossia: {options: []},
    vietnam: {options: ["utf8"]},
    multicol: {options: []},
    fontenc: {options: ["T1", "OT1", "T2A"]},
    inputenc: {options: ["utf8", "utf8x"]},
    lmodern: {options: []},
    cmbright: {options: []},
    helvet: {options: ["scaled"]},
    gfsporson: {options: []},
    eucal: {options: ["mathcal", "mathscr"]},
    mathrsfs: {options: []},
    cmap: {options: []},
    enumerate: {options: []},
    psfrag: {options: []},
    ifthen: {options: []},
    cite: {options: []},
    preview: {options: ["graphics", "tightpage", "active"]},
    fancyhdr: {options: []},
    lastpage: {options: []},
    csquotes: {options: ["autostyle"]},
    fullpage: {options: []},
    doi: {options: []},
    filecontents: {options: []},
    comment: {options: []},
    verbatim: {options: []},
    upquote: {options: []},
    todonotes: {options: ["colorinlistoftodos"]},
    biblatex: {options: ["style", "citestyle", "bibstyle", "sorting",
        "maxnames", "minnames", "maxcitenames", "mincitenames",
        "maxbibnames", "minbibnames", "giveninits", "uniquename",
        "doi", "url", "isbn", "natbib", "backref", "backend"]},
    natbib: {options: ["numbers", "authoryear", "round", "square", "sort&compress"]},
    enumitem: {options: []},
    microtype: {options: ["kerning", "tracking", "spacing", "protrusion", "expansion"]},
    siunitx: {options: []},
    tikz: {options: []},
    tikzpeople: {options: ["demo"]},
    pgfplots: {options: []}
}

// classes with a script profile; a beside-document .cls of another name runs on the engine
pub let CLASSES = ["article", "report", "book", "amsart", "standalone"]

// names the TeX engine leaves to the script adapters (Lambda_Pkg_Latex3 §9.4)
pub fn adapter_names() => [for (k, v in CATALOG) string(k)] ++ CLASSES ++ ["pgf", "amsfonts", "color"]

// adapter commands that read their arguments as tokens, as the CTAN packages
// do: bussproofs splits sequents at \fCenter, semantic parses its rule syntax
pub let RAW_ARGUMENT_COMMANDS = ["AxiomC", "UnaryInfC", "BinaryInfC", "TrinaryInfC",
    "QuaternaryInfC", "QuinaryInfC", "Axiom", "UnaryInf", "BinaryInf", "TrinaryInf",
    "RightLabel", "LeftLabel", "inference"]

fn canonical(name) {
    if (name == "pgf") "tikz"
    else if (name == "amsfonts") "amssymb"
    else if (name == "color") "xcolor"
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
        else if (command == "selectlanguage" or command == "foreignlanguage" or
                 command == "languageattribute" or command == "spanishdecimal") "babel"
        else if (command == "setdefaultlanguage" or command == "setotherlanguage" or
                 command == "newfontfamily" or command == "renewfontfamily" or
                 command == "XeTeXlinebreaklocale" or command == "textenglish") "polyglossia"
        else if (command == "lstset" or command == "lstinline") "listings"
        else if (command == "theoremstyle" or command == "newtheorem" or
                 command == "newtheorem*") "amsthm"
        else if (command == "colorlet" or command == "rowcolor") "xcolor"
        else if (command == "sisetup" or command == "ang") "siunitx"
        else if (command == "setlist") "enumitem"
        else if (command == "newgeometry" or command == "restoregeometry") "geometry"
        else if (bib_model.is_citation(command) or command == "nocite" or
            command == "printbibliography" or command == "addbibresource") "biblatex"
        else if (command == "pdfbookmark" or command == "bookmark") "hyperref"
        else if (command == "usetikzlibrary" or command == "tikzset" or
                 command == "pgfkeys" or command == "pgfmathsetmacro" or
                 command == "pgfmathdeclarefunction") "tikz"
        else if (command == "alltikzpeople" or command == "alltikzpeople*")
            "tikzpeople"
        else if (command == "PreviewEnvironment") "preview"
        else if (command == "lhead" or command == "chead" or command == "rhead" or
                 command == "lfoot" or command == "cfoot" or command == "rfoot") "fancyhdr"
        else if (command == "psfrag") "psfrag"
        else if (command == "ifthenelse" or command == "whiledo" or
                 command == "newboolean" or command == "setboolean") "ifthen"
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
            else if (package == "caption" or package == "subcaption")
                caption.issues(opts, node.source_offset)
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
    let booleans = [opts.giveninits, opts.doi, opts.url, opts.isbn, opts.natbib,
        opts.backref];
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

fn tikz_library_issues(node) {
    let names = util.split_top_level(util.text_of(node), ",");
    [for (raw in names
        where trim(raw) != "patterns" and
            trim(raw) != "decorations.pathmorphing" and
            trim(raw) != "lindenmayersystems" and
            trim(raw) != "backgrounds" and
            trim(raw) != "positioning" and
            trim(raw) != "arrows.meta" and trim(raw) != "math")
        diagnostic("unsupported-tikz-library", "tikz", trim(raw),
            "Unsupported TikZ library: " ++ trim(raw), node)]
}

fn scan(node, packages, outside_body) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let owner = package_for_command(tag, packages)
        let lua_at = if ((tag == "inline_math" or tag == "display_math") and
            node.source != null) index_of(node.source, "\\directlua") else null
        let own = if (tag == "directlua" or tag == "luacode" or tag == "luacode*")
            [diagnostic("unsupported-lua", "lua", tag,
                "Embedded Lua is outside the LaTeX package", node)]
        else if (lua_at != null)
            [util.diagnostic("unsupported-lua", "lua", "directlua",
                "Embedded Lua is outside the LaTeX package", node.source_offset + lua_at)]
        else if (tag == "numberwithin" and active(packages, "amsmath")) []
        else if (tag == "theoremstyle" and active(packages, "amsthm")) {
            let style = trim(util.text_of(node))
            if (style == "plain" or style == "definition" or style == "remark") []
            else [diagnostic("unsupported-theorem-style", "amsthm", style,
                "Unsupported theorem style " ++ style, node)]
        }
        else if ((tag == "selectlanguage" or tag == "foreignlanguage") and
            active(packages, "babel") and
            not language.supported(trim(util.text_of_child(node, 0))))
            [diagnostic("unsupported-language", "babel", util.text_of_child(node, 0),
                "Unsupported Babel language " ++ util.text_of_child(node, 0), node)]
        else if (tag == "languageattribute" and active(packages, "babel") and
            (trim(util.text_of_child(node, 0)) != "greek" or
             (trim(util.text_of_child(node, 1)) != "polutoniko" and
              trim(util.text_of_child(node, 1)) != "ancient")))
            [diagnostic("unsupported-language-attribute", "babel",
                util.text_of(node), "Unsupported Babel language attribute", node)]
        else if (tag == "languageattribute" and active(packages, "babel")) []
        else if (tag == "spanishdecimal" and active(packages, "babel") and
            trim(util.text_of(node)) != ".")
            [diagnostic("unsupported-spanish-decimal", "babel",
                util.text_of(node), "Unsupported Spanish decimal marker", node)]
        else if (tag == "spanishdecimal" and active(packages, "babel")) []
        else if ((tag == "setdefaultlanguage" or tag == "setotherlanguage") and
            active(packages, "polyglossia") and
            not language.supported(trim(util.text_of_skip_brack(node))))
            [diagnostic("unsupported-language", "polyglossia",
                util.text_of_skip_brack(node), "Unsupported polyglossia language", node)]
        else if (owner == "polyglossia" and outside_body) []
        else if (tag == "lstset" and active(packages, "listings"))
            listings.issues(listings.setting_options(node), node.source_offset)
        else if ((tag == "lstlisting" or tag == "lstinline") and
            active(packages, "listings"))
            listings.issues(listings.options(node), node.source_offset)
        else if (tag == "tcolorbox" and active(packages, "tcolorbox"))
            tcolorbox.issues(node)
        else if (tag == "captionsetup" and active(packages, "caption"))
            caption.issues(caption.setup(node), node.source_offset)
        else if (tag == "pdfbookmark" and active(packages, "hyperref")) []
        else if ((tag == "newtheorem" or tag == "newtheorem*") and
            active(packages, "amsthm")) []
        else if (tag == "usetikzlibrary" and active(packages, "tikz"))
            tikz_library_issues(node)
        else if (tag == "tikzset" and active(packages, "tikz") and
            util.raw_argument(node, "required", 0) != null) []
        else if (tag == "PreviewEnvironment" and active(packages, "preview")) {
            let env = trim(util.text_of(node))
            if (env == "tikzpicture" or env == "equation" or env == "equation*") []
            else [diagnostic("unsupported-preview-environment", "preview", env,
                "Unsupported preview environment: " ++ env, node)]
        }
        else if (owner == "fancyhdr" and outside_body and
            util.raw_argument(node, "required", 0) != null) []
        else if (tag == "enumerate" and active(packages, "enumerate") and
            util.optional_raw(node) != null)
            [diagnostic("unsupported-enumerate-label", "enumerate", tag,
                "Legacy enumerate label templates need a bounded adapter", node)]
        else if (tag == "cite" and active(packages, "cite") and
            len(util.split_top_level(util.text_of_skip_brack(node), ",")) > 1)
            [diagnostic("unsupported-cite-compression", "cite", tag,
                "cite package sorting and compression are not active", node)]
        else if (outside_body and owner != "latex" and tag != "DeclareMathOperator" and
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
    let declared = walk(ast, {packages: [], diagnostics: []}, true)
    // Subcaption loads caption upstream; preserve that dependency in the static registry.
    let loaded = if (active(declared.packages, "subcaption") and
        not active(declared.packages, "caption")) {*:declared,
            packages: declared.packages ++ [{key: "caption", val: {},
                offset: offset_for(declared.packages, "subcaption")}]} else declared
    let has_body = ast is element and util.find_child(ast, "document") != null
    {*:loaded, diagnostics: loaded.diagnostics ++ scan(ast, loaded.packages, has_body) ++
        (if (active(loaded.packages, "natbib") and active(loaded.packages, "biblatex"))
            [util.diagnostic("conflicting-bibliography-packages", "natbib", "biblatex",
                "Load biblatex with its natbib option instead of loading both packages",
                offset_for(loaded.packages, "natbib"))] else [])}
}

// HTML keeps a continuous table; the fixed-page PDF writer cannot repeat
// longtable heads or split rows until the shared pagination path supports it.
pub fn target_diagnostics(node, target, paged = false) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let own = if (target == "pdf" and
            (tag == "longtable" or tag == "xltabular"))
            [diagnostic("unsupported-pagination", tag, tag,
                "PDF page-spanning tables need repeated-header pagination", node)]
            else if ((target == "html" or target == "svg") and
                (tag == "longtable" or tag == "xltabular"))
                [diagnostic("continuous-table-approximation", tag, tag,
                    "Continuous output uses the first header and last footer; continued page bands require pagination", node)]
            else if (target == "pdf" and tag == "PreviewEnvironment")
                [diagnostic("unsupported-preview-crop", "preview", tag,
                    "PDF preview cropping needs a neutral tight-page export", node)]
            else if (target == "pdf" and tag == "usepackage" and
                contains(util.text_of_skip_brack(node), "cmap"))
                [diagnostic("unsupported-text-extraction", "cmap", "ToUnicode",
                    "PDF text extraction needs an embedded ToUnicode map", node)]
            else if ((tag == "pageref" or tag == "thepage" or
                (tag == "ref" and trim(util.text_of(node)) == "LastPage")) and not paged)
                [diagnostic("unresolved-page-counter", "latex", tag,
                    "Page references require paged output; continuous output displays a question mark", node)]
            else if (tag == "thispagestyle")
                [diagnostic("page-style-approximation", "fancyhdr", tag,
                    "Per-page style overrides are not represented by the document-wide margin profile", node)]
            else []
        own ++ [for (child in node, issue in target_diagnostics(child, target, paged)) issue]
    }
}

pub fn assets(node, base_uri) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let own = if (tag == "includegraphics")
            [image_asset(graphicx.resolved_source(graphicx.source(node), base_uri), node.source_offset)]
        else []
        own ++ [for (child in node, asset in assets(child, base_uri)) asset]
    }
}

fn image_asset(source, offset) {
    let origin = if (starts_with(source, "data:")) "data-uri"
        else if (index_of(source, "://") != null) "external-url" else "local-file"
    {kind: "image", source: source, origin: origin,
     available: if (origin == "data-uri") true
        else if (origin == "external-url") null else exists(source), offset: offset}
}

pub fn reference_diagnostics(node, labels, bibitems, packages, bibliography_active = false) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let own = if (tag == "ref" or tag == "autoref" or tag == "nameref" or
            ((tag == "cref" or tag == "Cref") and active(packages, "cleveref"))) {
            let keys = if (tag == "cref" or tag == "Cref")
                util.split_top_level(trim(util.text_of(node)), ",")
                else [trim(util.text_of(node))];
            [for (key in keys where util.lookup(labels, trim(key)) == null and
                not (trim(key) == "LastPage" and active(packages, "lastpage")))
                diagnostic("unresolved-reference",
                    if (active(packages, "cleveref") and
                        (tag == "cref" or tag == "Cref")) "cleveref"
                    else if (active(packages, "hyperref")) "hyperref" else "latex",
                    trim(key), "Unresolved reference " ++ trim(key), node)]
        } else if (tag == "cite" and not active(packages, "biblatex") and
            not active(packages, "natbib") and not bibliography_active) {
            let keys = util.split_top_level(trim(util.text_of_skip_brack(node)), ",");
            [for (key in keys
                  where not any([for (entry in bibitems) entry.key == trim(key)]))
                diagnostic("unresolved-citation",
                    if (active(packages, "biblatex")) "biblatex" else "latex", trim(key),
                    "Unresolved citation " ++ trim(key), node)]
        } else []
        own ++ [for (child in node, issue in reference_diagnostics(child, labels, bibitems, packages,
            bibliography_active)) issue]
    }
}
