// D10.2.2v2: generated headers use verified reconstructed byte positions.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,constructor_argument_mode:true,resources:{local:{},bundled:{}}};
let preload=profile.preload_kernel(kernel,options,100000)^;
// Expected trees were captured from the retired native TeX and LaTeX engines.
let sources=["\\def\\maketable{\\begin{tabular}{|l|l|}a&b\\\\c&d\\end{tabular}}\\begin{document}\\maketable\\end{document}","\\def\\columns{{|c|}}\\begin{document}\\begin{tabular}\\columns X\\end{tabular}\\end{document}","\\def\\maketable{\\begin{tabularx}{3cm}{p{2cm}}X\\end{tabularx}}\\begin{document}\\maketable\\end{document}","\\def\\maketable{\\begin{tabular} {l}X\\end{tabular}}\\begin{document}\\maketable\\end{document}","\\begin{document}\\def\\col{{cc}}\\begin{tabular}\\col A&B\\end{tabular}\\end{document}"];
let expected=[<latex_document <paragraph <def source_offset: 0, argument_groups: [{ kind: "required", raw: "\\maketable", start: 4, end: 14}, { kind: "required", raw: "\\begin{tabular}{|l|l|}a&b\\\\c&d\\end{tabular}", start: 14, end: 59}], "\\maketable" <curly_group <tabular source_offset: 15, columns: "|l|l|", <paragraph "a" 'alignment_tab' "b" 'row_sep' "c" 'alignment_tab' "d">>>>> <document source_offset: 59, <tabular source_offset: 75, columns: "|l|l|", <paragraph "a" 'alignment_tab' "b" 'row_sep' "c" 'alignment_tab' "d">>>>,
<latex_document <paragraph <def source_offset: 0, argument_groups: [{ kind: "required", raw: "\\columns", start: 4, end: 12}, { kind: "required", raw: "{|c|}", start: 12, end: 19}], "\\columns" <curly_group <curly_group "|c|">>>> <document source_offset: 19, <tabular source_offset: 35, columns: "|c|", <paragraph "X">>>>,
<latex_document <paragraph <def source_offset: 0, argument_groups: [{ kind: "required", raw: "\\maketable", start: 4, end: 14}, { kind: "required", raw: "\\begin{tabularx}{3cm}{p{2cm}}X\\end{tabularx}", start: 14, end: 60}], "\\maketable" <curly_group <tabularx source_offset: 15, width: "3cm", columns: "p{2cm}", <paragraph "X">>>>> <document source_offset: 60, <tabularx source_offset: 76, width: "3cm", columns: "p{2cm}", <paragraph "X">>>>,
<latex_document <paragraph <def source_offset: 0, argument_groups: [{ kind: "required", raw: "\\maketable", start: 4, end: 14}, { kind: "required", raw: "\\begin{tabular} {l}X\\end{tabular}", start: 14, end: 49}], "\\maketable" <curly_group <tabular source_offset: 15, <paragraph " " <curly_group "l"> "X">>>>> <document source_offset: 49, <tabular source_offset: 65, <paragraph " " <curly_group "l"> "X">>>>,
<latex_document <document source_offset: 0, <paragraph <def source_offset: 16, argument_groups: [{ kind: "required", raw: "\\col", start: 20, end: 24}, { kind: "required", raw: "{cc}", start: 24, end: 30}], "\\col" <curly_group <curly_group "cc">>>> <tabular source_offset: 30, columns: "cc", <paragraph "A" 'alignment_tab' "B">>>>];
[for(i,source in sources) { let r=profile.expand_with_kernel(source,kernel,options,100000,preload)^; let d=digest.digest(r)^; {issue:d.issue,match:d.ast==expected[i]} }];
let unsafe="\\def\\maketable{\\begin{tabular}{>{\\bfseries}l}X\\end{tabular}}\\begin{document}\\maketable\\end{document}";
let result=profile.expand_with_kernel(unsafe,kernel,options,100000,preload)^;
let tree=digest.digest(result)^;
{issue:tree.issue,unsupported:tree.unsupported}
