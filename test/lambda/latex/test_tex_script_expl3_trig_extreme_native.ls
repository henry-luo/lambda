// S1.8, S4.6.2, D10.2.2v2: large-angle results match native TeX.
import fp: lambda.doc.latex.tex_engine.fp
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let native=input("test/lambda/latex/fixtures/tex_expl3_trig_extreme_native.json","json")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let base={script_expl3:true,echo_definitions:false,adapters:[],resources:{local:{},bundled:{}}};
let fp_results=[for(entry in native.cases)
    (let result=fp.evaluate(entry.expression)^,
    {expression:entry.expression,issue:result.issue,actual:result.value,expected:entry.expected,
        source_match:entry.source=="\\ExplSyntaxOn\\fp_eval:n{" ++ entry.expression ++ "}\\ExplSyntaxOff"})];
let cached=[for(mode in [false,true]) profile.preload_expl3_kernel(kernel,{*:base,constructor_argument_mode:mode},100000)^];
let tex_results=[for(index,entry in native.cases,mode in [false,true] where index>=8 and index<14)
    (let result=profile.expand_with_kernel(entry.source,kernel,{*:base,constructor_argument_mode:mode},100000,cached[if(mode)1 else 0])^,
    let output=reconstruct.reconstruct(result)^,
    {expression:entry.expression,mode:mode,issue:result.issue,actual:output.text,expected:entry.expected,
        diagnostics_empty:len(result.diagnostic_records)==0})];
{fp_cases:len(fp_results),fp_failures:[for(r in fp_results where r.issue!=null or r.actual!=r.expected or not r.source_match) r],
 tex_cases:len(tex_results),tex_failures:[for(r in tex_results where r.issue!=null or r.actual!=r.expected or not r.diagnostics_empty) r]}
