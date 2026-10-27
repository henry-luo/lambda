// Standard materials: by default a grid where metalness rises to the right and
// roughness upward. PBR_CASE picks another case (mod_pbr.ls), such as `maps`, one
// texture map per sphere; test/scene3d/oracle/ renders the same description with
// Three.js to compare.
import P: .mod_pbr

let key = sys.proc.self.env.PBR_CASE# or "direct"
let d = P.description(if (P.CASES[key] != null) key else "direct");
<html
    <head <style "html,body{margin:0;background:#202428} scene3d{display:block}">>
    <body (P.scene(d))>
>
