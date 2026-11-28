import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let sources=["\\ExplSyntaxOn\\begin{document}A\\number\\l__intarray_loop_int B\\end{document}", "\\ExplSyntaxOn\\begin{document}A{\\number\\l__intarray_loop_int }B\\end{document}", "\\ExplSyntaxOn\\begin{document}A\\textbf{\\number\\l__intarray_loop_int }B\\end{document}", "\\ExplSyntaxOn\\begin{document}A\\section{\\number\\l__intarray_loop_int }B\\end{document}", "\\ExplSyntaxOn\\begin{document}A\\number\\c__fp_leading_shift_int B\\end{document}", "\\ExplSyntaxOn\\begin{document}A{\\number\\c__fp_leading_shift_int }B\\end{document}", "\\ExplSyntaxOn\\begin{document}A\\textbf{\\number\\c__fp_leading_shift_int }B\\end{document}", "\\ExplSyntaxOn\\begin{document}A\\section{\\number\\c__fp_leading_shift_int }B\\end{document}", "\\ExplSyntaxOn\\begin{document}A\\int_new:N\\l_my_int\\number\\l_my_int B\\end{document}", "\\ExplSyntaxOn\\begin{document}A{\\int_new:N\\l_my_int\\number\\l_my_int }B\\end{document}", "\\ExplSyntaxOn\\begin{document}A\\textbf{\\int_new:N\\l_my_int\\number\\l_my_int }B\\end{document}", "\\ExplSyntaxOn\\begin{document}A\\section{\\int_new:N\\l_my_int\\number\\l_my_int }B\\end{document}"];
let references=input("test/lambda/latex/fixtures/tex_format_count_native_trees.json","json")^;
// Native trees were captured with the archived C++ input engine.
[for(opt in [options,{*:options,constructor_argument_mode:true}]) (let preload=profile.preload_expl3_kernel(kernel,opt,20000000)^,
 [for(i in 0 to len(sources)-1) (let d=digest.digest(profile.expand_with_kernel(sources[i],kernel,opt,20000000,preload)^)^,
 {issue:d.issue,unsupported:d.unsupported,matches:if(d.issue == null) parse(format(d.ast,"json")^,{type:"json"})^==references[i] else null})])]
