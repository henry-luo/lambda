// Standard materials lit only by the room environment (the `room` case of mod_pbr.ls):
// the sphere grid against Three.js r186's RoomEnvironment prefiltered by PMREM (test/scene3d/oracle/).
import P: .mod_pbr

let d = P.description("room");
<html
    <head <style "html,body{margin:0;background:#202428} scene3d{display:block}">>
    <body (P.scene(d))>
>
