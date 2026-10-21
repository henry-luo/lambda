// A BVH jump drives a mannequin (Lambda_Pkg_Motion.md M1): the file converts
// to a <character>, gains a capsule body, and compiles into scene3d content.
import s: lambda.scene3d
import motion: lambda.scene3d.motion

let hero = motion.load("test/demo/scene3d/assets/motion/jump.bvh", {scale: 0.01})^
let figure = motion.mannequin(hero, {radius: 0.04, color: "#f0f0f0"})^
let viewport = s.normalize(s.scene([
    s.camera({id: "camera", position: [0.0, 1.1, 3.2], target: [0.0, 1.0, 0.0], fov: 45.0}),
    s.light('ambient'),
    motion.scene(figure, {id: "jumper", autoplay: "jump"})^
], {id: "jumper-view", camera: "camera", width: 200, height: 240, viewBox: "0 0 200 240",
    background: "#20242b", style: "display:block"}))^;
<html
    <head <style "html,body{margin:0;background:#ffffff}">>
    <body viewport>
>
