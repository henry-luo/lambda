// Presentation MathML conversion is font-independent (D7.2.8 / D7.1.1).
import sym: .symbols
import alphabet: .alphabet
import util: .util

pub let namespace = "http://www.w3.org/1998/Math/MathML"

fn key(raw) => util.command_name(raw)
fn value(n) => string(n.value or n.text or util.text_of(n))
fn require(ok, message) bool^ => if (ok) true else raise error("mathml: " ++ message)

// XML currently retains lexical QNames and xmlns attributes; resolve each scope.
pub fn bindings(n, inherited) => reduce([inherited, *[for (k,v in n)
    if (string(k) == "xmlns") {[""]:v}
    else if (starts_with(string(k), "xmlns:")) {[slice(string(k),6,len(string(k)))]:v}
    else {}]], (a,b) => {*:a,*:b})

pub fn local_name(n) => (let parts = split(string(name(n)), ":"), parts[len(parts)-1])
fn prefix(n) => (let parts = split(string(name(n)), ":"), if (len(parts)>1) parts[0] else "")

pub fn is_mathml(n) => n is element and local_name(n) == "math" and
    bindings(n,{})[prefix(n)] == namespace

pub fn parse_mathml(source) element^ {
    let document = parse(source, {type:"math", flavor:"mathml"})^
    let checked = require(is_mathml(document), "expected a MathML namespace math root")^
    document
}

// CLI resource reads use the ordinary IO boundary, with an explicit input format.
pub fn export_file(path, options = null) element^ => to_mathml(input(path,
    {type:options.input_type or "math", flavor:options.flavor})^,options)^

// Root detection must never confuse a TeX AST's unqualified math tag with MathML.
pub fn to_mathml(ast, options = null) element^ {
    let valid_source=require(not (ast is element) or is_mathml(ast) or ast.error==null,
        "input parse error: "++string(ast.error))^
    let valid_options=require((options==null or options is map) and
        (options.display==null or options.display is bool) and
        (options.source==null or options.source is string),"invalid conversion options")^
    if (is_mathml(ast) and options.display==null and options.source==null) ast
    else {
        let checked = require(ast is element or ast is array or ast is string,
            "expected a math AST")^
        let display_mode=if (options.display!=null) options.display else is_mathml(ast) and ast.display=="block"
        let context = {variant:"auto", style:if (display_mode) "display" else "text"}
        let body = if (is_mathml(ast)) <mrow for (child in content(ast)) child>
            else if (ast is element and name(ast) == 'math') row(util.content_items(ast),context)^
            else node(ast,context)^;
        <math *:if (is_mathml(ast)) ast else {},xmlns:namespace, display:if (display_mode) "block" else "inline",
            if (options.source != null) <semantics body; <annotation encoding:"application/x-tex",options.source>>
            else body>
    }
}

fn styled_context(c, command) map^ {
    let style = {displaystyle:"display",textstyle:"text",scriptstyle:"script",scriptscriptstyle:"scriptscript"}[command]
    let variant = alphabet.command_variant("\\" ++ command)
    let checked=require(style!=null or variant!=null or command=="operatorname",
        "unsupported style command \\" ++ command)^;
    {*:c, style:style or c.style, variant:variant or c.variant}
}

fn wrap_style(body, style) => <mstyle displaystyle:if (style == "display") "true" else "false",
    scriptlevel:if (style == "script") "1" else if (style == "scriptscript") "2" else "0",body>

fn row(items,c,i=0) element^ {
    if (i >= len(items)) { <mrow> }
    else {
        let n = items[i]
        let command = if (n is element) key(n.cmd or n.name) else ""
        let declaration = n is element and (name(n) == 'font_switch' or
            (name(n) == 'style_command' and n.arg == null))
        if (declaration) {
            let child = styled_context(c,command)^
            let tail = row(items,child,i+1)^;
            if (child.style != c.style) wrap_style(tail,child.style) else tail
        } else <mrow node(n,c)^; for (child in content(row(items,c,i+1)^)) child>
    }
}

fn token(text,c,kind="mi") {
    let chars = split(text,"")
    let mapped = join([for (ch in chars) chr(alphabet.variant(ch,
        if (c.variant == "auto") "normal" else c.variant))],"");
    if (kind == "mn") <mn mapped>
    else if (kind == "mtext") <mtext mapped>
    else if (kind == "mo") <mo mapped>
    else <mi *:if (c.variant == "normal" or (c.variant == "auto" and len(chars)==1 and
        ord(chars[0])>=0x391 and ord(chars[0])<=0x3A9)) {mathvariant:"normal"} else {},mapped>
}

let operator_chars="+-−=<>≤≥≠≈±∓×÷⋅∈∉∪∩()[]{}|,;:!/"
fn characters(text,c) => if (len(text)>0 and text!="." and all([for (ch in split(text,"")) contains("0123456789.",ch)]))
    token(text,c,"mn")
    // S2.6.1v2: TeX letters are separate array-carried tokens; ASCII words stay whole.
    else if (len(text)>1 and all([for (ch in split(text,""))
        not contains(operator_chars++"0123456789. \n\r\t",ch)])) token(text,c)
    else <mrow for (ch in split(text,"") where not contains([" ","\n","\t","\r"],ch))
        if (contains("0123456789",ch)) token(ch,c,"mn")
        else if (contains(operator_chars,ch)) token(if (ch=="-") "−" else ch,c,"mo")
        else token(ch,c)>

fn command(n,c) element^ {
    let command = key(n.name or n.cmd or n.value)
    let glyph_text = sym.lookup_symbol(command)
    let operator = sym.get_operator_name(command)
    let checked = require(glyph_text != null or operator != null,
        "unsupported command \\" ++ command)^
    if (operator != null) <mo movablelimits:if (sym.is_limit_op(command)) "true" else "false",operator>
    else if (sym.classify_symbol(command) == "mop") <mo largeop:"true",
        movablelimits:if (command=="intop" or not contains(command,"int")) "true" else "false",glyph_text>
    else if (sym.classify_symbol(command) != "mord") <mo glyph_text>
    else token(glyph_text,c)
}

fn fence(raw) => (let spelling=key(raw), if (spelling == "." or spelling == "") null
    else <mo stretchy:"true", sym.lookup_symbol(spelling) or raw>)

fn scripts(n,c) element^ {
    let base = node(n.base,c)^
    let child = {*:c,style:if (c.style=="display" or c.style=="text") "script" else "scriptscript"}
    let sub = if (n.sub != null) node(n.sub,child)^ else null
    let sup = if (n.sup != null) node(n.sup,child)^ else null
    let limits = n.modifier == "limits" or n.modifier == "displaylimits" or (n.modifier != "nolimits" and
        base is element and name(base) == 'mo' and
        base.movablelimits == "true")
    let operator = if (limits) <mo *:base, movablelimits:if (n.modifier=="limits") "false" else "true",for (child in content(base)) child> else base;
    if (limits and sub != null and sup != null) <munderover operator; sub; sup>
    else if (limits and sub != null) <munder operator; sub>
    else if (limits and sup != null) <mover operator; sup>
    else if (sub != null and sup != null) <msubsup base; sub; sup>
    else if (sub != null) <msub base; sub>
    else if (sup != null) <msup base; sup>
    else base
}

fn fraction(n,c) element^ {
    let command = key(n.cmd or "frac")
    let style = if (n.style != null and string(n.style) != "") ["display","text","script","scriptscript"][int(n.style)]
        else if (contains(["dfrac","dbinom"],command)) "display"
        else if (contains(["tfrac","tbinom"],command)) "text" else null
    let context = if (style != null) {*:c,style:style} else c
    let child = {*:context,style:if (context.style=="display") "text" else if (context.style=="text") "script" else "scriptscript"}
    let ruleless = contains(["binom","dbinom","tbinom","choose","atop","brace","brack"],command)
    let body = <mfrac *:if (n.thickness != null and string(n.thickness) != "") {linethickness:css_length(n.thickness)^}
        else if (ruleless) {linethickness:"0"} else {},
        node(n.numer,child)^; node(n.denom,child)^>
    let fences = if (contains(["binom","dbinom","tbinom","choose"],command)) ["(",")"]
        else if (command=="brace") ["{","}"] else if (command=="brack") ["[","]"]
        else [n.left_delim or n.left,n.right_delim or n.right]
    let result = if (fences[0] != null or fences[1] != null) <mrow fence(fences[0]); body; fence(fences[1])> else body;
    if (style != null) wrap_style(result,style) else result
}

fn matrix(n,c) element^ {
    let command = key(n.name or n.cmd or "matrix")
    let checked = require(contains(["matrix","pmatrix","bmatrix","Bmatrix","vmatrix","Vmatrix","cases"],command),
        "unsupported environment " ++ command)^
    let rows = util.parse_rows(util.content_items(n.body),0,len(util.content_items(n.body)),[],[],[])
    let valid_rows=require(all([for (r in rows) r.gap==null or trim(string(r.gap))==""]),
        "explicit TeX row gaps are not supported")^
    let body = <mtable for (r in rows) <mtr for (cell in r.cells)
        <mtd *:if (command=="cases") {style:"text-align:left"} else {},row(cell.items,c)^>>>
    let fences = {pmatrix:["(",")"],bmatrix:["[","]"],Bmatrix:["{","}"],vmatrix:["|","|"],Vmatrix:["‖","‖"],cases:["{","."]}[command];
    if (fences != null) <mrow fence(fences[0]);body;fence(fences[1])> else body
}

fn node(n,c) element^ {
    if (n == null) { <mrow> }
    else if (n is string) characters(n,c)
    else if (n is array) row(n,c)^
    else if (not (n is element)) raise error("mathml: unsupported AST value")
    else match name(n) {
        case 'math': row(util.content_items(n),c)^
        case 'group': row(util.content_items(n),c)^
        case 'brack_group': row(util.content_items(n),c)^
        case 'symbol': token(value(n),c)
        case 'number': token(value(n),c,"mn")
        case 'digit': token(value(n),c,"mn")
        case 'operator': token(value(n),c,"mo")
        case 'relation': token(value(n),c,"mo")
        case 'punctuation': token(value(n),c,"mo")
        case 'unicode_text': characters(value(n),c)
        case 'raw_math_text': token(value(n),{*:c,variant:"normal"},"mtext")
        case 'escaped_symbol': token(value(n),c)
        case 'command': command(n,c)^
        case 'symbol_command': command(n,c)^
        case 'big_operator': command(<command name:value(n)>,c)^
        case 'subsup': scripts(n,c)^
        case 'fraction': fraction(n,c)^
        case 'binomial': fraction(n,c)^
        case 'genfrac': fraction(n,c)^
        case 'infix_frac': fraction(n,c)^
        case 'radical': if (n.index != null) <mroot node(n.radicand,c)^;node(n.index,{*:c,style:"scriptscript"})^>
            else <msqrt node(n.radicand,c)^>
        case 'delimiter_group': <mrow fence(n.left);node(n.body or util.content_items(n),c)^;fence(n.right)>
        case 'sized_delimiter': raise error("mathml: explicit TeX delimiter sizing is not supported")
        case 'middle_delim': fence(n.delim or n.value)
        case 'style_command': styled(n,c)^
        case 'textstyle_command': styled(n,c)^
        case 'text_command': text_command(n)^
        case 'text_group': token(util.text_of(n),{*:c,variant:"normal"},"mtext")
        case 'accent': accented(n,c)^
        case 'overunder_command': if (key(n.cmd)=="underset") <munder node(n.base,c)^;node(n.annotation,c)^>
            else <mover node(n.base,c)^;node(n.annotation,c)^>
        case 'mathchoice': node(n[c.style],c)^
        case 'math_atom': raise error("mathml: explicit TeX atom classes are not supported")
        case 'mathop': raise error("mathml: compound TeX operators are not supported")
        case 'environment': matrix(n,c)^
        case 'matrix_command': matrix(n,c)^
        case 'space_command': spacing(n)^
        case 'spacing_command': spacing(n)^
        case 'hspace_command': spacing(n)^
        case 'skip_command': spacing(n)^
        case 'phantom_command': if (key(n.cmd)=="phantom") <mphantom node(n.content,c)^>
            else raise error("mathml: unsupported phantom " ++ key(n.cmd))
        default: raise error("mathml: unsupported construct " ++ string(name(n)))
    }
}

fn styled(n,c) element^ {
    let child=styled_context(c,key(n.cmd))^
    let body=node(n.arg or util.content_items(n),child)^;
    if (child.style != c.style) wrap_style(body,child.style)
    else if (key(n.cmd)=="operatorname") <mo movablelimits:if (n.limits==true) "true" else "false",util.text_of(n.arg)>
    else body
}

fn text_command(n) element^ {
    let checked=require(n.body==null or n.body is string,
        "structured TeX text requires a separate conversion")^
    let style=alphabet.command_variant(string(n.cmd))
    let text=util.text_of(n.body or n.content);
    <mtext *:if (style=="bold") {style:"font-weight:bold"}
        else if (style=="italic") {style:"font-style:italic"}
        else if (style=="mono") {style:"font-family:monospace"}
        else if (style=="sans") {style:"font-family:sans-serif"} else {},text>
}

fn accented(n,c) element^ {
    let command=key(n.cmd)
    let mark=if (command=="overline" or command=="bar") "¯" else if (command=="underline" or command=="underbar") "_"
        else sym.get_accent(command)
    let checked=require(mark != null,"unsupported accent " ++ command)^
    let body=node(n.base or n.body or n.arg,c)^;
    if (starts_with(command,"under")) <munder accentunder:"true",body;<mo stretchy:"true",mark>>
    else <mover accent:"true",body;<mo stretchy:"true",mark>>
}

fn spacing(n) element^ {
    let command=key(n.cmd or n.value)
    let widths={',':"0.16666666666666666em",':':"0.2222222222222222em",';':"0.2777777777777778em",
        '!':"-0.16666666666666666em",quad:"1em",qquad:"2em",enspace:"0.5em",thinspace:"0.16666666666666666em"}
    let width=n.width or n.dimension or n.amount or if (n.cmd!=null) n.value else widths[command]
    let checked=require(width != null,"unsupported spacing " ++ command)^;
    let dimension=css_length(if (width is element) util.text_of(width,true) else width)^;
    // CSS widths cannot be negative; a signed TeX kern is a margin instead.
    if (starts_with(dimension,"-")) <mspace width:"0em",style:"margin-left:"++dimension>
    else <mspace width:dimension>
}

fn css_length(raw) string^ {
    let text=trim(string(raw))
    let parsed=util.dimension_from_string(text)
    let explicit=ends_with(text,parsed.unit)
    let numeric=float(if (explicit) slice(text,0,len(text)-len(parsed.unit)) else text)^
    let checked=require(numeric-numeric==0 and
        contains(["em","ex","mu","pt","bp","pc","in","cm","mm","px"],parsed.unit),"unsupported dimension "++text)^;
    if (parsed.unit=="mu") string(numeric/18.0)++"em"
    // A TeX point is 1/72.27 inch; a CSS point is 1/72 inch.
    else if (parsed.unit=="pt") string(numeric*72.0/72.27)++"pt"
    else if (parsed.unit=="bp") string(numeric)++"pt"
    else if (parsed.unit=="pc") string(numeric*12.0*72.0/72.27)++"pt"
    else if (explicit) text else string(numeric)++"em"
}
