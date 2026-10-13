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
import dingbats: .dingbats
import algorithmic: .algorithmic
import algorithm2e: .algorithm2e
import decorations: .decorations
import paragraph: .paragraph
import filler: .filler
import paralist: .paralist
import codeblocks: .codeblocks
import frames: .frames
import footmisc: .footmisc
import appendix: .appendix
import adjustbox: .adjustbox
import pdfpages: .pdfpages
import boxes: ~~.elements.boxes

// `commands` names the commands and environments a package owns: they dispatch
// to its handlers and are attributed to it when unsupported, only while it is
// active. `conflicts` names packages that cannot be loaded with it.
pub let CATALOG = {
    amsmath: {options: [], commands: ["DeclareMathOperator", "DeclareMathOperator*",
        "tag", "tag*", "numberwithin", "eqref"]},
    amssymb: {options: []},
    amsthm: {options: [], commands: ["theoremstyle", "newtheorem", "newtheorem*"]},
    bussproofs: {options: [], commands: ["prooftree"]},
    semantic: {options: ["inference", "tdiagram"]},
    IEEEtrantools: {options: ["retainorgcmds"]},
    graphicx: {options: [], commands: ["resizebox", "scalebox", "rotatebox", "reflectbox"]},
    hyperref: {options: ["colorlinks", "hidelinks", "linkcolor", "urlcolor", "citecolor", "pdfauthor", "pdftitle", "pdfsubject", "pdfkeywords", "pdfusetitle", "ocgcolorlinks", "bookmarks", "bookmarksnumbered"],
        commands: ["pdfbookmark", "hypersetup", "hypertarget", "hyperlink",
            "phantomsection", "texorpdfstring"]},
    cleveref: {options: [], commands: ["cref", "Cref"]},
    listings: {options: [], commands: ["lstset", "lstinline", "lstlisting"]},
    tcolorbox: {options: [], commands: ["tcolorbox"]},
    url: {options: []},
    geometry: {options: ["a4paper", "a5paper", "letterpaper", "legalpaper", "landscape", "portrait", "margin", "left", "right", "top", "bottom", "inner", "outer", "paperwidth", "paperheight"],
        commands: ["newgeometry", "restoregeometry"]},
    xcolor: {options: ["table", "svgnames", "dvipsnames", "usenames"], commands: ["colorlet"],
        option_implies: {table: ["colortbl"]}},
    booktabs: {options: []},
    array: {options: [], commands: ["newcolumntype"], preamble: ["newcolumntype"]},
    multirow: {options: []},
    makecell: {options: [], commands: ["makecell"]},
    cellspace: {options: []},
    caption: {options: caption.OPTIONS, commands: ["captionsetup", "captionof"]},
    subcaption: {options: caption.OPTIONS, commands: ["subfigure", "subtable"], implies: ["caption"]},
    slashbox: {options: [], commands: ["slashbox", "backslashbox"]},
    diagbox: {options: [], commands: ["diagbox"]},
    longtable: {options: []},
    tabularx: {options: [], implies: ["array"]},
    xltabular: {options: []},
    babel: {options: ["english", "german", "ngerman", "french", "spanish", "russian", "greek"],
        commands: ["selectlanguage", "foreignlanguage", "languageattribute", "spanishdecimal"]},
    polyglossia: {options: [], commands: ["setdefaultlanguage", "setotherlanguage", "newfontfamily",
        "renewfontfamily", "XeTeXlinebreaklocale", "textenglish", "otherlanguage"]},
    vietnam: {options: ["utf8"]},
    multicol: {options: []},
    fontenc: {options: ["T1", "OT1", "T2A"]},
    inputenc: {options: ["utf8", "utf8x"]},
    lmodern: {options: []},
    // `font` requests a face Lambda does not ship: `role` says which family it
    // replaces, `css` the substitute stack, and a font-substitution diagnostic names it
    cmbright: {options: [], font: {face: "Computer Modern Bright", role: "main",
        css: "'Computer Modern Sans','Latin Modern Sans',sans-serif",
        message: "CM Bright is not bundled; using bundled Computer Modern Sans"}},
    helvet: {options: ["scaled"], font: {face: "Helvetica", role: "sans",
        css: "Helvetica,Arial,sans-serif",
        message: "Helvetica is not bundled; using a system sans-serif fallback"}},
    gfsporson: {options: [], commands: ["textporson"], font: {face: "GFS Porson", role: "greek",
        css: "'GFS Porson','Times New Roman',Georgia,serif",
        message: "GFS Porson is not bundled; using a Greek-capable serif fallback"}},
    eucal: {options: ["mathcal", "mathscr"], font: {face: "Euler calligraphic", role: "math",
        message: "Euler calligraphic is not bundled; using the math calligraphic font"}},
    mathrsfs: {options: [], font: {face: "Ralph Smith Formal Script", role: "math",
        message: "RSFS is not bundled; using the available math script font"}},
    cmap: {options: []},
    enumerate: {options: []},
    psfrag: {options: [], commands: ["psfrag"]},
    ifthen: {options: [], commands: ["ifthenelse", "whiledo", "newboolean", "setboolean"]},
    cite: {options: []},
    preview: {options: ["graphics", "tightpage", "active"], commands: ["PreviewEnvironment"]},
    fancyhdr: {options: [], commands: ["lhead", "chead", "rhead", "lfoot", "cfoot", "rfoot", "fancyplain"]},
    lastpage: {options: []},
    csquotes: {options: ["autostyle"], commands: ["enquote"]},
    fullpage: {options: []},
    doi: {options: [], commands: ["doi"]},
    filecontents: {options: []},
    comment: {options: []},
    verbatim: {options: []},
    upquote: {options: []},
    // \NewDocumentCommand is native to the engine's kernel, as in current LaTeX
    xparse: {options: []},
    // declarations whose commands already render (textcomp symbols, \nameref,
    // EPS stays diagnosed with epstopdf); math symbol and alphabet packages
    textcomp: {options: []},
    gensymb: {options: []},
    nameref: {options: []},
    epstopdf: {options: []},
    amscd: {options: []},
    cancel: {options: []},
    latexsym: {options: []},
    stmaryrd: {options: []},
    esint: {options: []},
    upgreek: {options: []},
    bbm: {options: []},
    dsfont: {options: []},
    // bound on the engine (lmd/package/latex/tex/binding/physics.sty): its commands
    // expand into math before the digester runs
    physics: {options: ["arrowdel", "italicdiff", "notrig"], implies: ["amsmath"],
        conflicts: [{package: "siunitx", message: "physics and siunitx both define \\qty"}]},
    // a binding for the macro layer (\DeclarePairedDelimiter, \prescript, ...); colon
    // relations and environments are math, tag forms are owned here
    mathtools: {options: ["fixamsmath", "donotfixamsmathbugs", "allowspaces", "disallowspaces",
        "legacycolonsymbols", "centercolon"], implies: ["amsmath"],
        commands: ["newtagform", "renewtagform", "usetagform", "mathtoolsset"],
        preamble: ["newtagform", "renewtagform", "usetagform"]},
    bm: {options: []},
    // \xspace is the engine binding (binding/xspace.sty)
    xspace: {options: []},
    // text decorations (decorations.ls); soul's settings hold until changed
    soul: {options: [], commands: decorations.SOUL_COMMANDS ++ decorations.SOUL_SETTINGS,
        preamble: decorations.SOUL_SETTINGS},
    // paragraph packages (paragraph.ls): declarations act like \centering
    setspace: {options: paragraph.SETSPACE_OPTIONS,
        commands: paragraph.SETSPACE_DECLARATIONS ++ paragraph.SETSPACE_ENVIRONMENTS,
        preamble: paragraph.SETSPACE_DECLARATIONS},
    parskip: {options: ["skip", "indent", "parfill"]},
    // filler paragraphs from bundled public-domain text (filler.ls)
    lipsum: {options: ["nopar"], commands: filler.COMMANDS.lipsum},
    // code blocks through the listings renderer; \VerbatimInput and \inputminted read
    // files and are reported, as is \VerbatimInput's highlighting cousin \inputminted
    fancyvrb: {options: [], commands: codeblocks.FANCYVRB_ENVIRONMENTS ++ ["Verb", "fvset", "VerbatimInput"],
        preamble: ["fvset"]},
    minted: {options: ["newfloat", "outputdir", "cache", "finalizecache", "frozencache"],
        commands: ["minted", "mint", "mintinline", "setminted", "inputminted", "listoflistings"],
        preamble: ["setminted"]},
    fancybox: {options: [], commands: boxes.FANCYBOX_COMMANDS},
    boxedminipage: {options: [], commands: ["boxedminipage"]},
    // framed blocks on the frame-box model (frames.ls)
    framed: {options: [], commands: frames.FRAMED},
    mdframed: {options: ["framemethod", "xcolor"], commands: ["mdframed", "newmdenv", "mdfdefinestyle",
        "surroundwithmdframed", "mdfsetup"], preamble: ["newmdenv", "mdfdefinestyle"]},
    // compact, run-in and inline lists, and enumerate's label templates
    paralist: {options: ["newitem", "olditem", "newenum", "oldenum", "alwaysadjust", "neveradjust", "neverdecrease", "increaseonly", "defblank", "pointlessenum", "pointedenum", "flushleft"],
        commands: paralist.ENVIRONMENT_NAMES},
    blindtext: {options: ["english", "math"], commands: filler.COMMANDS.blindtext},
    ragged2e: {options: [], commands: paragraph.RAGGED_DECLARATIONS ++ paragraph.RAGGED_ENVIRONMENT_NAMES},
    // without normalem, ulem underlines \emph
    ulem: {options: ["normalem", "normalbf", "UWforbf"], commands: decorations.ULEM_COMMANDS ++
        ["emph", "normalem", "ULforem"], preamble: ["normalem", "ULforem"]},
    // symbol marks, the footnote rule and \footref (footmisc.ls)
    footmisc: {options: footmisc.OPTIONS, commands: ["footref"]},
    // table colors and rule colors in the table builder (array.ls, render.ls)
    colortbl: {options: [], commands: ["rowcolor", "cellcolor", "columncolor", "arrayrulecolor",
        "doublerulesepcolor"], implies: ["array"], preamble: ["arrayrulecolor", "doublerulesepcolor"]},
    hhline: {options: [], commands: ["hhline"]},
    // D columns on the siunitx decimal grid (array.ls)
    dcolumn: {options: [], implies: ["array"]},
    // a caption and notes held to the table's width (render.ls)
    threeparttable: {options: ["para", "flushleft", "online", "normal"],
        commands: ["threeparttable", "tablenotes", "tnote", "measuredfigure"]},
    // keys on graphicx's sizing and the frame-box model (adjustbox.ls)
    adjustbox: {options: ["export", "Export"], commands: ["adjustbox"], implies: ["graphicx"]},
    // PDF pages through graphicx's PDF path (pdfpages.ls)
    pdfpages: {options: ["final", "draft"], commands: ["includepdf", "includepdfset"],
        implies: ["graphicx"], preamble: ["includepdfset"]},
    // lettered appendices on \appendix's counters (appendix.ls)
    appendix: {options: appendix.OPTIONS, commands: appendix.COMMANDS},
    // slanted fractions, in math or text
    nicefrac: {options: ["nice", "ugly"], commands: ["nicefrac"]},
    xfrac: {options: [], commands: ["sfrac"]},
    // \ce and \pu through lambda.doc.math's bounded mhchem profile, in math or text
    mhchem: {options: ["version"], commands: ["ce", "pu"]},
    // `preamble` lists owned commands that belong in the preamble, so they are not
    // reported there as unsupported
    float: {options: [], commands: ["newfloat", "floatname", "floatstyle", "restylefloat",
        "listof", "floatplacement"],
        preamble: ["newfloat", "floatname", "floatstyle", "restylefloat", "floatplacement"]},
    // the algorithm float (floats.kinds_for); its style and numbering follow these options
    algorithm: {options: ["plain", "ruled", "boxed", "section", "chapter", "H"],
        commands: ["listofalgorithms"], implies: ["float"],
        conflicts: [{package: "algorithm2e", message: "algorithm and algorithm2e both define the algorithm float"}]},
    algorithm2e: {options: ["ruled", "boxed", "boxruled", "plain", "linesnumbered", "lined", "vlined",
        "noline", "noend", "longend", "shortend", "english"],
        commands: ["listofalgorithms"] ++ algorithm2e.COMMANDS,
        preamble: algorithm2e.SETTINGS ++ ["SetKwFunction", "SetKwProg", "SetKw", "SetKwData", "ResetInOut"]},
    // subfig's \subfloat[caption]{body} and subfigure's \subfigure[caption]{body}
    subfig: {options: [], commands: ["subfloat"],
        conflicts: [{package: "subcaption", message: "subfig and subcaption define the same subfloats"},
                    {package: "subfigure", message: "subfig replaces the obsolete subfigure package"}]},
    subfigure: {options: [], commands: ["subfigure"],
        conflicts: [{package: "subcaption", message: "subfigure and subcaption both define subfigure"}]},
    wrapfig: {options: [], commands: ["wrapfigure", "wraptable"]},
    rotating: {options: [], commands: ["sidewaysfigure", "sidewaystable", "turn", "rotate", "sideways"]},
    pdflscape: {options: [], commands: ["landscape"]},
    lscape: {options: [], commands: ["landscape"]},
    algpseudocode: {options: ["noend"], commands: ["algorithmic"] ++ algorithmic.PSEUDOCODE_COMMANDS,
        preamble: ["algrenewcommand", "algnewcommand"] ++ algorithmic.LABEL_MACROS},
    algorithmic: {options: [], commands: ["algorithmic"] ++ algorithmic.ALGORITHMIC_COMMANDS},
    // continuous output never moves floats, so a barrier and its options change nothing
    placeins: {options: ["section", "above", "below"], commands: ["FloatBarrier"]},
    indentfirst: {options: []},
    bookmark: {options: [], commands: ["bookmark"], implies: ["hyperref"]},
    pifont: {options: [], commands: ["ding", "dinglist", "dingautolist", "dingline", "dingfill"]},
    marvosym: {options: [], commands: [for (k, v at dingbats.MARVOSYM) string(k)]},
    todonotes: {options: ["colorinlistoftodos"]},
    biblatex: {options: ["style", "citestyle", "bibstyle", "sorting",
        "maxnames", "minnames", "maxcitenames", "mincitenames",
        "maxbibnames", "minbibnames", "giveninits", "uniquename",
        "doi", "url", "isbn", "natbib", "backref", "backend"],
        commands: bib_model.CITATIONS ++ ["nocite", "printbibliography", "addbibresource"]},
    // loads biblatex with its Chicago styles; formatted through CSL (Lambda_Design_BibTeX_CSL §7.4)
    'biblatex-chicago': {options: ["authordate", "notes", "backend"]},
    natbib: {options: ["numbers", "authoryear", "round", "square", "sort&compress"],
        conflicts: [{package: "biblatex", code: "conflicting-bibliography-packages",
            message: "Load biblatex with its natbib option instead of loading both packages"}]},
    enumitem: {options: [], commands: ["setlist"]},
    microtype: {options: ["kerning", "tracking", "spacing", "protrusion", "expansion"]},
    siunitx: {options: [], commands: ["sisetup", "ang", "num", "si", "unit", "SI", "qty"]},
    tikz: {options: [], commands: ["usetikzlibrary", "tikzset", "pgfkeys", "pgfmathsetmacro",
        "pgfmathdeclarefunction"]},
    tikzpeople: {options: ["demo"], commands: ["alltikzpeople", "alltikzpeople*"]},
    pgfplots: {options: []}
}

fn commands_of(desc) => if (desc == null or desc.commands == null) [] else desc.commands

// commands owned by any package; the owner among active packages is resolved per call
let OWNED = map([for (k, v in CATALOG, cmd in commands_of(v), x in [cmd, true]) x])

// classes with a script profile; a beside-document .cls of another name runs on the engine
pub let CLASSES = ["article", "report", "book", "amsart", "standalone"]

// names the TeX engine leaves to the script adapters (Lambda_Design_TeX_Engine §5)
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

// the active package that owns a command; a later-loaded package wins, as in LaTeX
pub fn owner(packages, command) {
    if (OWNED[command] == null) null
    else {
        let owners = [for (entry in packages
            where contains(commands_of(CATALOG[entry.key]), command)) entry.key]
        if (len(owners) == 0) null else owners[len(owners) - 1]
    }
}

// the handler an active package's table supplies for a command, if any
pub fn handler(tables, packages, command) {
    let pkg = owner(packages, command)
    if (pkg == null) null else tables[pkg][command]
}

// one font-substitution diagnostic per active package that requests a face
pub fn font_issues(packages) {
    [for (entry in packages, let font = CATALOG[entry.key].font where font != null)
        util.diagnostic("font-substitution", entry.key, font.face, font.message, entry.offset)]
}

// the substitute CSS stack for a family role (main, sans, greek); the last-loaded package wins
pub fn font_css(packages, role) {
    let stacks = [for (entry in packages, let font = CATALOG[entry.key].font
        where font != null and font.role == role) font.css]
    if (len(stacks) == 0) null else stacks[len(stacks) - 1]
}

pub fn package_for_command(command, packages) {
    let pkg = owner(packages, command)
    if (pkg != null) pkg else "latex"
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
    else if (string(name(node)) == "documentclass") {
        // D7.2.7: a class without a profile or a beside-document .cls is unknown;
        // the document still renders with the article profile
        let cls = trim(util.text_of_skip_brack(node))
        if (cls == "" or contains(CLASSES, cls)) st
        else {*:st, diagnostics: st.diagnostics ++ [diagnostic("unknown-package", cls, null,
            "Unknown LaTeX class: " ++ cls, node)]}
    } else if (string(name(node)) == "usepackage") {
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
            trim(raw) != "positioning" and trim(raw) != "calc" and
            trim(raw) != "shapes.geometric" and
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
        else if ((contains(codeblocks.FANCYVRB_ENVIRONMENTS, tag) or false) and active(packages, "fancyvrb"))
            (let parsed = codeblocks.head(node.source, false),
             codeblocks.issues(codeblocks.keys_of(parsed.options, {}), false, null, node.source_offset))
        else if (tag == "minted" and active(packages, "minted"))
            (let parsed = codeblocks.head(node.source, true),
             codeblocks.issues(codeblocks.keys_of(parsed.options, {}), true, parsed.language, node.source_offset))
        else if ((tag == "mint" or tag == "mintinline") and active(packages, "minted"))
            codeblocks.issues(codeblocks.keys_of(node.options_raw, {}), true, node.language, node.source_offset)
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
        // a label template needs a counter (1, a, A, i or I outside braces)
        else if (tag == "enumerate" and (active(packages, "enumerate") or active(packages, "paralist")) and
            not active(packages, "enumitem") and node.options_raw != null and
            paralist.template(node.options_raw) == null)
            [diagnostic("unsupported-enumerate-label", if (active(packages, "enumerate")) "enumerate" else "paralist",
                tag, "The label template has no counter: " ++ node.options_raw, node)]
        else if (tag == "cite" and active(packages, "cite") and
            len(util.split_top_level(util.text_of_skip_brack(node), ",")) > 1)
            [diagnostic("unsupported-cite-compression", "cite", tag,
                "cite package sorting and compression are not active", node)]
        else if (outside_body and owner != "latex" and tag != "DeclareMathOperator" and
            tag != "addbibresource" and not (contains(preamble_of(CATALOG[owner]), tag) or false))
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

// biblatex-chicago loads biblatex with a Chicago style named by its own option;
// notes is the package default.
fn chicago_biblatex(st) {
    let opts = options_for(st.packages, "biblatex-chicago")
    let offset = offset_for(st.packages, "biblatex-chicago")
    if (opts == null) st
    else if (active(st.packages, "biblatex"))
        {*:st, diagnostics: st.diagnostics ++ [util.diagnostic("conflicting-bibliography-packages",
            "biblatex-chicago", "biblatex",
            "biblatex-chicago loads biblatex itself; do not load both", offset)]}
    else {*:st, packages: st.packages ++ [{key: "biblatex", offset: offset,
        val: {*:(if (opts.backend == null) {} else {backend: opts.backend}),
              style: if (opts.authordate != null) "chicago-authordate" else "chicago-notes"}}]}
}

pub fn collect(ast) {
    let walked = walk(ast, {packages: [], diagnostics: []}, true)
    let declared = chicago_biblatex(walked)
    // packages that load others upstream (subcaption loads caption, bookmark loads
    // hyperref); the implied package is active with its defaults
    let loaded = {*:declared, packages: with_implied(declared.packages, 0)}
    let has_body = ast is element and util.find_child(ast, "document") != null
    {*:loaded, diagnostics: loaded.diagnostics ++ scan(ast, loaded.packages, has_body) ++
        conflict_issues(loaded.packages)}
}

// packages that cannot be loaded together, as declared by each catalogue entry
fn conflict_issues(packages) {
    [for (entry in packages, c in conflicts_of(CATALOG[entry.key])
          where active(packages, c.package))
        util.diagnostic(if (c.code != null) c.code else "conflicting-packages",
            entry.key, c.package, c.message, entry.offset)]
}

fn conflicts_of(desc) => if (desc == null or desc.conflicts == null) [] else desc.conflicts

fn implies_of(desc, opts) {
    let always = if (desc == null or desc.implies == null) [] else desc.implies
    let optional = if (desc == null or desc.option_implies == null or opts == null) []
        else [for (key, names at desc.option_implies, implied in names where util.option_enabled(opts[key])) implied];
    always ++ optional
}

// the declared packages and those they load, level by level (xcolor's table option
// loads colortbl, which loads array); each implied package is added once
fn with_implied(packages, depth) {
    let added = [for (entry in packages, implied in implies_of(CATALOG[entry.key], entry.val)
        where not active(packages, implied)) {key: implied, val: {}, offset: entry.offset}]
    let unique = [for (i, entry in added
        where not any([for (j, other in added where j < i) other.key == entry.key])) entry];
    (if (len(unique) == 0 or depth > 3) packages else with_implied(packages ++ unique, depth + 1))
}

// the owned commands a package expects in the preamble (definitions, settings)
fn preamble_of(desc) => if (desc == null or desc.preamble == null) [] else desc.preamble

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
            else if (tag == "lipsum" or tag == "lipsum*" or tag == "blindtext" or tag == "Blindtext")
                [diagnostic("filler-text-approximation", if (starts_with(tag, "lipsum")) "lipsum" else "blindtext", tag,
                    "Filler text is bundled public-domain Latin, not the package's own paragraphs", node)]
            else if (tag == "xout")
                [diagnostic("xout-approximation", "ulem", tag,
                    "\\xout hatches its text; it renders struck through", node)]
            else if (tag == "sidewaysfigure" or tag == "sidewaystable")
                [diagnostic("sideways-approximation", "rotating", tag,
                    "The float renders upright; a quarter turn on its own page needs page rotation", node)]
            else if (tag == "landscape" and target == "pdf")
                [diagnostic("landscape-approximation", "pdflscape", tag,
                    "PDF pages keep their orientation; landscape content is not turned onto its own page", node)]
            else if (tag == "turn" or tag == "rotate" or tag == "sideways")
                [diagnostic("transform-approximation", "rotating", tag,
                    "The box is transformed in CSS; the surrounding layout keeps its untransformed size", node)]
            else if ((tag == "resizebox" or tag == "scalebox" or tag == "rotatebox") and
                graphicx.lone_image(node) == null)
                [diagnostic("transform-approximation", "graphicx", tag,
                    "The box is transformed in CSS; the surrounding layout keeps its untransformed size", node)]
            else if (tag == "adjustbox" and adjustbox.transforms_content(node))
                [diagnostic("transform-approximation", "adjustbox", tag,
                    "The box is transformed in CSS; the surrounding layout keeps its untransformed size", node)]
            else if (tag == "includepdf")
                [for (key in pdfpages.LAYOUT_KEYS where util.parse_kv_options(util.optional_raw(node))[key] != null)
                    diagnostic("pdfpages-layout-approximation", "pdfpages", key,
                        "Each page is inserted alone and upright; " ++ key ++ " has no effect", node)]
            else if (tag == "doublerulesepcolor")
                [diagnostic("rule-separation-color-approximation", "colortbl", tag,
                    "A double rule's gap shows the table background, not the separation color", node)]
            else []
        own ++ [for (child in node, issue in target_diagnostics(child, target, paged)) issue]
    }
}

pub fn assets(node, base_uri) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        // \includepdf reads its PDF like \includegraphics
        let own = if (tag == "includegraphics" or tag == "includepdf")
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
            (tag == "footref" and active(packages, "footmisc")) or
            ((tag == "cref" or tag == "Cref") and active(packages, "cleveref"))) {
            let keys = if (tag == "cref" or tag == "Cref")
                util.split_top_level(trim(util.text_of(node)), ",")
                else [trim(util.text_of(node))];
            [for (key in keys where util.lookup(labels, trim(key)) == null and
                not (trim(key) == "LastPage" and active(packages, "lastpage")))
                diagnostic("unresolved-reference",
                    if (active(packages, "cleveref") and
                        (tag == "cref" or tag == "Cref")) "cleveref"
                    else if (tag == "footref") "footmisc"
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
