// D10.2.2v2: preserve the native restart and authorized-format behavior.
import facade: lambda.doc.latex.tex_engine.facade
import profile: lambda.doc.latex.tex_engine.profile
let source = "\\ExplSyntaxOn\\immediate\\write16{LT:\\demo}\\ExplSyntaxOff";
let initial = profile.expand(source, {auto_expl3:true})^;
let format = "\\def\\ExplSyntaxOn{}\\def\\ExplSyntaxOff{}\\def\\demo{5}";
let result = facade.tex(source, {resources:{local:{}, bundled:{'expl3.ltx':{data:format}}}})^;
[initial.issue == "tex-expl3-restart", result.issue == null and result.root.messages == ["LT:5"]]
