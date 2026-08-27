// UA default behavior for HTML form controls.
// See vibe/Lambda_Design_DOM_State.md — these templates own the state
// transitions and default actions that used to live in radiant/event.cpp.
// The engine owns the storage; every write goes through the waist primitives.
import radiant
import validate: lambda.package.dom.validate
import editing: lambda.package.dom.editing
import aria: lambda.package.dom.aria
import ime: lambda.package.dom.ime

// Checkbox activation: a click flips checkedness unless the control is
// disabled, and clears the indeterminate bit (HTML 4.10.5.1.15).
view <input type:'checkbox'> state checked, indeterminate {}
on init(evt) { aria.reflect(~) }
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
on init(evt) { aria.reflect(~) }
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

// Select activation (F2): a click toggles the dropdown. This template owns
// open/close only — option commit and the overlay stay native.
//
// Claiming it needed two engine fixes first. Behavior dispatch runs earlier in
// click handling than native's open did, so the dropdown now exists during
// phases written assuming it could not; the overlay block is gated on the
// dropdown that was open when the click arrived. And the dropdown could outlive
// the FormControlProp backing it, which tripped "open dropdown state disagrees
// with form control" in state_machine.cpp — form_control_prop_release now
// closes a dropdown owning the prop being released, the single point that both
// release paths share (ESO28).
view <select> state dropdown_open {}
on init(evt) { aria.reflect(~) }
on click(evt) {
    if (radiant.get_state(~, "disabled")) { return 'pass' }
    radiant.set_dropdown_open(~, not radiant.dropdown_open(~))
}
// F2b: the commit half. The dropdown overlay is not a DOM element, so native
// resolves which option the pointer hit from the popup geometry and hands the
// index over on a behavior-only `optioncommit`; choosing and closing are the
// template's, so one interaction is no longer split between the two sides.
on optioncommit(evt) {
    if (evt.option_index == null) { return 'pass' }
    radiant.set_selected_index(~, evt.option_index)
    radiant.set_dropdown_open(~, false)
}

// Constraint validation (F3). There is no native validator behind this any
// more: te_validate is gone, and these handlers are the only thing that writes
// :valid/:invalid. Native keeps only the attribute reflection that backs
// :required/:optional/:read-only, which is engine mechanism (DOM_Pkg rules
// reflected IDL attributes N).
//
// One template covers every text-ish input — `text`, `email`, `url`, `number`,
// `password`, and an input whose type attribute is missing or unrecognised,
// which HTML treats as `text`. The type is not a matching concern here: it only
// selects which content check is_valid() applies, so splitting this per type
// would be four copies of one rule. Being the lowest-specificity input template
// it never displaces the checkbox and radio templates above, which declare
// `click` and win it outright; those two match here as well, and revalidate
// gates them out because their `value` attribute is not editable text.
//
// These hook the *post-mutation* `input`, dispatched from
// editing_dispatch_form_input after the buffer commit, so the handler reads the
// value the user actually typed. The pre-mutation `input` — the Reactive_UI
// contract where an app template owns the text — is deliberately not visible to
// behavior templates: it fires before the value exists, and claiming it would
// suppress the engine's own insert.
view <input> state valid, invalid {}
on init(evt)  { validate.revalidate(~) aria.reflect(~) }
on input(evt) { validate.revalidate(~) aria.reflect(~) }
on blur(evt)  { validate.revalidate(~) aria.reflect(~) }
on commit(evt) { editing.commit(~) }
on beforeinput(evt) { editing.apply_fn(~, evt, false) }

view <textarea> state valid, invalid {}
on init(evt)  { validate.revalidate(~) aria.reflect(~) }
on input(evt) { validate.revalidate(~) aria.reflect(~) }
on blur(evt)  { validate.revalidate(~) aria.reflect(~) }
on commit(evt) { editing.commit(~) }
on beforeinput(evt) { editing.apply_fn(~, evt, true) }

// <input type=range> has no activation template yet; it is here for the ARIA
// value mirrors, which are the last thing native's reflection still owned.
view <input type:'range'> state valid, invalid {}
on init(evt)  { aria.reflect_range(~) }
on input(evt) { aria.reflect_range(~) }

// The composition session, bound to the page rather than to a control (ES18).
// Composition events bubble from the focused control, so the ancestor walk
// reaches <body> and this template claims them there.
view <body> state ime_composing {}
on compositionstart(evt)  { ime.begin(~) }
on compositionupdate(evt) { ime.update(~, evt, null) }
on compositionend(evt)    { ime.end(~) }
