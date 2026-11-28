import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let opts={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let r=profile.preload_expl3_kernel(kernel,opts,20000000)^;
let names=["g__regex_state_active_intarray","g__regex_thread_info_intarray","g__regex_submatch_prev_intarray","g__regex_submatch_begin_intarray","g__regex_submatch_end_intarray","g__regex_submatch_case_intarray","g__regex_balance_intarray"];
// Native independently scans every entry; missing sparse parameters read as zero.
[for (name in names) (let f=r.fonts[r.definitions["control:" ++ name].index],
 {name:name,text:string(f.hyphen_char) ++ "/" ++ string(len([for(value in f.params where value!=0) value])),diagnostics:[]})]
