// The wing atlas: sixteen butterfly species painted as SVG in a 4 × 4 grid of 512-pixel cells, each a
// top view with the head up. Lambda describes the patterns; Radiant rasterizes the SVG into a mipmapped
// texture once (SH4). A wing is drawn in a 256-unit cell space, body at the origin, head toward -y.

pub let CELL = 512.0
pub let GRID = 4
let FIT = 2.0 * 0.86   // 256-unit drawing space to the 512-pixel cell, with a margin for antennae and tails

fn f(x) string => string(round(x * 100.0) / 100.0)
fn pt(p) string => f(p[0]) ++ " " ++ f(p[1])
fn lerp2(a, b, t) array => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]
// a deterministic pseudo-random number in [0, 1) for speckles, the same on both mirrored sides
fn hash(n) float {
    let x = sin(n * 12.9898 + 78.233) * 43758.5453;
    x - floor(x)
}

// cubic Bézier segments [start, control, control, end] sampled at n + 1 points
fn bez(s, t) array {
    let u = 1.0 - t
    let a = u * u * u, b = 3.0 * u * u * t, c = 3.0 * u * t * t, d = t * t * t;
    [a * s[0][0] + b * s[1][0] + c * s[2][0] + d * s[3][0], a * s[0][1] + b * s[1][1] + c * s[2][1] + d * s[3][1]]
}
fn sample(s, n) array => [for (k in 0 to n) bez(s, k * 1.0 / n)]

// a right-side wing: leading edge, outer margin and inner edge, with the rim its border runs along
fn wing(fore, sh) map {
    let fw = sh.fw or 1.0, fh = sh.fh or 1.0, hw = sh.hw or 1.0, hh = sh.hh or 1.0
    let segs = if (fore) [
        [[3.0, -6.0], [28.0, -60.0 * fh], [80.0 * fw, -108.0 * fh], [116.0 * fw, -98.0 * fh]],
        [[116.0 * fw, -98.0 * fh], [124.0 * fw, -68.0 * fh], [112.0 * fw, -28.0 * fh], [84.0 * fw, -4.0]],
        [[84.0 * fw, -4.0], [56.0, 8.0], [26.0, 8.0], [3.0, 4.0]]
    ] else [
        [[3.0, 0.0], [40.0, -10.0], [98.0 * hw, 2.0], [96.0 * hw, 38.0 * hh]],
        [[96.0 * hw, 38.0 * hh], [94.0 * hw, 82.0 * hh], [60.0 * hw, 112.0 * hh], [28.0, 100.0 * hh]],
        [[28.0, 100.0 * hh], [14.0, 90.0 * hh], [5.0, 50.0 * hh], [3.0, 20.0]]
    ]
    let lead = sample(segs[0], 24), edge = sample(segs[1], 24), inner = sample(segs[2], 24)
    let d = "M " ++ pt(segs[0][0]) ++ join([for (s in segs) " C " ++ pt(s[1]) ++ " " ++ pt(s[2]) ++ " " ++ pt(s[3])], "") ++ " Z";
    {fore: fore, d: d, lead: lead, edge: edge, inner: inner,
     outer: if (fore) [*slice(lead, 14, 25), *edge] else [*slice(lead, 18, 25), *edge, *slice(inner, 0, 10)],
     cen: if (fore) [62.0 * fw, -50.0 * fh] else [52.0 * hw, 50.0 * hh],
     root: if (fore) [4.0, -3.0] else [4.0, 8.0]}
}
fn inset(w, p, d) array {
    let dx = w.cen[0] - p[0], dy = w.cen[1] - p[1]
    let l = max(sqrt(dx * dx + dy * dy), 0.0001);
    [p[0] + dx / l * d, p[1] + dy / l * d]
}
// the outer rim between two fractions of its length
fn rim(w, from, to) array {
    let n = len(w.outer) - 1;
    slice(w.outer, int(floor(from * n)), int(floor(to * n + 0.999)) + 1)
}

// --- marks: SVG elements in wing space, drawn inside the wing's clip ---
fn dot(p, r, fill, alpha = 1.0) element => <circle cx: f(p[0]), cy: f(p[1]), r: f(r), fill: fill, 'fill-opacity': f(alpha)>
fn blob(p, rx, ry, turn, fill) element =>
    <ellipse cx: f(p[0]), cy: f(p[1]), rx: f(rx), ry: f(ry), fill: fill, transform: "rotate(" ++ f(turn * 57.2958) ++ " " ++ pt(p) ++ ")">
fn line(points, width, stroke, alpha = 1.0) element => <polyline points: join([for (p in points) pt(p)], " "), fill: "none",
    stroke: stroke, 'stroke-width': f(width), 'stroke-opacity': f(alpha), 'stroke-linecap': "round", 'stroke-linejoin': "round">
// a border band measured inward from the rim: the clip hides the outer half of the stroke
fn margin(w, width, color, from = 0.0, to = 1.0) element => line(rim(w, from, to), width * 2.0, color)
fn inset_line(w, d, width, color, from = 0.0, to = 1.0, alpha = 1.0) element =>
    line([for (p in rim(w, from, to)) inset(w, p, d)], width, color, alpha)
fn dots_along(w, d, r, color, every = 2, from = 0.0, to = 1.0, shift = 0) array {
    let n = len(w.outer) - 1;
    [for (k in (int(floor(from * n)) + shift) to int(floor(to * n)) where (k - int(floor(from * n)) - shift) % every == 0)
        dot(inset(w, w.outer[k], d), r, color)]
}
fn band(w, a, b, width, color, bend = 0.25, alpha = 1.0) element {
    let m = lerp2(lerp2(a, b, 0.5), w.cen, bend);
    <path d: "M " ++ pt(a) ++ " Q " ++ pt(m) ++ " " ++ pt(b), fill: "none", stroke: color, 'stroke-width': f(width),
        'stroke-opacity': f(alpha), 'stroke-linecap': "round">
}
fn veins(w, color, width, alpha = 1.0) array {
    let ends = if (w.fore) [w.lead[16], w.lead[20], *[for (k in 0 to 24 where k % 4 == 0) w.edge[k]]]
        else [*[for (k in 0 to 24 where k % 4 == 0) w.edge[k]], w.inner[6]]
    let cell = lerp2(w.root, w.cen, 0.75);
    [*[for (e in ends) band(w, w.root, e, width, color, 0.18, alpha)],
     // the closed discal cell, the most recognisable vein feature
     <ellipse cx: f(cell[0] * 0.75), cy: f(cell[1] * 0.75), rx: "26", ry: "9", fill: "none", stroke: color,
        'stroke-width': f(width), 'stroke-opacity': f(alpha), transform: "rotate(" ++ f(math.atan2(cell[1], cell[0]) * 57.2958) ++ " " ++
        f(cell[0] * 0.75) ++ " " ++ f(cell[1] * 0.75) ++ ")">]
}
fn eyespot(p, r, rings) array => [*[for (ring in rings) dot(p, r * ring[0], ring[1])],
    dot([p[0] - r * 0.18, p[1] - r * 0.18], r * 0.12, "#ffffff", 0.85)]
fn speckle(n, color, alpha, seed, rmax = 1.6) array =>
    [for (k in 1 to n) dot([hash(seed + k) * 140.0 - 5.0, hash(seed + k + 0.31) * 260.0 - 130.0], hash(seed + k + 0.67) * rmax + 0.4, color, alpha)]
fn chevrons(w, d, size, color, every = 3) array => [for (k in 1 to (len(w.outer) - 2) where (k - 1) % every == 0) {
    let p = inset(w, w.outer[k], d), q = w.outer[k + 1], o = w.outer[k - 1]
    let tx = q[0] - o[0], ty = q[1] - o[1]
    let tl = max(sqrt(tx * tx + ty * ty), 0.0001)
    let ux = tx / tl * size, uy = ty / tl * size, tip = inset(w, p, size * 0.9);
    line([[p[0] - ux, p[1] - uy], tip, [p[0] + ux, p[1] + uy]], 2.4, color)
}]
// a radial gradient filling the wing, centred on its root unless `c` says otherwise; `stops` are
// [offset, color] or [offset, color, opacity]
fn radial(key, stops, c, r = 130.0) array => [
    <defs <radialGradient id: key, gradientUnits: "userSpaceOnUse", cx: f(c[0]), cy: f(c[1]), r: f(r),
        *[for (s in stops) <stop offset: f(s[0]), 'stop-color': s[1], 'stop-opacity': f(if (len(s) > 2) s[2] else 1.0)>]>>,
    <rect x: "-20", y: "-150", width: "190", height: "300", fill: "url(#" ++ key ++ ")">]
fn fill(color) element => <rect x: "-20", y: "-150", width: "190", height: "300", fill: color>

// --- species: a painter per wing returns its marks; weights favour the reel's maroon, cream and brown ---
fn mourning(w, key, seed) array => [*radial(key, [[0.0, "#240b0e"], [0.3, "#56171f"], [1.0, "#702634"]], w.root),
    *speckle(90, "#2a0a0e", 0.35, seed), *veins(w, "#2b0d12", 1.6, 0.5), inset_line(w, 15.0, 15.0, "#170a0a"),
    *dots_along(w, 15.0, 3.4, "#7aa8ec", 2, 0.05, 0.95), margin(w, 9.0, "#efd99a"),
    *(if (w.fore) [for (k in [16, 20]) band(w, w.lead[k], inset(w, w.lead[k], 14.0), 4.0, "#efd99a", 0.0)] else [])]
fn peacock(w, key, seed) array => [*radial(key, [[0.0, "#3f120f"], [0.3, "#a5281f"], [1.0, "#c73b2b"]], w.root),
    *speckle(80, "#5a120e", 0.35, seed), margin(w, 11.0, "#3a2a22"),
    *(if (w.fore) [*[for (k in [8, 11, 14, 17, 20]) band(w, w.lead[k], inset(w, w.lead[k], 10.0), 5.0, if (k % 2 == 1) "#e6d9a6" else "#1d1410", 0.0)],
        *eyespot(inset(w, w.edge[3], 30.0), 24.0, [[1.0, "#e9dca4"], [0.86, "#26324f"], [0.72, "#4a6ab7"], [0.5, "#d8c34a"], [0.34, "#1b1210"], [0.18, "#7d95cf"]])]
      else [*eyespot(inset(w, w.edge[8], 32.0), 27.0, [[1.0, "#3a2a22"], [0.9, "#0f0c0c"], [0.62, "#4f6fae"], [0.42, "#0f0c0c"]]),
        *[for (k in 1 to 7) dot([inset(w, w.edge[8], 32.0)[0] + (hash(seed + k * 3) - 0.5) * 30.0, inset(w, w.edge[8], 32.0)[1] + (hash(seed + k * 5) - 0.5) * 30.0], 1.6, "#9fb4dc")]])]
fn admiral(w, key, seed) array => [*radial(key, [[0.0, "#120c0a"], [0.6, "#231814"], [1.0, "#2c1f19"]], w.root),
    *(if (w.fore) [band(w, w.lead[13], w.edge[20], 19.0, "#e2452a", 0.2),
        *[for (s in [[1, 12.0, 4.5], [3, 18.0, 3.8], [5, 12.0, 3.2], [2, 28.0, 5.0], [7, 22.0, 3.0]]) dot(inset(w, w.edge[s[0]], s[1]), s[2], "#f5f1e6")],
        dot(inset(w, w.edge[9], 34.0), 6.0, "#7ea0d8")]
      else [inset_line(w, 12.0, 13.0, "#e2452a", 0.1, 0.9), *dots_along(w, 12.0, 2.4, "#140c0a", 3, 0.15, 0.85),
        dot(inset(w, w.edge[22], 9.0), 4.0, "#6f93d6")]),
    margin(w, 3.0, "#0c0806")]
fn cream_moth(w, key, seed) array => [*radial(key, [[0.0, "#a98d64"], [0.28, "#e8d8b6"], [1.0, "#f4e9d2"]], w.root),
    *speckle(110, "#9a7b52", 0.18, seed),
    *[for (t in [0.35, 0.62]) band(w, lerp2(w.root, w.lead[24], t + 0.15), inset(w, w.edge[int(round(t * 24.0))], 30.0 - t * 20.0), 2.6, "#86663f", 0.35, 0.8)],
    dot(lerp2(w.root, w.cen, 0.8), 4.5, "#5c4128"), inset_line(w, 9.0, 1.8, "#86663f", 0.0, 1.0, 0.7),
    *dots_along(w, 4.0, 1.6, "#5c4128", 3), margin(w, 3.0, "#c9b48c")]
fn cabbage(w, key, seed) array => [*radial(key, [[0.0, "#8f9a86"], [0.16, "#eeeee2"], [1.0, "#fbfaf3"]], w.root),
    *veins(w, "#b8b8a8", 1.2, 0.45),
    *(if (w.fore) [margin(w, 18.0, "#3c3c3a", 0.42, 0.82), dot(lerp2(w.root, w.cen, 1.05), 6.0, "#2c2c2a"), dot(inset(w, w.edge[18], 22.0), 5.0, "#2c2c2a")]
      else [dot(inset(w, w.lead[22], 8.0), 5.0, "#3c3c3a")])]
fn owl(w, key, seed) array => [*radial(key, [[0.0, "#2f1e12"], [0.4, "#7b583c"], [1.0, "#a98563"]], w.root),
    *speckle(120, "#3a2416", 0.3, seed, 1.2), *speckle(60, "#e8d6b4", 0.2, seed + 500, 1.0),
    *(if (w.fore) [band(w, w.lead[15], w.edge[17], 13.0, "#e9d7ad", 0.15), *[for (d in [8.0, 16.0]) inset_line(w, d, 2.0, "#3a2416", 0.0, 1.0, 0.6)]]
      else [*eyespot(lerp2(w.cen, w.edge[12], 0.35), 31.0, [[1.0, "#f2d274"], [0.84, "#16100b"], [0.6, "#33241a"], [0.36, "#16100b"]]),
        inset_line(w, 10.0, 3.0, "#e9d7ad", 0.0, 1.0, 0.6)]),
    margin(w, 6.0, "#4b3322")]
fn buckeye(w, key, seed) array => [*radial(key, [[0.0, "#35251a"], [0.4, "#76573e"], [1.0, "#8d6a4b"]], w.root),
    *speckle(80, "#2e2016", 0.25, seed),
    *(if (w.fore) [band(w, w.lead[19], w.edge[7], 13.0, "#efe6d2", 0.1),
        *[for (k in [9, 13]) band(w, w.lead[k], inset(w, w.lead[k], 18.0), 6.0, "#e0782c", 0.0)],
        *eyespot(inset(w, w.edge[14], 22.0), 13.0, [[1.0, "#f3d27a"], [0.74, "#1b1b1b"], [0.5, "#6c7fc2"], [0.24, "#1b1b1b"]])]
      else [*eyespot(inset(w, w.edge[5], 24.0), 16.0, [[1.0, "#f3d27a"], [0.74, "#1b1b1b"], [0.5, "#a24a8c"], [0.24, "#1b1b1b"]]),
        *eyespot(inset(w, w.edge[15], 22.0), 10.0, [[1.0, "#f3d27a"], [0.7, "#1b1b1b"], [0.4, "#6c7fc2"]]),
        inset_line(w, 7.0, 6.0, "#e0782c", 0.1, 1.0)]),
    margin(w, 3.0, "#3a2a1d")]
fn morpho(w, key, seed) array => [fill("#0a0d16"),
    *radial(key, [[0.0, "#c6f8ff"], [0.22, "#45c8f4"], [0.55, "#1666cb"], [0.82, "#0b2c6e"], [1.0, "#0a1428", 0.0]], w.root, 112.0),
    band(w, w.root, w.cen, 16.0, "#ffffff", 0.05, 0.18), margin(w, 15.0, "#06080e"),
    *(if (w.fore) dots_along(w, 9.0, 2.6, "#f2f6ff", 3, 0.3, 0.8) else dots_along(w, 8.0, 2.2, "#c9e8ff", 3, 0.1, 0.8))]
fn ulysses(w, key, seed) array => [fill("#0c0e12"),
    *radial(key, [[0.0, "#9ff9ef"], [0.4, "#22b9c9"], [0.75, "#126e82", 0.9], [1.0, "#0a141e", 0.0]], lerp2(w.root, w.cen, 0.45), 70.0),
    margin(w, 18.0, "#08090c"), *(if (w.fore) [] else dots_along(w, 8.0, 2.4, "#4fd2dd", 3, 0.1, 0.9))]
fn monarch(w, key, seed) array => [*radial(key, [[0.0, "#c24c0b"], [0.35, "#f39a2a"], [1.0, "#f7b046"]], w.root),
    *veins(w, "#120c08", 4.5), margin(w, 15.0, "#120c08"),
    *(if (w.fore) [blob(inset(w, w.edge[1], 14.0), 22.0, 16.0, -0.6, "#120c08"),
        *[for (s in [[2, 22.0], [4, 26.0], [6, 22.0]]) dot(inset(w, w.edge[s[0]], s[1]), 3.4, "#f7b046")]] else []),
    *dots_along(w, 6.0, 2.4, "#ffffff", 2), *dots_along(w, 12.0, 1.9, "#ffffff", 2, 0.0, 1.0, 1)]
fn painted_lady(w, key, seed) array => [*radial(key, [[0.0, "#6e4330"], [0.3, "#e58b55"], [1.0, "#f1a676"]], w.root),
    *speckle(50, "#6e4330", 0.2, seed),
    *(if (w.fore) [blob(inset(w, w.edge[2], 18.0), 30.0, 22.0, -0.7, "#1f1612"),
        *[for (s in [[1, 10.0, 3.6], [3, 14.0, 3.0], [2, 24.0, 4.6]]) dot(inset(w, w.edge[s[0]], s[1]), s[2], "#f6efe3")],
        *[for (s in [[12, 30.0, 9.0, 6.0], [18, 18.0, 7.0, 5.0], [9, 46.0, 8.0, 5.0]]) blob(inset(w, w.edge[s[0]], s[1]), s[2], s[3], 0.5, "#1f1612")]]
      else [*dots_along(w, 18.0, 4.2, "#1f1612", 3, 0.15, 0.85), *dots_along(w, 18.0, 1.6, "#7ea6e0", 3, 0.15, 0.85),
        inset_line(w, 7.0, 2.0, "#1f1612", 0.0, 1.0, 0.8)]),
    margin(w, 4.0, "#2b1d17"), *dots_along(w, 3.0, 1.4, "#f6efe3", 3)]
fn swallowtail(w, key, seed) array => [*radial(key, [[0.0, "#2a2412"], [0.15, "#f4d65d"], [1.0, "#f9e27a"]], w.root),
    *veins(w, "#141414", 1.6, 0.8),
    *(if (w.fore) [for (s in [[6, 26.0, 9.0], [11, 44.0, 12.0], [16, 56.0, 10.0], [20, 34.0, 7.0]])
        band(w, w.lead[s[0]], inset(w, w.lead[s[0]], s[1]), s[2], "#141414", 0.0)] else []),
    margin(w, 22.0, "#141414"), *dots_along(w, 9.0, 3.6, "#f9e27a", 2, 0.1, 0.95),
    *(if (w.fore) [] else [*dots_along(w, 20.0, 3.2, "#3f6fd8", 3, 0.15, 0.8),
        *eyespot(inset(w, w.edge[22], 12.0), 8.0, [[1.0, "#e8742a"], [0.55, "#141414"]])])]
fn zebra(w, key, seed) array => [fill("#111111"),
    *(if (w.fore) [band(w, w.lead[5], w.edge[22], 9.0, "#f3df4b", 0.2), band(w, w.lead[15], inset(w, w.edge[12], 16.0), 8.0, "#f3df4b", 0.2),
        *dots_along(w, 10.0, 2.6, "#f3df4b", 3, 0.3, 0.7)]
      else [band(w, lerp2(w.root, w.lead[24], 0.25), inset(w, w.edge[4], 14.0), 9.0, "#f3df4b", 0.1), *dots_along(w, 7.0, 1.8, "#f3df4b", 2, 0.1, 0.9)])]
fn malachite(w, key, seed) array => [fill("#2a1e17"),
    *[for (k in 1 to 6) <ellipse cx: f(lerp2(w.root, w.cen, 0.6 + hash(seed + k) * 0.6)[0]), cy: f(lerp2(w.root, w.cen, 0.6 + hash(seed + k) * 0.6)[1]),
        rx: f(9.0 + hash(seed + k * 7) * 9.0), ry: f(6.0 + hash(seed + k * 11) * 6.0), fill: "#96e6aa", 'fill-opacity': "0.9">],
    *(if (w.fore) [for (k in [3, 6]) dot(inset(w, w.edge[k], 20.0), 5.0, "#96e6aa", 0.9)] else []),
    *[for (c in dots_along(w, 10.0, 2.4, "#96e6aa", 2, 0.1, 0.95)) c], margin(w, 4.0, "#1c140f")]
fn brimstone(w, key, seed) array => [*radial(key, [[0.0, "#cfc552"], [0.3, "#f5e868"], [1.0, "#fbf19c"]], w.root),
    *veins(w, "#a89a3a", 1.2, 0.3), dot(lerp2(w.root, w.cen, 0.95), 4.5, "#ef8a24"), margin(w, 2.0, "#b5a23a")]
fn lacewing(w, key, seed) array => [*radial(key, [[0.0, "#6e170f"], [0.3, "#dc4229"], [1.0, "#ef6939"]], w.root),
    *veins(w, "#3a0d08", 1.4, 0.5),
    *(if (w.fore) [margin(w, 30.0, "#140909", 0.4, 0.75), band(w, w.lead[17], inset(w, w.edge[8], 8.0), 7.0, "#f7f2ea", 0.05)] else []),
    margin(w, 16.0, "#140909"), *chevrons(w, 7.0, 4.0, "#f7f2ea", 2)]

// atlas order: the cell index is the species index the shaders read
pub let SPECIES = [
    {name: "Mourning cloak", weight: 3.0, line: "#e9d38f", paint: mourning, shape: {}},
    {name: "Peacock", weight: 2.0, line: "#2b1b16", paint: peacock, shape: {}},
    {name: "Red admiral", weight: 2.0, line: "#0c0806", paint: admiral, shape: {}},
    {name: "Cream moth", weight: 2.0, line: "#9c8158", paint: cream_moth, shape: {}},
    {name: "Cabbage white", weight: 1.5, line: "#9a9a8c", paint: cabbage, shape: {}},
    {name: "Owl", weight: 2.0, line: "#3a2618", paint: owl, shape: {}},
    {name: "Buckeye", weight: 1.5, line: "#3a2a1d", paint: buckeye, shape: {}},
    {name: "Morpho", weight: 1.5, line: "#05070c", paint: morpho, shape: {}},
    {name: "Ulysses", weight: 1.0, line: "#05070a", paint: ulysses, shape: {}},
    {name: "Monarch", weight: 1.0, line: "#120c08", paint: monarch, shape: {}},
    {name: "Painted lady", weight: 1.0, line: "#2b1d17", paint: painted_lady, shape: {}},
    {name: "Swallowtail", weight: 1.0, line: "#141414", paint: swallowtail, shape: {hh: 1.05}, tail: true},
    {name: "Zebra longwing", weight: 0.6, line: "#0c0c0c", paint: zebra, shape: {fw: 1.1, fh: 0.66, hw: 0.86, hh: 0.66}},
    {name: "Malachite", weight: 0.6, line: "#1c140f", paint: malachite, shape: {}},
    {name: "Brimstone", weight: 0.6, line: "#b5a23a", paint: brimstone, shape: {fw: 0.96, hw: 0.94}},
    {name: "Lacewing", weight: 0.8, line: "#120808", paint: lacewing, shape: {}}
]

fn tail(sh, color) element {
    let hh = sh.hh or 1.0;
    <path d: "M 40 " ++ f(96.0 * hh) ++ " Q 50 " ++ f(118.0 * hh) ++ " 46 " ++ f(134.0 * hh) ++ " Q 38 " ++ f(138.0 * hh) ++ " 34 " ++
        f(128.0 * hh) ++ " Q 32 " ++ f(112.0 * hh) ++ " 26 " ++ f(100.0 * hh), fill: color>
}
fn body() element => <g fill: "#2a1d14",
    <ellipse cx: "0", cy: "30", rx: "5", ry: "36"> <ellipse cx: "0", cy: "-14", rx: "7.5", ry: "17"> <circle cx: "0", cy: "-35", r: "6">
    for (s in [1.0, -1.0]) <g
        <path d: "M " ++ f(s * 2.0) ++ " -40 Q " ++ f(s * 8.0) ++ " -80 " ++ f(s * 22.0) ++ " -110", fill: "none", stroke: "#1a120c", 'stroke-width': "2">
        <circle cx: f(s * 22.0), cy: "-110", r: "3.4", fill: "#1a120c">>>

// one species in its cell: both sides share the clip paths and marks, mirrored about the body
fn cell(c, sp) element {
    let wings = [wing(false, sp.shape), wing(true, sp.shape)]
    let x = (c % GRID) * CELL + CELL / 2.0, y = int(c / GRID) * CELL + CELL / 2.0
    let painted = [for (i, w in wings) {
        let key = "s" ++ string(c) ++ (if (w.fore) "f" else "h");
        [<defs <clipPath id: key ++ "-clip", <path d: w.d>>>,
         <g 'clip-path': "url(#" ++ key ++ "-clip)", *sp.paint(w, key ++ "-fill", c * 1000 + i * 400)>,
         <path d: w.d, fill: "none", stroke: sp.line, 'stroke-width': "1.6">]
    }];
    <g transform: "translate(" ++ f(x) ++ " " ++ f(y) ++ ")",
        for (side in [1.0, -1.0]) <g transform: "scale(" ++ f(side * FIT) ++ " " ++ f(FIT) ++ ")",
            if (sp.tail == true) tail(sp.shape, sp.line);
            *painted[0]; *painted[1]>;
        <g transform: "scale(" ++ f(FIT) ++ ")", body()>>
}

// the whole sheet, transparent between butterflies
pub fn atlas() element => <svg xmlns: "http://www.w3.org/2000/svg", width: string(GRID * CELL), height: string(GRID * CELL),
    viewBox: "0 0 " ++ string(GRID * CELL) ++ " " ++ string(GRID * CELL),
    *[for (c, sp in SPECIES) cell(c, sp)]>
