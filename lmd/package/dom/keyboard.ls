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

// The keydown half of Space/Enter activation. Three templates need it — the
// generic <input>, <button>, and radio, which owns its own keydown for the
// arrow-key group navigation — so the rule lives here once rather than in three
// copies that can drift apart on a modifier or on the disabled check.
//
// It answers "arm" when the caller must remember the press for its own keyup;
// the caller stores that on its template instance, because S9.1.4 puts mutable
// state in the view state and not in a native pinned target.
pub pn keydown_activation(elem, evt, enter_activates, space_timing) {
    let chosen = if (dom.get_state(elem, "disabled")) "pass"
                 else action(evt, enter_activates, space_timing);
    if (chosen == "arm") { "arm" }
    else if (chosen == "click") { click(elem) }
    else { 'pass' }
}

// The keyup half. Only a Space that this element's own uncancelled keydown
// armed may complete an activation, so a keyup arriving after focus moved,
// after the author cancelled the keydown, or on a control that has since been
// disabled produces no click.
pub pn keyup_activation(elem, evt, armed) {
    if (armed and evt.key == " " and not dom.get_state(elem, "disabled")) { click(elem) }
    else { 'pass' }
}
