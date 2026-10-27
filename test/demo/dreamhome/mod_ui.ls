// The planner's chrome: top bar, catalog, the plan and 3D panes with their
// tool strips, the inspector, the status bar and the dialogs. Everything here
// draws state; each control names its command with data-act (and data-arg),
// which dreamhome.ls dispatches.
import controls: lambda.ui.controls
import icons: ~~.mod_icons
import A: ~~.greetings.mod_art
import C: .mod_catalog
import M: .mod_model
import P: .mod_plan
import S: .mod_scene
import TH: .mod_thumb

// chrome sizes, as set in dreamhome.css
pub let CHROME = {top: 54.0, status: 28.0, catalog: 276.0, inspector: 272.0}

// the plan and 3D panes share the space between the side panels
pub fn panes(st) {
    let w = max(320.0, st.vp.width - CHROME.catalog - CHROME.inspector)
    let h = max(260.0, st.vp.height - CHROME.top - CHROME.status)
    let plan_h = if (st.layout == "plan") h else if (st.layout == "3d") 0.0 else floor(h * .5);
    {w: w, plan: plan_h, view3d: h - plan_h}
}

// ---- small parts ---------------------------------------------------------------------

fn glyph(d, size = 18) => <svg class:"glyph", width:string(size), height:string(size), viewBox:"0 0 24 24", 'aria-hidden':"true",
    <path d:d, fill:"none", stroke:"currentColor", 'stroke-width':1.7, 'stroke-linecap':"round", 'stroke-linejoin':"round">>
let G = {select: "M5 3l13 8-6 1.5L9 19z", wall: "M3 17h18 M3 13h18 M7 13v4 M12 13v4 M17 13v4 M5 9h4v4 M14 9h4v4",
    room: "M4 4h16v16H4z M4 4l4 4 M20 4l-4 4 M8 8h8v8H8z", plan2d: "M4 4h16v16H4z M4 12h9 M13 4v16",
    split: "M3 4h18v16H3z M3 12h18", cube: "M12 3l8 4.5v9L12 21l-8-4.5v-9z M12 12l8-4.5 M12 12v9 M12 12L4 7.5",
    orbit: "M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M3 12c0-3 4-5 9-5s9 2 9 5-4 5-9 5 M21 12l-2 3 M21 12l-3-1",
    walk: "M13 4a1.5 1.5 0 1 0 0 .1 M11 8l-3 4 2 1 M11 8l3 2 3 1 M11 8l-1 6 3 3v4 M10 14l-3 7",
    cut: "M4 20V10l8-6 8 6v10z M4 14h16", sun: "M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M12 2v2 M12 20v2 M2 12h2 M20 12h2 M5 5l1.5 1.5 M17.5 17.5L19 19 M19 5l-1.5 1.5 M6.5 17.5L5 19",
    moon: "M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z", fit: "M4 9V4h5 M15 4h5v5 M20 15v5h-5 M9 20H4v-5",
    plus: "M12 5v14 M5 12h14", minus: "M5 12h14", ruler: "M3 17L17 3l4 4L7 21z M7 13l2 2 M10 10l2 2 M13 7l2 2",
    turn: "M20 12a8 8 0 1 1-2.3-5.7 M20 4v4h-4", trash: "M4 7h16 M9 7V4h6v3 M6 7l1 13h10l1-13",
    home: "M3 11l9-7 9 7 M5 10v10h14V10 M10 20v-6h4v6", swing: "M5 20V4 M5 4a16 16 0 0 1 14 16", mirror: "M12 3v18 M8 7l-4 5 4 5 M16 7l4 5-4 5",
    camera: "M3 8h4l2-3h6l2 3h4v11H3z M12 10a3.5 3.5 0 1 0 0 7 3.5 3.5 0 0 0 0-7"}

fn act(verb, arg: any, cls, children, more = {}) => <button type:"button", class:cls, 'data-act':verb, 'data-arg':string(arg), *:more,
    for (c in children) c>
fn tool(verb, arg: any, d, caption, on) => act(verb, arg, if (on) "tool on" else "tool", [glyph(d), <span caption>],
    {title:caption, 'aria-pressed':if (on) "true" else "false"})
fn icon_btn(verb, arg: any, d, caption, more = {}) => act(verb, arg, "icon-btn", [glyph(d)], {title:caption, 'aria-label':caption, *:more})
fn section(title, children) => <section class:"pane-section", <h3 title>; for (c in children) c>
fn stepper(caption, verb, delta, value, unit = " cm") => <div class:"stepper", <span caption>;
    (icon_btn(verb, 0.0 - delta, G.minus, "Less")); <b value ++ unit>; (icon_btn(verb, delta, G.plus, "More"))>
fn swatches(verb, colors, current) => <div class:"swatches", for (c in colors)
    act(verb, c, if (lower(current) == c) "swatch on" else "swatch", [], {style:"background:" ++ c, title:c})>
fn cm(x) => string(int(round(x)))

// ---- top bar ---------------------------------------------------------------------------

fn topbar(st) => <header class:"topbar",
    <div class:"brand", <span class:"brand-mark", (glyph(G.home, 20))>; <span class:"brand-name", "Dreamhome">>
    <div class:"doc-title",
        <span class:"doc-name", st.home.title>
        <span class:"doc-meta", A.n(M.floor_area(st.home)) ++ " m² · " ++ string(len(st.home.rooms)) ++ " rooms · " ++
            string(len(M.furniture(st.home))) ++ " pieces">>
    <nav class:"segmented", 'aria-label':"Drawing tools",
        (tool("tool", "select", G.select, "Select", st.tool == "select"));
        (tool("tool", "wall", G.wall, "Wall", st.tool == "wall"));
        (tool("tool", "room", G.room, "Room", st.tool == "room"))>
    <nav class:"segmented", 'aria-label':"Views",
        (tool("layout", "plan", G.plan2d, "2D", st.layout == "plan"));
        (tool("layout", "split", G.split, "2D + 3D", st.layout == "split"));
        (tool("layout", "3d", G.cube, "3D", st.layout == "3d"))>
    <div class:"top-actions",
        (act("undo", "", "icon-btn", [icons.icon("undo", 18)], if (len(st.hist.past) == 0) {disabled:"", title:"Undo"} else {title:"Undo (⌘Z)"}));
        (act("redo", "", "icon-btn", [icons.icon("redo", 18)], if (len(st.hist.future) == 0) {disabled:"", title:"Redo"} else {title:"Redo (⇧⌘Z)"}));
        <span class:"divider">;
        (act("dialog", "homes", "ghost-btn", [glyph(G.home, 16), "Homes"]));
        (act("dialog", "open", "ghost-btn", [icons.icon("folder", 16), "Open"]));
        (act("save", "", "ghost-btn", [icons.icon("save", 16), "Save"]));
        (act("dialog", "export", "primary-btn", [icons.icon("export", 16), "Export"]))>>

// ---- catalog -------------------------------------------------------------------------------

fn matches(it, q) => q == "" or contains(lower(it.name), lower(q)) or contains(lower(it.key), lower(q))
fn tile(it) => act("add", it.key, "tile", [<span class:"thumb", TH.thumb(it.key)>, <span class:"tile-name", it.name>,
    <span class:"tile-size", cm(it.w) ++ " × " ++ cm(it.d) ++ (if (it.opening != null) "" else " × " ++ cm(it.h))>],
    {title:"Add " ++ lower(it.name) ++ (if (it.opening != null) " to a wall" else "")})
fn catalog(st) {
    let q = trim(st.query)
    let shown = [for (it in C.ITEMS where (if (q == "") it.cat == st.cat else matches(it, q))) it];
    <aside class:"catalog", 'aria-label':"Catalog",
        <header class:"pane-head", <h2 "Catalog">; <p "Click a piece to place it in the plan.">>
        <div class:"search", (icons.icon("search", 16));
            apply(<control.input id:"query", class:"field", value:st.query, placeholder:"Search sofa, window, lamp…", on_change:"query">)>
        if (q == "") <div class:"cats", for (c in C.CATEGORIES)
            act("cat", c.key, if (st.cat == c.key) "cat on" else "cat", [glyph(c.icon, 20), <span c.short>], {title:c.name})>;
        <h3 class:"shelf-title", if (q == "") [for (c in C.CATEGORIES where c.key == st.cat) c.name][0]
            else string(len(shown)) ++ " found for “" ++ q ++ "”">
        <div class:"tiles", for (it in shown) tile(it)>
        if (len(shown) == 0) <p class:"hint", "Nothing matches. Try a room name, like bed or kitchen.">>
}

// ---- panes ------------------------------------------------------------------------------------

fn hint(st) => if (st.tool == "wall") "Drag to draw a wall · ends snap to other walls · Shift draws at any angle · Esc stops"
    else if (st.tool == "room") "Drag a rectangle to add a room with its four walls · Esc stops"
    else if (st.sel != null and M.kind_of(st.sel) == "wall") "Drag the wall to move it with its corners · drag an end to reshape"
    else if (st.sel != null) "Drag to move · round knob rotates · corner resizes · R turns 90° · Delete removes"
    else "Click to select · drag the floor to pan · wheel zooms"
fn plan_pane(st, p) => <section id:"plan-pane", class:"pane-2d", style:"height:" ++ A.n(p.plan) ++ "px",
    (P.plan(st.home, st, p.w, p.plan));
    <div class:"pane-tools left",
        (icon_btn("zoom", 1.25, G.plus, "Zoom in")); (icon_btn("zoom", .8, G.minus, "Zoom out")); (icon_btn("fit", "", G.fit, "Fit the plan"));
        (act("dims", "", if (st.dims) "icon-btn on" else "icon-btn", [glyph(G.ruler)], {title:"Wall dimensions", 'aria-pressed':string(st.dims)}))>
    <div class:"pane-caption", hint(st)>
    <div class:"scale-bar", <span style:"width:" ++ A.n(100.0 * st.view2d.zoom) ++ "px">; <b "1 m">>>
fn view3d_pane(st, p) => <section id:"view3d-pane", class:"pane-3d", style:"height:" ++ A.n(p.view3d) ++ "px",
    (S.scene(st.home, st, int(p.w), int(p.view3d)));
    <div class:"pane-tools left",
        (tool("mode3d", "orbit", G.orbit, "Orbit", st.mode3d == "orbit"));
        (tool("mode3d", "walk", G.walk, "Walk through", st.mode3d == "walk"))>
    <div class:"pane-tools right",
        (act("cutaway", "", if (st.cutaway) "icon-btn on" else "icon-btn", [glyph(G.cut)], {title:"Cut the walls down", 'aria-pressed':string(st.cutaway)}));
        (act("evening", "", if (st.evening) "icon-btn on" else "icon-btn", [glyph(if (st.evening) G.moon else G.sun)],
            {title:if (st.evening) "Evening: lamps lit" else "Daylight", 'aria-pressed':string(st.evening)}));
        (icon_btn("reset3d", "", G.fit, "Reset the view"))>
    <div class:"pane-caption dark", if (st.mode3d == "walk") "Drag to look around · W A S D or the arrow keys walk · drag the blue walker in the plan"
        else "Drag to orbit · Shift-drag pans · wheel zooms">>

// ---- inspector -----------------------------------------------------------------------------

fn head(title, sub = "") => <header class:"pane-head inspector-head", <div <h2 title>; if (sub != "") <p sub>>; (icon_btn("deselect", "", "M6 6l12 12 M18 6L6 18", "Done"))>
fn item_inspector(it) {
    let c = C.item_of(it.key)
    let opening = c.opening;
    <div class:"pane",
        (head(c.name, if (opening != null) "In a wall" else C.CATEGORIES[[for (i, k in C.CATEGORIES where k.key == c.cat) i][0]].name));
        if (opening != null) (section("Size", [stepper("Width", "size-w", 10.0, cm(it.w)), stepper("Height", "size-h", 10.0, cm(it.h)),
            if (opening == "window") stepper("Sill", "elev", 5.0, cm(it.elev)) else null]))
        else (section("Size", [stepper("Width", "size-w", 5.0, cm(it.w)), stepper("Depth", "size-d", 5.0, cm(it.d)),
            stepper("Height", "size-h", 5.0, cm(it.h)), stepper("Off the floor", "elev", 5.0, cm(it.elev))]));
        if (opening == "door" or opening == "double") (section("Opening", [<div class:"tool-row",
            (act("flip", "", "tool-btn wide", [glyph(G.swing, 16), "Swing side"]));
            if (opening == "door") (act("mirror", "", "tool-btn wide", [glyph(G.mirror, 16), "Hinge side"]))>]))
        else if (opening == null) (section("Turn", [<div class:"tool-row",
            (act("rot", -15.0, "tool-btn", ["−15°"])); (act("rot", 15.0, "tool-btn", ["+15°"])); (act("rot", 90.0, "tool-btn wide", [glyph(G.turn, 16), "90°"]));
            <b class:"readout", cm(it.rot) ++ "°">>]));
        (section("Colour", [swatches("color", C.FINISHES, it.color)]));
        if (c.light != null) <p class:"hint", "A lamp: it lights the 3D view, brightest in the evening.">;
        (section("Arrange", [<div class:"tool-row",
            (act("duplicate", "", "tool-btn wide", [icons.icon("copy", 15), "Duplicate"], {title:"Duplicate (⌘D)"}));
            (act("delete", "", "tool-btn wide danger", [glyph(G.trash, 15), "Delete"], {title:"Delete (⌫)"}))>]))>
}
fn wall_inspector(home, w) => <div class:"pane",
    (head("Wall", string(len(M.openings_on(home, w.id))) ++ " doors and windows"));
    (section("Size", [stepper("Length", "wall-len", 10.0, cm(M.wall_len(w))), stepper("Thickness", "wall-thick", 2.5, A.n(w.thick))]));
    (section("Paint", [swatches("paint", C.PAINTS, w.color), act("paint-all", "", "wide-btn", ["Use this paint on every wall"])]));
    (section("Arrange", [<div class:"tool-row", (act("delete", "", "tool-btn wide danger", [glyph(G.trash, 15), "Delete wall"]))>]))>
fn room_inspector(r) => <div class:"pane",
    (head(r.name, A.n(M.area(r.pts) / 10000.0) ++ " m²"));
    (section("Name", [apply(<control.input id:"room-name", class:"field", value:r.name, on_change:"room-name">)]));
    (section("Floor", [<div class:"floors", for (f in C.FLOORS)
        act("floor", f.key, if (r.floor == f.key) "floor-tile on" else "floor-tile",
            [<span class:"floor-chip", style:"background:repeating-linear-gradient(90deg," ++ f.color ++ " 0 9px," ++ f.line ++ " 9px 10px)">, <span f.name>])>]));
    (section("Arrange", [<div class:"tool-row", (act("delete", "", "tool-btn wide danger", [glyph(G.trash, 15), "Delete floor"]))>]))>
fn home_inspector(st) {
    let h = st.home;
    <div class:"pane",
        <header class:"pane-head", <h2 "Home">; <p "Choose anything in the plan to change it.">>;
        (section("Name", [apply(<control.input id:"home-title", class:"field", value:h.title, on_change:"title">)]));
        (section("Walls", [stepper("Height", "wall-height", 10.0, cm(h.wall_height))]));
        (section("Rooms", [if (len(h.rooms) == 0) <p class:"hint", "No rooms yet. Use the Room tool to draw one.">
            else <div class:"rows", for (r in h.rooms) act("select", r.id, "row", [<span class:"row-chip", style:"background:" ++ C.floor_of(r.floor).color>,
                <span class:"row-name", r.name>, <span class:"row-meta", A.n(M.area(r.pts) / 10000.0) ++ " m²">])>]));
        (section("Furniture", [if (len(M.furniture(h)) == 0) <p class:"hint", "Nothing yet. Pick a piece from the catalog.">
            else <div class:"rows", for (it in M.furniture(h)) act("select", it.id, "row",
                [<span class:"row-chip", style:"background:" ++ it.color>, <span class:"row-name", C.item_of(it.key).name>,
                 <span class:"row-meta", cm(it.w) ++ "×" ++ cm(it.d) ++ "×" ++ cm(it.h)>])>]))>
}
fn inspector(st) => <aside class:"inspector", 'aria-label':"Inspector",
    (let k = M.kind_of(st.sel), let x = M.thing_of(st.home, st.sel),
        if (x == null) home_inspector(st) else if (k == "wall") wall_inspector(st.home, x)
        else if (k == "room") room_inspector(x) else item_inspector(x))>

// ---- dialogs -------------------------------------------------------------------------------

fn home_card(t) => act("template", t.key, "home-card", [<span class:"home-thumb", if (len(t.home.walls) == 0) glyph(G.plus, 40) else P.sheet(t.home, 232.0, 150.0, null, false)>,
    <span class:"home-name", t.title>, <span class:"home-blurb", t.blurb>], {title:"Start from " ++ t.title})
pub fn dialog(st, templates) => <div class:"dialog-backdrop",
    <div class:if (st.dialog == "homes") "dialog wide" else "dialog", role:"dialog",
        if (st.dialog == "homes") <div
            <h2 "Homes">
            <p "Start from a furnished home, or from an empty site. Undo brings back the one you had.">
            <div class:"home-cards", for (t in templates) home_card(t)>
            <div class:"dialog-actions", (act("dialog-close", "", "ghost-btn", ["Close"]))>>
        else if (st.dialog == "open") <div
            <h2 "Open a home">
            <p "A home saved by Dreamhome, as a .mark file.">
            apply(<control.input id:"file-path", class:"field", value:st.open_path, on_change:"open-path">);
            <div class:"dialog-actions", (act("dialog-close", "", "ghost-btn", ["Cancel"])); (act("open", "", "primary-btn", ["Open"]))>>
        else <div
            <h2 "Export">
            <p "Files are written to this folder, named after the home.">
            apply(<control.input id:"file-path", class:"field", value:st.path, on_change:"path">);
            <div class:"export-choices",
                (act("export", "png", "export-choice", [<b "Floor plan image">, <span "A PNG of the furnished plan with room areas and wall lengths.">]));
                (act("export", "pdf", "export-choice", [<b "Floor plan PDF">, <span "The same plan, ready to print.">]));
                (act("export", "svg", "export-choice", [<b "Floor plan SVG">, <span "The plan as vector art for other drawing tools.">]));
                (act("export", "glb", "export-choice", [<b "3D model">, <span "A glTF binary (.glb) of the whole home, for Blender or a web viewer.">]));
                (act("export", "mark", "export-choice", [<b "Home file">, <span "A .mark file Dreamhome can open and edit again.">]))>
            <div class:"dialog-actions", (act("dialog-close", "", "ghost-btn", ["Close"]))>>;
        if (st.note != "") <p class:"dialog-note", st.note>>>

// ---- the whole planner ----------------------------------------------------------------------

pub fn planner(st, templates) {
    let p = panes(st);
    <div id:"planner", class:"planner layout-" ++ st.layout ++ (if (st.drag != null) " dragging" else ""), 'data-tool':st.tool,
        'data-selected':if (st.sel == null) "" else st.sel, 'data-mode3d':st.mode3d,
        (topbar(st));
        <div class:"main",
            (catalog(st));
            <main class:"workspace", style:"width:" ++ A.n(p.w) ++ "px",
                if (p.plan > 0.0) plan_pane(st, p);
                if (p.view3d > 0.0) view3d_pane(st, p)>;
            (inspector(st))>
        <footer class:"statusbar",
            <span class:"status", st.status>
            <span class:"zoom", string(int(round(st.view2d.zoom * 100.0))) ++ "% · " ++ string(len(st.hist.past)) ++ " edits">>;
        if (st.dialog != null) dialog(st, templates)>
}
