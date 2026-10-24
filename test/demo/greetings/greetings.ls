// Run: ./lambda.exe demo greetings
// A greeting card studio: pick a design for an occasion, then change its words,
// art, photos and paper on the front, the inside spread and the back; preview
// the folded card opening, and print it as a sheet that folds into the card.
import dom
import radiant
import resources: ~~.mod_resources
import updates: ~~.mod_publish
import A: .mod_art
import T: .mod_templates
import M: .mod_model
import R: .mod_render
import ui: .mod_ui

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "greetings.css"), 'text')^
let CARD_CSS = input(url_resolve(BASE, "card.css"), 'text')^

// sample photos ship with the Photo Studio demo (see photo/assets/ATTRIBUTION.md)
let SAMPLES = [{key: "coast", name: "Coast", url: url_resolve(BASE, "../photo/assets/coast.jpg")},
    {key: "mountains", name: "Mountains", url: url_resolve(BASE, "../photo/assets/mountains.jpg")}]
let PHOTOS = {coast: SAMPLES[0].url, mountains: SAMPLES[1].url}
let GALLERY = [for (t in T.TEMPLATES) {t: t, card: T.assemble(t, PHOTOS)}]

fn initial() => {card: GALLERY[0].card, hist: M.history(), view: "front", page: "front", sel: null,
    panel: "designs", occasion: "all", drag: null, preview: null, dialog: null, note: "", typing: false,
    status: "Pick a design on the left, or start changing this one.", vp: {width: 1280.0, height: 820.0},
    path: "temp/greetings", open_path: "temp/greetings/balloon-party.mark", photo_path: ""}

// ---- edits: pure state transitions ------------------------------------------------

fn clamp(v, lo, hi) => max(lo, min(hi, v))
fn sel_item(st) => M.item_of(st.card, st.page, st.sel)
fn ink(st) => A.paper_of(st.card.pages[st.page].paper).ink
// every card change records the card it replaces, so one undo steps back once
fn record_edit(st, card, status = null) => if (card == st.card) st
    else {*:st, card: card, hist: M.record(st.hist, st.card), typing: false, status: if (status == null) st.status else status}
fn patch_sel(st, patch) => if (sel_item(st) == null) st else record_edit(st, M.update(st.card, st.page, st.sel, patch))
fn adjust(st, field, delta, lo, hi) => (let it = sel_item(st), if (it == null) st else patch_sel(st, {[field]: clamp(it[field] + delta, lo, hi)}))
fn toggle(st, field) => (let it = sel_item(st), if (it == null) st else patch_sel(st, {[field]: not it[field]}))

fn add_item(st, it, placed = true) {
    let id = M.fresh_id(st.card)
    let item = {*:(if (placed) M.place(st.card, it) else it), id: id};
    {*:record_edit(st, M.add(st.card, st.page, item), "Added to the " ++ lower(T.page_name(st.page)) ++ " page."), sel: id}
}
fn add_text(st, key) => (let t = [for (x in ui.TEXT_STYLES where x.key == key) x][0],
    add_item(st, T.text(t.text, 0, 0, t.w, t.size, t.font, ink(st))))
fn add_art(st, key) => (let f = T.format_of(st.card.format), add_item(st, T.art(key, 0, 0, min(f.w, f.h) * .34)))
fn add_photo(st, url) => if (sel_item(st) != null and sel_item(st).kind == "photo") patch_sel(st, {src: url})
    else add_item(st, T.photo(url, 0, 0, 260, 260, "rounded"))

// a verse replaces the selected text, or goes on the inside right page
fn verse(st, arg) {
    let parts = split(arg, "/")
    let words = T.VERSES[parts[0]][int(parts[1])]
    let it = sel_item(st)
    if (it != null and it.kind == "text") { patch_sel(st, {text: words}) }
    else {
        let f = T.format_of(st.card.format)
        let box = T.verse_box(f)
        let inside = {*:st, view: "inside", page: "right", sel: null};
        add_item(inside, T.text(words, box.x, box.y, box.w, box.size, "georgia", ink(inside), {line: 1.45}), false)
    }
}

fn turn(st, delta) => (let it = sel_item(st), if (it == null) st
    else (let r = it.rot + delta, patch_sel(st, {rot: if (r > 180.0) r - 360.0 else if (r <= -180.0) r + 360.0 else r})))
fn grow(st, delta) => (let it = sel_item(st), if (it == null) st
    else (let w = clamp(it.w + delta, 20.0, 1400.0), patch_sel(st, {w: w, h: it.h * w / it.w})))
fn frame(st, key) => (let it = sel_item(st), if (it == null) st
    else patch_sel(st, if (key == "circle") {frame: key, h: it.w} else {frame: key}))
fn backdrop_all(st) => (let pg = st.card.pages[st.page],
    record_edit(st, M.set_all_pages(st.card, {paper: pg.paper, pattern: pg.pattern, pattern_color: pg.pattern_color,
        pattern_alpha: pg.pattern_alpha, border: pg.border, border_color: pg.border_color}), "Every page now has this backdrop."))
fn set_page(st, patch) => record_edit(st, M.set_page(st.card, st.page, patch))
fn show_view(st, key) => {*:st, view: key, page: if (key == "inside") "right" else key, sel: null}
fn history_step(st, forward) => (let step = if (forward) M.redo_step(st.hist, st.card) else M.undo_step(st.hist, st.card),
    if (step == null) st else {*:st, card: step.card, hist: step.history, sel: if (M.item_of(step.card, st.page, st.sel) == null) null else st.sel,
        typing: false, status: if (forward) "Redone." else "Undone."})
fn use_design(st, key) => (let g = [for (x in GALLERY where x.t.key == key) x][0],
    {*:record_edit(st, g.card, "Started from " ++ g.t.name ++ ". Undo goes back to your previous card."), view: "front", page: "front", sel: null})

fn change(st, verb, arg) => if (verb == "view") show_view(st, arg)
    else if (verb == "panel") {*:st, panel: arg}
    else if (verb == "occasion") {*:st, occasion: arg}
    else if (verb == "template") use_design(st, arg)
    else if (verb == "blank") {*:record_edit(st, T.blank(st.card.format), "A blank card. Add text, art and photos from the left."), view: "front", page: "front", sel: null}
    else if (verb == "format") record_edit(st, M.reformat(st.card, arg), "The card is now " ++ lower(T.format_of(arg).name) ++ ".")
    else if (verb == "card-occasion") record_edit(st, {*:st.card, occasion: arg})
    else if (verb == "add-text") add_text(st, arg)
    else if (verb == "verse") verse(st, arg)
    else if (verb == "add-art") add_art(st, arg)
    else if (verb == "add-photo") add_photo(st, [for (p in SAMPLES where p.key == arg) p][0].url)
    else if (verb == "paper") set_page(st, {paper: arg})
    else if (verb == "pattern") set_page(st, {pattern: arg})
    else if (verb == "pattern-color") set_page(st, {pattern_color: arg})
    else if (verb == "pattern-alpha") set_page(st, {pattern_alpha: clamp(st.card.pages[st.page].pattern_alpha + float(arg), .05, 1.0)})
    else if (verb == "border") set_page(st, {border: arg})
    else if (verb == "border-color") set_page(st, {border_color: arg})
    else if (verb == "backdrop-all") backdrop_all(st)
    else if (verb == "select") {*:st, sel: arg}
    else if (verb == "deselect") {*:st, sel: null}
    else if (verb == "font") patch_sel(st, {font: arg})
    else if (verb == "color") patch_sel(st, {color: arg})
    else if (verb == "align") patch_sel(st, {align: arg})
    else if (contains(["bold", "italic", "shadow", "flip"], verb)) toggle(st, verb)
    else if (verb == "size") adjust(st, "size", float(arg), 6.0, 320.0)
    else if (verb == "spacing") adjust(st, "spacing", float(arg), -5.0, 40.0)
    else if (verb == "line") adjust(st, "line", float(arg), .6, 3.0)
    else if (verb == "opacity") adjust(st, "opacity", float(arg), .1, 1.0)
    else if (verb == "zoom") adjust(st, "zoom", float(arg), 1.0, 4.0)
    else if (verb == "rot") turn(st, float(arg))
    else if (verb == "grow") grow(st, float(arg))
    else if (verb == "frame") frame(st, arg)
    else if (verb == "order") record_edit(st, M.reorder(st.card, st.page, st.sel, arg))
    else if (verb == "duplicate" and st.sel != null)
        (let id = M.fresh_id(st.card), {*:record_edit(st, M.duplicate(st.card, st.page, st.sel, id), "Duplicated."), sel: id})
    else if (verb == "delete" and st.sel != null) {*:record_edit(st, M.remove(st.card, st.page, st.sel), "Deleted. Undo brings it back."), sel: null}
    else if (verb == "undo" or verb == "redo") history_step(st, verb == "redo")
    else if (verb == "preview") {*:st, preview: arg, sel: null}
    else if (verb == "preview-done") {*:st, preview: null}
    else if (verb == "dialog") {*:st, dialog: arg, note: ""}
    else if (verb == "dialog-close") {*:st, dialog: null}
    else st

// ---- files ---------------------------------------------------------------------------

pn ensure_dir(path) {
    var at = ""
    for (part in split(path, "/")) {
        at = if (at == "") part else at ++ "/" ++ part
        if (part != "" and part != "." and not exists(at)) io.mkdir(at) ^ { null }
    }
}
pn render_file(html, path, opts) any^ {
    let done = radiant.export_document(html, path, {*:opts, local_resources: true})
    if (done is error) raise done
    if (done != true) raise error(string(done))
    return path
}
// pages render at 3 px per card unit: 300 dpi for a card measured in hundredths of an inch
pn export_files(st, kind) any^ {
    let dir = trim(st.path)
    if (dir == "") raise error("Choose a folder first.")
    ensure_dir(dir)
    let stem = dir ++ "/" ++ M.slug(st.card.title)
    let f = T.format_of(st.card.format)
    if (kind == "mark") {
        output(st.card, stem ++ ".mark", {format: 'mark', atomic: true})^
        return "Saved " ++ stem ++ ".mark"
    }
    if (kind == "png") {
        for (p in T.PAGES) {
            let html = format(R.document(R.page(st.card, p.key, 3.0, {uid: "x"}), CARD_CSS, st.card.title), 'html')
            render_file(html, stem ++ "-" ++ p.key ++ ".png", {format: "png", width: f.w * 3, height: f.h * 3})^
        }
        return "Wrote four page images to " ++ dir ++ "/"
    }
    if (kind == "sheets") {
        for (side in ["outside", "inside"]) {
            let html = format(R.document(R.sheet(st.card, side, 3.0), CARD_CSS, st.card.title), 'html')
            render_file(html, stem ++ "-" ++ side ++ ".png", {format: "png", width: f.w * 6, height: f.h * 3})^
        }
        return "Wrote " ++ stem ++ "-outside.png and -inside.png. Print them back to back, then fold."
    }
    // the PDF page is the unfolded card: 96 CSS px make an inch, card units are 1/100 in
    let page_css = CARD_CSS ++ " @page { size: " ++ A.n(f.w * .02) ++ "in " ++ A.n(f.h * .01) ++ "in; margin: 0 } .sheet { break-after: page; }"
    let html = format(R.document(<div (R.sheet(st.card, "outside", .96)); (R.sheet(st.card, "inside", .96))>, page_css, st.card.title), 'html')
    render_file(html, stem ++ ".pdf", {format: "pdf", pagination: true})^
    return "Wrote " ++ stem ++ ".pdf: two sheets to print back to back and fold."
}
pn open_card(st) any^ {
    let path = trim(st.open_path)
    let card = input(path, 'mark')^
    if (not M.valid(card)) raise error(path ++ " is not a Greetings card.")
    return {*:record_edit(st, card, "Opened " ++ path ++ "."), dialog: null, view: "front", page: "front", sel: null}
}
fn file_url(path) => (let p = trim(path),
    let full = if (starts_with(p, "~/")) (sys.proc.self.env.HOME# or "") ++ slice(p, 1, len(p))
        else if (starts_with(p, "/")) p else sys.proc.self.cwd# ++ "/" ++ p,
    if (starts_with(p, "file://")) p else "file://" ++ full)

pn command(st, verb, arg) {
    if (verb == "export" or verb == "save") {
        var next = {*:st, note: ""}
        export_files(st, if (verb == "save") "mark" else arg) ^ { next = {*:st, note: ^.message, status: ^.message} }
            ~ { next = {*:st, note: ~, status: ~} }
        return next
    }
    if (verb == "open") {
        var next = st
        open_card(st) ^ { next = {*:st, note: ^.message} } ~ { next = ~ }
        return next
    }
    if (verb == "photo-file") {
        let url = file_url(st.photo_path)
        if (st.photo_path == "" or not exists(slice(url, 7, len(url)))) { return {*:st, status: "No photo at that path."} }
        return add_photo(st, url)
    }
    return change({*:st, typing: false}, verb, arg)
}

// ---- gestures on the canvas -----------------------------------------------------------

let DEG = 57.29577951308232
fn local_delta(dx, dy, rot) => (let r = rot / DEG, {x: dx * math.cos(r) + dy * math.sin(r), y: dy * math.cos(r) - dx * math.sin(r)})

// A drag keeps the item as it was at pointerdown and recomputes it from the
// pointer's total travel, so rounding never accumulates.
fn dragged(st, x, y, shift) {
    let d = st.drag
    let it = d.start
    let s = d.scale
    let dx = x - d.x
    let dy = y - d.y
    let f = T.format_of(st.card.format)
    if (d.mode == "rotate") {
        let a = (math.atan2(y - d.cy, x - d.cx) - math.atan2(d.y - d.cy, d.x - d.cx)) * DEG
        let r = it.rot + a
        let snapped = if (shift) round(r / 15.0) * 15.0 else if (abs(r - round(r / 90.0) * 90.0) < 3.0) round(r / 90.0) * 90.0 else r;
        {patch: {rot: if (snapped > 180.0) snapped - 360.0 else if (snapped <= -180.0) snapped + 360.0 else snapped}, guides: null}
    }
    else if (d.mode == "scale" or d.mode == "width") {
        let l = local_delta(dx, dy, it.rot)
        let k = max(.15, (it.w * s + l.x) / (it.w * s));
        if (it.kind == "text" and d.mode == "width") {patch: {w: max(30.0, it.w + l.x / s)}, guides: null}
        else if (it.kind == "text") {patch: {w: it.w * k, size: it.size * k, spacing: it.spacing * k}, guides: null}
        else if (it.kind == "photo" and not shift and it.frame != "circle")
            {patch: {w: max(30.0, it.w + l.x / s), h: max(30.0, it.h + l.y / s)}, guides: null}
        else {patch: {w: it.w * k, h: it.h * k}, guides: null}
    }
    else {
        // the centre snaps to the page's middle lines, which then show as guides
        let tol = 6.0 / s
        let nx = it.x + dx / s
        let ny = it.y + dy / s
        let gx = abs(nx + it.w / 2.0 - f.w / 2.0) < tol
        let gy = it.kind != "text" and abs(ny + it.h / 2.0 - f.h / 2.0) < tol;
        {patch: {x: if (gx) (f.w - it.w) / 2.0 else nx, y: if (gy) (f.h - it.h) / 2.0 else ny}, guides: {x: gx, y: gy}}
    }
}

// The transform that shows `it` where the drag has taken it, applied to the element
// still laid out as `d.start`: a move translates, a resize scales about the box centre
// and translates that centre so the top-left corner stays put. A text box's height is
// not in the card, so its half-diagonal comes from the grabbed corner handle.
fn drag_transform(d, it) {
    let a = d.start
    let s = d.scale
    let kx = it.w / a.w
    let ky = if (a.kind == "text") kx else it.h / a.h
    let r0 = a.rot / DEG
    let half = if (d.mode != "scale") {x: 0.0, y: 0.0}
        else if (a.kind == "text")
            (let px = d.x - d.cx, let py = d.y - d.cy, {x: px * math.cos(r0) + py * math.sin(r0), y: py * math.cos(r0) - px * math.sin(r0)})
        else {x: a.w * s / 2.0, y: a.h * s / 2.0}
    let lx = (kx - 1.0) * half.x
    let ly = (ky - 1.0) * half.y
    let tx = lx * math.cos(r0) - ly * math.sin(r0) + (it.x - a.x) * s
    let ty = lx * math.sin(r0) + ly * math.cos(r0) + (it.y - a.y) * s;
    "translate(" ++ A.n(tx) ++ "px, " ++ A.n(ty) ++ "px) rotate(" ++ A.n(it.rot) ++ "deg) scale(" ++ string(kx) ++ ", " ++ string(ky) ++ ")"
}
pn drag_element(root, d) {
    let page = dom.query_selector(root, "#canvas [data-page=\"" ++ d.key ++ "\"]")
    return {page: page, item: dom.query_selector(page, "[data-id=\"" ++ d.id ++ "\"]")}
}
// D4.5.1v4: a gesture previews on the dragged element alone and the card changes once,
// on release; re-rendering the studio on every move is too slow. A transform write is
// a paint-only commit; a text-width drag must re-wrap, so it writes the width instead.
pn publish_drag(root, d, it, guides) {
    let el = drag_element(root, d)
    if (el.item == null) { return null }
    if (d.mode == "width") updates.style(el.item, "width", R.geometry(it, d.scale).width)
    else dom.presentation_style_set_property(el.item, "transform", drag_transform(d, it))
    updates.style(dom.query_selector(el.page, ".guide-x"), "display", if (guides != null and guides.x) "block" else "none")
    updates.style(dom.query_selector(el.page, ".guide-y"), "display", if (guides != null and guides.y) "block" else "none")
    return null
}
fn travelled(d, evt) => abs(evt.clientX - d.x) + abs(evt.clientY - d.y) >= 3
fn typing_target(evt) => dom.closest(evt.target, "input, textarea, select") != null

view <greetings_app> state st: initial() {
    <body ui.studio(st, GALLERY, SAMPLES)>
}
on load(evt) {
    let size = dom.viewport_size(dom.root_node(evt.target))
    if (size != null) { st = {*:st, vp: {width: float(size.width), height: float(size.height)}} }
    return 'pass'
}
on resize(evt) {
    let size = dom.viewport_size(dom.root_node(evt.target))
    if (size != null) { st = {*:st, vp: {width: float(size.width), height: float(size.height)}} }
    return 'pass'
}
on click(evt) {
    let btn = dom.closest(evt.target, "[data-act]")
    if (btn == null or dom.has_attribute(btn, "disabled")) { return 'pass' }
    st = command(st, dom.get_attribute(btn, "data-act"), dom.get_attribute(btn, "data-arg") or "")
    return 'handled'
}
on control_change(req) {
    let it = sel_item(st)
    if (req.action == "text" and it != null and req.value != it.text) {
        // a burst of typing is one undo step; a value equal to the text is the
        // control settling after undo, which must not cut the redo history
        let base = if (st.typing) st else {*:st, hist: M.record(st.hist, st.card)}
        st = {*:base, card: M.update(st.card, st.page, st.sel, {text: req.value}), typing: true}
    }
    else if (req.action == "title") { st = {*:record_edit(st, {*:st.card, title: req.value}), typing: false} }
    else if (req.action == "path") { st = {*:st, path: req.value} }
    else if (req.action == "open-path") { st = {*:st, open_path: req.value} }
    else if (req.action == "photo-path") { st = {*:st, photo_path: req.value} }
}
on pointerdown(evt) {
    if (st.preview != null or st.dialog != null or evt.button != 0) { return 'pass' }
    let canvas = dom.get_element_by_id(dom.root_node(evt.target), "canvas")
    let page = dom.closest(evt.target, "[data-page]")
    if (page == null or not dom.contains(canvas, page)) { return 'pass' }
    let key = dom.get_attribute(page, "data-page")
    let el = dom.closest(evt.target, "[data-id]")
    if (el == null) {
        st = {*:st, page: key, sel: null, typing: false}
        return 'prevent-default'
    }
    let id = dom.get_attribute(el, "data-id")
    let handle = dom.closest(evt.target, "[data-handle]")
    let box = dom.bounding_box(el)
    st = {*:st, page: key, sel: id, typing: false,
        drag: {mode: if (handle == null) "move" else dom.get_attribute(handle, "data-handle"), key: key, id: id,
            start: M.item_of(st.card, key, id), before: st.card, x: evt.clientX, y: evt.clientY,
            cx: box.x + box.width / 2.0, cy: box.y + box.height / 2.0, scale: ui.scale_for(st)}}
    dom.set_pointer_capture(canvas, evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (st.drag == null) { return 'pass' }
    if (travelled(st.drag, evt)) {
        let step = dragged(st, evt.clientX, evt.clientY, evt.shiftKey)
        publish_drag(dom.root_node(evt.target), st.drag, {*:st.drag.start, *:step.patch}, step.guides)
    }
    return 'prevent-default'
}
on pointerup(evt) {
    if (st.drag == null) { return 'pass' }
    let d = st.drag
    dom.release_pointer_capture(dom.get_element_by_id(dom.root_node(evt.target), "canvas"), evt.pointerId)
    let el = drag_element(dom.root_node(evt.target), d)
    if (el.item != null) dom.presentation_style_clear(el.item)
    // the release point decides the item's geometry, and the whole gesture is one undo step
    st = if (not travelled(d, evt)) {*:st, drag: null}
        else (let step = dragged(st, evt.clientX, evt.clientY, evt.shiftKey),
            {*:st, drag: null, card: M.update(d.before, d.key, d.id, {*:d.start, *:step.patch}), hist: M.record(st.hist, d.before),
             status: if (d.mode == "move") "Moved." else if (d.mode == "rotate") "Rotated." else "Resized."})
    return 'prevent-default'
}
on pointercancel(evt) {
    if (st.drag == null) { return 'pass' }
    // the drag only touched the element: clearing its preview restores it
    let el = drag_element(dom.root_node(evt.target), st.drag)
    if (el.item != null) dom.presentation_style_clear(el.item)
    st = {*:st, drag: null}
    return 'prevent-default'
}
on keydown(evt) {
    if (st.dialog != null or st.preview != null) {
        if (evt.key == "Escape") { st = {*:st, dialog: null, preview: null}; return 'prevent-default' }
        return 'pass'
    }
    if (typing_target(evt)) { return 'pass' }
    let mod = evt.metaKey or evt.ctrlKey
    let key = lower(evt.key)
    let action = if (mod and key == "z") (if (evt.shiftKey) "redo" else "undo") else if (mod and key == "y") "redo"
        else if (mod and key == "d") "duplicate" else if (mod and key == "s") "save"
        else if (key == "delete" or key == "backspace") "delete" else if (key == "escape") "deselect"
        else if (key == "]") "forward" else if (key == "[") "backward" else null
    if (contains(["arrowleft", "arrowright", "arrowup", "arrowdown"], key) and st.sel != null) {
        let step = if (evt.shiftKey) 10.0 else 1.0
        let it = sel_item(st)
        st = patch_sel(st, {x: it.x + (if (key == "arrowleft") 0.0 - step else if (key == "arrowright") step else 0.0),
            y: it.y + (if (key == "arrowup") 0.0 - step else if (key == "arrowdown") step else 0.0)})
        return 'prevent-default'
    }
    if (action == null) { return 'pass' }
    st = if (action == "forward" or action == "backward") command(st, "order", action) else command(st, action, "")
    return 'prevent-default'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Greetings — Lambda"><style CARD_CSS ++ CSS>>
    apply(<greetings_app>)>
