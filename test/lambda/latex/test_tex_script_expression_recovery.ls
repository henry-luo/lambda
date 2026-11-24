import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let forms=[*["\\the\\numexpr(1\\relax X","\\the\\numexpr((1)\\relax X",
 "\\the\\numexpr(1X","\\the\\numexpr(1+2]X","\\the\\numexpr(1)\\relax X",
 "\\the\\numexpr((2+3)*4\\relax Z","\\count0=\\numexpr(2\\relax \\the\\count0 X",
 "\\the\\dimexpr(2pt\\relax X","\\the\\glueexpr(2pt plus 1pt\\relax X",
 "\\ExplSyntaxOn\\int_abs:n{-1\\relax}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_min:nn{1\\relax}{2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_max:nn{\\int_abs:n{-1\\relax}\\relax}{2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_round:nn{7}{2\\relax}\\ExplSyntaxOff"], *["\\the\\numexpr(1", "\\the\\numexpr((1", "\\the\\numexpr(1+\\relax X", "\\the\\numexpr(1=2\\relax X", "\\the\\numexpr(1)foo", "\\the\\numexpr(1))X", "\\the\\dimexpr(1pt+2pt]X", "\\the\\glueexpr(1pt plus 2pt)\\relax X", "\\def\\q{\\relax}\\the\\numexpr(2\\q X", "\\the\\numexpr(2147483647+1\\relax X", "\\ExplSyntaxOn\\int_div_round:nn{7}{0}\\ExplSyntaxOff", "\\ExplSyntaxOn\\int_div_round:nn{7}{(2\\relax}\\ExplSyntaxOff"]];
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 {issue:r.issue,text:reconstruct.reconstruct(r)^.text==n.text,diagnostics:r.diagnostic_records==n.diagnostics}
}
let result=[for(source in forms)check(source)^];
{cases:len(result),supported:all([for(r in result)r.issue==null]),text:all([for(r in result)r.text]),
 diagnostics:all([for(r in result)r.diagnostics])}
