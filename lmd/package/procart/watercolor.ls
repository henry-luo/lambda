// Watercolor after Curtis, Anderson, Seims, Fleischer & Salesin, "Computer-Generated
// Watercolor" (SIGGRAPH 1997), ported from the Washes engine (github.com/castavridis/washes-js,
// MIT; see WASHES_LICENSE.txt): a shallow-water layer over a paper height field carries
// suspended pigment, which settles into a deposited layer; Kubelka-Munk glazing composites it.
// Pigments, paper and stroke trails are pure (S12.1.1v3). The sheet changes every time step, so
// `stamp`, `wet_sheet`, `dry`, `step`, `render` and `swatches` work on it in place through var
// borrows (S9.1.3v3); a pure formulation measured 4-8x slower per pass (PG18, proposed).
// Hot loops avoid calls and value-producing `if` expressions: in the current JIT a call with float
// arguments costs ~100 ns and a float `if` expression ~30 ns, against ~6 ns for a plain store.
import rng: lambda.procart.rng

// --- pigments: Kubelka-Munk absorption K and scattering S per RGB channel; density rho,
// staining omega and granulation gamma drive adsorption (Curtis Figure 5 via Washes)

pub let CLASSIC = [
    {name:"Quinacridone Rose",K:[0.22,1.47,0.57],S:[0.05,0.003,0.03],density:0.02,staining:5.5,granulation:0.81},
    {name:"Hansa Yellow",K:[0.06,0.21,1.78],S:[0.5,0.88,0.009],density:0.06,staining:1.0,granulation:0.08},
    // Washes raises Cerulean's staining and granulation from the paper's 1.0 / 0.31 so blue stays where it lands.
    {name:"Cerulean Blue",K:[1.52,0.32,0.25],S:[0.06,0.26,0.4],density:0.01,staining:5.0,granulation:0.75}]

// high-scattering gouache: the particles reflect light themselves, so the paper barely shows.
pub let GOUACHE = [
    {name:"Opaque Rose",K:[0.005,7.5,2.04],S:[2.5,2.5,2.5],density:0.2,staining:1.5,granulation:0.1},
    {name:"Opaque Yellow",K:[0.0012,0.009,10.0],S:[2.5,2.5,2.5],density:0.22,staining:1.0,granulation:0.05},
    {name:"Opaque Blue",K:[7.5,1.51,0.005],S:[2.5,2.5,2.5],density:0.2,staining:1.5,granulation:0.1}]

fn clamp01(v: float) float => if (v < 0.0) 0.0 else if (v > 1.0) 1.0 else v
fn fmin(a: float, b: float) float => if (a < b) a else b
fn imin(a: int, b: int) int => if (a < b) a else b
fn imax(a: int, b: int) int => if (a > b) a else b

// Washes _colorToKS: invert K/S = (1 - R)^2 / 2R at the target reflectance; S fixes how fast a
// layer approaches it, so a dried saturated stroke (thickness ~1.3) lands within ~0.05 of the colour.
pub fn pigment(title, rgb, density = 0.04, staining = 2.5, granulation = 0.3) map {
    let scatter = 3.0 * (0.85 + float(granulation) * 0.5)
    let ratio = [for (c in rgb) (let r = clamp01(float(c)), let q = if (r < 0.02) 0.02 else if (r > 0.992) 0.992 else r,
        (1.0 - q) * (1.0 - q) / (2.0 * q))];
    {name:title,K:[for (k in ratio) k * scatter],S:[scatter,scatter,scatter],
     density:float(density),staining:float(staining),granulation:float(granulation)}
}

// a third palette from sRGB colours, through the same conversion Washes uses for custom pigments.
pub let EARTH = [
    pigment("Burnt Sienna",[0.62,0.29,0.14],0.05,3.0,0.55),
    pigment("Yellow Ochre",[0.85,0.66,0.3],0.05,2.0,0.45),
    pigment("Indigo",[0.2,0.27,0.45],0.03,4.0,0.2)]

pub let PALETTES = [{id:"classic",title:"Curtis classic",pigments:CLASSIC},
    {id:"gouache",title:"Gouache",pigments:GOUACHE},{id:"earth",title:"Earth",pigments:EARTH}]

// the render and transfer loops read flat tables: K by pigment then channel, then S the same way;
// density, staining, granulation per pigment. Scattering is floored so K/S stays finite.
pub fn optics(pigments) array => [*[for (p in pigments, c in 0 to 2) float(p.K[c])],
    *[for (p in pigments, c in 0 to 2) (let s = float(p.S[c]), if (s < 0.00001) 0.00001 else s)]]
pub fn adsorption(pigments) array => [for (p in pigments, v in [p.density, p.staining, p.granulation]) float(v)]

// --- paper: a height field in [0.25, 0.75]; slopes steer water and hollows collect granulating pigment

pub let PAPER_COLOR = [0.985,0.965,0.918]      // warm cream
pub let MAX_CELLS = 250000                      // bounded work (PG8): a 21-plane sheet is 42 MB

// Washes samples four octaves of value noise about ten lattice cells apart per sheet cell, so the
// height is independent from cell to cell and its slope is fine noise rather than a drainage field
// (coherent bumps channel water and mottle every wash). Here each octave is a keyed uniform,
// narrowed by 0.743 to the variance of a smoothstep-blended lattice value; the octave weights,
// the fibre speckle and the compression around the mean are Washes' generatePaper constants.
fn octave(key, k: int, n: int) array^ => 0.5 + 0.743 * (rng.units(rng.derive(key, ["octave", k]), n)^ - 0.5)

pub fn paper(width: int, height: int, seed: int) array^ {
    let n = width * height
    let key = rng.derive(rng.key(seed), "watercolor-paper")
    let h = 0.5 * octave(key, 0, n)^ + 0.275 * octave(key, 1, n)^ + 0.15125 * octave(key, 2, n)^ +
        0.0831875 * octave(key, 3, n)^ + (rng.units(rng.derive(key, "fiber"), n)^ - 0.5) * 0.04;
    0.5 + (clip(h, 0.0, 1.0) - 0.5) * 0.5
}

// --- the sheet: one float[] of per-cell planes, each width x height cells, row-major. One buffer
// lets every pass borrow a single var root (S9.1.3v3 exclusivity is checked per root binding).

pub let PAPER = 0          // paper height h
pub let WET = 1            // water saturation; above 0.04 a cell takes part in the flow
pub let PRESSURE = 3
pub let U = 4              // velocity, cells per step
pub let V = 5
pub let SUSPENDED = 8      // three pigment planes g, carried by the water
pub let DEPOSITED = 11     // three pigment planes d, settled into the paper
let WET_TMP = 2, U_NEW = 6, V_NEW = 7, SCRATCH = 14, BINARY = 17, BLUR = 18, BLUR_LARGE = 19, BLUR_TMP = 20
pub let PLANES = 21

pub type Sheet = {width:int,height:int,scale:float,
    wet_diffusion:float,pigment_diffusion:float,edge_kernel:int,edge_kernel_large:int,velocity_clamp:float,
    cells:float[],x0:int,x1:int,y0:int,y1:int,steps:int,since_shrink:int,wet_cells:int}

// constants were tuned at 2 display pixels per cell (Washes SCALE_REF); diffusion scales as 1/s^2,
// kernels and the velocity clamp as 1/s, with the explicit-scheme stability caps. The clamp never
// exceeds 1.5, so a step moves water at most 0.45 cells, which the advection stencil relies on.
pub fn sheet_bounded(width: int, height: int, seed: int, scale: float, wetness: float, max_cells: int, planes: int = PLANES) map^ {
    if (max_cells < 64 or max_cells > 1000000 or width < 8 or height < 8 or width > max_cells div height)
        raise error("procart.watercolor: a sheet needs 8 x 8 to " ++ string(max_cells) ++ " cells")
    // Bound total storage to the existing one-million-cell, 21-plane native sheet budget.
    else if (planes < 2 or planes > 1000000 * PLANES or width * height > (1000000 * PLANES) div planes)
        raise error("procart.watercolor: sheet storage exceeds the native sheet budget")
    else {
        let s = scale / 2.0
        let n = width * height
        let water = clamp01(wetness);
        {width:width,height:height,scale:scale,
         wet_diffusion:fmin(0.2, 0.1 / (s * s)),pigment_diffusion:fmin(0.2, 0.045 / (s * s)),
         edge_kernel:imax(1, int(round(4.0 / s))),edge_kernel_large:imax(1, int(round(20.0 / s))),
         velocity_clamp:fmin(1.5, 1.0 / s),
         cells:[*paper(width, height, seed)^, *fill(n, water)^, *fill(n * (planes - 2), 0.0)^],
         x0:0,x1:-1,y0:0,y1:-1,steps:0,since_shrink:0,wet_cells:if (water > 0.04) n else 0}
    }
}

// The reference keeps its original work bound; native paint can choose a larger bounded window.
pub fn sheet(width: int, height: int, seed: int, scale: float = 2.5, wetness: float = 0.0) map^ =>
    sheet_bounded(width, height, seed, scale, wetness, MAX_CELLS)^

// a fresh sheet on the same paper: no water or pigment beyond the given wetness.
pub fn blank(s, wetness: float = 0.0) map^ {
    let n = s.width * s.height
    let water = clamp01(wetness);
    {*:s,cells:[*subview(s.cells, 0, n), *fill(n, water)^, *fill(n * (PLANES - 2), 0.0)^],
     x0:0,x1:-1,y0:0,y1:-1,steps:0,since_shrink:0,wet_cells:if (water > 0.04) n else 0}
}

// one plane as a view of the sheet, e.g. plane(s, wc.WET) or plane(s, wc.DEPOSITED + 2).
pub fn plane(s, index: int) array^ => (let n = s.width * s.height, subview(s.cells, index * n, (index + 1) * n)^)

pub let SETTINGS = {evaporation:0.9988,drying:true,edges:true,tilt:0.0}

// --- brushes: a round brush with cone falloff f = 1 - d/r

// stamp centres along a drag, every flow x radius (Washes applyPendingPaint), ending at (x1, y1).
pub fn trail(x0: float, y0: float, x1: float, y1: float, radius: float, flow: float = 0.4) array^ {
    let spacing = if (radius * flow < 0.5) 0.5 else radius * flow
    let steps = imax(1, int(ceil(sqrt((x1 - x0) ** 2 + (y1 - y0) ** 2) / spacing))^);
    [for (k in 1 to steps) [x0 + (x1 - x0) * float(k) / float(steps), y0 + (y1 - y0) * float(k) / float(steps)]]
}

// brush kinds: pigment arrives by f^2 (clamped at one), water by f and pressure by f^2; clear water
// also re-suspends deposited pigment; a blot removes pigment from both layers (Washes applyStamp).
// The disc is walked here from scalars, so the member-place var borrow (s.cells) has only scalar
// siblings and is borrowed in place (LR12-45).
pn brush_cells(var c: float[], n: int, w: int, h: int, cx: float, cy: float, r: float, kind: int, channel: int, pigments: int,
        amount: float, wet_gain: float, pressure_gain: float, lift: float) {
    let g = (SUSPENDED + channel) * n, wet = WET * n, pressure = PRESSURE * n
    let x0 = imax(0, int(floor(cx - r))), x1 = imin(w - 1, int(ceil(cx + r)))
    let y0 = imax(0, int(floor(cy - r))), y1 = imin(h - 1, int(ceil(cy + r)))
    var y: int = y0
    var fy: float = floor(cy - r)
    if (fy < 0.0) { fy = 0.0 }
    while (y <= y1) {
        var x: int = x0
        var fx: float = floor(cx - r)
        if (fx < 0.0) { fx = 0.0 }
        while (x <= x1) {
            let d2 = (fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)
            if (d2 <= r * r) {
                let i = y * w + x
                let f = 1.0 - sqrt(d2) / r
                let f2 = f * f
                if (kind == 2) {
                    var p: int = 0
                    while (p < pigments) {
                        c[(SUSPENDED + p) * n + i] = c[(SUSPENDED + p) * n + i] * (1.0 - 0.22 * f2)
                        c[(SUSPENDED + pigments + p) * n + i] = c[(SUSPENDED + pigments + p) * n + i] * (1.0 - 0.22 * f2)
                        p = p + 1
                    }
                } else {
                    if (kind == 0) {
                        let next = c[g + i] + amount * f2
                        if (next > 1.0) { c[g + i] = 1.0 } else { c[g + i] = next }
                    } else {
                        var p: int = 0
                        while (p < pigments) {
                            let di = c[(SUSPENDED + pigments + p) * n + i]
                            if (di >= 0.001) {
                                let moved = di * lift * f2
                                let next = c[(SUSPENDED + p) * n + i] + moved
                                c[(SUSPENDED + pigments + p) * n + i] = di - moved
                                if (next > 1.0) { c[(SUSPENDED + p) * n + i] = 1.0 } else { c[(SUSPENDED + p) * n + i] = next }
                            }
                            p = p + 1
                        }
                    }
                    let m = c[wet + i] + wet_gain * f
                    if (m > 1.0) { c[wet + i] = 1.0 } else { c[wet + i] = m }
                    c[pressure + i] = c[pressure + i] + pressure_gain * f2
                }
            }
            x = x + 1
            fx = fx + 1.0
        }
        y = y + 1
        fy = fy + 1.0
    }
}

let MARGIN = 24              // cells of padding around tracked activity
let SHRINK_INTERVAL = 60     // steps between active-region scans
let THRESHOLD = 0.001        // pigment or pressure that still counts as active

pn expand(var s: Sheet, cx: float, cy: float, r: float) {
    let lx = imax(0, int(floor(cx - r)) - MARGIN), rx = imin(s.width - 1, int(ceil(cx + r)) + MARGIN)
    let ty = imax(0, int(floor(cy - r)) - MARGIN), by = imin(s.height - 1, int(ceil(cy + r)) + MARGIN)
    if (s.x1 < s.x0) {
        s.x0 = lx
        s.x1 = rx
        s.y0 = ty
        s.y1 = by
    } else {
        s.x0 = imin(s.x0, lx)
        s.x1 = imax(s.x1, rx)
        s.y0 = imin(s.y0, ty)
        s.y1 = imax(s.y1, by)
    }
}

// brush = {kind:"pigment"|"water"|"lift", x, y, radius (cells), strength, channel (pigment only)}.
// The loads scale the paint and water each stamp carries (Washes paintLoad / waterLoad).
pub pn stamp_many(var s: Sheet, brush, pigments: int, paint_load: float = 1.0, water_load: float = 1.0) {
    if (pigments < 1 or pigments > 4096 or len(s.cells) != (12 + 3 * pigments) * s.width * s.height) {
        raise error("procart.watercolor: stamp pigment count does not match the sheet")
    }
    let r = float(brush.radius), cx = float(brush.x), cy = float(brush.y)
    let kind = if (brush.kind == "pigment") 0 else if (brush.kind == "water") 1 else 2
    let channel = if (kind == 0) imax(0, imin(pigments - 1, int(brush.channel))) else 0
    expand(s, cx, cy, r)^
    brush_cells(s.cells, s.width * s.height, s.width, s.height, cx, cy, r, kind, channel, pigments, float(brush.strength) * paint_load,
        (if (kind == 0) 0.45 else 0.55) * water_load, 0.18 * water_load, fmin(1.0, 0.18 * water_load))
}

pub pn stamp(var s: Sheet, brush, paint_load: float = 1.0, water_load: float = 1.0) {
    stamp_many(s, brush, 3, paint_load, water_load)^
}

// a uniform wash of clear water, as when the whole sheet is wetted for wet-in-wet work.
pn flood(var c: float[], n: int, wetness: float) {
    var i: int = WET * n
    let end = i + n
    while (i < end) {
        if (c[i] < wetness) { c[i] = wetness }
        i = i + 1
    }
}
pub pn wet_sheet(var s: Sheet, wetness: float = 0.62) {
    flood(s.cells, s.width * s.height, wetness)
    s.wet_cells = s.width * s.height
}

// a hair dryer: water above `level` leaves at once. At level 0 the suspended pigment settles where
// it stands, so the next wash glazes over a dry layer instead of mixing into it. A level just
// under the 0.04 flow threshold leaves a damp sheet: pigment still suspended, no longer moving.
pn drain(var c: float[], n: int, level: float) {
    var i: int = WET * n
    let end = i + n
    while (i < end) {
        if (c[i] > level) { c[i] = level }
        i = i + 1
    }
}
pub pn dry(var s: Sheet, level: float = 0.0) {
    if (level < 0.025) { s.wet_cells = evaporate(s.cells, s.width * s.height, 0.0) }
    else {
        drain(s.cells, s.width * s.height, level)
        if (level <= 0.04) { s.wet_cells = 0 }
    }
}

// --- one time step (Washes simStep): diffuse water, darken edges, move water, move pigment,
// adsorb and desorb, evaporate. Work stays inside the active region except evaporation.

pn copy_rect(var c: float[], n: int, to: int, from: int, w: int, x0: int, x1: int, y0: int, y1: int) {
    let dst = to * n, src = from * n
    var y: int = y0
    while (y <= y1) {
        var i: int = y * w + x0
        let end = y * w + x1
        while (i <= end) {
            c[dst + i] = c[src + i]
            i = i + 1
        }
        y = y + 1
    }
}

// capillary spreading through the fibres: explicit Laplacian diffusion of the water itself.
pn diffuse_wet(var c: float[], n: int, w: int, k: float, x0: int, x1: int, y0: int, y1: int) {
    let wet = WET * n, tmp = WET_TMP * n
    var y: int = y0
    while (y <= y1) {
        var i: int = tmp + y * w + x0
        let end = tmp + y * w + x1
        while (i <= end) {
            let m = c[i], a = c[i - 1], b = c[i + 1], up = c[i - w], dn = c[i + w]
            if (m >= 0.000001 or a >= 0.000001 or b >= 0.000001 or up >= 0.000001 or dn >= 0.000001) {
                c[i - tmp + wet] = m + k * (a + b + up + dn - 4.0 * m)
            }
            i = i + 1
        }
        y = y + 1
    }
}

pn binarize(var c: float[], n: int, w: int, x0: int, x1: int, y0: int, y1: int) {
    let wet = WET * n, out = BINARY * n
    var y: int = y0
    while (y <= y1) {
        var i: int = y * w + x0
        let end = y * w + x1
        while (i <= end) {
            if (c[wet + i] > 0.04) { c[out + i] = 1.0 } else { c[out + i] = 0.0 }
            i = i + 1
        }
        y = y + 1
    }
}

// separable running-sum box blur of the binary plane, valid on [x0..x1] x [y0..y1], with
// edge-clamped reads (Washes boxBlurRect). Each output cell continues its neighbour's sum; only
// the r cells nearest a sheet edge pay for clamping (a per-cell clamp tripled the pass).
pn box_blur(var c: float[], n: int, to: int, w: int, h: int, r: int, x0: int, x1: int, y0: int, y1: int) {
    let src = BINARY * n, tmp = BLUR_TMP * n, dst = to * n
    let inv = 1.0 / (2.0 * r + 1.0)
    let fast_x0 = imax(x0 + 1, r + 1), fast_x1 = imin(x1, w - 1 - r)
    // the first window: the in-sheet run plus the replicated edge cells beyond it
    let lo = imax(0, x0 - r), hi = imin(w - 1, x0 + r), left = imax(0, r - x0), right = imax(0, x0 + r - (w - 1))
    var y: int = imax(0, y0 - r)
    let ry1 = imin(h - 1, y1 + r)
    while (y <= ry1) {
        let row = src + y * w, out = tmp + y * w
        c[out + x0] = (left * c[row] + right * c[row + w - 1]) * inv
        var xx: int = lo
        while (xx <= hi) {
            c[out + x0] = c[out + x0] + c[row + xx] * inv
            xx = xx + 1
        }
        var x: int = x0 + 1
        while (x <= x1 and x < fast_x0) {
            c[out + x] = c[out + x - 1] + (c[row + imin(w - 1, x + r)] - c[row + imax(0, x - r - 1)]) * inv
            x = x + 1
        }
        while (x <= fast_x1) {
            c[out + x] = c[out + x - 1] + (c[row + x + r] - c[row + x - r - 1]) * inv
            x = x + 1
        }
        while (x <= x1) {
            c[out + x] = c[out + x - 1] + (c[row + imin(w - 1, x + r)] - c[row + imax(0, x - r - 1)]) * inv
            x = x + 1
        }
        y = y + 1
    }
    let fast_y0 = imax(y0 + 1, r + 1), fast_y1 = imin(y1, h - 1 - r)
    let top = imax(0, y0 - r), bottom = imin(h - 1, y0 + r), above = imax(0, r - y0), below = imax(0, y0 + r - (h - 1))
    let rw = r * w
    var cx: int = x0
    while (cx <= x1) {
        let col = tmp + cx, out = dst + cx
        c[out + y0 * w] = (above * c[col] + below * c[col + (h - 1) * w]) * inv
        var yy: int = top
        while (yy <= bottom) {
            c[out + y0 * w] = c[out + y0 * w] + c[col + yy * w] * inv
            yy = yy + 1
        }
        var cy: int = y0 + 1
        while (cy <= y1 and cy < fast_y0) {
            c[out + cy * w] = c[out + (cy - 1) * w] + (c[col + imin(h - 1, cy + r) * w] - c[col + imax(0, cy - r - 1) * w]) * inv
            cy = cy + 1
        }
        while (cy <= fast_y1) {
            let o = cy * w
            c[out + o] = c[out + o - w] + (c[col + o + rw] - c[col + o - rw - w]) * inv
            cy = cy + 1
        }
        while (cy <= y1) {
            c[out + cy * w] = c[out + (cy - 1) * w] + (c[col + imin(h - 1, cy + r) * w] - c[col + imax(0, cy - r - 1) * w]) * inv
            cy = cy + 1
        }
        cx = cx + 1
    }
}

// Curtis FlowOutward (section 4.3.3): lower the pressure near wet/dry boundaries so water, and the
// pigment it carries, flows outward and dries into a dark edge. Washes fades it out as a cell dries
// (no tide lines) and attenuates it in small islands, measured by the large blur.
pn darken_edges(var c: float[], n: int, w: int, x0: int, x1: int, y0: int, y1: int) {
    let wet = WET * n, pressure = PRESSURE * n, near = BLUR * n, large = BLUR_LARGE * n
    var y: int = y0
    while (y <= y1) {
        var i: int = y * w + x0
        let end = y * w + x1
        while (i <= end) {
            let m = c[wet + i]
            let deficit = 1.0 - c[near + i]
            if (m > 0.1 and deficit > 0.0) {
                // full strength above 0.4 water, fading linearly to nothing at 0.1
                if (m >= 0.4) { c[pressure + i] = c[pressure + i] - 0.045 * deficit * c[large + i] * m }
                else { c[pressure + i] = c[pressure + i] - 0.045 * deficit * c[large + i] * m * (m - 0.1) / 0.3 }
            }
            i = i + 1
        }
        y = y + 1
    }
}

// simplified shallow water (Curtis 4.3.1): pressure gradient, paper slope, viscosity and drag,
// an optional downhill tilt, then a circular speed clamp (a square one favoured the diagonals).
pn velocity(var c: float[], n: int, w: int, x0: int, x1: int, y0: int, y1: int, clamp: float, tilt: float) {
    let dt = 0.42
    let wet = WET * n, p = PRESSURE * n, h = PAPER * n, u = U * n, v = V * n, un = U_NEW * n, vn = V_NEW * n
    var y: int = y0
    while (y <= y1) {
        var i: int = y * w + x0
        let end = y * w + x1
        while (i <= end) {
            if (c[wet + i] < 0.04) {
                c[un + i] = 0.0
                c[vn + i] = 0.0
            } else {
                let ui = c[u + i], vi = c[v + i]
                let lu = c[u + i - 1] + c[u + i + 1] + c[u + i - w] + c[u + i + w] - 4.0 * ui
                let lv = c[v + i - 1] + c[v + i + 1] + c[v + i - w] + c[v + i + w] - 4.0 * vi
                let nu = ui + dt * (-0.5 * (c[p + i + 1] - c[p + i - 1]) - 0.06 * (c[h + i + 1] - c[h + i - 1]) + 0.1 * lu - 0.014 * ui)
                let nv = vi + dt * (-0.5 * (c[p + i + w] - c[p + i - w]) - 0.06 * (c[h + i + w] - c[h + i - w]) + 0.1 * lv - 0.014 * vi) + tilt * clamp
                let speed = sqrt(nu * nu + nv * nv)
                if (speed > clamp) {
                    c[un + i] = nu * clamp / speed
                    c[vn + i] = nv * clamp / speed
                } else {
                    c[un + i] = nu
                    c[vn + i] = nv
                }
            }
            i = i + 1
        }
        y = y + 1
    }
}

pn decay(var c: float[], n: int, index: int, factor: float, w: int, x0: int, x1: int, y0: int, y1: int) {
    let base = index * n
    var y: int = y0
    while (y <= y1) {
        var i: int = base + y * w + x0
        let end = base + y * w + x1
        while (i <= end) {
            c[i] = c[i] * factor
            i = i + 1
        }
        y = y + 1
    }
}

// semi-Lagrangian advection (Stam 1999) of all three pigments into the scratch planes: trace back
// along the velocity, sample bilinearly and scale by exp(-div v dt) so the scheme conserves mass,
// not density. Isotropic, with no CFL limit (Washes v0.67-0.68); writes clamp at one, as Washes'
// copy-back does. The speed clamp keeps the trace within half a cell, so the bilinear sample is a
// 3 x 3 stencil whose weights select the two cells on each axis: the same value, without
// converting the trace position to an index.
pn advect(var c: float[], n: int, w: int, adt: float, x0: int, x1: int, y0: int, y1: int) {
    let wet = WET * n, u = U * n, v = V * n
    var y: int = y0
    while (y <= y1) {
        var i: int = y * w + x0
        let end = y * w + x1
        while (i <= end) {
            if (c[wet + i] >= 0.04) {
                let area = math.exp(-0.5 * ((c[u + i + 1] - c[u + i - 1]) + (c[v + i + w] - c[v + i - w])) * adt)
                let du = c[u + i] * adt, dv = c[v + i] * adt
                let ax = abs(du), ay = abs(dv)
                let wl = 0.5 * (du + ax), wr = 0.5 * (ax - du), wm = 1.0 - ax
                let wu = 0.5 * (dv + ay) * area, wd = 0.5 * (ay - dv) * area, wn = (1.0 - ay) * area
                var p: int = 0
                while (p < 3) {
                    let j = (SUSPENDED + p) * n + i
                    let moved = wu * (wl * c[j - w - 1] + wm * c[j - w] + wr * c[j - w + 1]) +
                        wn * (wl * c[j - 1] + wm * c[j] + wr * c[j + 1]) +
                        wd * (wl * c[j + w - 1] + wm * c[j + w] + wr * c[j + w + 1])
                    if (moved > 1.0) { c[(SCRATCH + p) * n + i] = 1.0 } else { c[(SCRATCH + p) * n + i] = moved }
                    p = p + 1
                }
            }
            i = i + 1
        }
        y = y + 1
    }
}

// a little isotropic diffusion of the advected pigment gives the soft wet-in-wet blend; the rest of
// the padded region takes the advected value unchanged (Washes' copy-back, then diffusion).
pn spread(var c: float[], n: int, pigment: int, w: int, k: float, x0: int, x1: int, y0: int, y1: int,
        px0: int, px1: int, py0: int, py1: int) {
    let wet = WET * n, g = (SUSPENDED + pigment) * n, t = (SCRATCH + pigment) * n
    var y: int = py0
    while (y <= py1) {
        let inner = y >= y0 and y <= y1
        var x: int = px0
        var i: int = y * w + px0
        while (x <= px1) {
            let m = c[t + i]
            if (inner and x >= x0 and x <= x1 and c[wet + i] >= 0.04) {
                c[g + i] = m + k * (c[t + i - 1] + c[t + i + 1] + c[t + i - w] + c[t + i + w] - 4.0 * m)
            } else {
                c[g + i] = m
            }
            i = i + 1
            x = x + 1
        }
        y = y + 1
    }
}

// Curtis TransferPigment (section 4.5): suspended pigment settles by density, deposited pigment
// lifts back by density over staining; granulation gamma sends more of it into the paper's hollows.
// Both flows are non-negative here (pigment >= 0, h in [0.25, 0.75], gamma <= 1); the rare cell
// whose layer would pass one is capped as Washes does.
pn transfer(var c: float[], n: int, pigment: int, w: int, density: float, staining: float, granulation: float,
        x0: int, x1: int, y0: int, y1: int) {
    let wet = WET * n, h = PAPER * n, g = (SUSPENDED + pigment) * n, d = (DEPOSITED + pigment) * n
    var y: int = y0
    while (y <= y1) {
        var i: int = y * w + x0
        let end = y * w + x1
        while (i <= end) {
            if (c[wet + i] >= 0.04) {
                let ph = c[h + i]
                let gi = c[g + i], di = c[d + i]
                let down = gi * (1.0 - ph * granulation) * density
                let up = di * (1.0 + (ph - 1.0) * granulation) * density / staining
                if (di + down <= 1.0 and gi + up <= 1.0) {
                    c[d + i] = di + down - up
                    c[g + i] = gi + up - down
                } else {
                    let settle = if (di + down > 1.0) (if (di < 1.0) 1.0 - di else 0.0) else down
                    let lift = if (gi + up > 1.0) (if (gi < 1.0) 1.0 - gi else 0.0) else up
                    c[d + i] = di + settle - lift
                    c[g + i] = gi + lift - settle
                }
            }
            i = i + 1
        }
        y = y + 1
    }
}

// water leaves the whole sheet; a cell that dries locks its suspended pigment into the deposit.
// Settled cells are an exact no-op and are skipped. Returns the number of cells still wet.
pn evaporate(var c: float[], n: int, rate: float) int {
    let wet = WET * n, u = U * n, v = V * n, g = SUSPENDED * n, d = DEPOSITED * n
    var wet_count: int = 0
    var i: int = 0
    while (i < n) {
        let m = c[wet + i]
        if (m != 0.0 or c[g + i] != 0.0 or c[g + n + i] != 0.0 or c[g + 2 * n + i] != 0.0 or c[u + i] != 0.0 or c[v + i] != 0.0) {
            let next = m * rate
            if (next < 0.025) {
                var p: int = 0
                while (p < 3) {
                    let settled = c[d + p * n + i] + c[g + p * n + i]
                    if (settled > 1.0) { c[d + p * n + i] = 1.0 } else { c[d + p * n + i] = settled }
                    c[g + p * n + i] = 0.0
                    p = p + 1
                }
                c[u + i] = 0.0
                c[v + i] = 0.0
                c[wet + i] = 0.0
            } else {
                c[wet + i] = next
                if (next > 0.04) { wet_count = wet_count + 1 }
            }
        }
        i = i + 1
    }
    return wet_count
}

// the bounding box of cells a pass could still change: any water, pigment above the threshold,
// or interior pressure (boundary pressure never decays and would pin the region open).
pn active_bounds(c: float[], n: int, w: int, h: int, x0: int, x1: int, y0: int, y1: int) int[] {
    let wet = WET * n, g = SUSPENDED * n, p = PRESSURE * n
    var minx: int = x1 + 1, maxx: int = x0 - 1, miny: int = y1 + 1, maxy: int = y0 - 1
    var y: int = y0
    while (y <= y1) {
        var x: int = x0
        while (x <= x1) {
            let i = y * w + x
            let interior = x >= 1 and x <= w - 2 and y >= 1 and y <= h - 2
            if (c[wet + i] > 0.000001 or c[g + i] > THRESHOLD or c[g + n + i] > THRESHOLD or c[g + 2 * n + i] > THRESHOLD or
                    (interior and c[p + i] > THRESHOLD)) {
                if (x < minx) { minx = x }
                if (x > maxx) { maxx = x }
                if (y < miny) { miny = y }
                if (y > maxy) { maxy = y }
            }
            x = x + 1
        }
        y = y + 1
    }
    return [minx, maxx, miny, maxy]
}

// settings: {evaporation, drying, edges, tilt}; adsorption is the pigments' flat table.
pub pn step(var s: Sheet, adsorption: float[], settings = SETTINGS) {
    let w = s.width, h = s.height, n = s.width * s.height
    if (s.x1 >= s.x0) {
        // stencil passes read +-1 cell, so they cover the region's interior; copies pad by one.
        let x0 = imax(1, s.x0), x1 = imin(w - 2, s.x1), y0 = imax(1, s.y0), y1 = imin(h - 2, s.y1)
        let px0 = imax(0, s.x0 - 1), px1 = imin(w - 1, s.x1 + 1), py0 = imax(0, s.y0 - 1), py1 = imin(h - 1, s.y1 + 1)
        copy_rect(s.cells, n, WET_TMP, WET, w, px0, px1, py0, py1)
        diffuse_wet(s.cells, n, w, s.wet_diffusion, x0, x1, y0, y1)
        if (settings.edges) {
            let pad = s.edge_kernel_large
            binarize(s.cells, n, w, imax(0, s.x0 - pad), imin(w - 1, s.x1 + pad), imax(0, s.y0 - pad), imin(h - 1, s.y1 + pad))
            box_blur(s.cells, n, BLUR, w, h, s.edge_kernel, s.x0, s.x1, s.y0, s.y1)
            box_blur(s.cells, n, BLUR_LARGE, w, h, s.edge_kernel_large, s.x0, s.x1, s.y0, s.y1)
            darken_edges(s.cells, n, w, s.x0, s.x1, s.y0, s.y1)
        }
        velocity(s.cells, n, w, x0, x1, y0, y1, s.velocity_clamp, float(settings.tilt) * 0.08)^
        copy_rect(s.cells, n, U, U_NEW, w, x0, x1, y0, y1)
        copy_rect(s.cells, n, V, V_NEW, w, x0, x1, y0, y1)
        decay(s.cells, n, PRESSURE, 0.94, w, x0, x1, y0, y1)
        var p: int = 0
        while (p < 3) {
            copy_rect(s.cells, n, SCRATCH + p, SUSPENDED + p, w, px0, px1, py0, py1)
            p = p + 1
        }
        advect(s.cells, n, w, 0.42 * 0.7, x0, x1, y0, y1)
        p = 0
        while (p < 3) {
            spread(s.cells, n, p, w, s.pigment_diffusion, x0, x1, y0, y1, px0, px1, py0, py1)
            transfer(s.cells, n, p, w, adsorption[3 * p], adsorption[3 * p + 1], adsorption[3 * p + 2], s.x0, s.x1, s.y0, s.y1)^
            p = p + 1
        }
    }
    if (settings.drying) { s.wet_cells = evaporate(s.cells, n, float(settings.evaporation))^ }
    s.steps = s.steps + 1
    s.since_shrink = s.since_shrink + 1
    if (s.since_shrink >= SHRINK_INTERVAL and s.x1 >= s.x0) {
        s.since_shrink = 0
        let b = active_bounds(s.cells, n, w, h, s.x0, s.x1, s.y0, s.y1)
        if (b[1] < b[0]) {
            s.x0 = 0
            s.x1 = -1
            s.y0 = 0
            s.y1 = -1
        } else {
            s.x0 = imax(0, b[0] - MARGIN)^
            s.x1 = imin(w - 1, b[1] + MARGIN)^
            s.y0 = imax(0, b[2] - MARGIN)^
            s.y1 = imin(h - 1, b[3] + MARGIN)^
        }
    }
}

// --- rendering: Kubelka-Munk glazing of the mixed pigment over the textured paper (Curtis section 5)

// optics is the flat K/S table of `optics`; RGBA values land in [0, 1] (as_ubyte saturates the
// faint damp sheen that can pass one). The mixture's K and S are the pigment amounts' weighted
// means. Reflectance of a layer of thickness x over background Rb, with sinh and cosh sharing one
// exponential, 2(a sinh + b cosh) = (a + b)e - (a - b)/e (Washes v1.14). The exponent is capped at
// 12, where the layer is visually opaque.
pn composite(var rgba: float[], c: float[], n: int, optics: float[], w: int, x0: int, x1: int, y0: int, y1: int, water: bool) {
    let h = PAPER * n, wet = WET * n, g = SUSPENDED * n, d = DEPOSITED * n
    let paper_r = PAPER_COLOR[0], paper_g = PAPER_COLOR[1], paper_b = PAPER_COLOR[2]
    var y: int = y0
    while (y <= y1) {
        var i: int = y * w + x0
        let end = y * w + x1
        while (i <= end) {
            let a = c[g + i] + c[d + i], b = c[g + n + i] + c[d + n + i], e = c[g + 2 * n + i] + c[d + 2 * n + i]
            let total = a + b + e
            let tint = (c[h + i] - 0.5) * 0.06
            let m = c[wet + i]
            let o = i * 4
            if (total < 0.004) {
                // bare paper, with a faint sheen where it is damp
                rgba[o] = paper_r + tint + m * 0.018
                rgba[o + 1] = paper_g + tint + m * 0.018
                rgba[o + 2] = paper_b + tint + m * 0.0216
            } else {
                let wa = a / total, wb = b / total, wc = e / total
                // thickness min(total, 4) as a branchless min: (p + |p|) / 2 is max(p, 0)
                let x = total - 0.5 * ((total - 4.0) + abs(total - 4.0))
                var ch: int = 0
                while (ch < 3) {
                    let k = optics[ch] * wa + optics[3 + ch] * wb + optics[6 + ch] * wc
                    let s = optics[9 + ch] * wa + optics[12 + ch] * wb + optics[15 + ch] * wc
                    let background = PAPER_COLOR[ch] + tint
                    let ka = 1.0 + k / s
                    let kb = sqrt(ka * ka - 1.0)
                    let raw = kb * s * x
                    let ex = math.exp(raw - 0.5 * ((raw - 12.0) + abs(raw - 12.0)))
                    let inv = 1.0 / ex
                    let denom = (ka + kb) * ex - (ka - kb) * inv
                    if (denom < 0.000000002) { rgba[o + ch] = 0.0 }
                    else {
                        let layer = (ex - inv) / denom
                        let through = 2.0 * kb / denom
                        rgba[o + ch] = layer + through * through * background / (1.0 - layer * background)
                    }
                    ch = ch + 1
                }
            }
            if (water and m > 0.1) {
                // inspection overlay: standing water tinted blue by its depth
                let t = 0.55 * (m - 0.5 * ((m - 1.0) + abs(m - 1.0)))
                rgba[o] = rgba[o] + (0.16 - rgba[o]) * t
                rgba[o + 1] = rgba[o + 1] + (0.45 - rgba[o + 1]) * t
                rgba[o + 2] = rgba[o + 2] + (0.92 - rgba[o + 2]) * t
            }
            rgba[o + 3] = 1.0
            i = i + 1
        }
        y = y + 1
    }
}

// a width x height x 4 RGBA buffer in [0, 1] for `render`.
pub fn canvas(s) array^ => fill(s.width * s.height * 4, 1.0)^

// recomposite the active region, or the whole sheet when full (after a palette or overlay change).
pub pn render(var rgba: float[], s: Sheet, optics: float[], full: bool = true, water: bool = false) {
    let w = s.width, h = s.height
    if (full) { composite(rgba, s.cells, w * h, optics, w, 0, w - 1, 0, h - 1, water) }
    else if (s.x1 >= s.x0) { composite(rgba, s.cells, w * h, optics, w, s.x0, s.x1, s.y0, s.y1, water) }
}

// (H, W, 4) ubyte pixels for dom.set_canvas_pixels or lambda.image.
pub fn pixels(s, rgba) array => as_ubyte(reshape(rgba, [s.height, s.width, 4]))

// CSS swatches: each pigment dried at the given thickness on the paper, through the same optics
// as the sheet (a one-row sheet with one cell per pigment).
pub pn swatches(pigments, thickness: float = 1.0) array^ {
    let kinds = len(pigments)
    var c: float[] = fill(kinds * PLANES, 0.0)^
    var p: int = 0
    while (p < kinds) {
        c[PAPER * kinds + p] = 0.5
        c[(DEPOSITED + p % 3) * kinds + p] = thickness
        p = p + 1
    }
    var rgba: float[] = fill(kinds * 4, 1.0)^
    composite(rgba, c, kinds, optics(pigments), kinds, 0, kinds - 1, 0, 0, false)
    let bytes = as_ubyte(rgba)
    return [for (q in 0 to (kinds - 1)) "rgb(" ++ string(bytes[4 * q]) ++ "," ++ string(bytes[4 * q + 1]) ++ "," ++ string(bytes[4 * q + 2]) ++ ")"]
}

// totals for inspection: suspended and deposited pigment, wet cells and the active region.
fn amount(s, from: int) => (let n = s.width * s.height, sum(subview(s.cells, from * n, (from + 3) * n)))
pub fn measure(s) map {
    let n = s.width * s.height;
    {suspended:amount(s, SUSPENDED),deposited:amount(s, DEPOSITED),wet:float(s.wet_cells) / float(n),steps:s.steps,
     active:if (s.x1 < s.x0) [0,0] else [s.x1 - s.x0 + 1, s.y1 - s.y0 + 1]}
}
