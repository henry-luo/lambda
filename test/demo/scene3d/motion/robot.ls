// Quaternius' Animated Robot (CC0): a skinned Blender FBX with fourteen clips.
// The template owns the requested clip and the motion controller crossfades to
// it; gestures play once and return to idling (Lambda_Pkg_Motion.md §9.2).
import dom
import s: lambda.scene3d
import motion: lambda.scene3d.motion
import ui: ._ui

let clip = (name) => "RobotArmature|Robot_" ++ name
let choices = [{id: "idle", label: "Idle", clip: clip("Idle")}, {id: "walk", label: "Walk", clip: clip("Walking")},
    {id: "run", label: "Run", clip: clip("Running")}, {id: "dance", label: "Dance", clip: clip("Dance")},
    {id: "wave", label: "Wave", clip: clip("Wave"), once: true}, {id: "yes", label: "Yes", clip: clip("Yes"), once: true},
    {id: "no", label: "No", clip: clip("No"), once: true}, {id: "thumbs-up", label: "Thumbs up", clip: clip("ThumbsUp"), once: true},
    {id: "punch", label: "Punch", clip: clip("Punch"), once: true}, {id: "jump", label: "Jump", clip: clip("Jump"), once: true}]
// compiled once; each render only appends the controller
let compiled = motion.scene(motion.load("test/demo/scene3d/assets/motion/robot/Robot.fbx")^, {id: "robot"})^

fn stage(choice, cue) element^ => s.normalize(s.scene([
    s.camera({id: "camera", position: [0.0, 2.8, 8.4], target: [0.0, 2.0, 0.0], fov: 45.0}),
    // three-point rig: warm key, cool fill and a rim light so the robot reads against the backdrop
    s.light('ambient', {intensity: 1.45}),
    s.light('directional', {position: [3.0, 6.0, 6.0], color: "#fff4e6", intensity: 1.7}),
    s.light('directional', {position: [-5.0, 3.0, 4.0], color: "#dbe7ff", intensity: 0.8}),
    s.light('directional', {position: [0.0, 5.0, -6.0], color: "#ffffff", intensity: 0.6}),
    s.mesh(s.box([9.0, 0.1, 5.0]), s.material('lambert', {color: "#34465f"}), {position: [0.0, -0.05, 0.0]}),
    motion.control(compiled, ui.states(choices, 'idle'), {state: choice, cue: cue, fade: 0.25})^
], {id: "robot-view", camera: "camera", width: 960, height: 440, background: "#31496b"}))^

// `playing` records what the controller announces
view <workshop> state choice: 'idle', cue: 0, playing: "idle" {
    let viewport = stage(choice, cue) ^ { <p class: "missing", "The robot could not load: " ++ string(^.message)> };
    <div id: "workshop", 'data-state': playing,
        ui.buttons(choices, string(choice));
        <div class: "stage", viewport>
        <p class: "caption", "Quaternius' Animated Robot (CC0), one binary FBX. Pressing the playing clip again restarts it.">>
}
on click(evt) {
    let picked = ui.pick(dom.get_attribute(evt.target, "id"), choice, cue)
    choice = picked[0]
    cue = picked[1]
}
on statechange(evt) { playing = string(evt.to) }

ui.page("Animated robot", "LAMBDA MOTION / SKINNED CHARACTER",
    "Quaternius' robot walks, runs, dances and gestures. Its FBX converts into the same character schema as every other rig; pick a move and the controller crossfades into it, and gestures return to idling when they finish.",
    apply(<workshop>))
