// Draws card pages as HTML at a scale `s` (screen pixels per card unit):
// paper, a pattern layer, a border layer, then the items in stacking order.
// One renderer serves the editor, the gallery, the preview and the exports;
// only the editor passes a selection, which adds the handles.
import A: .mod_art
import T: .mod_templates

fn px(v, s) => A.n(v * s) ++ "px"

// the style properties a drag changes; the editor writes them directly while a gesture runs
pub fn geometry(it, s) => {left: px(it.x, s), top: px(it.y, s), width: px(it.w, s),
    height: if (it.kind == "text") "auto" else px(it.h, s),
    // every item keeps a transform, so a drag's transform-only preview stays paint-only (RAD_16 §8)
    transform: "rotate(" ++ A.n(it.rot) ++ "deg)",
    'font-size': if (it.kind == "text") px(it.size, s) else null,
    'letter-spacing': if (it.kind == "text") px(it.spacing, s) else null}

fn box_style(it, s) => (let g = geometry(it, s), "left:" ++ g.left ++ ";top:" ++ g.top ++ ";width:" ++ g.width
    ++ (if (it.kind == "text") "" else ";height:" ++ g.height)
    ++ ";transform:" ++ g.transform
    ++ (if (it.opacity < 1.0) ";opacity:" ++ A.n(it.opacity) else ""))

fn text_style(it, s) => (let g = geometry(it, s), ";font-family:" ++ A.font_of(it.font).family ++ ";font-size:" ++ g['font-size']
    ++ ";color:" ++ it.color ++ ";text-align:" ++ it.align ++ ";line-height:" ++ A.n(it.line)
    ++ (if (it.bold) ";font-weight:700" else "") ++ (if (it.italic) ";font-style:italic" else "")
    ++ (if (it.spacing != 0.0) ";letter-spacing:" ++ g['letter-spacing'] else ""))
    ++ (if (it.shadow) ";text-shadow:0 " ++ px(2, s) ++ " " ++ px(8, s) ++ " rgba(0,0,0,.4)" else "")

// a photo sits in a clipping frame; the frame's ring and padding scale with the photo
fn frame_style(it, s) => (let ring = px(max(3.0, it.w * .025), s),
    if (it.frame == "rounded") "border-radius:" ++ px(it.w * .08, s)
    else if (it.frame == "circle") "border-radius:50%;border:" ++ ring ++ " solid #ffffff"
    else if (it.frame == "arch") "border-radius:" ++ px(it.w * .5, s) ++ " " ++ px(it.w * .5, s) ++ " 0 0"
    else if (it.frame == "polaroid") "padding:" ++ px(it.w * .05, s) ++ " " ++ px(it.w * .05, s) ++ " " ++ px(it.w * .17, s)
        ++ ";background:#ffffff;box-shadow:0 " ++ px(3, s) ++ " " ++ px(12, s) ++ " rgba(15,23,42,.28)"
    else "")
fn photo_body(it, s) => <div class:"photo-frame", style:frame_style(it, s),
    <div class:"photo-clip",
        <div class:"photo-img", style:"background-image:url('" ++ it.src ++ "')"
            ++ (if (it.zoom != 1.0) ";transform:scale(" ++ A.n(it.zoom) ++ ")" else "")>>>

fn handles(it) => if (it.kind == "text") [<span class:"handle h-rotate", 'data-handle':"rotate", title:"Drag to rotate">,
        <span class:"handle h-width", 'data-handle':"width", title:"Drag to change the text width">,
        <span class:"handle h-scale", 'data-handle':"scale", title:"Drag to resize">]
    else [<span class:"handle h-rotate", 'data-handle':"rotate", title:"Drag to rotate">,
        <span class:"handle h-scale", 'data-handle':"scale", title:"Drag to resize">]

pub fn item(it, s, selected) {
    let cls = "item " ++ it.kind ++ (if (selected) " selected" else "")
    let body = if (it.kind == "text") [it.text]
        else if (it.kind == "art") [<div class:"art-wrap" ++ (if (it.flip) " flip" else ""), A.art_svg(it.art, it.color)>]
        else [photo_body(it, s)];
    <div class:cls, 'data-id':it.id, style:box_style(it, s) ++ (if (it.kind == "text") text_style(it, s) else ""),
        for (b in body) b;
        if (selected) for (h in handles(it)) h>
}

// opts: {uid, sel (an item id on this page or null), guides ({x, y} booleans or null)}
pub fn page(card, key, s, opts = {}) {
    let f = T.format_of(card.format)
    let pg = card.pages[key]
    let uid = (if (opts.uid == null) "pg" else opts.uid) ++ "-" ++ key
    let pattern = if (pg.pattern == "none") null else A.pattern_layer(pg.pattern, pg.pattern_color, pg.pattern_alpha, uid, f.w, f.h)
    let border = if (pg.border == "none") null else A.border_layer(pg.border, pg.border_color, f.w, f.h);
    <div class:"card-page", 'data-page':key, style:"width:" ++ px(f.w, s) ++ ";height:" ++ px(f.h, s) ++ ";background:" ++ A.paper_of(pg.paper).bg,
        if (pattern != null) pattern;
        if (border != null) border;
        for (it in pg.items) item(it, s, opts.sel == it.id);
        // a drag shows or hides the centre guides itself, so they exist while it runs
        if (opts.guides != null) <div class:"guide guide-x", style:"display:none">;
        if (opts.guides != null) <div class:"guide guide-y", style:"display:none">>
}

// ---- export documents -------------------------------------------------------

// a page or a print sheet as a standalone HTML document at `s` pixels per unit
pub fn document(body, css, title) => <html lang:"en",
    <head <meta charset:"UTF-8"> <title title> <style css ++ " html, body { margin: 0; padding: 0; background: #ffffff; } .sheet { display: flex; }">>
    <body body>>

// A half-fold print sheet: the outside carries the back beside the front, the
// inside carries the inside spread, so folding the printed sheet makes the card.
pub fn sheet(card, side, s) => <div class:"sheet", for (key in (if (side == "outside") ["back", "front"] else ["left", "right"]))
    page(card, key, s, {uid: "print-" ++ side})>
