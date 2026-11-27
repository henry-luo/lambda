// S1.8, D10.2.2v2: expl3 primitive aliases keep their original TeX meanings.
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:false,adapters:[],resources:{local:{},bundled:{}}};
let cached=[for (mode in [false,true]) profile.preload_expl3_kernel(kernel,{*:options,constructor_argument_mode:mode},100000)^];
let cases=[
    {source:"\\ifdefined\\tex_ifnum:D Y\\else N\\fi/\\ifx\\tex_ifnum:D\\ifnum Y\\else N\\fi", expected:"Y/Y"},
    {source:"\\ifdefined\\tex_ifx:D Y\\else N\\fi/\\ifx\\tex_ifx:D\\ifx Y\\else N\\fi", expected:"Y/Y"},
    {source:"\\ifdefined\\tex_def:D Y\\else N\\fi/\\ifx\\tex_def:D\\def Y\\else N\\fi", expected:"Y/Y"},
    {source:"\\tex_ifx:D a aY\\else N\\fi/\\tex_ifx:D a bN\\else Y\\fi", expected:"Y/Y"},
    {source:"\\tex_def:D\\test{OK}\\test", expected:"OK"},
    {source:"\\let\\ifx\\relax\\let\\def\\relax\\tex_ifx:D\\tex_ifx:D\\ifx N\\else Y\\fi/\\tex_def:D\\test{OK}\\test", expected:"Y/OK"},
    {source:"{\\let\\tex_ifx:D\\relax\\ifx\\tex_ifx:D\\relax Y\\else N\\fi}\\ifx\\tex_ifx:D\\ifx Y\\else N\\fi", expected:"{Y}Y"}
];
let results=[for (entry in cases, mode in [false,true])
    (let configured={*:options,constructor_argument_mode:mode},
    let result=profile.expand_with_kernel("\\ExplSyntaxOn" ++ entry.source ++ "\\ExplSyntaxOff",kernel,configured,100000,
        cached[if(mode) 1 else 0])^,
    let output=reconstruct.reconstruct(result)^,
    {expected:entry.expected,actual:output.text,issue:result.issue,reconstruct_issue:output.issue})];
// expl3 copies the live kernel control when it is defined, otherwise it copies end.
let altered=[
    {kernel:"\\makeatletter\\let\\@@end\\undefined\\makeatother", target:"\\end"},
    {kernel:"\\makeatletter\\let\\@@end\\relax\\makeatother", target:"\\relax"}
];
let kernel_results=[for (entry in altered, mode in [false,true])
    (let configured={*:options,constructor_argument_mode:mode},
    let source="\\ExplSyntaxOn\\ifx\\tex_end:D" ++ entry.target ++ " Y\\else N\\fi\\ExplSyntaxOff",
    let result=profile.expand_with_kernel(source,kernel ++ "\n" ++ entry.kernel,configured,100000)^,
    let output=reconstruct.reconstruct(result)^,
    {expected:"Y",actual:output.text,issue:result.issue,reconstruct_issue:output.issue})];
let all_results=[*results,*kernel_results];
{cases:len(all_results),failures:[for (result in all_results where result.actual != result.expected or result.issue != null or result.reconstruct_issue != null) result]}
