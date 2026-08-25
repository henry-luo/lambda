// UA default behavior for HTML form controls.
// See vibe/Lambda_Design_DOM_State.md — these templates own the state
// transitions and default actions that used to live in radiant/event.cpp.
// The engine owns the storage; every write goes through the waist primitives.
import radiant

// Checkbox activation: a click flips checkedness unless the control is
// disabled, and clears the indeterminate bit (HTML 4.10.5.1.15).
view <input type:'checkbox'> state checked, indeterminate {}
on click(evt) {
    // decline rather than claim: a disabled control has no activation
    // behavior, so the event falls through as unhandled
    if (radiant.get_state(~, "disabled")) { return 'pass' }
    radiant.set_state(~, "checked", not radiant.get_state(~, "checked"))
    radiant.set_state(~, "indeterminate", false)
    // the control's own state has settled; notify listeners (HTML 4.10.5)
    radiant.dispatch(~, "input")
    radiant.dispatch(~, "change")
}

// Radio activation: selecting is one-way — clicking a checked radio does not
// clear it. Group exclusivity still lives natively until radio_group() lands.
view <input type:'radio'> state checked {}
on click(evt) {
    if (radiant.get_state(~, "disabled")) { return 'pass' }
    // selecting is one-way; a click on an already-checked radio does nothing
    if (radiant.get_state(~, "checked")) { return 'pass' }
    radiant.set_state(~, "checked", true)
    radiant.dispatch(~, "input")
    radiant.dispatch(~, "change")
}
