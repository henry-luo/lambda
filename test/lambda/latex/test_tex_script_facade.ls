import facade: lambda.doc.latex.tex_engine.facade

let options={adapters:[], chaptered:[], resources:{local:{},bundled:{}}};
let tex_sources=["Hello", "\\def\\x{World}Hello \\x", "\\message{hello}Text", "\\iftrue A\\else B\\fi"];
let latex_sources=["\\begin{document}Hello\\end{document}",
 "\\begin{document}A \\textbf{bold} B\\end{document}",
 "\\begin{document}$x+1$\\end{document}",
 "\\def\\x{Hi}\\begin{document}\\x\\end{document}"];
let tex=[for(source in tex_sources) {source:source, actual:facade.tex(source,options,100000)^,
    native:parse(source,{type:"tex"})^}];
let latex=[for(source in latex_sources) {source:source, actual:facade.latex(source,options,100000)^,
    native:parse(source,{type:"latex"})^}];
let unsupported=[facade.tex("\\halign",options,100000)^,facade.latex("\\halign",options,100000)^];
{cases:len(tex)+len(latex)+len(unsupported),
 failures:[*[for(item in tex where item.actual.issue!=null or item.actual.root!=item.native) item],
    *[for(item in latex where item.actual.issue!=null or item.actual.root!=item.native) item],
    *[for(item in unsupported where item.root!=null or item.issue!="tex-unsupported-primitive" or item.unsupported!="halign") item]]}
