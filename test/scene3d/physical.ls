// S12.1.1v3 / D4.5.3v2: material values survive view edits; the host owns playback/resources.
import dom
import s: lambda.scene3d
fn stage(coat) element => s.scene([
    s.camera({id: "camera", type: 'orthographic', view_height: 3.0}),
    s.light('directional', {position: [0.3, 0.2, 2.0], intensity: 0.3}),
    s.mesh(s.box([2.0, 2.0, 2.0]), s.material('physical', {id: "finish", color: "#204060", roughness: 0.65,
        clearcoat: coat, 'clearcoat-roughness': 0.15}), {rotation: [0.2, 0.35, 0.0]}),
    s.clip("polish", [s.track("finish.clearcoat", [0.0, 1.0], [0.0, 1.0])], {duration: 1.0})
], {id: "scene", camera: "camera", width: 128, height: 128, background: "#000000", style: "display:block"})
view <panel> state coat: 0.0 {
    <div <button id: "polish", "Clear coat"> stage(coat)>
}
on click(evt) {
    if (dom.get_attribute(evt.target, "id") == "polish") { coat = 1.0 }
}
<html <head <style "html,body{margin:0}">> <body apply(<panel>)>>
