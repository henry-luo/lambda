// Keeps the page in step with the scene clock. Everything that changes while the gear plays is
// written through the presentation channel (transform, opacity), which Radiant commits without
// layout, so a frame costs paint only. Text is rebuilt only when the user acts (a sequence switch).
import dom
import scene_dom: lambda.scene3d.dom
import rig: .mod_rig
import cam: .mod_camera
import sc: .mod_scene
import ui: .mod_ui

pub fn node(owner, id) => dom.get_element_by_id(dom.root_node(owner), id)
fn fixed(value, places) {
    let factor = 10 ** places
    let whole = int(round(abs(value) * factor))
    let fraction = string(whole % factor);
    (if (value < 0 and whole > 0) "-" else "") ++ string(whole div factor) ++ "." ++
        join(fill(max(0, places - len(fraction)), "0"), "") ++ fraction
}
fn px(x) => fixed(x, 1) ++ "px"
fn deg(a) => fixed(a * 180.0 / rig.PI, 1) ++ "deg"
pn paint(target, property, value) { dom.presentation_style_set_property(target, property, value) }
pn show(owner, id, visible) { paint(node(owner, id), "opacity", if (visible) "1" else "0") }

pub fn sequence(st) => rig.SEQUENCES[st.seq]
pub fn locked(g) => g < 0.0005 or g > rig.PI - 0.0005
fn status(g) => if (g > rig.PI - 0.0005) 0 else if (g < 0.0005) 2 else 1
// the flight trace: tyre height across the whole sequence
pub fn trace(seq) => join([for (i in 0 to 48, let at = seq.duration * i / 48.0)
    fixed(6.0 + 174.0 * i / 48.0, 1) ++ "," ++ fixed(48.0 - 42.0 * rig.moment(seq, at).height / 1.7, 1)], " ")
pub fn speed_fraction(speed) => (speed - 0.25) / 1.75

// odometer: digit cells hold the characters "#" stands for in the pattern; blanks lead short numbers
pn digits(owner, id, pattern, text) {
    let cells = split(pattern, "")
    let chars = split(text, "")
    let padded = [*fill(max(0, len(cells) - len(chars)), " "), *chars]
    for (i, ch in cells where ch == "#") {
        let c = padded[i]
        let row = if (c == " ") 10 else int(c)
        for (k in 0 to 10) show(owner, id ++ "-" ++ string(i) ++ "-" ++ string(k), k == row)
    }
}
fn two(n) => (if (n < 10) "0" else "") ++ string(n)
fn whole(x) => string(int(round(x)))

pn panels(owner, st, m) {
    let seq = sequence(st)
    let g = m.gear
    let current_state = status(g)
    for (i in 0 to 2) show(owner, "chip-" ++ string(i), i == current_state)
    show(owner, "swatch-locked", locked(g))
    show(owner, "note-air", m.height > 0.005)
    show(owner, "note-ground", m.height <= 0.005)
    digits(owner, "note-height", "#.##", fixed(m.height, 2))
    // schematic: the rod strokes with the rack, the pinion turns, the legs sweep through 180°
    paint(node(owner, "schematic-rod"), "transform", "translateX(" ++ px(20.0 * m.stroke / rig.STROKE) ++ ")")
    paint(node(owner, "schematic-pinion"), "transform", "rotate(" ++ deg(g) ++ ")")
    paint(node(owner, "schematic-leg-l"), "transform", "rotate(" ++ deg(g) ++ ")")
    paint(node(owner, "schematic-leg-r"), "transform", "rotate(" ++ deg(0.0 - g) ++ ")")
    // flight: the marker rides the trace, pointing along it
    let ahead = rig.moment(seq, m.sec + 0.05)
    let slope = math.atan2(42.0 * (m.height - ahead.height) / 1.7, 174.0 * 0.05 / seq.duration)
    paint(node(owner, "flight-marker"), "transform", "translate(" ++ px(6.0 + 174.0 * m.sec / seq.duration) ++ ", " ++
        px(48.0 - 42.0 * m.height / 1.7) ++ ") rotate(" ++ deg(slope) ++ ")")
    for (row in [["height", m.height / 1.7, "#.##", fixed(m.height, 2)], ["gear", g / rig.PI, "###", whole(g * 180.0 / rig.PI)],
            ["stroke", m.stroke / rig.STROKE, "###", whole(m.stroke * 1000.0)]]) {
        paint(node(owner, "fill-" ++ row[0]), "transform", "scaleX(" ++ fixed(rig.clamp(row[1], 0.0, 1.0), 4) ++ ")")
        digits(owner, "value-" ++ row[0], row[2], row[3])
    }
    paint(node(owner, "wheel-icon"), "transform", "rotate(" ++ deg(0.0 - m.roll) ++ ")")
    // the operation bar
    let phase = [for (i, ph in seq.phases where m.sec < ph.until) i]
    let current = if (len(phase) > 0) phase[0] else len(seq.phases) - 1
    for (name in ui.ORDER, i, ph in rig.SEQUENCES[name].phases) show(owner, "phase-" ++ name ++ "-" ++ string(i), name == st.seq and i == current)
    digits(owner, "clock-now", "##", two(int(floor(m.sec))))
    slider(owner, "scrub", m.sec / seq.duration)
    slider(owner, "speed", speed_fraction(st.speed))
    digits(owner, "speed-value", "#.##", fixed(st.speed, 2))
    show(owner, "play-glyph-0", st.playing)
    show(owner, "play-glyph-1", not st.playing)
}
pn slider(owner, id, fraction) {
    let f = rig.clamp(fraction, 0.0, 1.0)
    paint(node(owner, id ++ "-fill"), "transform", "scaleX(" ++ fixed(f, 4) ++ ")")
    paint(node(owner, id ++ "-carrier"), "transform", "translateX(" ++ fixed(f * 100.0, 2) ++ "%)")
}

// camera attributes are the one scene edit made per frame, and only while the camera moves
pn camera(owner, st) {
    let target = node(owner, "cam")
    scene_dom.attr(target, "position", cam.position(st.camera))
    scene_dom.attr(target, "target", st.camera.target)
    scene_dom.attr(target, "fov", st.camera.fov)
}
// callouts: each anchor follows its part's pose and explode offset, projected into stage pixels
pn callouts(owner, st, m) {
    let pose = rig.pose(m.gear, m.flex, m.roll)
    let lens = cam.frame(st.camera)
    let compact = st.stage.width <= 800.0
    let lefts = [for (k in ui.CALLOUTS where k.side == "left") k.id]
    let rights = [for (k in ui.CALLOUTS where k.side == "right") k.id]
    for (k in ui.CALLOUTS) {
        let left = k.side == "left"
        let row = [for (i, id in (if (left) lefts else rights) where id == k.id) i][0]
        let x = if (compact) (if (left) 8.0 else st.stage.width - 158.0) else if (left) 252.0 else st.stage.width - 430.0
        let y = (if (left) st.stage.height * 0.40 else st.stage.height * 0.14) + (if (compact) 32.0 else 44.0) * row
        let part = if (k.part == null) k.anchor else rig.place(pose[k.part], k.anchor)
        let world = rig.add(part, rig.scale(sc.EXPLODE[k.wrap], st.explode))
        let point = cam.screen(lens, st.stage, world)
        let reach = 7.0 * max(len(k.title), 0.82 * len(k.note)) + 18.0
        let start = [if (left) x + reach else x - 8.0, y + 6.0]
        let tip = if (point == null) start else point
        let dx = tip[0] - start[0]
        let dy = tip[1] - start[1]
        paint(node(owner, "callout-text-" ++ k.id), "transform", "translate(" ++ px(x) ++ ", " ++ px(y) ++ ")")
        paint(node(owner, "callout-line-" ++ k.id), "transform", "translate(" ++ px(start[0]) ++ ", " ++ px(start[1]) ++ ") rotate(" ++
            deg(math.atan2(dy, dx)) ++ ") scaleX(" ++ fixed(sqrt(dx * dx + dy * dy) / 100.0, 4) ++ ")")
        paint(node(owner, "callout-dot-" ++ k.id), "transform", "translate(" ++ px(tip[0]) ++ ", " ++ px(tip[1]) ++ ")")
    }
}

// display modes edit the scene once per change
pub pn display(owner, st) {
    let hidden = sc.section_hidden(st.display.section)
    for (id in ["plate-near", "barrel", "barrel-cut"]) scene_dom.attr(node(owner, id), "visible", not contains(hidden, id))
    scene_dom.attr(node(owner, "m-plate"), "opacity", sc.plate_material(st.display.xray).opacity)
    scene_dom.attr(node(owner, "fuselage"), "visible", st.display.fuselage)
    for (id, offset in sc.EXPLODE) scene_dom.attr(node(owner, string(id)), "position", rig.scale(offset, st.explode))
    show(owner, "callouts", st.display.labels)
    for (t in ui.TOGGLES) scene_dom.attr(node(owner, "toggle-" ++ t.id), "aria-pressed", st.display[t.id])
}
// a sequence switch: the words and the flight trace that belong to the sequence
pub pn retitle(owner, st) {
    for (i, name in ui.ORDER) for (id in ["start", "finish", "total"]) show(owner, id ++ "-" ++ string(i), name == st.seq)
    scene_dom.attr(node(owner, "flight-trace"), "points", trace(sequence(st)))
    for (name in ["takeoff", "landing"]) scene_dom.attr(node(owner, "seq-" ++ name), "aria-pressed", st.seq == name)
}
pub pn sync(owner, st) {
    let m = rig.moment(sequence(st), st.at)
    panels(owner, st, m)
    camera(owner, st)
    if (st.display.labels) callouts(owner, st, m)
    for (v in cam.VIEWS) scene_dom.attr(node(owner, "view-" ++ v.id), "aria-pressed", st.view == v.id)
    scene_dom.attr(node(owner, "toggle-orbit"), "aria-pressed", st.display.orbit)
    scene_dom.attr(node(owner, "play"), "aria-pressed", st.playing)
    scene_dom.attr(node(owner, "scrub"), "aria-valuemin", 0)
    scene_dom.attr(node(owner, "scrub"), "aria-valuemax", sequence(st).duration)
    scene_dom.attr(node(owner, "scrub"), "aria-valuenow", fixed(st.at, 2))
    scene_dom.attr(node(owner, "speed"), "aria-valuemin", 0.25)
    scene_dom.attr(node(owner, "speed"), "aria-valuemax", 2.0)
    scene_dom.attr(node(owner, "speed"), "aria-valuenow", fixed(st.speed, 2))
}
