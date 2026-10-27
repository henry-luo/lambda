// Standard-material texture maps, one per sphere (the `maps` case of mod_pbr.ls):
// base color, metal-rough, normal from derivatives, normal from vertex tangents,
// occlusion with emission, and an alpha mask. test/scene3d/oracle/ checks the
// same description against Three.js.
import P: .mod_pbr

let d = P.description("maps");
<html
    <head <style "html,body{margin:0;background:#202428} scene3d{display:block}">>
    <body (P.scene(d))>
>
