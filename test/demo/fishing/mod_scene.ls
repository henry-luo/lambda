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
// ripples: final diameter, lifetime, how many crests (one, or a second trailing well behind), and how far
// each turns over its life (each ring's wobble then sits differently from its neighbours')
pub let RINGS = {'ring-s': {size: 1.7, life: 1.9, rings: 1, spin: 0.5}, 'ring-m': {size: 2.9, life: 2.4, rings: 2, spin: -0.4},
    'ring-l': {size: 4.2, life: 2.9, rings: 2, spin: 0.35}}
// the boat's own ripples: ovals stretched along the hull (aspect is their width over length). Their
// emitters ride the boat, so each ripple takes the hull's heading when it is born, then stays put.
pub let HULL_RINGS = {'hull-ring': {size: 4.4, life: 2.8, rings: 2, aspect: 0.5}, 'wake-ring': {size: 3.0, life: 2.3, rings: 1, aspect: 0.58}}
// A natural ripple as one disc of vertex colour and alpha, drawn like the wavy lines the moving surface
// throws rather than as ruled circles. The leading crest is a broad bright line with a soft glow, a faint
// trough shades it from behind, and a weaker crest may trail it well back. Each ring meanders on its own (its lobes
// drift round as the radius grows, so no two rings run parallel), and its strength rises and falls
// round the ring, so it thickens, thins and nearly breaks. The emitter's size curve spreads it (the
// bands widen as it travels, as real ripples' wavelength does) while its opacity curve fades it.
let CREST = [0.97, 0.99, 0.95]
let TROUGH = [0.02, 0.16, 0.18]
fn bump(x, at, width) => exp(0.0 - ((x - at) / width) * ((x - at) / width))
// a crest: a broad line with a soft glow around it
fn line(r, at, weight) => weight * (0.65 * bump(r, at, 0.04) + 0.35 * bump(r, at, 0.09))
fn ripple_band(r, a, rings) => (let lift = strength(a, r),
    let crest = lift * (line(r, 0.86, 0.62) + (if (rings > 1) line(r, 0.56, 0.3) else 0.0)),
    let trough = lift * (0.12 * bump(r, 0.75, 0.05) + (if (rings > 1) 0.06 * bump(r, 0.46, 0.05) else 0.0)),
    let al = min(0.85, crest + trough), let k = crest / max(crest + trough, 0.0001),
    [TROUGH[0] + (CREST[0] - TROUGH[0]) * k, TROUGH[1] + (CREST[1] - TROUGH[1]) * k, TROUGH[2] + (CREST[2] - TROUGH[2]) * k, al])
// how strongly a ring shows at this angle: it swells and fades round the ring, differently ring by ring
fn strength(a, r) => max(0.12, min(1.0, 0.62 + 0.28 * sin(2.0 * a + 5.0 * r + 0.7) + 0.2 * sin(5.0 * a - 7.0 * r + 1.1)))
// the foam crown a splash or a biting blade throws: a broad soft band, broken into patches
fn foam_band(r, a) => (let patch = max(0.0, min(1.0, 0.55 + 0.4 * sin(7.0 * a + 1.0) + 0.3 * sin(11.0 * a - 9.0 * r))),
    [0.97, 0.99, 0.97, 0.9 * patch * bump(r, 0.84, 0.08)])
// the radius at an angle: lobes of falling size whose phases drift with the radius, so each ring
// meanders its own way (the drift is slow enough that neighbouring rows never cross)
fn wobble(a, r) => 1.0 + 0.055 * sin(3.0 * a + 1.3 + 4.0 * r) + 0.032 * sin(5.0 * a + 0.4 - 6.0 * r) +
    0.018 * sin(9.0 * a + 2.0 + 9.0 * r) + 0.008 * sin(17.0 * a - 0.7 - 11.0 * r)
// the disc from 0.4 to 1.0 of its radius, fine enough round the ring and across it that a crest stays a
// smooth curve when a ripple spreads to several metres; band(r, a) gives each vertex its colour
fn ripple_geometry(band, aspect = 1.0) any^ {
    let seg = 128, radial = 50
    let rs = [for (j in 0 to radial) 0.4 + 0.6 * j / float(radial)]
    let positions = [for (j in 0 to radial, i in 0 to seg - 1, c in ripple_point(rs[j], i * u.TAU / float(seg), aspect)^) c]
    let colors = [for (j in 0 to radial, i in 0 to seg - 1, c in band(rs[j], i * u.TAU / float(seg))^) c]
    let indices = [for (j in 0 to radial - 1, i in 0 to seg - 1, k in ripple_quad(j * seg + i, j * seg + (i + 1) % seg, seg)) k];
    s.geometry(positions, {normals: [for (j in 0 to radial, i in 0 to seg - 1, c in [0.0, 1.0, 0.0]) c], colors: colors, indices: indices})
}
fn ripple_point(r, a, aspect) => (let q = 0.5 * r * wobble(a, r), [cos(a) * q * aspect, 0.0, sin(a) * q])
fn ripple_quad(a, b, seg) => [a, b, a + seg, b, b + seg, a + seg]
fn ring_emitter(id, spec) any^ => (let rings = int(spec.rings) ^ { 1 }, <'particle-emitter' id: id, backend: "cpu", space: "world", rate: 0.0, burst: 0, capacity: 96,
    lifetime: spec.life, gravity: [0.0, 0.0, 0.0], speed: 0.0, angle: 0.0, size: spec.size,
    ripple_geometry((r, a) => ripple_band(r, a, rings), spec.aspect or 1.0)^; s.material('basic', {color: "#ffffff", transparent: true, side: 'double'});
    <'particle-size-curve' times: [0.0, 0.3, 1.0], values: [0.1, 0.58, 1.0]>
    <'particle-rotation-curve' times: [0.0, 1.0], values: [0.0, 0.0, 0.0, 0.0, spec.spin or 0.0, 0.0]>
    <'particle-opacity-curve' times: [0.0, 0.07, 0.45, 1.0], values: [0.0, 1.0, 0.6, 0.0]>>)
// droplets thrown up by a splash, and a short-lived foam crown
fn splash_emitter() any^ => <'particle-emitter' id: "splash", backend: "cpu", space: "world", rate: 0.0, burst: 0, capacity: 320,
    shape: "cone", angle: 32.0, speed: 2.1, spread: 0.6, lifetime: 0.7, 'lifetime-min': 0.45, gravity: [0.0, -9.81, 0.0], size: 0.05,
    s.sphere(4, 6); s.material('basic', {color: "#f2fbff", transparent: true});
    <'particle-size-curve' times: [0.0, 1.0], values: [1.0, 0.55]>
    <'particle-opacity-curve' times: [0.0, 0.7, 1.0], values: [0.95, 0.85, 0.0]>>
fn foam_emitter() any^ => <'particle-emitter' id: "foam", backend: "cpu", space: "world", rate: 0.0, burst: 0, capacity: 48,
    lifetime: 0.55, gravity: [0.0, 0.0, 0.0], speed: 0.0, angle: 0.0, size: 0.8,
    ripple_geometry(foam_band)^; s.material('basic', {color: "#ffffff", transparent: true, side: 'double'});
    <'particle-size-curve' times: [0.0, 1.0], values: [0.35, 1.0]>
    <'particle-opacity-curve' times: [0.0, 0.15, 1.0], values: [0.0, 0.95, 0.0]>>
pub let EMITTERS = ["ring-s", "ring-m", "ring-l", "hull-ring", "wake-ring", "splash", "foam"]

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
    *(lagoon.elements()^), lagoon.water()^,
    boat.body(st.boat.x, st.boat.z, st.boat.h, [for (id, spec in HULL_RINGS) ring_emitter(string(id), spec)^])^,
    *[for (f in st.fish, let r = fish.ROSTER[f.i]) fish.body(r, f.x, f.y, f.z, f.h)^],
    otter.body(st.otter.x, st.otter.y, st.otter.z, st.otter.h)^,
    camera_body(st.camera)^,
    *[for (id, spec in RINGS) ring_emitter(string(id), spec)^], splash_emitter()^, foam_emitter()^,
    *(lagoon.clips()^), *(angler.clips()^), *[for (r in fish.ROSTER) fish.swim_clip(r)^], *(otter.clips()^)
], {id: "cove", camera: "cam", width: 1280, height: 800, background: "#6f9a4a", 'max-pixel-ratio': 1.5, tabindex: "0",
    role: "img", 'aria-label': "Willow Cove: a rowboat on a clear lagoon. Arrow keys or WASD row; Space casts and strikes.", *: attributes})
