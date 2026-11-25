import facade: lambda.doc.latex.tex_engine.facade

let root="test/lambda/latex/fixtures/local_packages/";
let tex_name="script_cs_exist_main.tex";
let tex_source=input(root ++ tex_name,"text")^;
let tex_options={script_expl3:true,source_name:root ++ tex_name,adapters:[],chaptered:[],
    resources:{local:{'script_cs_exist_child.tex':{data:input(root ++ "script_cs_exist_child.tex","text")^}},bundled:{}}};
let tex=facade.tex(tex_source,tex_options,100000)^;
let native_tex=input(root ++ tex_name,"tex")^;
let latex_name="vcenter-math-main.tex";
let latex_source=input(root ++ latex_name,"text")^;
let latex_options={constructor_argument_mode:true,source_name:root ++ latex_name,adapters:[],chaptered:[],
    resources:{local:{'vcenter-math-child.tex':{data:input(root ++ "vcenter-math-child.tex","text")^}},bundled:{}}};
let latex=facade.latex(latex_source,latex_options,100000)^;
let native_latex=input(root ++ latex_name,"latex")^;
let diagnostic_source="\\ifnum1 1 T\\else F\\fi X";
let diagnosed=facade.tex(diagnostic_source,{adapters:[],chaptered:[],resources:{local:{},bundled:{}}},100000)^;
let native_diagnostic=parse(diagnostic_source,{type:"tex"})^;
let absent=[facade.tex("\\input{missing}",{adapters:[],chaptered:[]},100000)^,
    facade.latex("\\input{missing}",{adapters:[],chaptered:[]},100000)^];
{cases:3+len(absent),failures:[*[for(item in [{actual:tex,native:native_tex},{actual:latex,native:native_latex},
    {actual:diagnosed,native:native_diagnostic}] where item.actual.issue!=null or item.actual.root!=item.native) item],
    *[for(item in absent where item.root!=null or item.issue!="tex-unsupported-primitive" or item.unsupported!="input") item]]}
