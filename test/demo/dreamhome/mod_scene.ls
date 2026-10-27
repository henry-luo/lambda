// The home in native 3D. The plan's centimetres become metres (plan x is
// world x, plan y is world z, y is up). Four unit shapes (box, cylinder, cone,
// ball) are shared resources that every part scales into place, and one
// lambert material per colour is shared too, so a furnished home stays a few
// hundred small meshes over a handful of geometries.
import C: .mod_catalog
import M: .mod_model
import P: .mod_plan
import lens: lambda.scene3d.camera

fn m(cm) => cm / 100.0
fn r3(v) => [for (x in v) round(x * 1000.0) / 1000.0]
fn vec(v) => join([for (x in r3(v)) string(x)], " ")

// ---- unit shapes, 1 m across and centred on the origin --------------------------------

let SIDES = 16
fn ring(i) => (let a = 2.0 * math.pi * i / SIDES, [math.cos(a), math.sin(a)])
// a frustum from radius .5 at the bottom to `top` at the top, with caps
fn frustum(id, top) {
    let slope = .5 - top
    let k = 1.0 / sqrt(1.0 + slope * slope)
    let tris = [for (i in 0 to SIDES - 1, let a = ring(i), let b = ring(i + 1)) [
        // side quad, wound counter-clockwise seen from outside
        [a[0] * .5, -.5, a[1] * .5], [b[0] * top, .5, b[1] * top], [b[0] * .5, -.5, b[1] * .5],
        [a[0] * .5, -.5, a[1] * .5], [a[0] * top, .5, a[1] * top], [b[0] * top, .5, b[1] * top],
        [0.0, .5, 0.0], [b[0] * top, .5, b[1] * top], [a[0] * top, .5, a[1] * top],
        [0.0, -.5, 0.0], [a[0] * .5, -.5, a[1] * .5], [b[0] * .5, -.5, b[1] * .5]]]
    let normals = [for (i in 0 to SIDES - 1, let a = ring(i), let b = ring(i + 1)) [
        [a[0] * k, slope * k, a[1] * k], [b[0] * k, slope * k, b[1] * k], [b[0] * k, slope * k, b[1] * k],
        [a[0] * k, slope * k, a[1] * k], [a[0] * k, slope * k, a[1] * k], [b[0] * k, slope * k, b[1] * k],
        [0.0, 1.0, 0.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0], [0.0, -1.0, 0.0], [0.0, -1.0, 0.0], [0.0, -1.0, 0.0]]];
    <geometry id: id, type: 'buffer', positions: [for (t in tris, p in t, x in p) x], normals: [for (t in normals, p in t, x in p) x]>
}
// a ball of 8 bands, each vertex's normal pointing out from the centre
fn globe(id) {
    let pole = (i, j) => (let phi = math.pi * i / 8.0, let th = 2.0 * math.pi * j / 12.0,
        [math.sin(phi) * math.cos(th) * .5, math.cos(phi) * .5, math.sin(phi) * math.sin(th) * .5])
    let quads = [for (i in 0 to 7, j in 0 to 11, let a = pole(i + 1, j), let b = pole(i + 1, j + 1), let c = pole(i, j + 1), let d = pole(i, j))
        [a, c, b, a, d, c]];
    <geometry id: id, type: 'buffer', positions: [for (q in quads, p in q, x in p) x], normals: [for (q in quads, p in q, x in p) x * 2.0]>
}
let SHAPES = [<geometry id: "g-box", type: 'box', size: [1.0, 1.0, 1.0]>, frustum("g-cyl", .5), frustum("g-cone", .3), globe("g-ball")]
let GEOMETRY = {box: "g-box", cyl: "g-cyl", cone: "g-cone", ball: "g-ball"}

// ---- materials --------------------------------------------------------------------------

// a material per colour: m-rrggbb, or m-rrggbb-g when it glows (lamps in the evening)
fn mat(color, glow = false) => "m-" ++ slice(lower(color), 1, 7) ++ (if (glow) "-g" else "")
fn material_of(id) => (let color = "#" ++ slice(id, 2, 8), let glow = ends_with(id, "-g"),
    if (color == C.GLASS) <material id: id, type: 'lambert', color: color, opacity: .35, transparent: true, side: 'double'>
    else if (glow) <material id: id, type: 'lambert', color: color, emissive: color, 'emissive-intensity': .9>
    else <material id: id, type: 'lambert', color: color, side: 'double'>)
fn materials_in(node) => [for (c in content(node) where c is element) *(if (name(c) == 'mesh') [c.material] else materials_in(c))]

fn cube(at, size, material, rot = null) => if (rot == null) <mesh geometry: "g-box", material: material, position: r3(at), scale: r3(size)>
    else <mesh geometry: "g-box", material: material, position: r3(at), rotation: [0.0, rot, 0.0], scale: r3(size)>

// ---- furniture ---------------------------------------------------------------------------

let BULBS = ["#fff6d8", "#fff8e8"]
let WHITE_FRAME = "#f5f4f0"
fn part_mesh(p, it, glow) => (let color = P.paint(p.c, it.color), let lit = glow and C.item_of(it.key).light != null and (p.c == "main" or contains(BULBS, color)),
    <mesh geometry: GEOMETRY[p.k], material: mat(color, lit),
        position: r3([m(((p.u0 + p.u1) / 2.0 - .5) * it.w), m((p.t0 + p.t1) / 2.0 * it.h), m(((p.v0 + p.v1) / 2.0 - .5) * it.d)]),
        scale: r3([max(.002, m((p.u1 - p.u0) * it.w)), max(.002, m((p.t1 - p.t0) * it.h)), max(.002, m((p.v1 - p.v0) * it.d))])>)
fn item_node(it, glow) => <group id: "3d-" ++ it.id, position: r3([m(it.x), m(it.elev), m(it.y)]), rotation: [0.0, 0.0 - it.rot / M.DEG, 0.0],
    for (p in C.item_of(it.key).parts) part_mesh(p, it, glow)>

// ---- walls, doors and windows --------------------------------------------------------------

// a stretch of wall along w from s0 to s1, between heights y0 and y1 (cm)
fn wall_piece(w, s0, s1, y0, y1, material) => (let u = M.wall_dir(w), let c = (s0 + s1) / 2.0,
    cube([m(w.x1 + u.x * c), m((y0 + y1) / 2.0), m(w.y1 + u.y * c)], [m(s1 - s0), m(y1 - y0), m(w.thick)], material, 0.0 - M.wall_angle(w) / M.DEG))
fn wall_node(home, w, height) {
    let paint = mat(w.color)
    let solid = [for (s in M.solid_spans(home, w)) wall_piece(w, s[0], s[1], 0.0, height, paint)]
    // above and below each opening, as far as the (possibly cut-away) wall reaches
    let around = [for (o in M.openings_on(home, w.id), let s0 = o.at - o.w / 2.0, let s1 = o.at + o.w / 2.0) *[
        *(if (o.elev > 1.0) [wall_piece(w, s0, s1, 0.0, min(o.elev, height), paint)] else []),
        *(if (o.elev + o.h < height - 1.0) [wall_piece(w, s0, s1, o.elev + o.h, height, paint)] else [])]]
    let ends = M.wall_ends(home, w)
    let cap = wall_piece(w, ends.from, ends.to, height, height + .6, mat("#5a5f66"));
    <group id: "3d-" ++ w.id, *[*solid, *around, cap]>
}
// in the opening's own frame: x along the wall, z across it, y up from the floor
fn frame_parts(o, color) => (let f = 6.0, let t = o.d + 2.0, [
    cube([m(f / 2.0 - o.w / 2.0), m(o.h / 2.0), 0.0], [m(f), m(o.h), m(t)], mat(color)),
    cube([m(o.w / 2.0 - f / 2.0), m(o.h / 2.0), 0.0], [m(f), m(o.h), m(t)], mat(color)),
    cube([0.0, m(o.h - f / 2.0), 0.0], [m(o.w), m(f), m(t)], mat(color))])
// a door leaf swung ajar on its hinge toward the side the plan shows it opening
fn door_leaf(x0, w, h, hinge, side, color) => <group position: [m(x0), 0.0, 0.0], rotation: [0.0, side * hinge * .42, 0.0],
    (cube([m(hinge * w / 2.0), m(h / 2.0), 0.0], [m(w), m(h), .04], mat(color)));
    (cube([m(hinge * (w - 9.0)), m(h * .48), m(side * 3.0)], [.02, .02, .06], mat("#b8bec6")))>
fn opening_node(o) {
    let kind = C.item_of(o.key).opening
    let side = if (o.flip) -1.0 else 1.0
    let hinge = if (o.mirror == true) -1.0 else 1.0
    let fw = o.w - 12.0
    let fh = o.h - 6.0
    let parts = if (kind == "door") [*frame_parts(o, P.paint("tint", o.color)), door_leaf(hinge * (0.0 - fw / 2.0), fw, fh, hinge, side, o.color)]
        else if (kind == "double") [*frame_parts(o, WHITE_FRAME), door_leaf(0.0 - fw / 2.0, fw / 2.0, fh, 1.0, side, o.color),
            door_leaf(fw / 2.0, fw / 2.0, fh, -1.0, side, o.color)]
        else if (kind == "slider") [*frame_parts(o, o.color),
            cube([m(0.0 - fw / 4.0), m(fh / 2.0), m(-2.0)], [m(fw / 2.0 + 4.0), m(fh), .015], mat(C.GLASS)),
            cube([m(fw / 4.0), m(fh / 2.0), m(2.0)], [m(fw / 2.0 + 4.0), m(fh), .015], mat(C.GLASS)),
            cube([0.0, m(fh / 2.0), m(2.0)], [.04, m(fh), .03], mat(o.color))]
        else if (kind == "window") [*frame_parts(o, o.color), cube([0.0, m(3.0), 0.0], [m(o.w), m(6.0), m(o.d + 2.0)], mat(o.color)),
            cube([0.0, m(o.h / 2.0), 0.0], [m(o.w - 10.0), m(o.h - 10.0), .015], mat(C.GLASS)),
            cube([0.0, m(o.h / 2.0), 0.0], [.04, m(o.h - 10.0), .04], mat(o.color)),
            cube([0.0, -.02, m(o.d / 2.0 + 3.0)], [m(o.w + 8.0), .03, .1], mat("#e7e3db"))]
        else frame_parts(o, o.color);
    <group id: "3d-" ++ o.id, position: r3([m(o.x), m(o.elev), m(o.y)]), rotation: [0.0, 0.0 - o.rot / M.DEG, 0.0], *parts>
}

// ---- floors -----------------------------------------------------------------------------

fn turn(a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
fn in_triangle(p, a, b, c) => turn(a, b, p) > 0 and turn(b, c, p) > 0 and turn(c, a, p) > 0
// Ear clipping: cut off a corner that turns the polygon's way and holds no other
// corner, until a triangle is left. Corners come in the polygon's own order.
fn ears(poly, acc) {
    let k = len(poly)
    if (k < 3) acc
    else if (k == 3) [*acc, poly]
    else (let found = [for (i in 0 to k - 1, let a = poly[(i + k - 1) % k], let b = poly[i], let c = poly[(i + 1) % k]
            where turn(a, b, c) > 0 and not any([for (j, p in poly where j != i and j != (i + 1) % k and j != (i + k - 1) % k) in_triangle(p, a, b, c)])) i],
        let i = if (len(found) == 0) 0 else found[0],
        ears([for (j, p in poly where j != i) p], [*acc, [poly[(i + k - 1) % k], poly[i], poly[(i + 1) % k]]]))
}
fn signed(pts) => sum([for (i, p in pts) (let q = pts[(i + 1) % len(pts)], p[0] * q[1] - q[0] * p[1])])
// triangles of a room's floor, wound to face up
pub fn floor_triangles(pts) => ears(if (signed(pts) > 0) pts else reverse(pts), [])
fn floor_node(r) {
    let tris = floor_triangles(r.pts)
    let positions = [for (t in tris, p in [t[0], t[2], t[1]]) *[m(p[0]), .005, m(p[1])]];
    <mesh id: "3d-" ++ r.id, material: mat(C.floor_of(r.floor).color),
        <geometry type: 'buffer', positions: r3(positions), normals: [for (i in 0 to len(positions) / 3 - 1) *[0.0, 1.0, 0.0]]>>
}

// ---- cameras and lights ------------------------------------------------------------------

// the orbit camera circles a target on the floor; the walker stands at eye height
pub fn orbit_camera(o) => (let cp = math.cos(o.pitch), {position: r3([o.tx + o.dist * cp * math.sin(o.yaw), o.dist * math.sin(o.pitch), o.tz + o.dist * cp * math.cos(o.yaw)]),
    target: r3([o.tx, .6, o.tz])})
pub fn walk_camera(wk) => (let a = wk.yaw / M.DEG, let eye = [m(wk.x), 1.6, m(wk.y)],
    {position: r3(eye), target: r3([eye[0] + math.cos(a) * math.cos(wk.pitch), 1.6 + math.sin(wk.pitch), eye[2] + math.sin(a) * math.cos(wk.pitch)])})
pub fn camera_of(st) => if (st.mode3d == "walk") walk_camera(st.walker) else orbit_camera(st.orbit)
// the orbit target and distance that frame the whole home
pub fn framing(home) => (let b = M.bounds(home), let span = max(b.x1 - b.x0, b.y1 - b.y0),
    {tx: m((b.x0 + b.x1) / 2.0), tz: m((b.y0 + b.y1) / 2.0), dist: max(6.0, m(span) * 1.25)})

// Seven lamps at most can shine (the renderer takes eight lights, and the sun is
// one): the ones nearest where the camera looks.
fn lamp_lights(home, st, look) {
    let lamps = [for (it in M.furniture(home), let l = C.item_of(it.key).light where l != null)
        {at: [m(it.x), m(it.elev + it.h * l.at), m(it.y)], color: l.color, power: l.intensity}]
    let near = sort(lamps, (l) => math.hypot(l.at[0] - look[0], l.at[2] - look[2])) ^ { lamps }
    let gain = if (st.evening) 3.2 else 1.0;
    [for (l in slice(near, 0, min(7, len(near)))) <light type: 'point', color: l.color, intensity: l.power * gain, distance: 7.0, position: r3(l.at)>]
}

// ---- the scene ----------------------------------------------------------------------------

pub fn scene(home, st, width, height) {
    let cut = st.cutaway and st.mode3d != "walk"
    let wall_h = if (cut) min(home.wall_height, 105.0) else home.wall_height
    let glow = st.evening
    let cam = camera_of(st)
    let b = M.bounds(home)
    let ground = cube([m((b.x0 + b.x1) / 2.0), -.03, m((b.y0 + b.y1) / 2.0)], [m(b.x1 - b.x0) + 14.0, .05, m(b.y1 - b.y0) + 14.0],
        mat(if (st.evening) "#3a4a3f" else "#dfe5d8"))
    let body = <group id: "home",
        ground
        for (r in home.rooms) floor_node(r);
        for (w in home.walls) wall_node(home, w, wall_h);
        // cut-away walls stop below the doors and windows, which would stand above them
        for (o in home.items where o.wall != null and not cut) opening_node(o);
        for (it in M.furniture(home) where not cut or it.elev < 100.0) item_node(it, glow)>
    let mats = unique(materials_in(body));
    <scene3d id: "home3d", camera: "cam", width: width, height: height, background: if (st.evening) "#1b2130" else "#e9eef3",
        <camera id: "cam", type: 'perspective', fov: if (st.mode3d == "walk") 70.0 else 45.0, near: .05, far: 200.0,
            position: cam.position, target: cam.target>
        <light type: 'ambient', color: "#ffffff", intensity: if (st.evening) .22 else .62>
        <light type: 'directional', color: if (st.evening) "#8ea2c8" else "#fff4e2", intensity: if (st.evening) .18 else .75,
            position: [3.0, 6.0, 4.0]>
        for (l in lamp_lights(home, st, cam.target)) l;
        <resources *[*SHAPES, *[for (id in mats) material_of(id)]]>
        body>
}

// the camera's position and target as attribute text, for moving it without a re-render
pub fn camera_attrs(st) => (let cam = camera_of(st), {position: vec(cam.position), target: vec(cam.target)})

// ---- picking ---------------------------------------------------------------------------------

// where a ray first meets a box turned `rot` degrees about its upright centre line
// (the slab method in the box's own frame), or null when it misses
fn box_hit(ray, c, half, rot) {
    let a = rot / M.DEG
    let o = [ray.origin[0] - c[0], ray.origin[1] - c[1], ray.origin[2] - c[2]]
    let lo = [o[0] * math.cos(a) + o[2] * math.sin(a), o[1], o[2] * math.cos(a) - o[0] * math.sin(a)]
    let dir = ray.direction
    let ld = [dir[0] * math.cos(a) + dir[2] * math.sin(a), dir[1], dir[2] * math.cos(a) - dir[0] * math.sin(a)]
    let spans = [for (i in 0 to 2) if (abs(ld[i]) < 1e-9) (if (abs(lo[i]) <= half[i]) [-1e9, 1e9] else [1e9, -1e9])
        else (let t1 = (0.0 - half[i] - lo[i]) / ld[i], let t2 = (half[i] - lo[i]) / ld[i], [min(t1, t2), max(t1, t2)])]
    let near = max([for (sp in spans) sp[0]])
    let far = min([for (sp in spans) sp[1]]);
    if (near <= far and far > 0.0) max(near, 0.0) else null
}
// The thing under a click in the 3D view: the nearest piece or wall the ray
// meets, else the room whose floor it reaches.
pub fn pick(home, st, width, height, x, y) {
    let cam = camera_of(st)
    let eye = lens.prepare({position: cam.position, target: cam.target, fov: if (st.mode3d == "walk") 70.0 else 45.0, near: .05, far: 200.0}, width, height)
    let ray = lens.ray(eye, x, y)
    let walls = if (st.cutaway and st.mode3d != "walk") [] else home.walls
    let hits = [*[for (it in M.furniture(home), let t = box_hit(ray, [m(it.x), m(it.elev + it.h / 2.0), m(it.y)], [m(it.w / 2.0), m(it.h / 2.0), m(it.d / 2.0)], it.rot)
            where t != null) {id: it.id, t: t}],
        *[for (w in walls, let t = box_hit(ray, [m((w.x1 + w.x2) / 2.0), m(home.wall_height / 2.0), m((w.y1 + w.y2) / 2.0)],
            [m(M.wall_len(w) / 2.0), m(home.wall_height / 2.0), m(w.thick / 2.0)], M.wall_angle(w)) where t != null) {id: w.id, t: t}]];
    if (len(hits) > 0) hits[argmin([for (h in hits) h.t])].id
    else if (ray.direction[1] >= 0.0) null
    else (let t = (0.0 - ray.origin[1]) / ray.direction[1], let r = M.room_at(home, (ray.origin[0] + ray.direction[0] * t) * 100.0,
        (ray.origin[2] + ray.direction[2] * t) * 100.0), if (r == null) null else r.id)
}
