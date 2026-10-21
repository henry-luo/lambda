// Local-only Mixamo acceptance (Lambda_Pkg_Motion.md §11): Mixamo exports are
// never committed, so this script is not a discovered test. The fixtures
// README lists the steps: fetch the two Mixamo exports Three.js r186 ships into
// temp/mixamo/, write their reference poses there with _oracle.mjs, then run
// this script. Every line should read true.
import motion: lambda.scene3d.motion
import o: .mod_motion_oracle

let dir = "temp/mixamo/"
let oracle = input(dir ++ "oracle.json")^
let files = ["mixamo.fbx", "Samba Dancing.fbx"]
let cases = [for (f in files) for (clip in [null, "mixamo.com"]) [f, clip]]
let loaded = map([for (f in files) (f, motion.load(dir ++ f, {rig: 'mixamo'})^)]);

"1 both characters load with their clips and the Mixamo humanoid map";
[for (f in files) (let c = loaded[f],
    [f, len(motion.joints(c)), [for (k in motion.clips_of(c)) k.name], len([for (j in motion.joints(c) where j.humanoid != null) j])])];

"2 joints agree with ufbx and with FBXLoader at the clip's keys";
[for (cs in cases) [o.id_of(cs),
    o.agrees([for (s, p in oracle.ufbx[o.id_of(cs)].nodes) o.joint_drift(loaded[cs[0]], cs[1], float(s), p, false)]),
    o.agrees([for (s, p in oracle.three.fbx[o.id_of(cs)]) o.joint_drift(loaded[cs[0]], cs[1], float(s), p, true)])]];

"3 skinned vertices agree with ufbx and FBXLoader (every 25th vertex)";
fn skin(cs, reference) => all([for (s, w in reference) o.skin_drift(loaded[cs[0]], cs[1], float(s), w, 25)[1]]);
[for (cs in cases) [o.id_of(cs), skin(cs, oracle.ufbx[o.id_of(cs)].skin), skin(cs, oracle.three.skinned[o.id_of(cs)])]];

// the largest difference between a target's and a source's world rotation change
// from rest, over humanoid-mapped joints at every `stride`th key: zero when retargeted
fn turn(m, rest) => [for (r in 0 to 2) [for (c in 0 to 2) sum([for (k in 0 to 2) m[4 * k + r] * rest[4 * k + c]])]];
fn turn_drift(target, target_clip, source, source_clip, stride) {
    let roles = map([for (j in motion.joints(source) where j.humanoid != null) (string(j.humanoid), j.name)])
    let pairs = [for (j in motion.joints(target) where j.humanoid != null and roles[string(j.humanoid)] != null) [j.name, roles[string(j.humanoid)]]]
    let k = [for (x in motion.clips_of(target) where x.name == target_clip) x][0]
    let target_rest = motion.pose(target)^
    let source_rest = motion.pose(source)^;
    max([for (i in 0 to len(k.times) - 1 where i % stride == 0)
        (let a = motion.pose(target, target_clip, i)^, let b = motion.pose(source, source_clip, i)^,
         max([for (pair in pairs) (let x = turn(a[pair[0]], target_rest[pair[0]]), let y = turn(b[pair[1]], source_rest[pair[1]]),
             max([for (r in 0 to 2) for (c in 0 to 2) abs(x[r][c] - y[r][c])]))]))])
}
fn moves(c, clip_name, joint) {
    let k = [for (x in motion.clips_of(c) where x.name == clip_name) x][0];
    o.gap(motion.pose(c, clip_name, 0)^[joint][12 to 14], motion.pose(c, clip_name, len(k.times) div 2)^[joint][12 to 14]) > 0.01
}

"4 the character takes the samba; the two exports use different joint frames, so attach retargets";
let samba = motion.load(dir ++ "Samba Dancing.fbx", {rig: 'mixamo', clip: "mixamo.com"})^
let dancer = motion.attach(loaded["mixamo.fbx"], {samba: samba})^;
[turn_drift(dancer, "samba", samba, "mixamo.com", 25) < 0.000001, moves(dancer, "samba", "mixamorig:LeftHand")];

"5 a CMU-named jump retargets onto the Mixamo character";
let cmu = motion.load("test/demo/scene3d/assets/motion/jump.bvh", {scale: 0.01, rig: 'cmu'})^
let jumper = motion.attach(loaded["mixamo.fbx"], {leap: cmu})^;
[turn_drift(jumper, "leap", cmu, "jump", 1) < 0.000001, moves(jumper, "leap", "mixamorig:Hips")];

"6 the scene compiles and validates, ready to play";
let compiled = motion.scene(dancer, {id: "dancer", autoplay: "samba"})^;
[name(compiled), len([for (m in content(motion.body_of(dancer)) where name(m) == 'mesh') m])]
