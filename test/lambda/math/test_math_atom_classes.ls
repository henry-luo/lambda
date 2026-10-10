import math: lambda.doc.math.math
import svg: .mod_svg_snapshot

fn ast(source) => parse(source, {type:"math", flavor:"latex"})^
fn box(source) => math.render_box(ast(source))^
fn close(a,b) => abs(a - b) < 0.00001
let classes = ["ord", "bin", "rel", "open", "close", "punct", "inner"]
let wrapped = [for (kind in classes) box("\\math" ++ kind ++ "{x}")]
let source = "a\\mathrel{\\mathop{=}\\limits^{?}}b"
let roundtrip = box(format(ast(source), {type:"math", flavor:"latex"})^)
let checks = [
    {name:"atom wrappers paint only their body", ok:all([for (b in wrapped) svg.painted_text(b.element) == "x"])},
    {name:"all seven atom classes survive parsing", ok:all([for (kind in classes)
        content(ast("\\math" ++ kind ++ "{x}"))[0].atom == "m" ++ kind])},
    {name:"mathbin uses binary spacing", ok:close(box("a\\mathbin{+}b").width,box("a+b").width)},
    {name:"mathrel uses relation spacing", ok:close(box("a\\mathrel{=}b").width,box("a=b").width)},
    {name:"mathord suppresses relation spacing", ok:box("a\\mathord{=}b").width < box("a=b").width},
    {name:"unary binary atom is normalized", ok:close(box("\\mathbin{+}b").width,box("+b").width)},
    {name:"nested operator limits keep their annotation", ok:svg.painted_text(box(source).element) == "a=?b"},
    {name:"formatter retains wrapper and operator bodies", ok:svg.painted_text(roundtrip.element) == "a=?b" and close(roundtrip.width,box(source).width)}
];
[for (check in checks where check.ok != true) check.name]
