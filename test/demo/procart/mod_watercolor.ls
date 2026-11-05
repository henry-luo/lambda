// Watercolor workshop: Curtis et al.'s simulation (lambda.procart.watercolor, ported from Washes)
// painted live. Studies are seeded, pure stroke plans; the sheet advances one animation frame at
// a time in a procedure, because every step updates the sheet in place (S12.1.1v3 / S9.1.3v3).
import rng: lambda.procart.rng
import wc: lambda.procart.watercolor

let Sheet = wc.Sheet

pub let WIDTH = 360            // visible cells; at 2.5 display pixels per cell the paper is 900 x 600
pub let HEIGHT = 240
pub let MARGIN = 6             // hidden cells on every side: boundary cells never flow, so pigment
pub let SHEET_WIDTH = WIDTH + 2 * MARGIN       // piles up there (Washes' CANVAS_OVERSCAN)
pub let SHEET_HEIGHT = HEIGHT + 2 * MARGIN
pub let SCALE = 2.5
pub let PAPER_SEED = 1997      // one sheet of paper for the session; seeds vary the painting
pub let STEPS_PER_FRAME = 2
pub let SIZES = [3.0, 5.0, 8.0, 12.0, 18.0]                 // brush radius in cells, sizes 1-5
pub let DRYING = [{id:"slow",title:"Slow",rate:0.9988},     // Washes' per-step rate
    {id:"normal",title:"Normal",rate:0.9976},{id:"fast",title:"Fast",rate:0.994}]
pub let TOOLS = [{id:"p0",kind:"pigment",channel:0},{id:"p1",kind:"pigment",channel:1},{id:"p2",kind:"pigment",channel:2},
    {id:"water",kind:"water",channel:0,title:"Clear water",label:"Water"},{id:"lift",kind:"lift",channel:0,title:"Blot",label:"Blot"}]
let STROKE_STRENGTH = 0.7      // a dragged stamp's paint (Washes _strokeStrength)
let RECORD_EVERY = 15          // frames between simulation-record snapshots: changing page text every
                               // frame cost more restyle and layout than the simulation itself
let FLOW_STRENGTH = 0.12       // a held brush keeps releasing paint (Washes continuous flow)

pub let STUDIES = [
    {id:"sky",title:"Wet-in-wet sky",tag:"WET-IN-WET / SOFT EDGES",wetness:0.62,
     note:"Bands of cerulean, rose and yellow flood a wetted sheet and bleed softly into each other."},
    {id:"blossoms",title:"Blossoms",tag:"WET-ON-DRY / DARK RIMS",wetness:0.0,
     note:"Petals painted on dry paper; as each dries, water flows outward and leaves a darker rim."},
    {id:"glazes",title:"Glazes",tag:"LAYERS / OPTICAL MIXING",wetness:0.0,
     note:"Three transparent washes, each dried before the next; Kubelka-Munk mixes their overlaps."},
    {id:"blooms",title:"Blooms",tag:"BACKRUNS / CLEAR WATER",wetness:0.0,
     note:"Clear water dropped into a damp wash pushes the pigment outward into cauliflower blooms."},
    {id:"free",title:"Free painting",tag:"YOUR BRUSH",wetness:0.0,
     note:"A blank sheet: drag on the paper to paint. Wet it first for soft, wet-in-wet strokes."}]

pub fn study(id) map => [for (item in STUDIES where item.id == id) item][0]
pub fn palette(st) map => wc.PALETTES[st.palette]

// --- seeded stroke plans in visible-paper coordinates: every stamp carries the frame it lands on

// stamps along a polyline, `rate` per frame from frame `at`; returns [stamps, next free frame].
fn stroke(points, tool, radius: float, strength: float, at: int, rate: int) array^ {
    let path = [for (k in 1 to (len(points) - 1),
        p in wc.trail(points[k - 1][0], points[k - 1][1], points[k][0], points[k][1], radius)^) p]
    let lead = [[points[0][0], points[0][1]], *path]
    let stamps = [for (i, p in lead) {frame:at + i div rate,kind:tool.kind,channel:tool.channel,
        x:p[0] + float(MARGIN),y:p[1] + float(MARGIN),radius:radius,strength:strength}];
    [stamps, at + len(lead) div rate + 1]
}

// a sequence of strokes painted one after another, with `gap` frames between them.
fn sequence(strokes, at: int, gap: int) array^ {
    let first = if (len(strokes) == 0) [[], at] else stroke(strokes[0].points, strokes[0].tool,
        strokes[0].radius, strokes[0].strength, at, strokes[0].rate)^;
    if (len(strokes) <= 1) first
    else (let rest = sequence(slice(strokes, 1, len(strokes))^, first[1] + gap, gap)^, [[*first[0], *rest[0]], rest[1]])
}

// a brush held still for `count` frames lets its load pool in one place.
fn pool(x: float, y: float, radius: float, times: int, tool, at: int) array^ =>
    [for (k in 0 to (times - 1)) {frame:at + k,kind:tool.kind,channel:tool.channel,
        x:x + float(MARGIN),y:y + float(MARGIN),radius:radius,strength:1.0}]

fn pigment_tool(channel: int) map => TOOLS[channel]
fn action(frame: int, kind) map => {frame:frame,kind:kind}
fn ramp(v: float) float => if (v > 0.0) v else 0.0

// rows spaced at 0.6 of the brush radius, so neighbouring strokes merge into one wash
fn rows(y0: float, y1: float, x0: float, x1: float, radius: float, tint, rate: int, wobble) array^ {
    let lines = int(floor((y1 - y0) / (0.6 * radius))) + 1;
    [for (k in 0 to (lines - 1), let y = y0 + (y1 - y0) * float(k) / float(imax(1, lines - 1)), let c = tint(k, y)^
        where c.strength > 0.03)
        {tool:pigment_tool(c.channel),radius:radius,strength:c.strength,rate:rate,
         points:(let line = [[x0, y + wobble(k, 0)], [0.5 * (x0 + x1), y + wobble(k, 1)], [x1, y + wobble(k, 2)]],
             if (k % 2 == 0) line else reverse(line))}]
}
fn imax(a: int, b: int) int => if (a > b) a else b

// wet-in-wet sky: cerulean fading down into rose and a yellow horizon glow, brushed in overlapping
// rows on the wetted sheet, then clouds blotted out while it is still wet.
fn sky_plan(key) array^ {
    let wob = rng.units(rng.derive(key, "wobble"), 3 * 64)^
    let wobble = ((k: int, j: int) => (wob[(3 * k + j) % len(wob)] - 0.5) * 6.0)
    let h = float(HEIGHT)
    let blue = rows(2.0, 0.52 * h, -4.0, float(WIDTH) + 4.0, 15.0,
        ((k: int, y: float) => {channel:2,strength:0.12 * ramp(1.0 - y / (0.52 * h))}), 14, wobble)^
    let rose = rows(0.4 * h, 0.78 * h, -4.0, float(WIDTH) + 4.0, 14.0,
        ((k: int, y: float) => {channel:0,strength:0.06 * ramp(1.0 - abs(y / h - 0.6) / 0.18)}), 14, wobble)^
    let gold = rows(0.6 * h, h - 2.0, -4.0, float(WIDTH) + 4.0, 15.0,
        ((k: int, y: float) => {channel:1,strength:0.1 * ramp(1.0 - abs(y / h - 0.84) / 0.24)}), 14, wobble)^
    let wash = sequence([*blue, *rose, *gold], 0, 1)^
    let clouds = [for (c in 0 to 4, pass in 0 to 3, let u = rng.units(rng.derive(key, ["cloud", c]), 3)^)
        (let x = 30.0 + u[0] * 260.0, let y = 20.0 + u[1] * 0.28 * h, let span = 26.0 + u[2] * 34.0,
         {tool:TOOLS[4],radius:13.0,strength:1.0,rate:8,
          points:if (pass % 2 == 0) [[x, y], [x + span, y + 3.0]] else [[x + span, y + 4.0], [x + 4.0, y - 2.0]]})]
    let blotted = sequence(clouds, wash[1] + 10, 2)^;
    [*wash[0], *blotted[0]]
}

// flowers on a jittered layout: green stems and leaves (yellow, then blue while it is wet), then
// petals painted on dry paper so each dries with a darker rim: rose, violet (rose glazed with blue
// while wet) or gold, with a contrasting centre.
fn blossom_plan(key) array^ {
    let layout = [[75.0, 80.0], [180.0, 62.0], [285.0, 84.0], [122.0, 162.0], [240.0, 158.0]]
    let flowers = [for (i, at in layout) (let u = rng.units(rng.derive(key, ["flower", i]), 8)^,
        {x:at[0] + (u[0] - 0.5) * 36.0,y:at[1] + (u[1] - 0.5) * 24.0,petals:5 + int(floor(u[2] * 3.0))^,
         turn:u[3] * 6.2832,length:13.0 + u[4] * 5.0,lean:(u[5] - 0.5) * 1.4,leaf:u[6],kind:int(floor(u[7] * 3.0))^})]
    let green = ((points, radius: float) => [
        {tool:pigment_tool(1),radius:radius,strength:0.6,rate:12,points:points},
        {tool:pigment_tool(2),radius:radius,strength:0.45,rate:12,points:points}])
    let stems = [for (f in flowers, s in green([[f.x, f.y + 8.0], [f.x + f.lean * 12.0, f.y + 38.0],
        [f.x + f.lean * 20.0, min(float(HEIGHT) - 6.0, f.y + 72.0)]], 2.5)) s]
    let leaves = [for (f in flowers, side in [-1.0, 1.0], s in green((let y = f.y + 34.0 + 10.0 * f.leaf * side, let x = f.x + f.lean * 12.0,
        [[x, y], [x + side * 16.0, y - 8.0], [x + side * 30.0, y - 4.0]]), 6.0)) s]
    let colour = [[{ch:0,s:0.45}], [{ch:0,s:0.4}, {ch:2,s:0.3}], [{ch:1,s:0.6}]]
    let petals = [for (f in flowers, p in 0 to (f.petals - 1), tone in colour[f.kind],
        let a = f.turn + 6.2832 * float(p) / float(f.petals))
        {tool:pigment_tool(tone.ch),radius:8.5,strength:tone.s,rate:8,
         points:[[f.x + 4.0 * math.cos(a), f.y + 4.0 * math.sin(a)], [f.x + f.length * math.cos(a), f.y + f.length * math.sin(a)]]}]
    let painted = sequence([*stems, *leaves, *petals], 0, 0)^
    let centres = [for (i, f in flowers, stamp in pool(f.x, f.y, 5.0, 3, pigment_tool(if (f.kind == 2) 0 else 1), painted[1] + 4 * i)^) stamp];
    [*painted[0], *centres]
}

// horizontal strokes that fill a disc, alternating direction like a wash brushed back and forth.
fn disc_strokes(cx: float, cy: float, r: float, channel: int, strength: float) array =>
    [for (k in 0 to 17, let dy = -r + 2.0 * r * (float(k) + 0.5) / 18.0, let half = sqrt(r * r - dy * dy))
        {tool:pigment_tool(channel),radius:11.0,strength:strength,rate:10,
         points:if (k % 2 == 0) [[cx - half, cy + dy], [cx + half, cy + dy]] else [[cx + half, cy + dy], [cx - half, cy + dy]]}]

// three overlapping washes in the Venn arrangement, each left to darken its rim, then dried.
fn glaze_plan(key) array^ {
    let u = rng.units(rng.derive(key, "glaze"), 6)^
    let first = sequence(disc_strokes(135.0 + (u[0] - 0.5) * 20.0, 92.0 + (u[1] - 0.5) * 12.0, 62.0, 1, 0.38), 0, 0)^
    let second = sequence(disc_strokes(225.0 + (u[2] - 0.5) * 20.0, 92.0 + (u[3] - 0.5) * 12.0, 62.0, 0, 0.3), first[1] + 85, 0)^
    let third = sequence(disc_strokes(180.0 + (u[4] - 0.5) * 20.0, 162.0 + (u[5] - 0.5) * 12.0, 62.0, 2, 0.34), second[1] + 85, 0)^;
    [*first[0], action(first[1] + 75, "dry"), *second[0], action(second[1] + 75, "dry"), *third[0], action(third[1] + 75, "dry")]
}

// a violet wash, cerulean rows then rose rows over them, left to go damp (pigment suspended but no
// longer flowing); clear water pooled in seeded spots (twice a brush's load) then makes wet islands
// whose edges darken and whose fronts push the pigment outward into blooms.
fn wash_layer(channel: int, strength: float) array^ => rows(34.0, 206.0, 40.0, 320.0, 12.0,
    ((k: int, y: float) => {channel:channel,strength:strength}), 12, ((k: int, j: int) => 0.0))^
fn bloom_plan(key) array^ {
    let wash = sequence([*wash_layer(2, 0.16)^, *wash_layer(0, 0.1)^], 0, 0)^
    let damp = wash[1] + 40
    let drops = [for (d in 0 to 4, let u = rng.units(rng.derive(key, ["drop", d]), 3)^)
        [for (stamp in pool(80.0 + u[0] * 200.0, 70.0 + u[1] * 100.0, 14.0 + u[2] * 6.0, 10, TOOLS[3], damp + 4 + 10 * d)^)
            {*:stamp,load:2.0}]];
    [*wash[0], {*:action(damp, "dry"),level:0.035}, *[for (group in drops, stamp in group) stamp]]
}

pub fn plan(id, seed: int) array^ {
    let key = rng.derive(rng.key(seed), ["watercolor", id]);
    if (id == "sky") sky_plan(key)^ else if (id == "blossoms") blossom_plan(key)^
    else if (id == "glazes") glaze_plan(key)^ else if (id == "blooms") bloom_plan(key)^ else []
}

// --- state

fn begin(st, id, seed: int) map^ {
    let started = {*:st,id:id,seed:seed,plan:plan(id, seed)^,cursor:0,frame:0,pending:[],full:true,
        sheet:wc.blank(st.sheet, study(id).wetness)^};
    {*:started,record:metrics(started)}
}

pub fn initial() map^ {
    let base = {seed:2026,id:"sky",palette:0,tool:2,size:3,drying:1,paused:false,edges:true,tilt:false,water:false,
        sheet:wc.sheet(SHEET_WIDTH, SHEET_HEIGHT, PAPER_SEED, SCALE)^,rgba:fill(SHEET_WIDTH * SHEET_HEIGHT * 4, 1.0)^,swatches:[],
        drag:null,token:0,ms:0.0,plan:[],cursor:0,frame:0,pending:[],full:true,record:[]};
    begin(base, base.id, base.seed)^
}

pub fn settings(st) map => {evaporation:DRYING[st.drying].rate,drying:not st.paused,edges:st.edges,tilt:if (st.tilt) 1.0 else 0.0}

// button actions; a new seed, study or replay starts the study over on the same paper.
pub fn command(st, act) map^ {
    if (act == "next") begin(st, st.id, st.seed + 1)^
    else if (act == "previous") begin(st, st.id, max(0, st.seed - 1))^
    else if (act == "replay") begin(st, st.id, st.seed)^
    else if (any([for (item in STUDIES) item.id == act])) begin(st, act, st.seed)^
    else if (act == "clear") begin(st, "free", st.seed)^
    else if (any([for (t in TOOLS) t.id == act])) {*:st,tool:[for (i, t in TOOLS where t.id == act) i][0]}
    else if (act == "smaller") {*:st,size:max(1, st.size - 1)}
    else if (act == "larger") {*:st,size:min(len(SIZES), st.size + 1)}
    else if (act == "palette") {*:st,palette:(st.palette + 1) % len(wc.PALETTES),full:true}
    else if (act == "drying") {*:st,drying:(st.drying + 1) % len(DRYING)}
    else if (act == "pause") {*:st,paused:not st.paused}
    else if (act == "edges") {*:st,edges:not st.edges}
    else if (act == "tilt") {*:st,tilt:not st.tilt}
    else if (act == "show-water") {*:st,water:not st.water,full:true}
    else if (act == "wet" or act == "dry") {*:st,pending:[*st.pending, {kind:act}]}
    else st
}

// --- the paper under the pointer: a dragged brush lays stamps along its path

pub fn brush(st, x: float, y: float, strength: float = STROKE_STRENGTH) map {
    let tool = TOOLS[st.tool];
    {kind:tool.kind,channel:tool.channel,x:x,y:y,radius:SIZES[st.size - 1],strength:strength}
}

// grid coordinates of a pointer inside the canvas box.
pub fn cell(box, client_x, client_y) array =>
    [(client_x - box.x) / box.width * float(SHEET_WIDTH), (client_y - box.y) / box.height * float(SHEET_HEIGHT)]

pub fn press(st, x: float, y: float) map => {*:st,drag:{x:x,y:y,moved:true},pending:[*st.pending, brush(st, x, y)]}

pub fn move(st, x: float, y: float) map^ {
    if (st.drag == null) st
    else (let path = wc.trail(st.drag.x, st.drag.y, x, y, SIZES[st.size - 1])^,
        {*:st,drag:{x:x,y:y,moved:true},pending:[*st.pending, *[for (p in path) brush(st, p[0], p[1])]]})
}

pub fn release(st) map => {*:st,drag:null}

// the sheet still changes while a plan, a stroke, queued work or any water remains.
pub fn busy(st) bool^ => st.cursor < len(st.plan)^ or st.drag != null or len(st.pending)^ > 0 or st.full or
    st.sheet.x1 >= st.sheet.x0 or st.sheet.wet_cells > 0

pn perform(var sheet: Sheet, item) {
    if (item.kind == "wet") { wc.wet_sheet(sheet) }
    else if (item.kind == "dry") { wc.dry(sheet, if (item.level == null) 0.0 else float(item.level))^ }
    else { wc.stamp(sheet, item, 1.0, if (item.load == null) 1.0 else float(item.load))^ }
}

// one animation frame: the plan's due stamps, queued input and a held brush, then the time steps
// and a Kubelka-Munk render of the region that changed (all of it every 20th frame, so drying
// sheen elsewhere stays current).
pub pn advance(st) map^ {
    let began = clock()
    var sheet: Sheet = st.sheet
    var rgba: float[] = st.rgba
    let pigments = palette(st).pigments
    let adsorption: float[] = wc.adsorption(pigments)^
    let optics: float[] = wc.optics(pigments)^
    // the plan is ordered by frame, so the due stamps lead what remains of it
    let due = [for (item in slice(st.plan, st.cursor, len(st.plan)^)^ where item.frame <= st.frame) item]
    for (item in [*due, *st.pending]) { perform(sheet, item)^ }
    if (st.drag != null and not st.drag.moved) { wc.stamp(sheet, brush(st, st.drag.x, st.drag.y, FLOW_STRENGTH))^ }
    var k: int = 0
    while (k < STEPS_PER_FRAME) {
        wc.step(sheet, adsorption^, settings(st))^
        k = k + 1
    }
    wc.render(rgba, sheet, optics^, st.full or st.frame % 20 == 0, st.water)^
    let swatches = if (st.full or len(st.swatches) == 0) wc.swatches(pigments)^ else st.swatches
    let next = {*:st,sheet:sheet,rgba:rgba,cursor:st.cursor + len(due),frame:st.frame + 1,pending:[],full:false,swatches:swatches,
        drag:if (st.drag == null) null else {*:st.drag,moved:false},ms:(clock() - began) * 1000.0}
    // the record refreshes periodically and once more when the sheet comes to rest
    return if (next.frame % RECORD_EVERY == 0 or not busy(next)^) {*:next,record:metrics(next)} else next
}

// --- presentation data

pub fn metrics(st) array {
    let m = wc.measure(st.sheet);
    [{label:"Sheet",value:string(WIDTH) ++ " × " ++ string(HEIGHT) ++ " cells"},
     {label:"Time steps",value:m.steps},
     {label:"Wet paper",value:string(int(round(m.wet * 100.0))) ++ "%"},
     {label:"Active region",value:if (m.active[0] == 0) "at rest" else string(m.active[0]) ++ " × " ++ string(m.active[1])},
     {label:"Pigment settled",value:string(int(round(100.0 * m.deposited / max(0.001, m.deposited + m.suspended)))) ++ "%"},
     {label:"Frame",value:string(int(round(st.ms))) ++ " ms"}]
}

pub fn tool_title(st, i: int) string^ => if (i < 3) palette(st)^.pigments[i].name else TOOLS[i].title
// the brush button shows the pigment's last word: Rose, Yellow, Blue, Sienna, Ochre, Indigo.
pub fn tool_label(st, i: int) string^ => if (i >= 3) TOOLS[i].label else (let words = split(tool_title(st, i)^, " "), words[len(words) - 1])
