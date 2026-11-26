import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
fn options(mode) => {echo_definitions:true,constructor_argument_mode:mode,adapters:[],resources:{local:{},bundled:{}}}
// Both modes resume independent documents from their matching immutable kernels.
let preloads=[for(mode in [false,true]) profile.preload_kernel(kernel,options(mode),100000)^];
let cases=[
 "\\setbox0=\\hbox{}\\begin{document}X\\end{document}",
 "\\setbox0=\\hbox{Hi}\\begin{document}A\\copy0!\\box0!\\copy0 B\\end{document}",
 "\\begin{document}A\\setbox0=\\hbox{Hi}B\\end{document}",
 "\\begin{document}A\\setbox0=\\hbox{Hi}\\copy0 B\\end{document}",
 "\\begin{document}\\setbox0=\\vbox{Hi}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{a%note\n b}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{\\textbf{Hi}}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{A\\hbox{B}C}\\copy0\\end{document}",
 "\\def\\a{X}\\setbox0=\\hbox{}\\begin{document}\\a\\end{document}",
 "\\def\\a{X}\\setbox0=\\hbox{Y}\\begin{document}\\copy0\\end{document}",
 "\\setbox0=\\hbox{A}\\begin{document}\\copy0/\\box0\\end{document}",
 "\\begin{document}\\ifvoid0 Y\\else N\\fi\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{A}\\ifhbox0 Y\\else N\\fi\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{A}{\\setbox0=\\hbox{B}\\copy0}\\copy0\\end{document}",
 "\\setbox0=\\hbox{A}\\begin{document}\\unhcopy0/\\unhbox0\\end{document}",
 "\\setbox0=\\vbox{A}\\begin{document}\\vsplit0 to 3pt\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{\\hrule width1pt height2pt}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{\\def\\x{Q}\\x}\\copy0\\end{document}",
 "\\begin{document}\\setbox0=\\hbox{é Ω}\\copy0\\end{document}"];
let results=[for(source in cases,mode in [false,true]) {
 let ex=profile.expand_with_kernel(source,kernel,options(mode),100000,
    if(source==cases[0]) null else if(mode) preloads[1] else preloads[0])^;
 let tex=parse(source,{type:"tex"})^;let native=parse(source,{type:"latex"})^;let d=digest.digest(ex)^;
 let guarded=d.issue=="latex-digest-unsupported" and d.unsupported=="box rule source mapping";
 {guarded:guarded,supported:ex.issue==null and d.issue==null,
  text:reconstruct.reconstruct(ex)^.text==tex.text,messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,
  tree:d.ast==native}
}];
{cases:len(results),guarded:len([for(r in results where r.guarded) r]),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics]),tree:all([for(r in results) r.tree])};
let path="test/lambda/latex/fixtures/local_packages/script-box-register-main.tex";
let child="script-box-register-child.tex";
[for(mode in [false,true]) {
 let ex=profile.expand_with_kernel(input(path,"text")^,kernel,{echo_definitions:true,constructor_argument_mode:mode,adapters:[],source_name:path,
  resources:{local:{[child]:{data:input("test/lambda/latex/fixtures/local_packages/" ++ child,"text")^}},bundled:{}}},100000)^;
 let tex=input(path,"tex")^;let native=input(path,"latex")^;let d=digest.digest(ex)^;
 {mode:mode,expansion_issue:ex.issue,issue:d.issue,unsupported:d.unsupported,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native,
  script:if(d.ast==native)null else d.ast,native:if(d.ast==native)null else native}
}]
