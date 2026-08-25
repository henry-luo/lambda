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

// Radio activation (HTML 4.10.5.1.16). Selecting is one-way — clicking an
// already-checked radio does nothing — and it must clear the previously
// selected peer in the same group, so this template owns the exclusivity walk
// too. Claiming activation without it would silently drop the deselection half.
view <input type:'radio'> state checked {}
on click(evt) {
    if (radiant.get_state(~, "disabled")) { return 'pass' }
    if (radiant.get_state(~, "checked")) { return 'pass' }
    for (peer in radiant.radio_group(~)) {
        radiant.set_state(peer, "checked", false)
    }
    radiant.set_state(~, "checked", true)
    radiant.dispatch(~, "input")
    radiant.dispatch(~, "change")
}

// Select activation is NOT claimed yet (F2 blocked).
//
// Opening the dropdown from a behavior template is state-inconsistent with the
// native dropdown+focus machinery: after the template opens it, a later
// focus_transition trips the engine's own invariant — "open dropdown state
// disagrees with form control" (state_machine.cpp). Immediately after the open
// the two agree (verified: open_dropdown == view, form bit set), so something
// between the open and that transition desyncs them.
//
// The likely root is ordering. Native opens the dropdown at the very end of
// click handling, after the overlay block has run; behavior dispatch happens
// much earlier in the same handler, so the dropdown exists during phases that
// were written assuming it could not. Gating the overlay block on the
// dropdown that was open when the click arrived was necessary but not
// sufficient.
//
// The primitives this needs — dropdown_open, set_dropdown_open, option_count,
// selected_index, set_selected_index — are implemented and tested; only the
// template is withheld. Claiming activation while the state machine reports an
// inconsistency would repeat the radio mistake from F1.
view <select> state dropdown_open {}
on click(evt) {
    if (radiant.get_state(~, "disabled")) { return 'pass' }
    radiant.set_dropdown_open(~, not radiant.dropdown_open(~))
}
