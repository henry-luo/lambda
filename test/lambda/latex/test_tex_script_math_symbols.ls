import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(body) any^ {
    let source = "\\begin{document}$" ++ body ++ "$\\end{document}";
    let expanded = profile.expand_with_kernel(source, kernel,
        {constructor_argument_mode: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"}) ^ { null };
    {body: body, expansion_issue: expanded.issue, issue: result.issue, unsupported: result.unsupported,
        native_parsed: native != null, matches: if (result.ast == null or native == null) null else result.ast == native,
        actual: if (result.ast == null or native == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or native == null or result.ast == native) null else native}
}
[for (body in [
    "\\downarrow", "\\vert", "\\Vert", "\\|", "\\uparrow+\\downarrow+\\updownarrow+\\Uparrow+\\Downarrow+\\Updownarrow",
    "\\lvert+\\rvert+\\lVert+\\rVert+\\langle+\\rangle", "\\lbrace+\\rbrace+\\lceil+\\rceil+\\lfloor+\\rfloor",
    "\\backslash+\\arrowvert+\\Arrowvert+\\bracevert+\\lmoustache+\\rmoustache", "\\lgroup+\\rgroup+\\lbrack+\\rbrack+\\lparen+\\rparen",
    "\\vert{x}", "\\downarrow[a]{x}{y}", "\\downarrow {x} [y]z", "\\downarrow^2+\\vert_i",
    "\\rightarrow+\\leftarrow+\\leftrightarrow+\\mapsto+\\to", "\\notin+\\forall+\\exists+\\subset+\\supset+\\propto",
    "\\circ+\\ast+\\star+\\bullet+\\oplus+\\otimes+\\odot+\\oslash", "\\wedge+\\vee+\\cap+\\cup+\\sqcap+\\sqcup",
    "\\triangleleft+\\triangleright+\\wr+\\bigcirc+\\diamond+\\bigtriangleup+\\bigtriangledown",
    "\\boxplus+\\boxminus+\\boxtimes+\\boxdot+\\square+\\blacksquare", "\\parallel+\\perp+\\angle+\\triangle+\\cong+\\sim+\\simeq",
    "\\prec+\\succ+\\preceq+\\succeq+\\ll+\\gg+\\asymp+\\bowtie", "\\models+\\vdash+\\dashv+\\top+\\bot+\\neg+\\lnot",
    "\\cot+\\sec+\\csc+\\arcsin+\\arccos+\\arctan+\\sinh+\\cosh+\\tanh", "\\arsinh+\\arcosh+\\artanh+\\sech+\\csch+\\coth",
    "\\lg+\\max+\\min+\\arg+\\det+\\gcd+\\lcm+\\deg+\\dim+\\ker+\\hom+\\limsup+\\liminf+\\sup+\\inf",
    "\\intop+\\oiint+\\oiiint", "\\downarrow[\\alpha+{x}]", "\\downarrow[]{}", "\\downarrow(x)",
    "\\downarrow[1[2]3]{a_{i}}", "\\vert {\\frac{a}{b}}", "\\downarrow% comment\n{x}",
    "\\left\\vert x\\right\\vert", "\\left\\langle x\\middle\\vert y\\right\\rangle",
    "\\rightarrow^{\\downarrow}+\\downarrow_\\alpha", "\\arcsin{x}+\\max_{a}{b}"
]) check(body)^]
