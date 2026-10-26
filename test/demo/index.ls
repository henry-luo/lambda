// Run: ./lambda.exe view test/demo/index.ls
// The demo index, laid out like an app store: a category sidebar beside a
// Discover page of featured apps and shelves, or one category's apps. A click
// on an app opens that demo in a window of its own.
import dom
import resources: .mod_resources
import art: .mod_demo_art

let CSS = input(url_resolve(resources.entry_uri()^, "index.css"), 'text')^
let EXE = sys.proc.self.argv#[0]

// the catalog of `lambda demo` (LAMBDA_DEMOS in lambda/main.cpp), in showcase order;
// `cat` files each demo under one CATEGORIES entry and `sub` is its store subtitle
let DEMOS = [
    {name: "chart", title: "Charts", cat: "science", sub: "Data visualization",
     blurb: "Dashboards, 3D surfaces and graphs, all live."},
    {name: "scene3d", title: "Scene3D", cat: "science", sub: "Interactive 3D",
     blurb: "Cells, dominoes, planets and robots in native 3D."},
    {name: "aquarium", title: "Aquarium", cat: "design", sub: "Aquascape studio",
     blurb: "Build a living tank with fish, plants, rocks and driftwood."},
    {name: "cityscape", title: "The Rings of a City", cat: "media", sub: "Live 3D film",
     blurb: "3,000 years of Chengdu in one live 3D film."},
    {name: "tetris", title: "Tetris", cat: "games", sub: "Arcade classic",
     blurb: "The falling-block classic, with Auto Play."},
    {name: "marble-drop", title: "Marble Drop", cat: "games", sub: "Mechanical puzzles",
     blurb: "Sixteen brass machines, curious devices, and a way home."},
    {name: "superlambda", title: "Superlambda", cat: "games", sub: "Platformer",
     blurb: "Run, jump and stomp to the flag in a red λ cap."},
    {name: "doom", title: "DOOM", cat: "games", sub: "3D shooter",
     blurb: "The 1993 classic, all nine maps, rebuilt in Lambda."},
    {name: "sentence-game", title: "Build Sentence", cat: "games", sub: "Learning game",
     blurb: "Build sentences from word cards, read aloud."},
    {name: "photo-album", title: "Photo Album", cat: "photography", sub: "Photo library",
     blurb: "A thousand photos: timeline, map, collages, films."},
    {name: "photo", title: "Photo Studio", cat: "photography", sub: "Photo editor",
     blurb: "Crop, tune and grade with a live before and after."},
    {name: "photos", title: "Photos", cat: "photography", sub: "Layered image editor",
     blurb: "Stack, blend and paint layers in a raster editor."},
    {name: "draw", title: "Draw", cat: "design", sub: "Diagrams",
     blurb: "Shapes and connectors, inspired by draw.io."},
    {name: "greetings", title: "Greetings", cat: "design", sub: "Card designer",
     blurb: "Design a greeting card, preview it open, print and fold."},
    {name: "word", title: "Word", cat: "productivity", sub: "Word processor",
     blurb: "A word processor with ribbon, tables and equations."},
    {name: "note", title: "Notes", cat: "productivity", sub: "Notes",
     blurb: "Folders, pins, search and rich text, like Mac Notes."},
    {name: "slides", title: "Slides", cat: "productivity", sub: "Presentations",
     blurb: "Create, edit and present slides, with notes and animations."},
    {name: "finder", title: "Finder", cat: "utilities", sub: "File browser",
     blurb: "Browse folders and archives, like macOS Finder."},
    {name: "book", title: "Books", cat: "media", sub: "Ebook reader",
     blurb: "Seven public-domain classics, and a reader that keeps your place."},
    {name: "music", title: "Music", cat: "media", sub: "Sheet music player",
     blurb: "Play engraved sheet music, notes and lyrics lit as they sound."},
    {name: "soundflow", title: "Soundflow", cat: "media", sub: "Visual instrument",
     blurb: "Draw across the strings; every stroke loops and sings."},
    {name: "mail", title: "Mail", cat: "utilities", sub: "Email",
     blurb: "An IMAP mail client, passwords kept in the Keychain."},
    {name: "archive", title: "Archive", cat: "utilities", sub: "Web archiver",
     blurb: "Save any web page, its media and a readable copy."},
    {name: "readability", title: "Reader View", cat: "utilities", sub: "Reader view",
     blurb: "Strip a cluttered article down to the story."}
]

// the store's sidebar, in order; each key is also its glyph in mod_demo_art
let CATEGORIES = [
    {key: "games", title: "Games", blurb: "Arcade classics, brass puzzle machines and a word game, all native."},
    {key: "productivity", title: "Productivity", blurb: "Write, plan and present in full editors."},
    {key: "photography", title: "Photography", blurb: "Browse a library, grade a photo, stack layers."},
    {key: "design", title: "Design", blurb: "Diagrams and greeting cards, ready to print."},
    {key: "science", title: "Data & Science", blurb: "Charts, graphs and interactive 3D worlds."},
    {key: "media", title: "Books & Media", blurb: "Classics to read, scores that play, an instrument you draw, and a city's history as a 3D film."},
    {key: "utilities", title: "Utilities", blurb: "Files, mail and the web, kept tidy."}
]
// Discover leads with these, then shows a one-row shelf per category
let FEATURED = ["marble-drop", "scene3d"]
let SHELF = 3

fn demo_of(key) => [for (d in DEMOS where d.name == key) d][0]
fn title_of(key) => demo_of(key).title
fn category_of(key) => [for (c in CATEGORIES where c.key == key) c][0]
fn in_category(key) => [for (d in DEMOS where d.cat == key) d]
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
        <p class:"hint", "Pick an app below to open it in its own window.">>;
    (art.emblem())>

fn get_button(live) => <span class:"get", if (live) "Running" else "Open">

// an App Store card: the art, then icon, name, subtitle and Open, then the blurb
fn card(d, running) => (let live = is_running(running, d.name),
    <button type:"button", class:if (live) "card running" else "card", 'data-demo':d.name,
        title:"lambda demo " ++ d.name, 'aria-label':"Open " ++ d.title ++ " in a new window",
        <span class:"card-art", (art.of(d.name))>
        <span class:"card-info",
            <span class:"app-icon", (art.icon(d.name))>
            <span class:"card-text",
                <span class:"card-title", d.title>
                <span class:"card-sub", d.sub>>;
            (get_button(live))>
        <span class:"card-blurb", d.blurb>>)

// a featured app: editorial copy over large art, with the command to run it
fn feature(d, running) => (let live = is_running(running, d.name),
    <button type:"button", class:if (live) "feature running" else "feature", 'data-demo':d.name,
        'aria-label':"Open " ++ d.title ++ " in a new window",
        <span class:"feature-copy",
            <span class:"feature-eyebrow", "FEATURED · " ++ upper(category_of(d.cat).title)>
            <span class:"feature-title", d.title>
            <span class:"feature-blurb", d.blurb>>
        <span class:"feature-art", (art.of(d.name))>
        <span class:"feature-foot",
            <span class:"app-icon", (art.icon(d.name))>
            <span class:"card-text",
                <span class:"card-title", d.title>
                <span class:"cmd", "lambda demo " ++ d.name>>;
            (get_button(live))>>)

fn side_item(key, caption, total, current) => <button type:"button", class:if (key == current) "side-item active" else "side-item",
    'data-cat':key, 'aria-current':if (key == current) "page" else "false",
    (art.glyph(if (key == "all") "discover" else key));
    <span class:"side-label", caption>
    <span class:"side-count", string(total)>>

fn sidebar(current) => <nav class:"sidebar", 'aria-label':"App categories",
    (side_item("all", "Discover", len(DEMOS), current));
    <p class:"side-head", "Categories">
    for (c in CATEGORIES) side_item(c.key, c.title, len(in_category(c.key)), current);
    <div class:"side-note", "Every app is one command away:"; <code "lambda demo <name>">>>

fn shelf(c, running) => (let apps = in_category(c.key), <section class:"shelf",
    <div class:"shelf-head",
        <h3 c.title>
        if (len(apps) > SHELF) <button type:"button", class:"see-all", 'data-cat':c.key,
            "See all " ++ string(len(apps)) ++ " ›">>
    <div class:"cards", for (i, d in apps where i < SHELF) card(d, running)>>)

fn discover(running) => <div class:"store-page",
    <header class:"store-head",
        <h2 "Discover">
        <p string(len(DEMOS)) ++ " apps in " ++ string(len(CATEGORIES)) ++ " categories, every one written in Lambda.">>
    <div class:"features", for (key in FEATURED) feature(demo_of(key), running)>
    for (c in CATEGORIES) shelf(c, running)>

fn category_page(c, running) => (let apps = in_category(c.key), <div class:"store-page",
    <header class:"store-head",
        <button type:"button", class:"back", 'data-cat':"all", "‹ Discover">
        <h2 c.title>
        <p c.blurb ++ " " ++ string(len(apps)) ++ (if (len(apps) == 1) " app." else " apps.")>>
    <div class:"cards", for (d in apps) card(d, running)>>)

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

view <demo_index> state running: [], next_id: 1, note: null, cat: "all" {
    <body
        (backdrop());
        <div id:"demo-index", class:"scroller",
            <main class:"page",
                (hero());
                <section class:"store",
                    (sidebar(cat));
                    <div class:"store-main",
                        if (cat == "all") discover(running) else category_page(category_of(cat), running)>>
                <footer class:"page-foot", "Every app is a real Lambda program, one command away: ";
                    <code "lambda demo <name>">>>>
        if (note != null) <div class:"toast-bar", 'aria-live':"polite",
            <span class:"toast", title:"Click to dismiss", note>>>
}
on click(evt) {
    if (dom.closest(evt.target, ".toast") != null) { note = null; return 'handled' }
    let pick = dom.closest(evt.target, "[data-cat]")
    if (pick != null) { cat = dom.get_attribute(pick, "data-cat"); return 'handled' }
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
