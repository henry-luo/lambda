// D10.2.2v2: expected stream states were captured from TeX Live 2025.
import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,resources:{local:{'ior-lines.txt':{data:"alpha\nbeta\n"}},bundled:{}}};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let prelude="\\ExplSyntaxOn\\ior_new:N\\g_a_ior\\ior_new:N\\g_b_ior\\ior_new:N\\g_c_ior";
let state_a="[\\ifnum\\g_a_ior<16 O\\else C\\fi/\\ior_if_eof:NTF\\g_a_ior{Y}{N}]";
let state_b="[\\ifnum\\g_b_ior<16 O\\else C\\fi/\\ior_if_eof:NTF\\g_b_ior{Y}{N}]";
let state_c="[\\ifnum\\g_c_ior<16 O\\else C\\fi/\\ior_if_eof:NTF\\g_c_ior{Y}{N}]";
let same_b="[\\ifx\\saved_slot\\g_b_ior E\\else D\\fi]";
let same_c="[\\ifx\\saved_slot\\g_c_ior E\\else D\\fi]";
let cases=[
    {name:"root", source:prelude ++
        "\\globaldefs=-1 \\ior_open:Nn\\g_a_ior{ior-lines.txt}" ++
        state_a ++
        "\\ior_close:N\\g_a_ior" ++
        state_a, expected:"[O/N][C/Y]"},
    {name:"local_open", source:prelude ++
        "\\globaldefs=-1 \\begingroup\\ior_open:Nn\\g_a_ior{ior-lines.txt}" ++
        state_a ++
        "\\endgroup" ++
        state_a ++
        "\\ior_open:Nn\\g_b_ior{ior-lines.txt}" ++
        state_b, expected:"[O/N][C/Y][O/N]"},
    {name:"local_close", source:prelude ++
        "\\ior_open:Nn\\g_a_ior{ior-lines.txt}\\let\\saved_slot\\g_a_ior\\globaldefs=-1 \\begingroup\\ior_close:N\\g_a_ior" ++
        state_a ++
        "\\endgroup" ++
        state_a ++
        "\\globaldefs=0 \\ior_open:Nn\\g_b_ior{ior-lines.txt}" ++
        state_b ++
        same_b, expected:"[C/Y][O/Y][O/N][D]"},
    {name:"reuse_restore", source:prelude ++
        "\\ior_open:Nn\\g_a_ior{ior-lines.txt}\\let\\saved_slot\\g_a_ior\\ior_close:N\\g_a_ior\\globaldefs=-1 \\begingroup\\ior_open:Nn\\g_b_ior{ior-lines.txt}" ++
        state_b ++
        "\\endgroup" ++
        state_b ++
        "\\ior_open:Nn\\g_c_ior{ior-lines.txt}" ++
        state_c ++
        same_c, expected:"[O/N][C/Y][O/N][E]"},
    {name:"global_override", source:prelude ++
        "\\ior_open:Nn\\g_a_ior{ior-lines.txt}\\globaldefs=-1 \\begingroup\\ior_close:N\\g_a_ior" ++
        state_a ++
        "\\globaldefs=1 \\ior_open:Nn\\g_b_ior{ior-lines.txt}" ++
        state_b ++
        "\\endgroup" ++
        state_a ++
        state_b, expected:"[C/Y][O/N][O/N][O/N]"},
    {name:"local_allocate", source:prelude ++
        "\\globaldefs=-1 \\begingroup\\ior_open:Nn\\g_a_ior{ior-lines.txt}\\ior_open:Nn\\g_b_ior{ior-lines.txt}" ++
        state_a ++
        state_b ++
        "\\endgroup" ++
        state_a ++
        state_b ++
        "\\ior_open:Nn\\g_c_ior{ior-lines.txt}" ++
        state_c, expected:"[O/N][O/N][C/Y][C/Y][O/N]"},
    {name:"nested_local", source:prelude ++
        "\\ior_open:Nn\\g_a_ior{ior-lines.txt}\\let\\saved_slot\\g_a_ior\\ior_close:N\\g_a_ior\\globaldefs=-1 \\begingroup\\ior_open:Nn\\g_b_ior{ior-lines.txt}" ++
        state_b ++
        "\\begingroup\\ior_close:N\\g_b_ior" ++
        state_b ++
        "\\endgroup" ++
        state_b ++
        "\\endgroup" ++
        state_b ++
        "\\ior_open:Nn\\g_c_ior{ior-lines.txt}" ++
        state_c ++
        same_c, expected:"[O/N][C/Y][O/Y][C/Y][O/N][E]"},
    {name:"nested_global", source:prelude ++
        "\\ior_open:Nn\\g_a_ior{ior-lines.txt}\\globaldefs=-1 \\begingroup\\ior_close:N\\g_a_ior\\begingroup\\ior_open:Nn\\g_b_ior{ior-lines.txt}" ++
        state_b ++
        "\\globaldefs=1 \\ior_close:N\\g_b_ior" ++
        state_b ++
        "\\endgroup\\endgroup" ++
        state_a ++
        state_b ++
        "\\ior_open:Nn\\g_c_ior{ior-lines.txt}" ++
        state_c, expected:"[O/N][C/Y][O/Y][C/Y][O/N]"}
];
[for (c in cases) (let r=profile.expand_with_kernel(c.source,kernel,options,100000,preload)^,{name:c.name,issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),expected:c.expected,match:join([for(t in r.output)t.value],"")==c.expected})]
