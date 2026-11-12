// lambda.paint pigment medium (Lambda_Pkg_Paint.md PT5, PE8, PE7): pigment layers, KM mixing,
// drying and impasto lighting.
import paint: lambda.paint
fn check(label, value) => value
fn near(a, b, tolerance) => all([for (i in 0 to 2) abs(a[i] - b[i]) <= tolerance])
fn rgb(l, x, y) => slice(paint.pixel(l, x, y), 0, 3) ^ { [-1, -1, -1] }

// a pigment layer is a clean film over its paper: bands, a substrate in linear light, no tiles
let blank = paint.layer(96, 64, {format: 'pigment'})^
let cream = paint.layer(96, 64, {format: 'pigment', paper: [1.0, 0.9, 0.7]})^
check("pigment-layer", blank.bands == 3 and blank.substrate == [1.0, 1.0, 1.0] and paint.painted_tiles(blank) == 0)
check("clean-shows-paper", rgb(blank, 5, 5) == [255, 255, 255] and near(rgb(cream, 50, 50), [255, 230, 179], 1))

// a pigment's opaque film shows its colour; K/S grows where the colour is dark
let y = paint.pigment([0.95, 0.8, 0.1])^
check("pigment-ks", len(y.K) == 3 and y.S == [3.0, 3.0, 3.0] and y.K[2] > y.K[1] and y.K[1] > y.K[0])
let oil = paint.brush({size: 24.0, hardness: 0.9, amount: 1.5})^
let yellow = paint.stroke_step(blank, oil, [0.95, 0.8, 0.1], [[10.0, 32.0], [60.0, 32.0]])^.layer
check("film-shows-colour", near(rgb(yellow, 20, 32), [242, 204, 26], 16))

// blue over wet yellow mixes by concentration toward green; each patch keeps its hue
let phthalo = [0.05, 0.4, 0.8]
let mixed = paint.stroke_step(yellow, oil, phthalo, [[40.0, 32.0], [86.0, 32.0]])^.layer
let g = rgb(mixed, 50, 32)
let blue = rgb(mixed, 80, 32)
check("blue-over-yellow-is-green", g[1] > g[0] + 40 and g[1] > g[2] + 40)
check("unmixed-patches", blue[2] > blue[0] and rgb(mixed, 20, 32)[0] > rgb(mixed, 20, 32)[2])

// mixing commutes: yellow over blue gives the same film
let blue_first = paint.stroke_step(blank, oil, phthalo, [[40.0, 32.0], [86.0, 32.0]])^.layer
let reversed = paint.stroke_step(blue_first, oil, [0.95, 0.8, 0.1], [[10.0, 32.0], [60.0, 32.0]])^.layer
check("deposit-commutes", rgb(reversed, 50, 32) == g)

// drying keeps the appearance and turns the body into relief; a glaze over dry yellow lies over
// it: it neither mixes into the yellow (as on the wet film) nor hides it, so the yellow takes blue
// out where the glaze is still transparent
let dry = paint.dried(yellow)^
check("drying-keeps-appearance", rgb(dry, 20, 32) == rgb(yellow, 20, 32) and rgb(dry, 5, 5) == [255, 255, 255])
let thin = paint.brush({size: 24.0, hardness: 0.9, amount: 0.15})^
let glaze = [[10.0, 32.0], [60.0, 32.0]]
let glazed = rgb(paint.stroke_step(dry, thin, phthalo, glaze)^.layer, 20, 32)
let wet_glaze = rgb(paint.stroke_step(yellow, thin, phthalo, glaze)^.layer, 20, 32)
let on_white = rgb(paint.stroke_step(blank, thin, phthalo, glaze)^.layer, 20, 32)
check("glaze-over-dry", glazed != wet_glaze and glazed != on_white and glazed[2] < on_white[2])

// lighting leaves a flat film alone and shades slopes: a clean area is flat, a stroke's rim is not
let light = paint.lighting({azimuth: 270.0, elevation: 45.0, relief: 1.0})^
fn lit(l, x, y) => slice(paint.present(l, {rect: [x, y, x + 1, y + 1], lighting: light})[0, 0], 0, 3) ^ { [-1, -1, -1] }
check("lighting-flat", lit(mixed, 5, 5) == rgb(mixed, 5, 5))
check("lighting-rims", lit(yellow, 30, 21) != rgb(yellow, 30, 21) and lit(yellow, 30, 21)[0] != lit(yellow, 30, 43)[0])
check("lighting-survives-drying", lit(dry, 30, 21) == lit(yellow, 30, 21))

// a libmypaint brush paints pigment too
let pencil = paint.brush({kind: 'mypaint', settings: {radius_logarithmic: 2.0, hardness: 0.8, opaque: 1.0}})^
let mp = paint.stroke_step(blank, pencil, [0.7, 0.1, 0.1], [[10.0, 10.0, 1.0, 0.0, 0.0, 0.0, 0.0], [80.0, 10.0, 1.0, 0.0, 0.0, 0.0, 0.1]])^.layer
check("mypaint-on-pigment", rgb(mp, 40, 10)[0] > rgb(mp, 40, 10)[1] + 40)

// refused: wash mode, lock alpha and rgba-only options on a pigment layer; drying an rgba layer
check("wash-refused", match (paint.stroke_step(blank, paint.brush({mode: 'wash'})^, phthalo, [[10.0, 10.0]]) ^ { ^ }) { case error: true default: false })
check("lock-alpha-refused", match (paint.stroke_step(blank, paint.brush({lock_alpha: 0.5})^, phthalo, [[10.0, 10.0], [20.0, 10.0]]) ^ { ^ }) { case error: true default: false })
check("background-refused", match (paint.present(blank, {background: [1.0, 1.0, 1.0, 1.0]}) ^ { ^ }) { case error: true default: false })
check("rgba-dry-refused", match (paint.dried(paint.layer(8, 8)^) ^ { ^ }) { case error: true default: false })
check("rgba-layer-paper-refused", match (paint.layer(8, 8, {paper: [1.0, 1.0, 1.0]}) ^ { ^ }) { case error: true default: false })
check("bad-light-refused", match (paint.lighting({elevation: 0.0}) ^ { ^ }) { case error: true default: false })
