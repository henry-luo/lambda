// Structure templates share one source-slot contract; symbol glyphs use the math registry.
import symbols: lambda.doc.math.symbols

pub let examples = [
    {id:"quadratic",label:"Quadratic formula",source:"x=\\frac{-b\\pm\\sqrt{b^2-4ac}}{2a}"},
    {id:"pythagorean",label:"Pythagorean theorem",source:"a^2+b^2=c^2"},
    {id:"integral",label:"Definite integral",source:"\\int_0^1 x^2\\,dx=\\frac{1}{3}"},
    {id:"sum",label:"Finite sum",source:"\\sum_{k=1}^{n}k=\\frac{n(n+1)}{2}"},
    {id:"binomial",label:"Binomial theorem",source:"(a+b)^n=\\sum_{k=0}^n\\binom{n}{k}a^{n-k}b^k"},
    {id:"euler",label:"Euler's identity",source:"e^{i\\pi}+1=0"}]
pub let structures = [
    {id:"fraction",label:"Fraction",glyph:"a⁄b",entries:[
        {id:"stacked",label:"Stacked fraction",template:"\\frac{#}{b}",body:"a"},
        {id:"inline",label:"Small fraction",template:"\\tfrac{#}{b}",body:"a"},
        {id:"binomial",label:"Binomial coefficient",template:"\\binom{#}{k}",body:"n"}]},
    {id:"script",label:"Script",glyph:"xⁿ",entries:[
        {id:"superscript",label:"Superscript",template:"{#}^{2}"},
        {id:"subscript",label:"Subscript",template:"{#}_{i}"},
        {id:"both",label:"Subscript and superscript",template:"{#}_{i}^{2}"}]},
    {id:"radical",label:"Radical",glyph:"√x",entries:[
        {id:"square",label:"Square root",template:"\\sqrt{#}"},
        {id:"cube",label:"Cube root",template:"\\sqrt[3]{#}"},
        {id:"nth",label:"Indexed root",template:"\\sqrt[n]{#}"}]},
    {id:"integral",label:"Integral",glyph:"∫",entries:[
        {id:"plain",label:"Integral",template:"\\int #\\,dx"},
        {id:"definite",label:"Definite integral",template:"\\int_0^1 #\\,dx"},
        {id:"double",label:"Double integral",template:"\\iint #\\,dx\\,dy"},
        {id:"contour",label:"Contour integral",template:"\\oint #\\,dz"}]},
    {id:"large",label:"Large Operator",glyph:"∑",entries:[
        {id:"sum",label:"Summation",template:"\\sum_{i=1}^{n} #"},
        {id:"product",label:"Product",template:"\\prod_{i=1}^{n} #"},
        {id:"union",label:"Union",template:"\\bigcup_{i=1}^{n} #"},
        {id:"intersection",label:"Intersection",template:"\\bigcap_{i=1}^{n} #"}]},
    {id:"bracket",label:"Bracket",glyph:"{x}",entries:[
        {id:"parentheses",label:"Parentheses",template:"\\left(#\\right)"},
        {id:"square",label:"Square brackets",template:"\\left[#\\right]"},
        {id:"braces",label:"Braces",template:"\\left\\{#\\right\\}"},
        {id:"absolute",label:"Absolute value",template:"\\left|#\\right|"}]},
    {id:"function",label:"Function",glyph:"sin",entries:[
        {id:"sin",label:"Sine",template:"\\sin\\left(#\\right)"},
        {id:"cos",label:"Cosine",template:"\\cos\\left(#\\right)"},
        {id:"tan",label:"Tangent",template:"\\tan\\left(#\\right)"},
        {id:"log",label:"Logarithm",template:"\\log_{b}\\left(#\\right)"},
        {id:"ln",label:"Natural logarithm",template:"\\ln\\left(#\\right)"}]},
    {id:"accent",label:"Accent",glyph:"x̂",entries:[
        {id:"hat",label:"Hat",template:"\\hat{#}"},
        {id:"bar",label:"Overbar",template:"\\overline{#}"},
        {id:"vector",label:"Vector",template:"\\vec{#}"},
        {id:"dot",label:"Dot",template:"\\dot{#}"}]},
    {id:"limit",label:"Limit and Log",glyph:"lim",entries:[
        {id:"limit",label:"Limit",template:"\\lim_{x\\to 0} #"},
        {id:"minimum",label:"Minimum",template:"\\min_{x} #"},
        {id:"maximum",label:"Maximum",template:"\\max_{x} #"}]},
    {id:"operator",label:"Operator",glyph:"≔",entries:[
        {id:"equals",label:"Equality",template:"# = y"},
        {id:"definition",label:"Defined as",template:"# := y"},
        {id:"arrow",label:"Implication",template:"# \\Rightarrow y"},
        {id:"underbrace",label:"Underbrace",template:"\\underbrace{#}_{n}"}]},
    {id:"matrix",label:"Matrix",glyph:"[⋮]",entries:[
        {id:"two",label:"2 × 2 matrix",template:"\\begin{pmatrix}#&b\\\\c&d\\end{pmatrix}",body:"a"},
        {id:"three",label:"3 × 3 matrix",template:"\\begin{bmatrix}#&b&c\\\\d&e&f\\\\g&h&i\\end{bmatrix}",body:"a"},
        {id:"column",label:"Column vector",template:"\\begin{pmatrix}#\\\\y\\\\z\\end{pmatrix}"},
        {id:"cases",label:"Cases",template:"\\begin{cases}#&x\\ge 0\\\\-x&x<0\\end{cases}"}]}]
let symbol_sets = [
    {id:"basic",label:"Basic Math",names:["pm","infty","neq","leq","geq","times","div","approx","equiv","propto","partial","nabla","forall","exists","in","notin","to","Rightarrow","sum","int"]},
    {id:"greek",label:"Greek Letters",names:["alpha","beta","gamma","delta","epsilon","theta","lambda","mu","pi","rho","sigma","phi","omega","Gamma","Delta","Theta","Lambda","Pi","Sigma","Omega"]},
    {id:"relations",label:"Relations & Sets",names:["subset","supset","subseteq","supseteq","cup","cap","emptyset","perp","parallel","sim","simeq","cong","ll","gg","oplus","otimes"]},
    {id:"arrows",label:"Arrows",names:["leftarrow","rightarrow","leftrightarrow","Leftarrow","Rightarrow","Leftrightarrow","mapsto","uparrow","downarrow"]}]
pub let symbol_groups = [for (g in symbol_sets) {*:g,entries:[for (n in g.names)
    {id:n,label:n,glyph:symbols.lookup_symbol(n) or n,token:"\\" ++ n ++ " "}]}]
pub fn structure(id) => [for (s in structures where s.id==id) s][0]
pub fn entry(category,id) => [for (e in structure(category).entries where e.id==id) e][0]
fn literal(text) {
    let escapes = map(["\\","\\textbackslash{}","{","\\{","}","\\}","#","\\#","$","\\$","%","\\%","&","\\&","_","\\_","^","\\textasciicircum{}","~","\\textasciitilde{}"]);
    join([for (c in split(text,"")) escapes[c] or c],"")
}

pub fn insert(source,start,end,entry) {
    let lo = max(0,min(len(source),start))
    let hi = max(lo,min(len(source),end))
    let picked = slice(source,lo,hi)
    let parts = split(entry.template or "","#")
    let body = if (entry.literal==true) literal(if (picked=="") entry.body or "text" else picked)
        else if (picked=="") entry.body or "x" else picked
    let fragment = entry.token or (parts[0] ++ body ++ parts[1])
    let at = lo+(if (entry.token!=null) len(fragment) else len(parts[0]));
    {source:slice(source,0,lo) ++ fragment ++ slice(source,hi),start:at,end:at+(if (entry.token!=null) 0 else len(body))}
}
