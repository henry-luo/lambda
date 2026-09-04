// S12.1.3: shared keyboard-activation policy. Native constructs the
// KeyboardEvent and dispatches the requested click; this module alone maps
// keys and timing.
import dom

pub fn action(evt, enter_activates, space_timing) {
    if (evt.ctrlKey or evt.altKey or evt.metaKey) { "pass" }
    else if (enter_activates and evt.key == "Enter") { "click" }
    else if (space_timing == "down" and evt.key == " ") { "click" }
    else if (space_timing == "up" and evt.key == " ") { "arm" }
    else { "pass" }
}

pub pn click(elem) {
    if (dom.keyboard_click(elem)) { 'prevent-default' } else { 'pass' }
}
