// lambda demo paint — keeps the retained shell in step with the model: control states, the sheet's
// zoom and pan, and the status line. Canvas pixels are written by the frame handler (PE7).
import dom
import model: .mod_model
import ui: .mod_ui

pub fn node(owner, id) => dom.get_element_by_id(owner, id)

fn text(v) string => string(v) ^ { "" }
fn whole(v) string => text(int(round(v)) ^ { 0 })

pn set_class(target, name, on) {
    let current = dom.get_attribute(target, "class")
    let classes = [for (c in split(if (current == null) "" else current, " ")^ where c != "" and c != name) c]
    let next = join(if (on) [*classes, name] else classes, " ")^
    if (next != current) dom.set_attribute(target, "class", next)
}

pn set_text(target, value) {
    if (dom.text_content(target) != value) dom.set_text_content(target, value)
}

// a slider follows the model unless the user is dragging it.
pn set_slider(owner, id, value, label) {
    let control = node(owner, id)
    if (not dom.matches(control, ":focus") and dom.get_state(control, "value") != text(value)) dom.set_state(control, "value", text(value))
    set_text(node(owner, id ++ "-value"), label)^
}

// the canvas sheet's place in the stage: zoom and pan move it by left, top, width and height, never
// by a transform, so rectangle writes keep their region repaint (PE7).
pub pn sheet(owner, st) {
    let s = node(owner, "sheet")
    dom.style_set_property(s, "left", whole(st.pan.x)^ ++ "px")
    dom.style_set_property(s, "top", whole(st.pan.y)^ ++ "px")
    dom.style_set_property(s, "width", whole(model.width(st) * st.zoom)^ ++ "px")
    dom.style_set_property(s, "height", whole(model.height(st) * st.zoom)^ ++ "px")
    set_text(node(owner, "zoom-value"), whole(st.zoom * 100.0)^ ++ "%")^
}

pn brushes(owner, st) {
    let holder = node(owner, "brushes")
    let markup = ui.brush_markup(st)
    if (dom.get_attribute(holder, "data-items") != markup) {
        dom.set_inner_html(holder, markup)
        dom.set_attribute(holder, "data-items", markup)
    }
    for (i, b in st.brushes) {
        let button = dom.query_selector(holder, "[data-brush='" ++ text(i)^ ++ "']")
        if (button != null) set_class(button, "active", st.tool == "brush" and i == st.brush)^
    }
}

// the paper menu follows the session's texture, including one a painting file brought back.
pn present_paper(owner, st) {
    let control = node(owner, "paper")
    let kind = model.paper_kind(st)
    let indexes = [for (i, p in model.PAPERS where p.kind == kind) i]
    if (len(indexes) > 0 and dom.selected_index(control) != indexes[0]) dom.set_selected_index(control, indexes[0])
    dom.set_attribute(owner, "data-paper", kind)
    return kind
}

// the medium menu and the light toggle follow the session.
pn present_medium(owner, st) {
    let control = node(owner, "medium")
    let kind = model.medium(st)
    let indexes = [for (i, m in model.MEDIA where m.kind == kind) i]
    if (len(indexes) > 0 and dom.selected_index(control) != indexes[0]) dom.set_selected_index(control, indexes[0])
    dom.set_attribute(owner, "data-medium", kind)
    let lit = text(st.ps.lighting != null)^
    dom.set_attribute(owner, "data-lit", lit)
    dom.set_attribute(node(owner, "action-light"), "aria-pressed", lit)
    dom.set_attribute(node(owner, "action-light"), "aria-disabled", text(kind != "pigment")^)
    dom.set_attribute(node(owner, "action-dry"), "aria-disabled", text(kind == "pixel")^)
    let wet = node(owner, "wet-pigment")
    dom.set_attribute(wet, "aria-disabled", text(kind != "watercolor")^)
    if (kind == "watercolor" and dom.selected_index(wet) != st.ps.wet_index)
        dom.set_selected_index(wet, st.ps.wet_index)
}

pub pn sync(owner, st) {
    for (t in model.TOOLS) set_class(node(owner, "tool-" ++ t), "active", st.tool == t)^
    brushes(owner, st)^
    let e = model.editing(st)
    set_slider(owner, "size", int(round(e.size)), whole(e.size)^ ++ " px")^
    set_slider(owner, "opacity", int(round(e.opacity * 100.0)), whole(e.opacity * 100.0)^ ++ "%")^
    let hsv = model.hsv(st.color)
    set_slider(owner, "hue", int(round(hsv.h)) % 360, "")^
    set_slider(owner, "sat", int(round(hsv.s * 100.0)), "")^
    set_slider(owner, "val", int(round(hsv.v * 100.0)), "")^
    let paper = present_paper(owner, st)^
    present_medium(owner, st)^
    let color = model.hex(st.color)^
    dom.style_set_property(node(owner, "color-chip"), "background-color", color)
    dom.set_attribute(owner, "data-color", color)
    dom.set_attribute(node(owner, "action-undo"), "aria-disabled", text(len(st.ps.undo) == 0)^)
    dom.set_attribute(node(owner, "action-redo"), "aria-disabled", text(len(st.ps.redo) == 0)^)
    dom.set_attribute(owner, "data-undo", text(len(st.ps.undo))^)
    dom.set_attribute(owner, "data-redo", text(len(st.ps.redo))^)
    dom.set_attribute(owner, "data-tool", st.tool)
    dom.set_attribute(owner, "data-brush", if (st.tool == "eraser") "Eraser" else st.brushes[st.brush].name)
    dom.set_attribute(owner, "data-brushes", text(len(st.brushes))^)
    let stage = node(owner, "stage")
    for (t in model.TOOLS) set_class(stage, "tool-" ++ t, st.tool == t or (t == "hand" and st.space))^
    sheet(owner, st)^
    set_text(node(owner, "status"), st.status ++ "  —  " ++ model.summary(st)^)^
}
