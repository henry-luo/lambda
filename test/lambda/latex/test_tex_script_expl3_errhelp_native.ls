// S1.8, D10.2.2v2: expl3 error help shares TeX's token-list parameter.
import compare: ._tex_script_recovery
let native=input("test/lambda/latex/fixtures/tex_expl3_errhelp_native.json","json")^;
let legacy=compare.preload_expl3({constructor_argument_mode:false})^;
let constructor=compare.preload_expl3({constructor_argument_mode:true})^;
let checked=[for(i,entry in native.cases,mode in [false,true])
    (let result=compare.check_expansion(entry.source,{script_expl3:true,constructor_argument_mode:mode},entry,
        if(i==0)null else if(mode)constructor else legacy)^,
    {index:i,mode:mode,result:result})];
let public=[for(i in [1,2,4]) (let entry=native.cases[i],
    let result=parse(entry.source,{type:"tex"}) ^ {null},
    {index:i,match:result!=null and result.text==entry.tex.text and
        result.messages==entry.tex.messages and result.diagnostics==entry.tex.diagnostics})];
{cases:len(checked),failures:[for(item in checked where item.result.issue!=null or
    item.result.reconstruct_issue!=null or not item.result.text_match or not item.result.messages_match or
    not item.result.diagnostics_match) item],public_cases:len(public),
    public_failures:[for(item in public where not item.match) item]}
