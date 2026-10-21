// Photo album demo: library browsing and viewer (vibe/Lambda_Demo_Photo_Album.md §3.1–3.2).
// S12.1.3 / D4.5.1v4: the native shell is retained; handlers publish state into regions and
// mount only the grid rows near the viewport, keyed by row index.
import dom
import resources: ~~.mod_resources
import updates: ~~.mod_publish
import corpus: .mod_corpus
import L: .mod_library
import G: .mod_grid
import ui: .mod_ui
import D: .mod_deck
import C: .mod_collage
import CA: .mod_collage_app
import slide: lambda.slide
import slide_html: lambda.slide.html

let BASE = url_resolve(resources.entry_uri()^, ".")
// PHOTO_ALBUM_DIR (relative to cwd) selects another corpus, such as the test fixture
let DIR = corpus.directory(slice(BASE, 7))
let DIR_URL = url_resolve("file://" ++ sys.proc.self.cwd# ++ "/", DIR)
let MANIFEST = input(DIR ++ "photos.json", 'json')^
let PRESENT = [for (p in MANIFEST.photos) {photo:exists(DIR ++ "photos/" ++ p.file), thumb:exists(DIR ++ "thumbs/" ++ p.file)}]
let LIB = L.build(MANIFEST, PRESENT, DIR_URL)^
let CTX = {lib:LIB, dir:DIR, dir_url:DIR_URL}
let FETCH = "python3 -I test/demo/photo-album/_fetch.py && ./lambda.exe run test/demo/photo-album/_thumbs.ls"
let GAP = 4.0
let ZOOM_LEVELS = [1.0, 1.5, 2.25, 3.4, 5.0, 8.0]
// the slideshow layer's header plus the player's control rows, as sized in the CSS and lambda.slide
let SHOW_CHROME = 172.0

let INITIAL = {mode:"library", album:"", query:"", favorites:[], selected:[], anchor:0, focus:0,
    target:160.0, width:0.0, height:0.0, top:0.0, ids:LIB.order, lay:null, rows:[], heads:[],
    layout_frame:0, band:null, scrub:null, pan:null, swallow_click:false, active:false, opening:null,
    sheet:null, show:false, collage:null, collage_edit:null,
    viewer:{open:false, id:0, zoom:1.0, x:0.0, y:0.0, info:false, strip:true}}

fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target), "album")
fn node(owner, id) => dom.get_element_by_id(owner, id)
fn px(v) => string(round(v * 100) / 100) ++ "px"

pn region(owner, id, html) {
    updates.markup(node(owner, id), html)
}

// ---------------------------------------------------------------- grid

fn grid_mode(st) => st.mode != "albums"

fn css_px(value) => float(slice(value, 0, max(0, len(value) - 2))) ^ { 0.0 }

// the port is always displayed (it also holds the album cards), so its content box gives the grid width
pn measure(owner, st) {
    let port = node(owner, "port")
    let metrics = dom.scroll_metrics(port)
    let padding = css_px(dom.computed_style(port, "padding-left")) + css_px(dom.computed_style(port, "padding-right"))
    return {*:st, width:float(metrics.client_width) - padding, height:float(metrics.client_height), top:float(dom.scroll_state(port).y)}
}

// mount rows/headers inside one screen of margin; unmount the rest (row index is the key)
pn mount(owner, st) {
    if (st.lay == null or not grid_mode(st)) { return st }
    let grid = node(owner, "grid")
    let rows = G.window(st.lay, st.top, st.height, st.height)
    let heads = G.headers_in(st.lay, st.top, st.height, st.height)
    for (i in st.rows) {
        if (not contains(rows, i)) {
            let el = dom.query_selector(grid, "[data-row='" ++ string(i) ++ "']")
            if (el != null) dom.remove_child(grid, el)
        }
    }
    for (i in st.heads) {
        if (not contains(heads, i)) {
            let el = dom.query_selector(grid, "[data-header='" ++ string(i) ++ "']")
            if (el != null) dom.remove_child(grid, el)
        }
    }
    // fragments are formatted as lists: format(element,'html') would wrap them in a whole document
    let rows_markup = join([for (i in rows where not contains(st.rows, i)) format([ui.row(LIB, st.lay, i, st)], 'html')], "")
    let heads_markup = join([for (i in heads where not contains(st.heads, i)) format([ui.header(st.lay, i)], 'html')], "")
    if (rows_markup ++ heads_markup != "") dom.append_child(grid, dom.parse_fragment(grid, heads_markup ++ rows_markup))
    let current = G.current_header(st.lay, st.top)
    updates.text(owner, "sticky", if (current == null or st.top < 8) "" else current.label)
    updates.show(owner, "sticky", current != null and st.top >= 8)
    let span = max(1.0, st.lay.height - st.height)
    updates.style(node(owner, "scrub-thumb"), "top", string(round(min(1.0, st.top / span) * 1000) / 10) ++ "%")
    return {*:st, rows:rows, heads:heads}
}

// rebuild the layout for the current scope and size; `keep` holds a tile in place across zoom/resize
pn relayout(owner, st, keep = null, pointer_y = 0.0) {
    let cards = st.mode == "albums" and st.query == ""
    updates.show(owner, "cards", cards)
    updates.show(owner, "grid", not cards)
    let measured = measure(owner, st)
    let ids = L.scope(LIB, measured.mode, measured.album, measured.favorites, measured.query)
    let width = max(200.0, measured.width)
    let lay = G.layout(L.items(LIB, ids, L.by_year(LIB, ids)), width, measured.target, GAP)
    let grid = node(owner, "grid")
    updates.style(grid, "height", px(lay.height))
    dom.set_inner_html(grid, "")
    region(owner, "scrub-marks", format([ui.scrubber(lay)], 'html'))
    let before = if (keep != null and measured.lay != null) G.locate(measured.lay, keep) else null
    let after = if (keep != null) G.locate(lay, keep) else null
    let top = if (before != null and after != null) G.anchored_top(before, after, pointer_y, measured.top)
        else if (keep == null) 0.0 else measured.top
    let clamped = max(0.0, min(top, lay.height - measured.height))
    dom.set_scroll_state(node(owner, "port"), 0.0, clamped)
    return mount(owner, {*:measured, ids:ids, lay:lay, rows:[], heads:[], top:clamped})
}

// the first tile of the row at the viewport's vertical centre anchors programmatic zoom and resizes
fn centre_tile(st) => if (st.lay == null) null else (
    let y = st.top + st.height / 2,
    [for (r in st.lay.rows where r.y <= y + r.h) r.tiles[0].id][last])

pn reveal(owner, st, id) {
    let at = G.locate(st.lay, id)
    if (at == null) { return st }
    let top = if (at.y < st.top + 8.0) at.y - 52.0 else if (at.y + at.h > st.top + st.height - 8.0) at.y + at.h - st.height + 8.0 else st.top
    if (top == st.top) { return st }
    let clamped = max(0.0, top)
    dom.set_scroll_state(node(owner, "port"), 0.0, clamped)
    return mount(owner, {*:st, top:clamped})
}

// ---------------------------------------------------------------- viewer

fn viewer_index(st) => index_of(st.ids, st.viewer.id) or 0

// The open animation grows the photo from its tile. The stage is still hidden when it starts, so
// its box is derived from the viewport and the viewer chrome sizes in photo-album.css.
let VIEWER_BAR = 52.0
let VIEWER_STRIP = 62.0
let VIEWER_INFO = 300.0
let OPEN_SECONDS = 0.26
pn open_from(owner, st, p) {
    let tile = node(owner, "tile-" ++ string(p.id))
    let screen = dom.viewport_size(owner)
    if (tile == null or screen == null) { return {x:0.0, y:0.0, s:0.94} }
    let r = dom.bounding_box(tile)
    let w = screen.width - (if (st.viewer.info) VIEWER_INFO else 0.0)
    let h = screen.height - VIEWER_BAR - (if (st.viewer.strip) VIEWER_STRIP else 0.0)
    let fit = min(w / p.width, h / p.height) * p.width
    return {x:r.x + r.width / 2 - w / 2, y:r.y + r.height / 2 - VIEWER_BAR - h / 2, s:max(0.02, r.width / max(1.0, fit))}
}
fn box_transform(x, y, s) => "translate(" ++ px(x) ++ "," ++ px(y) ++ ") scale(" ++ string(round(s * 1000) / 1000) ++ ")"

// frame-driven (not a CSS keyframe) so the start box can be computed per photo
pn begin_open(owner, st) {
    let from = open_from(owner, st, L.photo(LIB, st.viewer.id))
    let image = node(owner, "viewer-image")
    updates.style(image, "transform", box_transform(from.x, from.y, from.s))
    updates.style(image, "opacity", "0.6")
    return {*:st, opening:{id:st.viewer.id, start:clock(), from:from, frame:dom.request_frame(owner, "album_open")}}
}
pn step_open(owner, st) {
    let a = st.opening
    let image = node(owner, "viewer-image")
    let p = min(1.0, (clock() - a.start) / OPEN_SECONDS)
    // the animation yields to any viewer change: another photo, zoom or close
    if (p >= 1.0 or not st.viewer.open or st.viewer.id != a.id or st.viewer.zoom != 1.0) {
        updates.style(image, "opacity", "1")
        updates.style(image, "transform", box_transform(st.viewer.x, st.viewer.y, st.viewer.zoom))
        return {*:st, opening:null}
    }
    let e = 1.0 - (1.0 - p) * (1.0 - p) * (1.0 - p)
    updates.style(image, "transform", box_transform(a.from.x * (1.0 - e), a.from.y * (1.0 - e), a.from.s + (1.0 - a.from.s) * e))
    updates.style(image, "opacity", string(round((0.6 + 0.4 * e) * 100) / 100))
    return {*:st, opening:{*:a, frame:dom.request_frame(owner, "album_open")}}
}

pn publish_viewer(owner, st) {
    let v = st.viewer
    updates.attr(owner, "data-viewer", v.open)
    updates.show(owner, "viewer", v.open, "flex")
    if (not v.open) { return null }
    let p = L.photo(LIB, v.id)
    let image = node(owner, "viewer-image")
    let source = if (p.has_photo) p.photo_src else p.thumb_src
    if (dom.get_attribute(image, "src") != source) {
        dom.set_attribute(image, "src", source)
        dom.set_attribute(image, "alt", p.title)
    }
    // a photo that is not downloaded yet shows its average colour
    updates.style(node(owner, "viewer-stage"), "background-color", if (source == "") p.color or "#222" else "transparent")
    if (st.opening == null) updates.style(image, "transform", box_transform(v.x, v.y, v.zoom))
    updates.attr(owner, "data-zoomed", v.zoom > 1.0)
    updates.text(owner, "viewer-title", p.title)
    updates.text(owner, "viewer-subtitle", string(viewer_index(st) + 1) ++ " of " ++ string(len(st.ids)) ++ " · " ++ L.date_label(p))
    updates.text(owner, "viewer-zoom-label", if (v.zoom == 1.0) "Fit" else string(round(v.zoom * 100)) ++ "%")
    updates.attr(node(owner, "viewer-favorite"), "aria-pressed", contains(st.favorites, v.id))
    updates.attr(node(owner, "viewer-info"), "aria-pressed", v.info)
    updates.attr(node(owner, "viewer-strip"), "aria-pressed", v.strip)
    updates.show(owner, "viewer-info-panel", v.info)
    if (v.info) region(owner, "viewer-info-panel", format([ui.info(LIB, p)], 'html'))
    updates.show(owner, "filmstrip", v.strip, "flex")
    if (v.strip) region(owner, "filmstrip", format([ui.filmstrip(LIB, st.ids, v.id)], 'html'))
    let at = viewer_index(st)
    updates.attr(node(owner, "viewer-prev"), "aria-disabled", at == 0)
    updates.attr(node(owner, "viewer-next"), "aria-disabled", at >= len(st.ids) - 1)
    // neighbours load into hidden images so the next step shows a decoded photo
    let nearby = [for (j in [at + 1, at - 1] where j >= 0 and j < len(st.ids)) L.photo(LIB, st.ids[j])]
    region(owner, "prefetch", join([for (q in nearby where q.has_photo) format([<img src:q.photo_src,alt:"">], 'html')], ""))
    return null
}

fn open_viewer(st, id) => {*:st, focus:id, viewer:{*:st.viewer, open:true, id:id, zoom:1.0, x:0.0, y:0.0}}
fn step(st, delta) {
    let at = viewer_index(st)
    let next = max(0, min(len(st.ids) - 1, at + delta));
    if (len(st.ids) == 0) st else open_viewer(st, st.ids[next])
}
fn viewer_zoom(st, level) {
    let zoom = max(1.0, min(8.0, level));
    {*:st, viewer:{*:st.viewer, zoom:zoom, x:if (zoom == 1.0) 0.0 else st.viewer.x * zoom / st.viewer.zoom,
        y:if (zoom == 1.0) 0.0 else st.viewer.y * zoom / st.viewer.zoom}}
}
fn zoom_by(st, delta) {
    let at = [for (i, z in ZOOM_LEVELS where z >= st.viewer.zoom - 0.01) i][0] or len(ZOOM_LEVELS) - 1;
    viewer_zoom(st, ZOOM_LEVELS[max(0, min(len(ZOOM_LEVELS) - 1, at + delta))])
}
fn toggle(ids, id) => if (contains(ids, id) ^ { false }) [for (x in ids where x != id) x] else [*ids, id]

// ---------------------------------------------------------------- slideshow

// the photos a slideshow starts from: a selection in timeline order, the scope, or the scope from the viewer's photo
fn show_ids(st, source) => if (source == "selection") [for (id in st.ids where contains(st.selected, id)) id]
    else if (source == "viewer") slice(st.ids, viewer_index(st), len(st.ids)) else st.ids
fn show_title(st, source) {
    let scope = L.scope_title(LIB, st.mode, st.album, st.query);
    if (source == "viewer") L.photo(LIB, st.viewer.id).title else if (source == "selection") scope ++ " selection" else scope
}

pn open_sheet(owner, st, source) {
    let ids = slice(show_ids(st, source), 0, D.MAX_PHOTOS)
    if (len(ids) == 0) { return st }
    dom.set_state(node(owner, "show-name"), "value", show_title(st, source))
    updates.text(owner, "show-count", string(len(ids)) ++ (if (len(ids) == 1) " photo" else " photos") ++
        (if (len(show_ids(st, source)) > D.MAX_PHOTOS) " (the first " ++ string(D.MAX_PHOTOS) ++ ")" else ""))
    updates.text(owner, "show-status", "")
    return {*:st, sheet:{ids:ids}, viewer:{*:st.viewer, open:false}}
}

// writes <corpus>/slideshows/<name>.slides with photo paths relative to that folder (SLD1)
pn save_deck(owner, st) {
    let typed = trim(dom.get_state(node(owner, "show-name"), "value"))
    let title = if (typed == "") "Slideshow" else typed
    let dwell = float(int(dom.get_state(node(owner, "show-dwell"), "value")) ^ { 4 }) * 1000.0
    let deck = D.deck(LIB, {title:title, ids:st.sheet.ids, theme:dom.get_state(node(owner, "show-theme"), "value"),
        transition:dom.get_state(node(owner, "show-transition"), "value"), dwell_ms:dwell, prefix:"../"})
    if (not exists(DIR ++ "slideshows")) { io.mkdir(DIR ++ "slideshows")^ }
    let path = DIR ++ "slideshows/" ++ D.file_name(title)
    output(deck, path, {format:'mark', atomic:true})^
    return {title:title, path:path, name:"slideshows/" ++ D.file_name(title)}
}

pn present(owner, st) {
    var saved = null
    save_deck(owner, st) ^ { updates.text(owner, "show-status", "Could not save: " ++ ^.message) } ~ { saved = ~ }
    if (saved == null) { return st }
    let screen = dom.viewport_size(owner)
    let host = node(owner, "show")
    // the album_show view loads the saved file, so presenting plays exactly what was saved
    dom.set_attribute(host, "data-deck", saved.path)
    dom.set_attribute(host, "data-base", DIR_URL ++ "slideshows/")
    dom.set_attribute(host, "data-width", string(if (screen == null) 1280.0 else screen.width))
    dom.set_attribute(host, "data-height", string(if (screen == null) 600.0 else max(240.0, screen.height - SHOW_CHROME)))
    dom.request_frame(host, "album_show_open")
    updates.text(owner, "show-title", saved.title)
    updates.text(owner, "show-saved", "Saved as " ++ saved.name)
    return {*:st, sheet:null, show:true}
}

pn close_show(owner, st) {
    dom.request_frame(node(owner, "show"), "album_show_close")
    return {*:st, show:false}
}

// ---------------------------------------------------------------- collage

// a collage starts from the selection or the first photos shown, 2 to 30 of them
pn open_collage(owner, st, source) {
    let ids = slice(if (source == "selection") [for (id in st.ids where contains(st.selected, id)) id] else st.ids, 0, C.MAX_PHOTOS)
    if (len(ids) < C.MIN_PHOTOS) { return st }
    let title = L.scope_title(LIB, st.mode, st.album, st.query)
    updates.show(owner, "collage", true, "flex")
    let cs = CA.start(owner, CTX, ids, title)
    // keyboard shortcuts reach the album only from focus inside it
    dom.focus_set(node(owner, "collage"), false)
    return {*:st, collage:cs, viewer:{*:st.viewer, open:false}}
}

pn collage_command(owner, st, action) {
    let cs = st.collage
    if (action == "collage-close") {
        updates.show(owner, "collage", false)
        dom.focus_set(node(owner, "port"), false)
        return {*:st, collage:null, collage_edit:null}
    }
    if (action == "collage-undo" or action == "collage-redo") { return {*:st, collage:CA.undo(owner, CTX, cs, action == "collage-redo")} }
    if (action == "collage-shuffle") { return {*:st, collage:CA.change(owner, CTX, cs, C.shuffle(cs.model))} }
    var next = cs
    var status = ""
    if (action == "collage-save") { CA.save_model(owner, CTX, cs) ^ { status = "Could not save: " ++ ^.message } ~ { status = ~ } }
    if (action == "collage-open") { CA.load_saved(owner, CTX, cs) ^ { status = ^.message } ~ { next = ~ } }
    if (action == "collage-export") {
        CA.export_image(owner, CTX, cs) ^ { status = "Could not export: " ++ ^.message }
            ~ { status = "Exported " ++ slice(~.path, len(DIR), len(~.path)) ++ " (" ++ string(~.width) ++ " × " ++ string(~.height) ++ ") and its credits file" }
    }
    updates.text(owner, "cl-status", status)
    return {*:st, collage:next}
}

// sliders and the caption edit live; history records each control once, when its edit ends
pn collage_control(owner, st, id, value, final) {
    let key = if (id == "cl-gutter") "gutter" else if (id == "cl-radius") "radius" else if (id == "cl-border") "border"
        else if (id == "cl-caption") "caption" else if (id == "cl-aspect") "aspect" else if (id == "cl-credits") "credits" else ""
    if (key == "") { return st }
    let cs = st.collage
    let start = if (st.collage_edit == null) cs.model else st.collage_edit
    let typed = if (key == "caption" or key == "aspect") value else if (key == "credits") value == true
        else int(round(float(value) ^ { 0.0 }))
    let model = C.set(cs.model, key, typed)
    if (final) { return {*:st, collage:CA.change(owner, CTX, {*:cs, model:start}, model), collage_edit:null} }
    return {*:st, collage:CA.preview(owner, CTX, cs, model), collage_edit:start}
}

// ---------------------------------------------------------------- publish

pn publish(owner, st) {
    updates.attr(owner, "data-mode", if (st.query != "") "search" else st.mode)
    updates.attr(owner, "data-selected", len(st.selected))
    updates.text(owner, "title", L.scope_title(LIB, st.mode, st.album, st.query))
    updates.text(owner, "status", if (st.mode == "albums" and st.query == "") string(len(LIB.albums)) ++ " albums"
        else L.status(LIB, st.ids, st.selected))
    updates.text(owner, "favorite-count", string(len(st.favorites)))
    for (b in dom.query_selector_all(owner, ".sidebar .nav")) {
        let nav = dom.get_attribute(b, "data-nav")
        let album = dom.get_attribute(b, "data-album")
        updates.attr(b, "aria-current", st.query == "" and ((nav != null and nav == st.mode) or (album != null and st.mode == "album" and album == st.album)))
    }
    let cards = st.mode == "albums" and st.query == ""
    updates.show(owner, "empty", not cards and len(st.ids) == 0, "flex")
    if (not cards and len(st.ids) == 0) region(owner, "empty", format([ui.empty(st.query)], 'html'))
    updates.show(owner, "scrubber", not cards and len(st.ids) > 0)
    updates.show(owner, "selection-bar", len(st.selected) > 0, "flex")
    updates.text(owner, "selection-count", string(len(st.selected)) ++ " selected")
    let all_favorite = len(st.selected) > 0 and all([for (id in st.selected) contains(st.favorites, id)])
    updates.text(owner, "favorite-selected", if (all_favorite) "Remove from Favorites" else "Add to Favorites")
    for (t in dom.query_selector_all(node(owner, "grid"), ".tile")) {
        let id = int(dom.get_attribute(t, "data-id")) ^ { 0 }
        updates.attr(t, "aria-selected", contains(st.selected, id))
        updates.attr(t, "data-favorite", contains(st.favorites, id))
        updates.attr(t, "data-focused", st.focus == id)
    }
    if (st.focus > 0 and not st.viewer.open) updates.attr(node(owner, "port"), "aria-activedescendant", "tile-" ++ string(st.focus))
    publish_viewer(owner, st)
    updates.show(owner, "show-sheet", st.sheet != null, "flex")
    updates.show(owner, "show-layer", st.show, "flex")
    attr_show(owner, st)
    return st
}
pn attr_show(owner, st) {
    updates.attr(owner, "data-show", st.show)
    updates.attr(owner, "data-sheet", st.sheet != null)
}

// scope changes rebuild the layout from the top
pn rescope(owner, st) {
    let next = relayout(owner, {*:st, selected:[], anchor:0, focus:0, viewer:{*:st.viewer, open:false}, lay:null})
    return publish(owner, next)
}

fn select_tile(st, id, toggle_key, extend) {
    let anchor = index_of(st.ids, st.anchor)
    let at = index_of(st.ids, id)
    let selected = if (extend and anchor != null and at != null) slice(st.ids, min(at, anchor), max(at, anchor) + 1)
        else if (toggle_key) toggle(st.selected, id) else [id];
    {*:st, selected:selected, focus:id, anchor:if (extend and anchor != null) st.anchor else id}
}

pn command(owner, st, action) {
    if (action == "collage-scope" or action == "collage-selection") { return open_collage(owner, st, slice(action, 8, len(action))) }
    if (starts_with(action, "collage-") and st.collage != null) { return collage_command(owner, st, action) }
    if (starts_with(action, "slideshow-")) { return open_sheet(owner, st, slice(action, 10, len(action))) }
    if (action == "show-cancel") { return {*:st, sheet:null} }
    if (action == "show-save") {
        save_deck(owner, st) ^ { updates.text(owner, "show-status", "Could not save: " ++ ^.message) }
            ~ { updates.text(owner, "show-status", "Saved as " ++ ~.name ++ ". Open it with lambda view.") }
        return st
    }
    if (action == "show-present") { return present(owner, st) }
    if (action == "show-close") { return close_show(owner, st) }
    if (action == "close-viewer") { return {*:st, viewer:{*:st.viewer, open:false}} }
    if (action == "prev") { return step(st, -1) }
    if (action == "next") { return step(st, 1) }
    if (action == "toggle-favorite") { return {*:st, favorites:toggle(st.favorites, st.viewer.id)} }
    if (action == "toggle-info") { return {*:st, viewer:{*:st.viewer, info:not st.viewer.info}} }
    if (action == "toggle-strip") { return {*:st, viewer:{*:st.viewer, strip:not st.viewer.strip}} }
    if (action == "viewer-zoom-in") { return zoom_by(st, 1) }
    if (action == "viewer-zoom-out") { return zoom_by(st, -1) }
    if (action == "select-all") { return {*:st, selected:st.ids} }
    if (action == "clear-selection") { return {*:st, selected:[], anchor:0} }
    if (action == "favorite-selected") {
        let all_favorite = all([for (id in st.selected) contains(st.favorites, id)])
        let rest = [for (id in st.favorites where not contains(st.selected, id)) id]
        return {*:st, favorites:if (all_favorite) rest else [*rest, *st.selected]}
    }
    if (action == "zoom-in" or action == "zoom-out") {
        let target = float(G.zoom_step(int(st.target), if (action == "zoom-in") 1 else -1))
        dom.set_state(node(owner, "zoom"), "value", string(int(target)))
        return relayout(owner, {*:st, target:target}, centre_tile(st), st.height / 2)
    }
    return st
}

// favorites change the Favorites scope itself; rebuild it so removed photos leave the grid
pn after_favorites(owner, before, st) {
    if (st.mode == "favorites" and st.query == "" and before.favorites != st.favorites and not st.viewer.open) {
        return relayout(owner, st, centre_tile(st), st.height / 2)
    }
    return st
}

pn dispatch(owner, st, action) {
    let next = after_favorites(owner, st, command(owner, st, action))
    let published = publish(owner, next)
    if (st.viewer.open and not next.viewer.open) {
        let shown = reveal(owner, published, st.viewer.id)
        dom.focus_set(node(owner, "port"), false)
        return publish(owner, shown)
    }
    if (next.viewer.open and not st.viewer.open) dom.focus_set(node(owner, "viewer"), false)
    if (next.sheet != null and st.sheet == null) dom.focus_set(node(owner, "show-name"), false)
    if ((st.show and not next.show) or (st.sheet != null and next.sheet == null and not next.show)) dom.focus_set(node(owner, "port"), false)
    return published
}

// ---------------------------------------------------------------- gestures

pn scrub_to(owner, st, client_y) {
    let rail = dom.bounding_box(node(owner, "scrubber"))
    let fraction = max(0.0, min(1.0, (client_y - rail.y) / max(1.0, rail.height)))
    let top = fraction * max(0.0, st.lay.height - st.height)
    dom.set_scroll_state(node(owner, "port"), 0.0, top)
    return mount(owner, {*:st, top:top})
}
pn draw_band(owner, band, x, y) {
    let el = node(owner, "band")
    let rect = "left:" ++ px(min(band.x, x)) ++ ";top:" ++ px(min(band.y, y)) ++ ";width:" ++ px(abs(x - band.x)) ++ ";height:" ++ px(abs(y - band.y))
    if (el == null) {
        dom.append_child(node(owner, "grid"), dom.parse_fragment(node(owner, "grid"), "<div id=\"band\" class=\"band\" style=\"" ++ rect ++ "\"></div>"))
    } else { dom.set_attribute(el, "style", rect) }
}
// the click that ends a drag must not reselect the tile under the pointer
pn end_gesture(owner, st) {
    let el = node(owner, "band")
    if (el != null) dom.remove_child(node(owner, "grid"), el)
    return {*:st, pan:null, scrub:null, band:null, swallow_click:st.band != null or st.pan != null}
}

// ---------------------------------------------------------------- views and handlers

view <album_scrollport> { ~.rendered }
on scroll(evt) {
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "album")
    // one scroll frame at a time; the session clears the flag when it handles the frame
    if (dom.get_attribute(owner, "data-scroll-pending") != "true") {
        dom.set_attribute(owner, "data-scroll-pending", "true")
        dom.request_frame(owner, "album_scroll")
    }
    return 'handled'
}

// The slideshow player. A live player carries its own handlers, so it is created by this view's
// template when the album asks it to load a saved deck, and removed again on close.
view <album_show> state shown:null {
    <div id:"show",class:"show",
        (if (shown == null) null else slide.player(shown.deck, {instance:"album-show", width:shown.width, height:shown.height,
            base_uri:shown.base, autostart:true})^)>
}
on album_show_open(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let target = evt.target
    let base = dom.get_attribute(target, "data-base")
    shown = {deck:D.resolve_sources(input(dom.get_attribute(target, "data-deck"), 'slides')^, base), base:base,
        width:float(dom.get_attribute(target, "data-width")) ^ { 1280.0 }, height:float(dom.get_attribute(target, "data-height")) ^ { 600.0 }}
    // the player exists after this view re-renders; start it on the next frame
    dom.request_frame(target, "album_show_ready")
    return 'handled'
}
// a standalone .slides page autostarts from its load event; an embedded player is started by its host
on album_show_ready(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let player = dom.get_element_by_id(dom.root_node(evt.target), "album-show")
    if (player != null) {
        dom.request_frame(player, "slide_activate")
        dom.focus_set(player, false)
    }
    return 'handled'
}
on album_show_close(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    shown = null
    return 'handled'
}

view <album_session> state session:INITIAL { ~.rendered }
on album_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target, relayout(evt.target, {*:session, active:true}))
    dom.focus_set(node(evt.target, "port"), false)
    return 'handled'
}
on album_scroll(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    dom.set_attribute(evt.target, "data-scroll-pending", "false")
    let top = float(dom.scroll_state(node(evt.target, "port")).y)
    if (top != session.top) { session = mount(evt.target, {*:session, top:top}) }
    return 'handled'
}
on album_open(evt) {
    if (evt.event_phase != 2 or session.opening == null or evt.detail != session.opening.frame) { return 'pass' }
    session = step_open(evt.target, session)
    return 'handled'
}
on album_layout(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target, relayout(evt.target, {*:session, layout_frame:0}, centre_tile(session), session.height / 2))
    return 'handled'
}
on resize(evt) {
    let owner = owner_of(evt)
    if (owner != null and session.collage != null) { session = {*:session, collage:CA.preview(owner, CTX, session.collage, session.collage.model)} }
    if (owner != null and session.active and session.layout_frame == 0) {
        session = {*:session, layout_frame:dom.request_frame(owner, "album_layout")}
    }
    return 'pass'
}
on click(evt) {
    if (evt.button != null and evt.button != 0) { return 'pass' }
    if (session.swallow_click) {
        session = {*:session, swallow_click:false}
        return 'prevent-default'
    }
    let owner = owner_of(evt)
    if (session.collage != null) {
        if (dom.closest(evt.target, "#collage") == null) { return 'prevent-default' }
        let template = dom.closest(evt.target, "[data-collage-template]")
        let swatch = dom.closest(evt.target, "[data-collage-bg]")
        if (template != null or swatch != null) {
            let model = if (template != null) C.set(session.collage.model, "template", dom.get_attribute(template, "data-collage-template"))
                else C.set(session.collage.model, "background", dom.get_attribute(swatch, "data-collage-bg"))
            session = {*:session, collage:CA.change(owner, CTX, session.collage, model)}
            return 'handled'
        }
        let button = dom.closest(evt.target, "[data-action]")
        if (button != null) { session = dispatch(owner, session, dom.get_attribute(button, "data-action")) }
        return 'handled'
    }
    if (session.show and dom.closest(evt.target, ".show-bar") == null) { return 'pass' }
    if (session.sheet != null and dom.closest(evt.target, ".sheet") == null) { return 'prevent-default' }
    let nav = dom.closest(evt.target, "[data-nav]")
    let album = dom.closest(evt.target, "[data-album]")
    let action = dom.closest(evt.target, "[data-action]")
    let strip = dom.closest(evt.target, "[data-strip]")
    let tile = dom.closest(evt.target, ".tile")
    if (nav != null or album != null) {
        let key = dom.get_attribute(album, "data-album")
        dom.set_state(node(owner, "search"), "value", "")
        session = rescope(owner, {*:session, query:"", mode:if (album != null) "album" else dom.get_attribute(nav, "data-nav"), album:key or ""})
        dom.focus_set(node(owner, "port"), false)
        return 'handled'
    }
    if (strip != null) {
        session = publish(owner, open_viewer(session, int(dom.get_attribute(strip, "data-strip")) ^ { session.viewer.id }))
        return 'handled'
    }
    if (action != null) {
        session = dispatch(owner, session, dom.get_attribute(action, "data-action"))
        return 'handled'
    }
    if (tile != null and not session.viewer.open) {
        let id = int(dom.get_attribute(tile, "data-id")) ^ { 0 }
        session = publish(owner, select_tile(session, id, evt.metaKey or evt.ctrlKey, evt.shiftKey))
        dom.focus_set(node(owner, "port"), false)
        return 'handled'
    }
    return 'pass'
}
on dblclick(evt) {
    let owner = owner_of(evt)
    let tile = dom.closest(evt.target, ".tile")
    if (tile != null and not session.viewer.open) {
        session = begin_open(owner, publish(owner, open_viewer(session, int(dom.get_attribute(tile, "data-id")) ^ { 0 })))
        dom.focus_set(node(owner, "viewer"), false)
        return 'prevent-default'
    }
    if (session.viewer.open and dom.closest(evt.target, "#viewer-stage") != null and dom.closest(evt.target, "button") == null) {
        session = publish(owner, viewer_zoom(session, if (session.viewer.zoom > 1.0) 1.0 else 2.25))
        return 'prevent-default'
    }
    return 'pass'
}
on input(evt) {
    let owner = owner_of(evt)
    let id = dom.get_attribute(evt.target, "id")
    if (session.collage != null) {
        let value = if (id == "cl-caption") dom.get_state(evt.target, "value") else string(dom.range_value(evt.target))
        if (id != "cl-aspect" and id != "cl-credits") { session = collage_control(owner, session, id, value, false) }
        return 'handled'
    }
    if (id == "search") {
        session = rescope(owner, {*:session, query:dom.get_state(evt.target, "value")})
        return 'handled'
    }
    if (id == "zoom") {
        let target = float(int(dom.get_state(evt.target, "value")) ^ { 160 })
        session = publish(owner, relayout(owner, {*:session, target:target}, centre_tile(session), session.height / 2))
        return 'handled'
    }
    return 'pass'
}
// a control's change ends its edit: the collage history records it once
on change(evt) {
    if (session.collage == null) { return 'pass' }
    let owner = owner_of(evt)
    let id = dom.get_attribute(evt.target, "id")
    let value = if (id == "cl-credits") dom.get_state(evt.target, "checked")
        else if (id == "cl-gutter" or id == "cl-radius" or id == "cl-border") string(dom.range_value(evt.target))
        else if (id == "cl-aspect") [for (a in C.ASPECTS) a.key][dom.selected_index(evt.target) or 0]
        else dom.get_state(evt.target, "value")
    session = collage_control(owner, session, id, value, true)
    return 'handled'
}
on wheel(evt) {
    let owner = owner_of(evt)
    if (session.collage != null) {
        if (dom.closest(evt.target, "#cl-stage") == null) { return 'pass' }
        session = {*:session, collage:CA.wheel(owner, CTX, session.collage, evt)}
        return 'prevent-default'
    }
    if (session.viewer.open) {
        if (dom.closest(evt.target, "#viewer-stage") == null) { return 'pass' }
        session = publish(owner, viewer_zoom(session, session.viewer.zoom * (if (evt.deltaY < 0) 1.15 else 1 / 1.15)))
        return 'prevent-default'
    }
    // ctrl/⌘ + wheel (and trackpad pinch) resizes tiles around the photo under the pointer
    if ((evt.ctrlKey or evt.metaKey) and dom.closest(evt.target, "#port") != null) {
        let port = dom.bounding_box(node(owner, "port"))
        let grid = dom.bounding_box(node(owner, "grid"))
        let keep = G.tile_at(session.lay, evt.clientX - grid.x, evt.clientY - grid.y) or centre_tile(session)
        let target = float(G.zoom_step(int(session.target), if (evt.deltaY < 0) 1 else -1))
        if (target != session.target) {
            dom.set_state(node(owner, "zoom"), "value", string(int(target)))
            session = publish(owner, relayout(owner, {*:session, target:target}, keep, evt.clientY - port.y))
        }
        return 'prevent-default'
    }
    return 'pass'
}
on pointerdown(evt) {
    if (evt.button != 0) { return 'pass' }
    session = {*:session, swallow_click:false}
    let owner = owner_of(evt)
    if (session.collage != null) {
        if (dom.closest(evt.target, "#cl-stage,#cl-tray") == null) { return 'pass' }
        session = {*:session, collage:CA.press(owner, CTX, session.collage, evt)}
        dom.focus_set(node(owner, "collage"), false)
        dom.set_pointer_capture(node(owner, "collage"), evt.pointerId)
        return 'prevent-default'
    }
    if (session.viewer.open) {
        if (session.viewer.zoom > 1.0 and dom.closest(evt.target, "#viewer-image") != null) {
            session = {*:session, pan:{x:evt.clientX, y:evt.clientY, ox:session.viewer.x, oy:session.viewer.y, pointer:evt.pointerId}}
            dom.set_pointer_capture(node(owner, "viewer-stage"), evt.pointerId)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (dom.closest(evt.target, "#scrubber") != null) {
        session = {*:session, scrub:{pointer:evt.pointerId}}
        dom.set_pointer_capture(node(owner, "scrubber"), evt.pointerId)
        session = scrub_to(owner, session, evt.clientY)
        return 'prevent-default'
    }
    // rubber-band selection starts on the grid background, not on a tile
    if (dom.closest(evt.target, "#grid") != null and dom.closest(evt.target, ".tile") == null and session.lay != null) {
        let grid = dom.bounding_box(node(owner, "grid"))
        let base = if (evt.metaKey or evt.ctrlKey or evt.shiftKey) session.selected else []
        session = {*:session, band:{x:evt.clientX - grid.x, y:evt.clientY - grid.y, base:base, pointer:evt.pointerId}, selected:base}
        dom.set_pointer_capture(node(owner, "grid"), evt.pointerId)
        return 'prevent-default'
    }
    return 'pass'
}
on pointermove(evt) {
    let owner = owner_of(evt)
    if (session.collage != null and session.collage.drag != null) {
        session = {*:session, collage:CA.move(owner, CTX, session.collage, evt)}
        return 'prevent-default'
    }
    if (session.pan != null) {
        let pan = session.pan
        // the zoomed image may move by its overflow beyond the stage on each side, no further
        let stage = dom.bounding_box(node(owner, "viewer-stage"))
        let limit_x = stage.width * (session.viewer.zoom - 1.0) / 2.0
        let limit_y = stage.height * (session.viewer.zoom - 1.0) / 2.0
        let x = max(0.0 - limit_x, min(limit_x, pan.ox + evt.clientX - pan.x))
        let y = max(0.0 - limit_y, min(limit_y, pan.oy + evt.clientY - pan.y))
        session = publish(owner, {*:session, viewer:{*:session.viewer, x:x, y:y}})
        return 'prevent-default'
    }
    if (session.scrub != null) {
        session = scrub_to(owner, session, evt.clientY)
        return 'prevent-default'
    }
    if (session.band != null) {
        let band = session.band
        let grid = dom.bounding_box(node(owner, "grid"))
        let x = evt.clientX - grid.x
        let y = evt.clientY - grid.y
        draw_band(owner, band, x, y)
        let hits = G.tiles_in(session.lay, min(band.x, x), min(band.y, y), max(band.x, x), max(band.y, y))
        session = publish(owner, {*:session, selected:[*band.base, *[for (id in hits where not contains(band.base, id)) id]]})
        return 'prevent-default'
    }
    return 'pass'
}
on pointerup(evt) {
    if (session.collage != null and session.collage.drag != null) {
        session = {*:session, collage:CA.release(owner_of(evt), CTX, session.collage, evt), swallow_click:session.collage.drag.moved}
        return 'prevent-default'
    }
    if (session.pan == null and session.scrub == null and session.band == null) { return 'pass' }
    session = end_gesture(owner_of(evt), session)
    return 'prevent-default'
}
on pointercancel(evt) {
    session = end_gesture(owner_of(evt), session)
    return 'pass'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let key = evt.key
    let modifier = evt.metaKey or evt.ctrlKey
    if (session.collage != null) {
        if (key == "Escape") { session = dispatch(owner, session, "collage-close"); return 'prevent-default' }
        if (dom.closest(evt.target, "input,textarea,select") != null) { return 'pass' }
        if (modifier and lower(key) == "z") { session = dispatch(owner, session, if (evt.shiftKey) "collage-redo" else "collage-undo"); return 'prevent-default' }
        return 'pass'
    }
    if (session.show) {
        // the player owns its keys; Escape closes the show once the player has nothing of its own to dismiss
        let player = node(owner, "album-show")
        let busy = player != null and any([for (name in ["overview", "blackout", "pointer"]) dom.get_attribute(player, "data-slide-" ++ name) == "true"])
        if (key == "Escape" and not busy) { session = dispatch(owner, session, "show-close"); return 'prevent-default' }
        return 'pass'
    }
    if (session.sheet != null) {
        if (key == "Escape") { session = dispatch(owner, session, "show-cancel"); return 'prevent-default' }
        if (key == "Enter" and dom.closest(evt.target, "select,button") == null) { session = dispatch(owner, session, "show-present"); return 'prevent-default' }
        return 'pass'
    }
    if (session.viewer.open) {
        let action = if (key == "Escape") "close-viewer" else if (key == "ArrowLeft") "prev" else if (key == "ArrowRight") "next"
            else if (lower(key) == "i") "toggle-info" else if (lower(key) == "f") "toggle-favorite"
            else if (key == "+" or key == "=") "viewer-zoom-in" else if (key == "-" or key == "_") "viewer-zoom-out" else ""
        if (action != "") { session = dispatch(owner, session, action); return 'prevent-default' }
        if (key == "0") { session = publish(owner, viewer_zoom(session, 1.0)); return 'prevent-default' }
        if (key == "Home" or key == "End") {
            session = publish(owner, open_viewer(session, if (key == "Home") session.ids[0] else session.ids[last]))
            return 'prevent-default'
        }
        return 'pass'
    }
    if (modifier and lower(key) == "f") { dom.focus_set(node(owner, "search"), false); return 'prevent-default' }
    if (dom.closest(evt.target, "input,textarea,select") != null) {
        if (key == "Escape" and dom.get_attribute(evt.target, "id") == "search") {
            dom.set_state(evt.target, "value", "")
            session = rescope(owner, {*:session, query:""})
            dom.focus_set(node(owner, "port"), false)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (modifier and lower(key) == "a") { session = dispatch(owner, session, "select-all"); return 'prevent-default' }
    if (key == "Escape") { session = dispatch(owner, session, "clear-selection"); return 'prevent-default' }
    if (key == "+" or key == "=") { session = dispatch(owner, session, "zoom-in"); return 'prevent-default' }
    if (key == "-" or key == "_") { session = dispatch(owner, session, "zoom-out"); return 'prevent-default' }
    if (lower(key) == "f" and (len(session.selected) > 0 or session.focus > 0)) {
        let chosen = if (len(session.selected) > 0) session else {*:session, selected:[session.focus]}
        session = dispatch(owner, chosen, "favorite-selected")
        return 'prevent-default'
    }
    if (len(session.ids) == 0 or session.lay == null) { return 'pass' }
    if (key == "Enter" and session.focus > 0) {
        session = begin_open(owner, publish(owner, open_viewer(session, session.focus)))
        dom.focus_set(node(owner, "viewer"), false)
        return 'prevent-default'
    }
    if (key == " " and session.focus > 0) {
        session = publish(owner, {*:session, selected:toggle(session.selected, session.focus), anchor:session.focus})
        return 'prevent-default'
    }
    if (contains(["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End"], key)) {
        let id = if (key == "Home") session.ids[0] else if (key == "End") session.ids[last]
            else if (session.focus == 0) session.ids[0] else G.neighbour(session.lay, session.ids, session.focus, key)
        let moved = if (evt.shiftKey) select_tile(session, id, false, true) else {*:session, focus:id, anchor:id}
        session = publish(owner, reveal(owner, moved, id))
        return 'prevent-default'
    }
    return 'pass'
}

view <album_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target, "album")
    if (owner != null) dom.request_frame(owner, "album_activate")
    return 'handled'
}

<html lang:"en",<head <meta charset:"UTF-8"><meta name:"viewport",content:"width=device-width, initial-scale=1">
    <title "Photos — Lambda"><style slide_html.css><link rel:"stylesheet",href:url_resolve(BASE, "photo-album.css")>>
    apply(<album_document rendered:<body apply(<album_session rendered:ui.tree(LIB, FETCH)>)>>)
>
