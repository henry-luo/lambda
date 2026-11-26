import profile: lambda.doc.latex.tex_engine.profile
import expansion: lambda.doc.latex.tex_engine.expansion
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let opts={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let sources=["\\globaldefs=1\\relax","\\globaldefs=-1\\relax","\\afterassignment X", "\\expandafter\\def\\csname c_initex_cctab\\endcsname{caller}"];
[for (source in sources)
 (let base=profile.preload_kernel(kernel ++ "\n" ++ source,opts,20000000)^,
  let enabled=expansion.enable_script_expl3(base)^,
  {issue:enabled.issue,unsupported:enabled.unsupported,
   globaldefs_preserved:enabled.parameters.globaldefs==base.parameters.globaldefs,
   hook_preserved:enabled.after_assignment==base.after_assignment,
   cctab_preserved:enabled.definitions['control:c_initex_cctab']==base.definitions['control:c_initex_cctab']})]
