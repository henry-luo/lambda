// A motion controller driven by a template (Lambda_Pkg_Motion.md §9.2, M5):
// the template owns the requested state and cue, the host crossfades, chains
// `next` after a finished `once` state, and announces each change.
import dom
import s: lambda.scene3d
import motion: lambda.scene3d.motion

let hero = motion.load("test/demo/scene3d/assets/motion/jump.bvh", {scale: 0.01, rig: 'cmu'})^
// three clips from one take: the whole jump, its rising part and its mirror image
let rigged = motion.attach(motion.mannequin(hero, {radius: 0.04, color: "#f0f0f0"})^,
    {rise: motion.trim(hero, 0.2, 0.6)^, flip: motion.mirror(hero)^})^
// compiled once; each render only appends the controller
let compiled = motion.scene(rigged, {id: "hero"})^

fn stage(choice, cue) element^ => s.normalize(s.scene([
    s.camera({id: "camera", position: [0.0, 1.1, 3.2], target: [0.0, 1.0, 0.0], fov: 45.0}),
    s.light('ambient'),
    motion.control(compiled, [
        motion.motion_state('jump', "jump"),
        motion.motion_state('flip', "flip"),
        motion.motion_state('rise', "rise", {loop: 'once', next: 'jump', fade: 0.1})
    ], {state: choice, cue: cue, fade: 0.2})^
], {id: "hero-view", camera: "camera", width: 200, height: 240, viewBox: "0 0 200 240",
    background: "#20242b", style: "display:block"}))^

// `playing` records what the host announces; re-rendering with it leaves the request unchanged
view <director> state choice: 'jump', cue: 0, ticks: 0, playing: "jump" {
    let viewport = stage(choice, cue) ^ { <div id: "scene-error"> };
    <div id: "stage", 'data-state': playing, 'data-ticks': string(ticks),
        <button id: "pick-flip", "flip">
        <button id: "pick-rise", "rise">
        <button id: "again", "again">
        <button id: "tick", "tick">
        viewport>
}
on click(evt) {
    let id = dom.get_attribute(evt.target, "id")
    if (id == "pick-flip") { choice = 'flip' }
    else if (id == "pick-rise") { choice = 'rise' }
    else if (id == "again") { cue = cue + 1 }
    else if (id == "tick") { ticks = ticks + 1 }
}
on statechange(evt) { playing = string(evt.to) }
<html
    <head <style "html,body{margin:0;background:#ffffff} #stage{display:flex;flex-direction:column-reverse;align-items:flex-start} button{font-size:10px;margin:1px}">>
    <body apply(<director>)>
>
