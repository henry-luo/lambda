// text-mode control symbols paint literal characters while preserving TeX source.
import math: lambda.doc.math.math
import snapshot: .mod_svg_snapshot

let cases = [
    {raw: "price: \\$5; rate: 50\\%; key: a\\_b; A\\&B", text: "price: $5; rate: 50%; key: a_b; A&B"},
    {raw: "\\$ \\% \\# \\_ \\& \\{ \\}", text: "$ % # _ & { }"}
];
let commands = ["text", "textrm", "textbf", "textit", "textsf", "texttt", "textsc", "emph", "mbox", "hbox"];
let checks = [for (command in commands, entry in cases) {
    let source = "\\" ++ command ++ "{" ++ entry.raw ++ "}";
    let ast = parse(source, 'math')^;
    let actual = math.render_box(ast)^;
    let literal = math.render_box(<math <text_command cmd: "\\" ++ command, content: entry.text>>)^;
    let serialized = format(ast, {type: "math", flavor: "latex"})^;
    [
        {name: command ++ " decodes escaped text", ok: ast[0].content == entry.text},
        {name: command ++ " paints literal punctuation", ok: snapshot.painted_text(actual.element) == entry.text},
        {name: command ++ " measures decoded glyphs", ok:
            actual.width == literal.width and actual.height == literal.height and actual.depth == literal.depth},
        {name: command ++ " preserves escaped source", ok: serialized == source},
        {name: command ++ " renders after roundtrip", ok:
            snapshot.painted_text(math.render_math(parse(serialized, 'math')^)^) == entry.text}
    ]
}];
let unchanged = [for (source in ["  a  b  ", "\\unknown", "\\\\$"]) {
    let ast = parse("\\text{" ++ source ++ "}", 'math')^;
    {name: "preserves undecoded text: " ++ source, ok:
        ast[0].content == source and snapshot.painted_text(math.render_math(ast)^) == source}
}];
[for (check in [*[for (group in checks, item in group) item], *unchanged] where check.ok != true) check.name]
