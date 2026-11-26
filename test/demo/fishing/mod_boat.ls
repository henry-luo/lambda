// The rowboat and its angler, in boat space: +Z is the bow, +Y up, the origin on the waterline
// amidships. The hull, seats and gear are one merged buffer; the angler is a small hierarchy of
// groups (torso, head, arms, forearms); oars and rod are groups too, and the fishing line is a
// skinned ribbon whose bones sit at the rod tip, mid-line, bobber and hook. Every motion of the
// boat's contents is a clip sampled from one pose function, so the host only starts clips, and
// the same functions tell it where oar blades, the bobber and a hooked fish are at any moment.
import s: lambda.scene3d
import physics: lambda.physics
import shape: lambda.scene3d.shape
import t: lambda.scene3d.transform
import v: lambda.scene3d.camera
import prim: lambda.procart.primitive
import m: ~~.midtown.mod_mesh
import u: ~~.midtown.mod_util
import w: .mod_world
import fisher: .mod_fisher

// ---- hull: stations from stern (s = 0) to bow (s = 1), each a U-shaped section
pub let STERN = -1.25
pub let BOW = 1.35
let STATIONS = 16
let SECTION = 10
fn half_width(s) => 0.55 * (if (s < 0.42) 0.76 + 0.24 * sin(1.5708 * s / 0.42) else math.pow(max(0.0, cos(1.5708 * (s - 0.42) / 0.58)), 0.8))
fn sheer(s) => 0.3 + 0.13 * s * s * s
fn keel(s) => -0.17 + 0.12 * math.pow(s, 4.0)
fn station_z(s) => STERN + s * (BOW - STERN)
// a point on section s at angle a (-pi/2 port gunwale .. 0 keel .. pi/2 starboard gunwale), inset inward
fn hull_point(s, a, inset) => (let hw = max(0.0, half_width(s) - inset), let top = sheer(s), let bot = keel(s) + inset * 1.2,
    [hw * sin(a), top - (top - bot) * math.pow(max(0.0, cos(a)), 0.65), station_z(s)])
let PAINT = w.col("#e8673a")
let PAINT_LOW = w.col("#bf4a2d")
let STRAKE = w.col("#f47f3e")
let TRIM = w.col("#efe3c6")
let WOOD = w.col("#d39d63")
let WOOD_DARK = w.col("#c08a52")
let SEAT = w.col("#cfa97c")
fn hull_color(p, k, inner) any^ => if (inner) (if (k % 2 == 0) WOOD else WOOD_DARK) else if (p[1] < 0.03) PAINT_LOW else PAINT
// painted bands just proud of the planking, measured down from the sheer: an orange top strake over a
// cream stripe (bands stay crisp, whatever the hull's vertex spacing)
fn band_angle(s, down, side) => (let top = sheer(s), let bot = keel(s), let r = max(0.0, min(1.0, down / max(top - bot, 0.001))),
    side * math.acos(math.pow(r, 1.0 / 0.65)))
fn paint_band(drop0, drop1, rgb) any^ => m.merge([for (side in [-1.0, 1.0]) {
    positions: [for (i in 0 to STATIONS, d in [drop0, drop1], c in hull_point(i / float(STATIONS), band_angle(i / float(STATIONS), d, side), -0.004)^) c],
    normals: [for (i in 0 to STATIONS, d in [drop0, drop1], c in section_normal(band_angle(i / float(STATIONS), d, side), false)^) c],
    colors: [for (i in 0 to STATIONS, d in [drop0, drop1], c in rgb) c],
    indices: [for (i in 0 to STATIONS - 1, q in (if (side > 0.0) [i * 2, i * 2 + 2, i * 2 + 1, i * 2 + 1, i * 2 + 2, i * 2 + 3] else [i * 2, i * 2 + 1, i * 2 + 2, i * 2 + 1, i * 2 + 3, i * 2 + 2])) q]}])
fn section_angle(k) => -1.5708 + 3.14159 * k / float(SECTION)
fn surface(inset, inner) any^ {
    let pts = [for (i in 0 to STATIONS, k in 0 to SECTION) hull_point(i / float(STATIONS), section_angle(k), inset)]
    let normals = [for (i in 0 to STATIONS, k in 0 to SECTION, c in section_normal(section_angle(k), inner)) c]
    let row = SECTION + 1;
    {positions: [for (p in pts, c in p) c], normals: normals, colors: [for (i, p in pts, c in hull_color(p, (i % row), inner)^) c],
     indices: [for (i in 0 to STATIONS - 1, k in 0 to SECTION - 1, q in (if (inner) quad_in(i * row + k, row) else quad_out(i * row + k, row))) q]}
}
fn section_normal(a, inner) => if (inner) [0.0 - sin(a), cos(a), 0.0] else [sin(a), 0.0 - cos(a), 0.0]
fn quad_out(a, row) => [a, a + 1, a + row, a + 1, a + row + 1, a + row]
fn quad_in(a, row) => [a, a + row, a + 1, a + 1, a + row, a + row + 1]
// the gunwale cap joins the outer and inner top edges on both sides; the transom closes the stern
fn gunwale(side) any^ {
    let k = if (side > 0) SECTION else 0
    let outer = [for (i in 0 to STATIONS) hull_point(i / float(STATIONS), section_angle(k), 0.0)]
    let inner = [for (i in 0 to STATIONS) hull_point(i / float(STATIONS), section_angle(k), 0.04)]
    let n = STATIONS + 1;
    {positions: [*[for (p in outer, c in p) c], *[for (p in inner, c in [p[0], p[1] + 0.01, p[2]]) c]],
     normals: [for (i in 0 to 2 * n - 1, c in [0.0, 1.0, 0.0]) c], colors: [for (i in 0 to 2 * n - 1, c in STRAKE) c],
     indices: [for (i in 0 to STATIONS - 1, q in (if (side > 0) [i, i + n, i + 1, i + 1, i + n, i + n + 1] else [i, i + 1, i + n, i + 1, i + n + 1, i + n])) q]}
}
fn transom() any^ {
    let outer = [for (k in 0 to SECTION) hull_point(0.0, section_angle(k), 0.0)]
    let centre = [0.0, sheer(0.0) * 0.6, STERN];
    {positions: [*centre, *[for (p in outer, c in p) c]], normals: [for (i in 0 to SECTION + 1, c in [0.0, 0.0, -1.0]) c],
     colors: [*PAINT, *[for (p in outer, c in (if (p[1] > 0.22) STRAKE else PAINT)) c]],
     indices: [for (k in 0 to SECTION - 1, q in [0, k + 1, k + 2]) q]}
}
// the floorboards: an opaque deck just above the waterline across the whole interior, so the water
// film never shows inside the hull; planks run fore and aft in three alternating bands
pub let DECK = 0.04
fn deck_half(s) => (let inset = 0.035, let hw = max(0.0, half_width(s) - inset), let top = sheer(s), let bot = keel(s) + inset * 1.2,
    let r = max(0.0, min(1.0, (top - DECK) / max(top - bot, 0.001))), hw * sin(math.acos(math.pow(r, 1.0 / 0.65))))
fn deck() any^ {
    let bands = [[-1.0, -0.34], [-0.34, 0.34], [0.34, 1.0]]
    let rows = [for (i in 0 to STATIONS) {z: station_z(i / float(STATIONS)), hw: deck_half(i / float(STATIONS))}]
    let n = STATIONS + 1;
    m.merge([for (k, band in bands) {positions: [for (r in rows, f in band, c in [f * r.hw, DECK, r.z]) c],
        normals: [for (r in rows, f in band, c in [0.0, 1.0, 0.0]) c], colors: [for (r in rows, f in band, c in (if (k % 2 == 0) WOOD_DARK else WOOD)) c],
        indices: [for (i in 0 to STATIONS - 1, q in [i * 2, i * 2 + 2, i * 2 + 1, i * 2 + 1, i * 2 + 2, i * 2 + 3]) q]}])
}
fn board(x0, y0, z0, x1, y1, z1, rgb) any^ => m.tinted(m.cuboid(x0, y0, z0, x1, y1, z1), rgb)^
fn seat(z, depth) any^ => (let s = (z - STERN) / (BOW - STERN), let hw = half_width(s) - 0.05, board(0.0 - hw, 0.1, z - depth / 2.0, hw, 0.14, z + depth / 2.0, SEAT)^)
let IRON = w.col("#3a3a3a")
let BASKET = w.col("#c9a36a")
let BASKET_DARK = w.col("#a17c47")
let BOX = w.col("#a5503a")
let LANTERN_GLASS = w.col("#ffd27a")
let ROPE = w.col("#d7c49a")
// port (+X); starboard mirrors x. Well forward of the seat: the angler faces the bow and push-rows, so
// the handles come back to the chest at the catch and reach out over the knees at the finish
pub let OARLOCK = [0.535, 0.33, 0.12]
fn basket() any^ => m.merge([for (k in 0 to 4) m.tinted(prim.sweep([[0.36, DECK + k * 0.045, -0.92], [0.36, DECK + 0.045 + k * 0.045, -0.92]], 0.12 + 0.005 * k, 12, k == 0), if (k % 2 == 0) BASKET else BASKET_DARK)^])
fn rope() any^ => m.tinted(prim.sweep([for (i in 0 to 24, let a = i * 0.6) [0.02 + cos(a) * (0.07 + 0.002 * i), DECK + 0.016 + 0.012 * (i div 10), 0.52 + sin(a) * (0.07 + 0.002 * i)]], 0.016, 5, false), ROPE)^
fn lantern_pole() any^ => m.merge([m.tinted(m.post(0.0, 1.1, 0.25, 0.86, 0.018, 0.018, 6), IRON)^, board(-0.06, 0.8, 1.06, 0.06, 0.82, 1.18, IRON)^,
    board(-0.045, 0.68, 1.075, 0.045, 0.8, 1.165, LANTERN_GLASS)^, board(-0.055, 0.66, 1.065, 0.055, 0.68, 1.175, IRON)^])
pub fn boat_buffer() any^ => m.merge([surface(0.0, false)^, surface(0.035, true)^, gunwale(1.0)^, gunwale(-1.0)^, transom()^,
    paint_band(0.0, 0.05, STRAKE)^, paint_band(0.05, 0.075, TRIM)^,
    seat(-0.18, 0.26)^, seat(0.78, 0.22)^, seat(-1.0, 0.3)^,
    deck()^,
    *[for (side in [-1.0, 1.0]) m.tinted(m.post(side * OARLOCK[0], OARLOCK[2], OARLOCK[1] - 0.05, OARLOCK[1] + 0.04, 0.018, 0.018, 6), IRON)^],
    basket()^, board(-0.42, DECK, -0.72, -0.18, DECK + 0.12, -0.52, BOX)^, rope()^, lantern_pole()^])

// ---- small round and tubular parts shared by the oars, rod and bobber
let BALL = shape.sphere(8, 12)
fn blob(cx, cy, cz, rx, ry, rz, rgb) any^ => m.tinted(m.placed(BALL, t.compose([cx, cy, cz], [0.0, 0.0, 0.0, 1.0], [rx * 2.0, ry * 2.0, rz * 2.0]))^, rgb)^
fn limb(a, b, r0, r1, rgb) any^ => m.tinted(prim.sweep([a, b], [r0, r1], 8, true), rgb)^
let GRIP = w.col("#6b4a2f")

// ---- oars and rod
let OAR = w.col("#c79a63")
let BLADE = w.col("#e9ddc0")
let BLADE_TIP = w.col("#e8602f")
pub let OAR_IN = 0.46                        // handle length inboard of the oarlock
pub let OAR_OUT = 1.32                       // blade tip outboard
pub let BLADE_MID = OAR_OUT - 0.19           // the blade's centre, along the oar
// a blade (flat when feathered, x along the oar) from a to b along the oar, in one colour
fn blade(side, a, b, rgb) any^ => m.tinted(m.cuboid(min(side * a, side * b), -0.012, -0.075, max(side * a, side * b), 0.012, 0.075), rgb)^
// the loom, then a wooden blade with a cream stripe and an orange tip
pub fn oar_buffer(side) any^ => m.merge([limb([0.0 - side * OAR_IN, 0.0, 0.0], [side * (OAR_OUT - 0.34), 0.0, 0.0], 0.022, 0.02, OAR)^,
    blade(side, OAR_OUT - 0.38, OAR_OUT - 0.16, OAR)^, blade(side, OAR_OUT - 0.16, OAR_OUT - 0.13, BLADE)^, blade(side, OAR_OUT - 0.13, OAR_OUT, BLADE_TIP)^,
    limb([0.0 - side * OAR_IN, 0.0, 0.0], [0.0 - side * (OAR_IN - 0.13), 0.0, 0.0], 0.03, 0.03, GRIP)^])
pub let ROD = 1.75
let ROD_DARK = w.col("#3c2f2a")
let CORK = w.col("#c9a679")
let REEL = w.col("#9aa3a6")
pub fn rod_buffer() any^ => m.merge([limb([0.0, 0.0, -0.18], [0.0, 0.0, 0.12], 0.024, 0.02, CORK)^, limb([0.0, 0.0, 0.12], [0.0, 0.0, ROD], 0.013, 0.004, ROD_DARK)^,
    blob(0.0, -0.05, 0.0, 0.035, 0.035, 0.025, REEL)^])

// ---- the line: two crossed strips along seven segments, each vertex blended between two bones. The bones
// are the rod tip, five points along the curve the line sags in, the bobber and the hook, so the line
// bends smoothly instead of kinking at one sag point
pub let LINE_REST = [for (k in 0 to 7) [0.0, 1.0, k / 6.0]]
let LINE_SEGMENTS = 7
let LINE_STEPS = 3
let BOBBER_BONE = 7                        // ln-6 in document order (after ln-root)
fn line_vertex(k, j, side, axis) => (let a = LINE_REST[k], let b = LINE_REST[k + 1], let q = j / float(LINE_STEPS),
    let off = 0.006 * side, {p: [a[0] + (b[0] - a[0]) * q + (if (axis == 0) off else 0.0), a[1] + (b[1] - a[1]) * q + (if (axis == 1) off else 0.0), a[2] + (b[2] - a[2]) * q],
        bones: [k + 1, k + 2], wt: [1.0 - q, q]})
pub fn line_geometry() element {
    let verts = [for (axis in 0 to 1, k in 0 to LINE_SEGMENTS - 1, j in 0 to LINE_STEPS, side in [-1.0, 1.0]) line_vertex(k, j, side, axis)]
    let per_axis = LINE_SEGMENTS * (LINE_STEPS + 1) * 2
    let tris = [for (axis in 0 to 1, k in 0 to LINE_SEGMENTS - 1, j in 0 to LINE_STEPS - 1, q in strip(axis * per_axis + (k * (LINE_STEPS + 1) + j) * 2)) q];
    s.geometry([for (q in verts, c in q.p) c], {id: "g-line", normals: [for (q in verts, c in [0.0, 1.0, 0.0]) c], indices: tris,
        'skin-indices': [for (q in verts, c in [q.bones[0], q.bones[1], 0, 0]) c], 'skin-weights': [for (q in verts, c in [q.wt[0], q.wt[1], 0.0, 0.0]) c]})
}
fn strip(a) => [a, a + 1, a + 2, a + 1, a + 3, a + 2]
let BOB_RED = w.col("#e2412f")
let BOB_WHITE = w.col("#f6f1e6")
// the bobber is skinned whole to the bone that rides at the float (built at that bone's rest point)
pub fn bobber_geometry() any^ {
    let b = m.merge([blob(0.0, 1.016, 1.0, 0.036, 0.032, 0.036, BOB_RED)^, blob(0.0, 0.988, 1.0, 0.032, 0.024, 0.032, BOB_WHITE)^,
        m.tinted(m.post(0.0, 1.0, 1.04, 1.12, 0.006, 0.004, 4), BOB_RED)^])
    let n = (len(b.positions) div 3)^;
    s.geometry(b.positions, {id: "g-bobber", normals: b.normals, colors: b.colors, indices: b.indices,
        'skin-indices': [for (i in 0 to n - 1, c in [BOBBER_BONE, 0, 0, 0]) c], 'skin-weights': [for (i in 0 to n - 1, c in [1.0, 0.0, 0.0, 0.0]) c]})
}
pub fn line_rig() element => <skeleton id: "line-rig", <bone id: "ln-root", position: [0.0, 0.0, 0.0],
    *[for (k, p in LINE_REST) <bone id: "ln-" ++ string(k), position: p>]>>

// ---- the boat as a kinematic body holding everything that rides in it
fn pivot(id, position, children) element => s.group(children, {id: id, position: position})
fn part(id, buffer, material) any^ => s.mesh(m.geometry("g-" ++ id, buffer), material, {id: id, 'cast-shadow': true, 'receive-shadow': true})
// `riders`: more nodes that move with the boat (the scene's hull ripple emitters)
pub fn body(x, z, heading, riders = []) any^ => physics.body("boat", [
    physics.collider("boat-shape", 'box', {size: [1.1, 0.5, 2.6], groups: {membership: 0, filter: 0}}),
    part("hull", boat_buffer()^, "m-boat")^,
    fisher.skeleton()^, fisher.mesh()^,
    *[for (side in [-1.0, 1.0], let tag = if (side < 0.0) "r" else "l")
        pivot("oar-" ++ tag, [side * OARLOCK[0], OARLOCK[1], OARLOCK[2]], [part("oar-mesh-" ++ tag, oar_buffer(side)^, "m-oar")^])],
    pivot("rod", [-0.38, 0.06, -0.62], [part("rod-mesh", rod_buffer()^, "m-boat")^]),
    line_rig(),
    s.mesh(line_geometry(), "m-line", {id: "line", skeleton: "line-rig", 'cast-shadow': false, bounds: [-4.0, -2.0, -4.0, 4.0, 4.0, 8.0]}),
    s.mesh(bobber_geometry()^, "m-boat", {id: "bobber", skeleton: "line-rig", 'cast-shadow': true, bounds: [-4.0, -2.0, -4.0, 4.0, 4.0, 8.0]}),
    *riders],
    {type: 'kinematic-position', position: [x, 0.0, z], quaternion: u.quat(heading)})
pub fn materials() any^ => [s.material('lambert', {id: "m-boat", color: "#ffffff", side: 'double'}),
    s.material('basic', {id: "m-line", color: "#f6f2e4", side: 'double'})]
