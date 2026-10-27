// S12.1.1v3 / D7.2.4v4: physical layers and their tracks are declarative values.
import s: lambda.scene3d
fn scene(options = {}, kind = 'physical') => s.scene([s.camera(), s.mesh(s.box(), s.material(kind, {id: "finish", *: options}))])
fn message(scene) => s.normalize(scene) ^ { ^.message }
fn animated(property, values, kind = 'number') => s.scene([s.camera(), s.mesh(s.box(), s.material('physical', {id: "finish"})),
    s.clip("polish", [s.track("finish." ++ property, [0.0, 1.0], values, kind)])]);
[
 s.validate(scene())^,
 s.validate(scene({clearcoat: 0.8, 'clearcoat-roughness': 0.25, sheen: 0.4, 'sheen-color': "#cc3366", 'sheen-roughness': 0.6}))^,
 s.validate(animated("clearcoat", [0.0, 1.0]))^,
 s.validate(animated("clearcoat-roughness", [0.0, 1.0]))^,
 s.validate(animated("clearcoat-normal-scale", [1.0, -1.0]))^,
 s.validate(animated("sheen", [0.0, 1.0]))^,
 s.validate(animated("sheen-roughness", [0.0, 1.0]))^,
 s.validate(animated("sheen-color", [1.0, 0.0, 0.0, 0.0, 0.0, 1.0], 'color'))^,
 contains(message(scene({clearcoat: -0.1})), "physical material factor"),
 contains(message(scene({'clearcoat-roughness': 1.1})), "physical material factor"),
 contains(message(scene({sheen: 2.0})), "physical material factor"),
 contains(message(scene({'sheen-roughness': "soft"})), "physical material factor"),
 contains(message(scene({'sheen-color': "#zzzzzz"})), "sheen color"),
 contains(message(scene({clearcoat: 1.0}, 'standard')), "physical material factor"),
 contains(message(scene({'clearcoat-texture': "missing"})), "reference"),
 contains(message(scene({'sheen-color-texture': "missing"}, 'standard')), "physical material"),
 s.validate(s.scene([s.camera(), s.resources([s.texture("layer.png", {id: "layer"})]),
    s.mesh(s.box(), s.material('physical', {'clearcoat-texture': "layer", 'clearcoat-roughness-texture': "layer",
        'clearcoat-normal-texture': "layer", 'sheen-color-texture': "layer", 'sheen-roughness-texture': "layer"}))]))^
]
