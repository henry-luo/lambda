import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let empty={local:{},bundled:{}};
let available={local:{},bundled:{'demo.sty':{data:"\\def\\foo{OK}"},'expl3.sty':{data:"\\def\\ExplSyntaxOn{OK}"}}};
let unchanged={local:{},bundled:{'demo.sty':{data:"\\relax"}}};
fn run(source,resources,script_expl3=false) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:script_expl3,echo_definitions:false,adapters:[],resources:resources},5000)^;
 let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,expansions:r.expansions,text:b.text}
}
fn stops(source,resources) any^ {
 let r=run(source,resources)^; r.issue!=null and r.expansions<50
}
fn finishes(source,resources,expected,script_expl3=false) any^ {
 let r=run(source,resources,script_expl3)^; r.issue==null and r.text==expected
}
[stops("\\ExplSyntaxOn",empty)^,
 stops("\\ProvidesExplPackage{x}{d}{v}{i}",empty)^,
 stops("\\def\\foo{\\RequirePackage{demo}\\foo}\\foo",empty)^,
 stops("\\def\\foo{\\RequirePackage{demo}\\foo}\\foo",unchanged)^,
 stops("\\let\\alias\\ExplSyntaxOn\\alias",empty)^,
 finishes("\\RequirePackage{expl3}X",empty,"\\RequirePackage{expl3}X ")^,
 finishes("\\def\\ExplSyntaxOn{OK}\\ExplSyntaxOn",empty,"OK")^,
 finishes("\\ExplSyntaxOn",available,"OK")^,
 finishes("\\def\\foo{\\RequirePackage{demo}\\foo}\\foo",available,"OK")^,
 finishes("\\def\\RequirePackage#1{\\def\\foo{OK}}\\def\\foo{\\RequirePackage{demo}\\foo}\\foo",empty,"OK")^,
 finishes("\\ExplSyntaxOn A\\ExplSyntaxOff",empty,"A",true)^]
