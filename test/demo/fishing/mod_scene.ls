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
// ripples: final diameter, lifetime, and how many crests (one, or a second trailing well behind); each
// spawn gives a round one its own turn (mod_sim.spawn), so no two wavy outlines repeat
pub let RINGS = {'ring-s': {size: 1.7, life: 1.9, geometry: "single-ripple"},
    'ring-m': {size: 2.9, life: 2.4, geometry: "double-ripple"}, 'ring-l': {size: 4.2, life: 2.9, geometry: "double-ripple"}}
// the boat's own ripples: ovals stretched along the hull (aspect is their width over length). Their
// emitters ride the boat, so each ripple takes the hull's heading when it is born, then stays put.
pub let HULL_RINGS = {'hull-ring': {size: 4.4, life: 2.8, geometry: "hull-ripple"},
    'wake-ring': {size: 3.0, life: 2.3, geometry: "wake-ripple"}}
// A natural ripple as one disc of vertex colour and alpha, drawn like the wavy lines the moving surface
// throws rather than as ruled circles. The leading crest is a broad bright line with a soft glow, a faint
// trough shades it from behind, and a weaker crest may trail it well back. Each ring meanders on its own (its lobes
// drift round as the radius grows, so no two rings run parallel), and its strength rises and falls
// round the ring, so it thickens, thins and nearly breaks. The emitter's size curve spreads it (the
// bands widen as it travels, as real ripples' wavelength does) while its opacity curve fades it.
let CREST = [0.97, 0.99, 0.95]
let TROUGH = [0.02, 0.16, 0.18]
fn bump(x: float, at: float, width: float) float =>
    (let distance = (x - at) / width, exp(0.0 - distance * distance))
// a crest: a broad line with a soft glow around it
fn line(r: float, at: float, weight: float) float =>
    weight * (0.65 * bump(r, at, 0.04) + 0.35 * bump(r, at, 0.09))
fn ripple_band(lift: float, crest_profile: float, trough_profile: float) => (
    let crest = lift * crest_profile,
    let trough = lift * trough_profile,
    let al = min(0.85, crest + trough), let k = crest / max(crest + trough, 0.0001),
    [TROUGH[0] + (CREST[0] - TROUGH[0]) * k, TROUGH[1] + (CREST[1] - TROUGH[1]) * k, TROUGH[2] + (CREST[2] - TROUGH[2]) * k, al])
// how strongly a ring shows at this angle: it swells and fades round the ring, differently ring by ring
fn strength(a: float, r: float) float =>
    max(0.12, min(1.0, 0.62 + 0.28 * sin(2.0 * a + 5.0 * r + 0.7) + 0.2 * sin(5.0 * a - 7.0 * r + 1.1)))
// the foam crown a splash or a biting blade throws: a broad soft band, broken into patches
fn foam_band(r: float, a: float, profile: float) =>
    (let patch = max(0.0, min(1.0, 0.55 + 0.4 * sin(7.0 * a + 1.0) + 0.3 * sin(11.0 * a - 9.0 * r))),
        [0.97, 0.99, 0.97, 0.9 * patch * profile])
// the radius at an angle: lobes of falling size whose phases drift with the radius, so each ring
// meanders its own way (the drift is slow enough that neighbouring rows never cross)
fn wobble(a: float, r: float) float => 1.0 + 0.055 * sin(3.0 * a + 1.3 + 4.0 * r) + 0.032 * sin(5.0 * a + 0.4 - 6.0 * r) +
    0.018 * sin(9.0 * a + 2.0 + 9.0 * r) + 0.008 * sin(17.0 * a - 0.7 - 11.0 * r)
// the disc from 0.4 to 1.0 of its radius, fine enough round the ring and across it that a crest stays a
// smooth curve when a ripple spreads to several metres; band(r, a) gives each vertex its colour
let RIPPLE_SEGMENTS = 128
let RIPPLE_ROWS = 50
let RIPPLE_RADII = [for (j in 0 to RIPPLE_ROWS) 0.4 + 0.6 * j / float(RIPPLE_ROWS)]
let RIPPLE_ANGLES = [for (i in 0 to RIPPLE_SEGMENTS - 1) i * u.TAU / float(RIPPLE_SEGMENTS)]
// Radial profiles have one value per row. Both ripple colors share each vertex's angular lift.
fn ring_colors() any^ {
    let rows = [for (r in RIPPLE_RADII, let crest = line(r, 0.86, 0.62), let trough = 0.12 * bump(r, 0.75, 0.05))
        {r:r, crest:crest, trough:trough, double_crest:crest + line(r, 0.56, 0.3), double_trough:trough + 0.06 * bump(r, 0.46, 0.05)}];
    let bands = [for (row in rows, a in RIPPLE_ANGLES, let lift = strength(a, row.r))
        [ripple_band(lift, row.crest, row.trough), ripple_band(lift, row.double_crest, row.double_trough)]];
    {single:[for (band in bands, c in band[0]) c], double:[for (band in bands, c in band[1]) c]}
}
fn foam_colors() any^ {
    let rows = [for (r in RIPPLE_RADII) {radius:r, profile:bump(r, 0.84, 0.08)}];
    [for (row in rows, a in RIPPLE_ANGLES, c in foam_band(row.radius, a, row.profile)^) c]
}
fn ripple_point(r: float, direction) =>
    (let q = 0.5 * r * wobble(direction.angle, r), [direction.x * q, 0.0, direction.z * q])
fn ripple_quad(a, b, seg) => [a, b, a + seg, b, b + seg, a + seg]
fn ripple_stretch(positions, aspect) => [for (i, v in positions) if (i % 3 == 0) v * aspect else v]
// the emitters only the sim fires, through spawn commands: world space, no births of their own, no gravity
// unless given
fn fired(id, geometry, material, options, curves) element => s.particle_emitter(geometry, material,
    {id: id, backend: "cpu", space: "world", rate: 0.0, burst: 0, gravity: [0.0, 0.0, 0.0], *: options}, curves)
// all ripples use the same tessellation; only their profile and aspect vary.
fn ripple_resources() any^ {
    let seg = RIPPLE_SEGMENTS
    // Each column keeps its direction as the row changes its radius.
    let directions = [for (a in RIPPLE_ANGLES) {angle:a, x:cos(a), z:sin(a)}]
    let positions = [for (r in RIPPLE_RADII, direction in directions, c in ripple_point(r, direction)^) c]
    let normals = [for (r in RIPPLE_RADII, a in RIPPLE_ANGLES, c in [0.0, 1.0, 0.0]) c]
    let indices = [for (j in 0 to RIPPLE_ROWS - 1, i in 0 to seg - 1,
        k in ripple_quad(j * seg + i, j * seg + (i + 1) % seg, seg)) k]
    let rings = ring_colors()^
    let mesh = (id, vertices, colors) => s.geometry(vertices, {id: id, normals: normals, indices: indices, colors: colors});
    [
        s.material('basic', {id: "ripple-film", color: "#ffffff", transparent: true, side: 'double'}),
        mesh("single-ripple", positions, rings.single),
        mesh("double-ripple", positions, rings.double),
        mesh("hull-ripple", ripple_stretch(positions, 0.5), rings.double),
        mesh("wake-ripple", ripple_stretch(positions, 0.58), rings.single),
        mesh("foam-ripple", positions, foam_colors()^)
    ]
}
fn ring_emitter(id, spec) any^ =>
    fired(id, spec.geometry, "ripple-film",
        {capacity: 96, lifetime: spec.life, speed: 0.0, angle: 0.0, size: spec.size},
        [s.particle_curve('size', [0.0, 0.3, 1.0], [0.1, 0.58, 1.0])^, s.particle_curve('opacity', [0.0, 0.07, 0.45, 1.0], [0.0, 1.0, 0.6, 0.0])^])
// droplets thrown up by a splash, and a short-lived foam crown
fn splash_emitter() any^ => fired("splash", s.sphere(4, 6), s.material('basic', {color: "#f2fbff", transparent: true}),
    {capacity: 320, shape: "cone", angle: 32.0, speed: 2.1, spread: 0.6, lifetime: 0.7, 'lifetime-min': 0.45, gravity: [0.0, -9.81, 0.0], size: 0.05},
    [s.particle_curve('size', [0.0, 1.0], [1.0, 0.55])^, s.particle_curve('opacity', [0.0, 0.7, 1.0], [0.95, 0.85, 0.0])^])
fn foam_emitter() any^ => fired("foam", "foam-ripple", "ripple-film", {capacity: 48, lifetime: 0.55, speed: 0.0, angle: 0.0, size: 0.8},
    [s.particle_curve('size', [0.0, 1.0], [0.35, 1.0])^, s.particle_curve('opacity', [0.0, 0.15, 1.0], [0.0, 0.95, 0.0])^])
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
        *(otter.materials()^), *(ripple_resources()^)]),
    *(lagoon.elements()^), lagoon.water()^,
    boat.body(st.boat.x, st.boat.z, st.boat.h, [for (id, spec in HULL_RINGS) ring_emitter(string(id), spec)^])^,
    *[for (f in st.fish, let r = fish.ROSTER[f.i]) fish.body(r, f.x, f.y, f.z, f.h)^],
    otter.body(st.otter.x, st.otter.y, st.otter.z, st.otter.h)^,
    camera_body(st.camera)^,
    *[for (id, spec in RINGS) ring_emitter(string(id), spec)^], splash_emitter()^, foam_emitter()^,
    *(lagoon.clips()^), *(angler.clips()^), *[for (r in fish.ROSTER) fish.swim_clip(r)^], *(otter.clips()^)
], {id: "cove", camera: "cam", width: 1280, height: 800, background: "#6f9a4a", 'max-pixel-ratio': 1.5, tabindex: "0",
    role: "img", 'aria-label': "Willow Cove: a rowboat on a clear lagoon. Arrow keys or WASD row; Space casts and strikes.", *: attributes})
