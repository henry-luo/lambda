// Mixamo's Beta character, a local-only fixture (Lambda_Pkg_Motion.md §11):
// Mixamo files are never committed, so the page explains how to fetch them when
// temp/mixamo/ is empty. Beta plays its own dance, the samba of another Mixamo
// character and a CMU-named jump; both are retargeted, since their joint frames
// differ from Beta's (§8.3–8.4). _mixamo_replay.json replays it headlessly.
import dom
import s: lambda.scene3d
import motion: lambda.scene3d.motion
import ui: ._ui

let dir = "temp/mixamo/"
let choices = [["samba", "Samba"], ["own", "Its own dance"], ["leap", "Jump"]]
fn states() => [motion.motion_state('samba', "samba"), motion.motion_state('own', "mixamo.com"), motion.motion_state('leap', "leap")]

fn dancer() element^ {
    let hero = motion.load(dir ++ "mixamo.fbx", {rig: 'mixamo'})^
    let samba = motion.load(dir ++ "Samba Dancing.fbx", {rig: 'mixamo', clip: "mixamo.com"})^
    let jump = motion.load("test/demo/scene3d/assets/motion/jump.bvh", {scale: 0.01, rig: 'cmu'})^;
    motion.scene(motion.attach(hero, {samba: samba, leap: jump})^, {id: "dancer"})^
}
// compiled once; each render only appends the controller
let compiled = dancer() ^ { null }

fn stage(choice, cue) element^ => s.normalize(s.scene([
    s.camera({id: "camera", position: [0.0, 1.2, 4.2], target: [0.0, 0.95, 0.0], fov: 45.0}),
    s.light('ambient', {intensity: 0.6}),
    s.light('directional', {position: [1.0, 2.0, 3.0], intensity: 0.8}),
    s.mesh(s.box([4.0, 0.04, 2.6]), s.material('lambert', {color: "#2b3a52"}), {position: [0.0, -0.02, 0.0]}),
    motion.control(compiled, states(), {state: choice, cue: cue, fade: 0.3})^
], {id: "dancer-view", camera: "camera", width: 960, height: 440, background: "#20242b"}))^

fn missing() element => <div id: "mixamo-missing", class: "missing",
    <p "Mixamo files are not committed with Lambda. Fetch the two Mixamo exports that Three.js ships, then reopen this page:">
    <p <code "sh test/demo/scene3d/assets/motion/_fetch_mixamo.sh">>>

// `playing` records what the controller announces
view <dance> state choice: 'samba', cue: 0, playing: "samba" {
    let viewport = if (compiled == null) missing() else stage(choice, cue) ^ { <p class: "missing", "The dance could not compile: " ++ string(^.message)> };
    <div id: "dance", 'data-state': playing,
        if (compiled != null) ui.buttons(choices, string(choice));
        <div class: "stage", viewport>
        <p class: "caption", "Mixamo's Beta character, from the Mixamo exports Three.js r186 ships. The samba comes from another Mixamo character and the jump from a CMU-named BVH; both are retargeted onto Beta.">>
}
on click(evt) {
    let id = dom.get_attribute(evt.target, "id")
    if (id != null and starts_with(id, "play-")) {
        let next = symbol(slice(id, 5, len(id)))
        if (next == choice) { cue = cue + 1 } else { choice = next }
    }
}
on statechange(evt) { playing = string(evt.to) }

ui.page("Mixamo dance", "LAMBDA MOTION / RETARGETING",
    "One Mixamo character, three motions. Beta dances its own clip, takes a samba recorded on a differently built Mixamo rig, and jumps to a CMU-style BVH, all through one character schema and the shared mixer.",
    apply(<dance>))
