import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source) any^ {
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^;
 let native=parse(source,{type:"tex"})^;
 {supported:ex.issue==null,text:reconstruct.reconstruct(ex)^.text==native.text,
  messages:ex.messages==native.messages,diagnostics:ex.diagnostic_records==native.diagnostics}
}
let names=["l_tmpa_seq","l_tmpb_seq","g_tmpa_seq","g_tmpb_seq"];
let commands=["\\show\\", "\\meaning\\", "\\seq_count:N\\", "\\seq_use:Nn\\"];
let base=[for(name in names,command in commands)
 "\\ExplSyntaxOn" ++ command ++ name ++ (if(command=="\\seq_use:Nn\\") "{|}" else "") ++ "\\ExplSyntaxOff"];
let extra=[
 "\\ExplSyntaxOn\\ifx\\l_tmpa_seq\\c_empty_seq Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\seq_if_empty:NTF\\l_tmpa_seq{Y}{N}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\seq_put_right:Nn\\l_tmpa_seq{A}\\seq_use:Nn\\l_tmpa_seq{|}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\seq_put_right:Nn\\l_tmpa_seq{A}}[\\seq_count:N\\l_tmpa_seq]\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\seq_gput_right:Nn\\g_tmpa_seq{A}}[\\seq_count:N\\g_tmpa_seq]\\ExplSyntaxOff"
];
let results=[for(source in [*base,*extra]) check(source)^];
let duplicate="\\ExplSyntaxOn\\seq_new:N\\l_tmpa_seq\\ExplSyntaxOff";
let dup=profile.expand_with_kernel(duplicate,kernel,options,100000)^;
let native_dup=parse(duplicate,{type:"tex"})^;
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics]),
 duplicate_guard:dup.issue=="expl3-already-defined" and any([for(d in native_dup.diagnostics) d.code=="tex-errmessage"])};
fn ast_check(source,mode) any^ {
 let ex=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000)^;
 let result=digest.digest(ex)^; let tex=parse(source,{type:"tex"})^; let native=parse(source,{type:"latex"})^;
 {supported:result.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  diagnostics:ex.diagnostic_records==tex.diagnostics,tree:result.ast==native}
}
let sources=[
 "\\ExplSyntaxOn\\begin{document}\\seq_count:N\\l_tmpa_seq\\end{document}",
 "\\ExplSyntaxOn\\begin{document}A\\seq_use:Nn\\l_tmpa_seq{|}B\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\seq_count:N\\l_tmpa_seq}\\end{document}"
];
let trees=[for(source in sources,mode in [false,true]) ast_check(source,mode)^];
let guarded_sources=[
 "\\ExplSyntaxOn\\seq_put_right:Nn\\l_tmpa_seq{A}\\begin{document}\\seq_use:Nn\\l_tmpa_seq{|}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\seq_put_right:Nn\\l_tmpa_seq{A}\\seq_use:Nn\\l_tmpa_seq{|}\\end{document}"
];
let guards=[for(source in guarded_sources,mode in [false,true]) {
 let ex=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000)^;
 let result=digest.digest(ex)^;
 result.issue=="latex-digest-unsupported" and result.unsupported=="expl3 sequence macro source mapping"
}];
{cases:len(trees),supported:all([for(r in trees) r.supported]),text:all([for(r in trees) r.text]),
 diagnostics:all([for(r in trees) r.diagnostics]),tree:all([for(r in trees) r.tree]),guards:all(guards)};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-scratch-seq-main.tex";
let child="script-scratch-seq-child.tex";
let ex=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,script_expl3:true,constructor_argument_mode:true,echo_definitions:true,adapters:[],
  resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let result=digest.digest(ex)^; let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:result.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
 messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:result.ast==native}
