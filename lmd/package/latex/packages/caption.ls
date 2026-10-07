// Caption layout and options share one profile for floats and subfloats.
import util: ~~.util

pub let OPTIONS = ["font", "labelfont", "textfont", "labelsep", "justification", "skip", "labelformat"]
let FONTS = {small: "font-size:90%;", footnotesize: "font-size:80%;",
    scriptsize: "font-size:70%;", normalsize: "font-size:100%;",
    large: "font-size:120%;", bf: "font-weight:bold;", it: "font-style:italic;",
    sf: "font-family:sans-serif;", rm: "font-family:serif;",
    tt: "font-family:monospace;", normalfont: "font-style:normal;font-weight:normal;"}

fn font_parts(value) {
    if (value == null) [] else util.split_top_level(util.unwrap_braces(value), ",")
}

pub fn invalid_options(opts) {
    [for (key, value at opts where not any([for (allowed in OPTIONS)
        string(key) == allowed])) string(key)] ++
    [for (key in ["font", "labelfont", "textfont"],
        token in font_parts(opts[key]) where FONTS[trim(token)] == null) key] ++
    (if (opts.labelsep == null or any([for (item in ["colon", "period", "space", "quad", "newline", "none"])
        item == opts.labelsep])) [] else ["labelsep"]) ++
    (if (opts.justification == null or any([for (item in ["centering", "raggedright", "raggedleft", "justified"])
        item == opts.justification])) [] else ["justification"]) ++
    (if (opts.skip == null or util.css_dimension(opts.skip) != null) [] else ["skip"]) ++
    (if (opts.labelformat == null or any([for (item in ["simple", "parens", "empty"])
        item == opts.labelformat])) [] else ["labelformat"])
}

pub fn issues(opts, offset) => [for (key in invalid_options(opts))
    util.diagnostic("unsupported-caption-option", "caption", key,
        "Unsupported caption option or value: " ++ key, offset)]

pub fn initial(caption, subcaption) => {global: if (caption == null) {} else caption,
    figure: {}, table: {}, subfigure: if (subcaption == null) {} else subcaption,
    subtable: if (subcaption == null) {} else subcaption}

pub fn setup(node) => util.parse_kv_options(util.raw_argument(node, "required", 0))

pub fn update(config, node, current_type) {
    let requested = util.optional_raw(node)
    let target = if (requested != null) trim(requested)
        else if (current_type != null) current_type else "global"
    let opts = setup(node)
    if (config[target] == null or len(invalid_options(opts)) > 0) config
    else match target {
        case "figure": {*:config, figure: {*:config.figure, *:opts}}
        case "table": {*:config, table: {*:config.table, *:opts}}
        case "subfigure": {*:config, subfigure: {*:config.subfigure, *:opts}}
        case "subtable": {*:config, subtable: {*:config.subtable, *:opts}}
        default: {*:config, global: {*:config.global, *:opts}}
    }
}

fn font_css(value) => join([for (token in font_parts(value)) FONTS[trim(token)]], "")

fn separator(value, sub) {
    match value {
        case "period": ". "
        case "space": " "
        case "quad": "  "
        case "none": ""
        case "newline": "\n"
        default: if (sub) " " else ": "
    }
}

pub fn render(content, kind, ordinal_text, config, starred, offset) {
    let sub = starts_with(kind, "sub")
    let parent = if (kind == "subfigure") "figure" else if (kind == "subtable") "table" else kind
    let opts = {*:config.global, *:config[parent], *:config[kind]}
    if (len(invalid_options(opts)) > 0)
        util.unsupported_element("caption", "Unsupported caption options", offset)
    else {
        let label = if (starred or opts.labelformat == "empty") ""
            else if (sub) if (opts.labelformat == "simple") ordinal_text else "(" ++ ordinal_text ++ ")"
            else (if (kind == "table") "Table " else "Figure ") ++ ordinal_text
        let align = match opts.justification {
            case "raggedright": "left"
            case "raggedleft": "right"
            case "justified": "justify"
            default: "center"
        };
        <div class: "latex-caption latex-" ++ kind ++ "-caption",
            style: "text-align:" ++ align ++ ";white-space:pre-line;" ++ font_css(opts.font) ++
                (if (opts.skip == null) "" else "margin-top:" ++ util.css_dimension(opts.skip) ++ ";"),
            if (label != "") { <span class: "latex-caption-label", style: font_css(opts.labelfont),
                label ++ separator(opts.labelsep, sub)> }
            <span class: "latex-caption-text", style: font_css(opts.textfont), for (item in content) item>
        >
    }
}
