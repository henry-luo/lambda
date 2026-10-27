// MTL PBR maps keep their references through package prefixing, validation and glTF export.
import s: lambda.scene3d
fn nodes(n) => [n, *[for (c in content(n)) *nodes(c)]]
let model = s.load("test/scene3d/assets/pbr/finish.mtl", {id: "mapped"})^
let material = [for (n in nodes(model) where name(n) == 'material') n][0]
let textures = [for (n in nodes(model) where name(n) == 'texture') n]
let scene = s.scene([s.camera({id: "camera"}), model,
    <mesh material: material.id, (s.plane())>], {camera: "camera"})
let gltf = parse(format(scene, 'gltf')^, 'json')^;
[s.validate(scene)^, material.type == 'standard', material.roughness == .4,
 material.metalness == .75, material["normal-scale"] == .8, len(textures) == 2,
 starts_with(material["normal-texture"], "mapped-"), starts_with(material["metal-rough-texture"], "mapped-"),
 gltf.materials[0].pbrMetallicRoughness.roughnessFactor == .4,
 gltf.materials[0].pbrMetallicRoughness.metallicFactor == .75,
 gltf.materials[0].normalTexture.scale == .8, gltf.materials[0].pbrMetallicRoughness.metallicRoughnessTexture != null]
