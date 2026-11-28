import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import native_snapshot: ._tex_native_snapshot
import utf8: lambda.io.utf8
import pdf: lambda.doc.latex.tex_engine.pdfstrings
let native=input("test/lambda/latex/fixtures/tex_diagnostic_boundary_native.json","json")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let legacy=profile.preload_kernel(kernel,{*:options,constructor_argument_mode:false},100000)^;
let constructor=profile.preload_kernel(kernel,{*:options,constructor_argument_mode:true},100000)^;
// Compare all observable native results, including the complete document tree.
fn native_match(expanded,built,reference,source) bool^ =>
 source==reference.source and expanded.issue==null and built.issue==null and
 reconstruct.reconstruct(expanded)^.text==reference.tex.text and expanded.messages==reference.tex.messages and
 expanded.diagnostic_records==reference.tex.diagnostics and native_snapshot.tree_matches(built.ast,reference,source)^

let direct_checks=[for(index,case in native.cases,mode in [false,true]) {
 let message=(if(case.count==0) "" else join([for(i in 1 to case.count) case.unit],"")^) ++ case.suffix;
 let source="\\begin{document}\\errmessage{" ++ message ++ "}X\\end{document}";
 let expanded=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000,
     if(index==0)null else if(mode)constructor else legacy)^;
 let built=digest.digest(expanded)^;
 let base=source==case.source and expanded.issue==null and built.issue==null and
     reconstruct.reconstruct(expanded)^.text==case.tex.text and expanded.messages==case.tex.messages;
 let prefix=slice(binary(message)^,0,min(len(binary(message)^),511));
 let matched=case.valid and native_match(expanded,built,case,source)^;
 let guarded=not case.valid and expanded.diagnostics==["tex-errmessage"] and
     len(expanded.diagnostic_records)==0 and built.ast.tex_diagnostics==null and
     not utf8.is_valid(prefix)^ and pdf.escape_hex(prefix)^==case.diag_hex;
 {ok:base and (matched or guarded),matched:matched,guarded:guarded,label:case.label,mode:mode}
}];
let long=join([for(i in 1 to 512) "L"],"")^;
let mixed_bodies=["\\errmessage{" ++ long ++ "}\\errmessage{short}X",
 "\\errmessage{short}\\errmessage{" ++ long ++ "}X",
 "\\newcounter{n}\\setcounter{n}{1\\relax A}\\errmessage{" ++ long ++ "}X"];
let mixed_checks=[for(index,body in mixed_bodies,mode in [false,true]) {
 let source="\\begin{document}" ++ body ++ "\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000,if(mode)constructor else legacy)^;
 let d=digest.digest(r)^; let matched=native_match(r,d,native.mixed[index],source)^;
 {ok:matched,matched:matched,guarded:false,label:"mixed",mode:mode}
}];
let local_source="\\usepackage{long-message}\\begin{document}A\\end{document}";
let style="\\ProvidesPackage{long-message}\n\\errmessage{" ++ long ++ "}\n";
let local_checks=[for(mode in [false,true]) {
 let local_options={*:options,source_name:"test/lambda/latex/fixtures/local_packages/main.tex",constructor_argument_mode:mode,
  resources:{local:{'long-message.sty':{data:style}},bundled:{}}};
 let r=profile.expand_with_kernel(local_source,kernel,local_options,100000)^;
 let d=digest.digest(r)^; let matched=style==native.local.style and native_match(r,d,native.local,local_source)^;
 {ok:matched,matched:matched,guarded:false,label:"local",mode:mode}
}];
let checks=[*direct_checks,*mixed_checks,*local_checks];
{cases:len(checks),matched:len([for(c in checks where c.matched)c]),guarded:len([for(c in checks where c.guarded)c]),failures:[for(c in checks where not c.ok)c]}
let public_checks=[for(index in [3,6]) {
 let reference=native.cases[index];
 let tex=parse(reference.source,{type:"tex"})^;
 let latex=parse(reference.source,{type:"latex"})^;
 {tex:parse(format(tex,"json")^,{type:"json"})^==reference.tex,
  latex:native_snapshot.tree_matches(latex,reference,reference.source)^}
}];
{public_cases:len(public_checks),matches:all([for(c in public_checks)c.tex and c.latex])}
