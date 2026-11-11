// lambda demo paint — interactive painting with lambda.paint (Lambda_Pkg_Paint.md PT10). Pointer
// handlers feed pen samples into the session; one frame handler presents the dirty rectangle
// through dom.set_canvas_pixels_rect, so only changed pixels are uploaded and repainted (PE7).
import dom
import paint: lambda.paint
import resources: ~~.mod_resources
import model: .mod_model
import ui: .mod_ui
import present: .mod_present

let BASE = url_resolve(resources.entry_uri()^, ".")
let BRUSHES = model.palette(BASE)^
let INITIAL = model.initial(BRUSHES)^
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target), "paint")

// one frame request at a time; the frame handler clears the token.
pn requested(owner, st) {
    if (st.token != 0) { return st }
    return {*: st, token: dom.request_frame(owner, "paint_frame")}
}

pn publish(owner, st) {
    present.sync(owner, st)
    return requested(owner, st)
}

// the stage's size and the pointer's place in it, in stage CSS pixels.
pn stage_point(owner, evt) {
    let b = dom.bounding_box(present.node(owner, "stage"))
    return {x: evt.clientX - b.x, y: evt.clientY - b.y, width: b.width, height: b.height}
}

// the event's pen samples in canvas pixels; a mouse paints at full pressure.
pn samples(owner, st, evt) {
    let box = dom.bounding_box(present.node(owner, "canvas"))
    return paint.in_layer(dom.pointer_samples(evt), box, st.ps, if (evt.pointerType == "mouse") 1.0 else null)
}

// layer pixel under the pointer, or null outside the canvas.
pn canvas_pixel(owner, st, evt) {
    let box = dom.bounding_box(present.node(owner, "canvas"))
    let x = int(floor((evt.clientX - box.x) * model.width(st) / box.width))
    let y = int(floor((evt.clientY - box.y) * model.height(st) / box.height))
    if ((x < 0 or y < 0 or x >= model.width(st) or y >= model.height(st))^) { return null }
    return [x, y]
}

pn failed(owner, st, message) {
    return publish(owner, {*: st, status: message})
}

// a path typed in the app, relative to the working directory.
pn local_path(owner, id) {
    let path = trim(dom.get_state(present.node(owner, id), "value"))
    if (path == "") { return "" }
    if (starts_with(path, "/")^) { return path }
    return sys.proc.self.cwd# ++ "/" ++ path
}

pn parent_dir(path) {
    let cut = last_index_of(path, "/") ^ { -1 }
    if (cut > 0) io.mkdir(slice(path, 0, cut)) ^ { null }
}

pn command(owner, st, action) any^ {
    if (action == "save" or action == "export") {
        let path = local_path(owner, "file-path")^
        if (path == "" or not (ends_with(lower(path), ".painting") ^ { false })) raise error("Paint: choose a .painting file")
        parent_dir(path)^
        if (action == "save") {
            let n = paint.save_painting(st.ps, path)^
            dom.set_attribute(owner, "data-saved", path)
            return {*: st, status: "Saved " ++ path ++ " (" ++ string(n div 1024)^ ++ " KB)"}
        }
        let png = replace(path, ".painting", ".png")
        let n = paint.export_png(st.ps, png)^
        dom.set_attribute(owner, "data-exported", png)
        return {*: st, status: "Exported " ++ png ++ " (" ++ string(n div 1024)^ ++ " KB)"}
    }
    if (action == "open") {
        let path = local_path(owner, "file-path")^
        let opened = paint.open_painting(path)^
        let b = dom.bounding_box(present.node(owner, "stage"))
        return model.fit({*: st, ps: opened, stroke: null, drag: null, status: "Opened " ++ path}, b.width, b.height)
    }
    if (action == "undo") { return {*: st, ps: paint.undo(st.ps)^, status: "Undone"} }
    if (action == "redo") { return {*: st, ps: paint.redo(st.ps)^, status: "Redone"} }
    if (action == "clear") { return {*: st, ps: paint.clear(st.ps)^, status: "Canvas cleared; undo brings it back"} }
    let b = dom.bounding_box(present.node(owner, "stage"))
    if (action == "zoom-in") { return model.zoom_about(st, 1.25, b.width / 2.0, b.height / 2.0) }
    if (action == "zoom-out") { return model.zoom_about(st, 0.8, b.width / 2.0, b.height / 2.0) }
    if (action == "fit") { return model.fit(st, b.width, b.height) }
    if (action == "actual") { return model.actual(st, b.width, b.height) }
    if (action == "import") {
        let path = trim(dom.get_state(present.node(owner, "myb-path"), "value"))
        if (path == "" or not (ends_with(lower(path), ".myb") ^ { false })) raise error("Paint: choose a .myb brush file")
        let file = url_resolve("file://" ++ sys.proc.self.cwd# ++ "/", path)
        let r = paint.brush_from_myb(input(file, 'json')^)^
        let name = replace(slice(path, (last_index_of(path, "/") ^ { -1 }) + 1, len(path)), ".myb", "")
        let added = {*: st, brushes: [*st.brushes, model.entry(name, r.brush)]}
        let ignored = if (len(r.ignored) == 0) "" else " (not rendered: " ++ join(r.ignored, ", ") ++ ")"
        return {*: model.with_brush(added, len(st.brushes)), status: "Imported " ++ name ++ ignored}
    }
    return st
}

pn dispatch(owner, st, action) {
    let next = command(owner, st, action) ^ { return failed(owner, st, ^.message) }
    return publish(owner, next)
}

view <paint_session> state session: INITIAL { ~.rendered }
on paint_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let b = dom.bounding_box(present.node(evt.target, "stage"))
    session = publish(evt.target, model.fit(session, b.width, b.height))
    return 'handled'
}
on paint_frame(evt) {
    if ((evt.event_phase != 2 or evt.detail != session.token)^) { return 'pass' }
    let f = paint.frame(session.ps) ^ { session = {*: session, token: 0}; return 'handled' }
    if (f.rect != null) {
        let canvas = present.node(evt.target, "canvas")
        // the first frame sizes the bitmap; later frames write only their rectangle
        if (f.rect == [0, 0, model.width(session), model.height(session)]) dom.set_canvas_pixels(canvas, f.pixels)^
        else dom.set_canvas_pixels_rect(canvas, f.pixels, f.rect)^
    }
    session = {*: session, ps: f.session, token: 0}
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target, "button")
    if (button == null) { return 'pass' }
    let owner = owner_of(evt)
    let t = dom.get_attribute(button, "data-tool")
    let b = dom.get_attribute(button, "data-brush")
    let c = dom.get_attribute(button, "data-color")
    let a = dom.get_attribute(button, "data-action")
    if (t != null) { session = publish(owner, model.with_tool(session, t)) }
    else if (b != null) { session = publish(owner, model.with_brush(session, int(b))) }
    else if (c != null) {
        let rgb = model.hex_rgb(c) ^ { return 'pass' }
        session = publish(owner, {*: model.with_color(session, rgb), status: "Colour " ++ c})
    }
    else if (a != null) { session = dispatch(owner, session, a) }
    else { return 'pass' }
    return 'handled'
}
on change(evt) {
    if (dom.get_attribute(evt.target, "id") != "paper") { return 'pass' }
    let owner = owner_of(evt)
    let next = model.with_paper(session, dom.get_state(evt.target, "value")) ^ { session = failed(owner, session, ^.message); return 'handled' }
    session = publish(owner, next)
    return 'handled'
}
on input(evt) {
    let id = dom.get_attribute(evt.target, "id")
    if (not (id in ["size", "opacity", "hue", "sat", "val"])) { return 'pass' }
    let owner = owner_of(evt)
    let v = float(dom.get_state(evt.target, "value"))
    if (id == "size") { session = publish(owner, model.with_size(session, v)) }
    else if (id == "opacity") { session = publish(owner, model.with_opacity(session, v / 100.0)) }
    else {
        let h = float(dom.get_state(present.node(owner, "hue"), "value"))
        let s = float(dom.get_state(present.node(owner, "sat"), "value")) / 100.0
        let value = float(dom.get_state(present.node(owner, "val"), "value")) / 100.0
        session = publish(owner, model.with_color(session, model.from_hsv(h, s, value)))
    }
    return 'handled'
}
on pointerdown(evt) {
    if (dom.closest(evt.target, "#stage") == null) { return 'pass' }
    let owner = owner_of(evt)
    let stage = present.node(owner, "stage")
    dom.focus_set(owner, false)
    // the middle button, the hand tool or a held space bar pans the view
    if ((evt.button == 1 or session.tool == "hand" or session.space)^) {
        let p = stage_point(owner, evt)
        dom.set_pointer_capture(stage, evt.pointerId)
        session = publish(owner, {*: session, drag: {pointer: evt.pointerId, x: p.x, y: p.y, pan: session.pan}})
        return 'prevent-default'
    }
    let pixel = canvas_pixel(owner, session, evt)
    if ((session.tool == "picker")^) {
        if ((pixel == null)^) { return 'prevent-default' }
        let rgb = paint.color_at(session.ps, pixel[0], pixel[1]) ^ { return 'prevent-default' }
        session = publish(owner, {*: model.with_color(session, rgb), status: "Picked " ++ model.hex(rgb)})
        return 'prevent-default'
    }
    // a pen's eraser end presses button 5 (Pointer Events 3)
    let eraser = evt.button == 5
    if (evt.button != 0 and not eraser) { return 'pass' }
    let b = model.stroke_brush(session, eraser) ^ { session = failed(owner, session, ^.message); return 'prevent-default' }
    var ps = paint.begin(session.ps, evt.pointerId, b, session.color) ^ { session = failed(owner, session, ^.message); return 'prevent-default' }
    // feed paints in place (PT7); a failed feed leaves session.ps as it was
    paint.feed(ps, evt.pointerId, samples(owner, session, evt)) ^ { session = failed(owner, session, ^.message); return 'prevent-default' }
    dom.set_pointer_capture(stage, evt.pointerId)
    session = requested(owner, {*: session, ps: ps, stroke: {pointer: evt.pointerId, eraser: eraser}})
    return 'prevent-default'
}
on pointermove(evt) {
    let owner = owner_of(evt)
    if ((session.drag != null and session.drag.pointer == evt.pointerId)^) {
        let p = stage_point(owner, evt)
        let d = session.drag
        session = {*: session, pan: {x: d.pan.x + p.x - d.x, y: d.pan.y + p.y - d.y}}
        present.sheet(owner, session)^
        return 'prevent-default'
    }
    if ((session.stroke == null or session.stroke.pointer != evt.pointerId)^) { return 'pass' }
    var ps = session.ps
    paint.feed(ps, evt.pointerId, samples(owner, session, evt)) ^ { return 'prevent-default' }
    session = requested(owner, {*: session, ps: ps})
    return 'prevent-default'
}
on pointerup(evt) {
    let owner = owner_of(evt)
    let stage = present.node(owner, "stage")
    if ((session.drag != null and session.drag.pointer == evt.pointerId)^) {
        dom.release_pointer_capture(stage, evt.pointerId)
        session = publish(owner, {*: session, drag: null})
        return 'prevent-default'
    }
    if ((session.stroke == null or session.stroke.pointer != evt.pointerId)^) { return 'pass' }
    dom.release_pointer_capture(stage, evt.pointerId)
    let ended = paint.end(session.ps, evt.pointerId) ^ { paint.cancel(session.ps, evt.pointerId)^ }
    session = publish(owner, {*: session, ps: ended, stroke: null})
    return 'prevent-default'
}
on pointercancel(evt) {
    if ((session.stroke == null or session.stroke.pointer != evt.pointerId)^) { return 'pass' }
    let owner = owner_of(evt)
    session = publish(owner, {*: session, ps: paint.cancel(session.ps, evt.pointerId)^, stroke: null, drag: null})
    return 'handled'
}
on wheel(evt) {
    if (dom.closest(evt.target, "#stage") == null) { return 'pass' }
    let owner = owner_of(evt)
    let p = stage_point(owner, evt)
    // a pinch or a modified wheel zooms about the pointer; a plain wheel pans
    if (evt.ctrlKey or evt.metaKey) { session = model.zoom_about(session, if (evt.deltaY < 0) 1.1 else 1.0 / 1.1, p.x, p.y) }
    else { session = {*: session, pan: {x: session.pan.x - evt.deltaX, y: session.pan.y - evt.deltaY}} }
    present.sync(owner, session)
    return 'prevent-default'
}
on keydown(evt) {
    if (dom.closest(evt.target, "input") != null) { return 'pass' }
    let owner = owner_of(evt)
    let key = lower(evt.key)
    let modifier = evt.ctrlKey or evt.metaKey
    if (key == " ") {
        if ((not session.space)^) { session = publish(owner, {*: session, space: true}) }
        return 'prevent-default'
    }
    let action = if (modifier and key == "z") (if (evt.shiftKey) "redo" else "undo")
        else if (modifier and key == "s") "save" else if (modifier and key == "o") "open"
        else if (not modifier and (key == "+" or key == "=")) "zoom-in" else if (not modifier and key == "-") "zoom-out"
        else if (not modifier and key == "0") "fit" else if (not modifier and key == "1") "actual" else null
    if (action != null) { session = dispatch(owner, session, action); return 'prevent-default' }
    let tool = if (modifier) null else if (key == "b") "brush" else if (key == "e") "eraser" else if (key == "i") "picker"
        else if (key == "h") "hand" else null
    if (tool != null) { session = publish(owner, model.with_tool(session, tool)); return 'prevent-default' }
    if (not modifier and (key == "[" or key == "]")) {
        let e = model.editing(session)
        session = publish(owner, model.with_size(session, e.size * (if (key == "[") 0.8 else 1.25)))
        return 'prevent-default'
    }
    return 'pass'
}
on keyup(evt) {
    if ((evt.key != " " or not session.space)^) { return 'pass' }
    session = publish(owner_of(evt), {*: session, space: false})
    return 'handled'
}
on resize(evt) {
    session = requested(owner_of(evt), session)
    return 'pass'
}
view <paint_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target, "paint")
    if (owner != null) dom.request_frame(owner, "paint_activate")
    return 'handled'
}
<html lang:"en", <head <meta charset:"UTF-8"> <title "Paint — Lambda / Radiant">
    <link rel:"stylesheet", href:url_resolve(BASE, "paint.css")>>
    apply(<paint_document rendered:<body apply(<paint_session rendered:ui.tree(INITIAL)>)>>)
>
