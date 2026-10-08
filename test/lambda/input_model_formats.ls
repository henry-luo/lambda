// model input stays ordinary Lambda values, with dependencies left as references
fn child(doc, tag) => [for (node in content(doc) where name(node) == tag) node][0]

let obj = input("test/input/model/triangle.obj")^
let mtl = input("test/input/model/triangle.mtl")^
let gltf = input("test/input/model/animated.gltf")^
let a3d = input("test/input/model/animated.a3d")^;

[name(obj), content(child(obj, 'g'))[0], content(child(obj, 'v'))[0]];
content(child(obj, 'f'));
let material = child(mtl, 'material');
[material.name, child(material, 'map_Kd').file];
[gltf.asset.version, gltf.buffers[0].uri, gltf.animations[0].channels[0].target.path];
let action = child(a3d, 'action');
[name(a3d), a3d.name, action.name, action.duration_ms];
content(action)[1];
name(parse("v 1 2 3\n", 'obj')^);
name(parse("newmtl x\nKd 1 1 1\n", 'mtl')^);
parse("{\"asset\":{\"version\":\"2.0\"}}", 'gltf')^.asset.version
