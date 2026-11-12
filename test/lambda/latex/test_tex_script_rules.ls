import profile: lambda.doc.latex.tex_engine.profile
import utf8: lambda.io.utf8
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn render(t) => if(t.kind=="control") "\\" ++ t.value else t.value
fn render_tokens(tokens,source,index=0) any^ {
 if(index>=len(tokens)) "" else {
  let t=tokens[index];
  if(t.echo_span!=null) utf8.decode(slice(binary(source)^,t.echo_span.start,t.echo_span.end)^)^ ++ render_tokens(tokens,source,index+t.echo_count)^
  else render(t) ++ render_tokens(tokens,source,index+1)^
 }
}
fn check(source) any^ {
 let x=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let text=render_tokens(x.output,source)^;
 {issue:x.issue,matches:text==n.text}
}
[for(source in [
    "\\begin{document}A\\hrule!B\\end{document}",
    "\\begin{document}A\\vrule!B\\end{document}",
    "\\begin{document}A\\hrule width1pt height2pt depth3pt!B\\end{document}",
    "\\def\\r{\\vrule}\\begin{document}A\\r!B\\end{document}",
    "\\begin{document}A\\vrule depth2cm width3cm height4cm!B\\end{document}",
    "\\begin{document}A\\hrule height1cm height2cm!B\\end{document}",
    "\\def\\w{2pt}\\begin{document}A\\hrule width\\w!B\\end{document}",
    "\\begin{document}é\\hrule width 2cm!你\\end{document}",
    "\\begin{document}A\\vrule width-1pt height-2pt depth3pt!B\\end{document}",
    "\\begin{document}A\\hrule width1cm!B\\end{document}",
    "\\begin{document}A\\hrule why!B\\end{document}",
    "\\begin{document}A\\hrule  width 1cm!B\\end{document}",
    "\\begin{document}A\\hrule%note\n width1cm!B\\end{document}",
    "\\let\\r=\\vrule\\begin{document}A\\r width2cm!B\\end{document}",
    "\\dimen0=2pt\\begin{document}A\\hrule height\\dimen0!B\\end{document}",
    "\\begin{document}A\\vrule width\\dimexpr1pt+2pt\\relax!B\\end{document}",
    "\\def\\r{\\hrule width2pt height1pt}\\begin{document}A\\r!B\\end{document}",
    "\\begin{document}A\\hrule\\end{document}"
]) check(source)^];
let long_source="\\begin{document}A\\hrule width0." ++ join([for(i in 1 to 80) "0"],"") ++ "cm!B\\end{document}";
check(long_source)^
