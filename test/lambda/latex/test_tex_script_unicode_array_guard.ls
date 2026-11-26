import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import expansion: lambda.doc.latex.tex_engine.expansion
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let constructor={*:options,constructor_argument_mode:true};
let legacy_preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let constructor_preload=profile.preload_expl3_kernel(kernel,constructor,100000)^;
let sources=["\\ExplSyntaxOn\\c__codepoint_category_index_intarray\\the\\font\\ExplSyntaxOff", "\\ExplSyntaxOn\\c__codepoint_lowercase_blocks_intarray\\the\\font\\ExplSyntaxOff", "\\ExplSyntaxOn\\fontdimen1\\c__codepoint_category_index_intarray=7sp\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\number\\fontdimen17409\\c__codepoint_category_index_intarray\\relax\\ExplSyntaxOff", "\\ExplSyntaxOn\\font\\f=cmr10~at~15sp\\relax\\ifx\\f\\c__codepoint_category_index_intarray Y\\else N\\fi/\\ifdefined\\g__codepoint_category_index_intarray Y\\else N\\fi\\ExplSyntaxOff", "\\ExplSyntaxOn\\font\\f=cmr10~at~20sp\\relax\\ifx\\f\\c__codepoint_lowercase_blocks_intarray Y\\else N\\fi\\ExplSyntaxOff"];
// Native aliases resolve to the same font; its original control's identity remains guarded.
[for (entry in [{options:options,preload:legacy_preload},{options:constructor,preload:constructor_preload}])
 [for (source in sources) (let r=profile.expand_with_kernel(source,kernel,entry.options,100000,entry.preload)^,
  {text:reconstruct.reconstruct(r)^.text,issue:r.issue,unsupported:r.unsupported})]];
let base=profile.preload_kernel(kernel ++ "\n\\expandafter\\def\\csname g__codepoint_category_index_intarray\\endcsname{caller}",options,20000000)^;
let enabled=expansion.enable_script_expl3(base)^;
{issue:enabled.issue,unsupported:enabled.unsupported, preserved:enabled.definitions['control:g__codepoint_category_index_intarray']==base.definitions['control:g__codepoint_category_index_intarray']}
