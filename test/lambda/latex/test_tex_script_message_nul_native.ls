import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import native_snapshot: ._tex_native_snapshot
import utf8: lambda.io.utf8
import pdf: lambda.doc.latex.tex_engine.pdfstrings
let native=input("test/lambda/latex/fixtures/tex_message_nul_native.json","json")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let legacy=profile.preload_kernel(kernel,{*:options,constructor_argument_mode:false},100000)^;
let constructor=profile.preload_kernel(kernel,{*:options,constructor_argument_mode:true},100000)^;
let head="\\catcode0=12 \\begin{document}\\errmessage{";
let tail="^^@B}X\\end{document}";
let unicode=join([for(i in 1 to 256) "é"],"")^;
let sources=[
 "\\catcode0=12 \\begin{document}\\errmessage{A^^@B}X\\end{document}",
 "\\catcode0=12 \\begin{document}\\errmessage{^^@B}X\\end{document}",
 "\\catcode0=12 \\begin{document}\\errmessage{AA^^@BB}\\errmessage{CC}X\\end{document}",
 "\\catcode0=12 \\begin{document}\\message{A^^@B}\\message{C}\\errmessage{E}X\\end{document}",
 "\\catcode0=12 \\catcode10=12 \\newlinechar=10 \\begin{document}\\message{A^^@B^^JC}\\errmessage{D}X\\end{document}",
 head ++ slice(unicode,0,255)^ ++ tail,
 head ++ unicode ++ tail,
 head ++ join([for(i in 1 to 512) "a"],"")^ ++ tail,
 "\\catcode0=12 \\begin{document}\\immediate\\write16{A^^@B}X\\end{document}",
 "\\catcode0=12 \\begin{document}\\message{^^@A}\\message{X}\\errmessage{B}X\\end{document}",
 "\\catcode0=12 \\catcode10=12 \\newlinechar=10 \\begin{document}\\message{A^^@B^^JC^^@D}\\errmessage{E}X\\end{document}"
];
let checks=[for(index,source in sources,mode in [false,true]) {
 let reference=native.cases[index];
 let r=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000,
     if(index==0)null else if(mode)constructor else legacy)^;
 let d=digest.digest(r)^;
 let base=source==reference.source and r.issue==null and d.issue==null and
     reconstruct.reconstruct(r)^.text==reference.tex.text and r.messages==reference.tex.messages;
 let matched=reference.valid and r.diagnostic_records==reference.tex.diagnostics and
     native_snapshot.tree_matches(d.ast,reference,source)^;
 let prefix=slice(binary(unicode)^,0,511)^;
 let guard=not reference.valid and r.diagnostics==["tex-errmessage"] and len(r.diagnostic_records)==0 and
     d.ast.tex_diagnostics==null and not utf8.is_valid(prefix)^ and pdf.escape_hex(prefix)^==reference.diag_hex;
 {ok:base and (matched or guard),matched:matched,guarded:guard,index:index,mode:mode}
}];
{cases:len(checks),matched:len([for(c in checks where c.matched)c]),guarded:len([for(c in checks where c.guarded)c]),failures:[for(c in checks where not c.ok)c]};
let public_checks=[for(index in [0,4,5,7]) {
 let reference=native.cases[index];let tex=parse(reference.source,{type:"tex"})^;let latex=parse(reference.source,{type:"latex"})^;
 {tex:parse(format(tex,"json")^,{type:"json"})^==reference.tex,latex:native_snapshot.tree_matches(latex,reference,reference.source)^}
}];
{public_cases:len(public_checks),matches:all([for(c in public_checks)c.tex and c.latex])}
