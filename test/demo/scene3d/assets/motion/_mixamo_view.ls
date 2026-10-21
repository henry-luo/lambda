// Local-only (Lambda_Pkg_Motion.md §11; steps in README.md): the Mixamo
// character dances the Mixamo samba, retargeted because the two exports use
// different joint frames. Mixamo files are never committed, so this is not a
// registered case; _mixamo_view.json replays it headlessly.
import s: lambda.scene3d
import motion: lambda.scene3d.motion

let hero = motion.load("temp/mixamo/mixamo.fbx", {rig: 'mixamo'})^
let samba = motion.load("temp/mixamo/Samba Dancing.fbx", {rig: 'mixamo', clip: "mixamo.com"})^
let compiled = motion.scene(motion.attach(hero, {samba: samba})^, {id: "dancer", autoplay: "samba"})^

let viewport = s.normalize(s.scene([
    s.camera({id: "camera", position: [0.0, 1.2, 4.5], target: [0.0, 1.0, 0.0], fov: 45.0}),
    s.light('ambient', {intensity: 0.6}),
    s.light('directional', {position: [1.0, 2.0, 3.0], intensity: 0.8}),
    compiled
], {id: "dancer-view", camera: "camera", width: 240, height: 320, viewBox: "0 0 240 320",
    background: "#20242b", style: "display:block"}))^;
<html
    <head <style "html,body{margin:0;background:#ffffff}">>
    <body viewport>
>
