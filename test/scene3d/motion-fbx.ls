// An FBX character with a skinned, textured body (Lambda_Pkg_Motion.md M3):
// the file converts to a <character>, and motion.scene compiles its skeleton,
// two material meshes and jump clip into scene3d content.
import s: lambda.scene3d
import motion: lambda.scene3d.motion

let hero = motion.load("test/demo/scene3d/assets/motion/jump.fbx")^
let viewport = s.normalize(s.scene([
    s.camera({id: "camera", position: [0.0, 1.1, 3.2], target: [0.0, 1.0, 0.0], fov: 45.0}),
    s.light('ambient', {intensity: 0.6}),
    s.light('directional', {position: [1.0, 2.0, 3.0], intensity: 0.8}),
    motion.scene(hero, {id: "hero", autoplay: "jump"})^
], {id: "hero-view", camera: "camera", width: 200, height: 240, viewBox: "0 0 200 240",
    background: "#20242b", style: "display:block"}))^;
<html
    <head <style "html,body{margin:0;background:#ffffff}">>
    <body viewport>
>
