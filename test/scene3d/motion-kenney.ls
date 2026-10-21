// A third-party CC0 character (Kenney, Animated Characters Protagonists): its
// FBX converts to a <character>, takes Kenney's jump by joint name, and wears
// a skin the pack ships separately.
import s: lambda.scene3d
import motion: lambda.scene3d.motion

let dir = "test/demo/scene3d/assets/motion/kenney/"
let hero = motion.load(dir ++ "characterMedium.fbx")^
let jump = motion.load(dir ++ "jump.fbx", {clip: "Root|Jump"})^
let compiled = motion.scene(motion.attach(hero, {jump: jump})^, {id: "kenney", autoplay: "jump"})^

fn fields(n) map => map([for (k, x in n where k is string or k is symbol) (string(k), x)])
// the pack's FBX names no texture; its skins are separate images
fn dressed(group, image) element => <group *: fields(group), *[for (x in content(group))
    if (name(x) == 'resources') <resources *: fields(x), *[*[for (m in content(x))
        if (name(m) == 'material') <material *: {*: fields(m), color: "#ffffff", texture: "kenney-skin"}> else m],
        <texture id: "kenney-skin", src: image>]>
    else x]>;

let viewport = s.normalize(s.scene([
    s.camera({id: "camera", position: [0.0, 2.2, 9.0], target: [0.0, 1.9, 0.0], fov: 45.0}),
    s.light('ambient', {intensity: 0.6}),
    s.light('directional', {position: [1.0, 2.0, 3.0], intensity: 0.8}),
    dressed(compiled, "../demo/scene3d/assets/motion/kenney/skaterMaleA.png")
], {id: "kenney-view", camera: "camera", width: 200, height: 240, viewBox: "0 0 200 240",
    background: "#20242b", style: "display:block"}))^;
<html
    <head <style "html,body{margin:0;background:#ffffff}">>
    <body viewport>
>
