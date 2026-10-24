// Run: ./lambda.exe view test/demo/index.ls
// The demo index: one animated tile per `lambda demo` entry; a click opens
// that demo in a window of its own.
import dom
import resources: .mod_resources
import art: .mod_demo_art

let CSS = input(url_resolve(resources.entry_uri()^, "index.css"), 'text')^
let EXE = sys.proc.self.argv#[0]

// the catalog of `lambda demo` (LAMBDA_DEMOS in lambda/main.cpp), in showcase order
let DEMOS = [
    {name: "doom", title: "DOOM", tag: "GAME · 3D SHOOTER", hue: "#f87171",
     blurb: "All nine maps of the 1993 classic: weapons, monsters, doors and sound, with the game in Lambda and the walls in CSS 3D."},
    {name: "cityscape", title: "The Rings of a City", tag: "FILM · SCENE3D", hue: "#fbbf24",
     blurb: "Chengdu rebuilt from OpenStreetMap, playing 3,000 years of history as one live 150-second scene with an original score."},
    {name: "scene3d", title: "Scene3D", tag: "3D · PHYSICS", hue: "#60a5fa",
     blurb: "Living cells, domino cascades, a ringed planet and dancing robots: eleven native 3D worlds in one collection."},
    {name: "superlambda", title: "Superlambda", tag: "GAME · PLATFORMER", hue: "#fb7185",
     blurb: "Run, jump and stomp across a sunny meadow in a red λ cap. Grab the coins, bump the blocks, reach the flag."},
    {name: "tetris", title: "Tetris", tag: "GAME · CLASSIC", hue: "#a78bfa",
     blurb: "The falling-block classic, keyboard ready, with an Auto Play button for when you would rather watch."},
    {name: "sentence-game", title: "Sentence Match", tag: "GAME · LEARNING", hue: "#2dd4bf",
     blurb: "Three levels of sentence building, word-card puzzles and picture description, with every sentence read aloud."},
    {name: "photo-album", title: "Photo Album", tag: "PHOTOS · LIBRARY", hue: "#f472b6",
     blurb: "A thousand photographs on a timeline, with albums, a map of places, collages, slideshows and a memories film."},
    {name: "photo", title: "Photo Studio", tag: "PHOTOS · EDITOR", hue: "#fb923c",
     blurb: "Crop, tune and grade a photo with a live before-and-after, every pixel computed on Lambda arrays."},
    {name: "photos", title: "Photos", tag: "PHOTOS · LAYERS", hue: "#22d3ee",
     blurb: "A layered raster editor built on the shared image and PSD package: stack, blend and paint."},
    {name: "draw", title: "Draw", tag: "TOOLS · DIAGRAMS", hue: "#a3e635",
     blurb: "Shapes, connectors and pages in a diagram editor inspired by draw.io."},
    {name: "word", title: "Word", tag: "DOCUMENTS · EDITOR", hue: "#3b82f6",
     blurb: "A word processor with a ribbon, styles, tables, equations and WordArt. No WASM, no browser."},
    {name: "note", title: "Notes", tag: "DOCUMENTS · NOTES", hue: "#facc15",
     blurb: "Folders, pinned notes, full-text search and a rich-text editor, in the spirit of Mac Notes."},
    {name: "slides", title: "Slides", tag: "DOCUMENTS · PRESENTATION", hue: "#14b8a6",
     blurb: "An 18-slide strategy deck with builds, transitions, live charts and speaker notes."},
    {name: "chart", title: "Charts", tag: "DATA · VISUALIZATION", hue: "#818cf8",
     blurb: "Dashboards, scientific surfaces, Sankey, chord and force graphs: live, zoomable and animated."},
    {name: "wordcloud", title: "Word Cloud", tag: "DATA · TYPOGRAPHY", hue: "#e879f9",
     blurb: "Words sized by weight and packed into a cloud by lambda.chart, in any script."},
    {name: "finder", title: "Finder", tag: "SYSTEM · FILES", hue: "#94a3b8",
     blurb: "Browse real folders and archives with icons, columns and thumbnails, like macOS Finder."},
    {name: "mail", title: "Mail", tag: "SYSTEM · EMAIL", hue: "#38bdf8",
     blurb: "An IMAP mail client for Gmail, iCloud and more, with passwords in the Keychain and remote content blocked."},
    {name: "archive", title: "Archive", tag: "WEB · PRESERVATION", hue: "#f59e0b",
     blurb: "Paste a URL and keep the page, its media and a readable copy in a web archive of your own."},
    {name: "readability", title: "Reader View", tag: "WEB · READING", hue: "#4ade80",
     blurb: "Strip a cluttered article down to the story with a Lambda port of Mozilla's Readability."}
]

fn title_of(key) => [for (d in DEMOS where d.name == key) d.title][0]
fn is_running(running, key) => len([for (r in running where r.name == key) r]) > 0

// drifting light, a dot lattice and λ glyphs rising through the page
let GLYPHS = [[6, 22, 0], [14, 14, 6], [23, 30, 3], [34, 18, 9], [47, 26, 1], [58, 16, 7],
    [66, 34, 4], [75, 20, 10], [84, 28, 2], [93, 15, 8]]
fn backdrop() => <div class:"backdrop", 'aria-hidden':"true",
    <div class:"orb orb-a">
    <div class:"orb orb-b">
    <div class:"orb orb-c">
    <div class:"lattice">
    for (g in GLYPHS) <span class:"glyph",
        style:"left:" ++ string(g[0]) ++ "%;font-size:" ++ string(g[1]) ++ "px;animation-delay:-" ++ string(g[2]) ++ "s",
        "λ">>

// the hero mark: a λ that draws itself inside two counter-rotating rings
fn emblem() => <svg class:"emblem", viewBox:"0 0 240 240", 'aria-hidden':"true",
    <defs <linearGradient id:"emblem-ink", x1:0, y1:0, x2:1, y2:1,
        <stop offset:0, 'stop-color':"#7dd3fc">
        <stop offset:.5, 'stop-color':"#a78bfa">
        <stop offset:1, 'stop-color':"#f472b6">>>
    <circle cx:120, cy:120, r:104, fill:"none", stroke:"#a78bfa", 'stroke-width':1.5, 'stroke-dasharray':"4 10", opacity:".5",
        <animateTransform attributeName:"transform", type:"rotate", values:"0 120 120;360 120 120", dur:"40s", repeatCount:"indefinite">>
    <circle cx:120, cy:120, r:86, fill:"none", stroke:"#7dd3fc", 'stroke-width':1, 'stroke-dasharray':"60 24", opacity:".45",
        <animateTransform attributeName:"transform", type:"rotate", values:"360 120 120;0 120 120", dur:"24s", repeatCount:"indefinite">>
    <g <animateTransform attributeName:"transform", type:"rotate", values:"0 120 120;360 120 120", dur:"9s", repeatCount:"indefinite">;
        <circle cx:120, cy:16, r:5, fill:"#f472b6">
        <circle cx:120, cy:224, r:3, fill:"#7dd3fc">>
    <path d:"M76 58 C96 52 108 62 116 82 L160 186 M124 104 L82 186", fill:"none", stroke:"url(#emblem-ink)",
        'stroke-width':18, 'stroke-linecap':"round", 'stroke-linejoin':"round", 'stroke-dasharray':"300",
        <animate attributeName:"stroke-dashoffset", values:"300;0;0;300", keyTimes:"0;.3;.85;1", dur:"7s", repeatCount:"indefinite">>>

fn hero() => <header class:"hero",
    <div class:"hero-copy",
        <p class:"eyebrow", "λ  LAMBDA SCRIPT  ·  RADIANT">
        <h1 <span "Pure functions.">; <span class:"ink", "Living pixels.">>
        <p class:"lead", "Nineteen apps, from games and editors to 3D worlds and documents, written in Lambda and painted by Radiant. No browser. No framework. One language, start to finish.">
        <div class:"stats",
            <span class:"stat", <b "19">; " apps">
            <span class:"stat", <b "1">; " language">
            <span class:"stat", <b "0">; " browsers">>
        <p class:"hint", "Click a tile to open it in its own window.">>;
    (emblem())>

fn tile(d, live) => <button type:"button", class:if (live) "tile running" else "tile", 'data-demo':d.name,
    style:"--hue:" ++ d.hue, 'aria-label':"Open " ++ d.title ++ " in a new window",
    <span class:"tile-art", (art.of(d.name))>
    <span class:"tile-copy",
        <span class:"tile-tag", d.tag>
        <span class:"tile-title", d.title>
        <span class:"tile-blurb", d.blurb>>
    <span class:"tile-foot",
        <span class:"tile-cmd", "lambda demo " ++ d.name>
        <span class:"tile-open", if (live) "● Running" else "Open ↗">>>

// Each demo runs as its own `lambda demo` process and so gets its own window:
// Radiant has no window factory for a new browsing context yet. The task owns
// the process; its exit returns as `demoexit` on the index.
pn launch(root, id, key) {
    var code = -1
    io.exec(EXE, ["demo", key], {timeout_ms: 0}) ^ { code = -1 } ~ { code = ~.exit_code }
    let owner = dom.get_element_by_id(root, "demo-index")
    if (owner != null) dom.dispatch(owner, {type: "demoexit", bubbles: false, detail: {id: id, name: key, code: code}})
}

view <demo_index> state running: [], next_id: 1, note: null {
    <body
        (backdrop());
        <div id:"demo-index", class:"scroller",
            <main class:"page",
                (hero());
                <section class:"tiles", for (d in DEMOS) tile(d, is_running(running, d.name))>
                <footer class:"page-foot", "Every tile is a live Lambda drawing. Every demo is one command away: ";
                    <code "lambda demo <name>">>>>
        if (note != null) <div class:"toast", 'aria-live':"polite", note>>
}
on click(evt) {
    let tile = dom.closest(evt.target, "[data-demo]")
    if (tile == null) { return 'pass' }
    let key = dom.get_attribute(tile, "data-demo")
    start(launch, [dom.root_node(evt.target), next_id, key], {owner: 'document'})
    running = running ++ [{id: next_id, name: key}]
    next_id = next_id + 1
    note = "Opening " ++ title_of(key) ++ " in a new window…"
    return 'handled'
}
on demoexit(evt) {
    running = [for (r in running where r.id != evt.detail.id) r]
    note = if (evt.detail.code == 0) null
        else title_of(evt.detail.name) ++ " stopped with exit code " ++ string(evt.detail.code)
    return 'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Lambda Demos"><style CSS>>
    apply(<demo_index>)>
