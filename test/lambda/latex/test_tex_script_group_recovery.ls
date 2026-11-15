import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let script=profile.expand_with_kernel(source,kernel,options,100000)^;
 let native=parse(source,{type:"tex"})^;
 let rebuilt=reconstruct.reconstruct(script)^;
 let messages=slice(native.messages,len(baseline),len(native.messages))^;
 {source:source,match:script.issue==null and rebuilt.issue==null and rebuilt.text==native.text and
   script.messages==messages and script.diagnostic_records==native.diagnostics}
}
let cases=["A}B", "A}}B", "A\\endgroup B", "{\\endgroup}", "{A\\endgroup B}C",
 "\\begingroup}A\\endgroup B", "{\\endgroup X}Y", "A}\\def\\x{Q}\\x",
 "}A", "A}", "\\endgroup", "{\\endgroup\\endgroup}",
 "\\begingroup}}B\\endgroup", "\\begingroup{X\\endgroup}Y\\endgroup",
 "A\\def\\bad{}}\\bad B", "A\\def\\bad{\\endgroup}\\bad B",
 "A\\let\\eg\\endgroup\\eg B", "A\\catcode`x=2 xB",
 "A\n}B", "A\n\\endgroup B", "$a}b$", "$a\\endgroup b$",
 "{A\\endgroup \\def\\x{B}\\x}C", "\\begingroup}\\endgroup}Z",
"{\\aftergroup A\\endgroup B}C",
 "\\begingroup\\aftergroup A}B\\endgroup C",
 "\\def\\x{O}{\\def\\x{I}\\endgroup\\x}\\x",
 "\\begingroup\\def\\x{I}}\\x\\endgroup\\x",
 "{\\aftergroup A\\endgroup\\aftergroup B}C",
 "\\begingroup{\\aftergroup A\\endgroup B}C\\endgroup D",
 "\\begingroup}\\aftergroup Q\\endgroup Z",
 "{A\\endgroup\\global\\def\\x{X}}\\x",
 "\\iftrue A}B\\else N\\fi", "\\iffalse A}B\\else Y\\fi"];
let direct=[for(source in cases) check(source)^];
let root="test/lambda/latex/fixtures/local_packages/";
let main=root ++ "script_group_recovery_main.tex";
let child="script_group_recovery_child.tex";
let resources={local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}};
let expanded=profile.expand_with_kernel(input(main,"text")^,kernel,
 {source_name:main,echo_definitions:true,adapters:[],resources:resources},100000)^;
let native=input(main,"tex")^;
let rebuilt=reconstruct.reconstruct(expanded)^;
{cases:len(direct),failures:[for(case in direct where not case.match) case],
 foreign:{issue:expanded.issue,text_match:rebuilt.text==native.text,
  diagnostics_match:expanded.diagnostic_records==native.diagnostics,
  file_recorded:len(expanded.diagnostic_records)==2 and all([for(d in expanded.diagnostic_records) d.file==child])}}
