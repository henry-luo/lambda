import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn render(t) => if(t.kind=="control") "\\" ++ t.value else t.value
fn check(source) any^ {
 let x=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let text=join([for(t in x.output) render(t)],"") ++ (if(x.trailing_source == null) "" else x.trailing_source.text);
 {issue:x.issue,matches:text==n.text,messages:x.messages}
}
[for(source in [
    "\\begin{document}A\\insert0{hidden}B\\end{document}",
    "\\begin{document}A\\vadjust{hidden}B\\end{document}",
    "\\count0=0\\begin{document}A\\insert0{\\global\\count0=3 hidden}\\the\\count0 B\\end{document}",
    "\\count0=1\\begin{document}A\\insert0{\\count0=3 hidden}\\the\\count0 B\\end{document}",
    "\\begin{document}A\\vadjust{X{Y}\\insert1{Z}Q}B\\end{document}",
    "\\begin{document}A\\insert0{\\aftergroup Z hidden}B\\end{document}",
    "\\begin{document}A\\vadjust{\\message{inside}hidden}B\\end{document}",
    "\\begin{document}A\\insert\\numexpr2+3\\relax{hidden}B\\end{document}",
    "\\let\\v=\\vadjust\\begin{document}A\\v{hidden}B\\end{document}",
    "\\begin{document}A\\vadjust\\relax{hidden}B\\end{document}",
    "\\let\\beg={\\begin{document}A\\vadjust\\beg hidden}B\\end{document}",
    "\\begin{document}A\\vadjust{\\def\\x{hidden}\\x}B\\end{document}",
    "\\begin{document}A\\vadjust{\\gdef\\x{ok}}\\x B\\end{document}",
    "\\begin{document}A\\vadjust{X\\end{document}}tail",
    "\\count0=0\\def\\inc{\\global\\advance\\count0 by1}\\begin{document}A\\vadjust{\\aftergroup\\inc X}\\the\\count0 B\\end{document}",
    "\\count0=0\\begin{document}A\\vadjust{\\insert2{\\global\\count0=4}X}\\the\\count0 B\\end{document}",
    "\\begin{document}$a\\vadjust{hidden}b$\\end{document}",
    "\\begin{document}A\\vadjust{X \\unskip Y}B\\end{document}",
    "\\count0=1\\begin{document}A\\vadjust{\\begingroup\\global\\count0=7\\endgroup X}\\the\\count0 B\\end{document}",
    "\\def\\hide#1{\\vadjust{#1}}\\begin{document}A\\hide{hide}B\\end{document}",
    "\\begin{document}é\\vadjust{藏}你\\end{document}",
    "\\begin{document}A\\vadjust{\\begin{quote}hidden\\end{quote}}B\\end{document}",
    "\\count0=2\\begin{document}A\\insert\\count0{hidden}B\\end{document}",
    "\\begin{document}A\\vadjust{hidden\\par more}B\\end{document}"
]) check(source)^]
