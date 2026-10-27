// Catalog thumbnails: each piece drawn from its parts as a small axonometric
// picture seen from the front right, its top lit, its front a little darker
// and its right side darker still. Doors and windows are drawn face on.
import C: .mod_catalog
import P: .mod_plan
import A: ~~.greetings.mod_art

let CO = .866
let SI = .5
fn sx(x, y) => (x - y) * CO
fn sy(x, y, z) => (x + y) * SI - z
fn spt(p) => [sx(p[0], p[1]), sy(p[0], p[1], p[2])]
fn poly(ps, fill) => {pts: [for (p in ps) spt(p)], fill: fill}

// a part's faces in draw order, in the piece's centimetres: x across, y toward us, z up
fn faces(p, it) {
    let x0 = (p.u0 - .5) * it.w
    let x1 = (p.u1 - .5) * it.w
    let y0 = (p.v0 - .5) * it.d
    let y1 = (p.v1 - .5) * it.d
    let z0 = p.t0 * it.h
    let z1 = p.t1 * it.h
    let c = P.paint(p.c, it.color)
    if (p.k == "box") [poly([[x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]], A.tint(c, .18)),
        poly([[x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]], c),
        poly([[x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1]], A.shade(c, .2))]
    else (let cx = (x0 + x1) / 2.0, let cy = (y0 + y1) / 2.0, let rx = (x1 - x0) / 2.0, let ry = (y1 - y0) / 2.0,
        let k = if (p.k == "cone") .6 else 1.0,
        let at = (i, z, s) => (let a = 2.0 * math.pi * i / 16.0, [cx + rx * s * math.cos(a), cy + ry * s * math.sin(a), z]),
        if (p.k == "ball") [{ball: true, cx: sx(cx, cy), cy: sy(cx, cy, (z0 + z1) / 2.0), rx: (rx + ry) / 2.0 * 1.12, ry: (z1 - z0) / 2.0 + (rx + ry) * .1, fill: c}]
        // the side facets turned toward us, back to front, then the top
        else [*[for (i in 0 to 15 where math.cos(2.0 * math.pi * (i + .5) / 16.0) + math.sin(2.0 * math.pi * (i + .5) / 16.0) > 0)
                poly([at(i, z0, 1.0), at(i + 1, z0, 1.0), at(i + 1, z1, k), at(i, z1, k)],
                    A.shade(c, .26 - .2 * math.cos(2.0 * math.pi * (i + .5) / 16.0 - .8)))],
            poly([for (i in 0 to 15) at(i, z1, k)], A.tint(c, .18))])
}

fn bounds(shapes) => (let xs = [for (s in shapes) *(if (s.ball) [s.cx - s.rx, s.cx + s.rx] else [for (p in s.pts) p[0]])],
    let ys = [for (s in shapes) *(if (s.ball) [s.cy - s.ry, s.cy + s.ry] else [for (p in s.pts) p[1]])],
    {x0: min(xs), y0: min(ys), x1: max(xs), y1: max(ys)})
fn n(x) => A.n(x)

// nearer parts later: farther back (smaller x + y) first, lower first
pub fn piece_svg(it) {
    let parts = sort(C.item_of(it.key).parts, (p) => ((p.u0 + p.u1) * it.w + (p.v0 + p.v1) * it.d) * 10.0 + p.t0 * it.h) ^ { C.item_of(it.key).parts }
    let shapes = [for (p in parts) *faces(p, it)]
    let b = bounds(shapes)
    let pad = max(b.x1 - b.x0, b.y1 - b.y0) * .06;
    <svg class:"thumb-art", viewBox:n(b.x0 - pad) ++ " " ++ n(b.y0 - pad) ++ " " ++ n(b.x1 - b.x0 + pad * 2.0) ++ " " ++ n(b.y1 - b.y0 + pad * 2.0),
        'aria-hidden':"true",
        for (s in shapes) if (s.ball) <ellipse cx:n(s.cx), cy:n(s.cy), rx:n(s.rx), ry:n(s.ry), fill:s.fill, stroke:A.shade(s.fill, .25), 'stroke-width':".6">
            else <polygon points:join([for (p in s.pts) n(p[0]) ++ "," ++ n(p[1])], " "), fill:s.fill, stroke:A.shade(s.fill, .25),
                'stroke-width':".6", 'stroke-linejoin':"round">>
}

// a door or window face on, in a wall
pub fn opening_svg(it) {
    let kind = C.item_of(it.key).opening
    let w = it.w
    let h = it.h
    let base = it.elev
    let top = 260.0
    let ink = "#4b5563"
    let glass = "#cfe7f2";
    <svg class:"thumb-art", viewBox:n(0.0 - w / 2.0 - 30.0) ++ " " ++ n(-10.0) ++ " " ++ n(w + 60.0) ++ " " ++ n(top + 20.0), 'aria-hidden':"true",
        <rect x:n(0.0 - w / 2.0 - 30.0), y:"0", width:n(w + 60.0), height:n(top), fill:"#ece9e2">
        <rect x:n(0.0 - w / 2.0), y:n(top - base - h), width:n(w), height:n(h),
            fill:if (kind == "window" or kind == "slider") glass else if (kind == "arch") "#d9d5cc" else it.color, stroke:ink, 'stroke-width':"4">
        if (kind == "double" or kind == "slider") <line x1:"0", y1:n(top - base - h), x2:"0", y2:n(top - base), stroke:ink, 'stroke-width':"4">
        if (kind == "window") <line x1:"0", y1:n(top - base - h), x2:"0", y2:n(top - base), stroke:ink, 'stroke-width':"3">
        if (kind == "door") <circle cx:n(w / 2.0 - 14.0), cy:n(top - h * .5), r:"5", fill:"#b8bec6">
        if (kind == "double") for (x in [-12.0, 12.0]) <circle cx:n(x), cy:n(top - h * .5), r:"5", fill:"#b8bec6">>
}

let THUMBS = map([for (it in C.ITEMS) (it.key, if (it.opening != null) opening_svg(it) else piece_svg(it))])
pub fn thumb(key) => THUMBS[key]
