// Collage editor procedures: open, render, gestures, history, save/open and export (§3.3).
// The editor state is {model, history, source, selected, scale, drag}; `ctx` is {lib, dir, dir_url}.
import dom
import radiant
import updates: ~~.mod_publish
import L: .mod_library
import C: .mod_collage
import CU: .mod_collage_ui

// editor chrome around the stage, as sized in photo-album.css (.cl-bar, .cl-tray, .cl-panel, .cl-stage padding)
let CHROME_W = 180.0 + 280.0 + 48.0
let CHROME_H = 52.0 + 48.0

fn node(owner, id) => dom.get_element_by_id(owner, id)
pub fn lay(ctx, m) => C.layout(m, C.slot_aspects(ctx.lib, m))

// the canvas fits the stage; the stage is measured from the viewport because the layer may still be hidden
pn fit(owner, lay) {
    let screen = dom.viewport_size(owner)
    let w = (if (screen == null) 1200.0 else screen.width) - CHROME_W
    let h = (if (screen == null) 800.0 else screen.height) - CHROME_H
    return max(0.05, min(w / lay.width, h / lay.height))
}

pn render(owner, ctx, cs) {
    let l = lay(ctx, cs.model)
    let scale = fit(owner, l)
    updates.markup(node(owner, "cl-canvas-host"), format([CU.canvas(ctx.lib, cs.model, l,
        {scale:scale, source:"thumb", selected:cs.selected, handles:true})], 'html'))
    updates.markup(node(owner, "cl-tray"), format([CU.tray(ctx.lib, cs.source, cs.model)], 'html'))
    let m = cs.model
    for (b in dom.query_selector_all(owner, "[data-collage-template]")) updates.attr(b, "aria-pressed", dom.get_attribute(b, "data-collage-template") == m.template)
    for (b in dom.query_selector_all(owner, "[data-collage-bg]")) updates.attr(b, "aria-pressed", dom.get_attribute(b, "data-collage-bg") == m.background)
    updates.attr(node(owner, "collage"), "data-template", m.template)
    if (len(cs.history.past) == 0) dom.set_attribute(node(owner, "cl-undo"), "disabled", "") else dom.remove_attribute(node(owner, "cl-undo"), "disabled")
    if (len(cs.history.future) == 0) dom.set_attribute(node(owner, "cl-redo"), "disabled", "") else dom.remove_attribute(node(owner, "cl-redo"), "disabled")
    updates.show(owner, "cl-shuffle", m.template == "scrapbook")
    return {*:cs, scale:scale}
}

// form controls mirror the model when it is opened, undone or redone
pn sync_controls(owner, m) {
    // select values are set by index: set_state("value") is defined on text controls only
    dom.set_selected_index(node(owner, "cl-aspect"), index_of([for (a in C.ASPECTS) a.key], m.aspect) or 0)
    // ranges take their value as a property; set_state("value") is defined on text controls only
    dom.set_property(node(owner, "cl-gutter"), "value", string(m.gutter))
    dom.set_property(node(owner, "cl-radius"), "value", string(m.radius))
    dom.set_property(node(owner, "cl-border"), "value", string(m.border))
    dom.set_state(node(owner, "cl-caption"), "value", m.caption or "")
    dom.set_state(node(owner, "cl-credits"), "checked", m.credits)
}

pn saved_list(owner, ctx) {
    let dir = ctx.dir ++ "collages"
    let names = if (exists(dir)) [for (e in (input(dir, 'dir') ^ { [] }) where ends_with(e.name, ".mark")) e.name] else []
    updates.markup(node(owner, "cl-saved"), join([for (n in sort(names)) format([<option value:n, slice(n, 0, len(n) - 5)>], 'html')], ""))
    if (len(names) == 0) dom.set_attribute(node(owner, "cl-open"), "disabled", "")
    else {
        dom.remove_attribute(node(owner, "cl-open"), "disabled")
        dom.set_selected_index(node(owner, "cl-saved"), 0)
    }
}

pub pn start(owner, ctx, ids, title) {
    let model = C.create(ids, title)
    sync_controls(owner, model)
    saved_list(owner, ctx)
    updates.text(owner, "cl-status", "")
    return render(owner, ctx, {model:model, history:C.history(), source:ids, selected:-1, scale:1.0, drag:null})
}

// every edit goes through here: the previous model joins the undo history
pub pn change(owner, ctx, cs, model) {
    if (model == cs.model) { return cs }
    return render(owner, ctx, {*:cs, model:model, history:C.record(cs.history, cs.model)})
}

// draws a model without recording it, for edits still in progress
pub pn preview(owner, ctx, cs, model) {
    return render(owner, ctx, {*:cs, model:model})
}

pub pn undo(owner, ctx, cs, forward) {
    let step = if (forward) C.redo(cs.history, cs.model) else C.undo(cs.history, cs.model)
    if (step == null) { return cs }
    sync_controls(owner, step.model)
    return render(owner, ctx, {*:cs, model:step.model, history:step.history})
}

// ---------------------------------------------------------------- gestures

fn slot_of(el) => int(dom.get_attribute(el, "data-slot")) ^ { -1 }

pub pn press(owner, ctx, cs, evt) {
    let handle = dom.closest(evt.target, "[data-handle]")
    let item = dom.closest(evt.target, "[data-slot]")
    let tray = dom.closest(evt.target, "[data-tray]")
    if (tray != null) {
        return {*:cs, drag:{kind:"tray", id:int(dom.get_attribute(tray, "data-tray")) ^ { 0 }, x:evt.clientX, y:evt.clientY, moved:false}}
    }
    if (item == null) { return if (cs.selected >= 0) render(owner, ctx, {*:cs, selected:-1}) else cs }
    let slot = slot_of(item)
    let c = C.cell_at(lay(ctx, cs.model), slot)
    let kind = if (handle != null) dom.get_attribute(handle, "data-handle") else if (c.kind == "print") "move" else "cell"
    let rect = dom.bounding_box(item)
    let next = if (cs.selected != slot) render(owner, ctx, {*:cs, selected:slot}) else cs
    return {*:next, drag:{kind:kind, slot:slot, x:evt.clientX, y:evt.clientY, moved:false, start:next.model,
        box:{x:c.x, y:c.y, w:c.w, rot:c.rot}, rect:{x:rect.x, y:rect.y, w:rect.width, h:rect.height}}}
}

pn ghost(owner, x, y, visible) {
    let el = node(owner, "cl-ghost")
    updates.style(el, "display", if (visible) "block" else "none")
    updates.style(el, "left", string(x - 30) ++ "px")
    updates.style(el, "top", string(y - 30) ++ "px")
}

// Dragging inside a cell pans its crop; leaving the cell turns the drag into a swap. Prints move,
// rotate about their centre or resize from the corner.
pub pn move(owner, ctx, cs, evt) {
    let d = cs.drag
    let dx = evt.clientX - d.x
    let dy = evt.clientY - d.y
    let moved = d.moved or abs(dx) + abs(dy) > 3.0
    if (not moved) { return cs }
    let drag = {*:d, moved:true}
    if (d.kind == "tray" or d.kind == "swap") {
        ghost(owner, evt.clientX, evt.clientY, true)
        return {*:cs, drag:drag}
    }
    if (d.kind == "cell") {
        let outside = evt.clientX < d.rect.x or evt.clientX > d.rect.x + d.rect.w or evt.clientY < d.rect.y or evt.clientY > d.rect.y + d.rect.h
        if (outside) {
            ghost(owner, evt.clientX, evt.clientY, true)
            return render(owner, ctx, {*:cs, model:d.start, drag:{*:drag, kind:"swap"}})
        }
        let zoom = d.start.slots[d.slot].zoom
        let model = C.pan(d.start, d.slot, 0.0 - dx / (d.rect.w * zoom), 0.0 - dy / (d.rect.h * zoom))
        return render(owner, ctx, {*:cs, model:model, drag:drag})
    }
    let b = d.box
    let placed = if (d.kind == "move") {*:b, x:b.x + dx / cs.scale, y:b.y + dy / cs.scale}
        else if (d.kind == "resize") {*:b, w:max(80.0, b.w + dx / cs.scale)}
        else (let cx = d.rect.x + d.rect.w / 2.0, let cy = d.rect.y + d.rect.h / 2.0,
            {*:b, rot:b.rot + (math.atan2(evt.clientY - cy, evt.clientX - cx) - math.atan2(d.y - cy, d.x - cx)) * 57.29577951308232})
    return render(owner, ctx, {*:cs, model:C.place(d.start, d.slot, placed), drag:drag})
}

pub pn release(owner, ctx, cs, evt) {
    let d = cs.drag
    ghost(owner, 0, 0, false)
    if (d == null) { return cs }
    let ended = {*:cs, drag:null}
    if (not d.moved) { return ended }
    if (d.kind == "tray" or d.kind == "swap") {
        let hit = dom.element_from_point(dom.root_node(owner), evt.clientX, evt.clientY)
        let target = if (hit == null) null else dom.closest(hit, "[data-slot]")
        if (target == null) { return render(owner, ctx, ended) }
        let slot = slot_of(target)
        let model = if (d.kind == "tray") C.assign(cs.model, slot, d.id) else C.swap(d.start, d.slot, slot)
        return change(owner, ctx, {*:ended, selected:slot}, model)
    }
    // pans and print edits were drawn live; record them as one step from the drag's start
    return render(owner, ctx, {*:ended, history:C.record(cs.history, d.start)})
}

pub pn wheel(owner, ctx, cs, evt) {
    let item = dom.closest(evt.target, "[data-slot]")
    if (item == null) { return cs }
    return change(owner, ctx, cs, C.zoom(cs.model, slot_of(item), if (evt.deltaY < 0) 1.12 else 1.0 / 1.12))
}

// ---------------------------------------------------------------- files

// collages save as Mark data under <corpus>/collages/ and reopen from the editor's list
pub pn save_model(owner, ctx, cs) any^ {
    let dir = ctx.dir ++ "collages/"
    if (not exists(ctx.dir ++ "collages")) { io.mkdir(ctx.dir ++ "collages")^ }
    let file = L.slug(cs.model.caption, "collage") ++ ".mark"
    output(cs.model, dir ++ file, {format:'mark', atomic:true})^
    saved_list(owner, ctx)
    return "Saved as collages/" ++ file
}

pub pn load_saved(owner, ctx, cs) any^ {
    let file = dom.get_state(node(owner, "cl-saved"), "value")
    let model = input(ctx.dir ++ "collages/" ++ file, 'mark')^
    // a collage may only name photos this library has
    if (not C.valid(model) or any([for (s in model.slots) not (s.id is int) or s.id < 1 or s.id > len(ctx.lib.photos)]))
        raise error("Collage: " ++ file ++ " is not a collage of this library")
    sync_controls(owner, model)
    return change(owner, ctx, {*:cs, selected:-1}, model)
}

// Renders a collage through Radiant at `long_edge` pixels into `stem`.png or .jpg, with
// `stem`-credits.txt beside it; the export markup is the editor's canvas markup at export scale.
pub pn render_file(ctx, model, long_edge, fmt, stem) any^ {
    let l = lay(ctx, model)
    let scale = long_edge / max(l.width, l.height)
    let path = stem ++ (if (fmt == "jpeg") ".jpg" else ".png")
    let html = format([CU.export_document(ctx.lib, model, l, scale)], 'html')
    let width = int(round(l.width * scale))
    let height = int(round(l.height * scale))
    let started = clock()
    let done = radiant.export_document(html, path, {format:fmt, width:width, height:height, local_resources:true})
    if (done is error) raise done
    if (done != true) raise error("Collage: " ++ string(done))
    output(C.credits_text(ctx.lib, model), stem ++ "-credits.txt", {format:'text', atomic:true})^
    return {path:path, width:width, height:height, scale:scale, ms:round((clock() - started) * 1000)}
}

// exports with the size and format chosen in the panel, under <corpus>/collages/
pub pn export_image(owner, ctx, cs) any^ {
    let long_edge = [1080.0, 2048.0, 4096.0][dom.selected_index(node(owner, "cl-size")) or 1] or 2048.0
    let fmt = ["png", "jpeg"][dom.selected_index(node(owner, "cl-format")) or 0] or "png"
    if (not exists(ctx.dir ++ "collages")) { io.mkdir(ctx.dir ++ "collages")^ }
    return render_file(ctx, cs.model, long_edge, fmt, ctx.dir ++ "collages/" ++ L.slug(cs.model.caption, "collage"))
}
