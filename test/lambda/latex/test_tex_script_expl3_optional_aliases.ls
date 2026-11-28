// S1.8, D10.2.2v2: optional expl3 copies follow source order and definedness.
import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
    {kernel:"\\def\\XeTeXcharclass{sentinel}",
        source:"\\ExplSyntaxOn\\message{identity=\\ifx\\tex_XeTeXcharclass:D\\XeTeXcharclass Y\\else N\\fi}\\message{value=\\meaning\\tex_XeTeXcharclass:D}\\ExplSyntaxOff",
        expected:"identity=Y value=macro:->sentinel"},
    {kernel:"\\catcode`\\_=11 \\catcode`\\:=11 \\def\\tex_XeTeXcharclass:D{original} \\catcode`\\_=8 \\catcode`\\:=12 ",
        source:"\\ExplSyntaxOn\\message{defined=\\ifdefined\\tex_XeTeXcharclass:D Y\\else N\\fi}\\message{value=\\meaning\\tex_XeTeXcharclass:D}\\ExplSyntaxOff",
        expected:"defined=Y value=macro:->original"},
    {kernel:"\\chardef\\XeTeXprotrudechars=7 \\chardef\\protrudechars=9",
        source:"\\ExplSyntaxOn\\message{identity=\\ifx\\tex_protrudechars:D\\protrudechars Y\\else N\\fi}\\message{value=\\number\\tex_protrudechars:D}\\ExplSyntaxOff",
        expected:"identity=Y value=9"},
    {kernel:"\\def\\beginL{sentinel}\\catcode`\\_=11 \\catcode`\\:=11 \\def\\tex_endL:D{original}\\catcode`\\_=8 \\catcode`\\:=12 ",
        source:"\\ExplSyntaxOn\\message{begin=\\ifx\\tex_beginL:D\\beginL Y\\else N\\fi}\\message{enddefined=\\ifdefined\\tex_endL:D Y\\else N\\fi}\\ExplSyntaxOff",
        expected:"begin=Y enddefined=N"}
];
let results=[for(entry in cases,mode in [false,true])
    (let options={script_expl3:true,constructor_argument_mode:mode,echo_definitions:false,
        adapters:[],resources:{local:{},bundled:{}}},
    let result=profile.expand_with_kernel(entry.source,kernel ++ "\n" ++ entry.kernel,options,100000)^,
    {case:entry.kernel,mode:mode,issue:result.issue,matches:result.messages==[entry.expected]})];
// A copied directlua control must reach the existing unsupported LuaTeX guard.
let lua_kernel="\\makeatletter\\let\\@@hyph\\relax\\let\\@@italiccorr\\/\\let\\@@underline\\underline" ++
    "\\let\\frozen@everydisplay\\everydisplay\\let\\frozen@everymath\\everymath" ++
    "\\let\\pdftracingfonts\\undefined\\def\\directlua#1{}\\makeatother";
let lua=profile.expand_with_kernel("",kernel ++ "\n" ++ lua_kernel,
    {script_expl3:true,echo_definitions:false,adapters:[],resources:{local:{},bundled:{}}},100000)^;
{cases:len(results),failures:[for (r in results where r.issue!=null or not r.matches) r],
 lua_guard:lua.issue=="tex-unsupported-primitive" and lua.unsupported=="expl3 LuaTeX font initialization"}
