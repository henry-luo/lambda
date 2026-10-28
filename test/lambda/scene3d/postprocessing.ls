// S12.1.1v3: native postprocessing is described and validated as pure scene data.
import s: lambda.scene3d
import post: lambda.scene3d.postprocessing
let COPY = "void main(){fragment_color=texture(color_input,gl_FragCoord.xy/resolution);}"
let TINT = "uniform float amount;void main(){fragment_color=texture(color_input,gl_FragCoord.xy/resolution)*amount;}"
fn scene(passes, options = {}) element => s.scene([s.camera({id: "main"}),
    s.mesh(s.plane(), s.material()), post.chain(passes)], {camera: "main", background: "#123456", *: options})
fn valid(scene) => s.validate(scene) ^ { ^.message }
fn message(scene) => s.normalize(scene) ^ { ^.message }
[
 valid(scene([])),
 valid(scene([post.effect('bloom'), post.effect('fxaa')])),
 valid(scene([post.shader(COPY), post.shader(COPY, {}, {domain: 'display'})])),
 valid(scene([post.shader(TINT, {amount: 1.0}, {id: "tint"})])),
 valid(s.scene([s.camera(), post.chain([post.effect('bloom', {id: "glow"})]),
    s.clip("pulse", [s.track("glow.strength", [0.0, 1.0], [0.0, 2.0])])], {background: "#000"})),
 valid(s.scene([s.camera(), post.chain([post.shader(TINT, {amount: 1.0}, {id: "tint"})]),
    s.clip("tinting", [s.track("tint.uniforms.amount", [0.0, 1.0], [0.0, 1.0])])])),
 contains(message(scene([post.effect('unknown')])), "unknown postprocessing"),
 contains(message(scene([post.effect('bloom', {strength: -1.0})])), "nonnegative"),
 contains(message(scene([post.effect('bloom', {radius: "large"})])), "nonnegative"),
 contains(message(scene([post.effect('bloom')], {background: "transparent"})), "opaque viewport"),
 contains(message(scene([post.effect('bloom')], {background: "#1234"})), "opaque viewport"),
 contains(message(scene([post.effect('fxaa'), post.effect('bloom')])), "linear effects before"),
 contains(message(scene([post.effect('bloom', {domain: 'display'})])), "linear effects before"),
 contains(message(scene([post.effect('fxaa', {domain: 'linear'})])), "linear effects before"),
 contains(message(scene([post.effect('fxaa', {uniforms: {amount: 1}})])), "numeric options"),
 contains(message(scene([post.effect('shader')])), "fragment source"),
 contains(message(scene([post.shader(COPY, {color_input: 1})])), "uniform name"),
 contains(message(scene([post.shader(COPY, {atlas: "missing"})])), "reference"),
 contains(message(scene([for (i in 1 to 9) post.effect('fxaa')])), "eight effect"),
 contains(message(s.scene([s.camera(), post.chain([]), post.chain([])])), "one postprocessing"),
 contains(message(s.scene([s.camera(), s.group([post.chain([])])])), "invalid scene child")
]
