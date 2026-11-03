// HUD markup: location, minimap, courier panel, speedometer, controls, touch pedals, help and the
// title card. Everything that changes every frame is a stack shown by opacity or an element moved by
// transform, so a frame repaints without relayout; words change only when the situation does.
import u: .mod_util
import city: .mod_city
import cam: .mod_camera
import sc: .mod_scene

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
fn minimap() element => <div class: "map-disc", *[
    <div id: "map-world", class: "map-world", *[plan(),
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
