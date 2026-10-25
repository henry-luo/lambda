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
    {name: "chart", title: "Charts", tag: "DATA · VISUALIZATION",
     blurb: "Dashboards, 3D surfaces and graphs, all live."},
    {name: "scene3d", title: "Scene3D", tag: "3D · PHYSICS",
     blurb: "Cells, dominoes, planets and robots in native 3D."},
    {name: "cityscape", title: "The Rings of a City", tag: "FILM · SCENE3D",
     blurb: "3,000 years of Chengdu in one live 3D film."},
    {name: "tetris", title: "Tetris", tag: "GAME · CLASSIC",
     blurb: "The falling-block classic, with Auto Play."},
    {name: "marble-drop", title: "Marble Drop", tag: "GAME · MECHANICAL PUZZLES",
     blurb: "Sixteen brass machines, curious devices, and a way home."},
    {name: "superlambda", title: "Superlambda", tag: "GAME · PLATFORMER",
     blurb: "Run, jump and stomp to the flag in a red λ cap."},
    {name: "doom", title: "DOOM", tag: "GAME · 3D SHOOTER",
     blurb: "The 1993 classic, all nine maps, rebuilt in Lambda."},
    {name: "sentence-game", title: "Build Sentence", tag: "GAME · LEARNING",
     blurb: "Build sentences from word cards, read aloud."},
    {name: "photo-album", title: "Photo Album", tag: "PHOTOS · LIBRARY",
     blurb: "A thousand photos: timeline, map, collages, films."},
    {name: "photo", title: "Photo Studio", tag: "PHOTOS · EDITOR",
     blurb: "Crop, tune and grade with a live before and after."},
    {name: "photos", title: "Photos", tag: "PHOTOS · LAYERS",
     blurb: "Stack, blend and paint layers in a raster editor."},
    {name: "draw", title: "Draw", tag: "TOOLS · DIAGRAMS",
     blurb: "Shapes and connectors, inspired by draw.io."},
    {name: "greetings", title: "Greetings", tag: "DESIGN · CARDS",
     blurb: "Design a greeting card, preview it open, print and fold."},
    {name: "word", title: "Word", tag: "DOCUMENTS · EDITOR",
     blurb: "A word processor with ribbon, tables and equations."},
    {name: "note", title: "Notes", tag: "DOCUMENTS · NOTES",
     blurb: "Folders, pins, search and rich text, like Mac Notes."},
    {name: "slides", title: "Slides", tag: "DOCUMENTS · EDITOR",
     blurb: "Create, edit and present slides, with notes and animations."},
    {name: "finder", title: "Finder", tag: "SYSTEM · FILES",
     blurb: "Browse folders and archives, like macOS Finder."},
    {name: "music", title: "Music", tag: "MEDIA · SCORES",
     blurb: "Play engraved sheet music, notes and lyrics lit as they sound."},
    {name: "mail", title: "Mail", tag: "SYSTEM · EMAIL",
     blurb: "An IMAP mail client, passwords kept in the Keychain."},
    {name: "archive", title: "Archive", tag: "WEB · PRESERVATION",
     blurb: "Save any web page, its media and a readable copy."},
    {name: "readability", title: "Reader View", tag: "WEB · READING",
     blurb: "Strip a cluttered article down to the story."}
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

// "Living" rides a wave, a letter at a time
fn living() => <span class:"lead-word", for (i, ch in split("Living", "")) <span class:"wave",
    style:"animation-delay:" ++ string(i * 0.14) ++ "s", ch>>

fn hero() => <header class:"hero",
    <div class:"hero-copy",
        <p class:"eyebrow", "λ  LAMBDA SCRIPT  ·  RADIANT">
        <h1 <span "Pure functions.">; <span class:"ink", 'aria-label':"Living pixels.", (living()); " pixels";
            // the full stop is one live pixel that hops and lands
            <span class:"pixel", 'aria-hidden':"true">>>
        <p class:"lead", string(len(DEMOS)) ++ " apps, from games and editors to 3D worlds and documents, written in Lambda and painted by Radiant. No browser. No framework. One language, start to finish.">
        <div class:"stats",
            <span class:"stat", <b string(len(DEMOS))>; " apps">
            <span class:"stat", <b "1">; " language">
            <span class:"stat", <b "0">; " browsers">>
        <p class:"hint", "Click a tile to open it in its own window.">>;
    (art.emblem())>

fn tile(d, live) => <button type:"button", class:if (live) "tile running" else "tile", 'data-demo':d.name,
    'aria-label':"Open " ++ d.title ++ " in a new window",
    <span class:"tile-art", (art.of(d.name))>
    <span class:"tile-copy",
        <span class:"tile-tag", d.tag>
        <span class:"tile-title", d.title>
        <span class:"tile-blurb", d.blurb>>
    <span class:"tile-foot",
        <span class:"tile-cmd", "lambda demo " ++ d.name>
        <span class:"tile-open", if (live) "● Running" else "Open ↗">>>

// the first error a demo logged on its way out, without the log timestamp
fn first_error(stderr) => if (stderr == null) null else (let text = parse(stderr, "text") ^ { "" },
    let lines = [for (l in split(text, "\n") where contains(l, "[ERR!] ")) l],
    if (len(lines) == 0) null else trim(split(lines[0], "[ERR!] ")[1]))

// Each demo runs as its own `lambda demo` process and so gets its own window:
// Radiant has no window factory for a new browsing context yet. The task owns
// the process; its exit, and why a failed demo stopped, return as `demoexit`.
pn launch(root, id, key) {
    var code = -1
    var reason = null
    io.exec(EXE, ["demo", key], {timeout_ms: 0}) ^ { code = -1 } ~ { code = ~.exit_code; reason = first_error(~.stderr) }
    let owner = dom.get_element_by_id(root, "demo-index")
    if (owner != null) dom.dispatch(owner, {type: "demoexit", bubbles: false,
        detail: {id: id, name: key, code: code, reason: reason}})
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
        if (note != null) <div class:"toast-bar", 'aria-live':"polite",
            <span class:"toast", title:"Click to dismiss", note>>>
}
on click(evt) {
    if (dom.closest(evt.target, ".toast") != null) { note = null; return 'handled' }
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
    let title = title_of(evt.detail.name)
    note = if (evt.detail.code == 0) null
        else if (evt.detail.reason != null) title ++ " stopped: " ++ evt.detail.reason
        else title ++ " stopped with exit code " ++ string(evt.detail.code)
    return 'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Lambda Demos"><style CSS>>
    apply(<demo_index>)>
