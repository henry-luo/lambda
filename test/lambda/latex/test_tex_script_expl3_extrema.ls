import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
import compare: ._tex_script_fp_compare
let kernel = input("lmd/package/doc/latex/tex_engine/kernel.tex", "text")^;
let expressions = ["min(1,2)", "min(-1,-2)", "min(4)", "min(4,3,2,1)",
    "max(1,2)", "max(-1,-2)", "max(4)", "max(1,2,3,4,5)",
    "min(0,-0)", "min(-0,0)", "max(0,-0)", "max(-0,0)",
    "min(3,max(1,2),-4)", "max(1+2,2*3,-7)", "sign(-3)",
    "sign(0)", "sign(-0)", "sign(0.0001)", "sign(-0.0001)"];
let results = [for(expr in expressions) compare.check(expr)^];
{cases:len(results),failures:[for(result in results where result.issue!=null or not result.text_match or not result.messages_match or not result.diagnostics_match) result]}
let sources = ["\\ExplSyntaxOn\\begin{document}\\fp_eval:n{min(3,1,2)}\\ExplSyntaxOff\\end{document}",
    "\\ExplSyntaxOn\\begin{document}\\textbf{\\fp_eval:n{sign(-4)}}\\ExplSyntaxOff\\end{document}"];
fn check_tree(source) any^ {
    let result = profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:true,
        echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let tree = digest.digest(result)^;
    let native = parse(source,{type:"tex"})^;
    {source:source,issue:tree.issue,unsupported:tree.unsupported,
     text_match:reconstruct.reconstruct(result)^.text==native.text,
     diagnostics_match:result.diagnostic_records==native.diagnostics,
     ast_match:if(tree.ast==null)null else tree.ast==parse(source,{type:"latex"})^}
}
let trees = [for(source in sources) check_tree(source)^];
{cases:len(trees),failures:[for(tree in trees where tree.issue!=null or not tree.text_match or not tree.diagnostics_match or not tree.ast_match) tree]}
