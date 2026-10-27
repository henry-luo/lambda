// S12.1.1v3 / D4.5.3v2: material values survive view edits; the host owns playback/resources.
import dom
import s: lambda.scene3d
fn stage(factor) element => s.scene([
    s.camera({id: "camera", type: 'orthographic', view_height: 3.0}),
    s.light('directional', {position: [0.3, 0.2, 2.0], intensity: 0.3}),
    s.resources([s.texture("assets/pbr/checker.png", {id: "checker"})]),
    s.mesh(s.plane([6.0, 6.0, 1.0]), s.material('basic', {texture: "checker"}), {position: [0.0, 0.0, -1.0]}),
    s.mesh(s.box([1.7, 1.7, 0.4]), s.material('physical', {id: "finish", color: "#ffffff", roughness: 0.08, transmission: factor, ior: 1.5, thickness: 0.4}), {rotation: [0.2, 0.35, 0.0]}),
    s.clip("glass-change", [s.track("finish.transmission", [0.0, 1.0], [0.0, 1.0])], {duration: 1.0})
], {id: "scene", camera: "camera", width: 128, height: 128, background: "#000000", style: "display:block"})
view <panel> state factor: 0.0 {
    <div <button id: "glass-change", "Glass"> stage(factor)>
}
on click(evt) {
    if (dom.get_attribute(evt.target, "id") == "glass-change") { factor = 1.0 }
}
<html <head <style "html,body{margin:0}">> <body apply(<panel>)>>
