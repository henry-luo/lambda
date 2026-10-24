// Collage markup (S12.1.3). `canvas` draws one collage at any scale with inline styles only, so the
// editor and the exported document are the same markup: the export matches the screen.
import L: .mod_library
import C: .mod_collage

fn px(v) => string(round(v * 100) / 100) ++ "px"
fn pct(v) => string(round(v * 1000) / 10) ++ "%"
fn box(x, y, w, h, s) => "position:absolute;left:" ++ px(x * s) ++ ";top:" ++ px(y * s) ++ ";width:" ++ px(w * s) ++ ";height:" ++ px(h * s) ++ ";"
fn face(f) => "font-family:" ++ f.css ++ ";font-style:" ++ f.style ++ ";font-weight:" ++ f.weight ++ ";"

// a photo filling its box at the slot's focal point and zoom; a missing file shows its colour
fn picture(p, slot, source) {
    let src = if (source == "photo" and p.has_photo) p.photo_src else p.thumb_src
    let focus = pct(slot.fx) ++ " " ++ pct(slot.fy);
    if (src == "") null
    else <img src:src, alt:"", draggable:"false",
        style:"display:block;width:100%;height:100%;object-fit:cover;object-position:" ++ focus ++
            ";transform:scale(" ++ string(slot.zoom) ++ ");transform-origin:" ++ focus ++ ";">
}

// a cell or print's slot attributes; template previews are inert, so presses never pick their cells
fn slot_attrs(c, p, selected, inert) => if (inert) {} else {'data-slot':string(c.slot), 'aria-selected':string(selected == c.slot), title:p.title}

fn tile(lib, m, c, s, ox, oy, source, selected, inert) {
    let slot = m.slots[c.slot]
    let p = L.photo(lib, slot.id)
    let frame = if (m.border > 0) "box-shadow:inset 0 0 0 " ++ px(m.border * s) ++ " #ffffff;" else "";
    <div class:"cl-cell", *:slot_attrs(c, p, selected, inert),
        style:box(c.x - ox, c.y - oy, c.w, c.h, s) ++ "overflow:hidden;border-radius:" ++ px(m.radius * s) ++
            ";background:" ++ (p.color or "#cccccc") ++ ";" ++ (if (c.clip != null) "clip-path:" ++ c.clip ++ ";" else ""),
        (picture(p, slot, source));
        (if (m.border > 0) <div style:"position:absolute;inset:0;border-radius:inherit;" ++ frame> else null)>
}

fn print_card(lib, m, c, s, source, selected, handles, inert) {
    let slot = m.slots[c.slot]
    let p = L.photo(lib, slot.id)
    let pad = c.w * 0.05
    let photo_h = c.h - pad - c.w * 0.16;
    <div class:"cl-print", *:slot_attrs(c, p, selected, inert),
        style:box(c.x, c.y, c.w, c.h, s) ++ "box-sizing:border-box;padding:" ++ px(pad * s) ++ ";background:#fffdf8;" ++
            "transform:rotate(" ++ string(round(c.rot * 100) / 100) ++ "deg);box-shadow:0 " ++ px(6.0 * s) ++ " " ++ px(18.0 * s) ++ " rgba(0,0,0,0.28);",
        <div style:"position:relative;overflow:hidden;width:100%;height:" ++ px(photo_h * s) ++ ";background:" ++ (p.color or "#cccccc") ++ ";",
            (picture(p, slot, source))>;
        <div style:"height:" ++ px(c.w * 0.16 * s) ++ ";display:flex;align-items:center;justify-content:center;overflow:hidden;" ++
            face(C.find_font("hand")) ++ "color:#4a4036;font-size:" ++ px(c.w * 0.055 * s) ++ ";",
            (if (c.label) p.title else null)>;
        // editor-only handles on the selected print; exports never pass `handles`
        *(if (handles and selected == c.slot) [<div class:"cl-handle cl-rotate", 'data-handle':"rotate", title:"Drag to rotate">,
            <div class:"cl-handle cl-resize", 'data-handle':"resize", title:"Drag to resize">] else [])>
}

// balloon ropes and a woven basket; stripe stops are percentages so the weave scales with the canvas
fn decoration(d, s) => if (d.kind == "rope") <div style:box(d.x, d.y, d.w, d.h, s) ++ "background:#6b4f36;clip-path:" ++ d.clip ++ ";">
    else <div class:"cl-basket", style:box(d.x, d.y, d.w, d.h, s) ++ "border-radius:" ++ px(d.h * 0.08 * s) ++ " " ++ px(d.h * 0.08 * s) ++ " " ++
        px(d.h * 0.3 * s) ++ " " ++ px(d.h * 0.3 * s) ++ ";border-top:" ++ px(d.h * 0.14 * s) ++ " solid #7a4f27;" ++
        "background:repeating-linear-gradient(90deg, #b9824a 0%, #b9824a 7%, #9c6835 7%, #9c6835 12%);box-shadow:0 " ++ px(4.0 * s) ++ " " ++ px(10.0 * s) ++ " rgba(0,0,0,0.25);">

// options: {scale, source: "thumb" | "photo", selected: slot index or -1, handles: editor handles on
// a selected print, inert: no slot attributes (template previews)}
pub fn canvas(lib, m, lay, options) {
    let s = options.scale
    let bg = C.background(m)
    let f = C.font(m)
    let inert = options.inert == true
    let ink = if (lay.plate != null) "#1d1d1f" else bg.ink
    let cells = [for (c in lay.cells) if (c.kind == "print") print_card(lib, m, c, s, options.source, options.selected, options.handles == true, inert)
        else tile(lib, m, c, s, if (lay.mask != null) lay.mask.x else 0.0, if (lay.mask != null) lay.mask.y else 0.0,
            options.source, options.selected, inert)];
    <div class:"cl-canvas", style:"position:relative;overflow:hidden;width:" ++ px(lay.width * s) ++ ";height:" ++ px(lay.height * s) ++
        ";background:" ++ bg.css ++ ";color:" ++ bg.ink ++ ";font-family:-apple-system,'Helvetica Neue',Arial,sans-serif;",
        *[for (d in lay.decor) decoration(d, s)];
        *(if (lay.mask != null) [<div class:"cl-mask", style:box(lay.mask.x, lay.mask.y, lay.mask.w, lay.mask.h, s) ++ "clip-path:" ++ lay.mask.clip ++ ";",
            *cells>] else cells);
        // the centred title sits on a plate over the photos
        (if (lay.plate != null) <div class:"cl-plate", style:box(lay.plate.x, lay.plate.y, lay.plate.w, lay.plate.h, s) ++
            "border-radius:" ++ px(lay.plate.h * 0.5 * s) ++ ";background:rgba(255,255,255,0.93);box-shadow:0 " ++ px(8.0 * s) ++ " " ++ px(30.0 * s) ++ " rgba(0,0,0,0.22);"> else null);
        (if (lay.caption != null) <div class:"cl-caption", style:box(lay.caption.x, lay.caption.y, lay.caption.w, lay.caption.h, s) ++
            "display:flex;align-items:center;justify-content:center;white-space:nowrap;overflow:hidden;letter-spacing:0.02em;color:" ++ ink ++ ";" ++
            face(f) ++ "font-size:" ++ px(lay.caption.h * f.size * s) ++ ";",
            m.caption> else null);
        (if (lay.note != null) <div class:"cl-note", style:box(lay.note.x, lay.note.y, lay.note.w, lay.note.h, s) ++
            "display:flex;align-items:flex-start;justify-content:center;white-space:nowrap;overflow:hidden;letter-spacing:0.08em;color:" ++ ink ++
            ";font-size:" ++ px(lay.note.h * 0.42 * s) ++ ";",
            m.note> else null);
        (if (lay.credits != null) <div class:"cl-credits", style:box(lay.credits.x + 24.0, lay.credits.y, lay.credits.w - 48.0, lay.credits.h, s) ++
            "overflow:hidden;white-space:nowrap;line-height:" ++ px(lay.credits.h * s) ++ ";font-size:" ++ px(lay.credits.h * 0.4 * s) ++ ";",
            C.credits_line(lib, m)> else null)>
}

// a standalone document for radiant.export_document: the canvas alone, at export pixels
pub fn export_document(lib, m, lay, scale) => <html <head <meta charset:"UTF-8"> <style "html,body{margin:0;padding:0;}">>
    <body (canvas(lib, m, lay, {scale:scale, source:"photo", selected:-1}))>>

pub fn tray(lib, ids, m) => <div class:"cl-tray-list",
    *[for (id in ids) (let p = L.photo(lib, id),
        <div class:"cl-tray-item", 'data-tray':string(id), 'data-used':string(contains([for (s in m.slots) s.id], id)), title:p.title,
            style:"background-color:" ++ (p.color or "#ccc"),
            (if (p.thumb_src != "") <img src:p.thumb_src, alt:"", draggable:"false"> else null)>)]>

// Template previews: each template's own layout and look over nine colour swatches standing in
// for photos of mixed shapes, drawn once when the editor shell is built.
let SAMPLE_COLORS = ["#f4a259", "#5b8e7d", "#bc4b51", "#8cb369", "#4f6d7a", "#e9c46a", "#7d5ba6", "#f28482", "#84a59d"]
let SAMPLE_ASPECTS = [1.5, 0.67, 1.33, 1.0, 1.5, 0.75, 1.33, 1.78, 1.0]
let SAMPLE_LIB = {photos:[for (c in SAMPLE_COLORS) {title:"", color:c, thumb_src:"", photo_src:"", has_photo:false}]}
let PREVIEW_SIZE = 64.0

fn preview(t) {
    let m = {*:C.apply_template(C.create([for (i in 1 to len(SAMPLE_COLORS)) i], "Aa"), t.key), credits:false}
    let lay = C.layout(m, SAMPLE_ASPECTS);
    canvas(SAMPLE_LIB, m, lay, {scale:PREVIEW_SIZE / lay.width, source:"thumb", selected:-1, inert:true})
}

fn option(key, title, chosen) => <option value:key, *:(if (key == chosen) {selected:""} else {}), title>
fn slider(id, label, lo, hi, value) => <label class:"cl-field", <span label>;
    <input id:id, type:"range", min:string(lo), max:string(hi), step:"1", value:string(value)>>

// the editor shell; regions (#cl-canvas-host, #cl-tray, #cl-saved) are filled when it opens
pub fn editor() => <div id:"collage", class:"collage-layer", style:"display:none", role:"dialog", 'aria-modal':"true", 'aria-labelledby':"cl-title", tabindex:"-1",
    <header class:"cl-bar",
        <button type:"button", class:"tool", id:"cl-close", 'data-action':"collage-close", title:"Close collage (Esc)", 'aria-label':"Close collage", "✕">;
        <h2 id:"cl-title", "Collage">;
        <span class:"cl-spacer">;
        <button type:"button", id:"cl-undo", 'data-action':"collage-undo", title:"Undo (⌘Z)", "Undo">;
        <button type:"button", id:"cl-redo", 'data-action':"collage-redo", title:"Redo (⇧⌘Z)", "Redo">;
        <button type:"button", id:"cl-save", 'data-action':"collage-save", title:"Save the collage to reopen later", "Save">>;
    <div class:"cl-body",
        <aside class:"cl-tray", <h3 "Photos">;<p class:"cl-hint", "Drag a photo onto a cell to place it.">;<div id:"cl-tray">>;
        <div id:"cl-stage", class:"cl-stage", <div id:"cl-canvas-host", class:"cl-canvas-host">>;
        <aside class:"cl-panel",
            <h3 "Template">;
            <div class:"cl-templates", role:"group", 'aria-label':"Template",
                *[for (t in C.TEMPLATES) <button type:"button", class:"cl-template", 'data-collage-template':t.key, 'aria-pressed':"false", title:t.title,
                    (preview(t)); <span t.title>>]>;
            <label class:"cl-field", <span "Canvas">;
                <select id:"cl-aspect", *[for (a in C.ASPECTS) option(a.key, a.title, "square")]>>;
            (slider("cl-gutter", "Spacing", 0, 80, 16));
            (slider("cl-radius", "Corners", 0, 60, 6));
            (slider("cl-border", "White border", 0, 30, 0));
            <div class:"cl-field", <span "Background">;
                <div class:"cl-swatches", *[for (b in C.BACKGROUNDS) <button type:"button", class:"cl-swatch", 'data-collage-bg':b.key,
                    title:b.title, 'aria-label':b.title, 'aria-pressed':"false", style:"background:" ++ b.css>]>>;
            <label class:"cl-field", <span "Caption">;<input id:"cl-caption", type:"text", autocomplete:"off">>;
            <label class:"cl-field", <span "Title font">;
                <select id:"cl-font", *[for (f in C.FONTS) option(f.key, f.title, "sans")]>>;
            <label class:"cl-field", <span "Note">;<input id:"cl-note", type:"text", autocomplete:"off", placeholder:"A line under the caption">>;
            <label class:"cl-check", <input id:"cl-credits", type:"checkbox">;<span "Credits line">>;
            <button type:"button", class:"cl-wide", 'data-action':"collage-shuffle", id:"cl-shuffle", "Shuffle prints">;
            <h3 "Export">;
            <label class:"cl-field", <span "Size (long edge)">;
                <select id:"cl-size", (option("1080", "1080 px", "2048")); (option("2048", "2048 px", "2048")); (option("4096", "4096 px", "2048"))>>;
            <label class:"cl-field", <span "Format">;
                <select id:"cl-format", (option("png", "PNG", "png")); (option("jpeg", "JPEG", "png"))>>;
            <button type:"button", class:"cl-wide primary", 'data-action':"collage-export", id:"cl-export", "Export image">;
            <p id:"cl-status", class:"cl-status", role:"status">;
            <h3 "Saved collages">;
            <div class:"cl-open", <select id:"cl-saved">;<button type:"button", 'data-action':"collage-open", id:"cl-open", "Open">>>>>
