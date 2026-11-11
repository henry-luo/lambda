import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let root = "test/lambda/latex/fixtures/local_packages/";
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn parsed(name) any^ {
 let source=input(root ++ name ++ ".tex", "text")^;
 let style=input(root ++ name ++ ".sty", "text")^;
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true, resources:{local:map([name ++ ".sty", {data:style}]),bundled:{}}},100000)^;
 {native:input(root ++ name ++ ".tex", "latex")^, script:digest.digest(r)^}
}
fn offsets(node) any^ => if (not (node is element)) [] else [*(if (contains(["textbf", "textit"], string(name(node)))) [[string(name(node)), node.source_offset]] else []), *[for (child in node) *offsets(child)^]]
let case=parsed("script_provenance")^;
let want=offsets(case.native)^;
let got=if (case.script.ast == null) [] else offsets(case.script.ast)^;
"foreign formatting:"; {issue:case.script.issue, count:len(got), native_offsets:want, script_offsets:got,
 matches_native:len(want) == 3 and got == want}
fn generic_offsets(node) any^ => if (not (node is element)) [] else [*(if (string(name(node)) == "mystery") [node.source_offset] else []), *[for (child in node) *generic_offsets(child)^]]
let foreign=parsed("script_foreign")^;
"foreign generic source mapping pending:"; {native_offsets:generic_offsets(foreign.native)^,
 issue:foreign.script.issue, unsupported:foreign.script.unsupported}
