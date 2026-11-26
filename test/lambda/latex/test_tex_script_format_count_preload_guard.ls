import profile: lambda.doc.latex.tex_engine.profile
import expansion: lambda.doc.latex.tex_engine.expansion
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let sources=["\\count42=123\\relax", "\\expandafter\\def\\csname l__sort_min_int\\endcsname{caller}", "\\expandafter\\def\\csname c__fp_prec_int\\endcsname{caller}", "\\count41=99\\relax", "\\count125=7\\relax", "\\expandafter\\def\\csname __intarray_gset_count:Nw\\endcsname{caller}", "\\expandafter\\def\\csname tex_luatexversion:D\\endcsname{caller}", "\\expandafter\\def\\csname tex_omathchardef:D\\endcsname{caller}", "\\expandafter\\def\\csname tex_XeTeXversion:D\\endcsname{caller}", "\\count10=200\\relax", "\\count10=0\\relax", "\\countdef\\allocationnumber=5\\relax", "\\makeatletter\\countdef\\insc@unt=5\\relax\\makeatother", "\\makeatletter\\countdef\\m@ne=5\\relax\\makeatother", "\\def\\newcount#1{\\countdef#1=5}", "\\makeatletter\\def\\alloc@#1#2#3#4#5{}\\makeatother", "\\let\\advance\\relax", "\\makeatletter\\chardef\\@ne=2\\relax\\makeatother", "\\count22=0\\relax", "\\def\\wlog#1{}", "\\makeatletter\\def\\ch@ck#1#2#3{}\\makeatother", "\\makeatletter\\def\\newcount{\\alloc@0\\count\\countdef\\insc@unt}\\makeatother"];
[for(source in sources) (let base=profile.preload_kernel(kernel ++ "\n" ++ source,options,20000000)^,
 let enabled=expansion.enable_script_expl3(base)^,
 {issue:enabled.issue,unsupported:enabled.unsupported,
  loop:enabled.counts['41'],font_counter:enabled.counts['42'],future:enabled.counts['125'],
  tracker_preserved:if(enabled.issue != null) enabled.counts['10']==base.counts['10'] else null,
  fonts_preserved:if(enabled.issue != null) enabled.fonts==base.fonts else null,
  allocator_preserved:all([for(name in ["newcount","alloc@","ch@ck","wlog","advance","allocationnumber","insc@unt","m@ne"]) enabled.definitions['control:' ++ name]==base.definitions['control:' ++ name]]),
  sort_preserved:enabled.definitions['control:l__sort_min_int']==base.definitions['control:l__sort_min_int'],
  constant_preserved:enabled.definitions['control:c__fp_prec_int']==base.definitions['control:c__fp_prec_int'],
  branch_preserved:all([for(name in ["__intarray_gset_count:Nw","tex_luatexversion:D","tex_omathchardef:D","tex_XeTeXversion:D"]) enabled.definitions['control:' ++ name]==base.definitions['control:' ++ name]])})]
