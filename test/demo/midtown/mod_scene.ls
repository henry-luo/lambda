// The native Lumen Bay scene: merged city geometry per material, instanced street furniture, a
// physics world (fixed city, kinematic cars and people, dynamic props), native clips for the
// signals, crane, harbour and beacons, and three lighting looks. Every moving thing is a physics
// body so per-frame motion travels as pose commands instead of scene edits.
import s: lambda.scene3d
import o: lambda.scene3d.outdoor
import post: lambda.scene3d.postprocessing
import physics: lambda.physics
import t: lambda.scene3d.transform
import u: .mod_util
import city: .mod_city
import m: .mod_mesh
import tx: .mod_textures
import tr: .mod_traffic
import cam: .mod_camera

// collision groups
pub let STATIC = 1
pub let PLAYER = 2
pub let TRAFFIC = 4
pub let PROPS = 8
pub let NOBODY = {membership: 0, filter: 0}
pub let PARKED_Y = -60.0              // hidden bodies wait below the city

// ---- lighting looks; `sun` points from the city toward the light
pub let LOOKS = {
    day: {label: "DAY", background: "#a9c3da", fog: "#b8c9d7", density: 0.0026, sun: [0.42, 0.78, 0.33], sun_color: "#fff2df",
        sun_i: 2.5, sky: "#d8e7ff", ground: "#5f5c50", hemi_i: 0.8, env_i: 0.9, exposure: 1.0, windows: 0.0, shops: 0.15,
        bulbs: 0.3, pools: false, bloom: 0.15, zenith: [0.33, 0.52, 0.8], horizon: [0.72, 0.8, 0.86]},
    dusk: {label: "DUSK", background: "#c98f7c", fog: "#c08f84", density: 0.0032, sun: [-0.78, 0.17, 0.42], sun_color: "#ffad74",
        sun_i: 2.3, sky: "#f0b08c", ground: "#3a3240", hemi_i: 0.6, env_i: 0.55, exposure: 1.05, windows: 0.8, shops: 1.4,
        bulbs: 5.0, pools: true, bloom: 0.4, zenith: [0.25, 0.3, 0.55], horizon: [0.95, 0.62, 0.45]},
    night: {label: "NIGHT", background: "#0c1324", fog: "#121a2d", density: 0.0038, sun: [0.32, 0.72, -0.48], sun_color: "#9fb5ff",
        sun_i: 0.32, sky: "#273a62", ground: "#0b0d13", hemi_i: 0.42, env_i: 0.16, exposure: 1.15, windows: 1.7, shops: 2.4,
        bulbs: 10.0, pools: true, bloom: 0.75, zenith: [0.02, 0.03, 0.08], horizon: [0.07, 0.1, 0.18]}}
pub let LOOK_ORDER = ["day", "dusk", "night"]
pub fn next_look(look) => (let i = [for (k, n in LOOK_ORDER where n == look) k][0], LOOK_ORDER[(i + 1) % len(LOOK_ORDER)])
pub let HEADLIGHT = 420.0

// ---- materials
fn paint_id(color) => "m-paint-" ++ slice(color, 1, 7)
let TRAFFIC_PAINTS = [*tr.PAINTS, "#f2c230"]
fn std(id, color, options = {}) element => s.material('standard', {id: id, color: color, roughness: 0.8, *: options})
fn materials(look, lights) => [
    std("m-asphalt", "#ffffff", {texture: "t-asphalt", roughness: 0.93}),
    std("m-sidewalk", "#ffffff", {texture: "t-sidewalk", roughness: 0.9}),
    std("m-grass", "#ffffff", {texture: "t-grass", roughness: 0.97}),
    std("m-paving", "#ffffff", {texture: "t-paving", roughness: 0.9}),
    std("m-gravel", "#ffffff", {texture: "t-gravel", roughness: 1.0}),
    std("m-line-white", "#e8e6df", {roughness: 0.7}),
    std("m-line-yellow", "#e2b33c", {roughness: 0.7}),
    *[for (k in 0 to (city.STYLES - 1)) std("m-facade-" ++ string(k), "#ffffff", {texture: "t-facade-" ++ string(k),
        'emissive-texture': "t-facade-" ++ string(k) ++ "-lit", emissive: "#ffffff", 'emissive-intensity': look.windows,
        roughness: if (k == 0) 0.25 else 0.75, metalness: if (k == 0) 0.45 else 0.0})],
    std("m-store", "#ffffff", {texture: "t-store", 'emissive-texture': "t-store-lit", emissive: "#ffffff", 'emissive-intensity': look.shops, roughness: 0.55}),
    std("m-roof", "#ffffff", {texture: "t-roof", roughness: 0.95}),
    std("m-concrete", "#a9a7a1", {roughness: 0.9}),
    std("m-metal", "#3b4047", {roughness: 0.45, metalness: 0.6}),
    std("m-steel", "#6d7177", {roughness: 0.5, metalness: 0.7}),
    std("m-crane", "#e3b23c", {roughness: 0.55, metalness: 0.2}),
    s.material('basic', {id: "m-bulb", color: "#fff4d6", emissive: "#ffe2a8", 'emissive-intensity': look.bulbs}),
    s.material('basic', {id: "m-pool", color: "#ffd9a0", texture: "t-pool", 'alpha-mode': 'blend', opacity: 0.55}),
    std("m-signal", "#1d2024", {roughness: 0.6}),
    *[for (g in 0 to 1, c in ["red", "amber", "green"]) s.material('basic', {id: "sig-" ++ string(g) ++ "-" ++ c, color: "#151515",
        emissive: "#000000", 'emissive-intensity': 4.0})],
    std("m-trunk", "#5a4636", {roughness: 0.95}),
    o.foliage({id: "m-leaf", color: "#4f7a3a"}),
    std("m-water-basin", "#7e9bb0", {roughness: 0.15, metalness: 0.1}),
    // glossy, not transmissive: refraction would cost a second opaque pass and its capture targets
    std("m-water", "#1f4d60", {roughness: 0.06, metalness: 0.35, 'water-amplitude': 0.05, 'water-frequency': 0.3}),
    s.material('basic', {id: "m-sky", color: "#ffffff", side: 'back'}),
    std("m-hill", "#4c6b3e", {roughness: 1.0}),
    // vehicles
    *[for (c in [*TRAFFIC_PAINTS, "#c0262d"]) std(paint_id(c), c, {roughness: 0.32, metalness: 0.35})],
    std("m-parked", "#ffffff", {roughness: 0.35, metalness: 0.3}),
    std("m-glass", "#1c2732", {roughness: 0.08, metalness: 0.6}),
    std("m-trim", "#1b1c1f", {roughness: 0.7}),
    std("m-tyre", "#191919", {roughness: 0.9}),
    std("m-hub", "#c9ccd1", {roughness: 0.25, metalness: 0.9}),
    s.material('basic', {id: "m-headlamp", color: "#f4f1e6", emissive: "#fff2d0", 'emissive-intensity': if (lights) 6.0 else 0.5}),
    s.material('basic', {id: "m-taillamp", color: "#6e0c0c", emissive: "#ff2a1a", 'emissive-intensity': if (look.windows > 0.0) 3.0 else 1.0}),
    std("m-person", "#ffffff", {roughness: 0.85}),
    // props
    std("m-cone", "#f05a1a", {roughness: 0.6}),
    std("m-crate", "#ffffff", {texture: "t-crate", roughness: 0.85}),
    std("m-barrel", "#2f5f9a", {roughness: 0.4, metalness: 0.5}),
    std("m-bin", "#2d5a3b", {roughness: 0.7}),
    s.material('basic', {id: "m-beacon-pickup", color: "#000000", emissive: "#3cff9a", 'emissive-intensity': 1.6, 'alpha-mode': 'blend', opacity: 0.35, side: 'double'}),
    s.material('basic', {id: "m-beacon-drop", color: "#000000", emissive: "#ffb12e", 'emissive-intensity': 1.6, 'alpha-mode': 'blend', opacity: 0.35, side: 'double'}),
    s.material('basic', {id: "m-parcel", color: "#c58a3c", emissive: "#ffcf7a", 'emissive-intensity': 1.2})]

fn textures() => [
    *[for (k in 0 to (city.STYLES - 1)) *[tx.texture("t-facade-" ++ string(k), tx.facade_svg(k, false), 512, 512),
        tx.texture("t-facade-" ++ string(k) ++ "-lit", tx.facade_svg(k, true), 512, 512)]],
    tx.texture("t-store", tx.storefront_svg(false), 512, 144, {'wrap-t': 33071}),
    tx.texture("t-store-lit", tx.storefront_svg(true), 512, 144, {'wrap-t': 33071}),
    tx.texture("t-asphalt", tx.asphalt_svg(), 256, 256), tx.texture("t-sidewalk", tx.sidewalk_svg(), 256, 256),
    tx.texture("t-grass", tx.grass_svg(), 256, 256), tx.texture("t-paving", tx.paving_svg(), 256, 256),
    tx.texture("t-gravel", tx.gravel_svg(), 256, 256), tx.texture("t-roof", tx.roof_svg(), 128, 128),
    tx.texture("t-crate", tx.crate_svg(), 128, 128), tx.texture("t-pool", tx.pool_svg(), 128, 128, {'wrap-s': 33071, 'wrap-t': 33071})]

// ---- shared geometry: one buffer per vehicle kind and part, people, props and furniture
pub let PARTS = ["paint", "glass", "trim", "head", "tail"]
pub let PART_MATERIAL = {glass: "m-glass", trim: "m-trim", head: "m-headlamp", tail: "m-taillamp"}
let SHIRTS = [[0.75, 0.2, 0.2], [0.2, 0.42, 0.7], [0.92, 0.75, 0.3], [0.3, 0.6, 0.38], [0.92, 0.92, 0.9], [0.45, 0.3, 0.6], [0.95, 0.5, 0.25], [0.2, 0.2, 0.22]]
let PANTS = [[0.15, 0.18, 0.3], [0.25, 0.25, 0.27], [0.5, 0.42, 0.32], [0.1, 0.1, 0.12]]
let SKINS = [[0.95, 0.8, 0.66], [0.78, 0.57, 0.42], [0.55, 0.38, 0.27], [0.9, 0.72, 0.58]]
pub let PERSON_VARIANTS = 8
fn shared_geometry() => [
    *[for (kind in ["sedan", "hatch", "van", "taxi", "pickup"], let parts = m.vehicle(kind)) for (p in PARTS) m.geometry("g-" ++ kind ++ "-" ++ p, parts[p])],
    *[for (p in PARTS, let parts = m.vehicle("sedan", false)) m.geometry("g-player-" ++ p, parts[p])],
    m.geometry("g-tyre", m.tyre()), m.geometry("g-hub", m.hub()),
    *[for (k in 0 to (PERSON_VARIANTS - 1)) m.geometry("g-person-" ++ string(k),
        m.person(SHIRTS[k], PANTS[k % 4], SKINS[(k * 3) % 4], if (k % 3 == 0) [0.35, 0.22, 0.12] else [0.08, 0.07, 0.07]))],
    m.geometry("g-cone", m.cone()), m.geometry("g-crate", m.crate(1.0)), m.geometry("g-barrel", m.barrel()), m.geometry("g-bin", m.bin()),
    m.geometry("g-beacon", m.post(0.0, 0.0, 0.0, 40.0, 2.4, 2.4, 20)),
    m.geometry("g-parcel", m.block(0.0, -0.35, 0.0, 0.7, 0.7, 0.7))]

// ---- static city geometry, merged per material
fn mesh(id, b, material, options = {}) element => s.mesh(m.geometry("g-" ++ id, b), material, {id: id, *: options})
fn kind_lots(kind, tile) => m.merge([for (b in city.BLOCKS where b.kind == kind) m.ground_quad(b.lx0, b.lz0, b.lx1, b.lz1, city.CURB + 0.004, tile)])
// a stop bar across the approaching lane, from the centreline to the curb on the driver's right
fn stop_bar(cx, cz, f, r) => if (abs(f[0]) > 0.5)
        m.ground_quad(cx - 0.25, min(cz + r[1] * 0.3, cz + r[1] * city.ROAD), cx + 0.25, max(cz + r[1] * 0.3, cz + r[1] * city.ROAD), 0.013, 1.0)
    else m.ground_quad(min(cx + r[0] * 0.3, cx + r[0] * city.ROAD), cz - 0.25, max(cx + r[0] * 0.3, cx + r[0] * city.ROAD), cz + 0.25, 0.013, 1.0)
// centre lines between junction zones, stop lines before each signalled approach, zebra crossings on every arm
fn markings() {
    let n = city.N, e = city.EDGE
    let yellow = [for (i in 0 to n, k in 0 to (n - 1), axis in 0 to 1, off in [-0.17, 0.17],
            let a = city.road(k) + e, let b = city.road(k + 1) - e, let c = city.road(i) + off)
        if (axis == 0) m.ground_quad(c - 0.06, a, c + 0.06, b, 0.012, 1.0) else m.ground_quad(a, c - 0.06, b, c + 0.06, 0.012, 1.0)]
    let stops = [for (sg in city.SIGNALS, let f = city.DIRS[sg.d])
        stop_bar(city.road(sg.i) - f[0] * (e + 0.5), city.road(sg.j) - f[1] * (e + 0.5), f, [0.0 - f[1], f[0]])]
    let zebra = [for (i in 0 to n, j in 0 to n, d in 0 to 3, s in 0 to 9, let f = city.DIRS[d]
            where i + int(f[0]) >= 0 and i + int(f[0]) <= n and j + int(f[1]) >= 0 and j + int(f[1]) <= n)
        (let x = city.road(i), (let z = city.road(j), (let across = -5.4 + s * 1.2, (let near = e - 2.6,
            if (abs(f[0]) > 0.5) m.ground_quad(x + f[0] * near - (if (f[0] < 0.0) 2.2 else 0.0), z + across - 0.3, x + f[0] * near + (if (f[0] > 0.0) 2.2 else 0.0), z + across + 0.3, 0.013, 1.0)
            else m.ground_quad(x + across - 0.3, z + f[1] * near - (if (f[1] < 0.0) 2.2 else 0.0), x + across + 0.3, z + f[1] * near + (if (f[1] > 0.0) 2.2 else 0.0), 0.013, 1.0)))))];
    {white: m.merge([*stops, *zebra]), yellow: m.merge(yellow)}
}
fn buildings() {
    let g = city.CURB;
    [*[for (k in 0 to (city.STYLES - 1)) mesh("facade-" ++ string(k), m.merge([for (b in city.BUILDINGS where b.style == k) m.facade(b, g)]), "m-facade-" ++ string(k))],
     mesh("storefronts", m.merge([for (b in city.BUILDINGS) m.storefront(b, g)]), "m-store"),
     mesh("roofs", m.merge([for (b in city.BUILDINGS) m.roof(b)]), "m-roof"),
     mesh("rooftops", m.merge([for (b in city.BUILDINGS) m.rooftop(b)]), "m-concrete")]
}
let PARKED_SHAPES = {sedan: m.vehicle("sedan"), hatch: m.vehicle("hatch")}
fn parked_cars() {
    let placed = (c, part) => m.yawed(PARKED_SHAPES[c.kind][part], c.x, 0.0, c.z, c.heading);
    [mesh("parked-paint", m.merge([for (c in city.PARKED) m.tinted(placed(c, "paint"), hex_rgb(c.paint))]), "m-parked"),
     *[for (p in ["glass", "trim", "head", "tail"]) mesh("parked-" ++ p, m.merge([for (c in city.PARKED) placed(c, p)]), PART_MATERIAL[p], {'cast-shadow': p == "glass" or p == "trim"})]]
}
let HEX = "0123456789abcdef"
fn nibble(ch) => [for (k in 0 to 15 where slice(HEX, k, k + 1) == lower(ch)) k][0]
fn hex_rgb(c) => [for (k in 0 to 2) (nibble(slice(c, 1 + 2 * k, 2 + 2 * k)) * 16 + nibble(slice(c, 2 + 2 * k, 3 + 2 * k))) / 255.0]
fn scatter(items, size_of) => [for (p in items) t.compose([p.x, p.y or 0.0, p.z], u.quat(p.face or p.heading or 0.0), [size_of(p), size_of(p), size_of(p)])]
fn instanced(id, geometry_id, material, matrices, options = {}) element => s.mesh(geometry_id, material, {id: id, instances: matrices, *: options})
// trees beyond the barrier, on the grass around the city
let SCENERY_TREES = [for (k in 0 to 59, let a = k * u.TAU / 60.0, let r = city.LIMIT + 24.0 + (k % 3) * 9.0
    where not (cos(a) > 0.45 and abs(sin(a)) < 0.9)) {x: sin(a) * r * 1.02, z: 0.0 - cos(a) * r, s: 1.1 + (k % 4) * 0.12}]
fn street_furniture(look) {
    let trees = [*city.TREES, *SCENERY_TREES]
    let tree_m = scatter(trees, (p) => p.s)
    let lamp_m = scatter(city.LAMPS, (p) => 1.0)
    let signal_m = [for (sg in city.SIGNALS) t.compose([sg.x, city.CURB, sg.z], u.quat(city.heading_of_dir((sg.d + 2) % 4)), [1.0, 1.0, 1.0])];
    [s.resources([m.geometry("g-trunk", m.trunk()), m.geometry("g-crown", m.crown()), m.geometry("g-lamp", m.lamp_post()),
        m.geometry("g-bulb", m.lamp_bulb()), m.geometry("g-pool", m.light_pool()), m.geometry("g-mast", m.signal_mast()),
        *[for (k in 0 to 2) m.geometry("g-signal-" ++ string(k), m.signal_lamp(k))]]),
     instanced("trunks", "g-trunk", "m-trunk", tree_m), instanced("crowns", "g-crown", "m-leaf", tree_m),
     instanced("lamps", "g-lamp", "m-metal", lamp_m), instanced("bulbs", "g-bulb", "m-bulb", lamp_m, {'cast-shadow': false}),
     instanced("pools", "g-pool", "m-pool", lamp_m, {visible: look.pools, 'cast-shadow': false, 'receive-shadow': false}),
     instanced("masts", "g-mast", "m-signal", signal_m),
     *[for (g in 0 to 1, k, c in ["red", "amber", "green"])
        instanced("signals-" ++ string(g) ++ "-" ++ c, "g-signal-" ++ string(k), "sig-" ++ string(g) ++ "-" ++ c,
            [for (n, sg in city.SIGNALS where sg.group == g) signal_m[n]], {'cast-shadow': false})]]
}
// the yard's frame and crane, the plaza fountain, the stunt ramps
fn landmarks() {
    let f = city.FRAME, c = city.CRANE, p = city.PLAZA
    let frame = m.merge([*[for (q in city.FRAME_COLUMNS) m.block(q[0], city.CURB, q[1], 0.5, f.floors * f.storey, 0.5)],
        *[for (k in 1 to (f.floors - 1)) m.cuboid(f.x0 - 0.4, city.CURB + k * f.storey - 0.3, f.z0 - 0.4, f.x1 + 0.4, city.CURB + k * f.storey, f.z1 + 0.4)]])
    let fwd = u.forward(c.heading);
    [mesh("frame", frame, "m-concrete"),
     mesh("crane-mast", m.merge([m.block(c.x, city.CURB, c.z, 1.6, c.height, 1.6), m.block(c.x, city.CURB, c.z, 4.0, 1.2, 4.0)]), "m-crane"),
     s.group([mesh("crane-jib", m.merge([m.cuboid(-0.7, 0.0, -10.0, 0.7, 1.4, c.jib), m.block(0.0, -2.4, -9.0, 2.6, 2.4, 3.0),
            m.block(0.0, -1.6, 2.0, 1.8, 1.6, 2.2), m.post(0.0, 0.0, 1.4, 8.0, 0.4, 0.15, 6)]), "m-crane"),
         mesh("crane-hook", m.merge([m.cuboid(-0.03, -18.0, c.jib * 0.7 - 0.03, 0.03, 0.0, c.jib * 0.7 + 0.03), m.block(0.0, -19.0, c.jib * 0.7, 0.8, 1.0, 0.8)]), "m-steel")],
        {id: "crane-top", position: [c.x, city.CURB + c.height, c.z], quaternion: u.quat(c.heading)}),
     mesh("fountain", m.merge([m.post(p.x, p.z, city.CURB, city.CURB + 0.7, p.r, p.r, 24), m.post(p.x, p.z, city.CURB + 0.7, city.CURB + 2.6, 0.5, 0.25, 10)]), "m-concrete"),
     mesh("fountain-water", m.post(p.x, p.z, city.CURB + 0.7, city.CURB + 0.72, p.r - 0.35, p.r - 0.35, 24), "m-water-basin"),
     mesh("ramps", m.merge([for (r in city.RAMPS) m.wedge(r, city.CURB)]), "m-steel")]
}
// the harbour south of the barrier, hills and a sky dome around everything
fn surroundings(look) {
    let l = city.LIMIT, sh = city.SHORE
    let dome = s.sphere(12, 24)
    let colors = [for (i in 0 to (len(dome.positions) div 3 - 1), let y = dome.positions[3 * i + 1] * 2.0, let k = u.clamp(y * 1.6, 0.0, 1.0))
        *[for (c in 0 to 2) u.lerp(look.horizon[c], look.zenith[c], k)]];
    [mesh("grass", m.ground_quad(-1600.0, -1600.0, 1600.0, sh, -0.03, 6.0), "m-grass", {'cast-shadow': false}),
     mesh("barrier", m.merge([m.cuboid(0.0 - l - 1.0, 0.0, 0.0 - l - 1.0, l + 1.0, 1.1, 0.0 - l), m.cuboid(0.0 - l - 1.0, 0.0, l, l + 1.0, 1.1, l + 1.0),
        m.cuboid(0.0 - l - 1.0, 0.0, 0.0 - l, 0.0 - l, 1.1, l), m.cuboid(l, 0.0, 0.0 - l, l + 1.0, 1.1, l)]), "m-concrete"),
     mesh("promenade", m.slab(0.0 - l - 1.0, l + 1.0, l + 1.0, sh, -0.03, city.CURB, 4.0), "m-paving"),
     mesh("sea-wall", m.merge([m.cuboid(0.0 - l - 1.0, -3.0, sh, l + 1.0, city.CURB, sh + 1.2), *[for (k in 0 to 40) m.block(0.0 - l + k * 10.1, city.CURB, sh + 0.5, 0.12, 1.0, 0.12)],
        m.cuboid(0.0 - l - 1.0, city.CURB + 0.95, sh + 0.45, l + 1.0, city.CURB + 1.05, sh + 0.55)]), "m-steel"),
     mesh("harbour", m.ground_quad(-1600.0, sh + 1.2, 1600.0, 1600.0, -1.2, 40.0), "m-water", {'cast-shadow': false}),
     mesh("hills", m.merge([for (k in 0 to 13, let a = -1.9 + k * 0.29, let r = 880.0 + (k % 3) * 120.0)
        m.yawed(m.post(0.0, 0.0, -2.0, 60.0 + (k % 4) * 35.0, 260.0 + (k % 2) * 90.0, 20.0, 9), sin(a) * r, 0.0, 0.0 - cos(a) * r, a)]), "m-hill", {'cast-shadow': false}),
     s.mesh(s.geometry(dome.positions, {id: "g-sky", normals: dome.normals, indices: dome.indices, colors: colors}), "m-sky",
        {id: "sky-dome", scale: [2600.0, 2600.0, 2600.0], 'cast-shadow': false, 'receive-shadow': false, 'frustum-culling': false})]
}
// the sidewalk between the outermost roads and the perimeter barrier
fn outer_walks(o, l) => [m.slab(0.0 - l, 0.0 - l, l, 0.0 - o, 0.0, city.CURB, 4.0), m.slab(0.0 - l, o, l, l, 0.0, city.CURB, 4.0),
    m.slab(0.0 - l, 0.0 - o, 0.0 - o, o, 0.0, city.CURB, 4.0), m.slab(o, 0.0 - o, l, o, 0.0, city.CURB, 4.0)]
fn ground_cover() {
    let lines = markings();
    [mesh("asphalt", m.ground_quad(0.0 - city.LIMIT, 0.0 - city.LIMIT, city.LIMIT, city.LIMIT, 0.0, 8.0), "m-asphalt", {'cast-shadow': false}),
     mesh("sidewalks", m.merge([*[for (b in city.BLOCKS) m.slab(b.x0 + city.ROAD, b.z0 + city.ROAD, b.x1 - city.ROAD, b.z1 - city.ROAD, 0.0, city.CURB, 4.0)],
        *outer_walks(city.HALF + city.ROAD, city.LIMIT)]), "m-sidewalk"),
     mesh("lots-park", kind_lots("P", 6.0), "m-grass", {'cast-shadow': false}),
     mesh("lots-plaza", kind_lots("X", 4.0), "m-paving", {'cast-shadow': false}),
     mesh("lots-stunt", kind_lots("S", 8.0), "m-asphalt", {'cast-shadow': false}),
     mesh("lots-yard", kind_lots("C", 6.0), "m-gravel", {'cast-shadow': false}),
     mesh("lines-white", lines.white, "m-line-white", {'cast-shadow': false}),
     mesh("lines-yellow", lines.yellow, "m-line-yellow", {'cast-shadow': false})]
}

// ---- physics: the fixed city, then everything that moves
fn box_collider(id, x0, y0, z0, x1, y1, z1) element => physics.collider(id, 'box',
    {size: [x1 - x0, y1 - y0, z1 - z0], position: [(x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0], groups: {membership: STATIC, filter: PROPS}, friction: 0.7})
fn city_body() element {
    let walls = [for (k, o in city.OBSTACLES where o[2] - o[0] > 1.0 or o[3] - o[1] > 1.0) box_collider("wall-" ++ string(k), o[0], 0.0, o[1], o[2], o[4], o[3])]
    let slabs = [for (b in city.BLOCKS) box_collider("slab-" ++ string(b.bi) ++ "-" ++ string(b.bj), b.x0 + city.ROAD, -0.5, b.z0 + city.ROAD, b.x1 - city.ROAD, city.CURB, b.z1 - city.ROAD)]
    let ramps = [for (k, r in city.RAMPS, let f = u.forward(r.heading), let side = u.right(r.heading))
        physics.collider("ramp-" ++ string(k), 'convex', {groups: {membership: STATIC, filter: PROPS},
            vertices: [for (a in [-1.0, 1.0], c in [-1.0, 1.0], top in [false, true] where a > 0.0 or not top)
                [r.x + f[0] * a * r.length / 2.0 + side[0] * c * r.width / 2.0, city.CURB + (if (top) r.height else 0.0),
                 r.z + f[1] * a * r.length / 2.0 + side[1] * c * r.width / 2.0]]})];
    physics.body("city-solid", [box_collider("ground", -400.0, -1.0, -400.0, 400.0, 0.0, 400.0), *slabs, *walls, *ramps], {type: 'fixed'})
}
// the player's car: body panels, headlights and a box the props feel; the wheels ride separately
pub let WHEEL_IDS = ["wheel-fl", "wheel-fr", "wheel-rl", "wheel-rr"]
fn player_body(car, look, lights) element => physics.body("player", [
    physics.collider("player-shape", 'box', {size: [1.86, 1.1, 4.5], position: [0.0, 0.85, 0.0], groups: {membership: PLAYER, filter: PROPS}}),
    *[for (p in PARTS) s.mesh("g-player-" ++ p, if (p == "paint") "m-paint-c0262d" else PART_MATERIAL[p], {id: "player-" ++ p})],
    *[for (side in [-1.0, 1.0]) s.light('spot', {id: if (side < 0.0) "head-l" else "head-r", position: [side * 0.62, 0.7, 2.2],
        target: [side * 0.9, 0.0, 16.0], color: "#fff1d6", intensity: if (lights) HEADLIGHT else 0.0, angle: 0.48, penumbra: 0.5, distance: 55.0})]],
    {type: 'kinematic-position', position: [car.x, car.y, car.z], quaternion: u.quat(car.heading)})
fn wheel_body(id) element => physics.body(id, [physics.collider(id ++ "-shape", 'sphere', {radius: 0.05, groups: NOBODY}),
    s.mesh("g-tyre", "m-tyre", {id: id ++ "-tyre"}), s.mesh("g-hub", "m-hub", {id: id ++ "-hub"})],
    {type: 'kinematic-position', position: [0.0, PARKED_Y, 0.0]})
// traffic and people take their IDs from their records, so they are built as literal bodies
fn traffic_body(a) element => <'rigid-body' id: a.id, type: 'kinematic-position', position: [a.x, 0.0, a.z], quaternion: u.quat(a.heading),
    *[physics.collider(a.id ++ "-shape", 'box', {size: [a.hw * 2.0, 1.3, a.hl * 2.0], position: [0.0, 0.95, 0.0], groups: {membership: TRAFFIC, filter: PROPS}}),
      *[for (p in PARTS) s.mesh("g-" ++ a.kind ++ "-" ++ p, if (p == "paint") paint_id(a.paint) else PART_MATERIAL[p], {id: a.id ++ "-" ++ p})]]>
fn ped_body(p) element => <'rigid-body' id: p.id, type: 'kinematic-position', position: [p.x, p.y, p.z], quaternion: u.quat(p.heading),
    *[physics.collider(p.id ++ "-shape", 'sphere', {radius: 0.2, groups: NOBODY}),
      s.mesh("g-person-" ++ string(p.n % PERSON_VARIANTS), "m-person", {id: p.id ++ "-mesh"})]>
fn prop_body(k, p) element {
    let id = "prop-" ++ string(k)
    let groups = {membership: PROPS, filter: STATIC + PLAYER + TRAFFIC + PROPS}
    let shape = if (p.kind == "crate") physics.collider(id ++ "-shape", 'box', {size: [1.0, 1.0, 1.0], density: 55.0, friction: 0.6, groups: groups})
        else if (p.kind == "bin") physics.collider(id ++ "-shape", 'box', {size: [0.62, 0.92, 0.62], density: 30.0, friction: 0.5, groups: groups})
        else if (p.kind == "cone") physics.collider(id ++ "-shape", 'convex', {vertices: m.CONE_HULL, density: 90.0, friction: 0.7, groups: groups})
        else physics.collider(id ++ "-shape", 'convex', {vertices: m.BARREL_HULL, density: 140.0, friction: 0.5, restitution: 0.2, groups: groups});
    physics.body(id, [shape, s.mesh("g-" ++ p.kind, "m-" ++ p.kind, {id: id ++ "-mesh"})],
        {type: 'dynamic', position: [p.x, p.y, p.z], quaternion: u.quat(p.yaw), 'linear-damping': 0.15, 'angular-damping': 0.2})
}
fn beacon_body(kind) element => physics.body("beacon-" ++ kind, [physics.collider("beacon-" ++ kind ++ "-shape", 'sphere', {radius: 0.1, groups: NOBODY}),
    s.mesh("g-beacon", "m-beacon-" ++ kind, {id: "beacon-" ++ kind ++ "-light", 'cast-shadow': false, 'receive-shadow': false}),
    s.mesh("g-parcel", "m-parcel", {id: "beacon-" ++ kind ++ "-parcel", position: [0.0, 3.2, 0.0]})],
    {type: 'kinematic-position', position: [0.0, PARKED_Y, 0.0]})

// the camera rides its own kinematic body, so following the car is a pose command rather than a
// camera attribute edit (which would relayout the page every frame); it looks down its local -Z
fn camera_body(c) element => physics.body("camera-rig", [physics.collider("camera-rig-shape", 'sphere', {radius: 0.05, groups: NOBODY}),
    <camera id: "cam", type: 'perspective', fov: c.fov, near: 0.3, far: 3200.0, position: [0.0, 0.0, 0.0]>],
    {type: 'kinematic-position', position: c.position, quaternion: cam.look(c.position, c.target)})

// ---- clips on the scene clock: signal lamps, the crane's slow slew, harbour ripples, beacon pulse
let ON = {red: [1.0, 0.06, 0.03], amber: [1.0, 0.55, 0.02], green: [0.1, 1.0, 0.45]}
let OFF = [0.03, 0.025, 0.02]
fn lamp_track(g, c) element {
    // group 1 runs half a cycle behind group 0
    let shift = if (g == 1) tr.CYCLE / 2.0 else 0.0
    let spans = if (c == "green") [[0.0, tr.GREEN]] else if (c == "amber") [[tr.GREEN, tr.GREEN + tr.AMBER]] else [[tr.GREEN + tr.AMBER, tr.CYCLE]]
    let edges = [for (sp in spans, e in sp) (e + shift) - tr.CYCLE * floor((e + shift) / tr.CYCLE)]
    let marks = sort(unique([0.0, *edges, tr.CYCLE]))
    let lit_at = (at) => any([for (sp in spans, let local = (at - shift) - tr.CYCLE * floor((at - shift) / tr.CYCLE))
        local >= sp[0] - 0.0001 and local < sp[1] - 0.0001]);
    s.track("sig-" ++ string(g) ++ "-" ++ c ++ ".emissive", marks, [for (at in marks, x in (if (lit_at(at)) ON[c] else OFF)) x], 'color', {interpolation: 'discrete'})
}
pub fn clips() => [
    s.clip("signals", [for (g in 0 to 1, c in ["red", "amber", "green"]) lamp_track(g, c)], {duration: tr.CYCLE, autoplay: true}),
    s.clip("crane", [s.track("crane-top.quaternion", [0.0, 30.0, 60.0, 90.0, 120.0],
        [for (a in [0.0, 1.1, 0.4, -0.7, 0.0], q in u.quat(city.CRANE.heading + a)) q], 'quaternion')], {duration: 120.0, autoplay: true}),
    s.clip("harbour-ripples", [s.track("m-water.water-phase", [0.0, 12.0], [0.0, u.TAU])], {duration: 12.0, autoplay: true}),
    s.clip("beacons", [for (k in ["pickup", "drop"]) s.track("m-beacon-" ++ k ++ ".emissive", [0.0, 0.6, 1.2],
        [*(if (k == "pickup") [0.24, 1.0, 0.6] else [1.0, 0.7, 0.18]), *(if (k == "pickup") [0.6, 1.0, 0.85] else [1.0, 0.9, 0.55]),
         *(if (k == "pickup") [0.24, 1.0, 0.6] else [1.0, 0.7, 0.18])], 'color')], {duration: 1.2, autoplay: true})]

let SKY = s.environment('sky', {id: "sky", 'sun-direction': LOOKS.day.sun, 'sun-intensity': 12.0}) ^ { null }
// ---- the whole scene
pub fn scene(st) element {
    let look = LOOKS[st.look];
    s.scene([
        physics.world({state: 'playing', gravity: [0.0, -9.81, 0.0], timestep: 1.0 / 120.0, 'solver-iterations': 10}),
        s.light('directional', {id: "sun", position: [for (x in look.sun) x * 300.0], target: [0.0, 0.0, 0.0], color: look.sun_color,
            intensity: look.sun_i, 'cast-shadow': true, 'shadow-cascades': 3, 'shadow-distance': 220.0, 'shadow-map-size': 2048,
            'shadow-normal-bias': 0.05, 'shadow-softness': 1.5}),
        s.light('hemisphere', {id: "hemi", position: [0.0, 1.0, 0.0], color: look.sky, 'ground-color': look.ground, intensity: look.hemi_i}),
        o.fog('height', {id: "haze", color: look.fog, density: look.density, height: 0.0, falloff: 0.012}),
        s.resources([*(if (SKY == null) [] else [SKY]), *textures(), *materials(look, st.lights), *shared_geometry()]),
        *ground_cover(), *buildings(), *parked_cars(), *street_furniture(look), *landmarks(), *surroundings(look),
        city_body(), player_body(st.car, look, st.lights), *[for (id in WHEEL_IDS) wheel_body(id)],
        *[for (a in st.traffic) traffic_body(a)], *[for (p in st.peds) ped_body(p)], *[for (k, p in city.PROPS) prop_body(k, p)],
        beacon_body("pickup"), beacon_body("drop"), camera_body(st.camera),
        *clips(),
        post.chain([post.effect('bloom', {id: "glow", strength: look.bloom, threshold: 1.0, radius: 3.0}), post.effect('fxaa')])
    ], {id: "city", camera: "cam", width: 1280, height: 720, viewBox: "0 0 1280 720", preserveAspectRatio: "xMidYMid slice",
        background: look.background, environment: "sky", 'environment-intensity': look.env_i, 'tone-mapping': 'aces',
        // a full-window scene renders at one pixel per CSS pixel and is upsampled on the GPU for dense displays
        'max-pixel-ratio': 1.0,
        exposure: look.exposure, tabindex: "0", role: "img", 'aria-label': "Lumen Bay, a city to drive around in 3D"})
}

// the attribute edits that switch the lighting look, applied once per change
pub fn look_edits(look_name, lights) {
    let look = LOOKS[look_name];
    [{id: "city", key: "background", value: look.background}, {id: "city", key: "environment-intensity", value: look.env_i},
     {id: "city", key: "exposure", value: look.exposure},
     {id: "sun", key: "position", value: [for (x in look.sun) x * 300.0]}, {id: "sun", key: "color", value: look.sun_color},
     {id: "sun", key: "intensity", value: look.sun_i},
     {id: "hemi", key: "color", value: look.sky}, {id: "hemi", key: "ground-color", value: look.ground}, {id: "hemi", key: "intensity", value: look.hemi_i},
     {id: "haze", key: "color", value: look.fog}, {id: "haze", key: "density", value: look.density},
     *[for (k in 0 to (city.STYLES - 1)) {id: "m-facade-" ++ string(k), key: "emissive-intensity", value: look.windows}],
     {id: "m-store", key: "emissive-intensity", value: look.shops}, {id: "m-bulb", key: "emissive-intensity", value: look.bulbs},
     {id: "pools", key: "visible", value: look.pools}, {id: "glow", key: "strength", value: look.bloom},
     {id: "m-taillamp", key: "emissive-intensity", value: if (look.windows > 0.0) 3.0 else 1.0},
     *lights_edits(lights)]
}
pub fn lights_edits(lights) => [{id: "head-l", key: "intensity", value: if (lights) HEADLIGHT else 0.0},
    {id: "head-r", key: "intensity", value: if (lights) HEADLIGHT else 0.0},
    {id: "m-headlamp", key: "emissive-intensity", value: if (lights) 6.0 else 0.5}]
