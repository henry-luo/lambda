// A skinned caster's shadow follows its pose: a horizontal tile bound to one bone
// slides along x under a clip, and its shadow slides with it across a Lambert
// ground lit by a casting sun (Radiant_Scene3d_PBR_Shadows.md M4).
import s: lambda.scene3d

let tile = [-0.9, 1.0, -0.3, -0.3, 1.0, -0.3, -0.3, 1.0, 0.3, -0.9, 1.0, 0.3]
let ground = [-3.0, 0.0, -3.0, 3.0, 0.0, -3.0, 3.0, 0.0, 3.0, -3.0, 0.0, 3.0]
let rig = <skeleton id: "rig", <bone id: "root", <bone id: "arm", position: [-0.6, 1.0, 0.0]>>>
// the bone slides 1.2 m along x over one second, then holds
let slide = s.clip("slide", [s.track("arm.position", [0.0, 1.0], [-0.6, 1.0, 0.0, 0.6, 1.0, 0.0], 'vector')],
    {autoplay: true, duration: 2.0});
<html
    <head <style "html,body{margin:0;background:#000} scene3d{display:block}">>
    <body (s.scene([
        s.camera({id: "camera", position: [0.0, 5.0, 4.0], target: [0.0, 0.0, 0.0], fov: 45.0}),
        s.light('ambient', {intensity: 0.2}),
        s.light('directional', {position: [2.0, 5.0, 0.0], target: [0.0, 0.0, 0.0], 'cast-shadow': true, 'shadow-map-size': 1024,
            'shadow-softness': 1.0}),
        rig, slide,
        s.mesh(s.geometry(ground, {indices: [0, 2, 1, 0, 3, 2]}), s.material('lambert', {color: "#ffffff"}), {id: "ground"}),
        s.mesh(s.geometry(tile, {indices: [0, 2, 1, 0, 3, 2], 'skin-indices': [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0],
            'skin-weights': [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0]}),
            s.material('lambert', {color: "#ff3030", side: 'double'}), {id: "tile", skeleton: "rig"})
    ], {id: "scene", camera: "camera", width: 320, height: 240}))>
>
