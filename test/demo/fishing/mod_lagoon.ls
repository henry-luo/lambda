// The cove's static meshes, built once from mod_world: the floor and banks, the water and caustic
// films, rocks, lily pads, floor life (tube sponges, starfish, urchins, pebbles), the jetty, and
// instanced reeds and bank tufts that sway in the shared wind clip, and instanced underwater plants
// (strap weed, kelp, sword plants, lettuce and a carpet of leafy sheets) whose leaves wave in the plant
// shader. Everything that does
// not move shares a few merged buffers; colour lives in the vertices (linear light).
import s: lambda.scene3d
import o: lambda.scene3d.outdoor
import shape: lambda.scene3d.shape
import t: lambda.scene3d.transform
import prim: lambda.procart.primitive
import v: lambda.scene3d.camera
import m: ~~.midtown.mod_mesh
import u: ~~.midtown.mod_util
import w: .mod_world
import glsl: .mod_glsl
import flora: .mod_flora

fn tint(b, rgb) any^ => m.tinted(b, rgb)^
fn yaw_q(a) => u.quat(a)
fn place(b, x, y, z, yaw, sx, sy, sz) any^ =>
    m.placed(b, t.compose([x, y, z], yaw_q(yaw), [sx, sy, sz]))^

// ---- the floor grid and the two films above it share one height sample per vertex
let STEP = 0.75
let EX = 30.0
let EZ = 22.5
let NX = 80
let NZ = 60
fn gx(i) => 0.0 - EX + i * STEP
fn gz(j) => 0.0 - EZ + j * STEP
let HEIGHTS = [for (j in 0 to NZ, i in 0 to NX) w.floor_y(gx(i), gz(j))^]
fn h(i, j) => HEIGHTS[min(NZ, max(0, j)) * (NX + 1) + min(NX, max(0, i))]
fn cell(a, row) => [a, a + row, a + 1, a + 1, a + row, a + row + 1]
// The floor, water and caustic film share the same grid topology.
let GRID_INDICES = [for (j in 0 to NZ - 1, i in 0 to NX - 1, k in cell(j * (NX + 1) + i, NX + 1)) k]
fn grid_normal(i, j) any^ => v.unit([h(i - 1, j) - h(i + 1, j), 2.0 * STEP, h(i, j - 1) - h(i, j + 1)])^
pub fn floor_buffer() any^ {
    {positions: [for (j in 0 to NZ, i in 0 to NX, c in [gx(i), h(i, j), gz(j)]) c],
     normals: [for (j in 0 to NZ, i in 0 to NX, c in grid_normal(i, j)^) c],
     colors: [for (j in 0 to NZ, i in 0 to NX, c in w.floor_color(gx(i), gz(j), h(i, j))^) c], indices: GRID_INDICES}
}
// the films carry depth (r) and the beach foam lip (g) in their vertex colours
fn film_info(y) => (let d = max(0.0, 0.0 - y), [min(1.0, d / 2.2), if (y > 0.05) 0.0 else 1.0 - u.clamp(d / 0.3, 0.0, 1.0), 0.0])
let FILM_NORMALS = [for (y in HEIGHTS, c in [0.0, 1.0, 0.0]) c]
let FILM_COLORS = [for (y in HEIGHTS, c in film_info(y)^) c]
pub fn film_buffer(lift) any^ {
    {positions: [for (j in 0 to NZ, i in 0 to NX, c in [gx(i), if (lift == 0.0) 0.0 else h(i, j) + lift, gz(j)]) c],
     normals: FILM_NORMALS, colors: FILM_COLORS, indices: GRID_INDICES}
}

let ROCK_BASE = shape.sphere(6, 10)
// ---- floor life
let TUBE_A = w.col("#e0b04f")
let TUBE_B = w.col("#a58fc4")
let TUBE_IN = w.col("#5b4630")
fn tube(x, y, z, r, height, rgb) any^ =>
    m.merge([tint(prim.sweep([[x, y - 0.05, z], [x, y + height, z]], [r, r * 1.15], 8, false), rgb)^,
        tint(m.post(x, z, y + height - 0.01, y + height, r * 0.8, r * 0.8, 8), TUBE_IN)^])
fn tube_cluster(p) any^ => (let rgb = if (p.i % 2 == 0) TUBE_A else TUBE_B,
    m.merge([for (j in 0 to 3 + p.i % 3, let a = j * 2.4 + p.i, let d = 0.08 + 0.05 * (j % 2))
        tube(p.x + cos(a) * d, p.y, p.z + sin(a) * d, 0.035 + 0.01 * (j % 3), 0.18 + 0.09 * ((j + p.i) % 4), rgb)^]))
let STAR = w.col("#e8743f")
let STAR_TIP = w.col("#f3a35c")
fn star_buffer(p) any^ {
    let pts = [for (i in 0 to 9, let a = i * u.TAU / 10.0, let r = if (i % 2 == 0) 0.17 else 0.06) [cos(a) * r, sin(a) * r]]
    let b = {positions: [0.0, 0.025, 0.0, *[for (q in pts, c in [q[0], 0.0, q[1]]) c]],
        normals: [for (i in 0 to 10, c in [0.0, 1.0, 0.0]) c], colors: [*STAR, *[for (i in 0 to 9, c in (if (i % 2 == 0) STAR_TIP else STAR)) c]],
        indices: [for (i in 0 to 9, k in [0, (i + 1) % 10 + 1, i + 1]) k]}
    return place(b, p.x, p.y + 0.01, p.z, p.a * 3.0, 1.0 + 0.3 * (p.i % 3), 1.0, 1.0 + 0.3 * (p.i % 3))^
}
let URCHIN = w.col("#4b3c6b")
fn urchin_buffer(p) any^ => m.merge([tint(place(ROCK_BASE, p.x, p.y + 0.04, p.z, 0.0, 0.14, 0.11, 0.14)^, URCHIN)^,
    *[for (j in 0 to 11, let a = j * 2.39996, let e = 0.3 + 0.6 * ((j * 7) % 11) / 11.0)
        tint(prim.sweep([[p.x, p.y + 0.05, p.z], [p.x + cos(a) * 0.14 * (1.0 - e * 0.5), p.y + 0.05 + 0.14 * e, p.z + sin(a) * 0.14 * (1.0 - e * 0.5)]],
            [0.008, 0.001], 3, false), URCHIN)^]])
let PEBBLE = [w.col("#b7ab90"), w.col("#8f8a7c"), w.col("#c9bfa6")]
fn pebble_buffer(p) any^ => tint(place(ROCK_BASE, p.x, p.y + 0.02, p.z, p.a, 0.12 + 0.05 * (p.i % 3), 0.05, 0.09 + 0.04 * (p.i % 2))^, PEBBLE[p.i % 3])^

// ---- the jetty: a plank deck on posts, with a lantern post at its end
let PLANK = [w.col("#c2ab88"), w.col("#b29a78"), w.col("#cbb593")]
let POST = w.col("#5e4a36")
fn jetty_buffer() any^ {
    let j = w.JETTY
    let n = int((j.z0 - j.z1) / 0.32)^
    let planks = [for (k in 0 to n - 1, let z = j.z1 + k * 0.32)
        tint(m.cuboid(j.x - j.width / 2.0 - 0.04 * (k % 2), j.deck, z + 0.02, j.x + j.width / 2.0 + 0.04 * ((k + 1) % 2), j.deck + 0.06, z + 0.3), PLANK[k % 3])^]
    let posts = [for (z in [j.z1 + 0.1, j.z1 + 1.8, j.z1 + 3.5], x in [j.x - j.width / 2.0 + 0.1, j.x + j.width / 2.0 - 0.1])
        tint(m.post(x, z, -1.4, j.deck + 0.02, 0.08, 0.08, 7), POST)^]
    let rails = [tint(m.post(j.x + j.width / 2.0 - 0.1, j.z1 + 0.1, j.deck, j.deck + 0.9, 0.04, 0.04, 6), POST)^]
    return m.merge([*planks, *posts, *rails])
}
pub let LANTERN = [w.JETTY.x + w.JETTY.width / 2.0 - 0.1, w.JETTY.deck + 0.95, w.JETTY.z1 + 0.1]

// ---- instanced plants: one clump each, rooted at its origin so the wind bends it from the base
fn ribbon(a, lean, height, width, base, tip) => (let c = cos(a), let sn = sin(a), let lx = cos(a + 1.57) * lean, let lz = sin(a + 1.57) * lean,
    {positions: [-c * width, 0.0, -sn * width, c * width, 0.0, sn * width,
        lx * 0.5 + c * width * 0.7, height * 0.55, lz * 0.5 + sn * width * 0.7, lx * 0.5 - c * width * 0.7, height * 0.55, lz * 0.5 - sn * width * 0.7,
        lx, height, lz],
     normals: [for (k in 0 to 4, x in [lx, 0.6, lz]) x], colors: [*base, *base, *tip, *tip, *tip], indices: [0, 1, 2, 0, 2, 3, 3, 2, 4]})
let REED = w.col("#6f8f45")
let REED_TIP = w.col("#b5c27a")
let CATTAIL = w.col("#6a4428")
pub fn reed_buffer() any^ => m.merge([*[for (k in 0 to 11) ribbon(k * 2.1, 0.12 + 0.05 * (k % 3), 0.9 + 0.25 * (k % 4), 0.022, REED, REED_TIP)],
    *[for (k in 0 to 2, let a = k * 2.0) tint(prim.sweep([[cos(a) * 0.05, 0.85 + 0.12 * k, sin(a) * 0.05], [cos(a) * 0.06, 1.07 + 0.12 * k, sin(a) * 0.06]], 0.025, 6, true), CATTAIL)^]])
let TUFT = w.col("#5f8c3c")
let TUFT_TIP = w.col("#a9c76a")
pub fn tuft_buffer() any^ => m.merge([for (k in 0 to 6) ribbon(k * 2.6, 0.1 + 0.03 * (k % 2), 0.22 + 0.05 * (k % 3), 0.03, TUFT, TUFT_TIP)])
// one transform per spot; `tall` is the clump's height at scale 1, and underwater clumps shrink so their
// tips stay below the surface (null for plants on the bank)
fn instances(spots, size, tall = null) any^ => [for (p in spots, let k = size + 0.15 * (p.i % 3), let ky = size + 0.2 * (p.i % 4),
    let room = if (tall == null) ky else min(ky, max(0.2, (0.0 - p.y - 0.15) / tall)))
    s.transform([p.x, p.y - 0.02, p.z], [0.0, p.i * 1.7, 0.0], [k, room, k])^]

// ---- the cove as scene children
pub let SUN = v.unit([-0.32, 0.78, -0.54])^
// the underwater plants' wave shader, one material per kind of plant: [amplitude, reference height,
// wavelength along the leaf, rate]
// the plants' (and dipped oars') lighting, plus each one's own uniforms. refraction: how far what lies under
// the surface wavers per metre of water above it; haze: the colour the water veils it in
fn lit_material(id, vertex, uniforms) any^ => s.shader_material(vertex, glsl.PLANT_FRAGMENT, {time: 0.0, sun: SUN,
    sun_color: [for (c in w.col("#fff1d9")) c * 0.95], sky: [for (c in w.col("#cdebf0")) c * 0.62], ground: [for (c in w.col("#40583a")) c * 0.62], *: uniforms},
    {id: id, side: 'double'})
fn plant_material(id, wave) any^ => lit_material(id, glsl.PLANT_VERTEX, {wave: wave, refraction: 0.025, haze: w.col("#4f7f6c")})^
let PLANTS = {['m-weed']: [0.16, 0.9, 0.85, 1.25], ['m-kelp']: [0.3, 1.4, 1.2, 0.9], ['m-sword']: [0.06, 0.45, 0.7, 1.1],
    ['m-lettuce']: [0.04, 0.25, 0.5, 1.2], ['m-carpet']: [0.025, 0.1, 0.45, 1.0]}
pub fn materials() any^ => [
    s.material('lambert', {id: "m-ground", color: "#ffffff"}),
    s.material('lambert', {id: "m-plant", color: "#ffffff", side: 'double', 'wind-strength': 0.07, 'wind-height': 0.5, 'wind-frequency': 0.35}),
    s.material('lambert', {id: "m-reed", color: "#ffffff", side: 'double', 'wind-strength': 0.06, 'wind-height': 1.0, 'wind-frequency': 0.25}),
    s.material('lambert', {id: "m-lotus", color: "#ffffff", side: 'double', 'wind-strength': 0.025, 'wind-height': 0.6, 'wind-frequency': 0.4}),
    s.shader_material(glsl.SURFACE_VERTEX, glsl.WATER_FRAGMENT, {time: 0.0, sun: SUN,
        shallow: [0.06, 0.6, 0.56], deep: [0.0, 0.3, 0.36], sky: [0.62, 0.86, 0.9]}, {id: "m-water", transparent: true}),
    s.shader_material(glsl.SURFACE_VERTEX, glsl.CAUSTIC_FRAGMENT, {time: 0.0, strength: 0.3}, {id: "m-caustic", transparent: true}),
    *[for (id, wave in PLANTS) plant_material(string(id), wave)^],
    lit_material("m-oar", glsl.IMMERSED_VERTEX, {refraction: 0.15, haze: w.col("#3d8c86")})^]
pub fn elements() any^ => [
    s.mesh(m.geometry("g-floor", floor_buffer()^), "m-ground", {id: "floor", 'cast-shadow': false, 'receive-shadow': true}),
    s.mesh(m.geometry("g-rocks", m.merge([for (k in w.ROCKS) flora.rock_buffer(k)^])), "m-ground", {id: "rocks", 'cast-shadow': true, 'receive-shadow': true}),
    s.mesh(m.geometry("g-life", m.merge([*[for (p in w.TUBES) tube_cluster(p)^], *[for (p in w.STARS) star_buffer(p)^],
        *[for (p in w.URCHINS) urchin_buffer(p)^], *[for (p in w.PEBBLES) pebble_buffer(p)^], jetty_buffer()^])), "m-ground",
        {id: "floor-life", 'cast-shadow': true, 'receive-shadow': true}),
    s.mesh(m.geometry("g-pads", m.merge([*[for (p in w.PADS) flora.pad_buffer(p)^], *[for (p in w.PADS where p.flower != null) flora.lily_buffer(p)^]])),
        "m-plant", {id: "pads", 'cast-shadow': true, 'receive-shadow': true}),
    s.mesh(m.geometry("g-lotus", m.merge([for (st in w.LOTUS) flora.lotus_buffer(st)^])), "m-lotus", {id: "lotus", 'cast-shadow': true, 'receive-shadow': true}),
    s.mesh(m.geometry("g-carpet", flora.carpet_buffer()^), "m-carpet", {id: "carpet", instances: instances(w.CARPET, 1.1, 0.12)^, 'cast-shadow': false}),
    s.mesh(m.geometry("g-lettuce", flora.lettuce_buffer()^), "m-lettuce", {id: "lettuce", instances: instances(w.LETTUCE, 1.5, 0.3)^, 'cast-shadow': true}),
    s.mesh(m.geometry("g-swords", flora.sword_buffer()^), "m-sword", {id: "swords", instances: instances(w.SWORDS, 1.1, 0.5)^, 'cast-shadow': true}),
    s.mesh(m.geometry("g-kelp", flora.kelp_buffer()^), "m-kelp", {id: "kelp", instances: instances(w.KELP, 1.0, 1.65)^, 'cast-shadow': true}),
    s.mesh(m.geometry("g-fans", flora.fan_buffer()^), "m-plant", {id: "fans", instances: instances(w.FANS, 1.2, 0.45)^, 'cast-shadow': true}),
    s.mesh(m.geometry("g-moss", flora.moss_buffer()^), "m-ground", {id: "moss", instances: instances(w.MOSS_BALLS, 0.9, 0.2)^, 'cast-shadow': true}),
    s.mesh(m.geometry("g-weed", flora.weed_buffer()^), "m-weed", {id: "weed", instances: instances(w.GRASS_CLUMPS, 1.0, 1.0)^, 'cast-shadow': true}),
    s.mesh(m.geometry("g-reeds", reed_buffer()^), "m-reed", {id: "reeds", instances: instances(w.REEDS, 1.0)^, 'cast-shadow': true}),
    s.mesh(m.geometry("g-tufts", tuft_buffer()^), "m-plant", {id: "tufts", instances: instances(w.TUFTS, 1.2)^, 'cast-shadow': false}),
    s.mesh(m.geometry("g-caustic", film_buffer(0.025)^), "m-caustic", {id: "caustics", 'cast-shadow': false, 'receive-shadow': false})]
// the water film, its own element so the scene places it right after the cove's other meshes
pub fn water() any^ => s.mesh(m.geometry("g-water", film_buffer(0.0)^), "m-water", {id: "water", 'cast-shadow': false, 'receive-shadow': false})
// one long clip carries the water clock; the plants share the wind clip
pub let TIDE = 600.0
pub fn clips() any^ => [
    s.clip("tide", [for (id in ["m-water", "m-caustic", "m-oar", *[for (k, wave in PLANTS) string(k)]]) s.track(id ++ ".uniforms.time", [0.0, TIDE], [0.0, TIDE])],
        {duration: TIDE, autoplay: true}),
    o.wind("m-plant", 5.0)^, o.wind("m-reed", 7.0)^, o.wind("m-lotus", 8.0)^]
