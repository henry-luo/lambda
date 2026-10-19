// AsciiMath source writer; TeX and ASCII input share the direct-parser AST.
import common: .format_common
import util: .util

// Source spellings intentionally differ from the Unicode glyph registry.
let spellings = {
    'alpha': "alpha",
    'beta': "beta",
    'gamma': "gamma",
    'delta': "delta",
    'epsilon': "epsilon",
    'varepsilon': "epsilon",
    'zeta': "zeta",
    'eta': "eta",
    'theta': "theta",
    'vartheta': "theta",
    'iota': "iota",
    'kappa': "kappa",
    'lambda': "lambda",
    'mu': "mu",
    'nu': "nu",
    'xi': "xi",
    'pi': "pi",
    'varpi': "pi",
    'rho': "rho",
    'varrho': "rho",
    'sigma': "sigma",
    'varsigma': "sigma",
    'tau': "tau",
    'upsilon': "upsilon",
    'phi': "phi",
    'varphi': "phi",
    'chi': "chi",
    'psi': "psi",
    'omega': "omega",
    'Gamma': "Gamma",
    'Delta': "Delta",
    'Theta': "Theta",
    'Lambda': "Lambda",
    'Xi': "Xi",
    'Pi': "Pi",
    'Sigma': "Sigma",
    'Upsilon': "Upsilon",
    'Phi': "Phi",
    'Psi': "Psi",
    'Omega': "Omega",
    'times': "xx",
    'cdot': "*",
    'ast': "**",
    'pm': "+-",
    'mp': "-+",
    'div': "-:",
    'circ': "@",
    'oplus': "o+",
    'otimes': "ox",
    'odot': "o.",
    'setminus': "\\\\",
    'cap': "nn",
    'cup': "uu",
    'wedge': "^^",
    'vee': "vv",
    'leq': "<=",
    'le': "<=",
    'geq': ">=",
    'ge': ">=",
    'neq': "!=",
    'ne': "!=",
    'equiv': "-=",
    'approx': "~~",
    'cong': "~=",
    'prec': "-<",
    'succ': ">-",
    'in': "in",
    'notin': "!in",
    'subset': "sub",
    'supset': "sup",
    'subseteq': "sube",
    'supseteq': "supe",
    'propto': "prop",
    'to': "->",
    'rightarrow': "->",
    'leftarrow': "<-",
    'leftrightarrow': "<->",
    'Rightarrow': "=>",
    'Leftrightarrow': "<=>",
    'mapsto': "|->",
    'perp': "_|_",
    'parallel': "||",
    'infty': "oo",
    'infinity': "oo",
    'emptyset': "emptyset",
    'varnothing': "emptyset",
    'partial': "del",
    'nabla': "grad",
    'forall': "AA",
    'exists': "EE",
    'aleph': "aleph",
    'hbar': "hbar",
    'ell': "ell",
    'ldots': "...",
    'cdots': "cdots",
    'dots': "...",
    'sum': "sum",
    'prod': "prod",
    'coprod': "coprod",
    'int': "int",
    'iint': "iint",
    'iiint': "iiint",
    'oint': "oint",
    'lim': "lim",
    'limsup': "limsup",
    'liminf': "liminf",
    'sup': "sup",
    'inf': "inf",
    'max': "max",
    'min': "min",
    'det': "det",
    'gcd': "gcd",
    'sin': "sin",
    'cos': "cos",
    'tan': "tan",
    'sec': "sec",
    'csc': "csc",
    'cot': "cot",
    'arcsin': "arcsin",
    'arccos': "arccos",
    'arctan': "arctan",
    'sinh': "sinh",
    'cosh': "cosh",
    'tanh': "tanh",
    'log': "log",
    'ln': "ln",
    'exp': "exp",
    'ker': "ker",
    'deg': "deg",
    'hom': "hom",
    'arg': "arg",
    'dim': "dim"
}
let functions = ["sin", "cos", "tan", "sec", "csc", "cot", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "log", "ln", "exp", "ker", "deg", "hom", "arg", "dim"]
let function_words = ["sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "log", "ln", "exp", "sqrt", "abs", "floor", "ceil"]
let known_words = ["emptyset", "infinity", "epsilon", "upsilon", "arcsin", "arccos", "arctan", "lambda", "limsup", "liminf", "coprod", "approx", "alpha", "gamma", "delta", "theta", "kappa", "sigma", "omega", "Gamma", "Delta", "Theta", "Sigma", "Omega", "beta", "zeta", "iota", "sqrt", "sinh", "cosh", "tanh", "prod", "oint", "lim", "sin", "cos", "tan", "cot", "sec", "csc", "log", "exp", "sum", "int", "sup", "inf", "max", "min", "det", "gcd", "phi", "chi", "psi", "Phi", "Psi", "mu", "nu", "xi", "pi", "ln", "oo", "Xi", "Pi"]

let word_limit = max([for (word in known_words) len(word)])

pub fn to_ascii(ast) => item(ast,0)
fn item(value,depth) => common.item(value,depth,node,symbol_text)
fn text(value) => common.text(value)
fn mapped(value) => spellings[util.command_name(value)] or value
fn symbol_text(value) => if (value=="row_sep") "; " else if (value=="col_sep") ", " else mapped(value)
fn children(value,depth,separator=" ") {
    let items=util.content_items(value);
    sequence(items,0,len(items),depth,separator)
}

fn letter(value) => value is string and len(value)==1 and
    ((ord(value)>=65 and ord(value)<=90) or (ord(value)>=97 and ord(value)<=122))
fn letter_end(items,i,end) => if (i<end and letter(items[i])) letter_end(items,i+1,end) else i
fn close_paren(items,i,end,balance) {
    if (i>=end) -1
    else {
        let next=balance + (if (common.is_tag(items[i],"punctuation") and items[i].value=="(") 1
            else if (common.is_tag(items[i],"punctuation") and items[i].value==")") -1 else 0);
        if (next==0) i+1 else close_paren(items,i+1,end,next)
    }
}
fn matching_paren(items,i,end) => if (i<end and common.is_tag(items[i],"punctuation") and items[i].value=="(")
    close_paren(items,i+1,end,1) else -1

fn script(value,depth,separator=" ") => if (common.is_tag(value,"group")) "(" ++ children(value,depth+1,"") ++ ")"
    else if (common.is_tag(value,"paren_script")) children(value,depth+1,separator) else item(value,depth+1)
fn scripts(n,depth,separator=" ") =>
    (if (n.sub!=null) "_" ++ script(n.sub,depth,separator) else "") ++
    (if (n.sup!=null) "^" ++ script(n.sup,depth,separator) else "")

// A TeX token run may end in the scripted last letter of an ASCII word.
fn word_piece(items,i,end,depth,separator) {
    // Bound lookahead by the vocabulary; rescanning an unknown run would be quadratic.
    let stop=letter_end(items,i,min(end,i+word_limit))
    let trailing=stop<end and common.is_tag(items[stop],"subsup") and letter(items[stop].base)
    let letters=join(slice(items,i,stop),"") ++ (if (trailing) items[stop].base else "")
    let matches=[for (word in known_words where starts_with(letters,word)) word]
    let word=matches[0];
    if (word==null) {text:item(items[i],depth+1),next:i+1}
    else {
        let crosses_script=trailing and len(word)>stop-i
        let after=i+len(word)
        let paren_end=if (not crosses_script and word in function_words) matching_paren(items,after,end) else -1;
        if (paren_end>0) {text:word ++ "(" ++ sequence(items,after+1,paren_end-1,depth+1,separator) ++ ")",next:paren_end}
        else if (crosses_script) {text:word ++ scripts(items[stop],depth,""),next:stop+1}
        else {text:word,next:after}
    }
}

fn sequence(items,i,end,depth,separator,acc="",first=true) {
    if (i>=end) acc
    else {
        let piece=if (letter(items[i])) word_piece(items,i,end,depth,separator)
            else {text:item(items[i],depth+1),next:i+1};
        sequence(items,piece.next,end,depth,separator,acc ++ (if (first) "" else separator) ++ piece.text,false)
    }
}

fn command(n,depth) {
    if (not (n.name is string)) "?"
    else {
        let args=content(n)
        let function_args=len(args)>0 and util.command_name(n.name) in functions;
        mapped(n.name) ++ (if (function_args) "(" else "") ++
            join([for (arg in args) item(arg,depth+1)]," ") ++ (if (function_args) ")" else "")
    }
}

fn node(n,depth) {
    let kind=string(name(n));
    if (kind in ["operator","relation"]) if (n.value is string) mapped(n.value) else ""
    else if (kind in ["punctuation","delimiter"]) text(n.value)
    else if (kind=="subsup") item(n.base,depth+1) ++ scripts(n,depth)
    else if (kind=="radical") (if (n.index!=null) "root(" ++ item(n.index,depth+1) ++ ")" else "sqrt") ++
        "(" ++ item(n.radicand,depth+1) ++ ")"
    else if (kind in ["fraction","binomial","genfrac","frac_like","infix_frac"])
        "(" ++ item(n.numer,depth+1) ++ ")/(" ++ item(n.denom,depth+1) ++ ")"
    else if (kind=="command") command(n,depth)
    else if (kind=="symbol_command") if (n.name is string) mapped(n.name) else ""
    else if (kind=="delimiter_group") util.command_name(n.left) ++ children(n,depth) ++ util.command_name(n.right)
    else if (kind=="accent") util.command_name(n.cmd) ++ "(" ++ item(n.base,depth+1) ++ ")"
    else if (kind=="environment") text(n.name) ++ "(" ++ item(n.body,depth+1) ++ ")"
    else if (kind in ["text_command","textstyle_command","style_command"]) "\"" ++
        (if (n.content is string) n.content else children(n,depth)) ++ "\""
    else if (kind in ["space_command","spacing_command","hspace_command","skip_command"]) " "
    else if (kind in ["ascii_operator","quoted_text"]) children(n,depth,"")
    else if (kind=="big_operator") if (n.op is string) mapped(n.op) else ""
    else if (kind=="limits_modifier") ""
    else if (kind=="sized_delimiter") text(n.delim)
    else children(n,depth)
}

