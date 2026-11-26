import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let opts={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let setup="\\font\\seed=cmr10 at 1sp \\relax";
let source="\\ExplSyntaxOn\\ifx\\seed\\c__fp_exp_intarray Y\\else N\\fi/\\number\\fontdimen9\\seed\\relax\\ExplSyntaxOff";
let loaded=profile.preload_expl3_kernel(kernel ++ "\n" ++ setup,opts,20000000)^;
let actual=profile.expand_with_kernel(source,kernel ++ "\n" ++ setup,opts,20000000,loaded)^;
{preload:loaded.issue,issue:actual.issue,text:reconstruct.reconstruct(actual)^.text,diagnostics:actual.diagnostic_records};
let conflict=profile.preload_expl3_kernel(kernel ++ "\n" ++ setup ++ "\\font\\other=cmr10 at 21sp \\relax",opts,20000000)^;
{issue:conflict.issue,unsupported:conflict.unsupported}
