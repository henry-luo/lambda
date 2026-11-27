// S1.8, S4.6.2, D10.2.2v2: compare power expressions with native TeX.
import fp: lambda.doc.latex.tex_engine.fp
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let native=input("test/lambda/latex/fixtures/tex_expl3_power_native.json","json")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let base={script_expl3:true,echo_definitions:false,adapters:[],resources:{local:{},bundled:{}}};
let fp_results=[for(index,entry in native.cases)
    (let result=fp.evaluate(entry.expression)^,
    {index:index,expression:entry.expression,issue:result.issue,
        actual:if(result.value==null) null else decimal(result.value)^,expected:decimal(entry.expected)^})];
let cached=[for(mode in [false,true]) profile.preload_expl3_kernel(kernel,{*:base,constructor_argument_mode:mode},100000)^];
let tex_results=[for(index,entry in native.public_cases,mode in [false,true] where index<8)
    (let result=profile.expand_with_kernel(entry.source,kernel,{*:base,constructor_argument_mode:mode},100000,cached[if(mode)1 else 0])^,
    let output=reconstruct.reconstruct(result)^,
    {index:index,mode:mode,issue:result.issue,actual:output.text,expected:entry.expected,
        diagnostics_empty:len(result.diagnostic_records)==0})];
// Keep unsupported domains and non-finite magnitudes from producing finite values.
let guarded=["(-2)^2.5","0^-1","2^1000000","2^-1000000"];
let guard_results=[for(expression in guarded)
    (let result=fp.evaluate(expression)^,
    {expression:expression,issue:result.issue})];
{fp_cases:len(fp_results),fp_failures:[for(r in fp_results where r.issue!=null or r.actual!=r.expected) r],
 tex_cases:len(tex_results),tex_failures:[for(r in tex_results where r.issue!=null or r.actual!=r.expected or not r.diagnostics_empty) r],
 guard_failures:[for(r in guard_results where r.issue!="expl3-fp-unsupported") r]}
