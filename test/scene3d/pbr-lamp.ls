// Shadows from a point light (the `lamp` case of mod_pbr.ls): a lamp in a closed room throws
// sphere shadows on the floor, ceiling and walls, against Three.js r186's cube shadow map
// (test/scene3d/oracle/).
import P: .mod_pbr

let d = P.description("lamp");
<html
    <head <style "html,body{margin:0;background:#202428} scene3d{display:block}">>
    <body (P.scene(d))>
>
