// The whole Willow Cove scene, built once: physics world (only kinematic bodies move: the boat,
// every fish, the otter and the camera rig), sun and sky light, the cove, the ripple and splash
// emitters, and every clip. After this the host only sends pose targets, clip commands and
// particle spawns; the scene's DOM never changes, so it is never re-projected.
import s: lambda.scene3d
import physics: lambda.physics
import cam: ~~.midtown.mod_camera
import u: ~~.midtown.mod_util
import w: .mod_world
import lagoon: .mod_lagoon
import boat: .mod_boat
import angler: .mod_angler
import fish: .mod_fish
import otter: .mod_otter

pub let NOBODY = {membership: 0, filter: 0}
// rings: final diameter, lifetime, band width at full size
// ripples: final diameter, lifetime, and how many trailing rings follow the leading crest
pub let RINGS = {'ring-s': {size: 1.5, life: 1.9, rings: 2}, 'ring-m': {size: 2.5, life: 2.4, rings: 3}, 'ring-l': {size: 3.8, life: 2.9, rings: 3}}
fn ring_geometry(band) any^ {
    let seg = 56, inner = 0.5 - band
    let positions = [for (i in 0 to seg - 1, c in ring_pair(i * u.TAU / float(seg), inner)) c]
    let indices = [for (i in 0 to seg - 1, k in ring_quad(i, seg)) k];
    s.geometry(positions, {normals: [for (i in 0 to 2 * seg - 1, c in [0.0, 1.0, 0.0]) c], indices: indices})
}
fn ring_pair(a, inner) => [cos(a) * 0.5, 0.0, sin(a) * 0.5, cos(a) * inner, 0.0, sin(a) * inner]
fn ring_quad(i, seg) => (let a = i * 2, let b = ((i + 1) % seg) * 2, [a, b, a + 1, b, b + 1, a + 1])
// A natural ripple as one disc of vertex colour and alpha: a bright leading crest with a darker trough
// behind it, then weaker trailing crests, each band soft-edged; the radius wobbles a little around
// the circle so no ring is a perfect circle. The emitter's size curve spreads it (the bands widen as
// it travels, as real ripples' wavelength does) while its opacity curve fades it.
let CREST = [0.97, 0.99, 0.95]
let TROUGH = [0.02, 0.16, 0.18]
fn bump(x, at, width) => exp(0.0 - ((x - at) / width) * ((x - at) / width))
fn ripple_band(r, rings) any^ => (let crest = 0.62 * bump(r, 0.9, 0.035) + (if (rings > 1) 0.36 * bump(r, 0.74, 0.04) else 0.0) +
        (if (rings > 2) 0.2 * bump(r, 0.58, 0.045) else 0.0),
    let trough = 0.34 * bump(r, 0.82, 0.04) + (if (rings > 1) 0.2 * bump(r, 0.66, 0.045) else 0.0) + (if (rings > 2) 0.1 * bump(r, 0.5, 0.05) else 0.0),
    let a = min(0.85, crest + trough), let k = crest / max(crest + trough, 0.0001),
    [TROUGH[0] + (CREST[0] - TROUGH[0]) * k, TROUGH[1] + (CREST[1] - TROUGH[1]) * k, TROUGH[2] + (CREST[2] - TROUGH[2]) * k, a])
fn wobble(a) => 1.0 + 0.03 * sin(3.0 * a + 1.3) + 0.018 * sin(7.0 * a + 0.4) + 0.01 * sin(13.0 * a + 2.0)
fn ripple_geometry(rings) any^ {
    let seg = 64, radial = 26
    let rs = [for (j in 0 to radial) 0.4 + 0.6 * j / float(radial)]
    let positions = [for (j in 0 to radial, i in 0 to seg - 1, c in ripple_point(rs[j], i * u.TAU / float(seg))^) c]
    let colors = [for (j in 0 to radial, i in 0 to seg - 1, c in ripple_band(rs[j], rings)^) c]
    let indices = [for (j in 0 to radial - 1, i in 0 to seg - 1, k in ripple_quad(j * seg + i, j * seg + (i + 1) % seg, seg)) k];
    s.geometry(positions, {normals: [for (j in 0 to radial, i in 0 to seg - 1, c in [0.0, 1.0, 0.0]) c], colors: colors, indices: indices})
}
fn ripple_point(r, a) any^ => (let q = 0.5 * r * wobble(a), [cos(a) * q, 0.0, sin(a) * q])
fn ripple_quad(a, b, seg) => [a, b, a + seg, b, b + seg, a + seg]
fn ring_emitter(id, spec) any^ => <'particle-emitter' id: id, backend: "cpu", space: "world", rate: 0.0, burst: 0, capacity: 96,
    lifetime: spec.life, gravity: [0.0, 0.0, 0.0], speed: 0.0, angle: 0.0, size: spec.size,
    ripple_geometry(spec.rings)^; s.material('basic', {color: "#ffffff", transparent: true, side: 'double'});
    <'particle-size-curve' times: [0.0, 0.3, 1.0], values: [0.1, 0.58, 1.0]>
    <'particle-opacity-curve' times: [0.0, 0.07, 0.45, 1.0], values: [0.0, 1.0, 0.6, 0.0]>>
// droplets thrown up by a splash, and a short-lived foam crown
fn splash_emitter() any^ => <'particle-emitter' id: "splash", backend: "cpu", space: "world", rate: 0.0, burst: 0, capacity: 320,
    shape: "cone", angle: 32.0, speed: 2.1, spread: 0.6, lifetime: 0.7, 'lifetime-min': 0.45, gravity: [0.0, -9.81, 0.0], size: 0.05,
    s.sphere(4, 6); s.material('basic', {color: "#f2fbff", transparent: true});
    <'particle-size-curve' times: [0.0, 1.0], values: [1.0, 0.55]>
    <'particle-opacity-curve' times: [0.0, 0.7, 1.0], values: [0.95, 0.85, 0.0]>>
fn foam_emitter() any^ => <'particle-emitter' id: "foam", backend: "cpu", space: "world", rate: 0.0, burst: 0, capacity: 48,
    lifetime: 0.55, gravity: [0.0, 0.0, 0.0], speed: 0.0, angle: 0.0, size: 0.8,
    ring_geometry(0.22)^; s.material('basic', {color: "#ffffff", transparent: true, side: 'double'});
    <'particle-size-curve' times: [0.0, 1.0], values: [0.35, 1.0]>
    <'particle-opacity-curve' times: [0.0, 0.15, 1.0], values: [0.0, 0.95, 0.0]>>
pub let EMITTERS = ["ring-s", "ring-m", "ring-l", "splash", "foam"]

// the camera rides its own kinematic body, so following the boat is a pose command, not an attribute edit
fn camera_body(c) any^ => physics.body("camera-rig", [physics.collider("camera-rig-shape", 'sphere', {radius: 0.05, groups: NOBODY}),
    <camera id: "cam", type: 'perspective', fov: 40.0, near: 0.3, far: 220.0, position: [0.0, 0.0, 0.0]>],
    {type: 'kinematic-position', position: c.position, quaternion: cam.look(c.position, c.target)^})

pub fn scene(st, attributes) any^ => s.scene([
    // 12 substeps of 1/60 s absorb a 200 ms frame on a busy machine before the pacing guard pauses the scene
    physics.world({state: 'playing', gravity: [0.0, -9.81, 0.0], timestep: 1.0 / 60.0, 'max-substeps': 12}),
    s.light('directional', {id: "sun", position: [for (x in lagoon.SUN) x * 60.0], target: [0.0, 0.0, 0.0], color: "#fff1d9", intensity: 1.75,
        'cast-shadow': true, 'shadow-cascades': 2, 'shadow-distance': 40.0, 'shadow-map-size': 2048, 'shadow-normal-bias': 0.03, 'shadow-softness': 1.6}),
    s.light('hemisphere', {id: "sky", position: [0.0, 1.0, 0.0], color: "#cdebf0", 'ground-color': "#40583a", intensity: 0.62}),
    s.resources([*(lagoon.materials()^), *(boat.materials()^), s.material('lambert', {id: "m-fish", color: "#ffffff", side: 'double'}),
        *(otter.materials()^)]),
    *(lagoon.elements()^),
    boat.body(st.boat.x, st.boat.z, st.boat.h)^,
    *[for (f in st.fish, let r = fish.ROSTER[f.i]) fish.body(r, f.x, f.y, f.z, f.h)^],
    otter.body(st.otter.x, st.otter.y, st.otter.z, st.otter.h)^,
    camera_body(st.camera)^,
    *[for (id, spec in RINGS) ring_emitter(string(id), spec)^], splash_emitter()^, foam_emitter()^,
    *(lagoon.clips()^), *(angler.clips()^), *[for (r in fish.ROSTER) fish.swim_clip(r)^], *(otter.clips()^)
], {id: "cove", camera: "cam", width: 1280, height: 800, background: "#6f9a4a", 'max-pixel-ratio': 1.5, tabindex: "0",
    role: "img", 'aria-label': "Willow Cove: a rowboat on a clear lagoon. Arrow keys or WASD row; Space casts and strikes.", *: attributes})
