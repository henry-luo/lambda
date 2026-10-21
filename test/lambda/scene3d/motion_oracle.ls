// The motion converters against independent implementations (Lambda_Pkg_Motion.md
// §10): the pinned Three.js 0.186.1 BVHLoader, FBXLoader and SkeletonUtils, and
// ufbx v0.23.1. test/demo/scene3d/assets/motion/_oracle.mjs wrote their poses to
// oracle.json; every comparison is in character metres.
import motion: lambda.scene3d.motion
import t: lambda.scene3d.transform
import o: .mod_motion_oracle

let dir = "test/demo/scene3d/assets/motion/"
let oracle = input(dir ++ "oracle.json")^

"1 BVH agrees with BVHLoader, whose position channels add to OFFSET";
let jump_bvh = motion.load(dir ++ "jump.bvh", {scale: 0.01})^
let offsets_bvh = motion.load(dir ++ "offsets.bvh", {scale: 0.01})^;
[o.agrees([for (s, p in oracle.three.bvh.jump) o.joint_drift(jump_bvh, "jump", float(s), p, false)]),
 o.agrees([for (s, p in oracle.three.bvh.offsets) o.joint_drift(offsets_bvh, "offsets", float(s), p, false)])];

"2 FBX joints agree with ufbx in every case";
let cases = [["jump.fbx", "jump"], ["jump-ascii.fbx", "jump"], ["jump-7500.fbx", "jump"], ["rig.fbx", "curl"], ["rig.fbx", "wave"],
    ["rig.fbx", "sway"], ["kenney/characterMedium.fbx", null], ["kenney/jump.fbx", "Root|Jump"],
    ["cube-pets/animal-cat.fbx", "walk"], ["cube-pets/animal-cat.fbx", "dance"],
    ["robot/Robot.fbx", null], ["robot/Robot.fbx", "RobotArmature|Robot_Dance"], ["robot/Robot.fbx", "RobotArmature|Robot_Wave"]]
// FBXLoader keys every curve linearly, so files whose clips need cubic keys are compared with ufbx only
fn curved(cs) => cs[0] == "rig.fbx" or starts_with(cs[0], "cube-pets")
let loaded = map([for (f in unique([for (cs in cases) cs[0]])) (f, motion.load(dir ++ f)^)]);
[for (cs in cases) [o.id_of(cs), o.agrees([for (s, p in oracle.ufbx[o.id_of(cs)].nodes) o.joint_drift(loaded[cs[0]], cs[1], float(s), p, false)])]];

"3 and with FBXLoader where it evaluates the same curves";
[for (cs in cases where not curved(cs)) [o.id_of(cs), o.agrees([for (s, p in oracle.three.fbx[o.id_of(cs)]) o.joint_drift(loaded[cs[0]], cs[1], float(s), p, true)])]];
// FBXLoader keys every curve linearly and re-slerps Euler turns: it ignores
// constant and cubic keys, which ufbx and the converter honour
fn mid(cs) => if (cs[1] == "sway") "0.7" else "0.5";
[for (cs in cases where cs[0] == "rig.fbx")
    [o.id_of(cs), o.joint_drift(loaded[cs[0]], cs[1], float(mid(cs)), oracle.three.fbx[o.id_of(cs)][mid(cs)], true) > 0.001]];

"4 skinned vertices agree with ufbx and FBXLoader";
[for (cs in cases where motion.body_of(loaded[cs[0]]) != null and not starts_with(cs[0], "cube-pets"))
    [o.id_of(cs), [for (s, w in oracle.ufbx[o.id_of(cs)].skin) o.skin_drift(loaded[cs[0]], cs[1], float(s), w, 1)]]];
[for (cs in cases where not curved(cs) and oracle.three.skinned[o.id_of(cs)] != null)
    [o.id_of(cs), [for (s, w in oracle.three.skinned[o.id_of(cs)]) o.skin_drift(loaded[cs[0]], cs[1], float(s), w, 1)]]];

"5 a rigid prop rides its joint as ufbx places it";
let rig = loaded["rig.fbx"]
let prop = [for (m in content(motion.body_of(rig)) where name(m) == 'mesh' and m.name == "Prop") content(m)[0]][0]
fn prop_drift(clip_name, seconds, want) {
    let world = o.world_at(rig, clip_name, seconds)
    let js = motion.joints(rig);
    max([for (v in 0 to 3) (let p = [prop.positions[3 * v], prop.positions[3 * v + 1], prop.positions[3 * v + 2]],
        let j = js[int(prop["skin-indices"][4 * v])],
        o.gap(t.point(t.multiply(world[j.name], j.inverse_bind)^, p)^, want[v]))])
}
[for (s, w in oracle.ufbx["rig.fbx#curl"].static) o.within(prop_drift("curl", float(s), w.Prop))];

"6 retargeting agrees with SkeletonUtils given world-change-from-rest offsets";
let cmu = motion.load(dir ++ "jump.bvh", {scale: 0.01, rig: 'cmu'})^
let apose = motion.load(dir ++ "apose.fbx", {rig: 'mixamo'})^
// the A-pose is one rig with the jump, so attach would copy; call retarget directly
let moved = motion.retarget(motion.clips_of(cmu)[0], cmu, apose)^
let bent = <character source: 'fbx', units: 'm', *[motion.skeleton_of(apose), moved]>;
o.agrees([for (s, p in oracle.three.retarget["apose#jump"]) o.joint_drift(bent, "jump", float(s), p, true)]);

"7 Kenney's Cube Pets: a rigid rig with weighted tangents, each part placed as ufbx places it";
[for (cs in cases where starts_with(cs[0], "cube-pets"))
    [o.id_of(cs), o.agrees([for (s, w in oracle.ufbx[o.id_of(cs)].static) o.rigid_drift(loaded[cs[0]], cs[1], float(s), w, 3)])]]
