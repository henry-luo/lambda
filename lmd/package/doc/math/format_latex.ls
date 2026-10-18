// TeX source serialization; conversion policy stays in the package (D7.2.8).
import common: .format_common

pub fn to_latex(ast) => item(ast, 0)
fn item(value, depth) => common.item(value, depth, node, symbol_text)
fn children(value, depth, separator = " ") => common.children(value, depth, item, separator)
fn text(value) => common.text(value)
fn symbol_text(value) => if (value == "row_sep") " \\\\ " else if (value == "col_sep") " & " else "\\" ++ value
fn prefixed(value, fallback = "") => if (value is string)
    (if (starts_with(value, "\\")) "" else "\\") ++ value else fallback
fn braces(value, depth) => "{" ++ item(value, depth+1) ++ "}"
fn arguments(values, depth) => join([for (value in values) braces(value, depth)], "")
fn optional(value, depth) => if (value == null) "" else "[" ++
    (if (common.is_tag(value, "brack_group")) children(value, depth+1) else item(value, depth+1)) ++ "]"

fn script(value, marker, depth) {
    if (value == null) ""
    else {
        let needs_braces = if (value is string) len(value)>1
            else if (value is element) not common.is_tag(value,"group") and
                (not (common.is_tag(value,"command") or common.is_tag(value,"symbol_command")) or len(content(value))>0)
            else false;
        marker ++ (if (common.is_tag(value,"group")) "{" ++ children(value,depth+1) ++ "}"
            else if (needs_braces) braces(value,depth) else item(value,depth+1))
    }
}

fn two_arguments(n, first, second, fallback, depth) => common.text(n.cmd,fallback) ++
    optional(n.options,depth) ++ arguments([first,second],depth)

fn with_option(n, option, body, depth) => text(n.cmd) ++ optional(option,depth) ++ braces(body,depth)

fn generic_command(n, depth) {
    if (not (n.name is string)) "\\?"
    else "\\" ++ n.name ++ join([for (child in content(n))
        if (common.is_tag(child,"brack_group")) optional(child,depth)
        else if (child is element) braces(child,depth)
        else item(child,depth+1)], "")
}

fn frac_like(n, depth) {
    if (n.numer != null and n.denom != null) two_arguments(n,n.numer,n.denom,"\\frac",depth)
    else {
        let values = content(n)
        let explicit_command = values[0] is string and starts_with(values[0],"\\");
        if (len(values)==0) ""
        else (if (explicit_command) values[0] else "\\frac") ++
            join([for (i in (if (explicit_command) 1 else 0) to (len(values)-1))
                if (not explicit_command or common.is_tag(values[i],"group")) braces(values[i],depth)
                else item(values[i],depth+1)], "")
    }
}

fn environment(n, depth) => "\\begin{" ++ text(n.name) ++ "}" ++
    (if (n.columns is string) "{" ++ n.columns ++ "}" else "") ++
    (if (n.alignment is string) "[" ++ n.alignment ++ "]" else "") ++
    (if (n.pairs is string) "{" ++ n.pairs ++ "}" else "") ++
    " " ++ item(n.body,depth+1) ++ " \\end{" ++ text(n.name) ++ "}"

fn color(n, depth) => text(n.cmd) ++
    (if (n.border_color != null) braces(n.border_color,depth) else "") ++
    braces(if (n.color_raw != null) n.color_raw else n.color,depth) ++
    (if (n.content != null) braces(n.content,depth) else "")

fn transform(n, depth) => if (n.raise == null) with_option(n,n.options,n.body,depth)
    else text(n.cmd) ++ braces(n.raise,depth) ++ optional(n.height,depth) ++
        optional(n.depth,depth) ++ braces(n.body,depth)

fn node(n, depth) {
    let kind = string(name(n));
    if (kind in ["operator","relation","punctuation","delimiter","space_command","spacing_command","hspace_command","skip_command"]) text(n.value)
    else if (kind == "subsup") item(n.base,depth+1) ++
        (if (n.modifier is string) "\\" ++ n.modifier else "") ++
        script(n.sub,"_",depth) ++ script(n.sup,"^",depth)
    else if (kind == "brack_group") "[" ++ children(n,depth) ++ "]"
    // The parser stores a bracket group as the root index, so unwrap it once.
    else if (kind == "radical") "\\sqrt" ++ optional(n.index,depth) ++ braces(n.radicand,depth)
    else if (kind == "fraction") two_arguments(n,n.numer,n.denom,"\\frac",depth)
    // Binomials share the parser's numer/denom fields with fractions.
    else if (kind == "binomial") two_arguments(n,n.numer,n.denom,"\\binom",depth)
    else if (kind == "frac_like") frac_like(n,depth)
    else if (kind == "genfrac") "\\genfrac" ++ arguments([n.left_delim,n.right_delim,n.thickness,n.style,n.numer,n.denom],depth)
    else if (kind == "infix_frac") "{" ++ item(n.numer,depth+1) ++ " " ++ text(n.cmd) ++
        item(n.thickness,depth+1) ++ " " ++ item(n.denom,depth+1) ++ "}"
    else if (kind == "command") generic_command(n,depth)
    else if (kind == "symbol_command") if (n.name is string) "\\" ++ n.name else ""
    else if (kind == "delimiter_group") "\\left" ++ common.text(n.left,".") ++ " " ++
        (if (n.body != null) item(n.body,depth+1) else children(n,depth)) ++ " \\right" ++ common.text(n.right,".")
    else if (kind == "accent") prefixed(n.cmd,"\\hat") ++ braces(n.base,depth)
    else if (kind == "big_operator") text(n.op) ++
        (if (n.lower != null) "_" ++ braces(n.lower,depth) else "") ++
        (if (n.upper != null) "^" ++ braces(n.upper,depth) else "")
    else if (kind == "environment") environment(n,depth)
    // Decoded text paints glyphs; authored escapes belong in serialized TeX.
    else if (kind in ["text_command","textstyle_command"]) common.text(n.cmd,"\\text") ++ "{" ++
        (if (n.content_raw is string) n.content_raw else if (n.content is string) n.content else children(n,depth)) ++ "}"
    else if (kind in ["style_command","size_command","font_switch","array_rule","layout_control"]) text(n.cmd) ++
        (if (n.limits == true) "*" else "") ++ (if (n.arg != null) braces(n.arg,depth) else "")
    else if (kind == "overunder_command") text(n.cmd) ++ arguments([n.annotation,n.base],depth)
    else if (kind in ["extensible_arrow","extended_arrow"]) with_option(n,
        if (kind=="extended_arrow") n.lower else n.below,
        if (kind=="extended_arrow") n.upper else n.above,depth)
    else if (kind == "image_command") with_option(n,n.options,n.src,depth)
    else if (kind == "row_sep") " \\\\" ++ optional(n.gap,depth)
    else if (kind == "sized_delimiter") prefixed(n.size) ++ text(n.delim)
    else if (kind == "color_command") color(n,depth)
    else if (kind in ["box_command","phantom_command"]) with_option(n,n.options,n.content,depth)
    else if (kind in ["mathop_command","mathop"]) "\\mathop" ++ braces(if (n.body != null) n.body else n.content,depth)
    else if (kind == "mathchoice") "\\mathchoice" ++ arguments([n.display,n.text,n.script,n.scriptscript],depth)
    else if (kind == "math_atom") with_option(n,n.options,n.body,depth)
    else if (kind == "matrix_command") text(n.cmd) ++ braces(n.body,depth)
    else if (kind == "rule_command") "\\rule" ++ optional(n.raise,depth) ++ arguments([n.width,n.height],depth)
    else if (kind == "limits_modifier") ""
    else if (kind in ["ascii_operator","quoted_text"]) children(n,depth,"")
    else if (kind in ["not_overlay","not_empty"]) "\\not" ++ braces(n.target,depth)
    else if (kind == "box_transform") transform(n,depth)
    else if (kind == "equation_tag") "\\tag" ++ (if (n.starred==true) "*" else "") ++ braces(n.body,depth)
    else if (kind == "mod_command") if (n.body==null) text(n.cmd) else with_option(n,n.options,n.body,depth)
    else if (kind == "verbatim") "\\verb" ++ (if (n.starred==true) "*" else "") ++ text(n.delimiter) ++ text(n.value) ++ text(n.delimiter)
    else if (kind in ["cd_arrow","text_group"]) text(n.source)
    else children(n,depth)
}
