// Experimental digestion of a bounded set of expanded LaTeX tokens.
// Unsupported structure is reported until the corresponding parser is ported.
import utf8: lambda.io.utf8
import tex_reader: .reader
fn control(token, name) => token != null and token.kind == "control" and token.value == name
fn opening(token) => token != null and token.kind == "char" and token.cat == 1
fn closing(token) => token != null and token.kind == "char" and token.cat == 2
fn block(token) any^ => token != null and token.kind == "control" and
    contains(["begin", "end", "documentclass", "usepackage", "title", "author", "date", "maketitle", "tableofcontents", "part", "chapter", "section", "subsection", "subsubsection", "paragraph", "subparagraph"], token.value)
fn unsupported(index, name) => {index: index, issue: "latex-digest-unsupported", unsupported: name}

// These commands have native syntax or output shapes beyond generic arguments.
fn special_command(name) => contains(["verb", "lstinline", "Verb", "mint", "mintinline",
    "directlua", "luaexec", "setlength", "underline", "quad", "qquad", "newtheorem",
    "pretitle", "posttitle", "preauthor", "postauthor", "predate", "postdate",
    "def", "gdef", "edef", "xdef", "newcommand", "renewcommand", "providecommand",
    "newenvironment", "renewenvironment"], name) or
    (len(name) == 1 and contains(["\\", "$", "%", "#", "&", "_", "{", "}",
        "'", "`", "^", "\"", "~", "=", ".", "u", "v", "H", "c", "d", "b", "t", "k", ",", ";", ":", "!"], name))

fn group_end(tokens, index, depth) any^ {
    if (index >= len(tokens)) {end: index, issue: "latex-digest-unclosed-group"}
    else {
        let token = tokens[index];
        let next = depth + if (opening(token)) 1 else if (closing(token)) -1 else 0;
        if (next == 0) {end: index + 1, issue: null}
        else group_end(tokens, index + 1, next)
    }
}

fn read_group(tokens, index) any^ {
    if (index >= len(tokens) or not opening(tokens[index]))
        {issue: "latex-digest-expected-group", end: index}
    else {
        let end = group_end(tokens, index + 1, 1)^;
        if (end.issue != null) end
        else {issue: null, end: end.end, tokens: slice(tokens, index + 1, end.end - 1)^,
            start_offset: tokens[index].start, end_offset: tokens[end.end - 1].end,
            file: tokens[index].file, direct: tokens[index].direct == true and tokens[end.end - 1].direct == true and
                tokens[index].file == tokens[end.end - 1].file}
    }
}

fn bracket_end(tokens, index) any^ {
    if (index >= len(tokens)) {issue: "latex-digest-unclosed-optional", end: index}
    else if (tokens[index].kind == "char" and tokens[index].value == "]") {issue: null, end: index + 1}
    else if (tokens[index].kind != "char" or opening(tokens[index]) or closing(tokens[index]) or tokens[index].value == "[")
        unsupported(index, "optional argument")
    else bracket_end(tokens, index + 1)
}

fn read_optional(tokens, index) any^ {
    if (index >= len(tokens) or tokens[index].kind != "char" or tokens[index].value != "[")
        {issue: null, present: false, end: index}
    else {
        let end = bracket_end(tokens, index + 1)^;
        if (end.issue != null) end
        else {issue: null, present: true, end: end.end,
            tokens: slice(tokens, index + 1, end.end - 1)^,
            start_offset: tokens[index].start, end_offset: tokens[end.end - 1].end,
            file: tokens[index].file, direct: tokens[index].direct == true and tokens[end.end - 1].direct == true and
                tokens[index].file == tokens[end.end - 1].file}
    }
}

fn plain_group(group) => not any([for (token in group.tokens) token.kind != "char" or token.cat == 1 or token.cat == 2])
fn group_text(group) => join([for (token in group.tokens) token.value], "")
fn append_text(items, text) => if (text == "") items else [*items, text]

fn inline_element(name, offset, children) => <[name] source_offset: offset, *children>

fn source_span(token, sources) any^ {
    let found = [for (entry in sources where entry.id == token.file) entry];
    if (token.direct != true or token.start < 0 or token.end < token.start or len(found) == 0) null
    else {
        let bytes = binary(found[0].data)^;
        if (token.end > len(bytes)) null else utf8.decode(slice(bytes, token.start, token.end)^)^
    }
}

fn group_raw(group, sources) any^ => source_span({direct: group.direct, file: group.file,
    start: group.start_offset + 1, end: group.end_offset - 1}, sources)

// whitespace skipped while reading a control word still belongs to the source.
fn control_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let gap_token = if (next != null and token.direct == true and next.direct == true and
            token.file == next.file and next.start > token.end)
        {kind: "char", value: " ", cat: 10, start: token.end, end: next.start,
            file: token.file, direct: true} else null;
    let gap = if (gap_token == null) null else source_span(gap_token, sources)^;
    if (gap != null and gap != "" and (trim(gap) ^ { null }) == "") {token: gap_token, text: gap}
    else {token: null, text: ""}
}

// preserve whitespace while matching the native comment line-ending rule.
fn source_gap_bytes(bytes, index = 0, comment = false, seen = false, out = "") any^ {
    if (index >= len(bytes)) {valid: true, seen: seen, text: out}
    else {
        let b = bytes[index];
        if (comment) source_gap_bytes(bytes, index + 1, b != 10 and b != 13, seen, out)
        else if (b == 37) source_gap_bytes(bytes, index + 1, true, true, out)
        else if (contains([9, 10, 12, 13, 32], b)) source_gap_bytes(bytes, index + 1, false, seen, out ++ chr(b))
        else {valid: false, seen: seen, text: out}
    }
}

// the last token in a group can still have skipped source before its closing brace.
fn trailing_gap_end(bytes, index, comment = false) any^ {
    if (index >= len(bytes)) index
    else {
        let b = bytes[index];
        if (comment) trailing_gap_end(bytes, index + 1, b != 10 and b != 13)
        else if (b == 37) trailing_gap_end(bytes, index + 1, true)
        else if (contains([9, 10, 12, 13, 32], b)) trailing_gap_end(bytes, index + 1)
        else index
    }
}

fn format_control_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let found = [for (source in sources where source.id == token.file) source];
    if (token.direct != true or len(found) == 0 or (next != null and
            (next.direct != true or next.file != token.file or next.start < token.end)))
        {valid: false, seen: false, text: ""}
    else {
        let bytes = binary(found[0].data)^;
        let end = if (next != null and next.direct == true and next.file == token.file and next.start >= token.end)
            next.start else if (next == null) trailing_gap_end(bytes, token.end)^ else token.end;
        let bounded = end <= len(bytes) and (next != null or end == len(bytes) or bytes[end] == 125);
        if (not bounded) {valid: false, seen: false, text: ""}
        else source_gap_bytes(slice(bytes, token.end, end)^)
    }
}

fn source_control_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let adjacent = next != null and token.direct == true and next.direct == true and
        token.file == next.file and token.seq != null and next.seq == token.seq + 1;
    if (adjacent and next.start >= token.end) {
        let raw = source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)^;
        if (raw == null) null else {
            let gap = source_gap_bytes(binary(raw)^)^;
            if (gap.valid) gap.text else null
        }
    }
    else ""
}

// reconstructed control words need a separator before an adjacent ASCII letter.
fn spacing_word_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let adjacent = next != null and token.direct == true and next.direct == true and
        token.file == next.file and token.seq != null and next.seq == token.seq + 1;
    if (adjacent) source_control_gap(tokens, sources, index)
    else if (next != null and contains(["char", "active"], next.kind) and len(next.value) == 1 and
        index_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ@", next.value) != null) " "
    else ""
}

fn restore_control_gap(tokens, sources, index) any^ {
    let gap = control_gap(tokens, sources, index)^;
    if (gap.token == null) tokens
    else [*slice(tokens, 0, index + 1)^, gap.token, *slice(tokens, index + 1, len(tokens))^]
}

// declaration scanners consume inter-argument whitespace before echoing raw tokens.
fn restore_declaration_gaps(tokens, sources, index, limit_value, out = []) any^ {
    if (index >= limit_value) {issue: null, tokens: [*out, *slice(tokens, limit_value, len(tokens))^]}
    else {
        let token = tokens[index];
        let next = if (index + 1 < limit_value) tokens[index + 1] else null;
        let can_gap = next != null and token.direct == true and next.direct == true and
            token.file == next.file and token.kind != "control" and token.cat != 10 and next.start > token.end;
        let raw = if (can_gap) source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)^ else null;
        let gap = if (raw != null) source_gap_bytes(binary(raw)^)^ else null;
        let plain = gap != null and gap.valid and not gap.seen and raw != "";
        if (plain and contains(raw, "\n\n")) unsupported(index, "declaration paragraph source gap")
        else {
            let space = if (plain) {kind: "char", value: " ", cat: 10, direct: true,
                file: token.file, start: token.end, end: next.start} else null;
            restore_declaration_gaps(tokens, sources, index + 1, limit_value,
                [*out, token, *(if (space == null) [] else [space])])
        }
    }
}

fn whitespace(token, next, sources) any^ {
    let raw = source_span(token, sources)^;
    let gap = if (next != null and token.direct == true and next.direct == true and token.file == next.file and
        next.start > token.end) source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)^ else null;
    let value = (if (raw == null or raw == "") token.value else raw) ++
        (if (gap != null and (trim(gap) ^ { null }) == "") gap else "");
    let after = if (control(next, "par")) source_span(next, sources)^ else null;
    // a blank line contributes the first newline to the preceding space token.
    if (after != null and (starts_with(after, "\n") or starts_with(after, "\r")) and ends_with(value, "\n"))
        slice(value, 0, len(value) - if (ends_with(value, "\r\n")) 2 else 1)^
    else value
}

fn dollar(token) => token != null and token.kind == "char" and token.cat == 3
fn math_digit(token) any^ => token != null and token.kind == "char" and len(token.value) == 1 and
    ord(token.value) >= 48 and ord(token.value) <= 57
fn math_letter(token) any^ => token != null and token.kind == "char" and len(token.value) == 1 and
    ((ord(token.value) >= 65 and ord(token.value) <= 90) or (ord(token.value) >= 97 and ord(token.value) <= 122))

fn math_close(tokens, index, display, closing = null) any^ {
    if (index >= len(tokens)) {issue: "latex-digest-unclosed-math", index: index}
    else if (closing != null and control(tokens[index], closing)) {issue: null, index: index, end: index + 1}
    else if (closing == null and dollar(tokens[index])) {
        if (not display) {issue: null, index: index, end: index + 1}
        else if (index + 1 < len(tokens) and dollar(tokens[index + 1])) {issue: null, index: index, end: index + 2}
        else unsupported(index, "display math delimiter")
    }
    else math_close(tokens, index + 1, display, closing)
}

fn math_number(tokens, index, text = "") any^ {
    if (index >= len(tokens) or not (math_digit(tokens[index]) or
        (tokens[index].kind == "char" and tokens[index].value == "."))) {index: index, text: text}
    else math_number(tokens, index + 1, text ++ tokens[index].value)
}

fn flush_math_strings(items, run) => if (len(run) == 0) items
    else if (len(run) == 1) [*items, run[0]] else [*items, run]

fn math_content(items, index = 0, out = [], run = []) any^ {
    if (index >= len(items)) flush_math_strings(out, run)
    else if (items[index] is string) math_content(items, index + 1, out, [*run, items[index]])
    else math_content(items, index + 1, [*flush_math_strings(out, run), items[index]], [])
}

fn math_nonspace(tokens, index) any^ => if (index < len(tokens) and tokens[index].kind == "char" and tokens[index].cat == 10)
    math_nonspace(tokens, index + 1) else index

// match delimiter pairs only at the current brace and left/right nesting level.
fn math_delimiter_boundary(tokens, index, nested = 0, braces = 0) any^ {
    if (index >= len(tokens)) {index: index, kind: "end"}
    else {
        let token = tokens[index];
        if (braces == 0 and control(token, "right") and nested == 0) {index: index, kind: "right"}
        else if (braces == 0 and control(token, "middle") and nested == 0) {index: index, kind: "middle"}
        else {
            let next_braces = braces + if (opening(token)) 1 else if (closing(token)) -1 else 0;
            let next_nested = if (braces != 0) nested else if (control(token, "left")) nested + 1
                else if (control(token, "right")) nested - 1 else nested;
            math_delimiter_boundary(tokens, index + 1, next_nested, next_braces)
        }
    }
}
fn math_delimiter_control(name) => contains(["{", "}", "|", "vert", "Vert",
    "lvert", "rvert", "lVert", "rVert", "langle", "rangle", "lbrace", "rbrace", "lceil", "rceil",
    "lfloor", "rfloor", "uparrow", "downarrow", "updownarrow", "Uparrow", "Downarrow", "Updownarrow",
    "backslash", "arrowvert", "Arrowvert", "bracevert", "lmoustache", "rmoustache", "lgroup", "rgroup",
    "lbrack", "rbrack", "lparen", "rparen"], name)

fn math_delimiter_token(tokens, index, middle = false, right_side = false) any^ {
    let at = math_nonspace(tokens, index)^;
    if (at >= len(tokens)) if (middle) unsupported(at, "math delimiter")
        else {issue: null, index: at, value: "."}
    else if (right_side and opening(tokens[at])) {
        let group = read_group(tokens, at)^;
        if (group.issue != null) group
        else if (len(group.tokens) == 0 and tokens[at].end == tokens[group.end - 1].start)
            {issue: null, index: group.end, value: ""}
        else {
            let inner_at = math_nonspace(group.tokens, 0)^;
            let comments = any([for (token in group.tokens) token.kind == "source-comment"]);
            if (comments or inner_at + 1 != len(group.tokens) or
                group.tokens[inner_at].end != tokens[group.end - 1].start) unsupported(at, "braced math delimiter")
            else {
                let inner = math_delimiter_token(group.tokens, inner_at)^;
                if (inner.issue != null) inner else {issue: null, index: group.end, value: inner.value}
            }
        }
    }
    else {
        let token = tokens[at];
        let char_ok = token.kind == "char" and not opening(token) and not closing(token) and
            len(token.value) == 1 and ord(token.value) < 128;
        let control_ok = token.kind == "control" and (not middle or math_delimiter_control(token.value));
        if (char_ok and (not middle or contains(["(", ")", "[", "]", "|", "<", ">", "/", "."], token.value)))
            {issue: null, index: at + 1, value: token.value}
        else if (control_ok) {issue: null, index: at + 1, value: "\\" ++ token.value}
        else unsupported(at, "math delimiter")
    }
}
fn math_delimiter_body(tokens, index, items = []) any^ {
    let boundary = math_delimiter_boundary(tokens, index)^;
    let section = math_atoms(slice(tokens, index, boundary.index)^)^;
    if (section.issue != null) section
    else {
        let combined = [*items, *section.items];
        if (boundary.kind == "end") {issue: null, index: boundary.index, right: ".", items: combined}
        else {
            let delimiter = math_delimiter_token(tokens, boundary.index + 1, boundary.kind == "middle", boundary.kind == "right")^;
            if (delimiter.issue != null) delimiter
            else if (boundary.kind == "right")
                {issue: null, index: delimiter.index, right: delimiter.value, items: combined}
            else math_delimiter_body(tokens, delimiter.index, [*combined, <middle_delim delim: delimiter.value>])
        }
    }
}
fn math_delimiter_group(tokens, index) any^ {
    let left = math_delimiter_token(tokens, index + 1)^;
    if (left.issue != null) left
    else {
        let body = math_delimiter_body(tokens, left.index)^;
        if (body.issue != null) body
        else {issue: null, index: body.index,
            value: <delimiter_group left: left.value, right: body.right, *math_content(body.items)^>}
    }
}

// command arguments use the same atom and group rules as scripts.
fn math_arguments(tokens, index, count, items = []) any^ {
    if (count == 0) {issue: null, index: index, items: items}
    else {
        let arg = math_script_arg(tokens, index)^;
        if (arg.issue != null) arg
        else math_arguments(tokens, arg.index, count - 1, [*items, arg.value])
    }
}

// generic math commands consume adjacent or space-separated groups as children.
fn math_generic_arguments(tokens, index, items = []) any^ {
    let at = math_nonspace(tokens, index)^;
    if (at >= len(tokens)) {issue: null, index: at, items: items}
    else if (opening(tokens[at])) {
        let arg = math_script_arg(tokens, at)^;
        if (arg.issue != null) arg else math_generic_arguments(tokens, arg.index, [*items, arg.value])
    }
    else if (tokens[at].kind == "char" and tokens[at].value == "[") {
        let arg = read_generic_delimited(tokens, at, "[", "]", "latex-digest-unclosed-optional")^;
        if (arg.issue != null) arg
        else {
            let content = math_atoms(arg.tokens)^;
            if (content.issue != null) content
            else math_generic_arguments(tokens, arg.end, [*items, <brack_group *math_content(content.items)^>])
        }
    }
    else {issue: null, index: at, items: items}
}

fn math_command(tokens, index) any^ {
    let name = tokens[index].value;
    if (contains(["frac", "dfrac", "tfrac"], name)) {
        let args = math_arguments(tokens, index + 1, 2)^;
        if (args.issue != null) args
        else {issue: null, index: args.index,
            value: <fraction cmd: "\\" ++ name, numer: args.items[0], denom: args.items[1]>}
    }
    // atom class commands retain their body and spacing class in the math AST.
    else if (contains(["mathord", "mathop", "mathbin", "mathrel", "mathopen", "mathclose", "mathpunct", "mathinner"], name)) {
        let body = math_script_arg(tokens, index + 1)^;
        if (body.issue != null) body
        else {
            let node = if (name == "mathop") <mathop body: body.value>
                else <math_atom cmd: "\\" ++ name, atom: "m" ++ slice(name, 4, len(name))^, body: body.value>;
            {issue: null, index: body.index, value: node}
        }
    }
    else if (name == "mathchoice") {
        let args = math_arguments(tokens, index + 1, 4)^;
        if (args.issue != null) args
        else {issue: null, index: args.index, value: <mathchoice display: args.items[0], text: args.items[1],
            script: args.items[2], scriptscript: args.items[3]>}
    }
    else if (name == "left") math_delimiter_group(tokens, index)
    // style declarations change following math without consuming an argument.
    else if (contains(["displaystyle", "textstyle", "scriptstyle", "scriptscriptstyle"], name))
        {issue: null, index: index + 1, value: <style_command cmd: "\\" ++ name>}
    else if (name == "operatorname") {
        let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
        let star = next != null and next.kind == "char" and next.value == "*";
        let adjacent = star and next.direct == true and tokens[index].direct == true and
            next.file == tokens[index].file and next.start == tokens[index].end;
        if (star and not adjacent) unsupported(index, "operatorname star source mapping")
        else {
            let arg = math_script_arg(tokens, index + if (star) 2 else 1)^;
            if (arg.issue != null) arg
            else {issue: null, index: arg.index,
                value: <style_command cmd: "\\operatorname", *:if (star) {limits: true} else {}, arg: arg.value>}
        }
    }
    else if (contains(["mathrm", "mathbf", "boldsymbol", "mathit", "mathsf", "mathsfit",
        "mathtt", "mathcal", "mathbb", "mathfrak", "mathscr", "mathnormal"], name)) {
        let arg = math_script_arg(tokens, index + 1)^;
        if (arg.issue != null) arg
        else {issue: null, index: arg.index, value: <style_command cmd: "\\" ++ name, arg: arg.value>}
    }
    else if (name == "underline" or name == "overline") {
        let base = math_script_arg(tokens, index + 1)^;
        if (base.issue != null) base
        else {issue: null, index: base.index, value: <accent cmd: "\\" ++ name, base: base.value>}
    }
    else if (name == "sqrt") {
        let at = math_nonspace(tokens, index + 1)^;
        let opt = read_optional(tokens, at)^;
        if (opt.issue != null) opt
        else {
            let degree = if (opt.present) math_atoms(opt.tokens)^ else {issue: null, items: []};
            if (degree.issue != null) degree
            else {
                let body = math_script_arg(tokens, opt.end)^;
                if (body.issue != null) body
                else {
                    let idx = <brack_group *math_content(degree.items)^>;
                    let value = if (opt.present) <radical index: idx, radicand: body.value>
                        else <radical radicand: body.value>;
                    {issue: null, index: body.index, value: value}
                }
            }
        }
    }
    else if (contains(["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta",
        "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau",
        "upsilon", "phi", "chi", "psi", "omega", "Gamma", "Delta", "Theta", "Lambda", "Xi",
        "Pi", "Sigma", "Upsilon", "Phi", "Psi", "Omega", "infty", "partial", "nabla", "cdot",
        "times", "div", "pm", "mp", "leq", "geq", "approx", "equiv", "in", "propto", "subset", "supset",
        "notin", "forall", "exists", "to", "mapsto", "rightarrow", "leftarrow", "leftrightarrow",
        "circ", "ast", "star", "bullet", "oplus", "otimes", "odot", "oslash", "wedge", "vee", "cap", "cup",
        "sqcap", "sqcup", "triangleleft", "triangleright", "wr", "bigcirc", "diamond", "bigtriangleup",
        "bigtriangledown", "boxplus", "boxminus", "boxtimes", "boxdot", "square", "blacksquare", "parallel",
        "perp", "angle", "triangle", "cong", "sim", "simeq", "prec", "succ", "preceq", "succeq", "ll", "gg",
        "asymp", "bowtie", "models", "vdash", "dashv", "top", "bot", "neg", "lnot"], name))
        {issue: null, index: index + 1, value: <symbol_command name: name>}
    else if (contains(["sum", "prod", "coprod", "int", "iint", "iiint", "oint", "intop", "oiint", "oiiint", "lim", "sin", "cos", "tan", "log", "ln", "exp",
        "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "arsinh", "arcosh", "artanh",
        "sech", "csch", "coth", "lg", "max", "min", "arg", "det", "gcd", "lcm", "deg", "dim", "ker", "hom",
        "limsup", "liminf", "sup", "inf"], name))
        {issue: null, index: index + 1, value: <command name: name>}
    else if (math_delimiter_control(name) and not contains(["{", "}"], name)) {
        let args = math_generic_arguments(tokens, index + 1)^;
        if (args.issue != null) args else {issue: null, index: args.index, value: <command name: name, *args.items>}
    }
    else unsupported(index, "math command " ++ name)
}

fn math_script_arg(tokens, index) any^ {
    let at = math_nonspace(tokens, index)^;
    if (at >= len(tokens)) unsupported(at, "math script argument")
    else if (opening(tokens[at])) {
        let group = read_group(tokens, at)^;
        if (group.issue != null) group
        else {
            let contents = math_atoms(group.tokens)^;
            if (contents.issue != null) contents
            else {issue: null, index: group.end, value: <group *math_content(contents.items)^>}
        }
    }
    else if (math_letter(tokens[at]) or math_digit(tokens[at]))
        {issue: null, index: at + 1, value: tokens[at].value}
    else if (tokens[at].kind == "control") math_command(tokens, at)
    else unsupported(at, "math script argument")
}

fn math_scripts(tokens, index, base, sub = null, sup = null) any^ {
    let at = math_nonspace(tokens, index)^;
    let token = if (at < len(tokens)) tokens[at] else null;
    if (token != null and token.kind == "char" and (token.cat == 7 or token.cat == 8)) {
        let superscript = token.cat == 7;
        if ((superscript and sup != null) or (not superscript and sub != null)) unsupported(at, "repeated math script")
        else {
            let arg = math_script_arg(tokens, at + 1)^;
            if (arg.issue != null) arg
            else math_scripts(tokens, arg.index, base, if (superscript) sub else arg.value,
                if (superscript) arg.value else sup)
        }
    }
    else {
        let value = if (sub != null and sup != null) <subsup base: base, sub: sub, sup: sup>
            else if (sub != null) <subsup base: base, sub: sub>
            else if (sup != null) <subsup base: base, sup: sup> else base;
        {issue: null, index: at, value: value}
    }
}

fn math_atoms(tokens, index = 0, items = []) any^ {
    if (index >= len(tokens)) {items: items, issue: null}
    else {
        let token = tokens[index];
        if (token.kind == "char" and token.cat == 10) math_atoms(tokens, index + 1, items)
        // a braced group is an atom and can itself carry scripts.
        else if (opening(token)) {
            let group = math_script_arg(tokens, index)^;
            if (group.issue != null) group
            else {
                let scripted = math_scripts(tokens, group.index, group.value)^;
                if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
            }
        }
        else if (math_letter(token) or math_digit(token)) {
            let numeric = if (math_digit(token)) math_number(tokens, index)^ else {index: index + 1, text: token.value};
            let scripted = math_scripts(tokens, numeric.index, numeric.text)^;
            if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
        }
        else if (token.kind == "control") {
            let atom = math_command(tokens, index)^;
            if (atom.issue != null) atom
            else {
                let scripted = math_scripts(tokens, atom.index, atom.value)^;
                if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
            }
        }
        // ordinary delimiters are punctuation atoms and can carry scripts.
        else if (token.kind == "char" and contains(["(", ")", "[", "]", ",", ":", "|"], token.value)) {
            let scripted = math_scripts(tokens, index + 1, <punctuation value: token.value>)^;
            if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
        }
        else if (token.kind == "char" and contains(["+", "-", "*", "/"], token.value))
            math_atoms(tokens, index + 1, [*items, <operator value: token.value>])
        else if (token.kind == "char" and token.value == "=")
            math_atoms(tokens, index + 1, [*items, <relation value: token.value>])
        else unsupported(index, "math atom")
    }
}

fn math_element(tokens, sources, index) any^ {
    let named = control(tokens[index], "(") or control(tokens[index], "[");
    let display = if (named) control(tokens[index], "[") else index + 1 < len(tokens) and dollar(tokens[index + 1]);
    let first = if (display and not named) index + 2 else index + 1;
    let closing = if (not named) null else if (display) "]" else ")";
    let close = math_close(tokens, first, display, closing)^;
    if (close.issue != null) close
    else {
        let start = tokens[first - 1];
        let finish = tokens[close.index];
        let body = slice(tokens, first, close.index)^;
        if (start.direct != true or finish.direct != true or start.file != finish.file or
            any([for (token in body) token.direct != true or token.file != start.file]))
            unsupported(index, "math source mapping")
        else {
            let source = source_span({direct: true, file: start.file, start: start.end, end: finish.start}, sources)^;
            let atoms = math_atoms(body)^;
            if (source == null) unsupported(index, "math source mapping")
            else if (atoms.issue != null) atoms
            else {
                let ast = <math *math_content(atoms.items)^>;
                let node = if (display) <display_math source_offset: start.end, source: source, ast: ast>
                    else <inline_math source_offset: start.end, source: source, ast: ast>;
                {issue: null, index: close.end, node: node}
            }
        }
    }
}

fn span_tokens(input, offset, limit, tokens = []) any^ {
    let read = tex_reader.next(input)^;
    if (read.token == null or read.token.start >= limit) tokens
    else span_tokens(read.reader, offset, limit, [*tokens,
        {*:read.token, start: read.token.start + offset, end: read.token.end + offset}])
}

fn definition_node(name, attrs, children) => <[name] *:attrs, *children>

fn definition_target(tokens, sources, index) any^ {
    if (index >= len(tokens)) unsupported(index, "definition target")
    else if (opening(tokens[index])) {
        let group = read_group(tokens, index)^;
        if (group.issue != null) group
        else {
            let raw = group_raw(group, sources)^;
            if (raw == null) unsupported(index, "definition target source")
            else {issue: null, end: group.end, value: raw,
                descriptor: {kind: "required", raw: raw, start: group.start_offset, end: group.end_offset}}
        }
    }
    else if (tokens[index].kind == "control") {
        let target = tokens[index];
        let raw = source_span(target, sources)^;
        if (raw == null) unsupported(index, "definition target source")
        else {issue: null, end: index + 1, value: raw,
            descriptor: {kind: "required", raw: raw, start: target.start, end: target.end}}
    }
    else unsupported(index, "definition target")
}

fn definition_option(tokens, sources, index) any^ {
    let opt = read_optional(tokens, index)^;
    if (opt.issue != null or not opt.present) opt
    else {
        let raw = group_raw(opt, sources)^;
        if (raw == null) unsupported(index, "definition option source")
        else {*:opt, value: <brack_group raw>, descriptor: {kind: "optional", raw: raw,
            start: opt.start_offset, end: opt.end_offset}}
    }
}

fn definition_prefixes(tokens, sources, index = 0, items = []) any^ {
    if (index >= len(tokens) or tokens[index].kind != "control" or
        not contains(["global", "long", "protected"], tokens[index].value)) {index: index, items: items}
    else {
        let token = tokens[index];
        let node = if (token.value == "global") <global source_offset: token.start, argument_groups: []>
            else if (token.value == "long") <long source_offset: token.start, argument_groups: []>
            else <protected source_offset: token.start, argument_groups: []>;
        let gap = control_gap(tokens, sources, index)^;
        definition_prefixes(tokens, sources, index + 1,
            [*items, node, *(if (gap.text == "") [] else [gap.text])])
    }
}

fn definition_body_start(tokens, index) any^ {
    if (index >= len(tokens)) unsupported(index, "definition body")
    else if (opening(tokens[index])) {issue: null, index: index}
    else if (tokens[index].kind != "char" or closing(tokens[index])) unsupported(index, "definition parameter text")
    else definition_body_start(tokens, index + 1)
}

fn definition_span(span, sources) any^ {
    {
        let input = {*:tex_reader.reader(span.value, span.file), catcodes: span.catcodes};
        let tokens = span_tokens(input, span.start, len(binary(span.value)^))^;
        let prefix = definition_prefixes(tokens, sources)^;
        let base = prefix.index;
        let cmd = if (base >= len(tokens)) null else tokens[base];
        let supported = cmd != null and cmd.kind == "control" and
            contains(["def", "gdef", "edef", "xdef", "newcommand", "renewcommand", "providecommand",
                "newenvironment", "renewenvironment"], cmd.value);
        if (cmd != null and cmd.kind == "control" and contains(["DeclareRobustCommand", "NewDocumentCommand",
                "RenewDocumentCommand", "ProvideDocumentCommand", "DeclareDocumentCommand"], cmd.value)) {
            let parsed = inline(tokens, sources, 0, "", [], false)^;
            if (parsed.issue != null) parsed
            else if (parsed.index != len(parsed.tokens)) unsupported(parsed.index, "document command definition")
            else {issue: null, nodes: parsed.items}
        }
        else if (not supported) unsupported(0, "definition command")
        else {
            let primitive = contains(["def", "gdef", "edef", "xdef"], cmd.value);
            let environment = cmd.value == "newenvironment" or cmd.value == "renewenvironment";
            let starred = not primitive and base + 1 < len(tokens) and tokens[base + 1].kind == "char" and tokens[base + 1].value == "*";
            let target = definition_target(tokens, sources, base + if (starred) 2 else 1)^;
            let first = if (target.issue != null) target else if (primitive) {issue: null, present: false, end: target.end}
                else definition_option(tokens, sources, target.end)^;
            let second = if (first.issue != null) first else if (primitive) {issue: null, present: false, end: first.end}
                else definition_option(tokens, sources, first.end)^;
            let body_at = if (second.issue != null) second else if (primitive) definition_body_start(tokens, second.end)^
                else {issue: null, index: second.end};
            let body = if (body_at.issue != null) body_at else read_group(tokens, body_at.index)^;
            if (target.issue != null) target
            else if (first.issue != null) first
            else if (second.issue != null) second
            else if (body_at.issue != null) body_at
            else if (body.issue != null) body
            else {
                let ending = if (environment) read_group(tokens, body.end)^ else {issue: null, end: body.end};
                if (ending.issue != null) ending
                else if (ending.end != len(tokens)) unsupported(ending.end, "definition trailing tokens")
                else {
                    let raw = group_raw(body, sources)^;
                    let delimited = primitive and body_at.index > second.end;
                    let parameters = if (delimited) source_span({direct: true, file: span.file,
                        start: target.descriptor.end, end: tokens[body_at.index].start}, sources)^ else "";
                    let end_raw = if (environment) group_raw(ending, sources)^ else "";
                    let parsed = if (environment) {issue: null, index: len(body.tokens), items: [raw]}
                        else inline(body.tokens, sources, 0, "", [], false)^;
                    if (raw == null or end_raw == null or parameters == null) unsupported(body.end, "definition body source")
                    else if (parsed.issue != null) parsed
                    else if (parsed.index != len(if (parsed.tokens == null) body.tokens else parsed.tokens)) unsupported(body.end, "definition body")
                    else {
                        let group = <curly_group *parsed.items>;
                        let end_group = <curly_group end_raw>;
                        let groups = [target.descriptor, *(if (first.present) [first.descriptor] else []),
                            *(if (second.present) [second.descriptor] else []),
                            *(if (delimited) [] else [{kind: "required", raw: raw, start: body.start_offset, end: body.end_offset}]),
                            *(if (environment) [{kind: "required", raw: end_raw,
                                start: ending.start_offset, end: ending.end_offset}] else [])];
                        let children = [target.value, *(if (first.present) [first.value] else []),
                            *(if (second.present) [second.value] else []), *(if (delimited) [] else [group]),
                            *(if (environment) [end_group] else [])];
                        let attrs = {source_offset: cmd.start, *:if (starred) {starred: true} else {}, argument_groups: groups};
                        {issue: null, nodes: [*prefix.items, definition_node(cmd.value, attrs, children),
                            *(if (delimited) [parameters, group] else [])]}
                    }
                }
            }
        }
    }
}

// generic delimited groups can contain nested delimiters, groups and commands.
fn generic_delimited_end(tokens, index, opener, closer, issue, depth = 1) any^ {
    if (index >= len(tokens)) {issue: issue, end: index}
    else {
        let token = tokens[index];
        let next = if (token.kind == "char" and token.value == opener) depth + 1
            else if (token.kind == "char" and token.value == closer) depth - 1 else depth;
        if (next == 0) {issue: null, end: index + 1}
        else generic_delimited_end(tokens, index + 1, opener, closer, issue, next)
    }
}

fn read_generic_delimited(tokens, index, opener, closer, issue) any^ {
    let end = generic_delimited_end(tokens, index + 1, opener, closer, issue)^;
    if (end.issue != null) end
    else {issue: null, end: end.end, tokens: slice(tokens, index + 1, end.end - 1)^,
        start_offset: tokens[index].start, end_offset: tokens[end.end - 1].end,
        file: tokens[index].file, direct: tokens[index].direct == true and tokens[end.end - 1].direct == true and
            tokens[index].file == tokens[end.end - 1].file}
}

// parenthesized command groups are recognized only before another group or for coordinates.
fn coord_part(text) any^ {
    let chars = split(text, "")^;
    any([for (c in chars) index_of("0123456789", c) != null]) and
        not any([for (c in chars) index_of("0123456789.+- \t\r\n" ++ chr(11) ++ chr(12), c) == null])
}
fn coordinate(group, sources) any^ {
    let raw = group_raw(group, sources)^;
    let pieces = if (raw == null) [] else split(raw, ",")^;
    len(pieces) == 2 and coord_part(pieces[0])^ and coord_part(pieces[1])^
}
fn nonspace_index(tokens, index) any^ => if (index < len(tokens) and tokens[index].kind == "char" and tokens[index].cat == 10)
    nonspace_index(tokens, index + 1) else index
fn paren_argument(tokens, sources, index) any^ {
    let group = read_generic_delimited(tokens, index, "(", ")", "latex-digest-unclosed-paren")^;
    if (group.issue != null) false
    else {
        let after = nonspace_index(tokens, group.end)^;
        let next = if (after < len(tokens)) tokens[after] else null;
        let ending = tokens[group.end - 1];
        let gap = if (next != null and ending.direct == true and next.direct == true and
                ending.file == next.file and next.start >= ending.end)
            source_span({direct: true, file: ending.file, start: ending.end, end: next.start}, sources)^ else null;
        let followed = gap != null and (trim(gap) ^ { null }) == "" and
            (opening(next) or (next != null and next.kind == "char" and next.value == "["));
        followed or coordinate(group, sources)^
    }
}

// a comment can join otherwise separated arguments.
fn comment_gap(text) any^ {
    if (text == null or text == "") false
    else {
        let gap = source_gap_bytes(binary(text)^)^;
        gap.valid and gap.seen
    }
}

// Consecutive text arguments retain their separate boundaries in an array.
fn argument_run(items, start, end) any^ =>
    if (end < len(items) and items[end] is string) argument_run(items, start, end + 1)
    else end

fn pack_arguments(items, index = 0, result = []) any^ {
    if (index >= len(items)) result
    else if (items[index] is string) {
        let end = argument_run(items, index, index + 1)^;
        let item = if (end == index + 1) items[index] else slice(items, index, end)^;
        pack_arguments(items, end, [*result, item])
    }
    else pack_arguments(items, index + 1, [*result, items[index]])
}

// a synthetic next token owns the offset at the reconstructed segment boundary.
fn argument_end_offset(tokens, arg) any^ {
    let next = if (arg.end < len(tokens)) tokens[arg.end] else null;
    let closing_token = tokens[arg.end - 1];
    if (next != null and next.read_offset != null) next.read_offset
    else if (next != null and next.direct == true and closing_token.direct == true and
            next.file == closing_token.file and next.seq != null and closing_token.seq != null and next.seq != closing_token.seq + 1)
        next.start
    else arg.end_offset
}

fn generic_arguments(tokens, sources, index, previous_end, source_file, groups = [], items = [], allow_paren = true, stop_on_gap = false, literal_end = false) any^ {
    if (index >= len(tokens)) {issue: null, index: index, groups: groups, items: items}
    else {
        let token = tokens[index];
        let required = opening(token);
        let optional = token.kind == "char" and token.value == "[";
        let paren_candidate = allow_paren and token.kind == "char" and token.value == "(";
        let probe = if (paren_candidate) read_generic_delimited(tokens, index, "(", ")", "latex-digest-unclosed-paren")^ else null;
        let uncertain = paren_candidate and (token.direct != true or (probe.issue == null and
            (probe.direct != true or any([for (t in probe.tokens) t.direct != true]))));
        let paren = paren_candidate and not uncertain and paren_argument(tokens, sources, index)^;
        if (uncertain) unsupported(index, "parenthesized command source mapping")
        else if (not required and not optional and not paren)
            {issue: null, index: index, groups: groups, items: items}
        else if (token.direct != true or token.file != source_file or token.start < previous_end)
            unsupported(index, "command argument source gap")
        else if (token.start > previous_end and not comment_gap(source_span({direct: true, file: source_file,
                start: previous_end, end: token.start}, sources)^)^)
            if (stop_on_gap) {issue: null, index: index, groups: groups, items: items}
            else unsupported(index, "command argument source gap")
        else {
            let arg = if (required) read_group(tokens, index)^ else if (optional) read_generic_delimited(tokens, index, "[", "]", "latex-digest-unclosed-optional")^ else read_generic_delimited(tokens, index, "(", ")", "latex-digest-unclosed-paren")^;
            if (arg.issue != null) arg
            else {
                let raw = group_raw(arg, sources)^;
                let parsed = inline(arg.tokens, sources, 0, "", [], false)^;
                if (raw == null) unsupported(index, "command argument source")
                else if (parsed.issue != null) parsed
                else if (parsed.index != len(parsed.tokens)) unsupported(index, "command argument")
                else {
                    let descriptor = {kind: if (required) "required" else if (optional) "optional" else "parenthesized", raw: raw,
                        start: arg.start_offset, end: if (literal_end) arg.end_offset else argument_end_offset(tokens, arg)^};
                    let children = if (optional) [<brack_group *parsed.items>]
                        else if (paren) [<paren_group *parsed.items>]
                        else if (len(parsed.items) == 0) [<curly_group>] else parsed.items;
                    generic_arguments(tokens, sources, arg.end, arg.end_offset, source_file,
                        [*groups, descriptor], [*items, *children], allow_paren, stop_on_gap, literal_end)
                }
            }
        }
    }
}

fn accent_next(tokens, index) any^ =>
    if (index < len(tokens) and tokens[index].kind == "char" and tokens[index].cat == 10)
        accent_next(tokens, index + 1) else index

fn accent_gap_safe(tokens, sources, index) any^ {
    let token = tokens[index];
    let next_index = accent_next(tokens, index + 1)^;
    if (token.direct != true) true
    else if (next_index >= len(tokens)) false
    else {
        let next = tokens[next_index];
        if (next.direct != true or next.file != token.file or next.start < token.end) false
        else {
            let gap = source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)^;
            gap != null and (gap == "" or (trim(gap) ^ { null }) == "")
        }
    }
}

fn accent_name(name) => contains(["'", "`", "^", "\"", "~", "=", ".", "u", "v", "H", "c", "d", "b", "t", "k"], name)
fn accent_node(tokens, sources, index) any^ {
    let token = tokens[index];
    let offset = if (token.emit_origin != null) token.emit_origin
        else if (token.read_offset != null) token.read_offset
        else if (token.direct == true) token.start else null;
    let arg = accent_argument(tokens, sources, index + 1)^;
    if (offset == null) unsupported(index, "accent source mapping")
    else if (not accent_gap_safe(tokens, sources, index)^) unsupported(index, "accent source gap")
    else if (arg.issue != null) arg
    else {index: arg.index, node: <accent command: token.value, source_offset: offset, *arg.items>, issue: null}
}

fn accent_argument(tokens, sources, index) any^ {
    if (index >= len(tokens)) unsupported(index, "accent argument")
    else if (tokens[index].kind == "char" and tokens[index].cat == 10)
        accent_argument(tokens, sources, index + 1)
    else if (opening(tokens[index])) {
        let group = read_group(tokens, index)^;
        if (group.issue != null) group
        else if (any([for (t in group.tokens) t.emit_origin != null and t.kind == "char" and t.cat == 12 and
            contains(["\\", "{", "}", "$", "&", "#", "^", "_", "%", "~"], t.value)]))
            unsupported(index, "accent emitted syntax")
        else {
            let children = inline(group.tokens, sources, 0, "", [], false)^;
            if (children.issue != null) children
            else if (children.index != len(children.tokens)) unsupported(index, "accent group")
            else {index: group.end, items: [<curly_group *children.items>], issue: null}
        }
    }
    else if (tokens[index].kind == "char" and not closing(tokens[index]))
        {index: index + 1, items: [tokens[index].value], issue: null}
    else if (tokens[index].kind == "control" and accent_name(tokens[index].value)) {
        let nested = accent_node(tokens, sources, index)^;
        if (nested.issue != null) nested else {index: nested.index, items: [nested.node], issue: null}
    }
    else unsupported(index, "accent argument")
}

fn rule_span_needs_reparse(token, sources) any^ {
    let span = token.echo_span;
    let raw = source_span({direct: true, file: span.file, start: span.start, end: span.end}, sources)^;
    raw == null or index_of(raw, "%") != null
}

// generated rules have two fully synthetic arguments sharing one source origin.
fn generated_rule_arg(group, origin) any^ {
    let tokens = group.tokens;
    let same_origin = all([for (token in tokens) token.emit_origin == origin]);
    if (not same_origin) {issue: "latex-digest-unsupported", unsupported: "rule argument source"}
    else if (len(tokens) == 1 and control(tokens[0], "linewidth"))
        {issue: null, raw: "\\linewidth", items: [<linewidth source_offset: origin, argument_groups: []>]}
    else if (all([for (token in tokens) token.kind == "char" and token.cat == 12])) {
        let text = join([for (token in tokens) token.value], "");
        {issue: null, raw: text, items: if (text == "") [<curly_group>] else [text]}
    }
    else {issue: "latex-digest-unsupported", unsupported: "rule argument"}
}
fn generated_rule(tokens, index) any^ {
    let origin = tokens[index].emit_origin;
    let first = read_group(tokens, index + 1)^;
    if (first.issue != null) first
    else {
        let second = read_group(tokens, first.end)^;
        if (second.issue != null) second
        else {
            let width = generated_rule_arg(first, origin)^;
            let height = generated_rule_arg(second, origin)^;
            if (width.issue != null) width
            else if (height.issue != null) height
            else {
                let groups = [{kind: "required", raw: width.raw, start: origin, end: origin},
                    {kind: "required", raw: height.raw, start: origin, end: origin}];
                let content = pack_arguments([*width.items, *height.items])^;
                {issue: null, index: second.end, node: <rule source_offset: origin, argument_groups: groups, *content>}
            }
        }
    }
}

fn box_span_needs_reparse(tokens, sources, index, group) any^ {
    let opening = tokens[index];
    let closing = tokens[group.end - 1];
    let raw = if (opening.emit_file == closing.emit_file and closing.box_close == true)
        source_span({direct: true, file: opening.emit_file, start: opening.emit_origin, end: closing.emit_origin}, sources)^
        else null;
    raw == null or index_of(raw, "%") != null or any([for (t in group.tokens)
        (t.emit_origin != null or t.direct == true) and t.kind == "char" and t.cat == 12 and
        contains(["\\", "{", "}", "$", "&", "#", "^", "_", "%", "~"], t.value)])
}
fn generated_mbox(tokens, sources, index) any^ {
    let origin = tokens[index].emit_origin;
    let group = read_group(tokens, index + 1)^;
    if (group.issue != null) group
    else {
        let opening = tokens[index + 1];
        let closing = tokens[group.end - 1];
        let next = if (group.end < len(tokens)) tokens[group.end] else null;
        let safe = opening.box_open == true and closing.box_close == true and
            opening.emit_origin == origin and opening.emit_file == closing.emit_file and
            next != null and next.direct == true and next.file == opening.emit_file and
            next.start == closing.emit_origin + 1 and
            all([for (t in group.tokens) t.kind == "char" and t.direct == true and t.file == opening.emit_file and
                contains([10, 11, 12], t.cat) and
                not contains(["\\", "{", "}", "$", "&", "#", "^", "_", "%", "~"], t.value)]);
        let raw = if (safe) source_span({direct: true, file: opening.emit_file,
            start: origin + 1, end: closing.emit_origin}, sources)^ else null;
        let text = join([for (t in group.tokens) t.value], "");
        if (not safe or raw == null or raw != text) unsupported(index, "box source mapping")
        else {
            let groups = [{kind: "required", raw: raw, start: origin, end: next.start}];
            let children = if (text == "") [<curly_group>] else [text];
            {issue: null, index: group.end, node: <mbox source_offset: origin, argument_groups: groups, *children>}
        }
    }
}

fn inline(tokens, sources, index = 0, text = "", items = [], stop_blocks = true) any^ {
    if (index >= len(tokens)) {index: index, items: append_text(items, text), tokens: tokens, issue: null}
    else {
        let token = tokens[index];
        if (stop_blocks and (control(token, "par") or block(token)))
            {index: index, items: append_text(items, text), tokens: tokens, issue: null}
        else if (control(token, "par"))
            inline(tokens, sources, index + 1, "", [*append_text(items, text), 'parbreak'], stop_blocks)
        else if (not stop_blocks and control(token, "begin")) {
            let gap = environment_command_gap(tokens, sources, index)^;
            if (gap == null or gap != "") unsupported(index, "environment command source gap")
            else {
                let env = parse_environment(tokens, sources, index, "inline")^;
                if (env.issue != null) env
                else inline(env.tokens, sources, env.index, "", [*append_text(items, text), env.node], stop_blocks)
            }
        }
        // native inline parsing consumes the end command but leaves its argument.
        else if (control(token, "end")) {
            let gap = control_gap(tokens, sources, index)^;
            inline(tokens, sources, index + 1, text ++ gap.text, items, stop_blocks)
        }
        else if (token.kind == "source-span") {
            let definition = definition_span(token, sources)^;
            if (definition.issue != null) definition
            else inline(tokens, sources, index + 1, "", [*append_text(items, text), *definition.nodes], stop_blocks)
        }
        else if (dollar(token) or control(token, "(") or control(token, "[")) {
            let math = math_element(tokens, sources, index)^;
            if (math.issue != null) math
            else inline(tokens, sources, math.index, "", [*append_text(items, text), math.node], stop_blocks)
        }
        else if (opening(token)) {
            let group = read_group(tokens, index)^;
            if (group.issue != null) group
            else if (token.box_open == true and box_span_needs_reparse(tokens, sources, index, group)^)
                unsupported(index, "box source gap")
            else {
                let children = inline(group.tokens, sources, 0, "", [], false)^;
                if (children.issue != null) children
                else if (children.index != len(children.tokens)) unsupported(index, "group")
                else inline(tokens, sources, group.end, "", [*append_text(items, text),
                    <curly_group *children.items>], stop_blocks)
            }
        }
        else if (token.kind == "active" and contains(["\\", "{", "}", "$", "&", "#", "^", "_", "%"], token.value))
            unsupported(index, "active syntax character")
        else if (token.kind == "active")
            inline(tokens, sources, index + 1, text ++ token.value, items, stop_blocks)
        else if (token.kind == "char" and not opening(token) and not closing(token) and token.cat != 3)
            inline(tokens, sources, index + 1, text ++ (if (token.cat == 10)
                whitespace(token, if (index + 1 < len(tokens)) tokens[index + 1] else null, sources)^ else token.value), items, stop_blocks)
        else if (control(token, "item")) {
            let opt = read_optional(tokens, index + 1)^;
            if (opt.issue != null) opt
            else if (opt.present) {
                let label = <brack_group group_text(opt)>;
                inline(tokens, sources, opt.end, "", [*append_text(items, text),
                    <item source_offset: token.start, label>], stop_blocks)
            }
            else {
                let gap = control_gap(tokens, sources, index)^;
                inline(tokens, sources, index + 1, gap.text,
                    [*append_text(items, text), <item source_offset: token.start>], stop_blocks)
            }
        }
        else if (token.kind == "control" and token.direct != true and token.read_offset == null and
            contains(["textbf", "textit", "emph", "texttt", "textsf", "textrm", "underline"], token.value))
            unsupported(index, "inline command source mapping")
        else if (token.kind == "control" and contains(["textbf", "textit", "emph", "texttt", "textsf", "textrm", "underline"], token.value)) {
            // native parsing keeps a formatting node even without a following group.
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let star = next != null and next.kind == "char" and next.value == "*";
            let mapped = star and token.direct == true and next.direct == true and token.file == next.file;
            let starred = mapped and token.end == next.start;
            let at = index + if (starred) 2 else 1;
            let gap = format_control_gap(tokens, sources, at - 1)^;
            if (star and not mapped) unsupported(index, "inline command star source mapping")
            else if (at >= len(tokens) or not opening(tokens[at]) or (gap.valid and gap.seen)) {
                let node = inline_element(token.value, if (token.read_offset != null)
                    token.read_offset else token.start, []);
                let text_gap = if (gap.valid) gap.text else spacing_word_gap(tokens, sources, at - 1)^;
                inline(tokens, sources, at, text_gap,
                    [*append_text(items, text), node], stop_blocks)
            }
            else {
                let arg = read_group(tokens, at)^;
                if (arg.issue != null) arg
                else {
                    let children = inline(arg.tokens, sources, 0, "", [], false)^;
                    if (children.issue != null) children
                    else if (children.index != len(children.tokens)) unsupported(index, "inline group")
                    else {
                        let node = inline_element(token.value, if (token.read_offset != null)
                            token.read_offset else token.start, children.items);
                        inline(tokens, sources, arg.end, "", [*append_text(items, text), node], stop_blocks)
                    }
                }
            }
        }
        else if (token.kind == "control" and token.value == " ") {
            let gap = source_control_gap(tokens, sources, index)^;
            if (gap == null) unsupported(index, "control-space source gap")
            else inline(tokens, sources, index + 1, text ++ " " ++ gap, items, stop_blocks)
        }
        else if (token.kind == "control" and contains(["$", "%", "#", "&", "_", "{", "}"], token.value))
            inline(tokens, sources, index + 1, text ++ token.value, items, stop_blocks)
        else if (token.kind == "control" and contains(["\\", "quad", "qquad", ",", ";", ":", "!"], token.value)) {
            let gap = if (contains(["quad", "qquad"], token.value))
                spacing_word_gap(tokens, sources, index)^ else control_gap(tokens, sources, index).text;
            let value = if (token.value == "\\") 'row_sep' else if (token.value == "quad") 'quad'
                else if (token.value == "qquad") 'qquad' else if (token.value == ",") ','
                else if (token.value == ";") ';' else if (token.value == ":") ':' else '!';
            if (gap == null) unsupported(index, "spacing command source gap")
            else inline(tokens, sources, index + 1, gap, [*append_text(items, text), value], stop_blocks)
        }
        else if (token.kind == "control" and accent_name(token.value)) {
            let accent = accent_node(tokens, sources, index)^;
            if (accent.issue != null) accent
            else inline(tokens, sources, accent.index, "", [*append_text(items, text), accent.node], stop_blocks)
        }
        else if (token.kind == "control" and contains(["hrule", "vrule"], token.value) and token.echo_span != null and
            rule_span_needs_reparse(token, sources)^)
            unsupported(index, "rule comment source gap")
        else if (control(token, "rule") and token.emit_origin != null) {
            let rule = generated_rule(tokens, index)^;
            if (rule.issue != null) rule
            else inline(tokens, sources, rule.index, "", [*append_text(items, text), rule.node], stop_blocks)
        }
        else if (control(token, "mbox") and token.emit_origin != null) {
            let box = generated_mbox(tokens, sources, index)^;
            if (box.issue != null) box
            else inline(tokens, sources, box.index, "", [*append_text(items, text), box.node], stop_blocks)
        }
        else if (token.kind == "control" and special_command(token.value))
            unsupported(index, token.value)
        else if (token.kind == "control" and (token.direct != true or token.read_offset != null))
            unsupported(index, "command source mapping")
        else if (token.kind == "control") {
            let starred = index + 1 < len(tokens) and tokens[index + 1].kind == "char" and tokens[index + 1].value == "*";
            let at = index + if (starred) 2 else 1;
            let next = if (at < len(tokens)) tokens[at] else null;
            let anchor = tokens[at - 1];
            let gap = if (next != null and anchor.direct == true and next.direct == true and
                    anchor.file == next.file and next.start > anchor.end)
                source_span({direct: true, file: anchor.file, start: anchor.end, end: next.start}, sources)^ else "";
            let spaced_group = next != null and (opening(next) or
                (next.kind == "char" and (next.value == "[" or next.value == "("))) and gap != "";
            let joined = if (spaced_group) comment_gap(gap)^ else false;
            if (spaced_group and not joined and (gap == null or (trim(gap) ^ { null }) != ""))
                unsupported(index, "command argument source gap")
            else if (spaced_group and not joined) {
                let attrs = {source_offset: token.start, *:if (starred) {starred: true} else {}, argument_groups: []};
                let node = <[token.value] *:attrs>;
                inline(tokens, sources, at, gap, [*append_text(items, text), node], stop_blocks)
            }
            else if (at < len(tokens) and (opening(tokens[at]) or
                    (tokens[at].kind == "char" and (tokens[at].value == "[" or tokens[at].value == "(")))) {
                let args = generic_arguments(tokens, sources, at, anchor.end, token.file)^;
                if (args.issue != null) args
                else {
                    let attrs = {source_offset: token.start, *:if (starred) {starred: true} else {}, argument_groups: args.groups};
                    let content = pack_arguments(args.items)^;
                    let node = <[token.value] *:attrs, *content>;
                    inline(tokens, sources, args.index, "", [*append_text(items, text), node], stop_blocks)
                }
            }
            else {
                let gap = if (starred) {text: ""} else control_gap(tokens, sources, index)^;
                let attrs = {source_offset: token.start, *:if (starred) {starred: true} else {}, argument_groups: []};
                let node = <[token.value] *:attrs>;
                inline(tokens, sources, at, gap.text, [*append_text(items, text), node], stop_blocks)
            }
        }
        else unsupported(index, "token")
    }
}

fn declaration(name, offset, groups, children, starred = false) =>
    <[name] source_offset: offset, *:if (starred) {starred: true} else {}, argument_groups: groups, *children>

fn environment_node(name, offset, children) => <[name] source_offset: offset, *children>

// Native digestion assigns special headers, math or raw bodies to these environments.
fn special_environment(name) => contains(["tikzpicture", "filecontents", "filecontents*",
    "tabular", "array", "longtable", "tabularx", "xltabular", "table", "subfigure", "subtable",
    "multicols", "multicols*", "lstlisting", "tcolorbox", "tabbing",
    "verbatim", "minted", "alltt", "Verbatim", "BVerbatim", "LVerbatim", "SaveVerbatim",
    "VerbatimOut", "fancyvrb", "comment", "luacode", "luacode*",
    "equation", "equation*", "eqnarray", "eqnarray*", "align", "align*", "alignat", "alignat*",
    "aligned", "aligned*", "gather", "gather*", "multline", "multline*", "split", "split*",
    "IEEEeqnarray", "IEEEeqnarray*", "cases", "cases*", "array*", "matrix", "matrix*",
    "pmatrix", "pmatrix*", "bmatrix", "bmatrix*", "vmatrix", "vmatrix*", "Vmatrix", "Vmatrix*",
    "smallmatrix", "smallmatrix*"], name)

fn heading(name, attrs) => <[name] *:attrs>

// the LaTeX source parser requires the environment name brace to touch the command.
fn environment_command_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    if (next == null or token.direct != true or next.direct != true or token.file != next.file or
        next.start <= token.end) ""
    else source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)
}

// an environment has the same structure in a paragraph or a nested argument.
fn parse_environment(tokens, sources, index, environment) any^ {
    let token = tokens[index];
    let arg = read_group(tokens, index + 1)^;
    if (arg.issue != null) arg
    else if (not plain_group(arg) or token.direct != true or arg.direct != true or
        special_environment(group_text(arg)) or
        (group_text(arg) == "document" and environment != null)) unsupported(index, "environment")
    else if (group_text(arg) != "document" and arg.end < len(tokens) and tokens[arg.end].kind == "char" and
        tokens[arg.end].value == "[" and tokens[arg.end].direct == true)
        unsupported(index, "environment options")
    else {
        let name = group_text(arg);
        let body = blocks(tokens, sources, arg.end, [], name)^;
        if (body.issue != null) body
        // nested blocks can insert gap tokens; return the stream that owns their cursor.
        else {issue: null, index: body.index, tokens: body.tokens, node: environment_node(name, token.start, body.nodes)}
    }
}

fn blocks(tokens, sources, index = 0, nodes = [], environment = null) any^ {
    if (index >= len(tokens)) if (environment == null) {index: index, nodes: nodes, tokens: tokens, issue: null}
        else unsupported(index, "unclosed environment")
    else {
        let token = tokens[index];
        let env_gap = if (control(token, "begin") or control(token, "end"))
            environment_command_gap(tokens, sources, index)^ else "";
        if (control(token, "par")) blocks(tokens, sources, index + 1, nodes, environment)
        else if (env_gap == null) unsupported(index, "environment command source gap")
        else if (env_gap != "") {
            if ((trim(env_gap) ^ { null }) != "") unsupported(index, "environment command source gap")
            else {
                let rest = restore_control_gap(tokens, sources, index)^;
                let para = inline(rest, sources, index + 1)^;
                if (para.issue != null) para
                else blocks(para.tokens, sources, para.index, [*nodes, <paragraph *para.items>], environment)
            }
        }
        else if (control(token, "end")) {
            let arg = read_group(tokens, index + 1)^;
            if (arg.issue != null) arg
            else if (environment != null and plain_group(arg) and group_text(arg) == environment)
                {index: arg.end, nodes: nodes, tokens: tokens, issue: null}
            else unsupported(index, "environment end")
        }
        else if (control(token, "begin")) {
            let env = parse_environment(tokens, sources, index, environment)^;
            if (env.issue != null) env
            else blocks(env.tokens, sources, env.index, [*nodes, env.node], environment)
        }
        else if (token.kind == "control" and
            contains(["part", "chapter", "section", "subsection", "subsubsection", "paragraph", "subparagraph"], token.value)) {
            let starred = index + 1 < len(tokens) and tokens[index + 1].kind == "char" and tokens[index + 1].value == "*";
            let opt = read_optional(tokens, index + if (starred) 2 else 1)^;
            let arg = if (opt.issue != null) opt else read_group(tokens, opt.end)^;
            if (opt.issue != null) opt
            else if (arg.issue != null) arg
            else {
                let children = inline(arg.tokens, sources, 0, "", [], false)^;
                if (children.issue != null) children
                else if (children.index != len(children.tokens)) unsupported(index, "heading title")
                else {
                    let title = <curly_group *children.items>;
                    let toc = if (opt.present) <brack_group group_text(opt)> else null;
                    let attrs = {source_offset: token.start, *:if (starred) {starred: true} else {},
                        *:if (toc != null) {toc: toc} else {}, title: title};
                    blocks(tokens, sources, arg.end, [*nodes, heading(token.value, attrs)], environment)
                }
            }
        }
        else if (token.kind == "control" and (token.value == "maketitle" or token.value == "tableofcontents")) {
            if (index + 1 < len(tokens) and (opening(tokens[index + 1]) or
                (tokens[index + 1].kind == "char" and tokens[index + 1].value == "[")))
                unsupported(index, "declaration arguments")
            else {
                let rest = restore_control_gap(tokens, sources, index)^;
                blocks(rest, sources, index + 1, [*nodes, declaration(token.value, token.start, [], [])], environment)
            }
        }
        else if (token.kind == "control" and contains(["documentclass", "usepackage", "title", "author", "date"], token.value)) {
            // declarations follow the same adjacent-group rules as generic commands.
            let end = if (token.echo_span != null) index + token.echo_count else index + 1;
            let restored = restore_declaration_gaps(tokens, sources, index, end, slice(tokens, 0, index)^)^;
            if (restored.issue != null) restored
            else {
                let next = if (index + 1 < len(restored.tokens)) restored.tokens[index + 1] else null;
                let star = next != null and next.kind == "char" and next.value == "*";
                let mapped = star and token.direct == true and next.direct == true and token.file == next.file;
                let starred = mapped and token.end == next.start;
                let at = index + if (starred) 2 else 1;
                let after = if (starred) next.end else token.end;
                let args = if (star and not mapped) unsupported(index, "declaration star source mapping")
                    else generic_arguments(restored.tokens, sources, at, after, token.file, [], [], true, true, true)^;
                if (args.issue != null) args
                else {
                    let node = declaration(token.value, token.start, args.groups, pack_arguments(args.items)^, starred);
                    let rest = restore_control_gap(restored.tokens, sources, args.index - 1)^;
                    blocks(rest, sources, args.index, [*nodes, node], environment)
                }
            }
        }
        else {
            let para = inline(tokens, sources, index)^;
            if (para.issue != null) para
            else if (para.index == index) unsupported(index, "empty paragraph")
            else blocks(para.tokens, sources, para.index, [*nodes, <paragraph *para.items>], environment)
        }
    }
}

pub fn digest(expanded) any^ {
    if (expanded.issue != null) {ast: null, issue: expanded.issue, unsupported: expanded.unsupported}
    // consumed paragraph data can change native argument and math source mapping.
    else if (expanded.paragraph_data_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "paragraph assignment source mapping"}
    else if (expanded.paragraph_quantity_seen == true and any([for (token in expanded.output) token.kind == "source-span"]))
        {ast: null, issue: "latex-digest-unsupported", unsupported: "paragraph quantity definition source mapping"}
    else if (expanded.font_name_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font name source mapping"}
    else if (expanded.font_token_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font token source mapping"}
    else if (expanded.mathchardef_emission_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "math character source mapping"}
    else if (expanded.expl3_int_math_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 integer source mapping"}
    else if (expanded.generated_text_mapping_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "generated text source mapping"}
    else if (expanded.inspection_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "inspection source mapping"}
    else if (expanded.numeric_math_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "numeric math source mapping"}
    else if (expanded.vcenter_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "vcenter source mapping"}
    else if (expanded.shipout_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "shipout source mapping"}
    else if (expanded.box_movement_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "box movement source mapping"}
    else if (expanded.pdf_random_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "random or timer source mapping"}
    else if (expanded.raw_inline_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "raw inline source mapping"}
    else if (expanded.input_stream_state_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "input stream command source mapping"}
    else if (expanded.input_eof_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "input EOF conditional source mapping"}
    else if (expanded.expl3_dynamic_read_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 dynamic read source mapping"}
    else if (expanded.expl3_dynamic_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 dynamic assignment source mapping"}
    else if (expanded.expl3_cs_eq_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 control-sequence equality source mapping"}
    else if (expanded.expl3_cs_exist_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 control-sequence existence source mapping"}
    else if (expanded.expl3_seq_in_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 sequence membership source mapping"}
    else if (expanded.expl3_seq_body_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 sequence macro source mapping"}
    else if (expanded.expl3_tl_empty_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 token-list conditional source mapping"}
    else if (expanded.expl3_tl_put_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 token-list assignment source mapping"}
    else if (expanded.page_state_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "page state assignment source mapping"}
    else if (expanded.page_state_quantity_seen == true and any([for (token in expanded.output) token.kind == "source-span"]))
        {ast: null, issue: "latex-digest-unsupported", unsupported: "page state definition source mapping"}
    else if (expanded.font_alias_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font alias assignment source mapping"}
    else if (expanded.font_family_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font family assignment source mapping"}
    else if (expanded.font_character_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font character assignment source mapping"}
    else if (expanded.font_character_quantity_seen == true and any([for (token in expanded.output) token.kind == "source-span"]))
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font character definition source mapping"}
    else {
        let parsed = blocks(expanded.output, expanded.sources)^;
        if (parsed.issue != null) {ast: null, issue: parsed.issue, unsupported: parsed.unsupported}
        else {
            let loaded = if (expanded.packages == null) [] else
                [for (package in expanded.packages where package.kind == "local") package.name];
            let count = if (expanded.diagnostics == null) 0 else len(expanded.diagnostics);
            let records = if (expanded.diagnostic_records == null) [] else expanded.diagnostic_records;
            let complete_report = len(records) == count;
            let root = if (complete_report and (count > 0 or len(loaded) > 0))
                <latex_document tex_diagnostics: records, tex_packages: loaded, *parsed.nodes>
                else <latex_document *parsed.nodes>;
            {ast: root, issue: null, complete: false}
        }
    }
}
