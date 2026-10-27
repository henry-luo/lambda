import s: lambda.scene3d
import fx: lambda.scene3d.effects

fn valid(kind) => s.validate(s.scene([
    s.camera({id: "main"}),
    s.resources([s.texture("image.png", {id: "image"})]),
    s.mesh(s.plane(), fx.material(kind, "image", {}, {id: "ink"})^),
    fx.animate("ink", 4.0)^
]))^
let svg = <svg xmlns: "http://www.w3.org/2000/svg", width: 32, height: 16, <path d: "M0 0h32v16H0z", fill: "#ff0000">>
let texture = fx.svg_texture(svg, {id: "vector", width: 64, height: 32})^
let clip = fx.animate("ink", 5.0, {autoplay: false})^;
[
 len(fx.catalog()) == 5,
 [for (effect in fx.catalog()) valid(effect.id)],
 s.effect_material('channel-split', "image")^.type == 'shader',
 starts_with(texture.src, "data:image/svg+xml,"),
 contains(texture.src, "%23ff0000"),
 texture.width == 64 and texture.height == 32,
 clip.duration == 5.0 and clip.autoplay == false,
 content(clip)[0].path == "ink.uniforms.phase",
 content(clip)[0].times == [0.0, 5.0] and content(clip)[0].values == [0.0, 1.0],
 contains(fx.material('unknown', "image") ^ { ^.message }, "unknown effect"),
 contains(fx.material('flies', "") ^ { ^.message }, "texture ID"),
 contains(fx.material('flies', "image", {missing: 1}) ^ { ^.message }, "unknown uniform"),
 contains(fx.material('flies', "image", {amount: "wrong"}) ^ { ^.message }, "finite number"),
 contains(fx.material('flies', "image", {aspect: 0}) ^ { ^.message }, "out of range"),
 contains(fx.material('flies', "image", {radius: 0}) ^ { ^.message }, "out of range"),
 contains(fx.material('rolling-distort', "image", {spread: 0}) ^ { ^.message }, "out of range"),
 contains(fx.animate("ink", 0) ^ { ^.message }, "positive"),
 contains(fx.animate("ink.phase") ^ { ^.message }, "without dots"),
 contains(fx.svg_texture(<div>) ^ { ^.message }, "SVG element")
]
