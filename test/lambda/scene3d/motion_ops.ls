// Motion M4: humanoid roles, attaching and retargeting clips, and clip
// operations (Lambda_Pkg_Motion.md §8.2-§8.5). The CMU-named jump moves onto
// Mixamo-named FBX skeletons: a 1.5x giant with the same rest rotations takes
// the copy path, and an A-pose takes the retarget path.
import motion: lambda.scene3d.motion
import h: lambda.scene3d.motion.humanoid

let dir = "test/demo/scene3d/assets/motion/"
let cmu = motion.load(dir ++ "jump.bvh", {scale: 0.01, rig: 'cmu'})^
let giant = motion.load(dir ++ "giant.fbx", {rig: 'mixamo'})^
let apose = motion.load(dir ++ "apose.fbx", {rig: 'mixamo'})^

fn origin(m) => [m[12], m[13], m[14]];
fn rotation(m) => [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]];
// a · bᵀ for 3x3 column-major rotations
fn delta(a, b) => [for (c in 0 to 2) for (r in 0 to 2) sum([for (k in 0 to 2) a[k * 3 + r] * b[k * 3 + c]])];
fn gap(a, b) => max([for (i in 0 to len(a) - 1) abs(a[i] - b[i])]);
fn role_name(c, role) => [for (j in motion.joints(c) where j.humanoid == role) j.name][0];
fn keys(c, clip_name) => len([for (k in motion.clips_of(c) where k.name == clip_name) k][0].times);

"1 rig profiles tag VRM 1.0 roles";
[len(h.roles), len(h.required), giant.rig, cmu.rig,
    [for (j in motion.joints(giant)[0 to 3]) [j.name, j.humanoid]],
    [for (j in motion.joints(cmu) where j.name == "LowerBack" or j.name == "Spine1") [j.name, j.humanoid]]];

"2 the same rest rotations copy channels, scaling root motion by hip height";
let big = motion.attach(giant, {leap: cmu})^
let source_channels = content(motion.clips_of(cmu)[0])
let copied = content(motion.clips_of(big)[0]);
[[for (k in motion.clips_of(big)) [k.name, len(k.times), len(content(k))]],
    [for (ch in copied where ch.position != null) ch.joint],
    copied[1].quaternion == source_channels[1].quaternion,
    max([for (key in 0 to 30) (let a = motion.pose(cmu, "jump", key)^, let b = motion.pose(big, "leap", key)^,
        max([for (j in motion.joints(cmu)) gap(origin(b[role_name(big, j.humanoid)]), origin(a[j.name]) * 1.5)]))]) < 1e-9];

"3 different rest rotations retarget each world rotation change from rest";
let bent = motion.attach(apose, {leap: cmu})^
let rest_from = motion.pose(cmu)^
let rest_to = motion.pose(apose)^
fn rotation_gap(key) {
    let a = motion.pose(cmu, "jump", key)^
    let b = motion.pose(bent, "leap", key)^;
    max([for (j in motion.joints(cmu)) (let n = role_name(bent, j.humanoid),
        gap(delta(rotation(b[n]), rotation(rest_to[n])), delta(rotation(a[j.name]), rotation(rest_from[j.name]))))])
}
[[for (k in motion.clips_of(bent)) [k.name, len(k.times), len(content(k))]],
    [for (key in [0, 8, 15, 30]) rotation_gap(key) < 1e-9],
    max([for (key in 0 to 30) gap(origin(motion.pose(bent, "leap", key)^["mixamorig:Hips"]), origin(motion.pose(cmu, "jump", key)^["Hips"]))]) < 1e-9,
    (let k = motion.retarget(motion.clips_of(cmu)[0], cmu, apose)^, [name(k), k.name, len(content(k))])];
// with `rest_frame`, a source key stands in for the rest pose: that key leaves the target at rest
let framed = motion.retarget(motion.clips_of(cmu)[0], cmu, apose, {rest_frame: 15})^
let arm = [for (ch in content(framed) where ch.joint == "mixamorig:LeftArm") ch][0];
(gap(arm.quaternion[60 to 63], [for (j in motion.joints(apose) where j.name == "mixamorig:LeftArm") j][0].quaternion) < 1e-9);

"4 in_place holds the root's horizontal translation";
let held = motion.in_place(cmu)^
let hips = [for (ch in content(motion.clips_of(held)[0]) where ch.joint == "Hips") ch][0];
[unique([for (k in 0 to 30) hips.position[3 * k]]), unique([for (k in 0 to 30) hips.position[3 * k + 2]]),
    len(unique([for (k in 0 to 30) hips.position[3 * k + 1]])) > 1];

"5 mirror swaps sides and reflects across the YZ plane";
let flipped = motion.mirror(cmu)^
fn mirror_gap(key) {
    let a = motion.pose(cmu, "jump", key)^
    let b = motion.pose(flipped, "jump", key)^;
    max([for (pair in [["LeftHand", "RightHand"], ["RightFoot", "LeftFoot"], ["Head", "Head"]])
        (let p = origin(a[pair[0]]), gap(origin(b[pair[1]]), [0.0 - p[0], p[1], p[2]]))])
}
[for (key in [0, 8, 15, 30]) mirror_gap(key) < 1e-9];

"6 trim keeps whole keys and rebases them";
let cut = motion.trim(cmu, 0.2, 0.5)^
let piece = motion.clips_of(cut)[0];
[len(piece.times), piece.times[0], abs(piece.duration - 0.3) < 1e-6,
    gap(origin(motion.pose(cut, "jump", 0)^["LeftHand"]), origin(motion.pose(cmu, "jump", 6)^["LeftHand"])) < 1e-12,
    motion.validate(cut)^];

"7 operations that cannot apply are errors";
let rig = motion.load(dir ++ "rig.fbx")^;
[(motion.humanoid(cmu, 'maya') ^ { "unknown profile" }),
 (motion.mirror(rig) ^ { "no roles" }),
 (motion.retarget(motion.clips_of(rig)[0], rig, apose) ^ { "missing roles" }),
 (motion.attach(cmu, {leap: rig}) ^ { "no shared joints" }),
 (motion.trim(cmu, 0.5, 0.2) ^ { "reversed" }),
 (motion.trim(cmu, 2.0, 3.0) ^ { "no key" }),
 (motion.in_place(cmu, ['w']) ^ { "bad axis" })]
