import s: lambda.scene3d
fn nodes(n) => [n, *[for (c in content(n)) *nodes(c)]]
fn by_id(n, id) => [for (v in nodes(n) where v.id == id) v][0]
fn by_tag(n, tag) => [for (v in nodes(n) where name(v) == tag) v]
let obj = s.load("test/demo/scene3d/assets/loading/concave.obj", {id: "arrow"})^
let gltf = s.load("test/demo/scene3d/assets/loading/articulated.gltf", {id: "flag", autoplay: true, clip: "Wave"})^
let a3d = s.load("test/demo/scene3d/assets/loading/puppet.a3d", {id: "puppet"})^
let repeated = s.load("test/demo/scene3d/assets/loading/embedded.gltf", {id: "second"})^
let library = s.load("test/demo/scene3d/assets/loading/materials/palette.mtl", {id: "palette"})^;
[name(obj), obj.id, len(by_tag(obj, 'mesh')), len(by_tag(obj, 'texture'))];
let arrow_geometry = by_tag(obj, 'geometry')[0];
[len(arrow_geometry.positions), len(arrow_geometry.normals), len(arrow_geometry.uvs)];
let flag_geometry = by_tag(gltf, 'geometry')[0];
[len(flag_geometry.positions), len(flag_geometry["skin-indices"]), flag_geometry["skin-indices"][0], flag_geometry["skin-indices"][16],
    len(flag_geometry["morph-positions"]), flag_geometry["morph-positions"][18]];
let clips = by_tag(gltf, 'animation-clip');
[clips[0].id, clips[0].label, clips[0].autoplay, len(content(clips[0])), clips[0].duration];
[for (track in content(clips[0])) [track.path, track.type, track.interpolation]];
let a3d_clip = by_tag(a3d, 'animation-clip')[0];
[a3d_clip.id, a3d_clip.autoplay, len(content(a3d_clip)), len(by_tag(a3d, 'bone'))];
[by_id(gltf, "flag-asset-node-3").matrix[12], len(by_id(gltf, "flag-asset-node-4").quaternion),
    len(by_tag(library, 'material')), len(by_tag(library, 'mesh'))];
s.validate(s.scene([s.camera({id: "camera"}), s.light('ambient'), obj, gltf, a3d, repeated, library]))^;
contains((s.load("test/demo/scene3d/assets/loading/concave.obj", {id: "bad.id"}) ^ { ^.message }), "no dot")
