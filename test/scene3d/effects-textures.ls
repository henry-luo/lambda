// D4.5.2 / SH1: file SVG, pure SVG data and raster textures share shader sampling.
import s: lambda.scene3d
import fx: lambda.scene3d.effects
let svg = <svg xmlns: "http://www.w3.org/2000/svg", width: 64, height: 32,
    <path d: "M0 0h32v16H0z", fill: "#ff0000">
    <path d: "M32 0h32v16H32z", fill: "#00ff00">
    <path d: "M0 16h32v16H0z", fill: "#0000ff">
    <path d: "M32 16h32v16H32z", fill: "#ffffff">>
fn stage(id, texture, shader = true) element => s.scene([
    s.camera({id: "camera", type: 'orthographic', view_height: 2.0}),
    s.resources([texture]),
    s.mesh(s.plane([4.0, 2.0, 1.0]), if (shader) fx.material('channel-split', "art", {amount: 0.0})^
        else s.material('basic', {texture: "art"}))
], {id: id, width: 96, height: 48, camera: "camera", background: "#000000", style: "display:block"});
<html <head <style "html,body{margin:0;background:#000}">>
    <body
        stage("file-svg", s.texture("assets/effects-quadrants.svg", {id: "art", 'max-size': 32, 'min-filter': 9728, 'mag-filter': 9728}));
        stage("value-svg", fx.svg_texture(svg, {id: "art", width: 128, height: 64, 'max-size': 32, 'min-filter': 9728, 'mag-filter': 9728})^);
        stage("raster", s.texture("quadrants.png", {id: "art", 'min-filter': 9728, 'mag-filter': 9728}));
        stage("basic-svg", s.texture("assets/effects-quadrants.svg", {id: "art", 'min-filter': 9728, 'mag-filter': 9728}), false);
        stage("alpha-svg", fx.svg_texture(<svg xmlns: "http://www.w3.org/2000/svg", width: 64, height: 32,
            <path d: "M0 0h64v32H0z", fill: "#ff0000", opacity: 0.5>>, {id: "art"})^)> >
