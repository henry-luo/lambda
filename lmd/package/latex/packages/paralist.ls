// paralist and the enumerate package's label templates
// (vibe/impl/Lambda_Impl_Latex_Packages.md §7).
import util: ~~.util

// paralist's environments by form: compact lists drop their spacing (enumitem's
// nosep), run-in lists set each item as an indented paragraph, inline lists run
// their items into the text
pub let ENVIRONMENTS = {
    compactitem: {kind: "itemize", form: "compact"}, compactenum: {kind: "enumerate", form: "compact"},
    compactdesc: {kind: "description", form: "compact"},
    asparaitem: {kind: "itemize", form: "runin"}, asparaenum: {kind: "enumerate", form: "runin"},
    asparadesc: {kind: "description", form: "runin"},
    inparaitem: {kind: "itemize", form: "inline"}, inparaenum: {kind: "enumerate", form: "inline"},
    inparadesc: {kind: "description", form: "inline"}}
pub let ENVIRONMENT_NAMES = [for (k, v at ENVIRONMENTS) string(k)]
pub let INLINE_NAMES = [for (k, v at ENVIRONMENTS where v.form == "inline") string(k)]

// the counter characters of a label template and their \the… formats
let COUNTERS = {'1': "arabic", a: "alph", A: "Alph", i: "roman", I: "Roman"}

// The label template [(a)] or [Step 1.]: the first of 1, a, A, i, I outside braces
// is the counter and the rest prints as written, with its braces removed. Null
// when the template has no counter.
pub fn template(option) {
    let chars = split(option or "", "")
    let at = counter_at(chars, 0, 0);
    (if (at == null) null else {
        before: literal(slice(chars, 0, at)), style: COUNTERS[chars[at]],
        after: literal(slice(chars, at + 1, len(chars)))})
}

fn counter_at(chars, i, depth) {
    if (i >= len(chars)) null
    else if (chars[i] == "{") counter_at(chars, i + 1, depth + 1)
    else if (chars[i] == "}") counter_at(chars, i + 1, depth - 1)
    else if (depth == 0 and COUNTERS[chars[i]] != null) i
    else counter_at(chars, i + 1, depth)
}

fn literal(chars) => join([for (ch in chars where ch != "{" and ch != "}") ch], "")

// item n's label under a template (n counts from 1)
pub fn label(tpl, n) {
    let counter = match tpl.style {
        case "alph": lower(util.upper_alph(n))
        case "Alph": util.upper_alph(n)
        case "roman": util.roman(n)
        case "Roman": upper(util.roman(n))
        default: string(n)
    };
    tpl.before ++ counter ++ tpl.after
}

// a list's default label when it has no template: LaTeX's first-level 1. and bullet
pub fn default_label(kind, n) => if (kind == "enumerate") string(n) ++ "." else "•"
