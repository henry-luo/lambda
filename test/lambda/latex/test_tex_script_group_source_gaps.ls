import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let gaps=[{pre:"",post:""},{pre:" ",post:"  "},{pre:"\n\t",post:"\n\t"},
 {pre:"%comment\n\t",post:"%comment\n\t"},{pre:"\n\n",post:"\n\n"},
 {pre:"\r\n ",post:"\r\n "},{pre:"\n%é\n\t",post:"\n%é\n\t"}];
let shapes=["generic","optional","format","definition","default"];
fn check(shape,g,mode) any^ {
 let body=g.pre ++ "A" ++ g.post;
 let source=if(shape=="generic") "\\begin{document}\\foo{" ++ body ++ "}\\end{document}"
    else if(shape=="optional") "\\begin{document}\\foo[" ++ body ++ "]\\end{document}"
    else if(shape=="format") "\\begin{document}\\textbf{" ++ body ++ "}\\end{document}"
    else if(shape=="definition") "\\newcommand{\\q}{" ++ body ++ "}\\begin{document}X\\end{document}"
    else "\\newcommand{\\q}[1][" ++ body ++ "]{#1}\\begin{document}X\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:mode,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;let t=parse(source,{type:"tex"})^;
 let guarded=shape=="default" and (contains(g.pre,"%") or contains(g.post,"%"));
 {ok:if(guarded)d.issue=="latex-digest-unsupported" and d.unsupported=="definition option comment source mapping"
    else d.issue==null and d.ast==n, text:reconstruct.reconstruct(r)^.text==t.text,
    diagnostics:r.diagnostic_records==t.diagnostics}
}
let results=[for(shape in shapes,g in gaps,mode in [false,true])check(shape,g,mode)^];
{cases:len(results),ok:all([for(r in results)r.ok]),text:all([for(r in results)r.text]),diagnostics:all([for(r in results)r.diagnostics])}
