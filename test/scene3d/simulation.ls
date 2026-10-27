// GPU simulation (SH5/SH6): four particles drift up by velocities a second variable keeps at the
// `speed` uniform. A `time` track steps the simulation in fixed 0.25 s steps; a shader material
// places one quad per particle from the position records. `faster` re-renders the template with
// only a new `speed`, which must keep the simulated state; rewinding the time restarts it.
import dom
import s: lambda.scene3d

// each step reads the previous records of both variables, as GPUComputationRenderer does
let POSITION = "void main() {
    ivec2 at = ivec2(gl_FragCoord.xy);
    fragment_color = texelFetch(position, at, 0) + vec4(texelFetch(velocity, at, 0).xyz * delta, 0.0);
}"
let VELOCITY = "uniform float speed;
void main() { fragment_color = vec4(0.0, speed, 0.0, 0.0); }"
let QUAD = "uniform sampler2D positions;
void main() {
    vec4 p = texelFetch(positions, ivec2(gl_InstanceID, 0), 0);
    gl_Position = projection_matrix * view_matrix * vec4(position * 0.4 + p.xyz, 1.0);
}"
let WHITE = "void main() { fragment_color = vec4(1.0); }"

fn camera() element => s.camera({id: "main", type: 'orthographic', view_height: 4.0, position: [0.0, 0.0, 4.0]})

// particles at x = -1.5, -0.5, 0.5, 1.5: 32 pixels per unit in a 128 × 128 view
fn stage(speed) element => <scene3d id: "drift", width: 128, height: 128, camera: "main", background: "#000000",
    style: "display:block",
    camera();
    s.simulation("swarm", 4, 1, [
        s.variable("position", POSITION, [-1.5, 0.0, 0.0, 1.0, -0.5, 0.0, 0.0, 1.0, 0.5, 0.0, 0.0, 1.0, 1.5, 0.0, 0.0, 1.0]),
        s.variable("velocity", VELOCITY, [0.0, 0.5, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0])],
        {time: 0.0, speed: speed}, {timestep: 0.25});
    <mesh 'instance-count': 4, <geometry type: 'plane'> s.shader_material(QUAD, WHITE, {positions: "swarm.position"})>
    s.clip("run", [s.track("swarm.uniforms.time", [0.0, 8.0], [0.0, 8.0])], {duration: 8.0})>

fn broken(id, simulation, uniforms) element => <scene3d id: id, width: 32, height: 32, camera: "main", style: "display:block",
    camera(); simulation;
    <mesh <geometry type: 'plane'> s.shader_material(QUAD, WHITE, uniforms)>>
fn walker(id, shader, uniforms) element => s.simulation(id, 2, 2, [s.variable("position", shader)], uniforms)

view <lab> state speed: 0.5 {
    <div id: "lab", 'data-speed': string(speed),
        <button id: "faster", "faster">
        stage(speed)
        broken("untimed", walker("stopped", WHITE, {speed: 1.0}), {positions: "stopped.position"})
        broken("uncompiled", walker("bad", "void main() { fragment_color = ; }", {time: 0.0}), {positions: "bad.position"})
        broken("unnamed", walker("walk", WHITE, {time: 0.0}), {positions: "walk.heading"})>
}
on click(evt) {
    if (dom.get_attribute(evt.target, "id") == "faster") { speed = 1.0 }
}

<html
    <head <style "html,body{margin:0;background:#ffffff} button{font-size:10px;margin:1px}">>
    <body apply(<lab>)>
>
