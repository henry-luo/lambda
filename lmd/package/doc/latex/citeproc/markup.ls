// Bibliographic markup is decoded into a small, inert rich-text vocabulary (S1.8).
import c: .common
import util: ~~.util

let SYMBOLS = {LaTeX: "LaTeX", TeX: "TeX", ae: "æ", AE: "Æ", oe: "œ", OE: "Œ",
    ss: "ß", o: "ø", O: "Ø", l: "ł", L: "Ł", i: "ı", j: "ȷ",
    textendash: "–", textemdash: "—", textasciitilde: "~", textbackslash: "\\",
    '&': "&", '%': "%", '#': "#", '_': "_", '$': "$", '{': "{", '}': "}"}
let ACCENTS = {'"': "̈", ["'"]: "́", '`': "̀", '^': "̂", '~': "̃", '=': "̄",
    '.': "̇", u: "̆", v: "̌", H: "̋", c: "̧", k: "̨", r: "̊", b: "̱", d: "̣"}
let FORMATS = {emph: "font-style:italic", textit: "font-style:italic",
    textbf: "font-weight:bold", textsc: "font-variant:small-caps",
    textsuperscript: "vertical-align:super", textsubscript: "vertical-align:sub"}

fn command_end(source, index) {
    if (index >= len(source)) index
    else {
        let ch = slice(source, index, index + 1)
        if ((ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z"))
            command_end(source, index + 1)
        else index
    }
}

fn argument(source, index) {
    if (slice(source, index, index + 1) == "{")
        util.read_balanced(source, index, "{", "}") ^ { {raw: "", next: len(source), invalid: true} }
    else {raw: slice(source, index, index + 1), next: index + 1}
}

fn appended(body, text) {
    let end = len(body) - 1
    if (end >= 0 and body[end] is string and text is string)
        slice(body, 0, end) ++ [body[end] ++ text]
    else body ++ [text]
}

fn scan(source, index, body, issues, file) {
    if (index >= len(source)) {content: body, text: c.text(body), diagnostics: issues}
    else {
        let ch = slice(source, index, index + 1)
        if (ch == "{") {
            let arg = argument(source, index)
            let decoded = scan(arg.raw, 0, [], [], file)
            scan(source, arg.next, body ++ [<span class: "nocase", decoded.content>],
                issues ++ decoded.diagnostics ++ (if (arg.invalid == true)
                    [c.issue("malformed-bib-markup", "Unclosed protected group", file)] else []), file)
        } else if (ch == "\\") {
            let end = command_end(source, index + 1)
            let next = if (end == index + 1) end + 1 else end
            let cmd = slice(source, index + 1, next)
            if (SYMBOLS[cmd] != null)
                scan(source, next, appended(body, SYMBOLS[cmd]), issues, file)
            else if (ACCENTS[cmd] != null or FORMATS[cmd] != null or cmd == "url" or cmd == "nolinkurl") {
                let at = if (slice(source, next, next + 1) == " ") next + 1 else next
                let arg = argument(source, at)
                let decoded = scan(arg.raw, 0, [], [], file)
                let value = if (ACCENTS[cmd] != null)
                    normalize(replace(replace(decoded.text, "ı", "i"), "ȷ", "j") ++ ACCENTS[cmd], 'nfc')
                    else if (FORMATS[cmd] != null) <span style: FORMATS[cmd], decoded.content>
                    else decoded.content
                scan(source, arg.next, appended(body, value), issues ++ decoded.diagnostics, file)
            } else scan(source, next, body ++ ["\\" ++ cmd], issues ++
                [c.issue("unsupported-bib-command", "Unsupported bibliographic command \\" ++ cmd, file)], file)
        } else if (slice(source, index, index + 3) == "---")
            scan(source, index + 3, body ++ ["—"], issues, file)
        else if (slice(source, index, index + 2) == "--")
            scan(source, index + 2, body ++ ["–"], issues, file)
        else scan(source, index + 1, appended(body, if (ch == "~") " " else ch), issues, file)
    }
}

pub fn bibtex(value, file = null) => scan(c.text(value), 0, [], [], file)

fn html_node(node, file) {
    if (node is string) {content: [node], diagnostics: []}
    else if (not (node is element)) {content: [], diagnostics: []}
    else {
        let tag = lower(string(name(node)))
        let decoded = html_children(content(node), file)
        let style = {i: "font-style:italic", b: "font-weight:bold", sup: "vertical-align:super",
            sub: "vertical-align:sub", em: "font-style:italic", strong: "font-weight:bold"}[tag]
        let allowed_span = tag == "span" and (node.class == "nocase" or node.class == "nodecor" or
            node.style == "font-variant:small-caps;")
        let allowed = style != null or allowed_span or c.has(["html", "body", "document", "div"], tag)
        let value = if (style != null) <span style: style, decoded.content>
            else if (allowed_span) <span class: node.class,
                style: if (node.style == "font-variant:small-caps;") node.style else null,
                decoded.content> else decoded.content
        {content: value, diagnostics: decoded.diagnostics ++ (if (allowed) [] else
            [c.issue("unsupported-csl-markup", "Unsupported CSL rich-text tag " ++ tag, file)])}
    }
}

fn html_children(nodes, file) {
    let decoded = [for (node in nodes) html_node(node, file)]
    {content: [for (part in decoded) part.content],
        diagnostics: [for (part in decoded, issue in part.diagnostics) issue]}
}

pub fn csl(value, file = null) {
    if (not (value is string) or not contains(value, "<"))
        {content: [value], text: c.text(value), diagnostics: []}
    else {
        let document = parse("<div>" ++ value ++ "</div>", "html") ^ { null }
        // HTML parsing supplies a head element; only the wrapped body is field markup.
        let body = util.find_descendant(document, "body")
        let decoded = html_node(if (body == null) document else body, file)
        {*:decoded, text: c.text(decoded.content)}
    }
}
