// Scroll default-action policy (ESO48, ES33).
//
// The package decides whether an unclaimed input means a line, page, boundary,
// wheel, or scrollbar action. Native owns the live scroll chain, hit geometry,
// range clamp, drag conversion, scroll events, observers, and paint; policy
// never receives a mutable scroll offset.
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

// The public WheelEvent has already had its cancellation chance. A zero delta
// is not a scroll gesture; every non-zero physical delta is handed back to the
// native scroll chain through one semantic request.
pub pn wheel(evt) {
    if (evt.deltaX == 0 and evt.deltaY == 0) { 'pass' }
    else { dom.scroll_operation(evt.target, "wheel") }
}

// Native has classified the pointer against the painted scrollbar geometry.
// This is the entire scrollbar decision table: the package picks semantic
// paging versus thumb dragging, while native supplies dimensions, clamping,
// state storage, and the hot drag loop.
fn scrollbar_operation(part) {
    if (part == "horizontalBefore") { "pageLeft" }
    else if (part == "horizontalThumb") { "drag" }
    else if (part == "horizontalAfter") { "pageRight" }
    else if (part == "verticalBefore") { "pageBackward" }
    else if (part == "verticalThumb") { "drag" }
    else if (part == "verticalAfter") { "pageForward" }
    else { null }
}

pub pn scrollbar_press(evt) {
    if (evt.button != 0) { 'pass' }
    else {
        let op = scrollbar_operation(evt.scrollbarPart);
        if (op == null) { 'pass' }
        else { dom.scroll_operation(evt.target, op) }
    }
}
