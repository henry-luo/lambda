// Value-stepping policy for <input type=range> and <input type=number>
// (ESO58, ES30).
//
// The same seam as scroll.ls and caret.ls: the package names an operation, and
// native owns HTML's value algorithm behind it — the step base, step
// resolution, min/max clamping, the value-state sanitizer, and the normalized
// position the slider paints from. No arithmetic on the value happens here, so
// a `step` of 0.1 or a `min` of -5 does not need a second implementation to
// agree with `valueAsNumber` and `stepUp()`.
import dom

// A slider takes both axes plus the paging keys. A spinner takes only Up/Down:
// Left/Right and Home/End move the caret inside a number field's text, which is
// why HTML gives the two controls different key sets rather than one.
fn slider_operation(key) {
    if (key == "ArrowUp" or key == "ArrowRight") { "stepUp" }
    else if (key == "ArrowDown" or key == "ArrowLeft") { "stepDown" }
    else if (key == "PageUp") { "pageUp" }
    else if (key == "PageDown") { "pageDown" }
    else if (key == "Home") { "toMinimum" }
    else if (key == "End") { "toMaximum" }
    else { null }
}

fn spinner_operation(key) {
    if (key == "ArrowUp") { "stepUp" }
    else if (key == "ArrowDown") { "stepDown" }
    else { null }
}

// Any accelerator or Shift combination belongs to selection, to platform
// navigation, or to the author — never to a value step.
fn operation_for(evt, slider) {
    if (evt.shiftKey or evt.altKey or evt.ctrlKey or evt.metaKey) { null }
    else if (slider) { slider_operation(evt.key) }
    else { spinner_operation(evt.key) }
}

// HTML: a value changed by user interaction reports `input`, then `change`.
// input_operation answers false when the value did not actually move — a slider
// already at its bound — so holding an arrow key down does not emit a pair of
// events per repeat for a thumb that is not going anywhere.
pub pn apply_operation(elem, operation) {
    if (operation == null) { 'pass' }
    else {
        if (dom.input_operation(elem, operation)) {
            dom.dispatch(elem, { type: "input", bubbles: true, cancelable: false })
            dom.dispatch(elem, { type: "change", bubbles: true, cancelable: false })
        }
        // Consumed either way. An arrow on a focused slider must not fall
        // through to scroll.ls just because the thumb was already at the end of
        // its track; no browser scrolls the page in that case.
        'prevent-default'
    }
}

pub pn key_default(elem, evt, slider) {
    if (dom.get_state(elem, "disabled")) { 'pass' }
    else { apply_operation(elem, operation_for(evt, slider)) }
}

// The pointer half of a slider. Native resolves which value the point selects —
// the inverse of the thumb placement render_range draws, so the thumb size stays
// a render constant rather than becoming a number copied into Lambda — and
// commits it. The decision here is only that a press on the track moves the
// thumb, and that a captured move keeps moving it.
//
// It claims without preventing: the press must still perform its focus default,
// or a slider could never take keyboard focus by being clicked.
//
// Three answers, not two. `'pass'` declines; `true` claims and reports that the
// value moved; `false` claims a press that landed on the value already there.
// The caller needs that third case to decide whether the gesture owes a
// `change` when it ends — a click that moves nothing fires neither event.
pub pn point_to_value(elem, evt) {
    if (dom.get_state(elem, "disabled")) { 'pass' }
    else {
        let moved = dom.set_range_from_point(elem, evt.x, evt.y);
        if (moved) { dom.dispatch(elem, { type: "input", bubbles: true, cancelable: false }) }
        moved
    }
}
