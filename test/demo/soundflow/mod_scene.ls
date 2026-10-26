// Soundflow's picture: a dark field of strings, the ribbons drawn across them,
// and the bright tips that replay each ribbon. Rendering is pure (S12.1.3).
import m: .mod_model

fn num(v) => string(round(v * 10) / 10)
fn ink(voice) => m.VOICES[voice].ink
let LINE = "#4f4f4f"
let RIM = "#8a8a8a"
let FIELD = "#0b0b0c"

// ---- strings

// how a string is sounding now: its age, decay and voice, or null when still
fn sound_of(st, j) {
    let r = st.ring[string(j)]
    let age = if (r == null or st.wall == null) -1.0 else st.wall - r.at
    if (age < 0.0 or age >= m.RING_MS) null else {age: age, env: math.exp(-age / 420.0), voice: r.voice, gain: r.gain}
}
// a sounding string swings about its middle; the quadratic's control point
// sits twice as far out as the curve
fn string_path(s, r) {
    let l = max(1.0, m.length(s))
    let swing = if (r == null) 0.0 else 18.0 * r.gain * r.env * math.sin(r.age * 0.075)
    let cx = (s.a.x + s.b.x) / 2 - (s.b.y - s.a.y) / l * swing
    let cy = (s.a.y + s.b.y) / 2 + (s.b.x - s.a.x) / l * swing
    "M" ++ num(s.a.x) ++ " " ++ num(s.a.y) ++ " Q" ++ num(cx) ++ " " ++ num(cy) ++ " " ++ num(s.b.x) ++ " " ++ num(s.b.y)
}
// the inner dot brightens with pitch, as SoundBow's did
// (no wrapper: every element here is repainted each frame the string sounds)
fn rim(p, r) => <circle cx:num(p.x), cy:num(p.y), r:7, fill:FIELD, stroke:if (r == null) RIM else ink(r.voice), 'stroke-width':1.3>
fn dot(p, tone, r) => <circle cx:num(p.x), cy:num(p.y), r:3.2, fill:if (r == null) tone else ink(r.voice),
    opacity:if (r == null) "1" else num(0.4 + 0.6 * r.env)>
fn string_view(st, j) {
    let s = st.strings[j]
    let r = sound_of(st, j)
    let shade = string(int(70 + 150 * m.degree(s) / (m.DEGREES - 1)))
    let tone = "rgb(" ++ shade ++ "," ++ shade ++ "," ++ shade ++ ")"
    ;<g class:"string", 'data-note':m.note_name(m.string_note(st, j)),
        <path d:string_path(s, r), fill:"none", stroke:if (r == null) LINE else ink(r.voice),
            'stroke-width':if (r == null) "1.3" else num(1.3 + 1.4 * r.env), 'stroke-linecap':"round">;
        rim(s.a, r); dot(s.a, tone, r); rim(s.b, r); dot(s.b, tone, r)>
}

// ---- strokes

// without the wide ribbons, the trail carries the voice's colour
fn tip_view(k, at_ms, ribbons) {
    let p = m.phase(k, at_ms)
    let head = m.tip(k, p)
    if (head == null) null
    else <g class:"tip",
        <path d:m.trail(k, p, head), fill:"none", stroke:if (ribbons) "#ffffff" else ink(k.voice),
            'stroke-opacity':if (ribbons) "0.6" else "0.9", 'stroke-width':2.4,
            'stroke-linecap':"round", 'stroke-linejoin':"round">;
        <circle cx:num(head.x), cy:num(head.y), r:14, fill:ink(k.voice), opacity:"0.25">;
        <circle cx:num(head.x), cy:num(head.y), r:4.5, fill:"#ffffff">>
}
fn ribbon(k) => <path class:"ribbon", d:k.outline, fill:ink(k.voice), opacity:"0.92">
fn drawing_view(d) {
    let head = d.pts[len(d.pts) - 1]
    ;<g class:"drawing",
        <path d:m.outline(d.pts), fill:ink(d.voice)>;
        <circle cx:num(head.x), cy:num(head.y), r:5, fill:"#ffffff">>
}

// the field alone: strings under ribbons; live views add the tips
pub fn field(st, live) => <svg id:"stage", class:"stage", xmlns:"http://www.w3.org/2000/svg",
    viewBox:"0 0 " ++ num(m.W) ++ " " ++ num(m.H), preserveAspectRatio:"xMidYMid meet",
    <rect width:num(m.W), height:num(m.H), fill:FIELD>;
    <g class:"strings", for (j of st.strings) string_view(st, j)>;
    if (st.ribbons) <g class:"ribbons", for (k in st.strokes) ribbon(k)>;
    if (live) <g class:"tips", for (k in st.strokes) tip_view(k, st.clock, st.ribbons)>;
    if (live and st.drawing != null) drawing_view(st.drawing)>

// ---- chrome

fn icon(d, filled = false) => <svg class:"icon", viewBox:"0 0 20 20", 'aria-hidden':"true",
    <path d:d, fill:if (filled) "currentColor" else "none", stroke:"currentColor", 'stroke-width':1.6,
        'stroke-linecap':"round", 'stroke-linejoin':"round">>
let ICON = {
    undo: "M7 5.5 L3 9.5 L7 13.5 M3.5 9.5 H12 C15 9.5 17 11.5 17 14.5",
    clear: "M5.5 5.5 L14.5 14.5 M14.5 5.5 L5.5 14.5",
    save: "M10 3 V12.5 M6 8.5 L10 12.5 L14 8.5 M4 14.5 V16.5 H16 V14.5",
    pause: "M7.5 5 V15 M12.5 5 V15",
    play: "M7 5 L15 10 L7 15 Z",
    sound: "M3.5 8 H6.5 L10.5 4.5 V15.5 L6.5 12 H3.5 Z M13 7 C14.6 8.6 14.6 11.4 13 13 M15 5 C18 8 18 12 15 15",
    muted: "M3.5 8 H6.5 L10.5 4.5 V15.5 L6.5 12 H3.5 Z M13.5 8 L17.5 12 M17.5 8 L13.5 12",
    ribbon: "M3 14 C5 5 9 5 10 10 S15 15 17 6",
    thread: "M3 14 C5 5 9 5 10 10 S15 15 17 6 M3.5 3.5 L16.5 16.5"
}
fn tool(action, caption, d, filled = false) => <button class:"tool", 'data-action':action, title:caption, 'aria-label':caption,
    icon(d, filled)>
fn choice(action, value, caption, on) => <button class:("choice" ++ (if (on) " on" else "")),
    'data-action':action, 'data-value':string(value), 'aria-pressed':string(on), caption>

fn voices(st) => <div class:"voices", role:"group", 'aria-label':"Voice",
    <span class:"voice-name", m.VOICES[st.voice].name>
    for (i, v of m.VOICES) <button class:("voice" ++ (if (i == st.voice) " on" else "")), 'data-action':"voice",
        'data-value':string(i), title:v.name ++ " · " ++ string(i + 1), 'aria-label':v.name, 'aria-pressed':string(i == st.voice),
        style:"--ink:" ++ v.ink>>

fn menu(st) => <nav class:"panel", 'aria-label':"Soundflow menu",
    <p class:"panel-title", "Strings">
    <div class:"choices", for (i, l of m.LAYOUTS) choice("layout", i, l.name, i == st.layout)>
    <p class:"panel-title", "Scale">
    <div class:"choices", for (i, s of m.SCALES) choice("scale", i, s.name, i == st.scale)>
    <p class:"panel-title", "Speed">
    <div class:"choices", for (i, v of m.SPEEDS) choice("speed", i, string(v) ++ "×", i == st.speed)>
    <div class:"choices", <button class:"choice wide", 'data-action':"sample", "▷  Play a sample piece">>
    <p class:"panel-note", "Draw across the strings. Each stroke replays in a loop, and its tip sounds every string it crosses. Drag a string's end to retune it (longer is lower), or its middle to slide it.">
    <p class:"panel-keys", "1 2 3 voice · Space pause · − + speed · H strokes · Z undo · E save">>

fn hint() => <div class:"hint",
    <div class:"hint-ring", <span "DRAW YOUR SOUNDS…">>
    <button class:"hint-sample", 'data-action':"sample", "▷  or play a sample piece">>

// audio: {status, enabled, played, tones} from the live engine
pub fn page(st, audio) => <main id:"soundflow", tabindex:"0",
    'data-voice':m.VOICES[st.voice].key, 'data-layout':m.LAYOUTS[st.layout].key, 'data-scale':m.SCALES[st.scale].key,
    'data-strokes':string(len(st.strokes)), 'data-plucks':string(st.count), 'data-paused':string(st.paused),
    'data-sound':audio.status, 'data-played':string(audio.played), 'data-tones':string(audio.tones),
    'data-speed':string(m.speed_of(st)), 'data-ribbons':string(st.ribbons), 'data-drawing':string(st.drawing != null), 'data-dragging':string(st.drag != null),
    field(st, true)
    if (len(st.strokes) == 0 and st.drawing == null) hint();
    <header class:"top",
        <button class:"tool menu-button", 'data-action':"menu", title:"Strings, scales and a sample piece",
            'aria-label':"Menu", 'aria-expanded':string(st.menu), "•••">
        <span class:"brand", "soundflow">
        voices(st)>
    if (st.menu) menu(st);
    <footer class:"bottom",
        <div class:"tools",
            if (st.paused) tool("pause", "Play · Space", ICON.play, true) else tool("pause", "Pause · Space", ICON.pause);
            <button class:"tool speed", 'data-action':"next-speed", title:"Playback speed · − +",
                'aria-label':"Playback speed " ++ m.speed_name(st), m.speed_name(st)>
            tool("undo", "Undo · Z", ICON.undo)
            tool("sound", if (audio.enabled) "Sound off · M" else "Sound on · M",
                if (audio.enabled) ICON.sound else ICON.muted)>
        <p class:"status", 'aria-live':"polite", st.status>
        <div class:"tools",
            if (st.ribbons) tool("ribbons", "Hide wide strokes · H", ICON.ribbon)
            else tool("ribbons", "Show wide strokes · H", ICON.thread)
            tool("clear", "Clear the strokes", ICON.clear)
            tool("save", "Save a recording · E", ICON.save)>>>
