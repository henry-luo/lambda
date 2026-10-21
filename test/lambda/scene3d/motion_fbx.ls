// Motion M3: FBX profile 1 converts into the common <character> schema
// (Lambda_Pkg_Motion.md §7.2, §7.4). The generator writes one jump as BVH,
// binary FBX 7.4/7.5 and ASCII FBX, plus a rig that exercises the transform
// chain, binding and baking cases; its own FBX evaluator is the reference.
import motion: lambda.scene3d.motion
import t: lambda.scene3d.transform

let dir = "test/demo/scene3d/assets/motion/"
let bvh = motion.load(dir ++ "jump.bvh", {scale: 0.01})^
let hero = motion.load(dir ++ "jump.fbx")^
let wide = motion.load(dir ++ "jump-7500.fbx")^
let text = motion.load(dir ++ "jump-ascii.fbx")^
let rig = motion.load(dir ++ "rig.fbx")^
let expect = input(dir ++ "expected-fbx.json")^

fn origin(m) => [m[12], m[13], m[14]];
fn mm(x) => string(int(round(x * 1000)));
fn key_of(p) => mm(p[0]) ++ "," ++ mm(p[1]) ++ "," ++ mm(p[2]);
fn meshes(c, label) => [for (m in content(motion.body_of(c)) where name(m) == 'mesh' and m.name == label) m];
fn vertex(g, v) => [g.positions[3 * v], g.positions[3 * v + 1], g.positions[3 * v + 2]];
// largest world-position difference over joints, between two characters at one key
fn drift(a, b, clip_name, key) {
    let wa = motion.pose(a, clip_name, key)^
    let wb = motion.pose(b, clip_name, key)^;
    max([for (k, m in wa) max([for (i in 0 to 2) abs(origin(m)[i] - origin(wb[k])[i])])])
}
fn joint_drift(c, clip_name, key, want) {
    let world = motion.pose(c, clip_name, key)^;
    max([for (k, p in want) max([for (i in 0 to 2) abs(origin(world[string(k)])[i] - p[i])])])
}
// linear blend skinning from the schema: each joint's world pose times its inverse bind
fn skinned(c, g, world, v) {
    let js = motion.joints(c)
    let parts = [for (k in 0 to 3 where g["skin-weights"][4 * v + k] > 0)
        (let j = js[int(g["skin-indices"][4 * v + k])],
         t.point(t.multiply(world[j.name], j.inverse_bind)^, vertex(g, v))^ * g["skin-weights"][4 * v + k])];
    [sum([for (x in parts) x[0]]), sum([for (x in parts) x[1]]), sum([for (x in parts) x[2]])]
}
fn skin_drift(c, label, clip_name, key, want) {
    let world = motion.pose(c, clip_name, key)^;
    max([for (m in meshes(c, label)) for (g in content(m)) for (v in 0 to len(g.positions) / 3 - 1)
        (let s = skinned(c, g, world, v), let w = want[key_of(vertex(g, v))],
         max([abs(s[0] - w[0]), abs(s[1] - w[1]), abs(s[2] - w[2])]))])
}
fn key_at(c, clip_name, seconds) {
    let k = [for (x in motion.clips_of(c) where x.name == clip_name) x][0];
    [for (i in 0 to len(k.times) - 1 where abs(k.times[i] - seconds) < 1e-9) i][0]
}

"1 one conversion from binary 7.4, binary 7.5 and ASCII FBX";
[for (c in [hero, wide, text]) [motion.validate(c)^, c.source, c.units, len(motion.joints(c)),
    [for (k in motion.clips_of(c)) [k.name, k.rate, k.duration, len(k.times), len(content(k))]]]];

"2 the three FBX files and the BVH agree at every key";
[for (other in [bvh, wide, text]) max([for (key in 0 to 30) drift(hero, other, "jump", key)]) < 0.000001];

"3 the body splits by material, embeds its texture and skins every vertex";
let body = motion.body_of(hero)
let resources = content(body)[0]
let face = meshes(hero, "Body")[1]
let weights = [for (m in meshes(hero, "Body")) for (g in content(m)) for (v in 0 to len(g.positions) / 3 - 1)
    sum([for (k in 0 to 3) g["skin-weights"][4 * v + k]])];
[for (m in meshes(hero, "Body")) [m.material, len(content(m)[0].positions) / 3, len(content(m)[0].indices) / 3]];
[for (r in content(resources)) [name(r), r.id, r.color, r.texture]];
[starts_with([for (r in content(resources) where name(r) == 'texture') r.src][0], "data:image/png;base64,"),
    len(content(face)[0].uvs), max([for (w in weights) abs(w - 1)]) < 1e-9];

"4 skinned vertices match the reference";
[skin_drift(hero, "Body", "jump", 0, expect.jump_skinned["0"]) < 0.000001,
    skin_drift(hero, "Body", "jump", 15, expect.jump_skinned["15"]) < 0.000001];

"5 pivots, pre- and post-rotation, rotation orders, scaling and Z-up inches";
[for (k in motion.clips_of(rig)) [k.name, len(k.times), len(content(k))]];
[motion.clips_of(rig)[1].times, joint_drift(rig, null, 0, expect.rig.rest) < 0.000001,
    joint_drift(rig, "curl", key_at(rig, "curl", 0.5), expect.rig.curl_half) < 0.000001,
    joint_drift(rig, "wave", key_at(rig, "wave", 0.5), expect.rig.wave_half) < 0.000001];
[for (j in motion.joints(rig)) [j.name, if (j.scale == null) null else [for (x in j.scale) round(x * 1000000) / 1000000]]];

"6 rest differs from bind, five influences keep four, a prop rides the hand";
let names = [for (j in motion.joints(rig)) j.name]
let crowded = [for (m in meshes(rig, "Body")) for (g in content(m)) for (v in 0 to len(g.positions) / 3 - 1
    where g["skin-weights"][4 * v + 3] > 0) [for (k in 0 to 3) names[int(g["skin-indices"][4 * v + k])]]]
let prop = content(meshes(rig, "Prop")[0])[0]
fn prop_drift(clip_name, key, want) {
    let world = motion.pose(rig, clip_name, key)^;
    max([for (v in 0 to 3) (let s = skinned(rig, prop, world, v), max([for (i in 0 to 2) abs(s[i] - want[v][i])]))])
}
[skin_drift(rig, "Body", null, 0, expect.rig.rest_skinned) < 0.000001,
    skin_drift(rig, "Body", "curl", key_at(rig, "curl", 0.5), expect.rig.curl_half_skinned) < 0.000001];
[crowded[0], unique([for (v in 0 to 3) names[int(prop["skin-indices"][4 * v])]]),
    prop_drift(null, 0, expect.rig.prop_rest) < 0.000001,
    prop_drift("curl", key_at(rig, "curl", 0.5), expect.rig.prop_curl_half) < 0.000001];

"7 the scene names bones, prefixes body resources and skins each mesh";
let group = motion.scene(hero, {id: "hero", autoplay: "jump"})^;
[for (x in content(group)) [name(x), x.id, x.skeleton, x.material]];

"8 malformed FBX is rejected with a specific diagnostic";
let head = "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n FBXVersion: 7400\n}\n"
fn fbx(objects, links) => head ++ "Objects:  {\n" ++ objects ++ "}\nConnections:  {\n" ++ links ++ "}\n";
fn fbx_with(settings, objects, links) => head ++ "GlobalSettings:  {\n Properties70:  {\n" ++ settings ++ " }\n}\n" ++
    "Objects:  {\n" ++ objects ++ "}\nConnections:  {\n" ++ links ++ "}\n";
fn p(name, value) => "  P: \"" ++ name ++ "\", \"enum\", \"\", \"\"," ++ value ++ "\n";
fn limb(id, label, props) => " Model: " ++ string(id) ++ ", \"Model::" ++ label ++ "\", \"LimbNode\" {\n Properties70:  {\n" ++ props ++ " }\n }\n";
fn oo(child, parent) => " C: \"OO\"," ++ string(child) ++ "," ++ string(parent) ++ "\n";
fn floats(values) => "*" ++ string(len(values)) ++ " {\n a: " ++ join([for (v in values) string(v)], ",") ++ "\n }";
// a one-triangle mesh skinned to joint 1 with a joint bind translated by `x`,
// its cluster matrices given inline or left to the BindPose
fn skinned_mesh(id, x, inline = true) => " Geometry: " ++ string(id) ++ ", \"Geometry::G\", \"Mesh\" {\n  Vertices: " ++ floats([0, 0, 0, 1, 0, 0, 0, 1, 0]) ++
    "\n  PolygonVertexIndex: " ++ floats([0, 1, -3]) ++ "\n }\n Model: " ++ string(id + 1) ++ ", \"Model::M" ++ string(id) ++ "\", \"Mesh\" {\n }\n" ++
    " Deformer: " ++ string(id + 2) ++ ", \"Deformer::Skin\", \"Skin\" {\n }\n Deformer: " ++ string(id + 3) ++ ", \"SubDeformer::A\", \"Cluster\" {\n" ++
    "  Indexes: " ++ floats([0, 1, 2]) ++ "\n  Weights: " ++ floats([1, 1, 1]) ++
    (if (inline) "\n  Transform: " ++ floats([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]) ++
        "\n  TransformLink: " ++ floats([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, 0, 0, 1]) else "") ++ "\n }\n";
fn bind_pose(x) => " Pose: 30, \"Pose::BIND_POSES\", \"BindPose\" {\n  Type: \"BindPose\"\n" ++
    "  PoseNode:  {\n   Node: 1\n   Matrix: " ++ floats([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, 0, 0, 1]) ++ "\n  }\n" ++
    "  PoseNode:  {\n   Node: 11\n   Matrix: " ++ floats([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]) ++ "\n  }\n }\n";
fn mesh_links(id) => oo(id, id + 1) ++ oo(id + 1, 0) ++ oo(id + 2, id) ++ oo(id + 3, id + 2) ++ oo(1, id + 3);
let posed = parse(fbx(limb(1, "A", "") ++ skinned_mesh(10, 2, false) ++ bind_pose(2), oo(1, 0) ++ mesh_links(10)),
    {type: 'character', flavor: 'fbx'})^
fn rejects(source, diagnostic) => parse(source, {type: 'character', flavor: 'fbx'}) ^ { contains(string(^.message), diagnostic) ^ { false } } ~ { "accepted" };
[motion.validate(parse(fbx(limb(1, "A", ""), oo(1, 0)), {type: 'character', flavor: 'fbx'})^)^,
 motion.validate(posed)^ and abs(motion.joints(posed)[0].inverse_bind[12] + 0.02) < 1e-12,
 rejects(fbx(limb(1, "A", p("InheritType", "2")), oo(1, 0)), "InheritType other than RSrs"),
 rejects(fbx(limb(1, "A", p("RotationOrder", "6")), oo(1, 0)), "spheric XYZ is unsupported"),
 rejects(fbx(limb(1, "A", ""), oo(1, 9)), "connection names a missing object"),
 rejects(fbx(limb(1, "A", "") ++ limb(2, "B", ""), oo(1, 2) ++ oo(2, 1)), "cyclic"),
 rejects(fbx(limb(1, "A", "") ++ limb(2, "A", ""), oo(1, 0) ++ oo(2, 1)), "duplicate joint name 'A'"),
 rejects(fbx(" Model: 1, \"Model::Box\", \"Mesh\" {\n }\n", oo(1, 0)), "no skeleton"),
 rejects(fbx(limb(1, "A", "") ++ skinned_mesh(10, 0) ++ skinned_mesh(20, 5), oo(1, 0) ++ mesh_links(10) ++ mesh_links(20)),
    "meshes bind joint with different poses"),
 rejects(fbx_with(p("UnitScaleFactor", "0"), limb(1, "A", ""), oo(1, 0)), "UnitScaleFactor must be positive"),
 rejects(fbx_with(p("UpAxis", "0"), limb(1, "A", ""), oo(1, 0)), "not a permutation"),
 rejects(fbx(limb(1, "A", "") ++ " AnimationStack: 5, \"AnimStack::bad\", \"\" {\n Properties70:  {\n" ++
    "  P: \"LocalStart\", \"KTime\", \"Time\", \"\",10\n  P: \"LocalStop\", \"KTime\", \"Time\", \"\",5\n }\n }\n", oo(1, 0)),
    "clip stops before it starts")]
