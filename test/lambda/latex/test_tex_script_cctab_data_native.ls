import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let r=profile.preload_expl3_kernel(kernel,options,20000000)^;
let arrays=[r.definitions['control:c_initex_cctab'],r.definitions['control:c_other_cctab'],r.definitions['control:c_str_cctab'],r.definitions['control:g_tmpa_cctab'],r.definitions['control:g_tmpb_cctab']];
// The golden comes from native TeX reading every font parameter.
join([for (a in arrays, i in 1 to r.fonts[a.index].param_count) string(r.fonts[a.index].params[string(i)])],",")^ ++ ","
