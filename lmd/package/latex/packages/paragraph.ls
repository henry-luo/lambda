// Paragraph packages (vibe/impl/Lambda_Impl_Latex_Packages.md §7): setspace's line
// spacing, parskip's paragraph spacing and ragged2e's alignment.
import util: ~~.util
import font_decl: ~~.elements.font_decl

// ---- setspace -----------------------------------------------------------------
// The line height is LaTeX's 1.2 baselineskip ratio times the stretch
// (font_decl.line_height_for), as with \linespread.

// setspace.sty's one-half and double stretches at each class size; single is 1
let STRETCH = {onehalf: {'10pt': 1.25, '11pt': 1.213, '12pt': 1.241},
    double: {'10pt': 1.667, '11pt': 1.618, '12pt': 1.655}}
let SPACINGS = {singlespacing: "single", onehalfspacing: "onehalf", doublespacing: "double"}
let SPACING_ENVIRONMENTS = {singlespace: "single", onehalfspace: "onehalf", doublespace: "double"}

pub let SETSPACE_DECLARATIONS = [for (k, v at SPACINGS) string(k)] ++ ["setstretch"]
pub let SETSPACE_ENVIRONMENTS = [for (k, v at SPACING_ENVIRONMENTS) string(k)] ++ ["spacing"]
pub let SETSPACE_OPTIONS = [for (k, v at SPACINGS) string(k)] ++ ["nodisplayskipstretch"]

fn class_size(class_options) {
    let sizes = [for (size in ["11pt", "12pt"] where contains(class_options or [], size) or false) size];
    (if (len(sizes) == 0) "10pt" else sizes[0])
}

fn stretch_for(spacing, class_options) =>
    if (spacing == "single") 1.0 else STRETCH[spacing][class_size(class_options)]

fn positive(text) {
    let value = float(text) ^ { null };
    (if (value == null or value <= 0.0) null else value)
}

// the stretch a declaration or \setstretch{f} sets, or null
pub fn declared_stretch(el, class_options) {
    let tag = string(name(el))
    if (SPACINGS[tag] != null) stretch_for(SPACINGS[tag], class_options)
    else if (tag == "setstretch") positive(trim(util.text_of(el)))
    else null
}

// the stretch an environment sets: spacing{f} takes its value; null when unknown
pub fn environment_stretch(tag, value, class_options) {
    if (SPACING_ENVIRONMENTS[tag] != null) stretch_for(SPACING_ENVIRONMENTS[tag], class_options)
    else if (tag == "spacing" and value != null) positive(value)
    else null
}

// the stretch the package options set, or null
pub fn option_stretch(options, class_options) {
    let chosen = [for (k, v at SPACINGS where options != null and util.option_enabled(options[k])) v];
    (if (len(chosen) == 0) null else stretch_for(chosen[len(chosen) - 1], class_options))
}

pub fn stretch_css(stretch) => "line-height:" ++ string(font_decl.line_height_for(stretch))

// ---- ragged2e -------------------------------------------------------------------
// The alignment declarations and environments, which unlike LaTeX's own allow
// hyphenation
let RAGGED = {RaggedRight: "text-align:left", RaggedLeft: "text-align:right",
    Centering: "text-align:center", justifying: "text-align:justify"}
let RAGGED_ENVIRONMENTS = {FlushLeft: "RaggedRight", FlushRight: "RaggedLeft",
    Center: "Centering", justify: "justifying"}

pub let RAGGED_DECLARATIONS = [for (k, v at RAGGED) string(k)]
pub let RAGGED_ENVIRONMENT_NAMES = [for (k, v at RAGGED_ENVIRONMENTS) string(k)]

pub fn ragged_css(tag) {
    let base = RAGGED[RAGGED_ENVIRONMENTS[tag] or tag];
    (if (base == null) null else base ++ ";hyphens:auto")
}

// ---- parskip --------------------------------------------------------------------
// Paragraphs separated by half a baselineskip (0.6em at the 1.2 ratio) and not
// indented; skip= and indent= set either, and indent alone keeps LaTeX's 1.5em
pub fn parskip_stylesheet(options) {
    let skip = if (options == null or options.skip == null) null else util.css_dimension(string(options.skip))
    let indent_value = if (options == null) null else options.indent
    let indent = if (indent_value == null or indent_value == "false") "0"
        else if (indent_value == true or indent_value == "" or indent_value == "true") "1.5em"
        else util.css_dimension(indent_value) or "1.5em";
    ".latex-document p,.latex-document h1 + p,.latex-document h2 + p,.latex-document h3 + p," ++
        ".latex-document h4 + p,.latex-document p:first-child{text-indent:" ++ indent ++
        ";margin:" ++ (skip or "0.6em") ++ " 0;}\n"
}
