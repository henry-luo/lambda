// The floor plan as SVG in plan centimetres: a grid, room floors with their
// names and areas, walls with their doors and windows cut out, and furniture
// seen from above (each piece's 3D parts, highest last). Everything that can
// be picked carries data-id, and its handles carry data-handle.
import C: .mod_catalog
import M: .mod_model
import A: ~~.greetings.mod_art

pub let ACCENT = "#2f7de1"
let WALL_INK = "#3b4048"
let PAPER = "#f7f6f2"
fn n(x) => A.n(x)
fn pt(x, y) => n(x) ++ "," ++ n(y)
fn pts(ps) => join([for (p in ps) pt(p[0], p[1])], " ")
fn place(x, y, rot) => "translate(" ++ n(x) ++ " " ++ n(y) ++ ") rotate(" ++ n(rot) ++ ")"

// a role colour of a part, for its 3D material and its plan symbol alike
pub fn paint(c, color) => if (c == "main") color else if (c == "tint") A.tint(color, .32) else if (c == "shade") A.shade(color, .22) else c

// the plan window: a centre and pixels per centimetre, seen through a pane
pub fn frame(v, pw, ph) => {x0: v.cx - pw / 2.0 / v.zoom, y0: v.cy - ph / 2.0 / v.zoom, w: pw / v.zoom, h: ph / v.zoom}

// a stretch of wall from s0 to s1 along it, as the four corners of its thickness
pub fn wall_quad(w, s0, s1, thick) => (let u = M.wall_dir(w), let nx = 0.0 - u.y * thick / 2.0, let ny = u.x * thick / 2.0,
    let ax = w.x1 + u.x * s0, let ay = w.y1 + u.y * s0, let bx = w.x1 + u.x * s1, let by = w.y1 + u.y * s1,
    [[ax + nx, ay + ny], [bx + nx, by + ny], [bx - nx, by - ny], [ax - nx, ay - ny]])

// ---- grid ------------------------------------------------------------------------

fn grid(f, z) {
    let step = if (z * 50.0 >= 14.0) 50.0 else 100.0
    let gx = floor(f.x0 / step) * step
    let gy = floor(f.y0 / step) * step
    let major = (v) => abs(v / 100.0 - round(v / 100.0)) < .01;
    <g class:"grid", 'pointer-events':"none",
        for (i in 0 to int(f.w / step) + 1, let x = gx + i * step)
            <line x1:n(x), y1:n(f.y0), x2:n(x), y2:n(f.y0 + f.h), stroke:if (major(x)) "#dcd9d0" else "#e9e6df", 'stroke-width':n(1.0 / z)>
        for (i in 0 to int(f.h / step) + 1, let y = gy + i * step)
            <line x1:n(f.x0), y1:n(y), x2:n(f.x0 + f.w), y2:n(y), stroke:if (major(y)) "#dcd9d0" else "#e9e6df", 'stroke-width':n(1.0 / z)>>
}

// ---- rooms -------------------------------------------------------------------------

// a room's name and area, each over a white halo so it reads above the furniture
fn haloed(x, y, size, weight, ink, words, z) => [
    <text x:n(x), y:n(y), 'text-anchor':"middle", 'font-size':n(size), 'font-weight':weight, fill:"#ffffff", stroke:"#ffffff",
        'stroke-width':n(3.0 / z), 'stroke-linejoin':"round", 'stroke-opacity':".85", words>,
    <text x:n(x), y:n(y), 'text-anchor':"middle", 'font-size':n(size), 'font-weight':weight, fill:ink, words>]
fn room_label(r, z) => (let c = M.centroid(r.pts), let s = 12.0 / z,
    <g class:"room-label", 'pointer-events':"none",
        for (t in [*haloed(c[0], c[1] - s * .2, s, "600", "#3d3a35", r.name, z),
            *haloed(c[0], c[1] + s * 1.1, s * .85, "400", "#6f6a61", A.n(M.area(r.pts) / 10000.0) ++ " m²", z)]) t>)
fn room_shape(r, sel, z) => (let fl = C.floor_of(r.floor),
    <polygon class:"room", 'data-id':r.id, points:pts(r.pts), fill:A.tint(fl.color, .35),
        stroke:if (sel == r.id) ACCENT else A.shade(fl.color, .1), 'stroke-width':n((if (sel == r.id) 3.0 else 1.0) / z),
        'stroke-dasharray':if (sel == r.id) n(6.0 / z) ++ " " ++ n(4.0 / z) else "none">)

// ---- walls, doors and windows ---------------------------------------------------------

fn upright(a) => if (a > 90.0) a - 180.0 else if (a <= -90.0) a + 180.0 else a
fn wall_label(w, z) => (let u = M.wall_dir(w), let off = w.thick / 2.0 + 11.0 / z, let mx = (w.x1 + w.x2) / 2.0, let my = (w.y1 + w.y2) / 2.0,
    let x = mx + u.y * off, let y = my - u.x * off,
    <text class:"dim", x:"0", y:"0", transform:place(x, y, upright(M.wall_angle(w))), 'text-anchor':"middle", 'dominant-baseline':"middle",
        'font-size':n(10.5 / z), fill:"#5b6470", 'pointer-events':"none", A.n(M.wall_len(w) / 100.0) ++ " m">)
fn wall_shape(home, w, sel, z, dims) => <g class:"wall", 'data-id':w.id,
    for (s in M.solid_spans(home, w)) <polygon points:pts(wall_quad(w, s[0], s[1], w.thick)),
        fill:if (sel == w.id) ACCENT else WALL_INK>;
    if (dims or sel == w.id) wall_label(w, z);
    if (sel == w.id) for (e in [["a", w.x1, w.y1], ["b", w.x2, w.y2]])
        <circle class:"handle", 'data-handle':e[0], cx:n(e[1]), cy:n(e[2]), r:n(6.0 / z), fill:"#ffffff", stroke:ACCENT, 'stroke-width':n(2.0 / z)>>

// A door or window in its wall's frame: x along the wall, y across it. A door
// swings to the side `flip` picks, hinged at the end `mirror` picks.
fn leaf(x0, w, t, side, hinge, ink, z) => (let hx = x0 + (if (hinge > 0) 0.0 else w), let ex = x0 + (if (hinge > 0) w else 0.0),
    let y0 = side * t / 2.0, let y1 = side * (t / 2.0 + w),
    [<line x1:n(hx), y1:n(y0), x2:n(hx), y2:n(y1), stroke:ink, 'stroke-width':n(2.0 / z)>,
     <path d:"M" ++ pt(hx, y1) ++ " A" ++ n(w) ++ " " ++ n(w) ++ " 0 0 " ++ (if (side * hinge > 0) "0 " else "1 ") ++ pt(ex, y0),
        fill:"none", stroke:ink, 'stroke-width':n(1.0 / z), 'stroke-dasharray':n(4.0 / z) ++ " " ++ n(3.0 / z)>])
fn opening_shape(it, sel, z) {
    let kind = C.item_of(it.key).opening
    let ink = if (sel == it.id) ACCENT else "#4b5563"
    let w = it.w
    let t = it.d
    let side = if (it.flip) -1.0 else 1.0
    let hinge = if (it.mirror == true) -1.0 else 1.0;
    <g class:"opening", 'data-id':it.id, transform:place(it.x, it.y, it.rot),
        <rect x:n(0.0 - w / 2.0), y:n(0.0 - t / 2.0), width:n(w), height:n(t), fill:if (kind == "window" or kind == "slider") "#e4f1f7" else PAPER,
            stroke:ink, 'stroke-width':n(1.0 / z)>;
        if (kind == "door") for (e in leaf(0.0 - w / 2.0, w, t, side, hinge, ink, z)) e
        else if (kind == "double") for (e in [*leaf(0.0 - w / 2.0, w / 2.0, t, side, 1.0, ink, z), *leaf(0.0, w / 2.0, t, side, -1.0, ink, z)]) e
        else if (kind == "slider") for (k in [-1.0, 1.0])
            <rect x:n(if (k < 0) 0.0 - w / 2.0 else -2.0), y:n(k * t / 6.0 - t / 8.0), width:n(w / 2.0 + 2.0), height:n(t / 4.0), fill:"#cfe7f2", stroke:ink, 'stroke-width':n(1.0 / z)>
        else if (kind == "window") <line x1:n(0.0 - w / 2.0), y1:"0", x2:n(w / 2.0), y2:"0", stroke:ink, 'stroke-width':n(1.5 / z)>
        else <line x1:n(0.0 - w / 2.0), y1:"0", x2:n(w / 2.0), y2:"0", stroke:ink, 'stroke-width':n(1.0 / z), 'stroke-dasharray':n(3.0 / z) ++ " " ++ n(3.0 / z)>;
        if (sel == it.id) <rect x:n(0.0 - w / 2.0 - 4.0 / z), y:n(0.0 - t / 2.0 - 4.0 / z), width:n(w + 8.0 / z), height:n(t + 8.0 / z),
            fill:"none", stroke:ACCENT, 'stroke-width':n(2.0 / z)>>
}

// ---- furniture -------------------------------------------------------------------------

fn part_shape(p, it, z) {
    let x = (p.u0 - .5) * it.w
    let y = (p.v0 - .5) * it.d
    let w = max(.5, (p.u1 - p.u0) * it.w)
    let h = max(.5, (p.v1 - p.v0) * it.d)
    let fill = paint(p.c, it.color)
    let edge = A.shade(fill, .28)
    if (p.k == "box") <rect x:n(x), y:n(y), width:n(w), height:n(h), rx:n(min(w, h) * .08), fill:A.tint(fill, .08), stroke:edge, 'stroke-width':n(.8 / z),
        'fill-opacity':if (fill == C.GLASS) ".45" else "1">
    else <ellipse cx:n(x + w / 2.0), cy:n(y + h / 2.0), rx:n(w / 2.0), ry:n(h / 2.0), fill:A.tint(fill, .08), stroke:edge, 'stroke-width':n(.8 / z)>
}
// the selection frame moves with the piece: a rotate knob ahead of its back and a resize corner
fn selection(it, z) => <g class:"selection",
    <rect x:n(0.0 - it.w / 2.0 - 4.0 / z), y:n(0.0 - it.d / 2.0 - 4.0 / z), width:n(it.w + 8.0 / z), height:n(it.d + 8.0 / z), fill:"none",
        stroke:ACCENT, 'stroke-width':n(1.5 / z), 'stroke-dasharray':n(5.0 / z) ++ " " ++ n(3.0 / z)>
    <line x1:"0", y1:n(0.0 - it.d / 2.0 - 4.0 / z), x2:"0", y2:n(0.0 - it.d / 2.0 - 22.0 / z), stroke:ACCENT, 'stroke-width':n(1.5 / z)>
    <circle class:"handle", 'data-handle':"rotate", cx:"0", cy:n(0.0 - it.d / 2.0 - 26.0 / z), r:n(6.0 / z), fill:"#ffffff", stroke:ACCENT, 'stroke-width':n(2.0 / z)>
    <rect class:"handle", 'data-handle':"size", x:n(it.w / 2.0 - 1.0 / z), y:n(it.d / 2.0 - 1.0 / z), width:n(10.0 / z), height:n(10.0 / z),
        fill:"#ffffff", stroke:ACCENT, 'stroke-width':n(2.0 / z)>>
// Pieces up by the ceiling (lights, high shelves) show faintly over what stands
// below them, and only their centre dot takes a click, so they never cover it.
pub fn overhead(it) => it.elev >= 180.0
pub fn item_shape(it, sel, z) => (let parts = sort(C.item_of(it.key).parts, (p) => p.t1) ^ { [] }, let high = overhead(it),
    <g class:"item", 'data-id':it.id, transform:place(it.x, it.y, it.rot), opacity:if (high) ".45" else "1",
        if (high) <circle cx:"0", cy:"0", r:n(7.0 / z), fill:"#ffffff", 'fill-opacity':"0.01">
        else <rect x:n(0.0 - it.w / 2.0), y:n(0.0 - it.d / 2.0), width:n(it.w), height:n(it.d), fill:"#ffffff", 'fill-opacity':"0.01">;
        <g 'pointer-events':if (high) "none" else "auto", for (p in parts) part_shape(p, it, z)>
        if (high) <circle cx:"0", cy:"0", r:n(3.0 / z), fill:"#4b5563", 'pointer-events':"none">;
        if (sel == it.id) selection(it, z)>)

// ---- the walker of the 3D walk-through ------------------------------------------------------

fn walker(wk, z) => <g id:"walker", class:"walker", 'data-handle':"walker", transform:place(wk.x, wk.y, wk.yaw),
    <path d:"M0,0 L" ++ pt(70.0, -38.0) ++ " A80 80 0 0 1 " ++ pt(70.0, 38.0) ++ " Z", fill:ACCENT, 'fill-opacity':".18", stroke:"none">
    <circle cx:"0", cy:"0", r:n(9.0 / z + 6.0), fill:ACCENT, stroke:"#ffffff", 'stroke-width':n(2.0 / z)>>

// ---- the whole plan -----------------------------------------------------------------------

// rooms, walls and openings: the layer a wall drag redraws on every move
pub fn structure(home, sel, z, dims) => <g id:"plan-structure",
    <g class:"rooms", for (r in home.rooms) room_shape(r, sel, z)>
    <g class:"walls", for (w in home.walls) wall_shape(home, w, sel, z, dims)>
    <g class:"openings", for (it in home.items where it.wall != null) opening_shape(it, sel, z)>>

// flat pieces first, so a rug never covers the sofa standing on it
pub fn labels(home, z) => <g class:"room-labels", for (r in home.rooms) room_label(r, z)>
pub fn furniture(home, sel, z) => <g id:"plan-furniture",
    for (it in (sort(M.furniture(home), (it) => it.elev * 1000.0 + it.h) ^ { M.furniture(home) })) item_shape(it, sel, z)>

pub fn plan(home, st, pw, ph) {
    let v = st.view2d
    let z = v.zoom
    let f = frame(v, pw, ph);
    <svg id:"plan", class:"plan-svg tool-" ++ st.tool, width:n(pw), height:n(ph), viewBox:n(f.x0) ++ " " ++ n(f.y0) ++ " " ++ n(f.w) ++ " " ++ n(f.h),
        <rect class:"paper", x:n(f.x0), y:n(f.y0), width:n(f.w), height:n(f.h), fill:PAPER>;
        (grid(f, z));
        (structure(home, st.sel, z, st.dims));
        (furniture(home, st.sel, z));
        (labels(home, z));
        if (st.mode3d == "walk") walker(st.walker, z);
        <line id:"draft-wall", x1:"0", y1:"0", x2:"0", y2:"0", stroke:ACCENT, 'stroke-width':"15", 'stroke-opacity':".55", visibility:"hidden",
            'pointer-events':"none">
        <rect id:"draft-room", x:"0", y:"0", width:"1", height:"1", fill:ACCENT, 'fill-opacity':".12", stroke:ACCENT,
            'stroke-width':n(2.0 / z), visibility:"hidden", 'pointer-events':"none">
        <text id:"draft-label", x:"0", y:"0", 'font-size':n(12.0 / z), fill:ACCENT, 'font-weight':"600", visibility:"hidden",
            'pointer-events':"none", "">>
}

// The plan on its own, for the exports and the gallery: labels and lines are
// sized as on a sheet `span` pixels wide, so an export keeps their weight at any
// resolution. Without `notes` it is the bare plan, for small pictures.
pub fn sheet(home, w, h, span = null, notes = true) {
    let b = M.bounds(home)
    let pad = 120.0
    let bw = b.x1 - b.x0 + pad * 2.0
    let bh = b.y1 - b.y0 + pad * 2.0
    let z = min(w / bw, h / bh) * (if (span == null) 1.0 else span / w);
    <svg xmlns:"http://www.w3.org/2000/svg", width:n(w), height:n(h),
        viewBox:n(b.x0 - pad) ++ " " ++ n(b.y0 - pad) ++ " " ++ n(bw) ++ " " ++ n(bh),
        <rect x:n(b.x0 - pad), y:n(b.y0 - pad), width:n(bw), height:n(bh), fill:"#ffffff">;
        (structure(home, null, z, notes));
        (furniture(home, null, z));
        if (notes) labels(home, z);
        if (notes) <text x:n(b.x0 - pad + 20.0), y:n(b.y0 - pad + 40.0), 'font-size':n(16.0 / z), 'font-weight':"700", fill:"#2b2f36", home.title>;
        if (notes) <text x:n(b.x0 - pad + 20.0), y:n(b.y0 - pad + 40.0 + 20.0 / z), 'font-size':n(11.0 / z), fill:"#6b7280",
            A.n(M.floor_area(home)) ++ " m² · " ++ string(len(home.rooms)) ++ " rooms · " ++ string(len(M.furniture(home))) ++ " pieces">>
}
