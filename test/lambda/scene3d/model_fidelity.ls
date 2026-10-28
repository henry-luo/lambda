// S12.1.1v3 / D7.2.4v4: richer geometry and maps remain pure scene descriptions.
import s: lambda.scene3d
import camera: lambda.scene3d.camera
let P = [-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 0.0, 1.0, 0.0]
let UV = [0.0, 0.0, 1.0, 0.0, 0.5, 1.0]
let RGBA = [1.0, 0.0, 0.0, 0.25, 1.0, 0.0, 0.0, 0.5, 1.0, 0.0, 0.0, 1.0]
fn sample(geometry_options = {}, texture_options = {}) => s.scene([s.camera(),
    s.resources([s.texture("test/scene3d/quadrants.png", {id: "map", *: texture_options})]),
    s.mesh(s.geometry(P, geometry_options), s.material('standard', {texture: "map"}))])
fn message(scene) => s.normalize(scene) ^ { ^.message }
let infinite_camera = camera.prepare({position: [0.0, 0.0, 4.0], target: [0.0, 0.0, 0.0], 'infinite-far': true});
[
 s.validate(sample({uvs: UV, uvs1: UV, colors: RGBA}, {'uv-set': 1, 'uv-transform': [0.0, -1.0, 1.0, 1.0, 0.0, 0.0]}))^,
 s.validate(sample({uvs: UV, colors: [1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0]}))^,
 contains(message(sample({}, {'uv-set': 1})), "absent texture coordinate"),
 contains(message(sample({}, {'uv-set': 0})), "absent texture coordinate"),
 contains(message(sample({uvs: UV}, {'uv-set': 2})), "uv-set"),
 contains(message(sample({uvs: UV}, {'uv-transform': [1, 0, 0, 1]})), "six-number"),
 contains(message(sample({uvs: UV, uvs1: [0, 0]})), "geometry components"),
 contains(message(sample({colors: [1, 0, 0, -0.5, 1, 0, 0, 1, 1, 0, 0, 1]})), "color out of range"),
 s.validate(s.scene([s.camera({'infinite-far': true, near: 2000.0})]))^,
 s.validate(s.scene([s.camera({type: 'orthographic', near: 0.0})]))^,
 contains(message(s.scene([s.camera({type: 'orthographic', 'infinite-far': true})])), "invalid scene camera"),
 camera.project(infinite_camera, [0.0, 0.0, -10000.0]).visible,
 len(camera.clip_segment(infinite_camera, [0.0, 0.0, 0.0], [0.0, 0.0, -10000.0])) == 2,
 camera.project(camera.prepare(), [0.0, 0.0, -10000.0]).visible == false
]
