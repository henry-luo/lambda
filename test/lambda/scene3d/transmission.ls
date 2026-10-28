// S12.1.1v3 / D7.2.4v4: glass properties and tracks are pure descriptions.
import s: lambda.scene3d
fn scene(options = {}, kind = 'physical') => s.scene([s.camera(), s.mesh(s.box(), s.material(kind, {id: "glass", *: options}))])
fn message(description) => s.normalize(description) ^ { ^.message }
fn export_message(description) => format(description, 'gltf') ^ { ^.message }
fn animated(key, values, kind = 'number') => s.scene([s.camera(), s.mesh(s.box(), s.material('physical', {id: "glass"})),
    s.clip("glass-change", [s.track("glass." ++ key, [0.0, 1.0], values, kind)])]);
[
 s.validate(scene({transmission: 1.0, ior: 1.5, thickness: 0.4, 'attenuation-color': "#99ccff", 'attenuation-distance': 2.0}))^,
 s.validate(scene({transmission: 0.0}))^,
 s.validate(scene({transmission: 1.0, 'alpha-mode': 'mask', opacity: 0.5}))^,
 s.validate(scene({ior: 1.0}))^,
 s.validate(animated("transmission", [0.0, 1.0]))^,
 s.validate(animated("ior", [1.0, 2.0]))^,
 s.validate(animated("thickness", [0.0, 1.0]))^,
 s.validate(animated("attenuation-distance", [1.0, 2.0]))^,
 s.validate(animated("attenuation-color", [1.0, 0.0, 0.0, 0.0, 0.0, 1.0], 'color'))^,
 contains(message(scene({transmission: 1.1})), "physical material factor"),
 contains(message(scene({ior: 0.0})), "physical material factor"),
 contains(message(scene({ior: 0.9})), "physical material factor"),
 contains(message(scene({thickness: -1.0})), "physical material factor"),
 contains(message(scene({'attenuation-distance': 0.0})), "physical material factor"),
 contains(message(scene({'attenuation-color': "#zzzzzz"})), "attenuation color"),
 contains(message(scene({transmission: 1.0}, 'standard')), "physical material factor"),
 contains(message(scene({transmission: 1.0, 'alpha-mode': 'blend'})), "coverage"),
 contains(message(scene({transmission: 1.0, opacity: 0.5})), "coverage"),
 contains(export_message(scene({transmission: 1.0, 'alpha-mode': 'blend'})), "coverage"),
 contains(message(scene({'thickness-texture': "missing"})), "reference"),
 s.validate(s.scene([s.camera(), s.resources([s.texture("glass.png", {id: "layer"})]),
    s.mesh(s.box(), s.material('physical', {transmission: 1.0, 'transmission-texture': "layer", 'thickness-texture': "layer"}))]))^
]
