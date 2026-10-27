// Shader materials, shader instances and uniform tracks in the package validator (SH1–SH3)
import s: lambda.scene3d
fn message(scene) => s.normalize(scene) ^ { ^.message }
let camera = s.camera({id: "main"})
let VERTEX = "void main() { gl_Position = projection_matrix * view_matrix * vec4(position, 1.0); }"
let FRAGMENT = "void main() { fragment_color = vec4(1.0); }"
fn shader(uniforms, options = {}) element => s.shader_material(VERTEX, FRAGMENT, uniforms, options)
fn scene(mesh_options, uniforms = {time: 0.0}, extra = []) element => s.scene([camera,
    s.resources([s.texture("image.png", {id: "atlas"})]),
    s.mesh(s.plane(), shader(uniforms, {id: "wing"}), {id: "flock", *: mesh_options}), *extra]);
[
 s.validate(scene({'instance-count': 4, 'instance-data': {seed: [0.0, 1.0, 2.0, 3.0], offset: [0, 0, 1, 1, 2, 2, 3, 3]}},
    {time: 0.0, tint: "#ff8800", atlas: "atlas", weights: [1.0, 2.0, 3.0, 4.0]},
    [s.clip("beat", [s.track("wing.uniforms.time", [0.0, 1.0], [0.0, 1.0]),
        s.track("flock.material.uniforms.weights", [0.0, 1.0], [0, 0, 0, 0, 1, 1, 1, 1], 'vector'),
        s.track("wing.uniforms.tint", [0.0, 1.0], [1, 0.5, 0, 0, 0.5, 1], 'color')])])) ^ { ^.message },
 contains(message(s.scene([camera, s.mesh(s.plane(), <material type: 'shader', fragment: FRAGMENT>)])), "vertex and fragment"),
 contains(message(scene({}, {gl_time: 0.0})), "uniform name"),
 contains(message(scene({}, {position: 0.0})), "uniform name"),
 contains(message(scene({}, {time: ["soon"]})), "uniform value"),
 contains(message(scene({}, {tint: "#zz0000"})), "uniform value"),
 contains(message(scene({}, {atlas: "missing"})), "reference"),
 contains(message(s.scene([camera, s.mesh(s.plane(), s.material(), {'instance-count': 2})])), "require a shader material"),
 contains(message(scene({'instance-count': 2, instances: [s.transform()^]})), "instance-count must be"),
 contains(message(scene({'instance-count': 0})), "instance-count must be"),
 contains(message(scene({'instance-count': 2, 'instance-data': {seed: [0.0, 1.0, 2.0]}})), "1 to 4 numbers"),
 contains(message(scene({'instance-count': 2, 'instance-data': {color: [0.0, 1.0]}})), "1 to 4 numbers"),
 contains(message(scene({'morph-weights': [0.5]})), "skinned or morphed"),
 contains(message(scene({}, {time: 0.0}, [s.clip("beat", [s.track("wing.uniforms.time", [0.0, 1.0], [0, 0, 1, 1], 'vector')])])), "binding type"),
 contains(message(scene({}, {time: 0.0}, [s.clip("beat", [s.track("wing.uniforms.missing", [0.0, 1.0], [0.0, 1.0])])])), "binding type"),
 contains(message(scene({}, {time: 0.0}, [s.clip("beat", [s.track("camera-less.uniforms.time", [0.0, 1.0], [0.0, 1.0])])])), "target")
]
