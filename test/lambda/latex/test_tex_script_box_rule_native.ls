import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}};
let preload=profile.preload_kernel(kernel,options,100000)^;
let legacy={*:options,constructor_argument_mode:false};
let legacy_preload=profile.preload_kernel(kernel,legacy,100000)^;
let sources=[
 "\\begin{document}\\setbox0=\\hbox{\\hrule width1pt height2pt}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{\\hrule width1pt height2pt}\\box0\\end{document}",
 "\\begin{document}\\setbox0=\\vbox{A\\hrule width1pt height2pt B}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{\\vrule width2pt}\\copy0\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{A\\vrule width1pt B}\\copy0 Z\\end{document}",
 "\\begin{document}\\setbox0=\\vbox{é\\hrule height2pt depth1pt Ω}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{\\hrule width1pt}\\copy0/\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\vbox{\\vrule width2.5pt height1pt depth1pt}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{\\hrule }\\copy0 Z\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{\\vrule }\\copy0 Z\\end{document}",
 "\\begin{document}\\setbox0=\\vbox{\\hrule }\\copy0 Z\\end{document}",
 "\\begin{document}\\setbox0=\\vbox{\\vrule }\\copy0 Z\\end{document}"
];
fn direct(source, mode) any^ => digest.digest(profile.expand_with_kernel(source,kernel,
    if(mode) options else legacy,100000,if(mode) preload else legacy_preload)^)
// Native trees retain the endpoint of each replayed rule's last argument.
let trees=[for(source in sources) (direct(source,true)^).ast];
trees;
[for(i in 0 to len(sources) - 1) trees[i] == (parse(sources[i],{type:"latex"})^) and
    trees[i] == (direct(sources[i],false)^).ast]
