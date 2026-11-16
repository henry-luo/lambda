// The scene beyond the city: terrain chunks and the far land, the loop road with its verges, the
// gateway roundabout, the truss and cable-stayed bridges, the ridge tunnel and the guardrails.
// Everything here is static geometry built once (OR7); nothing is edited per frame.
import s: lambda.scene3d
import u: .mod_util
import m: .mod_mesh
import city: .mod_city
import land: .mod_land
import roads: .mod_roads
import lm: .mod_land_mesh
import tx: .mod_textures
import t: lambda.scene3d.transform
import o: lambda.scene3d.outdoor
import scenery: .mod_scenery
import sm: .mod_scenery_mesh

fn std(id, color, options = {}) element => s.material('standard', {id: id, color: color, roughness: 0.8, *: options})
pub fn textures() any^ => [tx.texture("t-terrain", tx.terrain_svg(), 256, 256), tx.texture("t-road", tx.road_svg(), 256, 512, {'wrap-s': 33071}),
    tx.texture("t-tunnel", tx.tunnel_svg(), 256, 256), tx.texture("t-shade", tx.flat_svg("#3a3a3a"), 8, 8)]
// the lighthouse lantern glows after dark
pub fn lantern(look) => if (look.windows > 0.0) 9.0 else 0.4
pub fn materials(look) any^ => [
    std("m-terrain", "#ffffff", {texture: "t-terrain", roughness: 0.97}),
    std("m-road", "#ffffff", {texture: "t-road", roughness: 0.94}),
    std("m-truss", "#55705f", {roughness: 0.5, metalness: 0.55}),
    std("m-pylon", "#cbc6bb", {roughness: 0.82}),
    std("m-stay", "#e2e3e0", {roughness: 0.3, metalness: 0.7}),
    std("m-rail", "#a8acb1", {roughness: 0.32, metalness: 0.8}),
    // the bore keeps a quarter of the sky light (an occlusion map), so its lamps carry it
    std("m-tunnel", "#ffffff", {texture: "t-tunnel", roughness: 0.55, 'occlusion-texture': "t-shade"}),
    std("m-road-bore", "#ffffff", {texture: "t-road", roughness: 0.94, 'occlusion-texture': "t-shade"}),
    s.material('basic', {id: "m-tunnel-lamp", color: "#fff6dd", emissive: "#ffe6b0", 'emissive-intensity': 3.2}),
    std("m-delineator", "#eeeeea", {roughness: 0.5}),
    std("m-stone", "#b8b0a3", {roughness: 0.75}),
    // scenery is coloured per vertex: these materials are white
    o.foliage({id: "m-tree", color: "#ffffff", roughness: 0.85}),
    std("m-farm", "#ffffff", {roughness: 0.82}),
    std("m-turbine", "#ffffff", {roughness: 0.45, metalness: 0.1}),
    std("m-rock", "#ffffff", {roughness: 0.95}),
    std("m-boat", "#ffffff", {roughness: 0.45}),
    std("m-sail", "#ffffff", {roughness: 0.8, side: 'double'}),
    s.material('basic', {id: "m-lantern", color: "#fff6dc", emissive: "#ffe7a8", 'emissive-intensity': lantern(look)})]

// region buffers go to the geometry as they are (no element-by-element copy)
fn geometry(id, b) element^ => s.geometry(b.positions, {id: id, normals: b.normals, indices: b.indices,
    *: (if (b.uvs != null) {uvs: b.uvs} else {}), *: (if (b.colors != null) {colors: b.colors} else {})})
fn mesh(id, b, material, options = {}) element^ => s.mesh(geometry("g-" ++ id, b), material, {id: id, *: options})
fn flat(options = {}) => {'cast-shadow': false, *: options}
fn structure(st) any^ {
    let k = "-" ++ string(st.start_k);
    if (st.kind == "truss") (let b = lm.truss_bridge(st), [mesh("truss-deck" ++ k, b.deck, "m-concrete"), mesh("truss-steel" ++ k, b.steel, "m-truss")])
    else if (st.kind == "cable") (let b = lm.cable_bridge(st), [mesh("cable-deck" ++ k, b.deck, "m-concrete"), mesh("cable-pylon" ++ k, b.pylon, "m-pylon"),
        mesh("cable-stays" ++ k, b.stays, "m-stay"), mesh("cable-rail" ++ k, b.rail, "m-rail"), mesh("cable-bulbs" ++ k, b.bulbs, "m-bulb", flat())])
    else (let b = lm.tunnel(st), [mesh("tunnel-bore" ++ k, b.bore, "m-tunnel"), mesh("tunnel-lamps" ++ k, b.lamps, "m-tunnel-lamp", flat()),
        mesh("tunnel-portals" ++ k, b.portals, "m-concrete"), mesh("tunnel-walks" ++ k, b.walks, "m-concrete")])
}
// the loop's asphalt in runs: open road, and the tunnel bore under its own dimmer material
let BORES = [for (st in roads.STRUCTURES where st.kind == "tunnel") [st.start_k, st.end_k]]
let BORE_RUNS = (let n = len(roads.LOOP.samples), let b = BORES[0], [[0, b[0], "m-road"], [b[0], b[1], "m-road-bore"], [b[1], n - 1, "m-road"]])
// ---- scenery: vegetation instanced in four tiles (the near grid's quarters) so the view culls what it
// cannot see; static buildings merged; turbine rotors and boats moved by native clips
let MID_X = (land.X0 + land.X1) / 2.0
let MID_Z = (land.Z0 + land.Z1) / 2.0
fn tile(p) => (if (p.x < MID_X) 0 else 1) + (if (p.z < MID_Z) 0 else 2)
fn matrix(p) any^ => t.compose([p.x, p.y, p.z], u.quat(p.yaw), [p.s, p.s, p.s])
fn instanced(id, geometry_id, material, items, options = {}) any^ =>
    [for (q in 0 to 3, let mine = [for (p in items where tile(p)^ == q) matrix(p)^] where len(mine) > 0)
        s.mesh(geometry_id, material, {id: id ++ "-" ++ string(q), instances: mine, *: options})]
fn placed_copies(b, items) any^ => m.merge([for (p in items) m.yawed(b, p.x, p.y, p.z, p.heading)^])
pub fn scenery_elements() any^ {
    let lh = scenery.LIGHTHOUSE;
    [s.resources([geometry("g-pine", sm.pine()), geometry("g-broad", sm.broadleaf()), geometry("g-bush", sm.bush()),
        geometry("g-rock", sm.rock()), geometry("g-rotor", sm.turbine_rotor()), geometry("g-hull", sm.hull()), geometry("g-sails", sm.sails())]),
     *instanced("pines", "g-pine", "m-tree", [for (p in scenery.TREES where p.kind == "pine") p])^,
     *instanced("broadleaves", "g-broad", "m-tree", [for (p in scenery.TREES where p.kind == "broad") p])^,
     *instanced("hedges", "g-bush", "m-tree", scenery.BUSHES)^,
     s.mesh("g-rock", "m-rock", {id: "sea-rocks", instances: [for (p in scenery.ROCKS) matrix(p)^], 'cast-shadow': false}),
     mesh("farmsteads", placed_copies(sm.farmstead(), scenery.FARMS)^, "m-farm"),
     mesh("turbine-towers", placed_copies(sm.turbine_tower(scenery.TURBINE_HUB), scenery.TURBINES)^, "m-turbine"),
     *[for (k, tb in scenery.TURBINES) s.group([s.group([s.mesh("g-rotor", "m-turbine", {id: "rotor-" ++ string(k) ++ "-mesh"})],
            {id: "rotor-" ++ string(k), position: [0.0, scenery.TURBINE_HUB, 2.6]})],
        {id: "turbine-" ++ string(k), position: [tb.x, tb.y, tb.z], quaternion: u.quat(tb.heading)})],
     mesh("lighthouse", m.yawed(sm.lighthouse(), lh.x, lh.y, lh.z, 0.0)^, "m-farm"),
     mesh("lighthouse-lantern", m.yawed(sm.lantern(), lh.x, lh.y, lh.z, 0.0)^, "m-lantern", flat()),
     *[for (b in scenery.BOATS) s.group([s.mesh("g-hull", "m-boat", {id: b.id ++ "-hull"}), s.mesh("g-sails", "m-sail", {id: b.id ++ "-sails"})],
        {id: b.id, position: [b.x, land.SEA, b.z], quaternion: u.quat(b.heading)})]]
}
// rotors turn once every five seconds; boats rise, fall and rock on a slow swell
fn spin(a) => [0.0, 0.0, sin(a / 2.0), cos(a / 2.0)]
pub fn clips() any^ => [
    s.clip("turbines", [for (k, tb in scenery.TURBINES) s.track("rotor-" ++ string(k) ++ ".quaternion", [0.0, 5.0 / 3.0, 10.0 / 3.0, 5.0],
        [for (q in 0 to 3, c in spin(tb.phase + q * u.TAU / 3.0)^) c], 'quaternion')], {duration: 5.0, autoplay: true})^,
    s.clip("boats", [*[for (b in scenery.BOATS) s.track(b.id ++ ".position", [0.0, 2.0, 4.0, 6.0],
            [for (q in 0 to 3, c in [b.x, land.SEA + 0.25 * sin(b.phase + q * u.PI / 2.0), b.z]) c], 'vector')],
        *[for (b in scenery.BOATS) s.track(b.id ++ ".quaternion", [0.0, 3.0, 6.0],
            [for (q in 0 to 2, c in u.quat(b.heading, 0.0, 0.05 * sin(b.phase + q * u.PI))^) c], 'quaternion')]], {duration: 6.0, autoplay: true})^]
pub fn elements() any^ {
    let ra = lm.roundabout();
    [*[for (q, c in lm.CHUNKS) mesh("terrain-" ++ string(q), lm.terrain_chunk(c), "m-terrain")],
     mesh("terrain-far", lm.far_terrain(), "m-terrain", flat()), mesh("terrain-rim", lm.rim_skirt(), "m-terrain", flat()),
     *[for (q, run in BORE_RUNS) mesh("road-loop-" ++ string(q), lm.asphalt(roads.LOOP, run[0], run[1]), run[2], flat())],
     mesh("road-verges", lm.verges(), "m-terrain", flat()),
     mesh("r1-apron", ra.apron, "m-asphalt", flat()), mesh("r1-lines", ra.lines, "m-line-white", flat()),
     mesh("r1-island", ra.island, "m-grass", flat()), mesh("r1-curb", ra.curb, "m-concrete"), mesh("r1-monument", ra.stone, "m-stone"),
     *[for (st in roads.STRUCTURES) *structure(st)^],
     mesh("guardrails", lm.guardrails(), "m-rail"), mesh("delineators", lm.delineators(), "m-delineator")]
}
