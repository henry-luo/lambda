// The studio's chrome: top bar, tool rail and panels, the canvas, the
// inspector, the status bar, the preview and the file dialog. Everything here
// draws state; each control names its command with data-act (and data-arg),
// which greetings.ls dispatches.
import controls: lambda.ui.controls
import icons: ~~.mod_icons
import A: .mod_art
import T: .mod_templates
import M: .mod_model
import R: .mod_render
import E: .mod_effects
import P: .mod_popup
import music: .mod_music

// chrome sizes, as set in greetings.css: top bar, status bar, rail, panel, inspector
pub let CHROME = {top: 56.0, status: 30.0, rail: 76.0, panel: 300.0, inspector: 288.0, pad: 56.0, caption: 40.0}

pub let PANELS = [{key: "designs", name: "Designs", icon: "library"}, {key: "text", name: "Text", icon: "wordart"},
    {key: "art", name: "Art", icon: "star"}, {key: "photos", name: "Photos", icon: "album"},
    {key: "backdrop", name: "Backdrop", icon: "shading"}, {key: "effects", name: "Effects", icon: "film"},
    {key: "card", name: "Card", icon: "settings"}]
pub let VIEWS = [{key: "front", name: "Front"}, {key: "inside", name: "Inside"}, {key: "back", name: "Back"}]
// One descriptor drives the canvas buttons, inspector tabs and choice lists.
let EFFECT_TOOLS = [
    {key: "animation", name: "Animation", icon: "film", choices: E.ANIMATIONS, command: "animation",
     hint: "Choose Still to remove animation. Printed cards stay still."},
    {key: "music", name: "Music", icon: "sound", choices: E.MUSIC, command: "music",
     hint: "Choose No music to remove the song. Original tunes play softly on a loop."},
    {key: "popup", name: "Pop-up", icon: "book", choices: P.DESIGNS, command: "popup-design",
     hint: "Paper layers rise on the inside left as the card opens. Print exports show the flat illustration."}]
fn effect_tool(key) => [for (tool in EFFECT_TOOLS where tool.key == key) tool][0]
fn effect_value(card, tool) => if (tool.key == "popup") P.of(card).design else E.of(card)[tool.key]
fn effect_name(tool, value) => [for (entry in tool.choices where entry.key == value) entry.name][0]

// text presets for the Text panel: button label, sample text, font and size
pub let TEXT_STYLES = [
    {key: "heading", label: "Add a heading", text: "Your words here", font: "roundhand", size: 64.0, w: 420.0},
    {key: "subheading", label: "Add a subheading", text: "A few more words", font: "baskerville", size: 30.0, w: 380.0},
    {key: "body", label: "Add body text", text: "Write your message here.", font: "avenir", size: 20.0, w: 360.0},
    {key: "signature", label: "Add a signature", text: "With love,\nYour name", font: "bradley", size: 32.0, w: 300.0}]

fn act(verb, arg: any, cls, children, more = {}) => <button type:"button", class:cls, 'data-act':verb, 'data-arg':string(arg), *:more,
    for (c in children) c>
fn icon_btn(verb, arg: any, graphic, caption, more = {}) => act(verb, arg, "icon-btn", [icons.icon(graphic, 18)], {title:caption, 'aria-label':caption, *:more})
fn chip(verb, arg: any, caption, on) => act(verb, arg, if (on) "chip on" else "chip", [caption], {'aria-pressed':if (on) "true" else "false"})
fn section(title, children) => <section class:"pane-section", <h3 title>; for (c in children) c>
fn swatches(verb, current) => <div class:"swatches", for (c in A.PALETTE)
    act(verb, c, if (lower(current) == c) "swatch on" else "swatch", [], {style:"background:" ++ c, title:c})>

pub fn scale_for(st) {
    let f = T.format_of(st.card.format)
    let wide = float(f.w) * (if (st.view == "inside") 2.0 else 1.0)
    // The spread gap is in screen pixels; only the two paper widths scale.
    let w = st.vp.width - CHROME.rail - CHROME.panel - CHROME.inspector - CHROME.pad - (if (st.view == "inside") 28.0 else 0.0)
    let h = st.vp.height - CHROME.top - CHROME.status - CHROME.pad - CHROME.caption;
    max(.25, min(1.4, min(w / wide, h / f.h)))
}

// ---- top bar --------------------------------------------------------------------

fn topbar(st) => <header class:"topbar",
    <div class:"brand", <span class:"brand-mark", (A.art_svg("envelope", "#e11d48"))>; <span class:"brand-name", "Greetings">>
    <div class:"doc-title",
        <span class:"doc-name", st.card.title>
        <span class:"doc-meta", T.occasion_of(st.card.occasion).name ++ " · " ++ T.format_of(st.card.format).name ++ " " ++ T.format_of(st.card.format).size
            ++ (if (P.of(st.card).design != "none") " · Pop-up" else "")>>
    <nav class:"view-tabs", 'aria-label':"Card pages", for (v in VIEWS)
        act("view", v.key, if (st.view == v.key) "view-tab on" else "view-tab", [v.name], {'aria-pressed':if (st.view == v.key) "true" else "false"})>
    <div class:"top-actions",
        (icon_btn("undo", "", "undo", "Undo", if (len(st.hist.past) == 0) {disabled:""} else {}));
        (icon_btn("redo", "", "redo", "Redo", if (len(st.hist.future) == 0) {disabled:""} else {}));
        <span class:"divider">;
        (act("dialog", "open", "ghost-btn", [icons.icon("folder", 16), "Open"]));
        (act("save", "", "ghost-btn", [icons.icon("save", 16), "Save"]));
        (act("preview", "closed", "ghost-btn", [icons.icon("eye", 16), "Preview"]));
        (act("dialog", "export", "primary-btn", [icons.icon("export", 16), "Print & share"]))>>

// ---- rail and panels -------------------------------------------------------------

fn rail(st) => <nav class:"rail", 'aria-label':"Tools", for (p in PANELS)
    (let on = if (st.effect_editor != null) p.key == "effects" else st.panel == p.key,
    act("panel", p.key, if (on) "rail-btn on" else "rail-btn", [icons.icon(p.icon, 22), <span p.name>],
        {'aria-pressed':if (on) "true" else "false"}))>

fn occasion_chips(current) => <div class:"chips",
    (chip("occasion", "all", "All", current == "all"));
    for (o in T.OCCASIONS) chip("occasion", o.key, o.name, current == o.key)>

// the gallery's cards are assembled once; thumbnails are live renders of each front
fn design_tile(g, current) {
    let f = T.format_of(g.card.format)
    let s = 118.0 / max(f.w, f.h)
    let fx = E.of(g.card);
    act("template", g.t.key, if (current == g.t.key) "design on" else "design", [
        <span class:"design-thumb", (R.page(g.card, "front", s, {uid: "g-" ++ g.t.key}))>,
        <span class:"design-badges",
            if (fx.animation != "none") <span class:"design-badge animation-badge", title:"Animation: " ++ E.animation(fx.animation).name,
                'aria-label':"Animated card", (icons.icon("film", 12))>;
            if (fx.music != "none") <span class:"design-badge music-badge", title:"Music: " ++ E.music(fx.music).name,
                'aria-label':"Card with music", (icons.icon("sound", 12))>;
            if (P.of(g.card).design != "none") <span class:"design-badge popup-badge", title:"Pop-up: " ++ P.design(P.of(g.card).design).name,
                'aria-label':"Pop-up card", (icons.icon("book", 12))>>,
        <span class:"design-name", g.t.name>,
        <span class:"design-occasion", T.occasion_of(g.t.occasion).name>], {title:"Start from " ++ g.t.name})
}
fn designs_panel(st, gallery) => <div class:"pane",
    <header class:"pane-head", <h2 "Designs">; <p "Pick a design, then make it yours.">>;
    (occasion_chips(st.occasion));
    <div class:"chips", (chip("occasion", "popup", "Pop-up cards", st.occasion == "popup"))>;
    <div class:"design-legend", for (tool in EFFECT_TOOLS) <span (icons.icon(tool.icon, 12)); " " ++ tool.name>>;
    <div class:"designs", for (g in gallery where st.occasion == "all" or g.t.occasion == st.occasion
        or (st.occasion == "popup" and P.of(g.card).design != "none")) design_tile(g, st.card.template)>>

fn verse_occasion(st) => if (st.occasion == "all" or st.occasion == "popup") st.card.occasion else st.occasion
fn text_panel(st) {
    let occ = verse_occasion(st)
    let sel = M.item_of(st.card, st.page, st.sel);
    <div class:"pane",
        <header class:"pane-head", <h2 "Text">; <p "Add words, or pick a verse for the inside.">>
        <div class:"text-styles", for (t in TEXT_STYLES)
            act("add-text", t.key, "text-style ts-" ++ t.key, [t.label], {style:"font-family:" ++ A.font_of(t.font).family})>;
        (section("Verses · " ++ T.occasion_of(occ).name, [
            occasion_chips(occ),
            <p class:"hint", if (sel != null and sel.kind == "text") "A verse replaces the selected text." else "A verse is added to the inside right page.">,
            <div class:"verses", for (i, v in T.VERSES[occ]) act("verse", occ ++ "/" ++ string(i), "verse", [v])>]))>
}

fn art_tile(a) => act("add-art", a.key, "art-tile", [<span class:"art-thumb", (A.art_svg(a.key, a.ink))>, <span class:"art-name", a.name>], {title:"Add " ++ a.name})
fn art_panel(st) => <div class:"pane",
    <header class:"pane-head", <h2 "Art">; <p "Click a piece to add it to the page.">>;
    for (g in A.GROUPS) section(g.name, [<div class:"art-grid", for (a in A.ART where a.group == g.key) art_tile(a)>])>

fn photos_panel(st, samples) {
    let sel = M.item_of(st.card, st.page, st.sel);
    <div class:"pane",
        <header class:"pane-head", <h2 "Photos">; <p if (sel != null and sel.kind == "photo") "Click a photo to put it in the selected frame." else "Click a photo to add it in a frame.">>;
        (section("Samples", [<div class:"photo-grid", for (p in samples)
            act("add-photo", p.key, "photo-tile", [<span class:"photo-thumb", style:"background-image:url('" ++ p.url ++ "')">, <span class:"art-name", p.name>])>]));
        (section("From a file", [
            <p class:"hint", "A JPEG, PNG or GIF path, absolute or relative to where Lambda runs.">,
            apply(<control.input id:"photo-path", class:"field", value:st.photo_path, placeholder:"~/Pictures/photo.jpg", on_change:"photo-path">),
            act("photo-file", "", "wide-btn", ["Add photo"])]))>
}

// pattern and border tiles show their shape in one neutral ink, whatever the current colour
let PREVIEW_INK = "#8a7d86"
fn paper_tile(p, on) => act("paper", p.key, if (on) "paper-tile on" else "paper-tile", [<span class:"paper-chip", style:"background:" ++ p.bg>, <span p.name>])
fn pattern_tile(p, pg, on) => act("pattern", p.key, if (on) "pattern-tile on" else "pattern-tile",
    [<span class:"pattern-chip", style:"background:" ++ A.paper_of(pg.paper).bg,
        if (p.key != "none") A.pattern_layer(p.key, PREVIEW_INK, .55, "pp-" ++ p.key, 120, 120)>, <span p.name>])
fn border_tile(b, pg, on) => act("border", b.key, if (on) "pattern-tile on" else "pattern-tile",
    [<span class:"pattern-chip", style:"background:" ++ A.paper_of(pg.paper).bg,
        if (b.key != "none") A.border_layer(b.key, PREVIEW_INK, 160, 160)>, <span b.name>])
fn backdrop_panel(st) {
    let pg = st.card.pages[st.page];
    <div class:"pane",
        <header class:"pane-head", <h2 "Backdrop">; <p "For the " ++ lower(T.page_name(st.page)) ++ " page.">>;
        (section("Paper", [<div class:"paper-grid", for (p in A.PAPERS) paper_tile(p, pg.paper == p.key)>]));
        (section("Pattern", [<div class:"pattern-grid", for (p in A.PATTERNS) pattern_tile(p, pg, pg.pattern == p.key)>,
            swatches("pattern-color", pg.pattern_color),
            <div class:"stepper", <span "Strength">; (icon_btn("pattern-alpha", -0.05, "minus", "Fainter"));
                <b string(int(round(pg.pattern_alpha * 100))) ++ "%">; (icon_btn("pattern-alpha", 0.05, "plus", "Stronger"))>]));
        (section("Border", [<div class:"pattern-grid", for (b in A.BORDERS) border_tile(b, pg, pg.border == b.key)>,
            swatches("border-color", pg.border_color)]));
        (act("backdrop-all", "", "wide-btn", ["Use this backdrop on every page"]))>
}

fn card_panel(st) => <div class:"pane",
    <header class:"pane-head", <h2 "Card">; <p "Name, shape and occasion.">>;
    (section("Name", [apply(<control.input id:"card-title", class:"field", value:st.card.title, on_change:"title">)]));
    (section("Shape", [<div class:"formats", for (f in T.FORMATS) act("format", f.key, if (st.card.format == f.key) "format on" else "format",
        [<span class:"format-shape fs-" ++ f.key>, <span class:"format-name", f.name>, <span class:"format-size", f.size>])>]));
    (section("Occasion", [<div class:"chips", for (o in T.OCCASIONS) chip("card-occasion", o.key, o.name, st.card.occasion == o.key)>,
        <p class:"hint", "The occasion picks the verses offered in the Text panel.">]));
    (section("Start over", [act("blank", "", "wide-btn", ["Blank card in this shape"])]))>

fn effect_choice(verb: string, entry: {key: string, name: string, hint: string}, current: string) => act(verb, entry.key, if (entry.key == current) "effect-choice on" else "effect-choice",
    [<b entry.name>, <span entry.hint>], {'aria-pressed':if (entry.key == current) "true" else "false"})
fn effects_inspector(st) {
    let kind = st.effect_editor
    let tool = effect_tool(kind)
    let value = effect_value(st.card, tool)
    let popup = P.of(st.card);
    <div id:"effect-settings", class:"pane", 'data-kind':kind,
        <header class:"pane-head inspector-head", <h2 tool.name>;
            (icon_btn("effect-settings", "", "close", "Done with effects"))>;
        <p class:"hint", "For the whole card. Play your changes in Preview.">;
        <div class:"effect-tabs", for (tool in EFFECT_TOOLS) act("effect-settings", tool.key,
            if (kind == tool.key) "effect-tab on" else "effect-tab", [icons.icon(tool.icon, 15), tool.name],
            {'aria-pressed':if (kind == tool.key) "true" else "false"})>;
        <div class:"effect-choices", for (entry in tool.choices) effect_choice(tool.command, entry, value)>;
        if (kind == "popup" and value != "none") <div
            (section("Depth", [<div class:"chips", for (d in P.DEPTHS) chip("popup-depth", d.key, d.name, popup.depth == d.key)>]));
            (section("Colour", [swatches("popup-color", popup.color)]))>;
        <p class:"hint", tool.hint>;
        (act("preview", "closed", "wide-btn", [icons.icon("eye", 16), " Preview this card"]))>
}

fn panel(st, gallery, samples) => if (st.panel == "text") text_panel(st) else if (st.panel == "art") art_panel(st)
    else if (st.panel == "photos") photos_panel(st, samples) else if (st.panel == "backdrop") backdrop_panel(st)
    else if (st.panel == "card") card_panel(st) else designs_panel(st, gallery)

// ---- canvas ----------------------------------------------------------------------

// These controls describe the edited card, including effects added after choosing a template.
fn canvas_effect(st, tool) {
    let value = effect_value(st.card, tool)
    let active = value != "none"
    let caption = (if (active) "Edit " else "Add ") ++ lower(tool.name)
    let current = effect_name(tool, value);
    act("effect-settings", tool.key, "canvas-effect" ++ (if (active) " has-effect" else " add-effect")
        ++ (if (st.effect_editor == tool.key) " is-open" else ""),
        [icons.icon(tool.icon, 16), if (not active) <span class:"effect-add", (icons.icon("plus", 8))>],
        {title:caption ++ (if (active) ": " ++ current else ""), 'aria-label':caption,
         'aria-expanded':if (st.effect_editor == tool.key) "true" else "false", 'aria-controls':"effect-settings", 'data-value':value})
}
// The paper owns the shell width; percentage-width headings must not size the spread.
fn page_shell(st, key, s) => <div class:if (st.page == key) "page-shell current" else "page-shell",
    style:"width:" ++ A.n(T.format_of(st.card.format).w * s) ++ "px",
    <div class:"page-heading", <span class:"page-label", T.page_name(key)>;
        if (st.page == key) <div class:"canvas-effects", 'aria-label':"Card effects", for (tool in EFFECT_TOOLS) canvas_effect(st, tool)>>
    <div class:"page-paper " ++ key, (R.page(st.card, key, s, {uid: "ed", sel: if (st.page == key) st.sel else null,
        guides: if (st.drag != null and st.drag.key == key) {x: false, y: false} else null}))>>
fn canvas(st, s) => <main class:"workspace",
    <div id:"canvas", class:"canvas view-" ++ st.view,
        if (st.view == "inside") <div class:"spread", (page_shell(st, "left", s)); (page_shell(st, "right", s))>
        else page_shell(st, st.view, s)>
    <div class:"canvas-caption", if (st.sel != null) "Drag to move · corner to resize · top dot to rotate · Delete removes"
        else "Click an item to edit it · Click the page to choose it for adding">>

// ---- inspector ---------------------------------------------------------------------

fn stepper(caption, verb, delta, value, unit = "") => <div class:"stepper", <span caption>;
    (icon_btn(verb, 0.0 - delta, "minus", "Less " ++ lower(caption))); <b value ++ unit>; (icon_btn(verb, delta, "plus", "More " ++ lower(caption)))>
fn toggle(verb, arg: any, caption, on, graphic) => act(verb, arg, if (on) "tool-btn on" else "tool-btn", [icons.icon(graphic, 16)], {title:caption, 'aria-label':caption, 'aria-pressed':if (on) "true" else "false"})

fn text_inspector(it) => [
    section("Words", [apply(<control.text_area id:"text-content", class:"field area", rows:4, value:it.text, on_change:"text">)]),
    section("Font", [<div class:"fonts", for (f in A.FONTS)
        act("font", f.key, if (it.font == f.key) "font-btn on" else "font-btn", [f.name], {style:"font-family:" ++ f.family})>]),
    section("Style", [<div class:"tool-row",
        (toggle("bold", "", "Bold", it.bold, "bold")); (toggle("italic", "", "Italic", it.italic, "italic"));
        <span class:"divider">;
        (toggle("align", "left", "Align left", it.align == "left", "align_left")); (toggle("align", "center", "Centre", it.align == "center", "align_center"));
        (toggle("align", "right", "Align right", it.align == "right", "align_right"));
        <span class:"divider">;
        (act("shadow", "", if (it.shadow) "tool-btn wide on" else "tool-btn wide", ["Shadow"]))>,
        stepper("Size", "size", 2.0, string(int(round(it.size)))),
        stepper("Spacing", "spacing", 0.5, A.n(it.spacing)),
        stepper("Line height", "line", 0.05, A.n(it.line))]),
    section("Colour", [swatches("color", it.color)])]

fn art_inspector(it) => [
    section("Colour", [swatches("color", it.color), <p class:"hint", "Art keeps its own accents and shading.">]),
    section("Size", [stepper("Width", "grow", 10.0, string(int(round(it.w)))), <div class:"tool-row", (act("flip", "", if (it.flip) "tool-btn wide on" else "tool-btn wide", ["Mirror"]))>])]

fn photo_inspector(it, samples) => [
    section("Frame", [<div class:"chips", for (f in A.FRAMES) chip("frame", f.key, f.name, it.frame == f.key)>]),
    section("Photo", [stepper("Zoom", "zoom", 0.1, string(int(round(it.zoom * 100))), "%"),
        <div class:"photo-grid small", for (p in samples) act("add-photo", p.key, "photo-tile", [<span class:"photo-thumb", style:"background-image:url('" ++ p.url ++ "')">])>])]

fn layer_label(it) => if (it.kind == "text") (let line = join(split(it.text, "\n"), " "), if (len(line) > 26) slice(line, 0, 26) ++ "…" else line)
    else if (it.kind == "art") A.art_of(it.art).name else "Photo"
fn layer_icon(it) => if (it.kind == "text") "wordart" else if (it.kind == "art") "star" else "album"
fn layers(st) => section("On this page", [
    if (len(M.items(st.card, st.page)) == 0) <p class:"hint", "Nothing yet. Add text, art or a photo from the left.">
    else <div class:"layers", for (it in reverse(M.items(st.card, st.page)))
        act("select", it.id, if (it.id == st.sel) "layer on" else "layer", [icons.icon(layer_icon(it), 15), <span layer_label(it)>])>])

fn inspector(st, samples) {
    let it = M.item_of(st.card, st.page, st.sel);
    <aside class:"inspector", 'aria-label':"Inspector",
        if (st.effect_editor != null) effects_inspector(st)
        else if (it == null) <div class:"pane",
            <header class:"pane-head", <h2 T.page_name(st.page)>; <p "Choose an item to change it.">>;
            (layers(st))>
        else <div class:"pane",
            <header class:"pane-head inspector-head",
                <h2 if (it.kind == "text") "Text" else if (it.kind == "art") A.art_of(it.art).name else "Photo">;
                (icon_btn("deselect", "", "close", "Done"))>;
            for (s in (if (it.kind == "text") text_inspector(it) else if (it.kind == "art") art_inspector(it) else photo_inspector(it, samples))) s;
            (section("Arrange", [
                stepper("Rotation", "rot", 5.0, A.n(it.rot), "°"),
                stepper("Opacity", "opacity", 0.1, string(int(round(it.opacity * 100))), "%"),
                <div class:"tool-row",
                    (act("order", "forward", "tool-btn wide", ["Forward"], {title:"Bring forward ( ] )"}));
                    (act("order", "backward", "tool-btn wide", ["Backward"], {title:"Send backward ( [ )"}));
                    (act("duplicate", "", "tool-btn wide", [icons.icon("copy", 15), "Copy"], {title:"Duplicate (⌘D)"}));
                    (act("delete", "", "tool-btn wide danger", ["Delete"], {title:"Delete (⌫)"}))>]));
            (layers(st))>>
}

// ---- preview: the card on a table, opening on its fold ------------------------------

pub fn preview_effects(st) {
    let fx = E.of(st.card);
    <div class:"preview-effects",
        if (fx.animation != "none") act("motion-toggle", "", "ghost-btn light", [icons.icon(if (st.motion_paused) "resume" else "pause", 16),
            if (st.motion_paused) "Resume animation" else "Pause animation"], {'aria-pressed':if (st.motion_paused) "true" else "false"});
        if (fx.music != "none") act("music-toggle", "", "ghost-btn light", [icons.icon("sound", 16),
            if (st.sound.muted or st.sound.failed) "Play music" else "Mute music"], {'aria-pressed':if (st.sound.muted) "true" else "false"});
        <span class:"preview-music", if (st.sound.failed) "Music unavailable. Try Play music again."
            else if (fx.music == "none") "No music" else E.music(fx.music).name>>
}
pub fn preview_bar(st) => <div class:"preview-bar",
    (act("preview", if (st.preview == "open") "closed" else "open", "primary-btn", [if (st.preview == "open") "Close the card" else "Open the card"]));
    if (P.of(st.card).design != "none") act("preview", "half", "ghost-btn light", ["Half open"], {'aria-pressed':if (st.preview == "half") "true" else "false"});
    (act("preview", if (st.preview == "back") "closed" else "back", "ghost-btn light", [if (st.preview == "back") "Show the front" else "Turn it over"]));
    (act("preview-done", "", "ghost-btn light", ["Back to editing"]))>

pub fn preview(st) {
    let f = T.format_of(st.card.format)
    let has_popup = P.of(st.card).design != "none"
    let s = P.preview_scale(st.card, f.w, f.h, max(1.0, st.vp.width - 80.0),
        max(1.0, st.vp.height - (if (st.vp.width < 480.0) 260.0 else 190.0)))
    let w = R.page(st.card, "front", s, {uid: "pv", animate: true});
    <div class:"preview" ++ (if (st.motion_paused) " effects-paused" else ""), role:"dialog", 'aria-label':"Preview",
        <div class:"preview-stage", style:"width:" ++ A.n(f.w * s * 2.0) ++ "px;height:" ++ A.n(f.h * s) ++ "px;perspective:" ++ A.n(P.CAMERA.perspective) ++ "px",
            <div class:"book " ++ st.preview ++ (if (has_popup) " popup-book" else ""),
                'data-width':A.n(f.w * s), 'data-height':A.n(f.h * s),
                style:"width:" ++ A.n(f.w * s) ++ "px;height:" ++ A.n(f.h * s) ++ "px;" ++ P.camera_style(),
                <div class:"leaf leaf-right", (R.page(st.card, "right", s, {uid: "pv", animate: true}))>
                <div class:"cover",
                    <div class:"face face-front", w>; <div class:"turn-shade turn-shade-front">;
                    <div class:"face face-inside", (R.page(st.card, "left", s, {uid: "pv", animate: true, popup_live: true}))>;
                    <div class:"turn-shade turn-shade-inside">>
                <div class:"leaf leaf-back", (R.page(st.card, "back", s, {uid: "pv", animate: true}))>>>;
        <div class:"preview-effects-host", (preview_effects(st))>;
        <div class:"preview-bar-host", (preview_bar(st))>>
}

// ---- dialogs -----------------------------------------------------------------------

pub fn dialog(st) => <div class:"dialog-backdrop",
    <div class:"dialog", role:"dialog", 'aria-label':if (st.dialog == "open") "Open a card" else "Print and share",
        if (st.dialog == "open") <div
            <h2 "Open a card">
            <p "A card saved by Greetings, as a .mark file.">
            apply(<control.input id:"file-path", class:"field", value:st.open_path, on_change:"open-path">);
            <div class:"dialog-actions", (act("dialog-close", "", "ghost-btn", ["Cancel"])); (act("open", "", "primary-btn", ["Open"]))>>
        else <div
            <h2 "Print & share">
            <p "Files are written to this folder, named after the card.">
            apply(<control.input id:"file-path", class:"field", value:st.path, on_change:"path">);
            <div class:"export-choices",
                (act("export", "png", "export-choice", [<b "Page images">, <span "Four PNGs, one per page, at 300 dpi.">]));
                (act("export", "sheets", "export-choice", [<b "Print sheets">, <span "Two PNGs to print back to back and fold in half.">]));
                (act("export", "pdf", "export-choice", [<b "Print-ready PDF">, <span "Both sheets in one PDF, the size of the unfolded card.">]));
                (act("export", "mark", "export-choice", [<b "Card file">, <span "A .mark file Greetings can open and edit again.">]))>
            <div class:"dialog-actions", (act("dialog-close", "", "ghost-btn", ["Close"]))>>;
        if (st.note != "") <p class:"dialog-note", st.note>>>

// ---- the whole studio --------------------------------------------------------------

pub fn studio(st, gallery, samples) {
    let s = scale_for(st);
    <div id:"studio", class:"studio" ++ (if (st.drag != null) " dragging" else ""), 'data-view':st.view, 'data-page':st.page,
        'data-panel':st.panel, 'data-selected':if (st.sel == null) "" else st.sel,
        'data-animation':E.of(st.card).animation, 'data-music':E.of(st.card).music,
        'data-effect-editor':st.effect_editor or "",
        'data-popup':P.of(st.card).design, 'data-popup-depth':P.of(st.card).depth, 'data-popup-color':P.of(st.card).color,
        'data-sound':music.status(st.sound), 'data-sound-count':string(st.sound.played),
        (topbar(st));
        <div class:"main",
            (rail(st));
            <div class:"panel", (panel(st, gallery, samples))>;
            (canvas(st, s));
            (inspector(st, samples))>
        <footer class:"statusbar",
            <span class:"status", st.status>
            <span class:"zoom", string(int(round(s * 100))) ++ "% · " ++ string(len(st.hist.past)) ++ " edits">>;
        if (st.dialog != null) dialog(st)>
}
