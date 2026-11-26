// D10.2.2v2: complete document trees were captured from the retired engine.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}};
let preload=profile.preload_kernel(kernel,options,100000)^;
let cases=[
 {source:"\\documentclass{article}\\begin{document}\\begin{tabular}{ll}\\catcode38=12\\relax A&B\\catcode38=4\\relax &C\\end{tabular}\\end{document}",expected:<latex_document <documentclass source_offset: 0, argument_groups: [{ kind: "required", raw: "article", start: 14, end: 23}], "article"> <document source_offset: 23, <tabular source_offset: 39, columns: "ll", <paragraph "A&B" 'alignment_tab' "C">>>>},
 {source:"\\documentclass{article}\\begin{document}\\begin{tabular}{ll}\\catcode38=11\\relax A&B\\catcode38=4\\relax &C\\end{tabular}\\end{document}",expected:<latex_document <documentclass source_offset: 0, argument_groups: [{ kind: "required", raw: "article", start: 14, end: 23}], "article"> <document source_offset: 23, <tabular source_offset: 39, columns: "ll", <paragraph "A&B" 'alignment_tab' "C">>>>},
 {source:"\\documentclass{article}\\begin{document}\\begin{tabular}{ll}A&B\\end{tabular}\\end{document}",expected:<latex_document <documentclass source_offset: 0, argument_groups: [{ kind: "required", raw: "article", start: 14, end: 23}], "article"> <document source_offset: 23, <tabular source_offset: 39, columns: "ll", <paragraph "A" 'alignment_tab' "B">>>>},
 {source:"\\documentclass{article}\\begin{document}\\begin{tabular}{ll}A\\&B&C\\end{tabular}\\end{document}",expected:<latex_document <documentclass source_offset: 0, argument_groups: [{ kind: "required", raw: "article", start: 14, end: 23}], "article"> <document source_offset: 23, <tabular source_offset: 39, columns: "ll", <paragraph "A&B" 'alignment_tab' "C">>>>}
];
[for(c in cases) (let r=profile.expand_with_kernel(c.source,kernel,options,100000,preload)^,
 let tree=digest.digest(r)^, let public=parse(c.source,{type:"latex"}) ^ {null},
 {issue:r.issue,tree_issue:tree.issue,unsupported:tree.unsupported,match:tree.ast==c.expected,public_match:public==c.expected})]
