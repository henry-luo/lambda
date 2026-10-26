// STL, PLY, meshopt-compressed glTF with texture/emission/light extensions, and glTF/GLB export.
import s: lambda.scene3d
fn nodes(n) => [n, *[for (c in content(n)) *nodes(c)]]
fn by_tag(n, tag) => [for (v in nodes(n) where name(v) == tag) v]
let dir = "test/demo/scene3d/assets/loading/"
let tetra = s.load(dir ++ "tetra.stl", {id: "tetra"})^;
let tetra_geometry = by_tag(tetra, 'geometry')[0];
[len(by_tag(tetra, 'mesh')), len(tetra_geometry.positions), slice(tetra_geometry.normals, 0, 3)];
let pyramid = s.load(dir ++ "pyramid.ply", {id: "pyramid"})^;
let pyramid_geometry = by_tag(pyramid, 'geometry')[0];
[len(pyramid_geometry.indices), len(pyramid_geometry.colors), len(pyramid_geometry.normals)];
let raw = input(dir ++ "pyramid.ply")^;
[name(raw), raw.format, raw.comments, [for (e in content(raw)) [name(e), e.count]], content(raw)[0].red];
let lit = s.load(dir ++ "lit.gltf", {id: "lit"})^;
let lit_geometry = by_tag(lit, 'geometry')[0];
let glow = [for (m in by_tag(lit, 'material') where m.name == "glow") m][0];
[len(lit_geometry.positions), slice(lit_geometry.uvs, 0, 4), glow.emissive, glow["emissive-intensity"]];
[for (l in by_tag(lit, 'light')) [l.id, l.type, l.name, l.intensity, l.distance, l.angle]];
let dark = s.load(dir ++ "lit.gltf", {id: "dark", lights: false})^;
len(by_tag(dark, 'light'));
let scene = s.scene([s.camera({id: "camera", position: [0.0, 1.0, 4.0]}), s.light('ambient', {intensity: 0.2}),
    s.light('point', {position: [0.0, 2.0, 2.0], distance: 10.0}), tetra, pyramid, lit], {camera: "camera"});
s.validate(scene)^;
let gltf = parse(format(scene, 'gltf')^, 'json')^;
[len(gltf.nodes), len(gltf.meshes), len(gltf.materials), gltf.extensionsUsed, len(gltf.extensions.KHR_lights_punctual.lights)];
let glb = format(scene, 'glb')^;
[type(glb), len(glb) % 4];
// a back-facing spot cone wider than a right angle is not a light
(s.validate(s.scene([s.light('spot', {angle: 2.0})])) ^ { ^.message }) != true;
(format(s.load("test/demo/scene3d/assets/loading/articulated.gltf", {id: "flag"})^, 'gltf') ^ { contains(^.message, "glTF export") })
