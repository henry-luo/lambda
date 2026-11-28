import profile: lambda.doc.latex.tex_engine.profile
import expansion: lambda.doc.latex.tex_engine.expansion
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let base=profile.preload_kernel(kernel,options,100000)^;
let altered=expansion.resume(base,"\\catcode`\\_=11 \\catcode`\\:=11 \\countdef\\c_log_iow=5 \\count5=77 \\count26=81", "<caller>",100000)^;
let enabled=expansion.enable_script_expl3(altered)^;
let again=expansion.enable_script_expl3(enabled)^;
// The caller's c_log_iow occupies no new slot; later format declarations still allocate in order.
{issue:again.issue, log:again.definitions['control:c_log_iow'], caller_count:again.counts['5'], scratch:again.counts['26'], max:again.counts['25'], alloc:again.counts['10'], same_counts:enabled.counts==again.counts}
