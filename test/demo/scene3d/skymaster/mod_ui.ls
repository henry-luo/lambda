// The page: masthead, the stage with its callouts, instrument panels and the gear operation bar.
// Built once. Everything that moves while the gear plays is an HTML box that mod_present moves with
// presentation transforms or opacity, which Radiant commits without layout; text changes only when
// the user acts.
import navigation: ~~._navigation
import rig: .mod_rig
import cam: .mod_camera
import sc: .mod_scene

// labelled parts: where each callout points, in the part's own (gear-down) coordinates
pub let CALLOUTS = [
    {id: "rod-end", side: "left", title: "ROD-END CONNECTOR", note: "RUNS IN THE PLATE SLOT", part: "rod", wrap: "x-actuator", anchor: [-0.055, 0.105, 0.0]},
    {id: "shaft", side: "left", title: "ROTARY OUTPUT SHAFT", note: "DRIVES BOTH SIDES", part: "pinion", wrap: "x-gearbox", anchor: [0.0, -0.16, -0.16]},
    {id: "pinion", side: "left", title: "PINION", note: "32 TEETH, 4 MM MODULE", part: "pinion", wrap: "x-gearbox", anchor: [0.0, -0.16, 0.021]},
    {id: "actuator", side: "right", title: "HYDRAULIC ACTUATOR", note: "EXTENDS / RETRACTS THE GEAR", part: null, wrap: "x-actuator", anchor: [0.4, 0.127, 0.0]},
    {id: "plate", side: "right", title: "MAIN PLATE", note: "AIRFRAME ATTACHMENT", part: null, wrap: "x-plate", anchor: [0.7, 0.0, 0.42]},
    {id: "rack", side: "right", title: "GEAR RACK", note: "ONE RACK, FORE AND AFT", part: "rack", wrap: "x-rack", anchor: [0.22, -0.07, 0.018]},
    {id: "leg", side: "right", title: "SPRING LEG", note: "STEEL, FLEXES ON TOUCHDOWN", part: "leg-r", wrap: "x-right", anchor: rig.add(rig.AXLE_END, [-0.02, 0.24, -0.06])},
    {id: "clevis", side: "right", title: "CLEVIS JOINT", note: "TRANSFERS ROTATION ONLY", part: "side-r", wrap: "x-right", anchor: rig.add(rig.JOINT, rig.scale(rig.AXIS, 0.047))},
    {id: "tube", side: "right", title: "SIDE TUBE", note: "SPINS ABOUT ITS AXIS", part: "side-r", wrap: "x-right", anchor: rig.add(rig.JOINT, rig.scale(rig.AXIS, 0.19))}]

pub let TOGGLES = [{id: "labels", label: "LABELS"}, {id: "section", label: "SECTION"}, {id: "xray", label: "X-RAY PLATE"},
    {id: "exploded", label: "EXPLODED"}, {id: "fuselage", label: "FUSELAGE"}, {id: "orbit", label: "ORBIT"}]
pub let STATES = [{id: "up", text: "Gear up & locked"}, {id: "transit", text: "In transit"}, {id: "down", text: "Gear down & locked"}]

fn head(title, index) element => <div class: "panel-head", <span class: "title", title> <span class: "index", index>>
fn pressed(on) => if (on) "true" else "false"
// a stack of alternatives in one spot; mod_present shows one by opacity
fn stack(id, items, shown: any) element => <span class: "stack", id: id,
    for (i, text in items) <span id: id ++ "-" ++ string(i), class: "stack-item", style: if (i == shown) "opacity:1" else "opacity:0", text>>
// odometer digits: each cell stacks 0-9 and a blank and shows one by opacity, so numbers never relayout
fn digit(id) element => <span class: "digit", for (k in 0 to 10)
    <span id: id ++ "-" ++ string(k), class: "glyph", style: if (k == 10) "opacity:1" else "opacity:0", if (k < 10) string(k) else " ">>
fn odometer(id, pattern) element => <span class: "odometer", for (i, ch in split(pattern, "")) if (ch == "#") digit(id ++ "-" ++ string(i)) else <span ch>>

fn callout(k) element => <div id: "callout-" ++ k.id, class: "callout",
    <div id: "callout-line-" ++ k.id, class: "callout-line">
    <div id: "callout-dot-" ++ k.id, class: "callout-dot">
    <div id: "callout-text-" ++ k.id, class: "callout-text", <span class: "callout-mark">
        <div class: "callout-title", k.title> <div class: "callout-note", k.note>>>

fn stage(seq, display, camera) element => <div class: "stage", id: "stage",
    sc.scene(seq, display, {fov: camera.fov, target: camera.target, position: cam.position(camera)});
    <div id: "callouts", class: "callouts", 'aria-hidden': "true", style: if (display.labels) "opacity:1" else "opacity:0",
        for (k in CALLOUTS) callout(k)>
    <div class: "scene-note", style: "left:300px;bottom:150px", "DRAG TO ORBIT" <br> "SCROLL TO ZOOM · R TO RESET">
    <div class: "scene-note fwd", style: "left:46%;bottom:150px", "———→" <br> "FWD">
    <div class: "scene-note", style: "right:300px;bottom:142px", "////// MAIN LANDING GEAR " <span class: "big", "337"> <br> "REV B">>

fn masthead() element => <header class: "masthead",
    <div class: "brand", <span class: "slashes", "///"> <span class: "model", "337">>
    <div class: "titles", <p class: "eyebrow", "GENERAL AVIATION">
        <h1 "Cessna 337 Skymaster " <span class: "slash", "/">>
        <p class: "subtitle", "MAIN LANDING GEAR / RETRACTION MECHANISM">>
    navigation.breadcrumbs("Skymaster gear", "../index.html");
    <div class: "plus-grid", 'aria-hidden': "true", for (r in 0 to 2) <div "+ + + + + + + + +">>
    <div class: "badge", <div class: "engines", "TWIN ENGINE · CENTRELINE THRUST " <span class: "hatch", "/////">>
        <div class: "number", "337"> <div class: "stack-note", "PUSH" <br> "PULL" <br> "PROFILE">>>

// the mechanism diagram: a fixed drawing with the rod, pinion and legs laid over it as moving boxes
fn schematic() element => <div class: "schematic",
    <svg class: "schematic-ground", width: "186", height: "96", viewBox: "0 0 186 96",
        <text x: "4", y: "8", "ACTUATOR">
        <rect x: "4", y: "13", width: "34", height: "11", rx: "5.5", fill: "none", stroke: "#3a342c", 'stroke-width': "1.2">
        <text x: "4", y: "45", "RACK"> <text x: "86", y: "45", "PINION">
        <line x1: "70", y1: "56", x2: "70", y2: "94", stroke: "#a99d8b", 'stroke-width': "1", 'stroke-dasharray': "2 2">
        <text x: "50", y: "68", "SHAFT">
        <line x1: "24", y1: "78", x2: "116", y2: "78", stroke: "#e4532c", 'stroke-width': "1.6">
        <circle cx: "70", cy: "78", r: "2.4", fill: "#e4532c">
        <text x: "130", y: "81", "LEG">>
    <div id: "schematic-rod", class: "schematic-rod",
        <svg width: "120", height: "40", viewBox: "0 0 120 40",
            <line x1: "38", y1: "18.5", x2: "60", y2: "18.5", stroke: "#3a342c", 'stroke-width': "1.2">
            <rect x: "58", y: "13", width: "5", height: "18", fill: "#e4532c">
            <line x1: "2", y1: "31", x2: "96", y2: "31", stroke: "#3a342c", 'stroke-width': "1.2">
            for (k in 0 to 15) <line x1: string(4 + 6 * k), y1: "31", x2: string(4 + 6 * k), y2: "34", stroke: "#3a342c", 'stroke-width': "1">>>
    <div id: "schematic-pinion", class: "schematic-pinion",
        <svg width: "22", height: "22", viewBox: "-11 -11 22 22",
            <circle r: "10", fill: "#fbf7f0", stroke: "#3a342c", 'stroke-width': "1.2">
            <line x1: "-7", y1: "-7", x2: "7", y2: "7", stroke: "#3a342c", 'stroke-width': "1">
            <line x1: "7", y1: "-7", x2: "-7", y2: "7", stroke: "#3a342c", 'stroke-width': "1">>>
    <div id: "schematic-leg-l", class: "schematic-leg", style: "left:23px", <span class: "schematic-foot">>
    <div id: "schematic-leg-r", class: "schematic-leg", style: "left:115px", <span class: "schematic-foot">>>

fn readout(id, name, pattern, unit) element => <div class: "readout", <span class: "name", name>
    <div class: "track", <div id: "fill-" ++ id, class: "fill">> <span class: "value", odometer("value-" ++ id, pattern); unit>>

fn left_rail() element => <aside class: "rail left",
    <section class: "panel", 'aria-label': "Mechanism state", head("MECHANISM STATE", "01");
        <div class: "chip", <span class: "chip-swatch", <span id: "swatch-locked", class: "swatch-locked">>
            <div class: "chip-copy", stack("chip", [for (s in STATES) s.text], 0);
                <div class: "chip-note", <span id: "note-air", "AIRBORNE · "; odometer("note-height", "#.##"); " M">
                    <span id: "note-ground", class: "note-ground", style: "opacity:0", "ON THE RUNWAY">>>>
        schematic()>
    <section class: "panel", 'aria-label': "Flight", head("FLIGHT", "02");
        <div class: "flight",
            <svg class: "flight-path", width: "186", height: "54", viewBox: "0 0 186 54",
                <polyline id: "flight-trace", points: "", fill: "none", stroke: "#e4532c", 'stroke-width': "1", 'stroke-dasharray': "2 3">
                <line x1: "0", y1: "48", x2: "186", y2: "48", stroke: "#3a342c", 'stroke-width': "1">>
            <div id: "flight-marker", class: "flight-marker",
                <svg width: "12", height: "10", viewBox: "-6 -5 12 10", <polygon points: "-5,-4 5,0 -5,4", fill: "#e4532c">>>>
        <div class: "readouts", readout("height", "HEIGHT", "#.##", " m"); readout("gear", "GEAR", "###", "°");
            readout("stroke", "STROKE", "###", " mm")>
        <div class: "wheel-row", <span "WHEEL">
            <div id: "wheel-icon", class: "wheel-icon",
                <svg width: "16", height: "16", viewBox: "-8 -8 16 16",
                    <circle r: "6.5", fill: "none", stroke: "#4c6d96", 'stroke-width': "1.2">
                    for (a in [90, 210, 330]) <line x1: "0", y1: "0", x2: string(6.0 * cos(a * rig.PI / 180.0)), y2: string(6.0 * sin(a * rig.PI / 180.0)),
                        stroke: "#4c6d96", 'stroke-width': "1.2">>>>>>

fn right_rail(seq, display, view_id) element => <aside class: "rail right",
    <section class: "panel", 'aria-label': "System overview", head("SYSTEM OVERVIEW", "03");
        <p class: "overview-title", "MAIN LANDING GEAR">
        <p class: "overview-copy", "One hydraulic actuator, a single rack and pinion, and a universal joint at each end of the output shaft. The side tubes spin; they never swing.">
        <div class: "stats", for (st in [["1", "ACTUATOR"], ["2", "LEGS"], ["180°", "SWEEP"]])
            <div class: "stat", <div class: "figure", st[0]> <div class: "unit", st[1]>>>>
    <section class: "panel", 'aria-label': "Sequence", head("SEQUENCE", "04");
        <div class: "segment", role: "group", for (name in ["takeoff", "landing"])
            <button id: "seq-" ++ name, type: "button", 'data-act': "seq", 'data-arg': name, 'aria-pressed': pressed(seq.name == name), upper(name)>>>
    <section class: "panel", 'aria-label': "Views", head("VIEWS", "05");
        <div class: "buttons", for (v in cam.VIEWS)
            <button id: "view-" ++ v.id, class: "view-button", type: "button", 'data-act': "view", 'data-arg': v.id, 'aria-pressed': pressed(view_id == v.id), v.label>>>
    <section class: "panel", 'aria-label': "Display", head("DISPLAY", "06");
        <div class: "buttons", for (t in TOGGLES)
            <button id: "toggle-" ++ t.id, class: "toggle", type: "button", 'data-act': "display", 'data-arg': t.id, 'aria-pressed': pressed(display[t.id]), t.label>>>>

fn slider(id, label) element => <div id: id, class: "slider", 'data-slider': id, role: "slider", 'aria-label': label, tabindex: "0",
    <div class: "rail-line"> <div id: id ++ "-fill", class: "rail-fill"> <div id: id ++ "-carrier", class: "knob-carrier", <span class: "knob">>>

// both sequences' words share the bar; mod_present shows the active sequence's set
pub let ORDER = ["landing", "takeoff"]
fn per_sequence(id, seq, text) element => (let shown = [for (i, name in ORDER where name == seq.name) i][0],
    stack(id, [for (name in ORDER) text(rig.SEQUENCES[name])], shown))
fn operation(seq) element => <section class: "panel operation", 'aria-label': "Gear operation", head("GEAR OPERATION", "07");
    <div class: "operation-body",
        <button id: "play", class: "play", type: "button", 'data-act': "play", 'aria-label': "Play or pause", stack("play-glyph", ["❚❚", "▶"], 0)>
        <div class: "timeline",
            <div class: "timeline-labels", per_sequence("start", seq, (q) => q.start);
                <span class: "phase", <span class: "stack", for (name in ORDER, i, ph in rig.SEQUENCES[name].phases)
                    <span id: "phase-" ++ name ++ "-" ++ string(i), class: "stack-item", style: "opacity:0", ph.caption>>>
                <span class: "finish", per_sequence("finish", seq, (q) => q.finish)>>
            slider("scrub", "Sequence position");
            <div class: "clock", "00:"; odometer("clock-now", "##"); " / 00:0"; per_sequence("total", seq, (q) => string(int(q.duration)))>>
        <div class: "speed", <div class: "timeline-labels", <span "SPEED">>
            <div class: "speed-row", slider("speed", "Playback speed"); <span class: "speed-value", odometer("speed-value", "#.##"); "×">>>>>

pub fn page(seq, display, view_id, camera) element => <main id: "skymaster", class: "app",
    stage(seq, display, camera);
    masthead();
    left_rail();
    right_rail(seq, display, view_id);
    operation(seq);
    <footer class: "colophon", <span "MECHANISM STUDY  ·  PROCEDURAL MODEL  ·  SINGLE DEGREE OF FREEDOM">
        <a href: "https://x.com/0x_mura/status/2116209868331987001", "REFERENCE / MURA " <span class: "hatch", "//////">>>>
