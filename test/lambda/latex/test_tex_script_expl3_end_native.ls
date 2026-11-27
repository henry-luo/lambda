// S1.8, D10.2.2v2: expl3 copies the native end primitive's meaning.
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let native=input("test/lambda/latex/fixtures/tex_expl3_end_native.json","json")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let preloads=[for(expl in [false,true],mode in [false,true])
    (let opt={*:options,script_expl3:expl,constructor_argument_mode:mode},
    if(expl) profile.preload_expl3_kernel(kernel,opt,100000)^ else profile.preload_kernel(kernel,opt,100000)^)];
let checked=[for(i,entry in native.cases,mode in [false,true])
    (let opt={*:options,script_expl3:entry.expl3,constructor_argument_mode:mode},
    let index=(if(entry.expl3) 2 else 0)+(if(mode) 1 else 0),
    let result=profile.expand_with_kernel(entry.source,kernel,opt,100000,
        if(i==0 or i==8)null else preloads[index])^,
    let built=reconstruct.reconstruct(result)^,
    {index:i,mode:mode,match:result.issue==null and built.issue==null and
        built.text==entry.tex.text and result.messages==entry.tex.messages and
        result.diagnostic_records==entry.tex.diagnostics})];
let public=[for(i in [0,1,4,6,8,9]) (let entry=native.cases[i],
    let result=parse(entry.source,{type:"tex"}) ^ {null},
    {index:i,match:result!=null and result.text==entry.tex.text and
        result.messages==entry.tex.messages and result.diagnostics==entry.tex.diagnostics})];
{cases:len(checked),failures:[for(item in checked where not item.match) item],
 public_cases:len(public),public_failures:[for(item in public where not item.match) item]}
