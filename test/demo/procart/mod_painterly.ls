// painterly rendering, ported from github.com/weimegan/painterly (MIT 6.815 NPR):
// jittered brush stamps sampled toward image detail and optionally turned along the
// local edge direction. Analysis and stroke plans are pure; only stamping is a pn.
import rng: lambda.procart.rng
import img: lambda.image
import media: lambda.doc.media

pub let WIDTH = 960          // canvas long edge in pixels
pub let BRUSH_SIZE = 60      // coarse stroke size; fine strokes are a quarter of it
pub let ANGLES = 18          // orientation bins over 180 degrees (the original's 36 over 360)
pub let NOISE = 0.3          // per-channel colour jitter
pub let DENSITY = [1200, 3000, 6000, 9000, 13000]   // strokes per layer for detail 1-5

pub let CATALOG = [
    {id:"single",title:"Single scale",tag:"ONE LAYER / UNIFORM",oriented:false,layers:1,
     note:"Equal brush strokes land anywhere on the canvas, each tinted from the photo beneath it."},
    {id:"painterly",title:"Two-scale painterly",tag:"TWO LAYERS / DETAIL",oriented:false,layers:2,
     note:"A coarse underpainting, then small strokes concentrated where the photo has fine detail."},
    {id:"oriented-single",title:"Oriented strokes",tag:"ONE LAYER / FLOW",oriented:true,layers:1,
     note:"Every stroke turns to follow the local edge direction, so brushwork flows with the forms."},
    {id:"oriented",title:"Oriented painterly",tag:"TWO LAYERS / FLOW",oriented:true,layers:2,
     note:"Flowing coarse strokes, then fine oriented detail where the photo is sharp."}
]
pub let BRUSHES = [{id:"bristle",title:"Long bristle"},{id:"round",title:"Round dab"},{id:"flat",title:"Dry flat"}]
pub let SOURCES = [{id:"coast",title:"Coast",path:"../photo/assets/coast.jpg"},
    {id:"mountains",title:"Mountains",path:"../photo/assets/mountains.jpg"}]

let LUMA = [[0.3],[0.6],[0.1],[0.0]]                 // the original's luminance weights, RGBA rows
let SOBEL = [[-1.0,0.0,1.0],[-2.0,0.0,2.0],[-1.0,0.0,1.0]]
let PAPER = [0.85,0.81,0.73]                          // linear-light canvas ground

fn clamp01(v: float) float => max(0.0, min(1.0, v))

// scipy's default truncation (4 sigma), applied as two separable 1-D passes.
fn gaussian(field, sigma: float) {
    let r = int(4.0 * sigma + 0.5)
    let w = [for (i in (0 - r) to r) exp(-float(i * i) / (2.0 * sigma * sigma))]
    let k = [for (v in w) v / sum(w)];
    convolve(convolve(field, [k]), transpose([k]))
}

// --- brushes: 64 x 64 coverage tiles grown from a fixed key, so a brush keeps its look across seeds

let TILE = 64

fn brush_alpha(id, x: int, y: int, grain: float, row: float) float {
    let u = (float(x) + 0.5) / float(TILE) - 0.5
    let v = (float(y) + 0.5) / float(TILE) - 0.5;
    if (id == "round") (let d = sqrt(u * u + v * v) / 0.42,
        clamp01((1.0 - d) * 4.0) * (0.78 + 0.22 * grain))
    else if (id == "bristle") (let half = 0.36 + 0.08 * row,
        clamp01((half - abs(u)) * 14.0) * clamp01((0.14 - abs(v)) * 30.0) * (0.45 + 0.4 * row + 0.15 * grain))
    else clamp01((0.34 - abs(u)) * 20.0) * clamp01((0.22 - abs(v)) * 30.0) * (if (row < 0.25) 0.15 else 0.6 + 0.4 * grain)
}

pub fn brush(id) array {
    let key = rng.derive(rng.key(1987), id)
    let grain = rng.units(rng.derive(key, "grain"), TILE * TILE)
    let rows = rng.units(rng.derive(key, "rows"), TILE);
    reshape([for (y in 0 to (TILE - 1), x in 0 to (TILE - 1)) brush_alpha(id, x, y, grain[y * TILE + x], rows[y])], [TILE, TILE])
}

// a layer's stamp tiles; the original re-rotated all 36 brushes for every stroke.
pub fn brush_set(tile, size: int, oriented: bool) array {
    let scaled = resize(tile, size, size);
    if (oriented) [for (k in 0 to (ANGLES - 1)) flatten(rotate(scaled, -180.0 * (float(k) + 0.5) / float(ANGLES)))]
    else [flatten(scaled)]
}

// --- analysis

// gamma-decoded like imageIO.imread; pixels stay RGBA so a stroke colour is one flat lookup.
pub fn source(bytes, width = WIDTH) map^ {
    let pixels = load({data:bytes,thumbnail:{width:width,height:width}})^
    let dims = shape(pixels)
    let h = dims[0], w = dims[1]
    let linear = as_float(pixels) ** 2.2;
    {width:w,height:h,linear:flatten(linear),lum:reshape(matmul(reshape(linear, [h * w, 4]), LUMA), [h, w])}
}

// sharpness drives the detail layer; the smoothed structure tensor gives stroke directions.
pub fn analyze(src) map {
    let smooth = gaussian(src.lum, 1.0)
    let high = src.lum - smooth
    let energy = gaussian(high * high, 4.0)
    let peak = max(flatten(energy))
    let ix = convolve(smooth, SOBEL)
    let iy = convolve(smooth, transpose(SOBEL));
    {width:src.width,height:src.height,linear:src.linear,
     sharpness:flatten(energy / (if (peak > 0.0) peak else 1.0)),
     xx:flatten(gaussian(ix * ix, 4.0)),xy:flatten(gaussian(ix * iy, 4.0)),yy:flatten(gaussian(iy * iy, 4.0))}
}

// the minimum-eigenvalue eigenvector of [[xx, xy], [xy, yy]] runs along the edge, a right
// angle from the gradient angle 0.5 * atan2(2xy, xx - yy); no per-pixel eigen solve is needed.
pub fn angle_bin(field, p: int) int {
    let edge = 0.5 * math.atan2(2.0 * field.xy[p], field.xx[p] - field.yy[p]) + math.pi / 2.0
    let turn = edge - math.pi * floor(edge / math.pi);
    min(ANGLES - 1, int(floor(turn / math.pi * float(ANGLES))))
}

// --- stroke plans

// rejection sampling against importance, as singleScalePaint does, but over the whole
// canvas: stamps clip at the edges instead of leaving an unpainted margin.
pub fn layer(field, key, size: int, count: int, detail_weighted: bool, oriented: bool) map {
    let w = field.width, h = field.height
    let mean = if (detail_weighted) sum(field.sharpness) / float(len(field.sharpness)) else 1.0
    let tries = if (detail_weighted) min(count * 40, int(ceil(1.3 * float(count) / max(mean, 0.025)))) else count
    let u = rng.units(rng.derive(key, "place"), 3 * tries)
    let picks = [for (i in 0 to (tries - 1), let p = int(u[3 * i] * float(h)) * w + int(u[3 * i + 1] * float(w))
        where not detail_weighted or u[3 * i + 2] <= field.sharpness[p] limit count) p]
    let jitter = rng.units(rng.derive(key, "color"), 3 * len(picks));
    {size:size,pixels:picks,
     colors:[for (i, p in picks, c in 0 to 2) (1.0 - NOISE / 2.0 + NOISE * jitter[3 * i + c]) * field.linear[4 * p + c]],
     bins:[for (p in picks) if (oriented) angle_bin(field, p) else 0]}
}

pub fn plan(field, seed: int, id, detail: int) map {
    let recipe = [for (r in CATALOG where r.id == id) r][0]
    let key = rng.derive(rng.key(seed), id)
    let count = DENSITY[detail - 1]
    let coarse = layer(field, rng.derive(key, "coarse"), BRUSH_SIZE, count, false, recipe.oriented);
    {recipe:recipe,width:field.width,height:field.height,
     layers:if (recipe.layers == 2) [coarse, layer(field, rng.derive(key, "fine"), BRUSH_SIZE div 4, count, true, recipe.oriented)]
        else [coarse]}
}

// --- painting

// tex is a size x size coverage tile; the stamp is centred on pixel p and clipped to the canvas.
pn stamp(var canvas: float[], w: int, h: int, tex, size: int, p: int, r: float, g: float, b: float) {
    let y0 = p div w - size div 2
    let x0 = p % w - size div 2
    let tx0 = max(0, 0 - x0)
    let tx1 = min(size, w - x0)
    let ty1 = min(size, h - y0)
    var ty: int = max(0, 0 - y0)
    while (ty < ty1) {
        var o: int = ((y0 + ty) * w + x0 + tx0) * 3
        var t: int = ty * size + tx0
        var tx: int = tx0
        while (tx < tx1) {
            let a: float = tex[t]
            canvas[o] = canvas[o] + a * (r - canvas[o])
            canvas[o + 1] = canvas[o + 1] + a * (g - canvas[o + 1])
            canvas[o + 2] = canvas[o + 2] + a * (b - canvas[o + 2])
            o = o + 3
            t = t + 1
            tx = tx + 1
        }
        ty = ty + 1
    }
}

// layers paint in order, each stroke blended over everything before it.
pub pn paint(plan, tile) {
    let w = plan.width, h = plan.height
    var canvas: float[] = [for (i in 0 to (w * h - 1), c in 0 to 2) PAPER[c]]
    for (stroke_layer in plan.layers) {
        let set = brush_set(tile, stroke_layer.size, plan.recipe.oriented)
        let c = stroke_layer.colors
        var k: int = 0
        while (k < len(stroke_layer.pixels)) {
            stamp(canvas, w, h, set[stroke_layer.bins[k]], stroke_layer.size, stroke_layer.pixels[k], c[3 * k], c[3 * k + 1], c[3 * k + 2])
            k = k + 1
        }
    }
    canvas
}

// gamma-encoded like imageIO.imwrite, then embedded as an in-memory PNG.
pub fn encode(plan, canvas) map^ {
    let pixels = as_ubyte(reshape(canvas, [plan.height, plan.width, 3]) ** (1.0 / 2.2))
    let bytes = img.encode(img.from_array(pixels)^, {format:'png'})^;
    {bytes:bytes,uri:media.data_uri(bytes, "image/png")^}
}
