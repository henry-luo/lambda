// lambda.paint M0 core: layers, the round dab brush, strokes and continuations, presentation
// (Lambda_Pkg_Paint.md PT2, PT4, PT7; Radiant_Design_Paint_Engine.md PE4, PE7).
import paint: lambda.paint
import strokes: lambda.paint.stroke
fn check(label, value) => value

let caps = paint.capabilities()^
check("engine", caps.engine and caps.dab_columns == 34 and caps.tile == 64)

// a 200 x 100 layer is a 4 x 2 grid of empty tiles
let blank = paint.layer(200, 100)^
check("blank-sparse", len(blank.tiles) == 8 and paint.painted_tiles(blank)^ == 0)

let red = [1.0, 0.0, 0.0]
let hard = paint.brush({size: 10.0, hardness: 1.0, spacing: 0.2, aa: 0.0, profile: 'linear'})^
let line = [[20, 50], [120, 50]]
let d = paint.dabs(hard, red, line)^
check("dab-spacing", d.count == 51 and len(d.table) == 51 * 34)

let painted = paint.stroke_step(blank, hard, red, line)^
check("paints", paint.pixel(painted.layer, 70, 50)^ == [255, 0, 0, 255])
check("outside", paint.pixel(painted.layer, 70, 70)^[3] == 0)
check("rect", painted.rect == [15, 45, 125, 55])
check("touched-tiles-only", paint.painted_tiles(painted.layer)^ == 2)
check("source-unchanged", paint.painted_tiles(blank)^ == 0)

// a stroke split into batches places exactly the dabs of the whole stroke
let path = [[20, 50, 0.2], [55.5, 50.2, 0.9], [90.25, 51.0, 0.4], [120, 50, 1.0]]
let soft = paint.brush({size: 12.0, hardness: 0.4, flow: 0.6, spacing: 0.17})^
let first = paint.stroke_step(blank, soft, red, [path[0], path[1]])^
let second = paint.stroke_step(blank, soft, red, [path[2], path[3]], first)^
let whole = paint.stroke_step(blank, soft, red, path)^
check("split-equals-whole", second.layer == whole.layer and second.bounds == whole.rect)
check("earlier-state-intact", first.layer != second.layer)

// wash composites the stroke once: retracing never exceeds the stroke opacity
let retrace = [[20, 50], [40, 50], [20, 50], [40, 50]]
let wash = paint.brush({size: 16.0, hardness: 0.5, opacity: 0.5, mode: 'wash'})^
let washed = paint.stroke_step(blank, wash, red, retrace)^
let wash_alpha = paint.pixel(washed.layer, 30, 50)^[3]
check("wash-caps-at-opacity", wash_alpha >= 120 and wash_alpha <= 128)
let built = paint.stroke_step(blank, paint.brush({size: 16.0, hardness: 0.5, opacity: 0.5})^, red, retrace)^
check("buildup-compounds", paint.pixel(built.layer, 30, 50)^[3] > 200)
let wash_first = paint.stroke_step(blank, wash, red, [retrace[0], retrace[1]])^
let wash_rest = paint.stroke_step(blank, wash, red, [retrace[2], retrace[3]], wash_first)^
check("wash-split-equals-whole", wash_rest.layer == washed.layer)

let eraser = paint.brush({size: 14.0, hardness: 1.0, aa: 0.0, erase: true})^
let erased = paint.stroke_step(painted.layer, eraser, red, [[60, 50], [80, 50]])^
check("erase", paint.pixel(erased.layer, 70, 50)^[3] == 0 and paint.pixel(erased.layer, 40, 50)^[3] == 255)

// opaque sRGB samples survive import into linear light and presentation
let pixels = reshape([for (p in 0 to 5, c in 0 to 3) if (c == 3) 255u8 else u8(p * 40 + c * 10)], [2, 3, 4])
check("pixels-round-trip", paint.present(paint.from_pixels(pixels)^)^ == pixels)
check("background", paint.present(blank, {rect: [0, 0, 1, 1], background: [1.0, 1.0, 1.0, 1.0]})^[0, 0] == [255, 255, 255, 255])

check("unknown-option", match (paint.brush({sise: 3}) ^ { ^ }) { case error: true default: false })
check("bad-colour", match (paint.stroke_step(blank, hard, [2.0, 0.0, 0.0], line) ^ { ^ }) { case error: true default: false })
check("continuation-mismatch", match (paint.stroke_step(blank, wash, red, [[1, 1]], first) ^ { ^ }) { case error: true default: false })
check("empty-batch", match (paint.stroke_step(blank, soft, red, [], first) ^ { ^ }) { case error: true default: false })
check("work-budget", match (strokes.render(blank, d.table, d.count, 10) ^ { ^ }) { case error: true default: false })
