import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 {constructor:false,body:"\\msg_expandable_error:nn{seq}{misused}X"},
 {constructor:true,body:"\\msg_expandable_error:nnn{kernel}{bad-variable}{\\foo}X"},
 {constructor:false,body:"\\msg_expandable_error:nn{unknown}{unknown}X"},
 {constructor:false,body:"\\msg_new:nnn{example}{oops}{Oops~#1}\\msg_expandable_error:nnn{example}{oops}{A}X"},
 {constructor:true,body:"\\msg_new:nnn{example}{oops}{Oops}\\msg_set:nnn{example}{oops}{Changed}\\msg_expandable_error:nn{example}{oops}X"},
 {constructor:true,body:"\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\edef\\x{\\l_s_seq}[\\meaning\\x]"},
 {constructor:true,body:"\\seq_new:N\\l_s_seq\\textbf{\\seq_put_left:Nn\\l_s_seq{A}B}"},
 {constructor:true,body:"[\\expandafter\\meaning\\csname msg~text~>~regex/x-overflow\\endcsname]"},
 {constructor:false,body:"\\msg_if_exist:nnTF{str}{unescape-name}{Y}{N}\\msg_if_exist:nnTF{str}{unescape-url}{Y}{N}[\\expandafter\\meaning\\csname msg~extra~text~>~str/unescape-name\\endcsname]"},
 {constructor:false,body:"[\\meaning\\cs_gset:cpn/\\meaning\\msg_if_exist:nnT/\\meaning\\__msg_expandable_error:nn]"}
];
fn check(item) any^ {
 let source="\\ExplSyntaxOn" ++ item.body ++ "\\ExplSyntaxOff";
 let result=profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:item.constructor,
  echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let native=parse(source,{type:"tex"})^;
 {body:item.body,issue:result.issue,unsupported:result.unsupported,
  text:if(result.issue==null)reconstruct.reconstruct(result)^.text==native.text else false,
  diagnostics:result.diagnostic_records==native.diagnostics,messages:result.messages==native.messages}
}
let checked=[for(item in cases)check(item)^];
let duplicate=profile.expand_with_kernel("\\ExplSyntaxOn\\msg_new:nnn{example}{oops}{A}\\msg_new:nnn{example}{oops}{B}",kernel,
 {script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let custom_kernel=kernel ++ "\n\\expandafter\\def\\csname c__msg_text_prefix_tl\\endcsname{X}\\expandafter\\def\\csname __msg_expandable_error:nn\\endcsname#1#2{USER}";
let preloaded=profile.expand_with_kernel("\\ExplSyntaxOn\\cs_if_exist:cTF{Xseq/misused}{Y}{N}\\__msg_expandable_error:nn{a}{b}\\ExplSyntaxOff",custom_kernel,
 {script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let property_kernel=kernel ++ "\n\\catcode`\\_=11 \\catcode`\\:=11 \\def\\c__msg_text_prefix_tl{\\prop_if_in:NnTF\\g_msg_module_type_prop{seq}{X}{Z}}";
let property_preload=profile.expand_with_kernel("\\ExplSyntaxOn\\cs_if_exist:cTF{Xseq/misused}{Y}{N}\\ExplSyntaxOff",property_kernel,
 {script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
{cases:len(checked),duplicate_guard:duplicate.issue=="tex-unsupported-primitive" and duplicate.unsupported=="msg_error:nnnn",
 preloaded:preloaded.issue==null and reconstruct.reconstruct(preloaded)^.text=="YUSER",
 property_prefix:property_preload.issue==null and reconstruct.reconstruct(property_preload)^.text=="Y",
 failures:[for(item in checked where item.issue!=null or not item.text or not item.diagnostics or not item.messages) item]}
