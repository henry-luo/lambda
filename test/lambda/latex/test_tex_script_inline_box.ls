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
 {issue:x.issue,matches:text==n.text,messages:x.messages}
}
[for(source in [
    "\\begin{document}A\\hbox{Hi}B\\end{document}",
    "\\begin{document}A\\vbox{Hi}B\\end{document}",
    "\\begin{document}A\\vtop{Hi}B\\end{document}",
    "\\begin{document}A\\hbox{}B\\end{document}",
    "\\begin{document}A\\hbox{X\\vbox{Y}Z}B\\end{document}",
    "\\begin{document}A\\hbox to2cm{X}B\\end{document}",
    "\\begin{document}A\\vbox spread2pt{X}B\\end{document}",
    "\\let\\b=\\hbox\\begin{document}A\\b{X}B\\end{document}",
    "\\count0=0\\begin{document}A\\hbox{X\\global\\count0=3}\\the\\count0 B\\end{document}",
    "\\count0=1\\begin{document}A\\vbox{\\count0=3 X}\\the\\count0 B\\end{document}",
    "\\begin{document}A\\hbox{X\\aftergroup Z}B\\end{document}",
    "\\begin{document}A\\hbox{\\message{box}X}B\\end{document}",
    "\\count0=0\\everyhbox{\\global\\advance\\count0 by1}\\begin{document}A\\hbox{X}\\the\\count0 B\\end{document}",
    "\\count0=0\\everyvbox{\\global\\advance\\count0 by1}\\begin{document}A\\vtop{X}\\the\\count0 B\\end{document}",
    "\\begin{document}A\\hbox{X\\leaders\\hrule\\hskip1pt Z}B\\end{document}",
    "\\begin{document}é\\hbox{藏}你\\end{document}",
    "\\begin{document}$a\\hbox{X}b$\\end{document}"
]) check(source)^]
