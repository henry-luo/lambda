// negation keeps parsed commands and their measured relation boxes.
import math: lambda.doc.math.math
import util: lambda.doc.math.util
import snapshot: .mod_svg_snapshot

let relations = [
    {source: "\\subset", glyph: "⊂"}, {source: "\\supset", glyph: "⊃"},
    {source: "\\subseteq", glyph: "⊆"}, {source: "\\supseteq", glyph: "⊇"},
    {source: "\\sqsubseteq", glyph: "⊑"}, {source: "\\sqsupseteq", glyph: "⊒"},
    {source: "\\in", glyph: "∈"}, {source: "\\leq", glyph: "≤"},
    {source: "\\sim", glyph: "∼"}, {source: "\\rightarrow", glyph: "→"},
    {source: "=", glyph: "="}, {source: "<", glyph: "<"}, {source: ">", glyph: ">"}
];
let group_checks = [for (relation in relations) {
    let ast = parse("\\not" ++ relation.source, 'math')^;
    let plain = math.render_box(parse(relation.source, 'math')^)^;
    let negated = math.render_box(ast)^;
    let serialized = format(ast, {type: "math", flavor: "latex"})^;
    [
        {name: relation.source ++ " glyph and slash", ok: snapshot.painted_text(negated.element) == relation.glyph ++ "/"},
        {name: relation.source ++ " relation advance", ok: negated.width == plain.width and negated.type == "mrel"},
        {name: relation.source ++ " survives LaTeX roundtrip", ok:
            snapshot.painted_text(math.render_math(parse(serialized, 'math')^)^) == relation.glyph ++ "/"},
        {name: relation.source ++ " remains in accessible title", ok:
            util.text_of(content(negated.element)[0]) == serialized}
    ]
}];
let full = parse("A\\not\\subset B", 'math')^;
let target = full[1].target;
let checks = [*group_checks, [
    {name: "command negation retains a parsed target", ok: target is element and target.name == "subset"},
    {name: "empty negation survives formatting", ok:
        format(parse("\\not{}", 'math')^, {type: "math", flavor: "latex"})^ == "\\not{}"}
]];
[for (group in checks, check in group where check.ok != true) check.name]
