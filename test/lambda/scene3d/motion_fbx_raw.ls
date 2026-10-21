// Motion M2: binary and ASCII FBX map onto one raw <fbx> record tree
// (Lambda_Pkg_Motion.md §4). The fixture generator writes the same jump as
// binary 7.4 with raw arrays, binary 7.5 with zlib arrays, and ASCII 7.4; the
// three trees agree once numeric kinds are set aside.
let dir = "test/demo/scene3d/assets/motion/"
let raw74 = input(dir ++ "jump.fbx")^
let raw75 = input(dir ++ "jump-7500.fbx")^
let text = input(dir ++ "jump-ascii.fbx")^

// ASCII spells embedded bytes as base64, which the JSON writer also produces
fn plain(v) {
    if (v is number) float(v)
    else if (v is array) [for (x in v) plain(x)]
    else if (v is binary) parse(format(v, 'json'), 'json') ^ { null }
    else v
}
fn tree(e) => [name(e), plain(e.props), [for (x in content(e)) tree(x)]];
fn body(f) => [for (x in content(f) where name(x) != 'FBXHeaderExtension') tree(x)];
fn records(f, tag) => [for (x in content(f) where name(x) == 'Objects') for (o in content(x) where name(o) == tag) o];
fn version(f) => [for (x in content(f) where name(x) == 'FBXHeaderExtension') for (v in content(x) where name(v) == 'FBXVersion') v.props[0]][0];

"1 each file keeps its header";
[for (f in [raw74, raw75, text]) [name(f), f.binary, f.version, version(f)]];

"2 the three trees agree modulo numeric kinds";
[body(raw74) == body(raw75), body(raw74) == body(text)];

"3 records keep their order and typed properties";
let curve74 = records(raw74, 'AnimationCurve')[0]
let curve_text = records(text, 'AnimationCurve')[0];
[len(records(raw74, 'Model')), len(records(raw74, 'AnimationCurve')),
    [for (m in records(raw74, 'Model')) m.props[1]][0 to 2],
    [for (r in content(curve74)) name(r)]];
[for (r in content(curve74)) type(r.props[0])];
[for (r in content(curve_text)) type(r.props[0])];
[type(curve74.props[0]), type(curve_text.props[0]),
    type(content(curve74)[2].props[0][1]), type(content(curve74)[3].props[0][1]),
    type(content(curve_text)[3].props[0][1])];

"4 empty strings are null and names use the ASCII spelling";
[records(raw74, 'AnimationCurve')[0].props, records(raw74, 'NodeAttribute')[0].props];

"5 malformed ASCII is rejected at the parser";
let head = "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n FBXVersion: 7400\n}\n";
[len(content(parse(head ++ "A: *2 {\n a: 1,2.5\n}\n", 'fbx')^)),
 (parse(head ++ "A: *3 {\n a: 1,2\n}\n", 'fbx') ^ { "short array" }),
 (parse(head ++ "A: 1.#INF\n", 'fbx') ^ { "non-finite" }),
 (parse("A: 1\n", 'fbx') ^ { "no version" }),
 (parse(replace(head, "7400", "6100"), 'fbx') ^ { "FBX 6" })]
