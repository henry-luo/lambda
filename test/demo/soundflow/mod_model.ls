// Soundflow's state: strings to bow, strokes that replay in loops, and the
// clock that drives them. Everything here is pure; the view's handlers own
// playback and the DOM (S12.1.3).
//
// A stroke keeps every point with the time it was drawn at. After the pen
// lifts, the stroke's tip travels the same path at the same pace, rests, and
// starts again; wherever the path crosses a string, the string sounds in the
// stroke's voice. A string's pitch comes from its length: drag an end to retune.

pub let W = 1280.0
pub let H = 800.0
// level balances the instruments: the harp's noise attack makes it the quietest
pub let VOICES = [
    {key: "harp", name: "Harp", ink: "#5cb8ad", shift: 0, level: 1.0},
    {key: "kalimba", name: "Kalimba", ink: "#ee7a4f", shift: 0, level: 0.5},
    {key: "glass", name: "Glass", ink: "#e6dccb", shift: 12, level: 0.36}
]
pub let SCALES = [
    {key: "pentatonic", name: "Pentatonic", root: 48, steps: [0, 2, 4, 7, 9]},
    {key: "dub", name: "Dub minor", root: 45, steps: [0, 3, 5, 7, 10]},
    {key: "hirajoshi", name: "Hirajoshi", root: 50, steps: [0, 2, 3, 7, 8]},
    {key: "whole", name: "Whole tone", root: 48, steps: [0, 2, 4, 6, 8, 10]}
]
pub let LAYOUTS = [{key: "harp", name: "Harp"}, {key: "loom", name: "Loom"},
    {key: "sun", name: "Sun"}, {key: "stars", name: "Constellation"}]
// the longest string sounds the lowest of DEGREES scale steps, the shortest the highest
pub let LMIN = 60.0
pub let LMAX = 640.0
pub let DEGREES = 15
pub let MAX_STROKES = 12
// playback speeds: the loops replay faster or slower; pitch stays put
pub let SPEEDS = [0.5, 0.75, 1.0, 1.5, 2.0]
let NORMAL_SPEED = 2
pub let RING_MS = 1600.0
let REST = 450.0
let NIB = 0.785
let GRAB = 20.0

pub fn clamp(v, lo, hi) => max(lo, min(hi, v))
fn fmod(a, b) => a - b * floor(a / b)
fn num(v) => string(round(v * 10) / 10)
fn pt(x, y, t) => {x: x, y: y, t: t}
fn rod(x1, y1, x2, y2) => {a: {x: x1, y: y1}, b: {x: x2, y: y2}}
fn replace_at(items, k, item) => [for (i of items) if (i == k) item else items[i]]
fn last_of(items) => items[len(items) - 1]
fn keep_last(items, n) => if (len(items) > n) slice(items, len(items) - n, len(items)) else items

// ---- strings and pitch

pub fn length(s) => math.hypot(s.b.x - s.a.x, s.b.y - s.a.y)
pub fn degree(s) => int(clamp(round((1.0 - math.log(clamp(length(s), LMIN, LMAX) / LMIN) /
    math.log(LMAX / LMIN)) * (DEGREES - 1)), 0, DEGREES - 1)) ^ { 0 }
// the length that sounds scale step d
fn span(d) => LMAX * math.pow(LMIN / LMAX, d / (DEGREES - 1.0))
pub fn note(scale, d) => (let k = len(scale.steps), scale.root + 12 * (d div k) + scale.steps[d % k])
pub fn string_note(st, i) => note(SCALES[st.scale], degree(st.strings[i]))
pub fn voice_note(st, voice, i) => string_note(st, i) + VOICES[voice].shift
let NAMES = ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"]
pub fn note_name(n) => NAMES[n % 12] ++ string(n div 12 - 1)

fn harp_layout() => [for (i in 0 to 12, let x = 250.0 + i * 65.0, let base = 700.0 - i * 14.0)
    rod(x, base - span(i), x, base)]
fn loom_layout() => [
    *[for (k in 0 to 5, let y = 190.0 + k * 84.0, let l = span(1 + 2 * k)) rod(640.0 - l / 2, y, 640.0 + l / 2, y)],
    *[for (k in 0 to 5, let x = 365.0 + k * 110.0, let l = span(2 + 2 * k)) rod(x, 400.0 - l / 2, x, 400.0 + l / 2)]]
// rays wind around the centre, the longest pointing right where there is room
fn sun_layout() => [for (i in 0 to 11, let a = i * math.pi / 6.0, let l = span(3 + i))
    rod(640.0 + 70.0 * math.cos(a), 400.0 + 70.0 * math.sin(a),
        640.0 + (70.0 + l) * math.cos(a), 400.0 + (70.0 + l) * math.sin(a))]
fn jitter(i, k) => fmod(math.sin(i * 12.9898 + k * 78.233) * 43758.5453, 1.0)
fn stars_layout() => [for (i in 0 to 15, let cx = 230.0 + jitter(i, 1) * 820.0, let cy = 170.0 + jitter(i, 2) * 460.0,
    let a = jitter(i, 3) * math.pi, let h = span(5 + int(jitter(i, 4) * 10)) / 2,
    let dx = math.cos(a) * h, let dy = math.sin(a) * h)
    rod(clamp(cx - dx, 20.0, W - 20.0), clamp(cy - dy, 20.0, H - 20.0), clamp(cx + dx, 20.0, W - 20.0), clamp(cy + dy, 20.0, H - 20.0))]
pub fn layout(k) => if (k == 1) loom_layout() else if (k == 2) sun_layout() else if (k == 3) stars_layout() else harp_layout()

// ---- strokes

// where segment p→q crosses string s: the fraction along p→q, or null
fn cross_at(p, q, s) {
    let rx = q.x - p.x, ry = q.y - p.y
    let sx = s.b.x - s.a.x, sy = s.b.y - s.a.y
    let den = rx * sy - ry * sx
    let ax = s.a.x - p.x, ay = s.a.y - p.y
    let u = if (abs(den) < 0.000001) -1.0 else (ax * sy - ay * sx) / den
    let v = if (abs(den) < 0.000001) -1.0 else (ax * ry - ay * rx) / den
    if (u >= 0.0 and u < 1.0 and v >= 0.0 and v <= 1.0) u else null
}
// a faster bow plays louder
fn bow_gain(p, q) => clamp(0.35 + 0.3 * math.hypot(q.x - p.x, q.y - p.y) / max(1.0, q.t - p.t), 0.35, 1.0)
fn segment_hits(p, q, strings) => [for (j of strings, let u = cross_at(p, q, strings[j]) where u != null)
    {s: j, at: p.t + u * (q.t - p.t), gain: bow_gain(p, q)}]
pub fn crossings(pts, strings) => if (len(pts) < 2) []
    else [for (i in 1 to len(pts) - 1) for (h in segment_hits(pts[i - 1], pts[i], strings)) h]

// A broad nib held at 45°: the ribbon is wide where the pen moves across the
// nib and hairline where it moves along it, and tapers at both ends.
fn heading(pts, i) => (let a = pts[max(0, i - 2)], let b = pts[min(len(pts) - 1, i + 2)],
    math.atan2(b.y - a.y, b.x - a.x))
fn half_width(pts, i, h) => (1.1 + 7.5 * abs(math.sin(h - NIB))) *
    min(1.0, 0.15 + min(i, len(pts) - 1 - i) / 6.0)
fn edges(pts) => [for (i of pts, let h = heading(pts, i), let w = half_width(pts, i, h))
    {lx: pts[i].x - math.sin(h) * w, ly: pts[i].y + math.cos(h) * w,
     rx: pts[i].x + math.sin(h) * w, ry: pts[i].y - math.cos(h) * w}]
pub fn outline(pts) {
    let side = edges(pts)
    if (len(pts) < 2) ""
    else "M" ++ join([for (e in side) num(e.lx) ++ " " ++ num(e.ly)], " L") ++ " L" ++
        join([for (e in reverse(side)) num(e.rx) ++ " " ++ num(e.ry)], " L") ++ " Z"
}
fn path_length(pts) => if (len(pts) < 2) 0.0
    else sum([for (i in 1 to len(pts) - 1) math.hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y)])

// born is the clock time the stroke's first pass began
pub fn make_stroke(voice, pts, strings, born) {
    let dur = last_of(pts).t
    {voice: voice, pts: pts, dur: dur, period: max(dur + REST, 700.0), born: born,
     outline: outline(pts), cross: crossings(pts, strings)}
}
pub fn phase(stroke, at_ms) => fmod(at_ms - stroke.born, stroke.period)
// the tip at phase p, or null while the stroke rests between passes
pub fn tip(stroke, p) {
    let pts = stroke.pts
    let after = [for (i in 1 to len(pts) - 1 where pts[i].t >= p) i]
    if (p > stroke.dur or len(after) == 0) null
    else (let q = pts[after[0]], let o = pts[after[0] - 1], let u = clamp((p - o.t) / max(1.0, q.t - o.t), 0.0, 1.0),
        {x: o.x + u * (q.x - o.x), y: o.y + u * (q.y - o.y)})
}
// the path just behind the tip
pub fn trail(stroke, p, head) {
    let behind = [for (e in stroke.pts where e.t <= p and e.t >= p - 280.0) e]
    "M" ++ join([for (e in [*behind, head]) num(e.x) ++ " " ++ num(e.y)], " L")
}

// ---- state

fn snapshot(st) => {strings: st.strings, strokes: st.strokes, layout: st.layout}
fn recross(strokes, strings) => [for (k in strokes) {*: k, cross: crossings(k.pts, strings)}]
fn checkpoint(st, next) => {*: next, history: keep_last([*st.history, snapshot(st)], 40)}

pub fn initial() => {strings: layout(0), layout: 0, scale: 0, voice: 0, strokes: [], drawing: null, drag: null,
    clock: 0.0, wall: null, paused: false, speed: NORMAL_SPEED, ribbons: true, ring: {}, hits: [], count: 0, history: [],
    menu: false, status: ""}
pub fn speed_of(st) => SPEEDS[st.speed]
pub fn speed_name(st) => string(speed_of(st)) ++ "×"

// which strings are sounding: string index → {at (wall ms), voice, gain}
fn ring_with(ring, hits, wall) => reduce([ring, *hits], (r, h) => {*: r, [string(h.s)]: {at: wall, voice: h.voice, gain: h.gain}})
fn sounding(st, hits: any, wall) => {*: st, hits: hits, ring: ring_with(st.ring, hits, wall), count: st.count + len(hits)}
pub fn ringing(st, wall) => len([for (k at st.ring where wall - st.ring[k].at < RING_MS) k]) > 0
// nothing moves: a frame would change nothing on screen
pub fn still(st, wall) => (len(st.strokes) == 0 or st.paused) and st.drawing == null and st.drag == null and not ringing(st, wall)

// a crossing at `at` is due when the stroke's tip passed it during (c0, c1]
fn due(stroke, at, c0, c1) => c1 - c0 >= stroke.period or (let d = fmod(at - phase(stroke, c0), stroke.period),
    d > 0.0 and d <= c1 - c0)

// one animation frame at wall time `wall` (ms): the loop clock advances at the
// playback speed unless paused, and every crossing the tips passed sounds
pub fn tick(st, wall) {
    let dt = if (st.wall == null) 0.0 else clamp(wall - st.wall, 0.0, 100.0)
    let c0 = st.clock
    let c1 = if (st.paused) c0 else c0 + dt * speed_of(st)
    let hits = if (c1 <= c0) [] else
        [for (k in st.strokes) for (h in k.cross where due(k, h.at, c0, c1)) {s: h.s, voice: k.voice, gain: h.gain}]
    sounding({*: st, clock: c1, wall: wall}, hits, wall)
}

// the notes a set of hits plays: [{voice: key, note, gain}]
pub fn playable(st, hits) => [for (h in hits) {voice: VOICES[h.voice].key, note: voice_note(st, h.voice, h.s),
    gain: h.gain * VOICES[h.voice].level}]

// ---- drawing

pub fn begin_stroke(st, x, y, t) => {*: st, drawing: {voice: st.voice, pts: [pt(x, y, 0.0)], t0: t}, hits: [], menu: false}
// the pen moved at wall time t: the new segment sounds every string it crosses at once
pub fn extend(st, x, y, t) => if (st.drawing == null) {*: st, hits: []} else (let d = st.drawing,
    let p = last_of(d.pts), let q = pt(clamp(x, 0.0, W), clamp(y, 0.0, H), max(p.t + 1.0, t - d.t0)),
    if (math.hypot(q.x - p.x, q.y - p.y) < 2.5) {*: st, hits: []} else sounding({*: st, drawing: {*: d, pts: [*d.pts, q]}},
        [for (h in segment_hits(p, q, st.strings)) {s: h.s, voice: d.voice, gain: h.gain}], t))
// the nearest string within reach of (x, y), or null
fn distance_to(s, x, y) {
    let dx = s.b.x - s.a.x, dy = s.b.y - s.a.y
    let u = clamp(((x - s.a.x) * dx + (y - s.a.y) * dy) / max(0.0001, dx * dx + dy * dy), 0.0, 1.0)
    math.hypot(s.a.x + u * dx - x, s.a.y + u * dy - y)
}
pub fn string_at(st, x, y, reach = 14.0) => (let near = [for (j of st.strings, let d = distance_to(st.strings[j], x, y)
    where d < reach order by d) j], if (len(near) > 0) near[0] else null)
// the pen lifted: a gesture becomes a looping stroke whose first pass is the
// one just drawn; a tap plucks the string under it
fn tap(st, d, t) => (let j = string_at(st, d.pts[0].x, d.pts[0].y),
    sounding({*: st, drawing: null}, if (j == null) [] else [{s: j, voice: d.voice, gain: 0.7}], t))
fn adopt(st, d) => checkpoint(st, {*: st, drawing: null, hits: [], status: "",
    strokes: keep_last([*st.strokes, make_stroke(d.voice, d.pts, st.strings, st.clock - last_of(d.pts).t)], MAX_STROKES)})
pub fn finish(st, t) => if (st.drawing == null) st
    else if (len(st.drawing.pts) < 3 or path_length(st.drawing.pts) < 12.0) tap(st, st.drawing, t)
    else adopt(st, st.drawing)

// ---- moving strings
// A string keeps its direction. Dragging an end stretches it along its own
// line, which retunes it; dragging its body slides it sideways, which keeps
// its pitch and moves where the loops meet it. Both stay inside the field.

let EDGE = 10.0
let BODY = 8.0
fn end_point(s, which) => if (which == "a") s.a else s.b
fn ends(st, x, y) => [for (j of st.strings) for (w in ["a", "b"]) (let e = end_point(st.strings[j], w),
    {s: j, end: w, d: math.hypot(e.x - x, e.y - y)})]
// what a press at (x, y) takes hold of: {s, end: "a" | "b" | "body"}, or null; ends win
pub fn grab_at(st, x, y) => (let near = [for (c in ends(st, x, y) where c.d < GRAB order by c.d) {s: c.s, end: c.end}],
    let body = string_at(st, x, y, BODY),
    if (len(near) > 0) near[0] else if (body != null) {s: body, end: "body"} else null)
pub fn begin_drag(st, grab, x, y) => {*: st, hits: [], menu: false,
    drag: {*: grab, from: {x: x, y: y}, orig: st.strings[grab.s], before: snapshot(st), moved: false}}

fn unit(p, q) => (let l = max(0.0001, math.hypot(q.x - p.x, q.y - p.y)), {x: (q.x - p.x) / l, y: (q.y - p.y) / l})
fn along(p, d, t) => {x: p.x + d.x * t, y: p.y + d.y * t}
fn axis_room(v, dv, hi) => if (abs(dv) < 0.000001) {lo: -1000000.0, hi: 1000000.0}
    else (let t1 = (EDGE - v) / dv, let t2 = (hi - EDGE - v) / dv, {lo: min(t1, t2), hi: max(t1, t2)})
// how far p may travel along d, either way, before leaving the field
fn room(p, d) => (let rx = axis_room(p.x, d.x, W), let ry = axis_room(p.y, d.y, H),
    {lo: max(rx.lo, ry.lo), hi: min(rx.hi, ry.hi)})
// the dragged end follows the pointer's projection onto the line, from LMIN to LMAX long
fn stretched(s, which, x, y) {
    let fixed = if (which == "a") s.b else s.a
    let d = unit(fixed, end_point(s, which))
    let l = clamp((x - fixed.x) * d.x + (y - fixed.y) * d.y, LMIN, min(LMAX, room(fixed, d).hi))
    if (which == "a") {a: along(fixed, d, l), b: s.b} else {a: s.a, b: along(fixed, d, l)}
}
// the whole string follows the pointer's travel across it
fn slid(s, from, x, y) {
    let d = unit(s.a, s.b)
    let n = {x: -d.y, y: d.x}
    let ra = room(s.a, n), rb = room(s.b, n)
    let o = clamp((x - from.x) * n.x + (y - from.y) * n.y, max(ra.lo, rb.lo), min(ra.hi, rb.hi))
    {a: along(s.a, n, o), b: along(s.b, n, o)}
}
fn reshape(st, g: any, moved: any) => (let strings = replace_at(st.strings, g.s, moved),
    {*: st, strings: strings, strokes: recross(st.strokes, strings), drag: {*: g, moved: g.moved or moved != g.orig}, hits: []})
pub fn drag_to(st, x, y) => reshape(st, st.drag,
    if (st.drag.end == "body") slid(st.drag.orig, st.drag.from, x, y) else stretched(st.drag.orig, st.drag.end, x, y))
// letting go plucks the string at its new pitch
fn released(st, g, t) => (let out = sounding({*: st, drag: null}, [{s: g.s, voice: st.voice, gain: 0.75}], t),
    if (g.moved) {*: out, history: keep_last([*st.history, g.before], 40)} else out)
pub fn end_drag(st, t) => if (st.drag == null) st else released(st, st.drag, t)

// ---- commands

fn wave_pts() => [for (i in 0 to 92, let x = 180.0 + i * 10.0) pt(x, 470.0 + 110.0 * math.sin((x - 180.0) / 70.0), i * 26.0)]
fn ellipse_pts() => [for (i in 0 to 90, let a = math.pi + i * 2.5 * math.pi / 90.0)
    pt(700.0 + 230.0 * math.cos(a), 330.0 + 120.0 * math.sin(a), i * 20.0)]
fn swoosh_pts() => [for (i in 0 to 40, let u = i / 40.0)
    pt((1.0 - u) * (1.0 - u) * 1080.0 + 2.0 * u * (1.0 - u) * 520.0 + u * u * 330.0,
       (1.0 - u) * (1.0 - u) * 120.0 + 2.0 * u * (1.0 - u) * 170.0 + u * u * 640.0, i * 32.0)]
// a hand zigzags with rounded turns, not sharp vertices
fn zigzag_pts() => [for (i in 0 to 120, let u = i / 120.0) pt(830.0 + 230.0 * u, 470.0 - 75.0 * math.sin(u * 7.0 * math.pi), i * 9.0)]
// a short piece for the harp: each part starts a little after the last
fn sample_strokes(st) => [for (k, part of [{voice: 0, pts: wave_pts()}, {voice: 1, pts: ellipse_pts()},
    {voice: 2, pts: swoosh_pts()}, {voice: 0, pts: zigzag_pts()}])
    make_stroke(part.voice, part.pts, st.strings, st.clock - last_of(part.pts).t + 500.0 * k)]

fn pick(items, value) => int(clamp(value, 0, len(items) - 1)) ^ { 0 }
fn relayout(st, k) => (let strings = layout(k),
    checkpoint(st, {*: st, layout: k, strings: strings, strokes: recross(st.strokes, strings), ring: {}, menu: false, status: LAYOUTS[k].name}))
fn sample(st) => (let base = {*: st, layout: 0, strings: layout(0), ring: {}},
    checkpoint(st, {*: base, strokes: sample_strokes(base), paused: false, menu: false, status: "A sample piece for the harp"}))
pub fn command(st, action, value) => if (action == "voice") {*: st, voice: pick(VOICES, value)}
    else if (action == "layout") relayout(st, pick(LAYOUTS, value))
    else if (action == "scale") {*: st, scale: pick(SCALES, value), menu: false, status: SCALES[pick(SCALES, value)].name}
    else if (action == "sample") sample(st)
    else if (action == "clear" and len(st.strokes) > 0) checkpoint(st, {*: st, strokes: [], status: "Cleared · undo brings it back"})
    else if (action == "undo" and len(st.history) > 0)
        {*: st, *: last_of(st.history), history: slice(st.history, 0, len(st.history) - 1), ring: {}, status: "Undone"}
    else if (action == "pause") {*: st, paused: not st.paused, status: if (st.paused) "" else "Paused"}
    else if (action == "menu") {*: st, menu: not st.menu}
    else if (action == "speed") (let k = pick(SPEEDS, value), {*: st, speed: k, status: "Speed " ++ string(SPEEDS[k]) ++ "×"})
    else if (action == "faster") command(st, "speed", min(st.speed + 1, len(SPEEDS) - 1))
    else if (action == "slower") command(st, "speed", max(st.speed - 1, 0))
    // the speed button steps up and wraps round to the slowest
    else if (action == "next-speed") command(st, "speed", (st.speed + 1) % len(SPEEDS))
    else if (action == "ribbons") {*: st, ribbons: not st.ribbons,
        status: if (st.ribbons) "Wide strokes hidden · tips still play" else ""}
    else st

// what an export records: each stroke's loop and where it is in that loop now,
// in real milliseconds at the current speed
pub fn export_plan(st) => (let v = speed_of(st), [for (k in st.strokes) {period: k.period / v, phase: phase(k, st.clock) / v,
    hits: [for (h in k.cross) {at: h.at / v, voice: VOICES[k.voice].key, note: voice_note(st, k.voice, h.s),
        gain: h.gain * VOICES[k.voice].level}]}])
// long enough for the slowest loop to come round twice
pub fn export_seconds(st) => clamp(2.0 * reduce([0.0, *[for (k in st.strokes) k.period]], (a, b) => max(a, b)) /
    speed_of(st) / 1000.0 + 2.0, 8.0, 24.0)
