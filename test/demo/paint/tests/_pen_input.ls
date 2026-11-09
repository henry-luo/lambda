// Pen input (Radiant_Design_Paint_Engine PE6, Lambda_Pkg_Paint PT10): pointer events carry the
// W3C pen fields, and dom.pointer_samples returns the samples a move coalesces as f32[N, 7] rows.
import dom
view <pen_probe> { ~.rendered }
// conversions to text may fail, so the helpers are procedures (E208)
pn pct(v) {
    string(int(round(v * 100.0)))
}
// the W3C fields of one pointer event, then its sample row count.
pn fields(evt, rows) {
    join([string(evt.pointerType), pct(evt.pressure), string(evt.tiltX), string(evt.tiltY),
        string(evt.twist), pct(evt.tangentialPressure), string(evt.button), string(evt.buttons),
        string(evt.pointerId), string(rows)], "|")
}
// each row as x from the first row, pressure, tilt and twist; the time steps in ms; and whether the
// last row is the event itself (time = timeStamp / 1000).
pn sample_text(s, evt) {
    let n = len(s)
    let rows = [for (r in s) join([string(int(round(r[0] - s[0][0]))), pct(r[2]), string(int(r[3])),
        string(int(r[4])), string(int(r[5]))], ":")]
    let steps = [for (i in 1 to (n - 1) where n > 1) string(int(round((s[i][6] - s[i - 1][6]) * 1000.0)))]
    let at_end = abs(s[n - 1][6] - evt.timeStamp / 1000.0) < 0.001
    join([join(rows, ","), join(steps, ","), if (at_end) "at-event" else "off-event"], "|")
}
pn record(evt, name) {
    if (dom.closest(evt.target, "#pad") == null) { return 'pass' }
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "fixture")
    let s = dom.pointer_samples(evt)
    dom.set_attribute(owner, name, fields(evt, len(s)))
    if (name == "data-move") { dom.set_attribute(owner, "data-samples", sample_text(s, evt)) }
    return 'handled'
}
on pointerdown(evt) {
    return record(evt, "data-down")
}
on pointermove(evt) {
    return record(evt, "data-move")
}
on pointerup(evt) {
    return record(evt, "data-up")
}
on keydown(evt) {
    // a key event has no pointer position, so it has no samples
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "fixture")
    dom.set_attribute(owner, "data-key-rows", string(len(dom.pointer_samples(evt))))
    return 'handled'
}
<html <head <style "body{margin:0;background:#fff}#pad{position:absolute;left:20px;top:20px;width:200px;height:120px;background:#eee}">>
    apply(<pen_probe rendered:<body id:"fixture", <div id:"pad">>>)
>
