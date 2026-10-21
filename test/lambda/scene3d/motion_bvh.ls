// Motion M1: a BVH file maps to a raw <bvh> tree, converts once into the common
// <character> schema, and compiles into scene3d content (Lambda_Pkg_Motion.md
// §5, §7.3, §8). World joint positions are checked against the fixture
// generator's own forward-kinematics pass.
import motion: lambda.scene3d.motion

let dir = "test/demo/scene3d/assets/motion/"
let raw = input(dir ++ "jump.bvh")^
let block = content(raw)[1]
let hero = motion.load(dir ++ "jump.bvh", {scale: 0.01})^
let expected = input(dir ++ "expected.json")^
let take = motion.clips_of(hero)[0]

fn origin(m) => [m[12], m[13], m[14]]
// the largest coordinate difference, in metres, over the reference joints
fn drift(frame) {
    let world = motion.pose(hero, "jump", frame)^
    let want = expected.positions[string(frame)];
    max([for (k, p in want) max([for (i in 0 to 2) abs(origin(world[string(k)])[i] - p[i] * 0.01)])])
}

let figure = motion.mannequin(hero)^
let group = motion.scene(figure, {id: "jumper", autoplay: "jump"})^
let clip_node = [for (x in content(group) where name(x) == 'animation-clip') x][0]
let mesh_node = [for (x in content(group) where name(x) == 'mesh') x][0];

"1 the raw tree keeps the file's structure";
[name(raw), name(content(raw)[0]), block.frames, len(block.data), len(block.data[0]), block.data[15][1]];

"2 one conversion into the character schema";
[motion.validate(hero)^, hero.source, hero.units, len(motion.joints(hero)), take.name, take.rate, len(take.times),
    len(content(take))];

"3 world joint positions match the reference within a micrometre";
[for (frame in [0, 8, 15, 30]) drift(frame) < 0.000001];

"4 a mannequin skins every bone, and the scene names bones and tracks";
[len(content(motion.body_of(figure))), mesh_node.skeleton, clip_node.label, clip_node.autoplay,
    len(content(clip_node)), content(clip_node)[0].path];

"5 an unknown clip or a bad scene id is an error";
[(motion.pose(hero, "walk") ^ { "no clip" }), (motion.scene(hero, {id: "a.b"}) ^ { "bad id" })]
;

"6 malformed BVH is rejected at the parser";
let head = "HIERARCHY\nROOT A\n{\n OFFSET 0 0 0\n CHANNELS 3 Zrotation Xrotation Yrotation\n}\n"
let good = parse(head ++ "MOTION\nFrames: 1\nFrame Time: 0.5\n1 2 3\n", 'bvh')^;
[content(good)[1].data[0][2],
 (parse(head ++ "MOTION\nFrames: 2\nFrame Time: 0.5\n1 2 3\n", 'bvh') ^ { "frame count" }),
 (parse(head ++ "MOTION\nFrames: 1\nFrame Time: 0.5\n1 2\n", 'bvh') ^ { "short row" }),
 (parse(head ++ "MOTION\nFrames: 1\nFrame Time: 0\n1 2 3\n", 'bvh') ^ { "frame time" }),
 (parse(replace(head, "Yrotation", "Wrotation") ++ "MOTION\nFrames: 1\nFrame Time: 0.5\n1 2 3\n", 'bvh') ^ { "channel" }),
 (parse(head, 'bvh') ^ { "no motion" })]
