// Butterflies: a swarm of 4,096 butterflies with flexible wings, and three single butterflies whose
// wing motion the sliders tune. Lambda describes the scenes and sets uniforms; the GPU simulates the
// swarm (a <simulation>), bends every wing (a shader material) and paints from a Lambda-drawn SVG atlas.
// Open with ./lambda.exe view test/demo/scene3d/butterflies/butterflies.ls
import dom
import s: lambda.scene3d
import fx: lambda.scene3d.effects
import navigation: ~~._navigation
import atlas: .mod_atlas
import glsl: .mod_glsl

// the atlas is painted and serialized once; every viewport samples the same mipmapped texture
let ATLAS = fx.svg_texture(atlas.atlas(), {id: "atlas", width: 2048, height: 2048, 'min-filter': 9987})^
let SWARM = 64      // 64 × 64 simulation records, one butterfly each
let HOUR = 3600.0   // the flight clip; the simulation restarts when it loops

// one wing grid, 12 × 8 cells hinged on the body axis, so the wings can bend
fn wing_grid() element {
    let sx = 12, sy = 8;
    s.geometry([for (j in 0 to sy) for (i in 0 to sx) *[i * 1.0 / sx - 0.5, j * 1.0 / sy - 0.5, 0.0]], {id: "wing-grid",
        uvs: [for (j in 0 to sy) for (i in 0 to sx) *[i * 1.0 / sx, j * 1.0 / sy]],
        normals: [for (j in 0 to sy) for (i in 0 to sx) *[0.0, 0.0, 1.0]],
        indices: [for (j in 0 to (sy - 1)) for (i in 0 to (sx - 1)) (let a = j * (sx + 1) + i, *[a, a + 1, a + sx + 2, a, a + sx + 2, a + sx + 1])]})
}
fn flight(paths, from = 0.0) element => s.clip("flight", [for (path in paths) s.track(path, [0.0, HOUR], [from, from + HOUR])],
    {duration: HOUR, autoplay: true})

// the swarm: the simulation moves every butterfly; the material places, bends and paints them
fn swarm() element => s.scene([
    s.camera({id: "main", fov: 45.0, near: 1.0, far: 200.0, position: [0.0, 0.0, 60.0]}),
    s.resources([ATLAS, wing_grid()]),
    s.simulation("swarm", SWARM, SWARM, [s.variable("position", glsl.POSITION_STEP), s.variable("velocity", glsl.VELOCITY_STEP)],
        // the visible half-extent at the swarm's depth: 60 × tan(22.5°) high, times the 760 × 600 aspect
        {time: 0.0, bounds: [31.48, 24.85]}),
    <mesh geometry: "wing-grid", 'instance-count': SWARM * SWARM,
        s.shader_material(glsl.SWARM_VERTEX, glsl.FRAGMENT,
            {time: 0.0, columns: SWARM * 1.0, positions: "swarm.position", velocities: "swarm.velocity", atlas: "atlas"},
            {id: "flock", side: 'double', 'alpha-to-coverage': true})>,
    // a three-second head start opens the page mid-flight
    flight(["swarm.uniforms.time", "flock.uniforms.time"], 3.0)
], {id: "swarm-view", width: 760, height: 600, viewBox: "0 0 760 600", camera: "main", background: "#ffffff",
    role: "img", 'aria-label': "A swarm of butterflies in clusters and streams"})

// the sliders: every one is a uniform of the single-butterfly material
let SLIDERS = [
    {key: "species", label: "Species", min: 0.0, max: 15.0, step: 1.0},
    {key: "flex", label: "Flex", min: 0.0, max: 1.5, step: 0.05},
    {key: "curl", label: "Curl", min: 0.0, max: 4.0, step: 0.1},
    {key: "lag", label: "Hindwing lag", min: 0.0, max: 2.0, step: 0.05},
    {key: "beat", label: "Flap speed", min: 0.5, max: 8.0, step: 0.1},
    {key: "amplitude", label: "Flap height", min: 0.4, max: 1.6, step: 0.05}
]
// a stiff small wing, a supple medium one and a very flexible large one
let PRESETS = [
    {species: 1, flex: 0.35, curl: 2.2, lag: 0.9, beat: 3.2, amplitude: 1.2},
    {species: 7, flex: 0.8, curl: 2.6, lag: 1.05, beat: 2.6, amplitude: 1.3},
    {species: 11, flex: 1.25, curl: 3.2, lag: 1.4, beat: 2.0, amplitude: 1.4}
]

fn solo(i, card) element => s.scene([
    s.camera({id: "main", fov: 40.0, position: [0.0, 0.0, 1.9]}),
    s.resources([ATLAS, wing_grid()]),
    <mesh geometry: "wing-grid",
        s.shader_material(glsl.SOLO_VERTEX, glsl.FRAGMENT, {time: 0.0, species: card.species * 1.0, flex: card.flex,
            curl: card.curl, lag: card.lag, beat: card.beat, amplitude: card.amplitude, atlas: "atlas"},
            {id: "wing", side: 'double', 'alpha-to-coverage': true})>,
    flight(["wing.uniforms.time"])
], {id: "solo-" ++ string(i), width: 300, height: 190, viewBox: "0 0 300 190", camera: "main", background: "#f6f2ea",
    role: "img", 'aria-label': atlas.SPECIES[card.species].name ++ " flapping"})

fn shown(slider, value) string => if (slider.key == "species") atlas.SPECIES[value].name else string(value)
fn showcase(i, card) element => <article class: "butterfly-card",
    <header <h2 atlas.SPECIES[card.species].name> <span class: "card-index", "0" ++ string(i + 1)>>;
    solo(i, card);
    <div class: "sliders",
        for (slider in SLIDERS) <label class: "slider",
            <span class: "slider-name", slider.label>
            <input id: "card-" ++ string(i) ++ "-" ++ slider.key, type: "range", min: string(slider.min), max: string(slider.max),
                step: string(slider.step), value: string(card[slider.key]), 'aria-label': slider.label>
            <output shown(slider, card[slider.key])>>>>

// one setting of one card; species is a whole atlas cell
fn tuned(card, key, value) map => {
    species: if (key == "species") int(value) else card.species,
    flex: if (key == "flex") value else card.flex,
    curl: if (key == "curl") value else card.curl,
    lag: if (key == "lag") value else card.lag,
    beat: if (key == "beat") value else card.beat,
    amplitude: if (key == "amplitude") value else card.amplitude
}

// the swarm never changes, so slider re-renders reproject only the card that moved (SH6)
view <studio> state cards: PRESETS {
    <div id: "studio", class: "studio",
        <section class: "swarm-card", swarm();
            <p class: "caption", "4,096 butterflies, simulated and drawn on the GPU. Larger wings beat slower and bend more.">>
        <aside class: "showcase", 'aria-label': "Single butterflies", for (i, card in cards) showcase(i, card)>>
}
on input(evt) {
    let parts = split(string(dom.get_attribute(evt.target, "id")), "-")
    if (len(parts) != 3 or parts[0] != "card") { return 'pass' }
    let index = int(parts[1])
    let value = float(dom.get_state(evt.target, "value"))
    cards = [for (i, card in cards) if (i == index) tuned(card, parts[2], value) else card]
    return 'handled'
}

<html lang: "en",
    <head <meta charset: "utf-8"> <meta name: "viewport", content: "width=device-width,initial-scale=1">
        <title "Butterflies / Scene3D">
        <link rel: "stylesheet", href: "../demo.css"> <link rel: "stylesheet", href: "butterflies.css">>
    <body <main class: "butterflies-page",
        navigation.breadcrumbs("Butterflies", "../index.html");
        <header class: "page-heading",
            <div <p class: "eyebrow", "SCENE3D / GPU SIMULATION"> <h1 "Butterflies"> <p class: "intro", "A swarm, and three wings up close.">>
            <p class: "heading-note", "Drag a slider to retune a wing while it flies.">>
        apply(<studio>)>>
>
