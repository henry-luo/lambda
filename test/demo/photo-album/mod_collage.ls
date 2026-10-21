// Collages (vibe/Lambda_Demo_Photo_Album.md §3.3): a plain-data model, pure template layouts and
// edit operations (S1.4, S12.1.3). Canvas units are export pixels at the aspect's base size.
import L: .mod_library

pub let MIN_PHOTOS = 2
pub let MAX_PHOTOS = 30
pub let TEMPLATES = [{key:"grid", title:"Grid"}, {key:"mosaic", title:"Mosaic"}, {key:"magazine", title:"Magazine"},
    {key:"scrapbook", title:"Scrapbook"}, {key:"heart", title:"Heart"}, {key:"circle", title:"Circle"}]
pub let ASPECTS = [{key:"square", title:"Square", width:2000, height:2000}, {key:"landscape", title:"Landscape 3:2", width:3000, height:2000},
    {key:"portrait", title:"Portrait 4:5", width:2400, height:3000}, {key:"wide", title:"Wide 16:9", width:3200, height:1800}]
pub let BACKGROUNDS = [{key:"white", title:"White", css:"#ffffff", ink:"#1d1d1f"}, {key:"cream", title:"Cream", css:"#f4ede0", ink:"#3b3127"},
    {key:"charcoal", title:"Charcoal", css:"#1f2124", ink:"#f2f2f7"},
    {key:"paper", title:"Paper", css:"repeating-linear-gradient(45deg, #f3eadb 0px, #f3eadb 6px, #ece0cc 6px, #ece0cc 12px)", ink:"#3b3127"}]
pub let MAX_ZOOM = 4.0
let CAPTION_BAND = 0.11   // of the canvas height
let CREDITS_BAND = 0.035
let PRINT_BORDER = 0.05   // scrapbook print margin, of the print width
let PRINT_CAPTION = 0.16

fn find(items, key) => [for (it in items where it.key == key) it][0] or items[0]
pub fn aspect(m) => find(ASPECTS, m.aspect)
pub fn background(m) => find(BACKGROUNDS, m.background)

pub fn create(ids, title) => {template:"mosaic", aspect:"square", gutter:16, radius:6, border:0, background:"white",
    caption:title, credits:true, seed:1,
    slots:[for (id in slice(ids, 0, MAX_PHOTOS)) {id:id, fx:0.5, fy:0.5, zoom:1.0, free:null}]}

pub fn valid(m) => m is map and len(m.slots) >= MIN_PHOTOS and len(m.slots) <= MAX_PHOTOS and
    (contains([for (t in TEMPLATES) t.key], m.template) ^ { false }) and (contains([for (a in ASPECTS) a.key], m.aspect) ^ { false })

// ---------------------------------------------------------------- layouts

fn rect(x, y, w, h) => {x:x, y:y, w:w, h:h}
fn inset(r, d) => {x:r.x + d, y:r.y + d, w:max(1.0, r.w - 2.0 * d), h:max(1.0, r.h - 2.0 * d)}
fn cell(slot, r) => {slot:slot, kind:"cell", x:r.x, y:r.y, w:r.w, h:r.h, rot:0.0}

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

// deterministic jitter in [0, 1) for photo i and draw k
fn rand(seed, i, k) => ((seed * 9301 + i * 49297 + k * 12289 + 7) % 233280) / 233280.0

pub fn print_height(w, aspect) {
    let inner = w * (1.0 - 2.0 * PRINT_BORDER);
    w * PRINT_BORDER + max(inner * 0.6, min(inner * 1.4, inner / aspect)) + w * PRINT_CAPTION
}

// Free-form prints on a jittered grid, each slightly rotated; a moved print keeps its own box.
fn scrapbook(m, aspects, r) {
    let n = len(m.slots)
    let cols = max(1, int(ceil(math.sqrt(n * r.w / r.h))))
    let rows = int(ceil(n / cols))
    let cw = r.w / cols
    let ch = r.h / rows;
    [for (i, s in m.slots) (
        let w = min(cw * 0.86, ch * 0.86 / (print_height(1.0, aspects[i]))),
        let h = print_height(w, aspects[i]),
        let cx = r.x + (i % cols + 0.5) * cw + (rand(m.seed, i, 1) - 0.5) * cw * 0.18,
        let cy = r.y + (int(floor(i / cols)) + 0.5) * ch + (rand(m.seed, i, 2) - 0.5) * ch * 0.18,
        let placed = if (s.free != null) s.free else {x:cx - w / 2.0, y:cy - h / 2.0, w:w, rot:(rand(m.seed, i, 3) - 0.5) * 16.0},
        {slot:i, kind:"print", x:placed.x, y:placed.y, w:placed.w, h:print_height(placed.w, aspects[i]), rot:placed.rot})]
}

// heart outline as clip-path polygon percentages
let HEART = "polygon(" ++ join([for (k in 0 to 47) (
    let t = k * 6.283185307179586 / 48.0,
    let x = 16.0 * math.pow(math.sin(t), 3.0),
    let y = 13.0 * math.cos(t) - 5.0 * math.cos(2.0 * t) - 2.0 * math.cos(3.0 * t) - math.cos(4.0 * t),
    string(round((x + 17.0) / 34.0 * 1000.0) / 10.0) ++ "% " ++ string(round((14.0 - y) / 31.0 * 1000.0) / 10.0) ++ "%")], ", ") ++ ")"

// Layout in canvas units: bands, an optional mask rectangle and one cell or print per slot.
pub fn layout(m, aspects) {
    let a = aspect(m)
    let W = float(a.width)
    let H = float(a.height)
    let top = if (trim(m.caption or "") != "") H * CAPTION_BAND else 0.0
    let bottom = if (m.credits) H * CREDITS_BAND else 0.0
    let body = rect(0.0, top, W, H - top - bottom)
    let g = float(m.gutter)
    let masked = m.template == "heart" or m.template == "circle"
    let side = min(body.w, body.h) * 0.96
    let mask_rect = rect(body.x + (body.w - side) / 2.0, body.y + (body.h - side) / 2.0, side, side)
    let area = inset(if (masked) mask_rect else body, g / 2.0)
    let idx = [for (i, s in m.slots) i]
    let raw = if (m.template == "grid") grid(idx, area)
        else if (m.template == "magazine") magazine(idx, aspects, area)
        else if (m.template == "scrapbook") scrapbook(m, aspects, inset(body, g))
        else mosaic(idx, aspects, area);
    {width:W, height:H, caption:if (top > 0.0) rect(0.0, 0.0, W, top) else null,
        credits:if (bottom > 0.0) rect(0.0, H - bottom, W, bottom) else null,
        mask:if (masked) {*:mask_rect, clip:if (m.template == "circle") "circle(50% at 50% 50%)" else HEART} else null,
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

// a new seed re-deals scrapbook prints; moved prints are re-dealt too
pub fn shuffle(m) => {*:m, seed:m.seed + 1, slots:[for (s in m.slots) {*:s, free:null}]}

pub fn set(m, key, value) => if (key == "template" and contains([for (t in TEMPLATES) t.key], value)) {*:m, template:value}
    else if (key == "aspect" and contains([for (a in ASPECTS) a.key], value)) {*:m, aspect:value, slots:[for (s in m.slots) {*:s, free:null}]}
    else if (key == "background" and contains([for (b in BACKGROUNDS) b.key], value)) {*:m, background:value}
    else if (key == "gutter") {*:m, gutter:clamp(value, 0, 80)}
    else if (key == "radius") {*:m, radius:clamp(value, 0, 60)}
    else if (key == "border") {*:m, border:clamp(value, 0, 30)}
    else if (key == "caption") {*:m, caption:value}
    else if (key == "credits") {*:m, credits:value == true}
    else m

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
