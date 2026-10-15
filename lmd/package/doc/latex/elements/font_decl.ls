// latex/elements/font_decl.ls — Font declaration commands and alignment declarations
// \itshape, \bfseries, \ttfamily, \rmfamily, \sffamily, \scshape, \slshape, \upshape, \mdseries
// \centering, \raggedleft, \raggedright
// NFSS: \fontfamily, \fontseries, \fontshape, \usefont, \linespread

import util: lambda.doc.latex.util
import sym: lambda.doc.latex.symbols
import color: lambda.doc.latex.elements.color

// ============================================================
// Font declaration style map
// ============================================================

// The three text families read the document root's variables, which css.ls sets to
// the bundled Computer Modern faces and font packages or fontspec replace
// (render.ls document_style); the generic family is the fallback outside a document.
pub let SERIF_CSS = "font-family:var(--latex-serif,serif)"
pub let SANS_CSS = "font-family:var(--latex-sans,sans-serif)"
pub let MONO_CSS = "font-family:var(--latex-mono,monospace)"

// maps font declaration tag names to CSS style strings
let FONT_DECL_STYLES = {
    'itshape':   "font-style:italic",
    'bfseries':  "font-weight:bold",
    'bf':        "font-weight:bold",
    'it':        "font-style:italic",
    'rm':        SERIF_CSS,
    'tt':        MONO_CSS,
    'ttfamily':  MONO_CSS,
    'rmfamily':  SERIF_CSS,
    'sffamily':  SANS_CSS,
    'scshape':   "font-variant:small-caps",
    'slshape':   "font-style:oblique",
    'upshape':   "font-style:normal",
    'mdseries':  "font-weight:normal",
    'normalfont': "font-style:normal;font-weight:normal;font-variant:normal"
}

// NFSS family codes. The CM and LM faces are bundled; the others name the
// faces their TeX fonts clone, so the request falls back to installed fonts.
let NFSS_FAMILIES = {
    cmr: "'Computer Modern Serif','Latin Modern Roman',serif",
    lmr: "'Computer Modern Serif','Latin Modern Roman',serif",
    cmss: "'Computer Modern Sans','Latin Modern Sans',sans-serif",
    lmss: "'Computer Modern Sans','Latin Modern Sans',sans-serif",
    cmtt: "'Computer Modern Typewriter','Latin Modern Mono',monospace",
    lmtt: "'Computer Modern Typewriter','Latin Modern Mono',monospace",
    ptm: "'Times New Roman',Times,serif",
    phv: "Helvetica,Arial,sans-serif",
    pcr: "'Courier New',Courier,monospace",
    ppl: "Palatino,'Palatino Linotype','Book Antiqua',serif",
    pbk: "'Bookman Old Style',Bookman,serif",
    pag: "'Century Gothic','Avant Garde',sans-serif",
    pnc: "'Century Schoolbook','New Century Schoolbook',serif",
    bch: "Charter,'Bitstream Charter',serif"
}
let BUNDLED_FAMILIES = ["cmr", "lmr", "cmss", "lmss", "cmtt", "lmtt"]
let NFSS_SERIES = {m: "font-weight:normal", l: "font-weight:300", sb: "font-weight:600",
    b: "font-weight:bold", bx: "font-weight:bold", eb: "font-weight:800"}
let NFSS_SHAPES = {n: "font-style:normal", up: "font-style:normal", it: "font-style:italic",
    sl: "font-style:oblique", sc: "font-variant:small-caps"}
let NFSS_DECLS = ["fontfamily", "fontseries", "fontshape", "usefont", "linespread"]

// alignment declaration tag names to CSS style strings
let ALIGN_DECL_STYLES = {
    'centering':   "text-align:center",
    'raggedright': "text-align:left",
    'raggedleft':  "text-align:right"
}

// ============================================================
// Public API
// ============================================================

// check if a tag name is a font declaration
pub fn is_font_decl(tag_str) {
    FONT_DECL_STYLES[tag_str] != null or contains(NFSS_DECLS, tag_str)
}

// get CSS style for a fixed font switch tag
pub fn font_decl_style(tag_str) {
    FONT_DECL_STYLES[tag_str]
}

fn arg_text(el, i) {
    let raw = util.raw_argument(el, "required", i)
    if (raw == null) null else trim(raw)
}

fn nfss_part(table, key) => if (key == null) null else table[key]

// The CSS of a font declaration element: a fixed switch, or an NFSS command
// read from its arguments. Null for a value outside the tables.
pub fn decl_css(el) {
    let tag = string(name(el))
    if (FONT_DECL_STYLES[tag] != null) font_decl_style(tag)
    else if (tag == "fontfamily") {
        let family = nfss_part(NFSS_FAMILIES, arg_text(el, 0))
        if (family == null) null else "font-family:" ++ family
    }
    else if (tag == "fontseries") nfss_part(NFSS_SERIES, arg_text(el, 0))
    else if (tag == "fontshape") nfss_part(NFSS_SHAPES, arg_text(el, 0))
    else if (tag == "usefont") {
        // \usefont{encoding}{family}{series}{shape}
        let family = nfss_part(NFSS_FAMILIES, arg_text(el, 1))
        let series = nfss_part(NFSS_SERIES, arg_text(el, 2))
        let shape = nfss_part(NFSS_SHAPES, arg_text(el, 3))
        if (family == null or series == null or shape == null) null
        else "font-family:" ++ family ++ ";" ++ series ++ ";" ++ shape
    }
    else if (tag == "linespread") {
        let factor = float(arg_text(el, 0)) ^ { null }
        if (factor == null or factor <= 0.0) null
        else "line-height:" ++ string(line_height_for(factor))
    }
    else null
}

// LaTeX's baselineskip is 1.2 times the font size; \linespread scales it
pub fn line_height_for(factor) => 1.2 * factor

// Family requests for faces Lambda does not ship, as font-substitution diagnostics.
pub fn substitution_issues(node) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        let code = if (tag == "fontfamily") arg_text(node, 0)
            else if (tag == "usefont") arg_text(node, 1) else null
        let own = if (code != null and NFSS_FAMILIES[code] != null and
            not contains(BUNDLED_FAMILIES, code))
            [util.diagnostic("font-substitution", "latex", code,
                "NFSS family " ++ code ++ " is not bundled; using an installed equivalent",
                node.source_offset)] else [];
        own ++ [for (child in node, issue in substitution_issues(child)) issue]
    }
}

// check if a tag name is an alignment declaration
pub fn is_align_decl(tag_str) {
    ALIGN_DECL_STYLES[tag_str] != null
}

// get CSS style for an alignment declaration tag
pub fn align_decl_style(tag_str) {
    ALIGN_DECL_STYLES[tag_str]
}

// wrap rendered items in a div with a paragraph declaration's style: an alignment
// declaration's own, or one a package supplies (ragged2e, setspace)
pub fn wrap_align_decl(tag_str, items, style = null) {
    <div class: "latex-" ++ tag_str, style: style or ALIGN_DECL_STYLES[tag_str], for c in items { c }>
}

// ============================================================
// Declaration lists given as TeX source
// ============================================================

// titlesec formats, sectsty fonts, tocloft hooks, authblk fonts and titling's
// pre/post code are declaration lists in raw TeX. Each reads as:
//   css        font switches and sizes, \color, \fontsize, NFSS commands
//   align      \centering, \raggedright, \raggedleft, titlesec's \fil… and the
//              center/flushleft/flushright environments
//   transform  \MakeUppercase or \MakeLowercase, applied to the text that follows
//   rule       a \titlerule or \hrule
//   space      a \vspace or \vskip length
//   text       printed text, \\ and the environment commands aside
//   unknown    other commands
pub fn declarations(raw, custom_colors) =>
    read_declarations(raw or "", 0, {css: "", align: null, transform: null, rule: false,
        space: null, text: "", unknown: []}, custom_colors)

let ALIGN_COMMANDS = {centering: "center", raggedright: "left", raggedleft: "right",
    filcenter: "center", filright: "left", filleft: "right", filinner: "left", filouter: "right",
    Centering: "center", RaggedRight: "left", RaggedLeft: "right"}
let ALIGN_ENVIRONMENTS = {center: "center", flushleft: "left", flushright: "right"}
// spacing and structure commands with no effect on a declaration list's style
let NEUTRAL_COMMANDS = ["selectfont", "par", "noindent", "relax", "normalsize", "quad", "qquad",
    "hfill", "hfil", "null", "strut", "nobreak"]

fn read_declarations(raw, i, acc, colors) {
    if (i >= len(raw)) acc
    else {
        let ch = slice(raw, i, i + 1)
        if (ch == "\\") {
            let name_end = letters_end(raw, i + 1)
            if (name_end == i + 1)
                // a control symbol: \\ breaks a line, \  is a space, \% and the like print
                read_declarations(raw, i + 2, {*:acc, text: acc.text ++
                    (if (slice(raw, i + 1, i + 2) == "\\") "" else if (slice(raw, i + 1, i + 2) == " ") " "
                     else slice(raw, i + 1, i + 2))}, colors)
            // TeX skips the spaces after a control word
            else declaration_command(raw, slice(raw, i + 1, name_end), skip_spaces(raw, name_end), acc, colors)
        }
        else if (ch == "{" or ch == "}" or ch == "%") read_declarations(raw, i + 1, acc, colors)
        else read_declarations(raw, i + 1, {*:acc, text: acc.text ++ (if (ch == "~") "\u00A0" else ch)}, colors)
    }
}

fn letters_end(raw, i) {
    if (i >= len(raw)) i
    else {
        let ch = slice(raw, i, i + 1)
        if ((ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z") or ch == "@") letters_end(raw, i + 1) else i
    }
}

// the braced or bracketed group at i (after spaces): {value, next}, or null
fn group_at(raw, i, opener, closer) {
    let at = skip_spaces(raw, i)
    if (at >= len(raw) or slice(raw, at, at + 1) != opener) null
    else {
        let stop = group_close(raw, at + 1, 1, opener, closer)
        {value: slice(raw, at + 1, stop), next: min(stop + 1, len(raw))}
    }
}

fn group_close(raw, i, depth, opener, closer) {
    if (i >= len(raw)) i
    else {
        let ch = slice(raw, i, i + 1)
        if (ch == opener) group_close(raw, i + 1, depth + 1, opener, closer)
        else if (ch == closer and depth == 1) i
        else if (ch == closer) group_close(raw, i + 1, depth - 1, opener, closer)
        else group_close(raw, i + 1, depth, opener, closer)
    }
}

fn skip_spaces(raw, i) => if (i < len(raw) and slice(raw, i, i + 1) == " ") skip_spaces(raw, i + 1) else i

// the length after \vskip: everything up to the next command or the end
fn bare_length(raw, i) {
    let at = skip_spaces(raw, i)
    let rest = slice(raw, at, len(raw))
    let stop = index_of(rest, "\\")
    {value: trim(if (stop == null) rest else slice(rest, 0, stop)), next: if (stop == null) len(raw) else at + stop}
}

fn add_css(acc, css) => if (css == null) acc else {*:acc, css: acc.css ++ css ++ ";"}

fn declaration_command(raw, command, after, acc, colors) {
    let size = sym.get_font_size(command)
    let group = group_at(raw, after, "{", "}")
    let option = group_at(raw, after, "[", "]")
    if (FONT_DECL_STYLES[command] != null) read_declarations(raw, after, add_css(acc, FONT_DECL_STYLES[command]), colors)
    else if (size != null) read_declarations(raw, after, add_css(acc, "font-size:" ++ size), colors)
    else if (ALIGN_COMMANDS[command] != null)
        read_declarations(raw, after, {*:acc, align: ALIGN_COMMANDS[command]}, colors)
    else if (command == "color") {
        let spec = group_at(raw, if (option == null) after else option.next, "{", "}")
        let css = if (spec == null) null else color.spec_color(if (option == null) null else option.value,
            spec.value, colors)
        if (css == null) read_declarations(raw, after, {*:acc, unknown: acc.unknown ++ ["color"]}, colors)
        else read_declarations(raw, spec.next, add_css(acc, "color:" ++ css), colors)
    }
    else if (command == "fontsize" and group != null) {
        let second = group_at(raw, group.next, "{", "}")
        let css = util.css_dimension(group.value)
        read_declarations(raw, if (second == null) group.next else second.next,
            add_css(acc, if (css == null) null else "font-size:" ++ css), colors)
    }
    else if ((command == "fontfamily" or command == "fontseries" or command == "fontshape") and group != null) {
        let css = if (command == "fontfamily") (let family = NFSS_FAMILIES[trim(group.value)],
                if (family == null) null else "font-family:" ++ family)
            else if (command == "fontseries") NFSS_SERIES[trim(group.value)]
            else NFSS_SHAPES[trim(group.value)]
        read_declarations(raw, group.next, if (css == null) {*:acc, unknown: acc.unknown ++ [command]}
            else add_css(acc, css), colors)
    }
    else if ((command == "begin" or command == "end") and group != null)
        read_declarations(raw, group.next, if (ALIGN_ENVIRONMENTS[trim(group.value)] != null and command == "begin")
            {*:acc, align: ALIGN_ENVIRONMENTS[trim(group.value)]} else acc, colors)
    else if (command == "MakeUppercase" or command == "uppercase")
        read_declarations(raw, after, {*:acc, transform: "uppercase"}, colors)
    else if (command == "MakeLowercase" or command == "lowercase")
        read_declarations(raw, after, {*:acc, transform: "lowercase"}, colors)
    else if (command == "titlerule" or command == "hrule")
        read_declarations(raw, if (option == null) after else option.next, {*:acc, rule: true}, colors)
    else if (command == "vspace" or command == "vspace*") {
        let star = slice(raw, after, after + 1) == "*"
        let length = group_at(raw, if (star) after + 1 else after, "{", "}")
        read_declarations(raw, if (length == null) after else length.next,
            {*:acc, space: if (length == null) acc.space else util.css_dimension(length.value)}, colors)
    }
    else if (command == "vskip" or command == "bigskip" or command == "medskip" or command == "smallskip") {
        let length = if (command == "vskip") bare_length(raw, after) else null
        let css = if (command == "bigskip") "12pt" else if (command == "medskip") "6pt"
            else if (command == "smallskip") "3pt" else util.css_dimension(split(length.value, " plus")[0])
        read_declarations(raw, if (length == null) after else length.next, {*:acc, space: css}, colors)
    }
    else if (contains(NEUTRAL_COMMANDS, command) or false)
        read_declarations(raw, after, {*:acc, text: acc.text ++ (if (command == "quad") " "
            else if (command == "qquad") "  " else "")}, colors)
    else read_declarations(raw, after, {*:acc, unknown: acc.unknown ++ [command]}, colors)
}

// the CSS of a one-argument font macro's body: a text command applied to #1
// (\textsc{#1}) or declarations before it ({\scshape #1}); null when it is neither
let TEXT_COMMANDS = {textbf: "font-weight:bold", textit: "font-style:italic", emph: "font-style:italic",
    textsl: "font-style:oblique", textsc: "font-variant:small-caps", texttt: MONO_CSS,
    textsf: SANS_CSS, textrm: SERIF_CSS, textup: "font-style:normal",
    textmd: "font-weight:normal"}
pub fn argument_font_css(source) {
    let body = trim(source or "")
    let command = if (starts_with(body, "\\") and ends_with(body, "{#1}")) slice(body, 1, len(body) - 4) else null
    if (body == "" or body == "#1") null
    else if (command != null and TEXT_COMMANDS[command] != null) TEXT_COMMANDS[command]
    else (let decls = declarations(replace(body, "#1", ""), null),
        if (decls.css == "" or len(decls.unknown) > 0 or trim(decls.text) != "") null else decls.css)
}

// wrap rendered items in a span with the declaration's style; a declaration
// whose value is outside the tables is reported in place
pub fn wrap_font_decl(decl, items) {
    let tag_str = string(name(decl))
    let style = decl_css(decl)
    if (style == null) [util.unsupported_element("latex",
        "Unsupported font declaration value: \\" ++ tag_str, decl.source_offset), *items]
    else <span class: "latex-" ++ tag_str, style: style, for c in items { c }>
}
