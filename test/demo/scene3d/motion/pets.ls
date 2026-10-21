// Kenney's Cube Pets (CC0): rigid FBX rigs with no skeleton, whose parts are
// animated directly, convert into <character>s like skinned ones. The three pets
// share one set of clips; the template owns which one plays, and each pet's
// motion controller crossfades to it (Lambda_Pkg_Motion.md §9.2).
import dom
import s: lambda.scene3d
import motion: lambda.scene3d.motion
import ui: ._ui

let dir = "test/demo/scene3d/assets/motion/cube-pets/"
let pets = [["cat", -2.3], ["dog", 0.0], ["penguin", 2.3]]
// gestures play once and return to idling
let choices = [["idle", "Idle"], ["walk", "Walk"], ["run", "Run"], ["eat", "Eat"], ["dance", "Dance"],
    ["gesture-positive", "Cheer"], ["gesture-negative", "Sulk"]]
fn states() => [for (c in choices)
    if (starts_with(c[0], "gesture")) motion.motion_state(symbol(c[0]), c[0], {loop: 'once', next: 'idle', fade: 0.15})
    else motion.motion_state(symbol(c[0]), c[0])];
// compiled once; each render only appends the controllers
let compiled = [for (p in pets) (let c = motion.load(dir ++ "animal-" ++ p[0] ++ ".fbx")^,
    motion.scene(c, {id: p[0], transform: {position: [p[1], 0.0, 0.0]}})^)]

fn stage(choice, cue) element^ => s.normalize(s.scene([
    s.camera({id: "camera", position: [0.0, 2.0, 5.6], target: [0.0, 0.75, 0.0], fov: 45.0}),
    s.light('ambient', {intensity: 0.65}),
    s.light('directional', {position: [2.0, 4.0, 3.0], intensity: 0.85}),
    s.mesh(s.box([8.4, 0.1, 3.6]), s.material('lambert', {color: "#2f4b45"}), {position: [0.0, -0.05, 0.0]}),
    *[for (g in compiled) motion.control(g, states(), {state: choice, cue: cue, fade: 0.2})^]
], {id: "pets-view", camera: "camera", width: 960, height: 440, background: "#162436"}))^

// `playing` records what the controllers announce
view <parade> state choice: 'idle', cue: 0, playing: "idle" {
    let viewport = stage(choice, cue) ^ { <p class: "missing", "The pets could not load: " ++ string(^.message)> };
    <div id: "parade", 'data-state': playing,
        ui.buttons(choices, string(choice));
        <div class: "stage", viewport>
        <p class: "caption", "Kenney's Cube Pets (CC0), three FBX files of rigid parts. Pressing the playing clip again restarts it.">>
}
on click(evt) {
    let id = dom.get_attribute(evt.target, "id")
    if (id != null and starts_with(id, "play-")) {
        let next = symbol(slice(id, 5, len(id)))
        if (next == choice) { cue = cue + 1 } else { choice = next }
    }
}
on statechange(evt) { if (evt.controller == "cat-motion") { playing = string(evt.to) } }

ui.page("Cube pets", "LAMBDA MOTION / RIGID RIGS",
    "A cat, a dog and a penguin from Kenney's Cube Pets. Each is a stack of rigid FBX parts with no skeleton; the converter turns every part into a joint, so the pets play their own clips through the same character pipeline as skinned bodies.",
    apply(<parade>))
