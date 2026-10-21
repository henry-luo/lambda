// The motion converters against independent implementations (Lambda_Pkg_Motion.md
// §10): the pinned Three.js 0.186.1 BVHLoader, FBXLoader and SkeletonUtils, and
// ufbx v0.23.1. test/demo/scene3d/assets/motion/_oracle.mjs wrote their poses to
// oracle.json; every comparison is in character metres.
import motion: lambda.scene3d.motion
import t: lambda.scene3d.transform

let dir = "test/demo/scene3d/assets/motion/"
let oracle = input(dir ++ "oracle.json")^
// 0.01 mm: both references compute in float32 in places
let close = 0.00001

fn origin(m) => [m[12], m[13], m[14]];
fn gap(a, b) => max([abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])]);
// Three.js drops characters such as ':' from node names
fn sanitized(n) => replace(replace(n, ":", ""), " ", "_");
fn key_at(k, seconds) => [for (i in 0 to len(k.times) - 1 where abs(k.times[i] - seconds) < 0.000001) i][0];
fn world_at(c, clip_name, seconds) {
    let k = if (clip_name == null) null else [for (x in motion.clips_of(c) where x.name == clip_name) x][0];
    if (k == null) motion.pose(c)^ else motion.pose(c, clip_name, key_at(k, seconds))^
}
// the largest joint difference over the joints the reference names, or null when it names none
fn joint_drift(c, clip_name, seconds, want, renamed) {
    let world = world_at(c, clip_name, seconds)
    let gaps = [for (j in motion.joints(c)) for (w in [want[if (renamed) sanitized(j.name) else j.name]] where w != null)
        gap(origin(world[j.name]), w)];
    if (len(gaps) == 0) null else max(gaps)
}
fn mm(x) => int(round(x * 1000)) ^ { 0 };
// references key skinned vertices by their bind position in millimetres; allow a rounding step
fn lookup(want, p) {
    let exact = want[string(mm(p[0])) ++ "," ++ string(mm(p[1])) ++ "," ++ string(mm(p[2]))];
    if (exact != null) exact
    else [for (dx in [-1, 0, 1]) for (dy in [-1, 0, 1]) for (dz in [-1, 0, 1])
        for (hit in [want[string(mm(p[0]) + dx) ++ "," ++ string(mm(p[1]) + dy) ++ "," ++ string(mm(p[2]) + dz)]] where hit != null) hit][0]
}
// [vertices compared, largest difference]; vertices with more than four influences are not referenced
fn skin_drift(c, clip_name, seconds, want) {
    let world = world_at(c, clip_name, seconds)
    let js = motion.joints(c)
    let gs = [for (m in content(motion.body_of(c)) where name(m) == 'mesh') for (g in content(m) where name(g) == 'geometry' and g["skin-indices"] != null) g]
    let gaps = [for (g in gs) for (v in 0 to len(g.positions) / 3 - 1)
        for (w in [lookup(want, [g.positions[3 * v], g.positions[3 * v + 1], g.positions[3 * v + 2]])] where w != null)
            (let p = [g.positions[3 * v], g.positions[3 * v + 1], g.positions[3 * v + 2]],
             let parts = [for (s in 0 to 3 where g["skin-weights"][4 * v + s] > 0)
                (let j = js[int(g["skin-indices"][4 * v + s])],
                 t.point(t.multiply(world[j.name], j.inverse_bind)^, p)^ * g["skin-weights"][4 * v + s])],
             gap([sum([for (x in parts) x[0]]), sum([for (x in parts) x[1]]), sum([for (x in parts) x[2]])], w))];
    [len(gaps), max(gaps) < close]
}
fn agrees(drifts) => all([for (d in drifts) d != null and d < close]);
fn id_of(cs) => cs[0] ++ (if (cs[1] == null) "" else "#" ++ cs[1]);

"1 BVH agrees with BVHLoader, whose position channels add to OFFSET";
let jump_bvh = motion.load(dir ++ "jump.bvh", {scale: 0.01})^
let offsets_bvh = motion.load(dir ++ "offsets.bvh", {scale: 0.01})^;
[agrees([for (s, p in oracle.three.bvh.jump) joint_drift(jump_bvh, "jump", float(s), p, false)]),
 agrees([for (s, p in oracle.three.bvh.offsets) joint_drift(offsets_bvh, "offsets", float(s), p, false)])];

"2 FBX joints agree with ufbx in every case";
let cases = [["jump.fbx", "jump"], ["jump-ascii.fbx", "jump"], ["jump-7500.fbx", "jump"], ["rig.fbx", "curl"], ["rig.fbx", "wave"],
    ["rig.fbx", "sway"], ["kenney/characterMedium.fbx", null], ["kenney/jump.fbx", "Root|Jump"]]
let loaded = map([for (f in unique([for (cs in cases) cs[0]])) (f, motion.load(dir ++ f)^)]);
[for (cs in cases) [id_of(cs), agrees([for (s, p in oracle.ufbx[id_of(cs)].nodes) joint_drift(loaded[cs[0]], cs[1], float(s), p, false)])]];

"3 and with FBXLoader where it evaluates the same curves";
[for (cs in cases where cs[0] != "rig.fbx") [id_of(cs), agrees([for (s, p in oracle.three.fbx[id_of(cs)]) joint_drift(loaded[cs[0]], cs[1], float(s), p, true)])]];
// FBXLoader keys every curve linearly and re-slerps Euler turns: it ignores
// constant and cubic keys, which ufbx and the converter honour
fn mid(cs) => if (cs[1] == "sway") "0.7" else "0.5";
[for (cs in cases where cs[0] == "rig.fbx")
    [id_of(cs), joint_drift(loaded[cs[0]], cs[1], float(mid(cs)), oracle.three.fbx[id_of(cs)][mid(cs)], true) > 0.001]];

"4 skinned vertices agree with ufbx and FBXLoader";
[for (cs in cases where motion.body_of(loaded[cs[0]]) != null)
    [id_of(cs), [for (s, w in oracle.ufbx[id_of(cs)].skin) skin_drift(loaded[cs[0]], cs[1], float(s), w)]]];
[for (cs in cases where cs[0] != "rig.fbx" and oracle.three.skinned[id_of(cs)] != null)
    [id_of(cs), [for (s, w in oracle.three.skinned[id_of(cs)]) skin_drift(loaded[cs[0]], cs[1], float(s), w)]]];

"5 a rigid prop rides its joint as ufbx places it";
let rig = loaded["rig.fbx"]
let prop = [for (m in content(motion.body_of(rig)) where name(m) == 'mesh' and m.name == "Prop") content(m)[0]][0]
fn prop_drift(clip_name, seconds, want) {
    let world = world_at(rig, clip_name, seconds)
    let js = motion.joints(rig);
    max([for (v in 0 to 3) (let p = [prop.positions[3 * v], prop.positions[3 * v + 1], prop.positions[3 * v + 2]],
        let j = js[int(prop["skin-indices"][4 * v])],
        gap(t.point(t.multiply(world[j.name], j.inverse_bind)^, p)^, want[v]))])
}
[for (s, w in oracle.ufbx["rig.fbx#curl"].static) prop_drift("curl", float(s), w.Prop) < close];

"6 retargeting agrees with SkeletonUtils given world-change-from-rest offsets";
let cmu = motion.load(dir ++ "jump.bvh", {scale: 0.01, rig: 'cmu'})^
let apose = motion.load(dir ++ "apose.fbx", {rig: 'mixamo'})^
// the A-pose is one rig with the jump, so attach would copy; call retarget directly
let moved = motion.retarget(motion.clips_of(cmu)[0], cmu, apose)^
let bent = <character source: 'fbx', units: 'm', *[motion.skeleton_of(apose), moved]>;
agrees([for (s, p in oracle.three.retarget["apose#jump"]) joint_drift(bent, "jump", float(s), p, true)])
