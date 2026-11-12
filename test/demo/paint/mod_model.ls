// lambda demo paint — the editor model (Lambda_Pkg_Paint.md PT10). Everything here is a pure
// function of the app state: the paint session, tools, brush palette, colour and the view.
import paint: lambda.paint
import color: lambda.color
import watercolor: lambda.procart.watercolor

// a new canvas's size; an opened painting keeps its own
pub let WIDTH = 1200
pub let HEIGHT = 800
pub let FILE = "temp/paint/study.painting"
pub let TOOLS = ["brush", "eraser", "picker", "hand"]
pub let ZOOM_MIN = 0.1
pub let ZOOM_MAX = 8.0

pub fn clamp(v, lo, hi) => if (v < lo) lo else if (v > hi) hi else v

// the palette's built-in brushes: round dab brushes, then the bundled .myb files.
let ROUND = [{name: "Round", options: {size: 12.0, hardness: 0.85}},
    {name: "Airbrush", options: {size: 60.0, hardness: 0.0, profile: 'linear', flow: 0.2, spacing: 0.08}},
    {name: "Marker", options: {size: 24.0, hardness: 0.6, mode: 'wash', opacity: 0.45}}]
let MYB = [{name: "Pencil", file: "pencil.myb"}, {name: "Ink", file: "ink.myb"},
    {name: "Charcoal", file: "charcoal.myb"}, {name: "Blender", file: "blender.myb"}]
// bitmap-tip brushes (surface.tip); Chalk and Charcoal also read the paper's grain.
let TIPPED = [{name: "Chalk", tip: 'chalk', options: {size: 30.0, spacing: 0.15, grain: 0.7, angle_jitter: 180.0}},
    {name: "Flat", tip: 'flat', options: {size: 26.0, angle: 35.0, spacing: 0.08}},
    {name: "Speckle", tip: 'speckle', options: {size: 40.0, spacing: 0.35}}]
// brushes for both media (M2): a bristle brush paints a trail per hair that thins as its load
// runs out; Oil is a round brush with body. On an oil canvas every brush lays wet pigment.
let MEDIA_BRUSHES = [{name: "Bristle", options: {kind: 'bristle', size: 28.0, count: 28, depletion: 0.0025, pickup: 0.15}},
    {name: "Oil", options: {size: 26.0, hardness: 0.9, spacing: 0.1, amount: 1.2}}]
// the canvas media: pixel paint (rgba) or oil (a wet pigment film over the paper, PE8)
pub let MEDIA = [{kind: "pixel", label: "Pixel"}, {kind: "pigment", label: "Oil"}, {kind: "watercolor", label: "Watercolor"}]
pub let WET_PIGMENTS = watercolor.CLASSIC
pub let PAPERS = [{kind: "smooth", label: "Smooth"}, {kind: "cold_press", label: "Cold press"},
    {kind: "rough", label: "Rough"}, {kind: "canvas", label: "Canvas"}]
// the paper's height-field tile, in pixels: small enough to build quickly at start-up
pub let PAPER_SIZE = 128

// a palette entry: a brush with the size and opacity its controls set.
pub fn entry(name, b) => if ((string(b.kind) ^ { "" }) == "mypaint")
    ({name: name, brush: b, size: b.compiled.base_radius * 2.0,
      opacity: if (b.settings.opaque == null) 1.0 else b.settings.opaque.base})
    else ({name: name, brush: b, size: b.size, opacity: b.opacity})

// the palette: built-in round brushes and the .myb brushes found beside the demo.
pub fn palette(base) array^ {
    let rounds = [for (r in ROUND) entry(r.name, paint.brush(r.options)^)]
    let mybs = [for (m in MYB) (let b = paint.brush_from_myb(input(url_resolve(base, "brushes/" ++ m.file), 'json')^)^.brush,
        entry(m.name, if (m.name == "Charcoal") paint.brush({kind: 'mypaint', settings: b.settings, grain: 0.6})^ else b))]
    let tipped = [for (t in TIPPED) entry(t.name, paint.brush({*: t.options, tip: paint.tip(t.tip, {size: 64})^})^)]
    let media = [for (m in MEDIA_BRUSHES) entry(m.name, paint.brush(m.options)^)];
    [*rounds, *mybs, *tipped, *media]
}

pub fn initial(brushes) map^ => {ps: paint.session(WIDTH, HEIGHT, {texture: {kind: 'cold_press', size: PAPER_SIZE}})^,
    tool: "brush", brush: 0, brushes: brushes,
    eraser: entry("Eraser", paint.brush({size: 32.0, hardness: 0.7, erase: true})^),
    color: [0.11, 0.13, 0.3], zoom: 1.0, pan: {x: 0.0, y: 0.0}, token: 0, stroke: null, drag: null,
    space: false, wet_needs_step:false, status: "Ready"}

// the palette entry the size and opacity controls edit.
pub fn editing(st) => if (st.tool == "eraser") st.eraser else st.brushes[st.brush]

fn with_editing(st, e) => if (st.tool == "eraser") ({*: st, eraser: e})
    else ({*: st, brushes: [for (i, b in st.brushes) if (i == st.brush) e else b]})

pub fn with_size(st, size) => with_editing(st, {*: editing(st), size: clamp(float(size), 1.0, 400.0)})
pub fn with_opacity(st, opacity) => with_editing(st, {*: editing(st), opacity: clamp(float(opacity), 0.01, 1.0)})

pub fn with_tool(st, tool) => if (tool in TOOLS) ({*: st, tool: tool, status: tool_status(tool)}) else st
fn tool_status(tool) => if (tool == "brush") "Brush: drag on the canvas to paint"
    else if (tool == "eraser") "Eraser: drag to remove paint" else if (tool == "picker") "Picker: click to take a colour"
    else "Hand: drag to move the canvas"

pub fn with_brush(st, i) => if (i >= 0 and i < (len(st.brushes) ^ { 0 }))
    ({*: st, brush: i, tool: "brush", status: "Brush: " ++ st.brushes[i].name}) else st

// the paper kind the session's texture was built from ("smooth" for none).
pub fn paper_kind(st) => if (st.ps.texture_spec == null) "smooth" else string(st.ps.texture_spec.kind) ^ { "smooth" }

pub fn with_paper(st, kind) map^ {
    let label = [for (p in PAPERS where p.kind == kind) p.label]
    if (len(label) == 0) raise error("Paint: unknown paper " ++ string(kind))
    else ({*: st, ps: paint.with_texture(st.ps, if (kind == "smooth") null else ({kind: kind, size: PAPER_SIZE}))^,
        status: "Paper: " ++ label[0] ++ " (grained brushes such as Chalk show it)"})
}

// the canvas medium: "pigment" for an oil canvas, "pixel" otherwise.
pub fn medium(st) => if (st.ps.wet != null) "watercolor" else if ((string(st.ps.layer.format) == "pigment") ^ { false }) "pigment" else "pixel"

// a fresh canvas of another medium on the same paper; the old painting and its history go.
pub fn with_medium(st, kind) map^ {
    let label = [for (m in MEDIA where m.kind == kind) m.label]
    if (len(label) == 0) raise error("Paint: unknown medium " ++ string(kind))
    else if (kind == medium(st)) st
    else ({*: st, stroke: null, wet_needs_step:false,
        ps: paint.session(width(st), height(st), {medium: kind, paper: st.ps.paper, texture: st.ps.texture_spec})^,
        status: if (kind == "pigment") "Oil: wet strokes mix; Dry sets them, Light shows the paint's relief"
            else if (kind == "watercolor") "Watercolor: choose a pigment; water carries paint until Dry"
            else "Pixel: strokes lay colour over colour"})
}

pub fn with_wet_pigment(st, index: int) map^ =>
    ({*:st, ps:paint.select_wet(st.ps, index, st.ps.wet_water)^,
      status:"Watercolor pigment: " ++ st.ps.wet_pigments[index].name})

// the oil canvas under its light, or flat.
pub fn with_light(st) map^ => ({*: st, ps: paint.with_lighting(st.ps, st.ps.lighting == null)^,
    status: if (st.ps.lighting == null) "Light on: the paint's relief catches the light" else "Light off: flat colour"})

pub fn with_color(st, rgb) => ({*: st, color: [for (v in rgb) clamp(float(v), 0.0, 1.0)]})

// the brush a stroke starts with: the palette entry at its size and opacity; a pen's eraser end
// always erases.
pub fn stroke_brush(st, eraser: bool) map^ {
    let e = if (eraser or st.tool == "eraser") st.eraser else st.brushes[st.brush]
    paint.adjust_brush(e.brush, {size: e.size, opacity: e.opacity})^
}

pub fn width(st) => st.ps.layer.width
pub fn height(st) => st.ps.layer.height

// zoom by `factor` about a point of the stage (stage CSS pixels), keeping that point fixed.
pub fn zoom_about(st, factor, x, y) {
    let zoom = clamp(st.zoom * factor, ZOOM_MIN, ZOOM_MAX)
    let k = zoom / st.zoom;
    {*: st, zoom: zoom, pan: {x: x - (x - st.pan.x) * k, y: y - (y - st.pan.y) * k}}
}

// the zoom that shows the whole canvas in a stage of this size, centred.
pub fn fit(st, w, h) {
    let zoom = clamp(min(w / width(st), h / height(st)) * 0.94, ZOOM_MIN, ZOOM_MAX);
    {*: st, zoom: zoom, pan: {x: (w - width(st) * zoom) / 2.0, y: (h - height(st) * zoom) / 2.0}}
}

// actual pixels, centred.
pub fn actual(st, w, h) =>
    ({*: st, zoom: 1.0, pan: {x: (w - width(st)) / 2.0, y: (h - height(st)) / 2.0}})

pub fn hex(rgb) => color.hex([rgb[0], rgb[1], rgb[2], 1.0]) ^ { "#000000" }
pub fn hex_rgb(text) array^ => slice(color.parse(text)^, 0, 3)
pub fn hsv(rgb) => color.hsv([rgb[0], rgb[1], rgb[2], 1.0])
pub fn from_hsv(h, s, v) => slice(color.rgb(h, s, v), 0, 3) ^ { [0.0, 0.0, 0.0] }

pub fn summary(st) {
    let e = editing(st);
    join([if (st.tool == "eraser") "Eraser" else e.name, string(int(round(e.size))) ++ " px",
        string(int(round(e.opacity * 100.0))) ++ "%", "zoom " ++ string(int(round(st.zoom * 100.0))) ++ "%",
        "undo " ++ string(len(st.ps.undo))], " · ")
}
