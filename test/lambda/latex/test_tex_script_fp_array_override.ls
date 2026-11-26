import profile: lambda.doc.latex.tex_engine.profile
import expansion: lambda.doc.latex.tex_engine.expansion
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let opts={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let sources=["\\expandafter\\countdef\\csname g__intarray_font_int\\endcsname=5\\relax",
 "\\expandafter\\def\\csname c__fp_exp_intarray\\endcsname{caller}"];
[for (source in sources)
 (let base=profile.preload_kernel(kernel ++ "\n" ++ source,opts,20000000)^,
  let key=if (contains(source,"g__intarray_font_int")) "control:g__intarray_font_int" else "control:c__fp_exp_intarray",
  let enabled=expansion.enable_script_expl3(base)^,
  {issue:enabled.issue,unsupported:enabled.unsupported,preserved:enabled.definitions[key]==base.definitions[key]})];
let ready=profile.preload_expl3_kernel(kernel,{*:opts,script_expl3:true},20000000)^;
let changed=expansion.resume(ready,"\\expandafter\\def\\csname c__fp_exp_intarray\\endcsname{caller}","<caller>",20000000)^;
let again=expansion.enable_script_expl3(changed)^;
{issue:again.issue,preserved:again.definitions['control:c__fp_exp_intarray']==changed.definitions['control:c__fp_exp_intarray'],fonts_unchanged:again.fonts==changed.fonts}
