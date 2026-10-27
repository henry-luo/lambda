// GPU simulations, their variables, samplers and uniform tracks in the package validator (SH5)
import s: lambda.scene3d
fn message(scene) => s.normalize(scene) ^ { ^.message }
let camera = s.camera({id: "main"})
let STEP = "void main() { fragment_color = texelFetch(position, ivec2(gl_FragCoord.xy), 0); }"
let QUAD = "uniform sampler2D records; void main() { gl_Position = vec4(position, 1.0) + texelFetch(records, ivec2(0), 0); }"
let WHITE = "void main() { fragment_color = vec4(1.0); }"
fn walker(variables, uniforms = {time: 0.0}, options = {}) element => s.simulation("swarm", 2, 2, variables, uniforms, options)
fn scene(simulation, sampled = "swarm.position", extra = []) element => s.scene([camera, simulation,
    s.mesh(s.plane(), s.shader_material(QUAD, WHITE, {records: sampled}, {id: "wing"})), *extra]);
[
 s.validate(scene(walker([s.variable("position", STEP, [for (i in 0 to 15) 0.0]), s.variable("velocity", STEP)],
    {time: 0.0, gust: [1.0, 0.0, 0.0]}, {timestep: 0.05}), "swarm.velocity",
    [s.clip("flight", [s.track("swarm.uniforms.time", [0.0, 60.0], [0.0, 60.0]),
        s.track("swarm.uniforms.gust", [0.0, 1.0], [1, 0, 0, 0, 1, 0], 'vector')])])) ^ { ^.message },
 contains(message(scene(walker([s.variable("position", STEP)], {gust: 1.0}))), "numeric time uniform"),
 contains(message(scene(walker([]))), "one to four variable"),
 contains(message(scene(walker([for (n in ["a", "b", "c", "d", "e"]) s.variable(n, STEP)]))), "one to four variable"),
 contains(message(scene(walker([s.variable("delta", STEP)]))), "GLSL name"),
 contains(message(scene(walker([s.variable("position", STEP, [0.0, 1.0])]))), "4 initial numbers"),
 contains(message(scene(walker([s.variable("position", STEP), s.variable("position", STEP)]))), "duplicate"),
 contains(message(scene(walker([s.variable("position", STEP)], {time: 0.0, position: 1.0}))), "repeats a variable"),
 contains(message(scene(walker([s.variable("position", STEP)], {time: 0.0, atlas: "atlas"}))), "uniform value"),
 contains(message(scene(walker([s.variable("position", STEP)], {time: 0.0}, {timestep: 0.0}))), "timestep"),
 contains(message(scene(s.simulation("swarm", 1024, 512, [s.variable("position", STEP)]))), "262144"),
 contains(message(scene(walker([s.variable("position", STEP)]), "swarm.heading")), "no simulation variable"),
 contains(message(scene(walker([s.variable("position", STEP)]), "flock.position")), "simulation reference"),
 contains(message(scene(walker([s.variable("position", STEP)]), "swarm.position", [s.clip("flight",
    [s.track("swarm.uniforms.time", [0.0, 1.0], [0, 0, 1, 1], 'vector')])])), "binding type")
]
