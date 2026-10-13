// Bounded listings profile: preserved source, captions, labels, and line numbers.
import util: ~~.util
import font_decl: ~~.elements.font_decl

let OPTION_NAMES = ["numbers", "firstnumber", "stepnumber", "caption", "label",
    "frame", "basicstyle", "breaklines", "showstringspaces", "language"]

pub fn options(node) => util.parse_kv_options(node.options_raw)

pub fn setting_options(node) => util.parse_kv_options(util.raw_argument(
    node, "required", 0))

pub fn merged(defaults, local) => {*:defaults, *:local}

fn preamble_options_at(node, at, command, current) {
    if (at >= len(node)) current
    else preamble_options_at(node, at + 1, command, preamble_options_of(node[at], command, current))
}

fn preamble_options_of(node, command, current) {
    if (not (node is element) or string(name(node)) == "document") current
    else if (string(name(node)) == command) merged(current, setting_options(node))
    else preamble_options_at(node, 0, command, current)
}

// the key settings of a preamble command (\lstset, fancyvrb's \fvset, minted's
// \setminted), merged in source order
pub fn preamble_settings(ast, command) => preamble_options_of(ast, command, {})

pub fn preamble_options(ast) => preamble_settings(ast, "lstset")

pub fn issues(options, offset) {
    let unknown = [for (key, value at options
        where not any([for (allowed in OPTION_NAMES) string(key) == allowed]))
        util.diagnostic("unsupported-listing-option", "listings", string(key),
            "Unsupported listings option " ++ string(key), offset)]
    let number_style = if (options.numbers == null or options.numbers == "none" or
        options.numbers == "left" or options.numbers == "right") []
        else [util.diagnostic("unsupported-listing-option", "listings", "numbers",
            "Unsupported listings number placement", offset)]
    let frame_style = if (options.frame == null or options.frame == "none" or
        options.frame == "single") []
        else [util.diagnostic("unsupported-listing-option", "listings", "frame",
            "Unsupported listings frame", offset)]
    let numerical = [for (key in ["firstnumber", "stepnumber"],
        let value = options[key]
        where value != null and ((int(value) ^ { null }) == null or int(value) < 1))
        util.diagnostic("invalid-listing-number", "listings", key,
            "Listings line numbers must be positive integers", offset)]
    let highlighting = if (options.language == null) []
        else [util.diagnostic("listing-highlighting-omitted", "listings",
            options.language, "Listings syntax highlighting is not implemented",
            offset)]
    unknown ++ number_style ++ frame_style ++ numerical ++ highlighting
}

fn basic_style(options) {
    let raw = if (options.basicstyle == null) "" else options.basicstyle
    let size = if (contains(raw, "\\tiny")) "font-size:0.6em;"
        else if (contains(raw, "\\scriptsize")) "font-size:0.7em;"
        else if (contains(raw, "\\footnotesize")) "font-size:0.8em;"
        else if (contains(raw, "\\small")) "font-size:0.9em;" else ""
    font_decl.MONO_CSS ++ ";" ++ size
}

fn code_lines(source, options) {
    let numbered = options.numbers == "left" or options.numbers == "right"
    let first = if (options.firstnumber == null) 1 else int(options.firstnumber)
    let step = if (options.stepnumber == null) 1 else int(options.stepnumber)
    let lines = split(source, "\n");
    [for (index, line in lines)
        <span class: "latex-listing-line",
            if (numbered and index % step == 0)
                <span class: "latex-listing-number", string(first + index)>;
            <span class: "latex-listing-source", line>
        >]
}

pub fn render_environment(node, defaults, ordinal) =>
    render_code(if (node.source == null) util.text_of(node) else node.source,
        merged(defaults, options(node)), ordinal)

// a code block: listings' environment, and fancyvrb's and minted's in listings' terms
// (codeblocks.ls); a language names the code's class
pub fn render_code(body, selected, ordinal, language = null) {
    let numbered = selected.numbers == "left" or selected.numbers == "right"
    let framed = selected.frame == "single"
    let caption = selected.caption
    let label = selected.label
    let css = basic_style(selected) ++
        (if (framed) "border:1px solid currentColor;padding:0.5em;" else "") ++
        (if (selected.breaklines == "true") "white-space:pre-wrap;" else "");
    <figure class: "latex-listing", id: if (label == null) null else util.slugify(label),
        if (caption != null)
            <figcaption class: "latex-listing-caption",
                "Listing " ++ string(ordinal) ++ ": " ++ caption>;
        <pre class: if (selected.numbers == "right") "latex-listing-numbered latex-listing-right"
            else if (numbered) "latex-listing-numbered" else "latex-listing-plain",
            style: css,
            <code class: if (language == null) null else "language-" ++ language,
                if (numbered) for (line in code_lines(body, selected)) line
                else body
            >
        >
    >
}

pub fn render_inline(node) =>
    <code class: "latex-code", style: basic_style(options(node)), util.text_of(node)>
