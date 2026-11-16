// HUD markup: location, minimap, courier panel, speedometer, controls, touch pedals, help and the
// title card. Everything that changes every frame is a stack shown by opacity or an element moved by
// transform, so a frame repaints without relayout; words change only when the situation does.
import u: .mod_util
import city: .mod_city
import cam: .mod_camera
import sc: .mod_scene
import land: .mod_land
import roads: .mod_roads
import media: lambda.doc.media

// a readout cell stacks 0-9 and a blank, showing one at a time
pub let GLYPHS = "0123456789 "
fn cell(id) element => <span class: "cell", *[for (k in 0 to 10)
    <span id: id ++ "-" ++ string(k), class: "glyph", style: if (k == 10) "opacity:1" else "opacity:0", *[slice(GLYPHS, k, k + 1)]>]>
pub fn readout(id, width) element => <span class: "readout", id: id, *[for (i in 0 to (width - 1)) cell(id ++ "-" ++ string(i))]>
pub let GEARS = ["R", "N", "1", "2", "3", "4", "5"]

// ---- the minimap: a static city plan at one pixel per metre (x east, z south), turned and slid
// under the fixed car arrow by one transform on its holder
pub let MAP_RADIUS = 96.0
pub let MAP_SCALE = 0.62
let PLAN_HALF = city.SHORE + 30.0
fn kind_fill(kind) => if (kind == "P") "#4d7a45" else if (kind == "X") "#8a7967" else if (kind == "S") "#4a4c52"
    else if (kind == "C") "#8c7e63" else "#c8c3b8"
fn plan() element {
    let l = city.LIMIT, w = PLAN_HALF
    let blocks = [for (b in city.BLOCKS) <rect x: b.x0 + city.ROAD, y: b.z0 + city.ROAD, width: city.PITCH - 2.0 * city.ROAD,
        height: city.PITCH - 2.0 * city.ROAD, rx: 2, fill: kind_fill(b.kind)>]
    let towers = [for (b in city.BUILDINGS) <rect x: b.x0, y: b.z0, width: b.x1 - b.x0, height: b.z1 - b.z0,
        fill: if (b.kind == "T") "#6f7f92" else "#9a958b">];
    <svg class: "map-plan", width: 2.0 * w, height: 2.0 * w, viewBox: string(0.0 - w) ++ " " ++ string(0.0 - w) ++ " " ++ string(2.0 * w) ++ " " ++ string(2.0 * w),
        style: "left:" ++ string(0.0 - w) ++ "px;top:" ++ string(0.0 - w) ++ "px", *[
        <rect x: 0.0 - w, y: 0.0 - w, width: 2.0 * w, height: 2.0 * w, fill: "#34502f">,
        <rect x: 0.0 - w, y: city.SHORE, width: 2.0 * w, height: w - city.SHORE + w, fill: "#2e5d78">,
        <rect x: 0.0 - l, y: 0.0 - l, width: 2.0 * l, height: 2.0 * l, fill: "#5a5f66">, *blocks, *towers]>
}
// the land beyond the city at the same scale: sea, river, the loop road (darker through the tunnel)
// and the roundabout; it sits under the city plan. It is an image of an SVG, so Radiant rasterises it
// once and only turns the bitmap each frame (as inline SVG it was redrawn every frame, ~2 ms)
let REGION = {x0: -320.0, x1: 1620.0, z0: -460.0, z1: 460.0}
fn pt(x, z) any^ => string(round(x)) ++ " " ++ string(round(z))
fn outline(points) any^ => join([for (k, p in points) (if (k == 0) "M " else "L ") ++ pt(p[0], p[1])], " ") ++ " Z"
// a band `half` wide either side of a run of road samples (every third), as one closed outline
fn band(pts, half) any^ => (let picked = [for (k, p in pts where k % 3 == 0 or k == len(pts) - 1) p],
    outline([*[for (p in picked, let r = u.right(p.heading)) [p.x + r[0] * half, p.z + r[1] * half]],
        *[for (p in reverse(picked), let r = u.right(p.heading)) [p.x - r[0] * half, p.z - r[1] * half]]]))
fn river_band() any^ {
    let pts = land.RIVER
    let n = len(pts)
    let side = (k, sgn) any^ => (let a = pts[max(0, k - 1)], let b = pts[min(n - 1, k + 1)], let dx = b[0] - a[0], let dz = b[1] - a[1],
        let l = sqrt(dx * dx + dz * dz), let w = land.river_half(pts[k][1]) * sgn, [pts[k][0] - dz / l * w, pts[k][1] + dx / l * w]);
    outline([*[for (k in 0 to (n - 1)) side(k, 1.0)], *[for (k in 0 to (n - 1)) side(n - 1 - k, -1.0)]])
}
fn region_svg() element^ {
    let r = REGION
    let coast = [for (k in 0 to 48, let x = r.x0 + k * 40.0) [x, land.coast_z(x)]]
    let loop = roads.LOOP.samples^
    let bores = [for (st in roads.STRUCTURES where st.kind == "tunnel") [for (p in loop where p.s >= st.a and p.s <= st.b) p]];
    <svg xmlns: "http://www.w3.org/2000/svg", width: r.x1 - r.x0, height: r.z1 - r.z0,
        viewBox: string(r.x0) ++ " " ++ string(r.z0) ++ " " ++ string(r.x1 - r.x0) ++ " " ++ string(r.z1 - r.z0), *[
        <rect x: r.x0, y: r.z0, width: r.x1 - r.x0, height: r.z1 - r.z0, fill: "#3c5a33">,
        <path d: outline([*coast, [r.x1, r.z1], [r.x0, r.z1]]), fill: "#2e5d78">, <path d: river_band(), fill: "#2e5d78">,
        <path d: band(loop, 6.5), fill: "#2b2f35">, <path d: band(loop, 4.0), fill: "#d6d0c2">,
        *[for (b in bores) <path d: band(b, 4.0), fill: "#5d6168">],
        <rect x: city.LIMIT, y: -6.0, width: roads.R1.x - city.LIMIT, height: 12.0, fill: "#d6d0c2">,
        <circle cx: roads.R1.x, cy: roads.R1.z, r: roads.R1.ring + 5.0, fill: "#d6d0c2">,
        <circle cx: roads.R1.x, cy: roads.R1.z, r: roads.R1.island, fill: "#4d7a45">]>
}
fn region_plan() element^ => (let r = REGION, <img class: "map-plan", alt: "", width: r.x1 - r.x0, height: r.z1 - r.z0,
    src: media.data_uri(binary(format(region_svg()^, 'xml')^), "image/svg+xml")^,
    style: "left:" ++ string(r.x0) ++ "px;top:" ++ string(r.z0) ++ "px;width:" ++ string(r.x1 - r.x0) ++ "px;height:" ++ string(r.z1 - r.z0) ++ "px">)
fn minimap() element => <div class: "map-disc", *[
    <div id: "map-world", class: "map-world", *[*(let rp = region_plan() ^ { null }, if (rp == null) [] else [rp]), plan(),
        <div id: "map-target", class: "map-target", style: "opacity:0", *[<div class: "map-target-ring">]>]>,
    <div id: "map-pointer", class: "map-pointer", style: "opacity:0", *[<div class: "map-pointer-tip">]>,
    <div id: "map-north", class: "map-north", *[<span "N">]>,
    <svg class: "map-car", viewBox: "-10 -10 20 20", width: 20, height: 20, <path d: "M 0 -8 L 6 7 L 0 3.5 L -6 7 Z", fill: "#ff5a4f", stroke: "#fff", 'stroke-width': 1.2>>]>

// ---- the speedometer: 0-220 km/h over 240°, a needle, rev bar, gear and damage
fn polar(r, deg) => (let a = (deg - 90.0) * u.PI / 180.0, [r * cos(a), r * sin(a)])
fn speedometer() element => <div class: "dial-wrap", *[<svg class: "dial", viewBox: "-100 -100 200 200", width: 196, height: 196, 'aria-hidden': "true", *[
    <circle cx: 0, cy: 0, r: 92, fill: "rgba(12,16,24,0.82)", stroke: "rgba(255,255,255,0.18)", 'stroke-width': 2>,
    *[for (k in 0 to 22, let deg = -120.0 + 240.0 * k / 22.0, let a = polar(if (k % 2 == 0) 70.0 else 76.0, deg), let b = polar(82.0, deg))
        <line x1: a[0], y1: a[1], x2: b[0], y2: b[1], stroke: if (k >= 18) "#ff6b5b" else "rgba(255,255,255,0.8)", 'stroke-width': if (k % 2 == 0) 2.4 else 1.2>],
    *[for (k in 0 to 11, let p = polar(56.0, -120.0 + 240.0 * k / 11.0) where k % 2 == 0)
        <text x: p[0], y: p[1] + 4.0, 'text-anchor': "middle", class: "dial-num", *[string(k * 20)]>]]>,
    <div id: "needle", class: "needle", style: "transform:rotate(-120deg)">,
    <div class: "hub">]>

pub let BUTTONS = [{act: "look", key: "N", label: "Day / Night"}, {act: "lights", key: "L", label: "Headlights"},
    {act: "camera", key: "V", label: "Camera"}, {act: "courier", key: "M", label: "Courier"},
    {act: "auto", key: "O", label: "Auto drive"}, {act: "reset", key: "R", label: "Reset car"},
    {act: "pause", key: "P", label: "Pause"}, {act: "help", key: "H", label: "Keys"}]
fn button(b) element => <button id: "btn-" ++ b.act, class: "hud-button", type: "button", 'data-act': b.act, 'aria-pressed': "false",
    title: b.label ++ " (" ++ b.key ++ ")", *[<span class: "key", *[b.key]>, <span class: "label", *[b.label]>]>
fn pedal(hold, label, glyph) element => <button class: "pedal pedal-" ++ hold, type: "button", 'data-hold': hold, 'aria-label': label, *[glyph]>
let KEYS = [["W / ↑", "Accelerate"], ["S / ↓", "Brake, then reverse"], ["A D / ← →", "Steer"], ["Space", "Handbrake (drift)"],
    ["C", "Look back (hold)"], ["V", "Change camera"], ["L or Z", "Headlights"], ["N", "Day, dusk or night"],
    ["M", "Start or stop a courier run"], ["O", "Auto drive"], ["R", "Put the car back on the road"], ["P / Esc", "Pause"], ["Drag", "Look around"]]

pub fn page(st) element => <div id: "midtown", class: "drive", 'data-started': "false", 'data-help': "false", 'data-paused': "false", *[
    <div id: "stage", class: "stage", *[sc.scene(st)]>,
    <header class: "panel location", *[
        <div class: "brand", "MIDTOWN">, <div id: "district", class: "district", *[city.district(st.car.x, st.car.z)]>,
        <div id: "street", class: "street", *[city.street(st.car.x, st.car.z)]>,
        <div id: "look-name", class: "look", *[sc.LOOKS[st.look].label ++ " · " ++ cam.LABELS[st.camera.mode]]>]>,
    <section class: "panel map-card", *[minimap()]>,
    <section id: "mission", class: "panel mission", 'data-phase': "idle", *[
        <div class: "mission-head", *[<span class: "mission-tag", "COURIER">, <span id: "cash", class: "cash", "$0">]>,
        <div id: "mission-text", class: "mission-text", "Press M to pick up a parcel">,
        <div id: "timer-box", class: "timer", style: "opacity:0", *[readout("timer", 3), <span class: "unit", "s left">]>]>,
    <div id: "toast", class: "toast", style: "opacity:0", <span id: "toast-text", " ">>,
    <section class: "gauges", *[
        speedometer(),
        <div class: "speed-read", *[readout("speed", 3), <span class: "unit", "km/h">]>,
        <div class: "gear", *[for (k, g in GEARS) <span id: "gear-" ++ string(k), class: "gear-glyph", style: if (g == "N") "opacity:1" else "opacity:0", *[g]>]>,
        <div class: "bars", *[
            <div class: "bar-row", *[<span class: "bar-name", "RPM">, <div class: "bar", <div id: "rpm-fill", class: "fill rpm", style: "transform:scaleX(0.1)">>]>,
            <div class: "bar-row", *[<span class: "bar-name", "DMG">, <div class: "bar", <div id: "damage-fill", class: "fill damage", style: "transform:scaleX(0)">>]>]>]>,
    <nav class: "controls", 'aria-label': "Driving controls", *[for (b in BUTTONS) button(b)]>,
    <div class: "pedals", 'aria-label': "Touch controls", *[
        <div class: "steer", *[pedal("left", "Steer left", "◀"), pedal("right", "Steer right", "▶")]>,
        <div class: "feet", *[pedal("brake", "Handbrake", "⤿"), pedal("down", "Brake", "▼"), pedal("up", "Accelerate", "▲")]>]>,
    <section id: "help", class: "panel help", *[<h2 "Keys">,
        <dl *[for (k in KEYS) <div class: "key-row", *[<dt *[k[0]]>, <dd *[k[1]]>]>]>]>,
    <div id: "paused", class: "paused", <div class: "paused-card", *[<strong "Paused">, <span "Press P or Pause to drive on">]>>,
    <section id: "intro", class: "intro", <div class: "intro-card", *[
        <p class: "eyebrow", "LAMBDA · SCENE3D · OPEN CITY">,
        <h1 "Midtown">,
        <p class: "lede", "Lumen Bay is yours: thirty-six blocks of towers, parks and harbour streets, with traffic that keeps to its lanes and people who keep out of your way.">,
        <ul class: "intro-list", *[<li "Drive anywhere, jump the ramps in the stunt lot, and knock the cones flying.">,
            <li "Run courier jobs across town against the clock.">, <li "Switch between day, dusk and night, and four cameras.">]>,
        <div class: "intro-actions", *[<button id: "start", class: "start", type: "button", 'data-act': "start", "Start driving">,
            <button class: "ghost", type: "button", 'data-act': "auto-start", "Watch it drive">]>,
        <p class: "intro-keys", "W A S D or arrows to drive · Space for the handbrake · H for every key">]>>]>
