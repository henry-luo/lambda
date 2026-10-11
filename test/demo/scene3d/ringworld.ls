// visual demo: open with ./lambda.exe view test/demo/scene3d/ringworld.ls
import s: lambda.scene3d

let pi = 3.141592653589793

fn sphere_point(latitude, longitude, rows, columns) array {
    let theta = pi * latitude / rows
    let phi = 2.0 * pi * longitude / columns;
    [sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi)]
}

fn planet_color(latitude, longitude, rows, columns) array {
    let theta = pi * latitude / rows
    let phi = 2.0 * pi * longitude / columns
    // a longitude-dependent storm makes the planet's rotation visible.
    let storm = exp(0.0 - (((theta - 1.8) / 0.15) ** 2 + ((phi - 0.95) / 0.32) ** 2))
    let stripe = sin(theta * 25.0 + 0.5 * sin(phi * 3.0) + 2.0 * storm)
    let detail = 0.035 * sin(theta * 93.0 + cos(phi * 7.0))
    let color = if (stripe > 0.5) [0.98, 0.77, 0.52]
        else if (stripe < -0.5) [0.62, 0.31, 0.23]
        else [0.88, 0.53, 0.34];
    [for (i in 0 to 2, let c = color[i] * (1.0 - storm * 0.55) + detail)
        min(1.0, max(0.0, c + (if (i == 0) storm * 0.35 else 0.0)))]
}

fn sphere(id, rows, columns, banded) element {
    let positions = [for (j in 0 to rows) for (i in 0 to columns)
        *sphere_point(j, i, rows, columns)]
    // the duplicated seam keeps each triangle local in longitude.
    let indices = [for (j in 0 to (rows - 1), i in 0 to (columns - 1),
        let a = j * (columns + 1) + i, let b = a + columns + 1)
        *[a, a + 1, b, a + 1, b + 1, b]]
    let colors = if (banded) [for (j in 0 to rows) for (i in 0 to columns)
        *planet_color(j, i, rows, columns)] else null;
    s.geometry(positions, {id: id, normals: positions, indices: indices,
        *: (if (banded) {colors: colors} else {})})
}

fn ring(inner, outer, color, options = {}) element {
    let segments = 144
    let positions = [for (i in 0 to segments) for (r in [inner, outer])
        *[r * cos(2.0 * pi * i / segments), 0.0, r * sin(2.0 * pi * i / segments)]]
    let normals = [for (i in 0 to segments) *[0.0, 1.0, 0.0, 0.0, 1.0, 0.0]]
    let indices = [for (i in 0 to (segments - 1), let a = i * 2)
        *[a, a + 2, a + 1, a + 1, a + 2, a + 3]];
    s.mesh(s.geometry(positions, {normals: normals, indices: indices}),
        s.material('lambert', {color: color, side: 'double'}), options)
}

fn moon(position, radius, material) element => <mesh geometry: "moon", material: material,
    position: position, scale: [radius, radius, radius]>

fn orbit_track(id, turns) element {
    let steps = turns * 4;
    s.track(id ++ ".quaternion", [for (i in 0 to steps) 48.0 * i / steps],
        [for (i in 0 to steps) *[0.0, sin(pi * i / 4.0), 0.0, cos(pi * i / 4.0)]], 'quaternion')
}

fn control(id, label, title) element => <button id: id, type: "button", title: title, 'aria-label': title, label>

let stars = [for (i in 0 to 159) {
    // deterministic scatter gives a stable composition without an external asset.
    let x = 28.0 * ((i * 73 % 163) / 163.0 - 0.5)
    let y = 19.0 * ((i * 47 % 167) / 167.0 - 0.5)
    let z = -9.0 - (i % 7)
    let radius = if (i % 13 == 0) 0.036 else 0.016;
    s.transform([x, y, z], [0.0, 0.0, 0.0], [radius, radius, radius])^
}]

let viewport = s.normalize(s.scene([
    s.camera({id: "main", position: [7.5, 4.8, 11.0], target: [0.0, 0.0, 0.0], fov: 42.0}),
    s.light('ambient', {color: "#b5c7ef", intensity: 0.18}),
    s.light('directional', {position: [-7.0, 9.0, 6.0], color: "#fff0d8", intensity: 1.15}),
    s.light('directional', {position: [5.0, -2.0, -5.0], color: "#729bff", intensity: 0.38}),
    s.resources([
        sphere("planet", 56, 96, true),
        sphere("moon", 20, 32, false),
        s.box([1.0, 1.0, 1.0], {id: "star"}),
        s.material('lambert', {id: "surface", color: "#ffffff"}),
        s.material('lambert', {id: "ice", color: "#74ded3"}),
        s.material('lambert', {id: "dust", color: "#dfbda4"}),
        s.material('lambert', {id: "violet", color: "#aaa1e5"}),
        s.material('basic', {id: "starlight", color: "#b6cbe3"})
    ]),
    <mesh geometry: "star", material: "starlight", instances: stars>,
    s.group([
        <mesh id: "planet-spin", geometry: "planet", material: "surface", scale: [2.1, 1.95, 2.1]>,
        ring(2.62, 2.76, "#635b61"),
        ring(2.80, 3.12, "#b79b82"),
        ring(3.15, 3.23, "#e0c39a"),
        ring(3.26, 3.73, "#b69474"),
        ring(3.77, 3.87, "#e8cfa4"),
        ring(3.99, 4.13, "#7e746f"),
        ring(4.17, 4.24, "#b69b82")
    ], {rotation: [0.14, 0.0, -0.27]}),
    s.group([
        ring(5.22, 5.235, "#315466"),
        s.group([moon([-4.65, 0.0, 2.4], 0.54, "ice")], {id: "ice-orbit"})
    ], {rotation: [-0.16, 0.0, 0.13]}),
    s.group([
        ring(6.02, 6.032, "#383c57"),
        s.group([moon([4.72, 0.0, -3.72], 0.33, "violet")], {id: "violet-orbit"})
    ], {rotation: [0.38, 0.0, -0.2]}),
    s.group([moon([4.0, 3.0, 0.5], 0.22, "dust")], {id: "dust-orbit"}),
    s.clip("orbits", [orbit_track("planet-spin", 4), orbit_track("ice-orbit", 2),
        orbit_track("violet-orbit", 1), orbit_track("dust-orbit", 3)], {duration: 48.0, autoplay: true})
], {id: "ringworld", camera: "main", width: 1040, height: 570,
    viewBox: "0 0 1040 570", background: "#080e1b",
    tabindex: "0", role: "img", 'aria-label': "Interactive ringed planet and three orbiting moons",
    'aria-describedby': "navigation-help", style: "display:block;width:100%;height:auto;touch-action:none"}))^;

<html
    <head
        <title "Ringworld / Native 3D">
        <link rel: "stylesheet", href: "ringworld.css">
        <script type: "importmap", "{\"imports\":{\"three\":\"./vendor/three/three.module.js\"}}">
    >
    <body
        <main *[
            <header *[
                <div *[<p class: "eyebrow", "PROCEDURAL PLANET STUDY / 001">, <h1 "RINGWORLD">]>,
                <div class: "edition", "LAMBDA + RADIANT">
            ]>,
            <nav class: "controls", 'aria-label': "Scene controls", *[
                <div class: "control-group", *[
                    <span class: "label", "PLAYBACK">,
                    <button id: "auto-play", type: "button", 'aria-pressed': "false",
                        title: "Automatically orbit, pan, tilt and zoom the camera", "Auto Play">,
                    <button id: "play", type: "button", 'aria-pressed': "true", "Pause">,
                    control("rewind", "↺", "Restart animation")
                ]>,
                <div class: "control-group", *[
                    <span class: "label", "DRAG">,
                    <button id: "orbit-mode", type: "button", 'aria-pressed': "true", "Orbit">,
                    <button id: "pan-mode", type: "button", 'aria-pressed': "false", "Pan">
                ]>,
                <div class: "control-group", *[
                    <span class: "label", "ORBIT / TILT">,
                    control("orbit-left", "←", "Orbit left"), control("orbit-right", "→", "Orbit right"),
                    control("tilt-up", "↑", "Tilt up"), control("tilt-down", "↓", "Tilt down")
                ]>,
                <div class: "control-group", *[
                    <span class: "label", "ZOOM">,
                    control("zoom-in", "+", "Zoom in"), control("zoom-out", "−", "Zoom out"),
                    control("reset", "Reset view", "Reset camera and pan"),
                    control("top", "Top", "View the rings from above"), control("side", "Side", "View the planet at its equator")
                ]>
            ]>,
            <p id: "navigation-help", "Auto Play tours the camera · Drag to orbit · Pan mode or Shift-drag to move · Scroll to zoom · Arrow keys pan · Shift + arrows tilt · + / − zoom · R resets · Space pauses">,
            viewport,
            <div class: "caption", *[
                <div *[
                    <h2 "A small world, in motion.">,
                    <p "A rotating storm-banded atmosphere, seven tilted rings and three moons on different orbits. Every surface is generated in Lambda and lit in native 3D.">
                ]>,
                <div class: "stats", *[for (stat in [
                    {value: "03", label: "MOONS"}, {value: "07", label: "RINGS"},
                    {value: "160", label: "STARS"}])
                    <div *[<div class: "stat", stat.value>, <div class: "label", stat.label>]>
                ]>
            ]>
        ]>
        <script type: "module", src: "ringworld.js">
    >
>
