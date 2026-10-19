// visual demo: open with ./lambda.exe demo cityscape
// "The Rings of a City": Chengdu inside the Third Ring Road at 1:10,000, from the
// Jinsha Sun Bird (c. 1250 BC) to 2022, as one native scene3d clip. Build assets/
// with _generate.py first (OpenStreetMap data); see README.md.
import s: lambda.scene3d
import dom
import score: ._score

let root = "test/demo/cityscape/assets/"
// the model is generated, not checked in: say how to build it instead of failing inside load.
let generated = if (not exists(root ++ "city.glb") or not exists(root ++ "city.json"))
    raise error("cityscape: assets/city.glb is missing; build it with " ++
        "'python3 -I test/demo/cityscape/_generate.py --fetch' (downloads OpenStreetMap data, see README.md)")
let story = input(root ++ "city.json", 'json')^
let city = s.load(root ++ "city.glb", {id: "city"})^
let duration = 150.0
let pi = math.pi
let half = story.half
let lines = story.outlines
let marks = story.landmarks
let stats = story.stats
let exaggeration = 1.6

// ---------------------------------------------------------------- loaded asset ids

fn walk(n) array => [n, *[for (c in content(n) where c is element and name(c) != 'geometry') *walk(c)]]
let loaded = walk(city)
// the generator names glTF nodes and materials; the loader prefixes their ids.
fn asset_id(tag, label) => [for (n in loaded where name(n) == tag and n.name == label) n.id][0]
let band_count = len(story.zones) * story.bands_per_zone

// ---------------------------------------------------------------- colours

fn hexdigit(ch) => [for (i in 0 to 15 where slice("0123456789abcdef", i, i + 1) == ch) i][0]
fn channel(hex, i) => (hexdigit(slice(hex, 1 + i * 2, 2 + i * 2)) * 16 + hexdigit(slice(hex, 2 + i * 2, 3 + i * 2))) / 255.0
// static colours are sRGB text; colour tracks carry linear components.
fn linear(v) => if (v <= 0.04045) v / 12.92 else ((v + 0.055) / 1.055) ** 2.4
fn rgb(hex) array => [for (i in 0 to 2) linear(channel(hex, i))]

// a look colours the slab, the five building zones (core and rings), water, roads and ring roads.
fn look(slab, core, others, water, road, ringroad) map => {slab: slab, core: core, ring1: others, ring2: others,
    ring3: others, outer: others, water: water, road: road, ringroad: ringroad}
let day = look("#dedbd5", "#ffffff", "#ffffff", "#7c8794", "#c9c6c0", "#bab6af")
let looks = {
    day: day,
    night: look("#16181b", "#3a3d42", "#3a3d42", "#a9b6c4", "#2a2d32", "#30343a"),
    qin: look("#d8d0c1", "#f7d49a", "#ece7df", "#7c8794", "#cbc2b2", "#c2b9a8"),
    water: look("#d0d7db", "#eaf0f2", "#eaf0f2", "#3d97bb", "#c3cacd", "#b8bfc3"),
    brocade: look("#e1cdc5", "#f9b8a5", "#ef9e8b", "#8c7b82", "#d8b9ad", "#cdaa9e"),
    luo: look("#d7d1c5", "#f6e2c0", "#e6e3dd", "#22b4dc", "#c9c3b7", "#bdb6aa"),
    mingqing: look("#d5c5a5", "#efdcb6", "#e3d6bc", "#7e898c", "#c6b48f", "#b9a886"),
    mono: look("#a9a9a9", "#d2d2d2", "#d2d2d2", "#8a8a8a", "#9c9c9c", "#959595"),
    growth: look("#202327", "#f4f4f4", "#f4f4f4", "#8d9aa7", "#353940", "#454a52"),
    rings: {slab: "#e4e0d9", core: "#f29b4a", ring1: "#f5c27f", ring2: "#8ec7e6", ring3: "#a798df", outer: "#7c71c2",
        water: "#6d8ea6", road: "#d3cfc8", ringroad: "#c4c0b9"}
}
// tree-ring washes colour the slab itself by ring zone in the finale.
let washes = {core: "#f6c393", ring1: "#f7dcb5", ring2: "#c7e2f1", ring3: "#d7d0f2", outer: "#c9c3ea"}

// chapter cuts; every look crossfades over `fade` seconds.
let fade = 1.5
let cuts = [{t: 0.0, look: "day"}, {t: 15.5, look: "night"}, {t: 28.0, look: "day"}, {t: 38.0, look: "qin"},
    {t: 50.0, look: "water"}, {t: 62.0, look: "brocade"}, {t: 74.0, look: "luo"}, {t: 84.0, look: "mingqing"},
    {t: 96.0, look: "mono"}, {t: 105.0, look: "growth"}, {t: 124.0, look: "day"}, {t: 134.0, look: "rings"},
    {t: 143.0, look: "night"}, {t: 146.5, look: "day"}]

fn no_extra(name, start, finish) array => []
fn role_keys(colour, extra) array =>
    [for (i in 0 to (len(cuts) - 1), let cut = cuts[i], let c = colour(cut.look),
          let start = if (i == 0) 0.0 else cut.t + fade,
          let finish = if (i == len(cuts) - 1) duration else cuts[i + 1].t)
        *[[start, c], *extra(cut.look, start, finish), [finish, c]]]
fn colour_track(target, keys) element =>
    s.track(target ++ ".color", [for (k in keys) k[0]], [for (k in keys) *rgb(k[1])], 'color')
fn palette_track(target, role) element => colour_track(target, role_keys((name) => looks[name][role], no_extra))

// ---------------------------------------------------------------- procedural geometry

fn quads(n, closed) array =>
    [for (i in 0 to (if (closed) n - 1 else n - 2), let j = (i + 1) % n)
        *[i * 2, j * 2, i * 2 + 1, i * 2 + 1, j * 2, j * 2 + 1]]
fn up(n) array => [for (i in 1 to n) *[0.0, 1.0, 0.0]]

// a flat strip between two outlines sampled at the same angles.
fn band_fill(inner, outer, y) element =>
    s.geometry([for (i in 0 to (len(inner) - 1)) *[inner[i][0], y, inner[i][1], outer[i][0], y, outer[i][1]]],
        {normals: up(len(inner) * 2), indices: quads(len(inner), true)})
// each vertex shifts along its averaged tangent's normal, so off-centre outlines keep an even width.
fn offsets(points, width) array =>
    [for (i in 0 to (len(points) - 1), let n = len(points), let a = points[(i + n - 1) % n], let b = points[(i + 1) % n],
          let tx = b[0] - a[0], let tz = b[1] - a[1], let l = sqrt(tx * tx + tz * tz) + 0.000001, let p = points[i])
        [[p[0] - tz / l * width / 2.0, p[1] + tx / l * width / 2.0], [p[0] + tz / l * width / 2.0, p[1] - tx / l * width / 2.0]]]
fn ribbon(points, width, y) element {
    let edges = offsets(points, width)
    s.geometry([for (q in edges) *[q[0][0], y, q[0][1], q[1][0], y, q[1][1]]],
        {normals: up(len(edges) * 2), indices: quads(len(edges), true)})
}
fn wall(points, height) element =>
    s.geometry([for (p in points) *[p[0], 0.0, p[1], p[0], height, p[1]]],
        {normals: [for (p in points, let r = sqrt(p[0] ** 2 + p[1] ** 2) + 0.000001) *[p[0] / r, 0.0, p[1] / r, p[0] / r, 0.0, p[1] / r]],
         indices: quads(len(points), true)})
// a wall is a vertical strip plus a flat cap, so it still reads when seen edge-on from above.
fn flat_colour(colour) element => s.material('basic', {color: colour, side: 'double'})
fn rampart(points, height, colour) array =>
    [s.mesh(wall(points, height), flat_colour(colour)), s.mesh(ribbon(points, 0.05, height), flat_colour(colour))]
fn lerp_outline(a, b, u) array => [for (i in 0 to (len(a) - 1)) [a[i][0] + (b[i][0] - a[i][0]) * u, a[i][1] + (b[i][1] - a[i][1]) * u]]

let samples = len(lines.core)
let centre = [for (i in 1 to samples) [0.0, 0.0]]
let edge = [for (j in 0 to (samples - 1), let a = 2.0 * pi * j / samples)
    [half / max(abs(cos(a)), abs(sin(a))) * cos(a), half / max(abs(cos(a)), abs(sin(a))) * sin(a)]]
let zone_lines = [centre, lines.core, lines.ring1, lines.ring2, lines.ring3, edge]

fn polar(r, a) array => [r * cos(a), r * sin(a)]
fn strip(left, right) element =>
    s.geometry([for (i in 0 to (len(left) - 1)) *[left[i][0], 0.0, left[i][1], right[i][0], 0.0, right[i][1]]],
        {normals: up(len(left) * 2), indices: quads(len(left), false)})
fn annulus(r0, r1, segments) element =>
    strip([for (i in 0 to segments) polar(r0, 2.0 * pi * i / segments)], [for (i in 0 to segments) polar(r1, 2.0 * pi * i / segments)])

// the Sun Bird gold foil: a rim, a hollow sun with twelve swirling rays and four birds.
fn ray(i) element {
    let steps = 8
    strip([for (t in 0 to steps, let u = t / steps) polar(0.25 + 0.38 * u, pi * i / 6.0 + 0.8 * u + 0.13 * (1.0 - u))],
          [for (t in 0 to steps, let u = t / steps) polar(0.25 + 0.38 * u, pi * i / 6.0 + 0.8 * u - 0.13 * (1.0 - u))])
}
// a bird's body thickens mid-way and narrows to a neck; the head swells again near the front.
fn bird_width(u) => 0.065 * (sin(pi * u) ** 0.6) + (if (u > 0.82) 0.03 * sin(pi * (u - 0.82) / 0.18) else 0.0)
fn bird(i) element {
    let steps = 16
    strip([for (t in 0 to steps, let u = t / steps) polar(0.78 - bird_width(u), pi * i / 2.0 + 1.25 * u)],
          [for (t in 0 to steps, let u = t / steps) polar(0.78 + bird_width(u), pi * i / 2.0 + 1.25 * u)])
}
fn gold() element => s.material('basic', {color: "#f6c453", side: 'double'})
fn glow(colour, opacity) element => s.material('basic', {color: colour, opacity: opacity, transparent: true, side: 'double'})

// ---------------------------------------------------------------- camera

// shots: target point, yaw (camera bearing, 0 = from the south), pitch and distance.
let shots = [
    {t: 0.0, x: 0.0, z: 0.6, yaw: 18.0, pitch: 40.0, d: 31.0}, {t: 7.5, x: 0.0, z: 0.4, yaw: 34.0, pitch: 36.0, d: 28.0},
    {t: 10.0, x: -1.6, z: 2.2, yaw: 52.0, pitch: 15.0, d: 6.0}, {t: 15.0, x: 0.4, z: -0.4, yaw: 84.0, pitch: 18.0, d: 5.0},
    {t: 18.0, x: marks.jinsha[0], z: marks.jinsha[1], yaw: 60.0, pitch: 62.0, d: 9.0},
    {t: 27.0, x: marks.jinsha[0], z: marks.jinsha[1], yaw: 76.0, pitch: 55.0, d: 7.0},
    {t: 31.0, x: 0.0, z: 0.0, yaw: 32.0, pitch: 50.0, d: 26.0}, {t: 37.5, x: 0.0, z: 0.0, yaw: 16.0, pitch: 46.0, d: 24.0},
    {t: 40.5, x: 0.0, z: -0.3, yaw: -10.0, pitch: 42.0, d: 8.0}, {t: 49.5, x: 0.0, z: -0.3, yaw: 8.0, pitch: 37.0, d: 7.0},
    {t: 52.0, x: -1.0, z: -1.0, yaw: 45.0, pitch: 36.0, d: 16.0}, {t: 61.5, x: -1.0, z: -1.0, yaw: 32.0, pitch: 40.0, d: 14.0},
    {t: 64.0, x: marks.brocade[0], z: marks.brocade[1], yaw: 100.0, pitch: 30.0, d: 4.5},
    {t: 73.5, x: marks.brocade[0], z: marks.brocade[1], yaw: 128.0, pitch: 27.0, d: 4.0},
    {t: 76.0, x: 0.3, z: 0.0, yaw: 160.0, pitch: 58.0, d: 9.0}, {t: 83.5, x: 0.3, z: 0.0, yaw: 188.0, pitch: 60.0, d: 8.5},
    {t: 86.0, x: -0.6, z: -0.5, yaw: 200.0, pitch: 48.0, d: 6.5}, {t: 95.5, x: -0.6, z: -0.5, yaw: 224.0, pitch: 45.0, d: 6.0},
    {t: 98.0, x: 0.0, z: -0.3, yaw: 238.0, pitch: 80.0, d: 9.0}, {t: 105.5, x: 0.0, z: 0.0, yaw: 248.0, pitch: 84.0, d: 13.0},
    {t: 110.0, x: 0.0, z: 0.0, yaw: 255.0, pitch: 82.0, d: 20.0}, {t: 117.0, x: 0.0, z: 0.0, yaw: 268.0, pitch: 70.0, d: 26.0},
    {t: 123.5, x: 0.0, z: 0.0, yaw: 282.0, pitch: 56.0, d: 25.0},
    {t: 126.0, x: marks.shudu.position[0], z: marks.shudu.position[1], yaw: 300.0, pitch: 13.0, d: 2.4},
    {t: 133.5, x: marks.shudu.position[0], z: marks.shudu.position[1], yaw: 332.0, pitch: 19.0, d: 2.8},
    {t: 136.5, x: 0.0, z: 0.0, yaw: 342.0, pitch: 70.0, d: 24.0}, {t: 142.5, x: 0.0, z: 0.0, yaw: 358.0, pitch: 80.0, d: 22.0},
    {t: 146.0, x: 0.0, z: 0.4, yaw: 378.0, pitch: 40.0, d: 30.0}, {t: 150.0, x: 0.0, z: 0.5, yaw: 390.0, pitch: 38.0, d: 31.0}]

fn smootherstep(u) => u * u * u * (u * (u * 6.0 - 15.0) + 10.0)
fn camera_at(t) map {
    let k = max(0, min(len(shots) - 2, len([for (shot in shots where shot.t <= t) shot]) - 1))
    let a = shots[k], b = shots[k + 1]
    let u = smootherstep(max(0.0, min(1.0, (t - a.t) / (b.t - a.t))))
    {x: a.x + (b.x - a.x) * u, z: a.z + (b.z - a.z) * u, yaw: a.yaw + (b.yaw - a.yaw) * u,
     pitch: a.pitch + (b.pitch - a.pitch) * u, d: a.d + (b.d - a.d) * u}
}
let step = 0.25
let frames = [for (i in 0 to int(duration / step), let t = i * step) {t: t, view: camera_at(t)}]
let times = [for (f in frames) f.t]
// the rig turns about its target; the camera sits on the rig's +z axis looking back at it.
let camera_tracks = [
    s.track("rig.position", times, [for (f in frames) *[f.view.x, 0.0, f.view.z]], 'vector'),
    s.track("rig.quaternion", times, [for (f in frames, let h = f.view.yaw * pi / 360.0) *[0.0, sin(h), 0.0, cos(h)]], 'quaternion'),
    s.track("cam.position", times, [for (f in frames, let p = f.view.pitch * pi / 180.0)
        *[0.0, f.view.d * sin(p), f.view.d * cos(p)]], 'vector')]

// ---------------------------------------------------------------- growth and highlights

let flat = 0.001
fn scale_track(id, keys) element => s.track(id ++ ".scale", [for (k in keys) k[0]], [for (k in keys) *[1.0, k[1], 1.0]], 'vector')
fn uniform_track(id, keys) element => s.track(id ++ ".scale", [for (k in keys) k[0]], [for (k in keys) *[k[1], k[1], k[1]]], 'vector')
fn visible_track(id, on, off) element =>
    s.track(id ++ ".visible", [0.0, on, off], [false, true, false], 'bool')

// buildings sink for Jinsha, return for today, then regrow band by band, centre first.
let band_tracks = [for (k in 0 to (band_count - 1)) scale_track(asset_id('group', "band-" ++ string(k)), [
    [0.0, 1.0], [15.5 + k * 0.05, 1.0], [17.5 + k * 0.05, flat], [28.0 + k * 0.35, flat], [29.2 + k * 0.35, 1.0],
    [103.0 + k * 0.04, 1.0], [104.5 + k * 0.04, flat], [107.0 + k * 1.0, flat], [108.2 + k * 1.0, 1.0], [duration, 1.0]])]

// walls rise out of the ground and sink when they were demolished; hiding them also removes the caps.
fn rise(id, up_at, down_at) array => [scale_track(id, [[0.0, flat], [up_at, flat], [up_at + 1.5, 1.0],
    [down_at, 1.0], [down_at + 1.5, flat], [duration, flat]]), visible_track(id, up_at, down_at + 1.5)]
let wall_tracks = [*rise("wall-qin", 39.0, 49.5), *rise("wall-great", 75.0, 97.5), *rise("wall-lesser", 86.5, 97.0),
    *rise("wall-imperial", 86.0, 101.5)]

// ring-road glows light up as the city reaches them; tree-ring lines appear outward in the finale.
let glow_tracks = [visible_track("glow-ring1", 112.5, 124.0), visible_track("glow-ring2", 116.0, 124.0),
    visible_track("glow-ring3", 119.5, 124.0)]
let ring_fractions = [for (i in 1 to 14) i / 14.0]
let tree_rings = [*[for (u in ring_fractions) lerp_outline(lines.core, lines.ring3, u)],
    *[for (u in [0.35, 0.7]) lerp_outline(lines.ring3, edge, u)]]
let tree_tracks = [for (i in 0 to (len(tree_rings) - 1)) visible_track("tree-" ++ string(i), 135.0 + i * 0.22, 143.0)]

// water pulses south-east from the north-west corner, bucket by bucket, for Dujiangyan.
fn pulse(k) => (name, start, finish) =>
    if (name == "water") [[52.0 + k * 0.9, looks.water.water], [52.6 + k * 0.9, "#7af0ff"], [54.0 + k * 0.9, looks.water.water]] else []
let water_tracks = [for (k in 0 to (story.flow_buckets - 1))
    colour_track(asset_id('material', "water-" ++ string(k)), role_keys((name) => looks[name].water, pulse(k)))]
let zone_ids = [for (z in story.zones) asset_id('material', "building-" ++ z)]
let material_tracks = [
    *[for (i in 0 to (len(story.zones) - 1)) palette_track(zone_ids[i], story.zones[i])],
    *[for (z in story.zones) colour_track("wash-" ++ z,
        role_keys((name) => if (name == "rings") washes[z] else looks[name].slab, no_extra))],
    *water_tracks, palette_track(asset_id('material', "road"), "road"),
    *[for (r in ["ring1", "ring2", "ring3"]) palette_track(asset_id('material', r), "ringroad")]]

// sun bird: rises over Jinsha, spins throughout, returns over the centre at the end.
let sun_tracks = [
    uniform_track("sun", [[0.0, flat], [16.5, flat], [19.5, 0.85], [26.5, 0.85], [28.5, flat],
        [143.0, flat], [145.0, 1.25], [duration, 1.25]]),
    s.track("sun.position", [0.0, 140.0, 140.5, duration],
        [marks.jinsha[0], 0.3, marks.jinsha[1], marks.jinsha[0], 0.3, marks.jinsha[1], 0.0, 0.5, 0.0, 0.0, 0.5, 0.0], 'vector'),
    s.track("sun.quaternion", [for (i in 0 to 150) i * 1.0], [for (i in 0 to 150, let h = -0.2 * i) *[0.0, sin(h), 0.0, cos(h)]], 'quaternion'),
    uniform_track("brocade", [[0.0, flat], [62.5, flat], [64.0, 0.55], [73.0, 0.55], [74.0, flat], [duration, flat]]),
    scale_track("beam", [[0.0, flat], [125.0, flat], [126.5, 0.42], [133.0, 0.42], [134.0, flat], [duration, flat]]),
    uniform_track("arrow", [[0.0, flat], [50.5, flat], [51.5, 1.0], [61.0, 1.0], [62.0, flat], [duration, flat]])]

let tracks = [*camera_tracks, *band_tracks, *wall_tracks, *glow_tracks, *tree_tracks, *material_tracks, *sun_tracks]

// ---------------------------------------------------------------- scene

let shudu = marks.shudu.position
let jinsha = marks.jinsha
let scene = s.scene([
    s.group([s.camera({id: "cam", position: [0.0, 20.0, 24.0], target: [0.0, 0.0, 0.0], fov: 34.0, near: 0.1, far: 200.0})],
        {id: "rig"}),
    s.light('ambient', {color: "#ffffff", intensity: 0.36}),
    s.light('directional', {position: [-6.0, 10.0, 4.0], color: "#fff4e6", intensity: 0.66}),
    s.light('directional', {position: [6.0, 5.0, -4.0], color: "#dfe7ff", intensity: 0.2}),
    s.mesh(s.box([half * 2.0, 0.25, half * 2.0]), s.material('lambert', {color: "#2a2c30"}), {position: [0.0, -0.127, 0.0]}),
    *[for (i in 0 to 4) s.mesh(band_fill(zone_lines[i], zone_lines[i + 1], 0.0),
        s.material('lambert', {id: "wash-" ++ story.zones[i], color: day.slab, side: 'double'}))],
    s.group([city], {scale: [1.0, exaggeration, 1.0]}),
    s.group(rampart(lines.qin, 0.045, "#f3c47c"), {id: "wall-qin", visible: false}),
    s.group(rampart(lines.core, 0.04, "#fff3d6"), {id: "wall-great", visible: false}),
    s.group(rampart(lines.lesser, 0.035, "#f0b46a"), {id: "wall-lesser", visible: false}),
    s.group(rampart(lines.imperial, 0.05, "#ffd36b"), {id: "wall-imperial", visible: false}),
    *[for (r in [{id: "ring1", colour: "#ffcf6e"}, {id: "ring2", colour: "#7fd0ff"}, {id: "ring3", colour: "#c9b4ff"}])
        s.mesh(ribbon(lines[r.id], 0.09, 0.01 * exaggeration), s.material('basic', {color: r.colour, side: 'double'}),
            {id: "glow-" ++ r.id, visible: false})],
    *[for (i in 0 to (len(tree_rings) - 1)) s.mesh(ribbon(tree_rings[i], 0.022, 0.012 * exaggeration),
        s.material('basic', {color: "#ffffff", side: 'double'}), {id: "tree-" ++ string(i), visible: false})],
    s.group([
        s.mesh(annulus(0.94, 1.0, 72), gold()),
        s.mesh(annulus(0.17, 0.25, 36), gold()),
        *[for (i in 0 to 11) s.mesh(ray(i), gold())],
        *[for (i in 0 to 3) s.mesh(bird(i), gold())],
        *[for (h in [{r: 1.35, o: 0.3, y: -0.01}, {r: 1.9, o: 0.16, y: -0.02}, {r: 2.7, o: 0.08, y: -0.03}])
            s.mesh(annulus(0.001, h.r, 64), glow("#f7b84a", h.o), {position: [0.0, h.y, 0.0]})]
    ], {id: "sun", position: [jinsha[0], 0.3, jinsha[1]], scale: [flat, flat, flat]}),
    s.group([*[for (h in [{r: 0.35, o: 0.9}, {r: 0.8, o: 0.35}, {r: 1.4, o: 0.15}])
            s.mesh(annulus(0.001, h.r, 48), glow("#ff8f72", h.o), {position: [0.0, 0.02 + h.r * 0.01, 0.0]})]],
        {id: "brocade", position: [marks.brocade[0], 0.03, marks.brocade[1]], scale: [flat, flat, flat]}),
    s.group([s.mesh(s.box([0.03, 1.0, 0.03]), glow("#ffe9a8", 0.85), {position: [0.0, 0.5, 0.0]})],
        {id: "beam", position: [shudu[0], 0.0, shudu[1]], scale: [1.0, flat, 1.0]}),
    s.group([s.mesh(strip([[0.0, -0.09], [1.1, -0.09], [1.1, -0.24], [1.6, 0.0]], [[0.0, 0.09], [1.1, 0.09], [1.1, 0.24], [1.6, 0.0]]),
            s.material('basic', {color: "#5fe6ff", side: 'double'}), {position: [0.0, 0.02, 0.0]})],
        {id: "arrow", position: [0.0 - half + 0.9, 0.05, 0.0 - half + 0.9], rotation: [0.0, 0.0 - pi / 4.0, 0.0], scale: [flat, flat, flat]}),
    s.clip("film", tracks, {duration: duration, autoplay: true})
], {id: "film-view", camera: "cam", width: 1280, height: 720, viewBox: "0 0 1280 720", background: "#050607",
    role: "img", 'aria-label': "Chengdu inside the Third Ring Road, animated from 1250 BC to 2022",
    style: "display:block;width:1280px;height:720px"})

// ---------------------------------------------------------------- captions

fn thousands(n) string {
    let text = string(n)
    let k = len(text)
    if (k <= 3) text else thousands(int(n / 1000)) ++ "," ++ slice(text, k - 3, k)
}
let count = thousands(stats.buildings)
let ring3_km = string(int(round(stats.perimeter_km.ring3)))
let ring3_area = string(int(round(stats.area_km2.ring3)))

// `tick` places each chapter on the chronological timeline; "today" and 1991 are flash-forwards.
let ticks = ["前1250", "前311", "前256", "汉", "876", "明清", "1958", "1991", "2002", "2022"]
let chapters = [
    {t: 16.0, end: 27.8, tick: 0, year: "前1250", zh: "金沙 · 太阳神鸟", en: "Jinsha · the Sun Bird"},
    {t: 28.5, end: 37.8, tick: 9, year: "2022", zh: "今天的成都", en: "Chengdu today"},
    {t: 38.5, end: 49.8, tick: 1, year: "前311", zh: "秦 · 张仪筑城", en: "Qin · Zhang Yi raises the walls"},
    {t: 50.5, end: 61.8, tick: 2, year: "前256", zh: "都江堰", en: "Dujiangyan"},
    {t: 62.5, end: 73.8, tick: 3, year: "汉", zh: "锦官城", en: "The Brocade City"},
    {t: 74.5, end: 83.8, tick: 4, year: "876", zh: "唐 · 罗城", en: "Tang · the Luo City"},
    {t: 84.5, end: 95.8, tick: 5, year: "明清", zh: "大城 · 皇城 · 少城", en: "Great, Imperial and Lesser Cities"},
    {t: 96.5, end: 105.8, tick: 6, year: "1958", zh: "城墙消失", en: "The walls come down"},
    {t: 106.5, end: 123.8, tick: 8, year: "2002", zh: "一环 · 二环 · 三环", en: "The ring roads"},
    {t: 124.5, end: 133.8, tick: 7, year: "1991", zh: "蜀都大厦", en: "Shudu Mansion"},
    {t: 134.5, end: 142.8, tick: 9, year: "年轮", zh: "从金沙到三环", en: "From Jinsha to the Third Ring"}]

let subtitles = [
    {t: 8.0, end: 15.3, zh: "成都，三环以内，按一万分之一缩小。", en: "Chengdu inside the Third Ring Road, at 1:10,000."},
    {t: 17.0, end: 22.0, zh: "约公元前1250年，金沙的古蜀工匠打造了太阳神鸟金饰。", en: "Around 1250 BC, Shu artisans at Jinsha made the Sun Bird gold foil."},
    {t: 22.0, end: 27.5, zh: "2001年，它在一处工地重见天日。", en: "In 2001, builders on a construction site brought it back to light."},
    {t: 29.0, end: 37.5, zh: "今天，这片土地上有 " ++ count ++ " 栋建筑。", en: "Today, " ++ count ++ " buildings stand on this ground."},
    {t: 39.0, end: 44.0, zh: "公元前311年，秦国张仪在这里筑城。", en: "In 311 BC, Zhang Yi of Qin raised the first city walls here."},
    {t: 44.0, end: 49.5, zh: "城墙屡筑屡塌，相传依龟行之迹而成，人称“龟城”。", en: "Legend says the walls stood only once they followed a turtle's trail: the Turtle City."},
    {t: 51.0, end: 56.0, zh: "公元前256年，李冰修都江堰，岷江水从西北流入平原。", en: "In 256 BC, Li Bing built Dujiangyan; Min River water flowed in from the northwest."},
    {t: 56.0, end: 61.5, zh: "从此“水旱从人，不知饥馑”。", en: "Flood and drought now answered to people, and famine was forgotten."},
    {t: 63.0, end: 68.0, zh: "汉代蜀锦兴盛，朝廷在城西南设锦官。", en: "Han brocade thrived; the court set up a brocade office southwest of the city."},
    {t: 68.0, end: 73.5, zh: "成都从此又名“锦官城”。", en: "Chengdu became the Brocade City."},
    {t: 75.0, end: 80.0, zh: "公元876年，高骈筑罗城，引两江环抱城池。", en: "In 876, Gao Pian built the Luo City and turned two rivers around it."},
    {t: 80.0, end: 83.5, zh: "府河与南河，从此绕城而行。", en: "The Fu and Nan rivers have wrapped the old city ever since."},
    {t: 85.0, end: 90.0, zh: "明清时，成都城中套城：大城、皇城、少城。", en: "Under the Ming and Qing, Chengdu nested three cities: Great, Imperial and Lesser."},
    {t: 90.0, end: 95.5, zh: "少城的街巷，今天还留在宽窄巷子。", en: "The Lesser City's lanes survive today as Kuanzhai Alley."},
    {t: 97.0, end: 101.0, zh: "1958年前后，城墙陆续拆除。", en: "Around 1958, the city walls were pulled down."},
    {t: 101.0, end: 105.5, zh: "1968年，皇城也被拆除。", en: "In 1968, the Imperial City followed."},
    {t: 107.0, end: 113.0, zh: "城墙没了，城市开始向外一层层铺展。", en: "Without walls, the city spread outward, layer by layer."},
    {t: 113.0, end: 118.0, zh: "一环、二环、三环，环环相套。", en: "First Ring, Second Ring, Third Ring, one inside the next."},
    {t: 118.0, end: 123.5, zh: "2002年三环路通车，全长约" ++ ring3_km ++ "公里，圈住约" ++ ring3_area ++ "平方公里。",
        en: "The Third Ring opened in 2002: about " ++ ring3_km ++ " km long, enclosing about " ++ ring3_area ++ " km²."},
    {t: 125.0, end: 129.5, zh: "1991年前后，34层的蜀都大厦落成。", en: "Around 1991, the 34-storey Shudu Mansion was completed."},
    {t: 129.5, end: 133.5, zh: "此后，高楼一栋接一栋长起来。", en: "After it, towers rose one after another."},
    {t: 135.0, end: 139.0, zh: "从金沙到三环，三千多年。", en: "From Jinsha to the Third Ring: more than three thousand years."},
    {t: 139.0, end: 142.8, zh: "城市像一棵树，一圈一圈地长。", en: "Like a tree, the city grows one ring at a time."},
    {t: 143.5, end: 146.5, zh: "新的年轮，还在长出来。", en: "New rings are still forming."}]

// each caption fades in and out on the document timeline the scene clip also starts on.
// the stylesheet owns the animation name, so a restart can swap it (see .replay below).
fn timed(t, finish) string => "animation-duration:" ++ string(finish - t) ++ "s;animation-delay:" ++ string(t) ++ "s"
fn caption(c, cls) element => <div class: "cap " ++ cls, style: timed(c.t, c.end),
    *[<div class: "zh", c.zh>, <div class: "en", c.en>]>
fn era(c) element => <div class: "cap era", style: timed(c.t, c.end),
    *[<div class: "year", c.year>, <div class: "zh", c.zh>, <div class: "en", c.en>]>
fn title_card(t, finish) element => <div class: "cap title", style: timed(t, finish), *[
    <div class: "kicker", "成都 · 从金沙到三环">,
    <h1 "一座城的年轮">, <div class: "sub-title", "The Rings of a City">,
    <div class: "facts", "1:10,000 · " ++ count ++ " 栋建筑 buildings · " ++ string(stats.slab_km2) ++ " km²">,
    <div class: "credit", "数据 Data © OpenStreetMap contributors (ODbL) · 高度多为估算 most heights estimated">,
    <div class: "credit", "致敬 Eian《一座城的年轮》 · after Eian's film">]>

// the era marker holds on its tick and glides to the next one as each chapter opens.
fn tick_left(i) string => string(i * 100.0 / (len(ticks) - 1)) ++ "%"
let era_frames = "{" ++ join([for (i in 0 to (len(chapters) - 1), let c = chapters[i],
        let previous = if (i == 0) c.tick else chapters[i - 1].tick)
    string((c.t - 0.8) / duration * 100.0) ++ "%{left:" ++ tick_left(previous) ++ "}" ++
    string(c.t / duration * 100.0) ++ "%{left:" ++ tick_left(c.tick) ++ "}"], "") ++ "}"
let fade_frames = "{0%{opacity:0}10%{opacity:1}90%{opacity:1}100%{opacity:0}}"
// twin keyframes: toggling .replay changes every animation-name, which restarts the animations.
let keyframes = "@keyframes era" ++ era_frames ++ "@keyframes era2" ++ era_frames ++
    "@keyframes fade" ++ fade_frames ++ "@keyframes fade2" ++ fade_frames
let css = "html,body{margin:0;background:#050607;color:#eef0f2;" ++
    "font-family:'Hiragino Sans GB','Heiti SC',Arial,sans-serif}" ++
    ".stage{position:relative;width:1280px;height:720px;margin:0 auto;overflow:hidden}" ++
    ".cap{position:absolute;opacity:0}" ++
    ".cap,.hud,.timeline{animation-name:fade;animation-timing-function:linear;animation-fill-mode:both}" ++
    ".replay .cap,.replay .hud,.replay .timeline{animation-name:fade2}" ++
    ".sound{position:absolute;right:24px;top:16px;padding:6px 12px;border:1px solid rgba(246,196,83,0.7);" ++
    "border-radius:15px;background:rgba(5,6,7,0.55);color:#f6c453;font-size:12px;letter-spacing:1px;cursor:pointer}" ++
    ".sound[data-state=on]{background:#f6c453;color:#16181b}.sound[data-state=error]{border-color:#b45b4c;color:#e8a092}" ++
    ".narrate{top:52px}" ++
    ".shade{position:absolute;left:0;right:0;bottom:0;height:150px;background:linear-gradient(rgba(0,0,0,0),rgba(0,0,0,0.6))}" ++
    ".sub{left:0;right:0;bottom:40px;text-align:center}.sub .zh{font-size:22px;letter-spacing:1px}" ++
    ".sub .en{font-size:13px;color:#c8ccd1;margin-top:6px}" ++
    ".era{left:48px;top:236px;padding:18px 22px;background:rgba(5,6,7,0.45);border-left:2px solid #f6c453}" ++
    ".era .year{font-size:56px;font-weight:200;letter-spacing:2px}.era .zh{font-size:20px;margin-top:6px}" ++
    ".era .en{font-size:12px;color:#b9bec5;letter-spacing:1px;margin-top:4px}" ++
    ".hud{position:absolute;left:28px;top:22px;font-size:11px;letter-spacing:2px;color:#d6d9dd;opacity:0}" ++
    ".hud .en{font-size:10px;color:#9aa1a9;margin-top:4px;letter-spacing:1px}" ++
    ".timeline{position:absolute;left:420px;right:300px;top:30px;height:1px;background:rgba(220,224,228,0.5);opacity:0}" ++
    ".tick{position:absolute;top:-16px;font-size:10px;color:#c3c8ce;margin-left:-22px;width:44px;text-align:center;white-space:nowrap}" ++
    ".marker{position:absolute;top:-4px;width:9px;height:9px;margin-left:-4px;border-radius:5px;background:#f6c453;" ++
    "animation-name:era;animation-duration:" ++ string(duration) ++ "s;animation-timing-function:linear;" ++
    "animation-fill-mode:both}.replay .marker{animation-name:era2}" ++
    ".title{left:72px;top:206px;padding:26px 30px;background:rgba(5,6,7,0.55)}" ++
    ".title .kicker{font-size:13px;letter-spacing:4px;color:#f6c453}" ++
    ".title h1{font-size:52px;font-weight:600;letter-spacing:10px;margin:10px 0 4px}" ++
    ".title .sub-title{font-size:18px;letter-spacing:4px;color:#d9dde2}" ++
    ".title .facts{font-size:12px;color:#c8ccd1;margin-top:18px;letter-spacing:1px}" ++
    ".title .credit{font-size:10px;color:#9aa1a9;margin-top:6px}" ++
    keyframes;

// ---------------------------------------------------------------- soundtrack

// an original score composed in _score.ls, played with the music package's bundled SoundFont.
// the audio device opens only on the viewer's click (S12.1.1v2/D7.4.6).
let soundtrack = score.performance()
let soundfont = sys.lambda.home# ++ "/package/music/soundfonts/TimGM6mb.sf2"
let sound_button = <button id: "sound", class: "sound", type: "button",
    title: "Play the film from the start with its soundtrack", "♪ 声音 · Sound">
let narration_button = <button id: "narrate", class: "sound narrate", type: "button",
    title: "Speak the subtitles in Chinese, cued to the soundtrack", "旁白 · Narration">

// opens the score at 0 and plays it; null when the SoundFont or the audio device is unavailable.
pn start_soundtrack(owner) {
    var problem = ""
    let bytes = input(soundfont, 'binary') ^ { problem = ^.message }
    if (problem != "") { return null }
    let opened = dom.midi_open(owner, soundtrack, bytes)
    if (opened == null) { return null }
    for (step in [["volume", 0.85], ["seek", 0.0], ["play", 0.0]]) {
        if (not dom.midi_command(owner, opened, step[0], step[1])) {
            dom.audio_close(owner, opened)
            return null
        }
    }
    return opened
}

// the narrator: the most natural male Mandarin voice installed, since
// dom.speech_voices() lists voices best first; null falls back to the zh-CN default.
fn narrator_voice() {
    let male = [for (v in dom.speech_voices() where v.lang == "zh-CN" and v.gender == "male") v]
    if (len(male) > 0) male[0] else null
}

pn stop_narration(owner, lines) {
    for (line in lines) { dom.speech_command(owner, line, "cancel") }
    dom.set_attribute(owner, "data-state", "off")
    dom.set_text_content(owner, "旁白 · Narration")
}

// starting the score bumps data-epoch; cityscape.js then rewinds the film and its captions to 0.
// Narration speaks the subtitles still ahead at their film times, scheduled on the
// document's audio clock (Radiant_Design_Speech §3.4): clock0 is that clock when the score began.
view <sound_control> state token: null, epoch: 0, clock0: 0.0, lines: [], spoken: 0 { ~.rendered }
on click(evt) {
    let narrate = dom.closest(evt.target, "#narrate")
    if (narrate != null) {
        if (len(lines) > 0) {
            stop_narration(narrate, lines)
            lines = []
            return 'handled'
        }
        let playing = if (token == null) null else dom.midi_info(narrate, token)
        if (playing == null or playing.state != "playing") {
            dom.set_attribute(narrate, "data-state", "error")
            dom.set_text_content(narrate, "旁白 · 先开声音 · Sound first")
            return 'handled'
        }
        let elapsed = dom.audio_clock(narrate) - clock0
        let voice = narrator_voice()
        dom.set_attribute(narrate, "data-voice", if (voice == null) "default" else voice.name)
        for (s in subtitles) {
            if (s.t > elapsed + 0.2) {
                // a slightly unhurried pace suits documentary narration.
                let line = dom.speak(narrate, s.zh, {voice: if (voice == null) null else voice.id,
                    lang: "zh-CN", rate: 0.9, at: clock0 + s.t})
                if (line != null) { lines = lines ++ [line] }
            }
        }
        dom.set_attribute(narrate, "data-state", if (len(lines) > 0) "on" else "error")
        dom.set_text_content(narrate, if (len(lines) > 0) "旁白 · 关 · Narration off" else "旁白 · 无声 · No voice")
        return 'handled'
    }
    let owner = dom.closest(evt.target, "#sound")
    if (owner == null) { return 'pass' }
    // the film restarts or goes quiet, so narration cued to the old start stops too.
    if (len(lines) > 0) {
        stop_narration(dom.query_selector(dom.closest(owner, ".audio"), "#narrate"), lines)
        lines = []
    }
    let info = if (token == null) null else dom.midi_info(owner, token)
    if (token != null) dom.audio_close(owner, token)
    token = null
    // a click while the score plays mutes it; any other click restarts the film with sound.
    if (info != null and info.state == "playing") {
        dom.set_attribute(owner, "data-state", "off")
        dom.set_text_content(owner, "♪ 声音 · Sound")
        return 'handled'
    }
    token = start_soundtrack(owner)
    if (token == null) {
        dom.set_attribute(owner, "data-state", "error")
        dom.set_text_content(owner, "♪ 无声 · No audio")
        return 'handled'
    }
    clock0 = dom.audio_clock(owner)
    epoch = epoch + 1
    dom.set_attribute(owner, "data-epoch", string(epoch))
    dom.set_attribute(owner, "data-state", "on")
    dom.set_text_content(owner, "♪ 静音 · Mute")
    return 'handled'
}
// each narrated line that finishes is counted on the narration button.
on speechend(evt) {
    if (evt.cancelled) { return 'handled' }
    spoken = spoken + 1
    dom.set_attribute(evt.target, "data-spoken", string(spoken))
    return 'handled'
}

<html
    <head
        <meta charset: "utf-8">
        <title "一座城的年轮 · The Rings of a City">
        <style css>
    >
    <body
        <div class: "stage", *[
            scene,
            <div class: "shade">,
            <div class: "hud", style: timed(7.5, 146.0),
                *[<div "成都 · 三环以内 · 1:10000">, <div class: "en", "CHENGDU · INSIDE THE THIRD RING · OPENSTREETMAP">]>,
            <div class: "timeline", style: timed(15.5, 143.5), *[
                *[for (i in 0 to (len(ticks) - 1)) <div class: "tick", style: "left:" ++ tick_left(i), ticks[i]>],
                <div class: "marker">]>,
            title_card(0.0, 7.5), title_card(146.5, 160.0),
            *[for (c in chapters) era(c)],
            *[for (c in subtitles) caption(c, "sub")],
            apply(<sound_control rendered: <div class: "audio", *[sound_button, narration_button]> >)
        ]>
        <script type: "module", src: "cityscape.js">
    >
>
