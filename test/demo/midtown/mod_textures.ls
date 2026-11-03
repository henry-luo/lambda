// Procedural SVG textures, rasterised by Radiant as scene textures. Facades are 8 x 8 window cells
// (one cell per 4 m bay and 3.5 m storey); each has a twin emission map of lit windows for night.
// Image rows run top to bottom; texture v = 0 is the bottom row, so v climbs with the wall.
import fx: lambda.scene3d.effects
import rng: lambda.procart.rng
import city: .mod_city

let KEY = rng.key([city.SEED, "textures"])
let NS = "http://www.w3.org/2000/svg"
fn svg(size_w, size_h, parts) element => <svg xmlns: NS, width: size_w, height: size_h, viewBox: "0 0 " ++ string(size_w) ++ " " ++ string(size_h), *parts>
fn rect(x, y, w, h, paint, extra = {}) element => <rect x: x, y: y, width: w, height: h, fill: paint, *: extra>
pub fn texture(id, picture, w, h, options = {}) element => fx.svg_texture(picture, {id: id, width: w, height: h,
    'wrap-s': 10497, 'wrap-t': 10497, 'min-filter': 9987, *: options}) ^ { <texture id: id> }

// ---- facades
let WALL = ["#3c4a59", "#c9bca2", "#8e4b3b", "#e4e3dc"]
let GLASS = ["#5d7891", "#2e3a46", "#2b3540", "#26313d"]
let LIGHTS = ["#ffd58f", "#ffe9bf", "#fff3d6", "#d7e9ff", "#ffc978"]
// the window opening inside one 64 px cell
fn opening(style) => if (style == 0) {x: 3, y: 4, w: 58, h: 52} else if (style == 1) {x: 14, y: 14, w: 36, h: 38}
    else if (style == 2) {x: 18, y: 14, w: 28, h: 38} else {x: 0, y: 30, w: 64, h: 24}
fn lit(style, i, j) => rng.unit(rng.derive(KEY, ["lit", style, i, j])) < (if (style == 0) 0.38 else 0.5)
fn cell_detail(style, x, y) => if (style == 0) [rect(x, y + 56, 64, 8, "#26303a")]
    else if (style == 1) [rect(x + 11, y + 52, 42, 4, "#ddd3bf"), rect(x + 12, y + 10, 40, 4, "#b2a68f")]
    else if (style == 2) [rect(x + 15, y + 52, 34, 4, "#e6dccb"), rect(x + 15, y + 10, 34, 4, "#b56a55"),
        rect(x, y + 31, 64, 1, "#7b3f31"), rect(x, y + 47, 64, 1, "#7b3f31"), rect(x, y + 15, 64, 1, "#7b3f31")]
    else [rect(x, y + 54, 64, 2, "#c9c8c0")]
pub fn facade_svg(style, emissive) element {
    let o = opening(style)
    let cells = [for (j in 0 to 7, i in 0 to 7, let x = i * 64, let y = j * 64) *[
        *(if (emissive) [] else cell_detail(style, x, y)),
        rect(x + o.x, y + o.y, o.w, o.h, if (emissive) (if (lit(style, i, j)) LIGHTS[rng.below(rng.derive(KEY, ["hue", style, i, j]), len(LIGHTS))] else "#000000")
            else GLASS[style], if (emissive) {} else {opacity: 0.96 - 0.12 * ((i * 7 + j * 3) % 4) / 4.0}),
        *(if (emissive or style != 0) [] else [rect(x + 31, y + o.y, 2, o.h, "#7e8d9a")])]];
    svg(512, 512, [rect(0, 0, 512, 512, if (emissive) "#000000" else WALL[style]), *cells])
}
// ---- the ground-floor shop band: four bays with awnings, signs and doors
let AWNINGS = ["#b8433a", "#2f6f8f", "#3f7d4d", "#c58b2b", "#6b4d8f", "#c4572f"]
pub fn storefront_svg(emissive) element => svg(512, 144, [rect(0, 0, 512, 144, if (emissive) "#000000" else "#4a4440"),
    *[for (k in 0 to 3, let x = k * 128, let hue = AWNINGS[rng.below(rng.derive(KEY, ["awning", k]), len(AWNINGS))]) *(
        if (emissive) [rect(x + 10, 46, 72, 86, "#ffdca6"), rect(x + 14, 8, 100, 20, hue)]
        else [rect(x + 6, 6, 116, 26, "#d9d3c7"), rect(x + 14, 8, 100, 20, hue), rect(x, 30, 128, 10, hue),
            rect(x + 10, 46, 72, 86, "#2f3f4c"), rect(x + 90, 50, 28, 94, "#3a2b22"), rect(x + 8, 132, 76, 4, "#b7b0a4")])]])

// ---- ground surfaces, each one tile of world texture
fn speckle(id, size, base, colors, n, smallest, largest) element => svg(size, size, [rect(0, 0, size, size, base),
    *[for (k in 0 to (n - 1), let key = rng.derive(KEY, [id, k]), let w = rng.between(rng.derive(key, "w"), smallest, largest))
        rect(rng.between(rng.derive(key, "x"), 0.0, size - w), rng.between(rng.derive(key, "y"), 0.0, size - w), w, w,
            colors[rng.below(rng.derive(key, "c"), len(colors))], {opacity: 0.55})]])
pub fn asphalt_svg() element => speckle("asphalt", 256, "#3d3f43", ["#34363a", "#4a4c50", "#2e3033", "#55575a"], 420, 1.0, 3.0)
pub fn grass_svg() element => speckle("grass", 256, "#5b7d3c", ["#4f7034", "#6a8c46", "#46672f", "#7a9a52"], 520, 2.0, 5.0)
pub fn gravel_svg() element => speckle("gravel", 256, "#9b8f7b", ["#8a7f6c", "#b0a48e", "#7d7262", "#c2b7a1"], 520, 2.0, 5.0)
pub fn roof_svg() element => speckle("roof", 128, "#6c6b68", ["#605f5c", "#7a7975", "#56554f"], 160, 1.0, 3.0)
// one metre square slabs with darker joints
pub fn sidewalk_svg() element => svg(256, 256, [rect(0, 0, 256, 256, "#b9b6ae"),
    *[for (k in 0 to 3) *[rect(k * 64, 0, 2, 256, "#9d9a92"), rect(0, k * 64, 256, 2, "#9d9a92")]],
    *[for (i in 0 to 3, j in 0 to 3 where (i + j) % 3 == 0) rect(i * 64 + 2, j * 64 + 2, 62, 62, "#c3c0b8")]])
// running-bond brick paving for the plaza
pub fn paving_svg() element => svg(256, 256, [rect(0, 0, 256, 256, "#8f7f70"),
    *[for (row in 0 to 15, k in 0 to 4, let x = k * 64 - (if (row % 2 == 1) 32 else 0))
        rect(x + 1, row * 16 + 1, 62, 14, if ((row * 5 + k) % 3 == 0) "#a48f7b" else "#9b8775")]])
pub fn crate_svg() element => svg(128, 128, [rect(0, 0, 128, 128, "#b8874f"),
    *[for (k in 0 to 4) rect(0, k * 26, 128, 2, "#8a6236")],
    rect(0, 0, 128, 12, "#7a5530"), rect(0, 116, 128, 12, "#7a5530"), rect(0, 0, 12, 128, "#7a5530"), rect(116, 0, 12, 128, "#7a5530"),
    <path d: "M 12 112 L 112 16 L 116 22 L 18 116 Z", fill: "#7a5530">])
// a soft round pool of lamplight: alpha falls off from the centre
pub fn pool_svg() element => <svg xmlns: NS, width: 128, height: 128, viewBox: "0 0 128 128",
    <defs <radialGradient id: "fall", cx: "50%", cy: "50%", r: "50%",
        <stop offset: "0%", 'stop-color': "#ffffff", 'stop-opacity': "0.85">
        <stop offset: "55%", 'stop-color': "#ffffff", 'stop-opacity': "0.3">
        <stop offset: "100%", 'stop-color': "#ffffff", 'stop-opacity': "0">>>
    <rect width: 128, height: 128, fill: "url(#fall)">>
