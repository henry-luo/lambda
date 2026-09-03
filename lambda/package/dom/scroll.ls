// Keyboard document-scroll policy (ESO48, ES30).
//
// The package decides whether an unclaimed key is a line, page, or boundary
// scroll. Native owns the live nearest scrollport, range clamp, scroll event,
// geometry observers, and paint; a policy handler never receives pixels or a
// mutable scroll offset.
import dom

fn operation_for(key, shift, alt, ctrl, meta) {
    // Modified arrows belong to selection and platform navigation policy. A
    // plain Shift+Space is the browser's reverse page-scroll exception.
    if (alt or ctrl or meta) { null }
    else if (key == " ") { if (shift) "pageBackward" else "pageForward" }
    else if (shift) { null }
    else if (key == "ArrowUp") { "lineBackward" }
    else if (key == "ArrowDown") { "lineForward" }
    else if (key == "ArrowLeft") { "lineLeft" }
    else if (key == "ArrowRight") { "lineRight" }
    else if (key == "PageUp") { "pageBackward" }
    else if (key == "PageDown") { "pageForward" }
    else if (key == "Home") { "documentStart" }
    else if (key == "End") { "documentEnd" }
    else { null }
}

pub pn navigate(body, evt) {
    let op = operation_for(evt.key, evt.shift, evt.alt, evt.ctrl, evt.meta);
    if (op == null) { 'pass' }
    else { dom.scroll_operation(body, op) }
}
