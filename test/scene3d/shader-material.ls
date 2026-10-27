// Shader materials (SH1–SH3): Lambda-authored GLSL draws three instances that its vertex
// shader places from per-instance data, samples a texture, and reads a `time` uniform a
// keyframe track drives (SH2). `soften` re-renders the template with a new `flex` value only,
// which must keep the linked program (SH6). The other scenes fail with engine diagnostics.
import dom
import s: lambda.scene3d

// the quad narrows by mix(1, cos(time), flex): flat at time 0, closed at time π when flex is 0.5
let VERTEX = "uniform float time, flex;
out vec2 surface;
void main() {
    vec3 p = position;
    p.x *= mix(1.0, cos(time), flex);
    p.xy += offset;
    surface = uv;
    gl_Position = projection_matrix * view_matrix * model_matrix * instance_matrix * vec4(p, 1.0);
}"
let FRAGMENT = "uniform sampler2D quadrants;
uniform vec3 tint;
in vec2 surface;
void main() { fragment_color = vec4(texture(quadrants, surface).rgb * tint, 1.0); }"

fn camera() element => s.camera({id: "main", type: 'orthographic', view_height: 2.0, position: [0.0, 0.0, 4.0]})

// three unit quads at x = -2, 0 and 2: 32 pixels per unit in a 192 × 64 view
fn stage(flex) element => <scene3d id: "flock", width: 192, height: 64, camera: "main", background: "#000000",
    style: "display:block",
    camera();
    // nearest sampling keeps each quadrant's color exact at the asserted pixels
    s.resources([s.texture("quadrants.png", {id: "quadrants", 'min-filter': 9728, 'mag-filter': 9728})]);
    <mesh 'instance-count': 3, 'instance-data': {offset: [-2.0, 0.0, 0.0, 0.0, 2.0, 0.0]},
        <geometry type: 'plane', size: [1.0, 1.0, 1.0]>
        s.shader_material(VERTEX, FRAGMENT, {time: 0.0, flex: flex, tint: "#ffffff", quadrants: "quadrants"},
            {id: "wing", side: 'double'})>
    s.clip("beat", [s.track("wing.uniforms.time", [0.0, 4.0], [0.0, 4.0])], {duration: 4.0})>

fn broken(id, vertex, uniforms) element => <scene3d id: id, width: 32, height: 32, camera: "main", style: "display:block",
    camera();
    <mesh <geometry type: 'plane'> s.shader_material(vertex, "void main() { fragment_color = vec4(1.0); }", uniforms)>>

let PLAIN = "void main() { gl_Position = projection_matrix * view_matrix * vec4(position, 1.0); }"
let WITH_FLEX = "uniform float flex; void main() { gl_Position = vec4(position * flex, 1.0); }"

view <bench> state flex: 0.5 {
    <div id: "bench", 'data-flex': string(flex),
        <button id: "soften", "soften">
        stage(flex)
        broken("syntax", "void main() { gl_Position = ; }", {})
        broken("unset", WITH_FLEX, {})
        broken("mismatch", WITH_FLEX, {flex: [1.0, 2.0]});
        <scene3d id: "unshaded", width: 32, height: 32, camera: "main", style: "display:block",
            camera();
            <mesh 'instance-count': 2, <geometry type: 'plane'> <material type: 'basic'>>>
        broken("plain", PLAIN, {})>
}
on click(evt) {
    if (dom.get_attribute(evt.target, "id") == "soften") { flex = 0.25 }
}

<html
    <head <style "html,body{margin:0;background:#ffffff} button{font-size:10px;margin:1px}">>
    <body apply(<bench>)>
>
