import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnTF\\l_s_seq{d}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnT\\l_s_seq{b}{Y}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnT\\l_s_seq{d}{Y}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnF\\l_s_seq{b}{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnF\\l_s_seq{d}{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_in:NnTF\\l_s_seq{}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{}\\seq_if_in:NnTF\\l_s_seq{}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,{b,c},d}\\seq_if_in:NnTF\\l_s_seq{b,c}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,{b,c},d}\\seq_if_in:NnTF\\l_s_seq{{b,c}}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\foo{A}\\seq_put_right:Nn\\l_s_seq{\\foo}\\seq_if_in:NnTF\\l_s_seq{\\foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\foo{A}\\seq_put_right:Nn\\l_s_seq{\\foo}\\seq_if_in:NnTF\\l_s_seq{A}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\foo{A}\\seq_put_right:Nn\\l_s_seq{\\foo}\\def\\foo{B}\\seq_if_in:NnTF\\l_s_seq{\\foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{~}\\seq_if_in:NnTF\\l_s_seq{~}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\let\\s\\l_s_seq\\seq_clear:N\\l_s_seq\\seq_if_in:NnTF\\s{b}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}{\\seq_put_right:Nn\\l_s_seq{d}\\seq_if_in:NnTF\\l_s_seq{d}{Y}{N}}\\seq_if_in:NnTF\\l_s_seq{d}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnTF\\l_s_seq{a}{\\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\let\\contains\\seq_if_in:NnTF\\contains\\l_s_seq{b}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\edef\\test{\\seq_if_in:NnTF\\noexpand\\l_s_seq{b}{Y}{N}}\\test\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnTF\\l_s_seq{b}{Y}{\\undefined}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_if_in:NnTF\\l_s_seq{b}{\\int_eval:n{2+3}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\afterassignment X\\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\afterassignment X\\seq_if_in:NnTF\\l_s_seq{d}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\globaldefs=1 \\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}\\globaldefs=0 \\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_reverse:N\\l_s_seq\\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{x}\\catcode`x=12 \\seq_if_in:NnTF\\l_s_seq{x}{Y}{N}\\catcode`x=11 \\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{x}\\catcode`x=12 \\seq_put_right:Nn\\l_s_seq{x}\\seq_if_in:NnTF\\l_s_seq{x}{Y}{N}\\catcode`x=11 \\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\tl_set:Nn\\l__seq_internal_a_tl{BEFORE}\\tl_set:Nn\\l__seq_internal_b_tl{OLD}\\globaldefs=1 \\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}\\globaldefs=0 /\\tl_use:N\\l__seq_internal_a_tl/\\tl_use:N\\l__seq_internal_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\tl_set:Nn\\l__seq_internal_a_tl{BEFORE}\\tl_set:Nn\\l__seq_internal_b_tl{OLD}\\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}/\\tl_use:N\\l__seq_internal_a_tl/\\tl_use:N\\l__seq_internal_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\tl_set:Nn\\l__seq_internal_a_tl{BEFORE}\\tl_set:Nn\\l__seq_internal_b_tl{OLD}\\globaldefs=1 \\seq_if_in:NnTF\\l_s_seq{z}{Y}{N}\\globaldefs=0 /\\tl_use:N\\l__seq_internal_a_tl/\\tl_use:N\\l__seq_internal_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\tl_set:Nn\\l__seq_internal_a_tl{BEFORE}\\tl_set:Nn\\l__seq_internal_b_tl{OLD}\\afterassignment X\\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}/\\tl_use:N\\l__seq_internal_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_if_in:NnTF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_if_in:NnT\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_if_in:NnF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\seq_if_in:NnTF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{α藏}\\seq_if_in:NnTF\\l_s_seq{α藏}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_if_in:NnTF\\l_s_seq{a}{\\seq_if_in:NnTF\\l_s_seq{z}{N}{Y}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_if_in:NnTF\\s{b}{Y}{N}\\ExplSyntaxOff",
"\\ExplSyntaxOn\\long\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_if_in:NnTF\\s{b}{Y}{N}\\ExplSyntaxOff",
"\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\group_begin:\\seq_if_in:NnTF\\l_s_seq{a}{Y}{N}\\group_end:\\ExplSyntaxOff",
"\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\let\\s\\seq_if_in:NnTF\\ifx\\s\\seq_if_in:NnTF Y\\else N\\fi\\ExplSyntaxOff",
"\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_if_in:NnTF\\l_s_seq{a}{\\seq_put_right:Nn\\l_s_seq{b}}{}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
"\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{\\par}\\seq_if_in:NnTF\\l_s_seq{\\par}{Y}{N}\\ExplSyntaxOff",
"\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{{a}{b}}\\seq_if_in:NnTF\\l_s_seq{{a}{b}}{Y}{N}\\ExplSyntaxOff",
"\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_if_in:NnTF\\l_s_seq{a}{Y}{N}\\meaning\\__seq_item:n\\ExplSyntaxOff",
"\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_if_in:NnTF\\l_s_seq{a}{Y}{N}\\show\\__seq_item:n\\ExplSyntaxOff",
"\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\globaldefs=1 \\seq_if_in:NnTF\\l_s_seq{a}{Y}{N}\\globaldefs=0 \\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_if_in:NnTF\\l_s_seq{a}{Y}{N}\\ExplSyntaxOff\\end{document}","\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_in:NnTF\\l_s_seq{a}{\\def\\x{Y}}{\\def\\x{N}}\\ExplSyntaxOff\\begin{document}\\x\\end{document}","\\begin{document}$\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_if_in:NnTF\\l_s_seq{a}{x}{y}\\ExplSyntaxOff$\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_seq_in_main.tex","text")^;
let resources={local:{'script_seq_in_child.tex':{data:input(root ++ "script_seq_in_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_seq_in_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_seq_in_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}

// built-in definitions use fixed categories even if the supplied kernel changes them.
let unusual_kernel=kernel ++ "\n\\catcode35=12 ";
let reset_source="\\catcode35=6 \\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_if_in:NnTF\\l_s_seq{a}{Y}{N}\\ExplSyntaxOff";
let initial=profile.expand_with_kernel(reset_source,unusual_kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let initial_text=reconstruct.reconstruct(initial)^;
{issue:initial.issue,reconstruct_issue:initial_text.issue,text_match:initial_text.text=="Y"}
