import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let constructor={*:options,constructor_argument_mode:true};
let legacy_preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let constructor_preload=profile.preload_expl3_kernel(kernel,constructor,100000)^;
let sources = ["\\ExplSyntaxOn\\regex_match:nnTF{a}{a}{}{}\\number\\fontdimen1\\g__regex_state_active_intarray\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\regex_match:nnTF{a}{a}{}{}\\number\\hyphenchar\\g__regex_state_active_intarray\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\regex_match:nnTF{a}{a}{}{}\\number\\fontdimen9\\c__fp_exp_intarray\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\fontdimen1\\g__regex_state_active_intarray=7sp\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\regex_match:nnTF{a}{a}{}{}\\font\\f=cmr10~at~8sp\\relax\\number\\fontdimen1\\f\\relax\\ExplSyntaxOff"];
// Regex changes its native scratch arrays, while unrelated array reads remain usable.
[for (entry in [{options:options,preload:legacy_preload},{options:constructor,preload:constructor_preload}])
 [for (source in sources) (let r=profile.expand_with_kernel(source,kernel,entry.options,100000,entry.preload)^,
   {issue:r.issue,unsupported:r.unsupported})]];
[for (source in sources) parse(source,{type:"tex"}) ^ {null}]
