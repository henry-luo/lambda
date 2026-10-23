// Collages (vibe/Lambda_Demo_Photo_Album.md §3.3): a plain-data model, pure template layouts and
// edit operations (S1.4, S12.1.3). Canvas units are export pixels at the aspect's base size.
import L: .mod_library

pub let MIN_PHOTOS = 2
pub let MAX_PHOTOS = 30

// print styles for the "prints" layout: print size against its grid cell (spread), position
// jitter, tilt range in degrees, a square photo window, a large centred hero, a zigzag of
// alternate columns, and the photo title on the print's strip
let SCRAPBOOK = {spread:0.86, jitter:0.18, tilt:16.0, square:false, hero:false, stagger:false, label:true}
let PILE = {spread:1.32, jitter:0.34, tilt:14.0, square:false, hero:true, stagger:false, label:false}
let POLAROIDS = {spread:0.84, jitter:0.0, tilt:0.0, square:true, hero:false, stagger:false, label:false}
let KEEPSAKE = {spread:1.0, jitter:0.06, tilt:10.0, square:false, hero:false, stagger:true, label:false}

fn look(background, font, gutter, radius, border) => {background:background, font:font, gutter:gutter, radius:radius, border:border}

// A template is a layout family (`layout`, with its `shape` or `prints` style), where the caption and
// note sit (`text`: top, bottom or a centred plate) and the look it applies when chosen.
pub let TEMPLATES = [
    {key:"grid", title:"Grid", layout:"grid", text:"top", look:look("white", "sans", 16, 6, 0)},
    {key:"mosaic", title:"Mosaic", layout:"mosaic", text:"top", look:look("white", "sans", 16, 6, 0)},
    {key:"magazine", title:"Magazine", layout:"magazine", text:"top", look:look("cream", "serif", 12, 0, 0)},
    {key:"frame", title:"Frame", layout:"frame", text:"bottom", look:look("white", "script", 10, 0, 0)},
    {key:"spotlight", title:"Spotlight", layout:"grid", text:"center", look:look("white", "script", 8, 4, 0)},
    {key:"diamonds", title:"Diamonds", layout:"lattice", shape:"diamond", text:"top", look:look("white", "sans", 14, 0, 0)},
    {key:"honeycomb", title:"Honeycomb", layout:"lattice", shape:"hexagon", text:"top", look:look("honey", "serif", 12, 0, 0)},
    {key:"scrapbook", title:"Scrapbook", layout:"prints", prints:SCRAPBOOK, text:"top", look:look("paper", "hand", 16, 0, 0)},
    {key:"pile", title:"Pile", layout:"prints", prints:PILE, text:"top", look:look("linen", "hand", 0, 0, 0)},
    {key:"polaroids", title:"Polaroids", layout:"prints", prints:POLAROIDS, text:"top", look:look("blush", "serif", 24, 0, 0)},
    {key:"keepsake", title:"Keepsake", layout:"prints", prints:KEEPSAKE, text:"top", look:look("kraft", "script", 24, 0, 0)},
    {key:"heart", title:"Heart", layout:"mask", shape:"heart", text:"top", look:look("blush", "script", 10, 0, 0)},
    {key:"circle", title:"Circle", layout:"mask", shape:"circle", text:"top", look:look("midnight", "serif", 10, 0, 0)},
    {key:"star", title:"Star", layout:"mask", shape:"star", text:"top", look:look("sunset", "sans", 8, 0, 0)},
    {key:"balloon", title:"Balloon", layout:"mask", shape:"balloon", text:"top", look:look("sky", "script", 8, 0, 0)}]
pub let ASPECTS = [{key:"square", title:"Square", width:2000, height:2000}, {key:"landscape", title:"Landscape 3:2", width:3000, height:2000},
    {key:"portrait", title:"Portrait 4:5", width:2400, height:3000}, {key:"wide", title:"Wide 16:9", width:3200, height:1800}]
pub let BACKGROUNDS = [{key:"white", title:"White", css:"#ffffff", ink:"#1d1d1f"}, {key:"cream", title:"Cream", css:"#f4ede0", ink:"#3b3127"},
    {key:"charcoal", title:"Charcoal", css:"#1f2124", ink:"#f2f2f7"},
    {key:"paper", title:"Paper", css:"repeating-linear-gradient(45deg, #f3eadb 0px, #f3eadb 6px, #ece0cc 6px, #ece0cc 12px)", ink:"#3b3127"},
    {key:"blush", title:"Blush", css:"#efe4df", ink:"#2b2421"},
    {key:"linen", title:"Linen", css:"repeating-linear-gradient(90deg, #f1efea 0px, #f1efea 3px, #e9e6df 3px, #e9e6df 5px)", ink:"#34312c"},
    {key:"kraft", title:"Kraft", css:"repeating-linear-gradient(115deg, #d2b48c 0px, #d2b48c 4px, #c9aa80 4px, #c9aa80 9px)", ink:"#3a2a1a"},
    {key:"honey", title:"Honey", css:"linear-gradient(160deg, #fff4d6 0%, #ffd98a 100%)", ink:"#4a3410"},
    {key:"sky", title:"Sky", css:"linear-gradient(180deg, #cfe6ff 0%, #f3eeff 100%)", ink:"#23324a"},
    {key:"sunset", title:"Sunset", css:"linear-gradient(135deg, #ffb88c 0%, #ff7e9d 50%, #b98cff 100%)", ink:"#ffffff"},
    {key:"midnight", title:"Midnight", css:"radial-gradient(circle at 50% 40%, #33507a 0%, #141e30 75%)", ink:"#f2f2f7"},
    {key:"bloom", title:"Bloom", css:"radial-gradient(circle at 20% 15%, #ffd3e2 0%, #fff8f0 45%, #d8ecff 100%)", ink:"#3a2c3a"}]
// title fonts: `size` is the text height as a share of its band, `style` and `weight` its face
pub let FONTS = [{key:"sans", title:"Sans", css:"-apple-system,'Helvetica Neue',Arial,sans-serif", style:"normal", weight:"700", size:0.42},
    {key:"serif", title:"Serif", css:"Didot,'Bodoni 72',Georgia,serif", style:"italic", weight:"400", size:0.5},
    {key:"script", title:"Script", css:"'Snell Roundhand','Brush Script MT','Segoe Script',cursive", style:"normal", weight:"400", size:0.6},
    {key:"hand", title:"Handwritten", css:"'Bradley Hand','Segoe Print','Comic Sans MS',cursive", style:"normal", weight:"400", size:0.5}]
pub let MAX_ZOOM = 4.0
let CAPTION_BAND = 0.11   // of the canvas height
let NOTE_BAND = 0.05
let CREDITS_BAND = 0.035
let PLATE_WIDTH = 0.64    // the centred title plate, of the canvas width
let PRINT_BORDER = 0.05   // print margin, of the print width
let PRINT_CAPTION = 0.16

fn find(items, key) => [for (it in items where it.key == key) it][0] or items[0]
pub fn template(m) => find(TEMPLATES, m.template)
pub fn aspect(m) => find(ASPECTS, m.aspect)
pub fn background(m) => find(BACKGROUNDS, m.background)
pub fn find_font(key) => find(FONTS, key)
pub fn font(m) => find_font(m.font)
fn has(items, key) => contains([for (it in items) it.key], key) ^ { false }

pub fn create(ids, title) => {template:"mosaic", aspect:"square", gutter:16, radius:6, border:0, background:"white",
    font:"sans", caption:title, note:"", credits:true, seed:1,
    slots:[for (id in slice(ids, 0, MAX_PHOTOS)) {id:id, fx:0.5, fy:0.5, zoom:1.0, free:null}]}

pub fn valid(m) => m is map and len(m.slots) >= MIN_PHOTOS and len(m.slots) <= MAX_PHOTOS and (has(TEMPLATES, m.template) ^ { false }) and (has(ASPECTS, m.aspect) ^ { false })

// ---------------------------------------------------------------- layouts

fn rect(x, y, w, h) => {x:x, y:y, w:w, h:h}
fn inset(r, d) => {x:r.x + d, y:r.y + d, w:max(1.0, r.w - 2.0 * d), h:max(1.0, r.h - 2.0 * d)}
fn cell(slot, r) => {slot:slot, kind:"cell", x:r.x, y:r.y, w:r.w, h:r.h, rot:0.0, clip:null}
// + 0.0 turns a rounded -0 into 0
fn pct(v) => string(round(v * 1000.0) / 10.0 + 0.0) ++ "%"
// a clip-path polygon from [x, y] points in the unit box
fn polygon(points) => "polygon(" ++ join([for (p in points) pct(p[0]) ++ " " ++ pct(p[1])], ", ") ++ ")"

// Layouts work on slot indexes. Equal cells, columns chosen for the canvas shape; a short last row
// stretches to the full width.
fn grid(slots, r) {
    let n = len(slots)
    let cols = max(1, min(n, int(round(math.sqrt(n * r.w / r.h)))))
    let rows = int(ceil(n / cols))
    let h = r.h / rows;
    [for (i, s in slots) (
        let row = int(floor(i / cols)),
        let in_row = if (row == rows - 1) n - cols * (rows - 1) else cols,
        let w = r.w / in_row,
        cell(s, rect(r.x + (i - row * cols) * w, r.y + row * h, w, h)))]
}

// Recursive split: the wider side divides; each half's share follows its photos' shapes, so wide
// photos get wide cells. Every split covers its rectangle exactly.
fn mosaic(slots, aspects, r) {
    let n = len(slots);
    if (n == 1) [cell(slots[0], r)]
    else (
        let k = int(floor(n / 2)),
        let side = r.w >= r.h,
        let a = sum([for (i in 0 to (k - 1)) if (side) aspects[i] else 1.0 / aspects[i]]),
        let b = sum([for (i in k to (n - 1)) if (side) aspects[i] else 1.0 / aspects[i]]),
        let share = max(0.25, min(0.75, a / (a + b))),
        let first = if (side) rect(r.x, r.y, r.w * share, r.h) else rect(r.x, r.y, r.w, r.h * share),
        let second = if (side) rect(r.x + r.w * share, r.y, r.w * (1.0 - share), r.h) else rect(r.x, r.y + r.h * share, r.w, r.h * (1.0 - share)),
        [*mosaic(slice(slots, 0, k), slice(aspects, 0, k), first), *mosaic(slice(slots, k, n), slice(aspects, k, n), second)])
}

// a hero photo takes 62% along the canvas's long side; the rest fill the remainder as a mosaic
fn magazine(slots, aspects, r) {
    let n = len(slots);
    if (n == 1) [cell(slots[0], r)]
    else (
        let side = r.w >= r.h,
        let hero = if (side) rect(r.x, r.y, r.w * 0.62, r.h) else rect(r.x, r.y, r.w, r.h * 0.62),
        let rest = if (side) rect(r.x + r.w * 0.62, r.y, r.w * 0.38, r.h) else rect(r.x, r.y + r.h * 0.62, r.w, r.h * 0.38),
        [cell(slots[0], hero), *mosaic(slice(slots, 1, n), slice(aspects, 1, n), rest)])
}

// equal cells in a run from (x, y) along x (or down y), `size` long and `depth` thick
fn run(slots, x, y, size, depth, along_x) => [for (j, s in slots) (let step = size / len(slots),
    cell(s, if (along_x) rect(x + j * step, y, step, depth) else rect(x, y + j * step, depth, step)))]

// A hero framed by a ring of small cells, clockwise from the top left: rows across the full width
// at top and bottom, columns between them. The ring is about as thick as a ring cell is long.
fn frame(slots, aspects, r) {
    let k = len(slots) - 1;
    if (k < 4) magazine(slots, aspects, r)
    else (
        let d = min(min(r.w, r.h) * 0.28, 2.0 * (r.w + r.h) / (k + 4)),
        let sides = max(2, min(k - 2, 2 * int(round((r.h - 2.0 * d) / d)))),
        let top = int(ceil((k - sides) / 2)),
        let right = int(ceil(sides / 2)),
        let bottom = k - sides - top,
        let ring = slice(slots, 1, k + 1),
        let mid = r.h - 2.0 * d,
        [cell(slots[0], rect(r.x + d, r.y + d, r.w - 2.0 * d, mid)),
            *run(slice(ring, 0, top), r.x, r.y, r.w, d, true),
            *run(slice(ring, top, top + right), r.x + r.w - d, r.y + d, mid, d, false),
            *run(reverse(slice(ring, top + right, top + right + bottom)), r.x, r.y + r.h - d, r.w, d, true),
            *run(reverse(slice(ring, top + right + bottom, k)), r.x, r.y + d, mid, d, false)])
}

// lattice cell shapes in their bounding box
let DIAMOND = polygon([[0.5, 0.0], [1.0, 0.5], [0.5, 1.0], [0.0, 0.5]])
let HEXAGON = polygon([[0.5, 0.0], [1.0, 0.25], [1.0, 0.75], [0.5, 1.0], [0.0, 0.75], [0.0, 0.25]])
let ROOT3 = 1.7320508075688772

// Lattice sites that fit whole in a w x h area, as offsets {x, y, w, h} of each shape's box centre
// from the area centre, at pitch u. Diamonds (half-diagonal u) tile edge to edge around a hero
// diamond twice as large at the centre, standing in for four sites; pointy hexagons
// (circumradius u) tile in offset rows.
fn sites(shape, w, h, u) {
    if (shape == "diamond") (
        let ax = int(floor(w / (2.0 * u) - 1.0)),
        let by = int(floor(h / (2.0 * u) - 1.0)),
        [for (a in (0 - ax) to ax, b in (0 - by) to by where (a + b) % 2 != 0 and abs(a) + abs(b) > 1)
            {x:a * u, y:b * u, w:2.0 * u, h:2.0 * u}])
    else (
        let rows = int(floor((h / 2.0 - u) / (1.5 * u))),
        let cols = int(ceil(w / (ROOT3 * u))),
        [for (row in (0 - rows) to rows, q in (0 - cols) to cols,
            let x = (q + 0.5 * abs(row % 2)) * ROOT3 * u
            where abs(x) + ROOT3 * u / 2.0 <= w / 2.0)
            {x:x, y:row * 1.5 * u, w:ROOT3 * u, h:2.0 * u}])
}

// the largest pitch, shrinking from a first guess, whose lattice holds `need` sites
fn pitch(shape, w, h, need, u) => if (len(sites(shape, w, h, u)) >= need or u < 4.0) u else pitch(shape, w, h, need, u * 0.94)

// Shaped cells on a lattice: the sites nearest the centre, so the cluster stays round; corners
// the cluster leaves free show the background.
fn lattice(n, shape, r) {
    let diamond = shape == "diamond"
    let need = if (diamond) n - 1 else n
    // first guess: n cells' area fills the rectangle; the hero diamond needs a quarter of the short side
    let guess = if (diamond) min(min(r.w, r.h) / 4.0, 1.3 * math.sqrt(r.w * r.h / (2.0 * (n + 3))))
        else min(min(r.w, r.h) / 2.0, 1.3 * math.sqrt(r.w * r.h / (2.6 * n)))
    let u = pitch(shape, r.w, r.h, need, guess)
    let cx = r.x + r.w / 2.0
    let cy = r.y + r.h / 2.0
    let clip = if (diamond) DIAMOND else HEXAGON
    let near = [for (s in sites(shape, r.w, r.h, u), let d = s.x * s.x + s.y * s.y order by d, s.y, s.x limit need) s];
    [*(if (diamond) [{slot:0, kind:"cell", x:cx - 2.0 * u, y:cy - 2.0 * u, w:4.0 * u, h:4.0 * u, rot:0.0, clip:clip}] else []),
        *[for (j, s in near) (let slot = if (diamond) j + 1 else j,
            {slot:slot, kind:"cell", x:cx + s.x - s.w / 2.0, y:cy + s.y - s.h / 2.0, w:s.w, h:s.h, rot:0.0, clip:clip})]]
}

// deterministic jitter in [0, 1) for photo i and draw k
fn rand(seed, i, k) => ((seed * 9301 + i * 49297 + k * 12289 + 7) % 233280) / 233280.0

pub fn print_height(w, aspect) {
    let inner = w * (1.0 - 2.0 * PRINT_BORDER);
    w * PRINT_BORDER + max(inner * 0.6, min(inner * 1.4, inner / aspect)) + w * PRINT_CAPTION
}

// a print at its dealt box, or at the box the user moved it to
fn print_at(m, i, opening, label, box) => (let placed = if (m.slots[i].free != null) m.slots[i].free else box,
    {slot:i, kind:"print", x:placed.x, y:placed.y, w:placed.w, h:print_height(placed.w, opening), rot:placed.rot, label:label})

// the photo window's aspect on a print: square in some styles
fn window(p, aspects, i) => if (p.square) 1.0 else aspects[i]

// Free-form prints dealt on a jittered grid in the style `p`, each slightly rotated. A hero (slot 0)
// is dealt last, so it lies on top in the middle.
fn prints(m, p, aspects, r) {
    let n = len(m.slots)
    let hero = p.hero and n > 1
    let rest = if (hero) [for (i in 1 to (n - 1)) i] else [for (i in 0 to (n - 1)) i]
    let cols = max(1, int(ceil(math.sqrt(len(rest) * r.w / r.h))))
    let rows = int(ceil(len(rest) / cols))
    let cw = r.w / cols
    let ch = r.h / rows
    let dealt = [for (j, i in rest) (
        let col = j % cols,
        let row = int(floor(j / cols)),
        let w = min(cw * p.spread, ch * p.spread / print_height(1.0, window(p, aspects, i))),
        let h = print_height(w, window(p, aspects, i)),
        // staggered columns zigzag up and down and lean alternately left and right
        let lift = if (p.stagger) (if (col % 2 == 0) -0.1 else 0.1) * ch else 0.0,
        // a short last row is centred
        let short = if (row == rows - 1) (cols * rows - len(rest)) * cw / 2.0 else 0.0,
        let cx = r.x + short + (col + 0.5) * cw + (rand(m.seed, i, 1) - 0.5) * cw * p.jitter,
        let cy = r.y + (row + 0.5) * ch + lift + (rand(m.seed, i, 2) - 0.5) * ch * p.jitter,
        let rot = if (p.stagger) (if ((col + row) % 2 == 0) -0.5 else 0.5) * (0.4 + 0.6 * rand(m.seed, i, 3)) * p.tilt
            else (rand(m.seed, i, 3) - 0.5) * p.tilt,
        print_at(m, i, window(p, aspects, i), p.label, {x:cx - w / 2.0, y:cy - h / 2.0, w:w, rot:rot}))];
    if (not hero) dealt
    else (
        let w = min(r.w * 0.56, r.h * 0.72 / print_height(1.0, window(p, aspects, 0))),
        let h = print_height(w, window(p, aspects, 0)),
        [*dealt, print_at(m, 0, window(p, aspects, 0), p.label, {x:r.x + (r.w - w) / 2.0, y:r.y + (r.h - h) / 2.0, w:w, rot:(rand(m.seed, 0, 3) - 0.5) * 6.0})])
}

// heart outline
let HEART = polygon([for (k in 0 to 47) (
    let t = k * 6.283185307179586 / 48.0,
    let x = 16.0 * math.pow(math.sin(t), 3.0),
    let y = 13.0 * math.cos(t) - 5.0 * math.cos(2.0 * t) - 2.0 * math.cos(3.0 * t) - math.cos(4.0 * t),
    [(x + 17.0) / 34.0, (14.0 - y) / 31.0])])
// five-pointed star, its points alternating between the outer and inner radius
let STAR = polygon([for (k in 0 to 9) (
    let t = k * 3.141592653589793 / 5.0 - 1.5707963267948966,
    let rad = if (k % 2 == 0) 0.5 else 0.21,
    [0.5 + rad * math.cos(t), 0.55 + rad * math.sin(t)])])
// balloon envelope: an arc over the top, tapering to the mouth
let BALLOON = polygon([*[for (k in 0 to 25) (
    let t = (-35.0 + k * 10.0) * 0.017453292519943295,
    [0.5 + 0.5 * math.cos(t), 0.42 - 0.42 * math.sin(t)])], [0.2, 0.82], [0.37, 0.97], [0.63, 0.97], [0.8, 0.82]])
// mask shapes: `aspect` is the shape box's width over height, `foot` the extra height below it
// (as a share of the shape height) that holds its decoration
let SHAPES = [{key:"heart", aspect:1.0, foot:0.0, clip:HEART}, {key:"circle", aspect:1.0, foot:0.0, clip:"circle(50% at 50% 50%)"},
    {key:"star", aspect:1.0, foot:0.0, clip:STAR}, {key:"balloon", aspect:0.8, foot:0.26, clip:BALLOON}]

// a rope from (x1, y1) to (x2, y2), `t` thick, as a clipped box
fn rope(x1, y1, x2, y2, t) {
    let x = min(x1, x2) - t
    let w = abs(x2 - x1) + 2.0 * t
    let h = y2 - y1
    let a = (x1 - x) / w
    let b = (x2 - x) / w
    let d = t / w;
    {kind:"rope", x:x, y:y1, w:w, h:h, clip:polygon([[a - d, 0.0], [a + d, 0.0], [b + d, 1.0], [b - d, 1.0]])}
}

// a balloon's ropes and basket below its envelope `e`
fn basket(e) {
    let b = rect(e.x + e.w * 0.37, e.y + e.h * 1.12, e.w * 0.26, e.h * 0.12);
    [rope(e.x + e.w * 0.38, e.y + e.h * 0.96, b.x + b.w * 0.06, b.y, e.w * 0.006),
        rope(e.x + e.w * 0.62, e.y + e.h * 0.96, b.x + b.w * 0.94, b.y, e.w * 0.006), {kind:"basket", *:b}]
}

// Caption and note bands: at the top, at the bottom above the credits, or on a plate over the body.
fn text_rects(t, m, W, H, credits) {
    let cap = if (trim(m.caption or "") != "") H * CAPTION_BAND else 0.0
    let note = if (trim(m.note or "") != "") H * NOTE_BAND else 0.0
    let block = cap + note
    let at = t.text
    let plate = if (at == "center" and block > 0.0) (let ph = block * 1.4, rect(W * (1.0 - PLATE_WIDTH) / 2.0, (H - credits - ph) / 2.0, W * PLATE_WIDTH, ph)) else null
    let y = if (plate != null) plate.y + (plate.h - block) / 2.0 else if (at == "bottom") H - credits - block else 0.0
    let x = if (plate != null) plate.x else 0.0
    let w = if (plate != null) plate.w else W;
    {top:if (at == "top") block else 0.0, bottom:if (at == "bottom") block else 0.0, plate:plate,
        caption:if (cap > 0.0) rect(x, y, w, cap) else null, note:if (note > 0.0) rect(x, y + cap, w, note) else null}
}

// Layout in canvas units: text bands, an optional mask and its decoration, and one cell or print per slot.
pub fn layout(m, aspects) {
    let t = template(m)
    let a = aspect(m)
    let W = float(a.width)
    let H = float(a.height)
    let bottom = if (m.credits) H * CREDITS_BAND else 0.0
    let text = text_rects(t, m, W, H, bottom)
    let body = rect(0.0, text.top, W, H - text.top - text.bottom - bottom)
    let g = float(m.gutter)
    let shape = if (t.layout == "mask") find(SHAPES, t.shape) else null
    // the shape box, with its foot below it, fits the body centred
    let sh = if (shape == null) 0.0 else min(body.h * 0.96 / (1.0 + shape.foot), body.w * 0.96 / shape.aspect)
    let mask_rect = if (shape == null) null
        else rect(body.x + (body.w - sh * shape.aspect) / 2.0, body.y + (body.h - sh * (1.0 + shape.foot)) / 2.0, sh * shape.aspect, sh)
    let area = inset(if (shape != null) mask_rect else body, g / 2.0)
    let idx = [for (i, s in m.slots) i]
    let raw = if (t.layout == "grid") grid(idx, area)
        else if (t.layout == "magazine") magazine(idx, aspects, area)
        else if (t.layout == "frame") frame(idx, aspects, area)
        else if (t.layout == "lattice") lattice(len(idx), t.shape, area)
        else if (t.layout == "prints") prints(m, t.prints, aspects, inset(body, g))
        else mosaic(idx, aspects, area);
    {width:W, height:H, caption:text.caption, note:text.note, plate:text.plate,
        credits:if (bottom > 0.0) rect(0.0, H - bottom, W, bottom) else null,
        mask:if (shape != null) {*:mask_rect, clip:shape.clip} else null,
        decor:if (shape != null and shape.key == "balloon") basket(mask_rect) else [],
        // cells give up half a gutter on every side, so neighbours sit one gutter apart
        cells:[for (c in raw) if (c.kind == "print") c else {*:c, x:c.x + g / 2.0, y:c.y + g / 2.0,
            w:max(1.0, c.w - g), h:max(1.0, c.h - g)}]}
}

pub fn cell_at(lay, slot) => [for (c in lay.cells where c.slot == slot) c][0]

// ---------------------------------------------------------------- edits

pub fn slot_aspects(lib, m) => [for (s in m.slots) L.photo(lib, s.id).aspect]

fn with_slots(m, slots) => {*:m, slots:slots}
fn clamp(v, lo, hi) => max(lo, min(hi, v))

// swapping moves the photos and their crops; positions (and scrapbook boxes) stay with the slot
pub fn swap(m, a, b) => if (a == b or a < 0 or b < 0 or a >= len(m.slots) or b >= len(m.slots)) m
    else with_slots(m, [for (i, s in m.slots) if (i == a) {*:m.slots[b], free:s.free}
        else if (i == b) {*:m.slots[a], free:s.free} else s])

// a photo already in the collage swaps places; a new photo replaces the slot's photo
pub fn assign(m, slot, id) {
    let at = index_of([for (s in m.slots) s.id], id);
    if (at != null) swap(m, slot, at)
    else with_slots(m, [for (i, s in m.slots) if (i == slot) {*:s, id:id, fx:0.5, fy:0.5, zoom:1.0} else s])
}

pub fn pan(m, slot, dfx, dfy) => with_slots(m, [for (i, s in m.slots) if (i == slot)
    {*:s, fx:clamp(s.fx + dfx, 0.0, 1.0), fy:clamp(s.fy + dfy, 0.0, 1.0)} else s])

pub fn zoom(m, slot, factor) => with_slots(m, [for (i, s in m.slots) if (i == slot)
    {*:s, zoom:clamp(s.zoom * factor, 1.0, MAX_ZOOM)} else s])

pub fn place(m, slot, box) => with_slots(m, [for (i, s in m.slots) if (i == slot) {*:s, free:box} else s])

// a new seed re-deals the prints of print templates; moved prints are re-dealt too
pub fn shuffle(m) => {*:m, seed:m.seed + 1, slots:[for (s in m.slots) {*:s, free:null}]}

pub fn set(m, key, value) => if (key == "template" and has(TEMPLATES, value)) {*:m, template:value}
    else if (key == "aspect" and has(ASPECTS, value)) {*:m, aspect:value, slots:[for (s in m.slots) {*:s, free:null}]}
    else if (key == "background" and has(BACKGROUNDS, value)) {*:m, background:value}
    else if (key == "font" and has(FONTS, value)) {*:m, font:value}
    else if (key == "gutter") {*:m, gutter:clamp(value, 0, 80)}
    else if (key == "radius") {*:m, radius:clamp(value, 0, 60)}
    else if (key == "border") {*:m, border:clamp(value, 0, 30)}
    else if (key == "caption") {*:m, caption:value}
    else if (key == "note") {*:m, note:value}
    else if (key == "credits") {*:m, credits:value == true}
    else m

// choosing a template also applies its look; moved prints are re-dealt in the new layout
pub fn apply_template(m, key) => if (not has(TEMPLATES, key)) m
    else (let k = find(TEMPLATES, key).look, {*:m, template:key, background:k.background, font:k.font,
        gutter:k.gutter, radius:k.radius, border:k.border, slots:[for (s in m.slots) {*:s, free:null}]})

// history: the models before (past) and after (future) the current one; at most 50 undo steps
pub fn history() => {past:[], future:[]}
pub fn record(h, before) => {past:[*slice(h.past, max(0, len(h.past) - 49), len(h.past)), before], future:[]}
pub fn undo(h, current) => if (len(h.past) == 0) null
    else {model:h.past[last], history:{past:slice(h.past, 0, len(h.past) - 1), future:[current, *h.future]}}
pub fn redo(h, current) => if (len(h.future) == 0) null
    else {model:h.future[0], history:{past:[*h.past, current], future:slice(h.future, 1, len(h.future))}}

// ---------------------------------------------------------------- credits

pub fn credit(lib, id) => (let p = L.photo(lib, id), p.title ++ " — " ++ p.author ++ " (" ++ p.license ++ ")")
pub fn credits_line(lib, m) => "Photos: " ++ join([for (s in m.slots) (let p = L.photo(lib, s.id), p.author ++ " (" ++ p.license ++ ")")], " · ")
pub fn credits_text(lib, m) => join([m.caption or "Collage", "", *[for (s in m.slots) (
    let p = L.photo(lib, s.id), credit(lib, s.id) ++ (if (p.source_url != "") " " ++ p.source_url else ""))]], "\n") ++ "\n"
