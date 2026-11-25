// D10.2.2v2: preserve the native restart and authorized-format behavior.
import facade: lambda.doc.latex.tex_engine.facade
import profile: lambda.doc.latex.tex_engine.profile
let source = "\\ExplSyntaxOn\\immediate\\write16{LT:\\demo}\\ExplSyntaxOff";
let initial = profile.expand(source, {auto_expl3:true})^;
let format = "\\def\\ExplSyntaxOn{}\\def\\ExplSyntaxOff{}\\def\\demo{5}";
let result = facade.tex(source, {resources:{local:{}, bundled:{'expl3.ltx':{data:format}}}})^;
let bytes = facade.tex(source, {resources:{local:{}, bundled:{'expl3.ltx':{data:binary(format)^}}}})^;
let missing = facade.tex(source, {resources:{local:{}, bundled:{'expl3.ltx':{missing:true}}}})^;
let isolated = facade.tex(source, {resources:{local:{}, bundled:{}}})^;
let nested = facade.tex(source, {resources:{local:{'part.tex':{data:"\\def\\demo{9}"}},
    bundled:{'expl3.ltx':{data:"\\def\\ExplSyntaxOn{}\\def\\ExplSyntaxOff{}\\input{part}"},
        'part.tex':{data:"\\def\\demo{5}"}}}})^;
[
    initial.issue == "tex-expl3-restart",
    result.issue == null and result.root.messages == ["LT:5"],
    bytes.issue == null and bytes.root.messages == ["LT:5"],
    missing.issue == "tex-expl3-unavailable",
    isolated.issue == null and not contains(isolated.root.messages,"LT:5"),
    nested.issue == null and nested.root.messages == ["LT:5"]
]
