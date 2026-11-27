import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import compare: ._tex_script_recovery
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
// Keep native references independent of the migrated public parser.
let native=input("test/lambda/latex/fixtures/tex_expl3_nx_meaning_native.json","json")^;
let cached=compare.preload_expl3({constructor_argument_mode:true})^;
fn check(source,index) any^ {
 let r=compare.check_expansion(source,{script_expl3:true,constructor_argument_mode:true},
     native.cases[index],if(index==0)null else cached)^;
 {supported:r.issue==null,text:r.text_match,messages:r.messages_match,diagnostics:r.diagnostics_match}
}
let names=["tl_set:Nx","tl_gset:Nx","tl_put_left:Nx","tl_put_right:Nx","tl_gput_left:Nx","tl_gput_right:Nx"];
let sources=[for(name in names,body in ["\\A","\\noexpand\\A","\\unexpanded{\\A}"])
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{O}\\def\\A{X}{\\" ++ name ++ "\\l_test_tl{" ++ body ++ "}}\\def\\A{Y}[\\tl_use:N\\l_test_tl]\\ExplSyntaxOff"];
let extra=[
 "\\ExplSyntaxOn\\protected\\long\\def\\f{\\exp_args:NNx\\tl_set:Nn}\\ifx\\f\\tl_set:Nx Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\let\\f\\tl_set:Nx\\ifx\\f\\tl_set:Nx Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\let\\f\\tl_set:Nx\\def\\A{X}\\f\\l_test_tl{\\A}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\protected\\def\\A{X}\\tl_set:Nx\\l_test_tl{\\A}\\def\\A{Y}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\use:x{A\\int_eval:n{1+2}B}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\A{X}\\use:x{\\exp_not:N\\A}\\def\\A{Y}\\ExplSyntaxOff"
];
let results=[for(index,source in [*sources,*extra]) check(source,index)^];
let shown=[for(index,name in [*names,"use:x","exp_args:NNx"])
 check("\\ExplSyntaxOn\\show\\" ++ name ++ "\\ExplSyntaxOff",len(results)+index)^];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics]),
 shown:all([for(r in shown) r.supported and r.text and r.messages and r.diagnostics])}
