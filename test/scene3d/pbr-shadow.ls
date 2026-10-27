// Shadows from a directional light (the `shadow` case of mod_pbr.ls): spheres and a cut-out leaf
// on a tilted ground, against Three.js r186's PCF shadow map (test/scene3d/oracle/).
import P: .mod_pbr

let d = P.description("shadow");
<html
    <head <style "html,body{margin:0;background:#202428} scene3d{display:block}">>
    <body (P.scene(d))>
>
