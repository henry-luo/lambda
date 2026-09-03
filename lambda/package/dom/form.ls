// UA default behavior for HTML form controls.
// See vibe/Lambda_Design_DOM_State.md — these templates own the state
// transitions and default actions that used to live in radiant/event.cpp.
// The engine owns the storage; every write goes through the waist primitives.
import dom
import tree: lambda.package.dom.tree
import validate: lambda.package.dom.validate
import editing: lambda.package.dom.editing
import aria: lambda.package.dom.aria
import ime: lambda.package.dom.ime
import menu: lambda.package.dom.menu
import caret: lambda.package.dom.caret
import keymap: lambda.package.dom.keymap
import scroll: lambda.package.dom.scroll
import focus: lambda.package.dom.focus
import dom_edit: lambda.package.dom.dom_edit
import commands: lambda.package.dom.commands
import submit: lambda.package.dom.submit
import details: lambda.package.dom.details

// Checkbox activation: a click flips checkedness unless the control is
// disabled, and clears the indeterminate bit (HTML 4.10.5.1.15).
view <input type:'checkbox'> state checked, indeterminate {}
on init(evt) { aria.reflect(~) }
on click(evt) {
    // decline rather than claim: a disabled control has no activation
    // behavior, so the event falls through as unhandled
    if (dom.get_state(~, "disabled")) { return 'pass' }
    dom.set_state(~, "checked", not dom.get_state(~, "checked"))
    dom.set_state(~, "indeterminate", false)
    // the control's own state has settled; notify listeners (HTML 4.10.5)
    // The event spelling, rather than the bare name: `input` and `change` both
    // bubble and are not cancelable (HTML 4.10.5), which is exactly what the
    // name form assumes -- so this is the same dispatch, said explicitly, and
    // it is what gives the event-valued path its coverage in the UI fixtures.
    dom.dispatch(~, { type: "input", bubbles: true, cancelable: false })
    dom.dispatch(~, { type: "change", bubbles: true, cancelable: false })
}

// Radio activation (HTML 4.10.5.1.16). Selecting is one-way — clicking an
// already-checked radio does nothing — and it must clear the previously
// selected peer in the same group, so this template owns the exclusivity walk
// too. Claiming activation without it would silently drop the deselection half.
view <input type:'radio'> state checked {}
on init(evt) { aria.reflect(~) }
on click(evt) {
    if (dom.get_state(~, "disabled")) { return 'pass' }
    if (dom.get_state(~, "checked")) { return 'pass' }
    for (peer in tree.radio_group(~)) {
        dom.set_state(peer, "checked", false)
    }
    dom.set_state(~, "checked", true)
    dom.dispatch(~, "input")
    dom.dispatch(~, "change")
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
// One commit path for the pointer, Enter, and the harness's select_option.
pn commit_option(elem, index) {
    dom.set_selected_index(elem, index)
    dom.set_dropdown_open(elem, false)
}

view <select> state dropdown_open {}
on init(evt) { aria.reflect(~) }
on click(evt) {
    if (dom.get_state(~, "disabled")) { return 'pass' }
    dom.set_dropdown_open(~, not dom.dropdown_open(~))
}
// F2b: the commit half. The dropdown overlay is not a DOM element, so native
// resolves which option the pointer hit from the popup geometry and hands the
// index over on a behavior-only `optioncommit`; choosing and closing are the
// template's, so one interaction is no longer split between the two sides.
on optioncommit(evt) {
    if (evt.option_index == null) { return 'pass' }
    commit_option(~, evt.option_index)
}
// F11: the keys an open dropdown responds to. Enter reaches the same commit the
// pointer does — not a second copy of it — which is the point of moving the
// other three keys here rather than leaving them native alongside it.
on dropdownkey(evt) {
    let count = dom.option_count(~);
    let hover = dom.hover_index(~);
    if (evt.key == "ArrowUp") { if (hover > 0) dom.set_hover_index(~, hover - 1) else true }
    else if (evt.key == "ArrowDown") { if (hover < count - 1) dom.set_hover_index(~, hover + 1) else true }
    else if (evt.key == "Enter") {
        if (hover >= 0 and hover < count) { commit_option(~, hover) } else { true }
    }
    else if (evt.key == "Escape") { dom.set_dropdown_open(~, false) }
    else { 'pass' }
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

// Form activation is behavior-only: the ordinary click remains the author
// event, while submit/reset policy runs after click cancellation is settled.
view <form> state form_activation {}
on submitactivation(evt) { submit.run(~, null) }

view <button> state form_activation {}
// Popover activation is a click default, so synthetic and trusted clicks use
// this one behavior instead of the retired JS-only activation hook.
on click(evt) {
    if (dom.activate_popover(~)) { true } else { 'pass' }
}
on submitactivation(evt) {
    let kind = dom.get_attribute(~, "type");
    let normalized = if (kind == null or kind == "") "submit" else lower(kind);
    if (normalized == "button" or normalized == "reset") { true }
    submit.run(tree.form_of(~), ~)
}
on resetactivation(evt) {
    let kind = dom.get_attribute(~, "type");
    if (kind != null and lower(kind) == "reset") {
        submit.reset(tree.form_of(~))
    }
    else { true }
}

view <input type:'submit'> state form_activation {}
on submitactivation(evt) { submit.run(tree.form_of(~), ~) }

view <input type:'image'> state form_activation {}
on submitactivation(evt) { submit.run(tree.form_of(~), ~) }

view <input type:'reset'> state form_activation {}
on resetactivation(evt) { submit.reset(tree.form_of(~)) }

// The composition session, bound to the page rather than to a control (ES18).
// Composition events bubble from the focused control, so the ancestor walk
// reaches <body> and this template claims them there.
view <body> state ime_composing, context_menu_open {}
// F10: the context menu is document-scoped state, the same cardinality argument
// ES18 made for the IME session — one menu per document, not one per control.
on contextmenu(evt) { menu.open_for(~) }
// F9: keyboard caret navigation. Document-scoped for the same reason — one
// caret per document, not one per control.
on caretkey(evt) { caret.navigate(~, evt) }
// F11: key -> edit intent, one rule set for both surfaces.
on keyintent(evt) { keymap.resolve(~, evt) }
// ESO48: runs only after keydown, caret, and activation have all declined.
on scrollkey(evt) { scroll.navigate(~, evt) }
// ES30: Tab order belongs to the package; native sends focus events and applies
// the scroll request after this policy handler chooses the target.
on focuskey(evt) { focus.navigate(~, evt) }
// F13: editing a plain contenteditable, the DOM twin of the text-control applier.
on domedit(evt) { dom_edit.apply_fn(~, evt) }
// F14.1: the legacy command surface. Behavior-only — `document.execCommand` is
// a method call, not an event — and document-scoped for the same reason the IME
// session is: it addresses whatever the one selection currently covers.
on execcommand(evt) { commands.exec(~, evt) }
on compositionstart(evt)  { ime.begin(~) }
on compositionupdate(evt) { ime.update(~, evt, null) }
on compositionend(evt)    { ime.end(~) }
