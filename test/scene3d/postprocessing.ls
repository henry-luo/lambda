// S12.1.1v3 / D4.5.2: template values describe passes; native playback owns time/resources.
import dom
import s: lambda.scene3d
import post: lambda.scene3d.postprocessing
let GRADE = "uniform float amount;void main(){vec4 c=texture(color_input,gl_FragCoord.xy/resolution);fragment_color=vec4(c.rgb*amount,c.a);}"
fn stage(amount) element => s.scene([
    s.camera({id: "camera", type: 'orthographic', view_height: 2.0}),
    s.mesh(s.plane([1.0, 1.0, 1.0]), s.material('basic', {color: "#ff0000"})),
    post.chain([post.shader(GRADE, {amount: amount}, {id: "grade", domain: 'display'})]),
    s.clip("fade", [s.track("grade.uniforms.amount", [0.0, 1.0], [1.0, 0.0])], {duration: 1.0})
], {id: "scene", camera: "camera", width: 128, height: 128, background: "#000000", style: "display:block"})
view <controls> state amount: 1.0 {
    <div <button id: "dim", "Dim"> stage(amount)>
}
on click(evt) {
    if (dom.get_attribute(evt.target, "id") == "dim") { amount = 0.25 }
}
<html <head <style "html,body{margin:0}">> <body apply(<controls>)>>
