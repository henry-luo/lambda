// S1.8, D10.2.2v2: expl3 copies control meanings at initialization.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let alias_prefix="\\catcode`\\_=11 \\catcode`\\:=11 ";
let alias_suffix="\\catcode`\\_=8 \\catcode`\\:=12 ";
let identity="\\ExplSyntaxOn\\message{identity=\\ifx\\tex_inputlineno:D\\inputlineno Y\\else N\\fi}";
let cases=[
    {kernel:alias_prefix ++ "\\let\\tex_ifnum:D\\relax " ++ alias_suffix,
        source:"\\ExplSyntaxOn\\message{identity=\\ifx\\tex_ifnum:D\\ifnum Y\\else N\\fi}\\message{relax=\\ifx\\tex_ifnum:D\\relax Y\\else N\\fi}\\ExplSyntaxOff",
        expected:"identity=Y relax=N"},
    {kernel:alias_prefix ++ "\\let\\tex_let:D\\relax " ++ alias_suffix,
        source:"\\ExplSyntaxOn\\message{identity=\\ifx\\tex_let:D\\let Y\\else N\\fi}\\ExplSyntaxOff",
        expected:"identity=Y"},
    {kernel:"\\let\\inputlineno\\relax",source:identity ++ "\\message{relax=\\ifx\\tex_inputlineno:D\\relax Y\\else N\\fi}\\ExplSyntaxOff",
        expected:"identity=Y relax=Y"},
    {kernel:"\\def\\inputlineno{myline}",source:identity ++ "\\message{value=\\meaning\\tex_inputlineno:D}\\ExplSyntaxOff",
        expected:"identity=Y value=macro:->myline"},
    {kernel:"\\chardef\\inputlineno=65",source:identity ++ "\\message{value=\\number\\tex_inputlineno:D}\\ExplSyntaxOff",
        expected:"identity=Y value=65"},
    {kernel:"\\countdef\\inputlineno=300 \\inputlineno=27",source:"\\ExplSyntaxOn\\advance\\inputlineno by 1 " ++
        "\\message{identity=\\ifx\\tex_inputlineno:D\\inputlineno Y\\else N\\fi}\\message{value=\\the\\tex_inputlineno:D}\\ExplSyntaxOff",
        expected:"identity=Y value=28"}
];
let results=[for(entry in cases,mode in [false,true])
    (let options={script_expl3:true,constructor_argument_mode:mode,echo_definitions:false,
        adapters:[],resources:{local:{},bundled:{}}},
    let result=profile.expand_with_kernel(entry.source,kernel ++ "\n" ++ entry.kernel,options,100000)^,
    {case:entry.kernel,mode:mode,issue:result.issue,matches:result.messages==[entry.expected]})];
// Copying the original let primitive must retain the source-mapping guard.
let document="\\ExplSyntaxOn\\let\\q\\tex_let:D\\global\\q\\l_tmpa_tl=\\l_tmpb_tl\\ExplSyntaxOff\\begin{document}Hello\\end{document}";
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let expanded=profile.expand_with_kernel(document,kernel ++ "\n" ++ cases[1].kernel,options,100000)^;
let tree=digest.digest(expanded)^;
{cases:len(results),failures:[for (r in results where r.issue!=null or not r.matches) r],
 guard:tree.issue=="latex-digest-unsupported" and tree.unsupported=="expl3 token-list assignment source mapping"}
