import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source) any^ {
 let doc="\\begin{document}" ++ source ++ "\\end{document}";
 let r=profile.expand_with_kernel(doc,kernel,{echo_definitions:true, resources:{local:{},bundled:{}}},100000)^;
 let text=join([for(t in r.output) if(t.kind == "control") "\\" ++ t.value else t.value],"");
 let native=parse(doc,{type:"tex"})^;
 {text:text,issue:r.issue,unsupported:r.unsupported,matches_native:if(r.issue != null) null else text == native.text}
}
"new/set/step:"; report("\\newcounter{n}\\setcounter{n}{3}\\stepcounter{n}\\arabic{n}/\\then")
"group global:"; report("\\newcounter{n}{\\setcounter{n}{3}}\\arabic{n}")
"child reset:"; report("\\newcounter{p}\\newcounter{c}[p]\\setcounter{c}{8}\\stepcounter{p}\\arabic{c}")
"nested reset:"; report("\\newcounter{p}\\newcounter{c}[p]\\newcounter{d}[c]\\setcounter{d}{8}\\stepcounter{p}\\arabic{d}")
"mixed format:"; report("\\newcounter{n}\\setcounter{n}{4}\\arabic{n}/\\roman{n}/\\Roman{n}/\\alph{n}/\\Alph{n}/\\fnsymbol{n}")
"counter expression:"; report("\\newcounter{a}\\newcounter{b}\\setcounter{a}{9}\\setcounter{b}{\\numexpr\\value{a}+2\\relax}\\arabic{b}")
"add and refstep:"; report("\\newcounter{n}\\setcounter{n}{-3}\\addtocounter{n}{5}\\refstepcounter{n}\\arabic{n}")
"unknown counter:"; report("\\setcounter{missing}{2}\\arabic{missing}")
"existing c@ register:"; report("\\makeatletter\\countdef\\c@foo=90 \\count90=7 \\makeatother\\arabic{foo}")
"defined the macro:"; report("\\newcounter{x}\\setcounter{x}{12}\\thex")
"negative globaldefs:"; report("\\newcounter{n}\\globaldefs=-1{\\setcounter{n}{4}}\\globaldefs=0\\arabic{n}")
"duplicate declaration:"; report("\\newcounter{n}\\newcounter{n}\\arabic{n}")
"expanded counter name:"; report("\\def\\cn{nn}\\newcounter{\\cn}\\setcounter{\\cn}{4}\\arabic{nn}")
"late declared child:"; report("\\newcounter{c}[p]\\newcounter{p}\\setcounter{c}{7}\\stepcounter{p}\\arabic{c}")
"low level shared register:"; report("\\newcounter{n}\\makeatletter\\global\\c@n=9\\makeatother\\arabic{n}")
"zero formats:"; report("\\newcounter{n}\\roman{n}/\\Roman{n}")
"symbol variants:"; report("\\newcounter{n}\\setcounter{n}{2}\\fnsymbol{n}/\\setcounter{n}{3}\\fnsymbol{n}/\\setcounter{n}{6}\\fnsymbol{n}/\\setcounter{n}{7}\\fnsymbol{n}/\\setcounter{n}{8}\\fnsymbol{n}/\\setcounter{n}{9}\\fnsymbol{n}")
"after newcount:"; report("\\newcount\\mine\\newcounter{n}\\mine=11\\setcounter{n}{3}\\number\\mine/\\arabic{n}")
"implicit expression, native LI-014:"; report("\\newcounter{n}\\setcounter{n}{\\numexpr1+2}\\arabic{n}")
"value as register:"; report("\\newcounter{a}\\newcounter{b}\\setcounter{a}{7}\\setcounter{b}{\\value{a}}\\arabic{b}")
"expanded parent name:"; report("\\def\\parentname{parent}\\newcounter{parent}\\newcounter{child}[\\parentname]\\setcounter{child}{4}\\stepcounter{parent}\\arabic{child}")
"own format in parent name:"; report("\\newcounter{a}[\\thea]\\arabic{a}")
"symbol without endline:"; report("\\endlinechar=-1\\newcounter{n}\\setcounter{n}{7}\\fnsymbol{n}/\\endlinechar=13")
"nested implicit expressions, native LI-014:"; report("\\newcounter{n}\\setcounter{n}{\\numexpr\\numexpr1+2+3}\\arabic{n}")
"macro step of main counter:"; report("\\newcounter{n}\\def\\advanceit{\\stepcounter{n}}\\advanceit\\arabic{n}")
"macro sets main counter:"; report("\\newcounter{n}\\def\\setit{\\setcounter{n}{8}}\\setit\\arabic{n}")
"macro unknown empty value:"; report("\\def\\setit{\\setcounter{missing}{}}\\setit")
"trailing numeric material:"; report("\\newcounter{n}\\setcounter{n}{1+2}\\arabic{n}")
"duplicate with parent:"; report("\\newcounter{n}\\newcounter{n}[n]\\stepcounter{n}\\arabic{n}")
"counter control sequence defined:"; report("\\newcounter{n}\\ifcsname c@n\\endcsname yes\\else no\\fi")
"counter aliases share register:"; report("\\newcounter{n}\\makeatletter\\let\\savedcount=\\c@n\\global\\savedcount=17\\makeatother\\arabic{n}")
"exhausted count allocation:"; report("\\count20=25\\newcounter{n}\\setcounter{n}{3}\\arabic{n}")

fn report_diagnostics(source) any^ {
 let doc="\\begin{document}" ++ source ++ "\\end{document}";
 let r=profile.expand_with_kernel(doc,kernel,{resources:{local:{},bundled:{}}},100000)^;
 let native=parse(doc,{type:"tex"})^;
 {issue:r.issue,script:r.diagnostics,native:[for(d in native.diagnostics) d.code]}
}
"duplicate:"; report_diagnostics("\\newcounter{n}\\newcounter{n}")
"alpha zero:"; report_diagnostics("\\newcounter{n}\\alph{n}")
"symbol range:"; report_diagnostics("\\newcounter{n}\\setcounter{n}{10}\\fnsymbol{n}")
"numeric trailing:"; report_diagnostics("\\newcounter{n}\\setcounter{n}{1+2}\\arabic{n}")
"exhausted allocation:"; report_diagnostics("\\count20=25\\newcounter{n}\\setcounter{n}{3}\\arabic{n}")
"explicit relax then token:"; report_diagnostics("\\newcounter{n}\\setcounter{n}{1\\relax A}\\arabic{n}")
"expression relax then token:"; report_diagnostics("\\newcounter{n}\\setcounter{n}{\\numexpr1+2\\relax A}\\arabic{n}")

fn report_ast(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 {issue:d.issue,unsupported:d.unsupported,matches_native:if(d.ast == null) null else d.ast == (parse(source,{type:"latex"})^)}
}
"direct counter AST:"; report_ast("\\begin{document}\\newcounter{n}\\setcounter{n}{3}Value \\arabic{n}.\\end{document}")
"parent and step AST:"; report_ast("\\begin{document}\\newcounter{p}\\newcounter{c}[p]\\stepcounter{p}\\arabic{c}\\end{document}")
"commented counter AST:"; report_ast("\\begin{document}\\newcounter%hi\n{n}\\stepcounter{n}\\arabic{n}\\end{document}")
"counter before document AST:"; report_ast("\\newcounter{n}\\begin{document}\\stepcounter{n}\\arabic{n}\\end{document}")
"argument before empty expansion:"; report_ast("\\def\\gone{}\\begin{document}\\mystery{X}\\gone\\end{document}")
"argument before nonempty expansion:"; report_ast("\\def\\word{Y}\\begin{document}\\mystery{X}\\word\\end{document}")
"argument before direct text:"; report_ast("\\begin{document}\\mystery{X} Y\\end{document}")
"empty macro then text:"; report_ast("\\def\\gone{}\\begin{document}\\mystery{A}\\gone B\\end{document}")
"empty macro then punctuation:"; report_ast("\\def\\gone{}\\begin{document}\\mystery{A}\\gone!\\end{document}")
"macro formatting follows:"; report_ast("\\def\\fmt{\\textbf{Y}}\\begin{document}\\mystery{A}\\fmt\\end{document}")
"second argument boundary:"; report_ast("\\def\\word{Y}\\begin{document}\\mystery{A}{B}\\word\\end{document}")
"optional argument boundary:"; report_ast("\\def\\word{Y}\\begin{document}\\mystery[A]{B}\\word\\end{document}")
"parenthesis argument boundary:"; report_ast("\\def\\word{Y}\\begin{document}\\mystery(1,2){B}\\word\\end{document}")
"adjacent direct then empty:"; report_ast("\\def\\gone{}\\begin{document}\\mystery{A}X\\gone Y\\end{document}")
"source whitespace before expansion:"; report_ast("\\def\\word{Y}\\begin{document}\\mystery{A} \\word\\end{document}")
"multiple empty expansions:"; report_ast("\\def\\gone{}\\begin{document}\\mystery{A}\\gone\\gone!\\end{document}")

fn report_diagnostic_ast(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 let n=parse(source,{type:"latex"})^;
 {issue:d.issue,script_diagnostics:r.diagnostics,native_diagnostics:[for(x in n.tex_diagnostics) x.code],
  children_match:if(d.ast == null) null else content(d.ast) == content(n),
  full_ast_match:if(d.ast == null) null else d.ast == n}
}
"relax trailing token, diagnostic metadata pending:"; report_diagnostic_ast("\\begin{document}\\newcounter{n}\\setcounter{n}{1\\relax A}\\arabic{n}\\end{document}")
"expression trailing token, diagnostic metadata pending:"; report_diagnostic_ast("\\begin{document}\\newcounter{n}\\setcounter{n}{\\numexpr1+2\\relax A}\\arabic{n}\\end{document}")

let reference_source="\\newcounter{outer}\\newcounter{inner}[outer]\\begin{document}" ++
 "\\setcounter{outer}{4}\\setcounter{inner}{8}\\immediate\\write16{LCOUNTER-ONE=\\arabic{outer}/\\roman{outer}/\\Roman{outer}/\\alph{outer}/\\Alph{outer}/\\arabic{inner}}" ++
 "\\stepcounter{outer}\\immediate\\write16{LCOUNTER-TWO=\\arabic{outer}/\\arabic{inner}/\\theouter}" ++
 "{\\setcounter{inner}{9}}\\addtocounter{inner}{2}\\refstepcounter{inner}\\immediate\\write16{LCOUNTER-THREE=\\arabic{inner}}\\end{document}";
let reference_result=profile.expand_with_kernel(reference_source,kernel,{resources:{local:{},bundled:{}}},100000)^;
"pdfLaTeX reference:"; {issue:reference_result.issue,unsupported:reference_result.unsupported,messages:reference_result.messages}

let cycle=profile.expand_with_kernel("\\newcounter{n}[n]\\stepcounter{n}",kernel,{resources:{local:{},bundled:{}}},100000)^;
"cyclic reset bounded:"; {issue:cycle.issue}
