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
    "\\begin{document}A\\leaders\\hrule\\hskip2cm!B\\end{document}",
    "\\begin{document}A\\cleaders\\vrule width1pt\\hskip2pt!B\\end{document}",
    "\\begin{document}A\\xleaders\\hrule height2pt\\hskip3pt!B\\end{document}",
    "\\begin{document}A\\leaders\\hrule!B\\end{document}",
    "\\begin{document}A\\leaders\\relax\\vrule\\hskip1cm!B\\end{document}",
    "\\let\\r=\\hrule\\begin{document}A\\leaders\\r\\hskip1cm!B\\end{document}",
    "\\def\\r{\\hrule height1pt}\\begin{document}A\\leaders\\r\\hskip1cm!B\\end{document}",
    "\\begin{document}A\\leaders\\hrule\\hfill!B\\end{document}",
    "\\begin{document}é\\leaders\\vrule width1cm\\hskip2cm!你\\end{document}",
    "\\begin{document}$a\\leaders\\hrule\\hskip2pt!b$\\end{document}",
    "\\begin{document}A\\vadjust{\\leaders\\hrule\\hskip2pt}B\\end{document}",
    "\\begin{document}A\\leaders\\hbox{x}\\hskip2cm!B\\end{document}",
    "\\begin{document}A\\cleaders\\vbox{x}\\hskip2pt!B\\end{document}",
    "\\begin{document}A\\xleaders\\vtop{x}\\hskip2pt!B\\end{document}",
    "\\count0=0\\begin{document}A\\leaders\\hbox{X\\global\\count0=3}\\hskip2pt!\\the\\count0 B\\end{document}",
    "\\count0=1\\begin{document}A\\leaders\\hbox{\\count0=3 X}\\hskip2pt!\\the\\count0 B\\end{document}",
    "\\begin{document}A\\leaders\\hbox to 2cm{x}\\hskip1cm!B\\end{document}",
    "\\begin{document}A\\leaders\\vbox spread 2pt{x}\\hskip1cm!B\\end{document}",
    "\\begin{document}A\\leaders\\hbox{X\\aftergroup Z}\\hskip1cm!B\\end{document}",
    "\\begin{document}A\\leaders\\hbox{\\message{box}X}\\hskip1cm!B\\end{document}",
    "\\begin{document}A\\leaders\\hbox{X\\vadjust{Y}Z}\\hskip1cm!B\\end{document}",
    "\\count0=0\\everyhbox{\\global\\advance\\count0 by1}\\begin{document}A\\leaders\\hbox{X}\\hskip1cm!\\the\\count0 B\\end{document}",
    "\\count0=0\\everyvbox{\\global\\advance\\count0 by1}\\begin{document}A\\leaders\\vtop{X}\\hskip1cm!\\the\\count0 B\\end{document}",
    "\\let\\b=\\hbox\\begin{document}A\\leaders\\b{x}\\hskip1cm!B\\end{document}"
]) check(source)^]
