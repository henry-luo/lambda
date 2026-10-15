// native control ownership is shared across style families (D7.2.5; S8.2.2v5).
import dom
import tree: lambda.dom.tree
import c: lambda.ui.core.component
import binding: .binding

pub fn attrs(node, family = "dtna") {
    let p = c.props(node);
    {*:c.attrs(p), class:c.classes(c.kind(node), p, "", family) ++ (if (p.danger) " " ++ family ++ "-danger" else "") ++
    (if (p.shape == null) "" else " " ++ family ++ "-shape-" ++ c.text(p.shape)) ++ (if (p.block) " " ++ family ++ "-block" else ""), style:p.style,
    *:c.boolean_attr("disabled", p.disabled), *:(if (p.label == null) {} else {'aria-label':p.label})}
}

// enum symbols are serialized at the HTML boundary, as native getters read text attributes.
pub fn input(node, family = "dtna") {
    let p = c.props(node);
    <input *:attrs(node,family), type:c.text(c.option(p, "type", "text")),
    value:c.option(p, "value", c.option(p, "default_value", "")),
    placeholder:p.placeholder, name:p.name,
    *:c.boolean_attr("readonly", p.readonly), *:c.boolean_attr("required", p.required),
    maxlength:p.maxlength, autocomplete:p.autocomplete>
}

pub pn notify_change(node, evt, action, requested, family = "dtna") {
    let p = c.props(node);
    // settle after the application handles its request; the frame follows an existing DOM replacement.
    if (c.has(p,"value") or c.has(p,"checked") or c.kind(node) == 'radio') {
        dom.request_frame(evt.target,family ++ "_control_sync")
    }
    if (p.on_change == null) { emit("ui_change", c.action(node,action,requested)) }
    else {
        emit("control_change", {*:binding.request(p.on_change,p.id,requested),node:evt.target,event:evt})
    }
}
pub pn sync(node, target) {
    let p = c.props(node);
    // render events can target a checkbox label or a skin's wrapper.
    let field = if (dom.matches(target,"input,textarea,select")) target
                else dom.query_selector(target,"input,textarea,select")
    if (field == null) { return }
    // preedit belongs to the native composition session, including an empty
    // active composition. Reassert the accepted value only after it commits.
    if (dom.ime_preedit(field) != null) { return }
    if (c.kind(node) == 'radio') {
        // UA owns exclusivity; reassert only application-controlled peers in that same native group.
        for (peer in tree.radio_group(field) where dom.has_attribute(peer,"data-ui-controlled-checked")) {
            let checked = dom.get_attribute(peer,"data-ui-controlled-checked") == "true"
            if (dom.get_state(peer,"checked") != checked) dom.set_state(peer,"checked",checked)
        }
    } else if (c.has(p,"checked")) {
        if (dom.get_state(field,"checked") != p.checked) dom.set_state(field,"checked",p.checked)
    }
    else if (c.has(p,"value") and c.kind(node) == 'select') {
        let matches = [for (index,option in p.options where c.text(option.value) == c.text(p.value)) index]
        let fallback = [for (index,option in p.options where not option.disabled) index]
        let selected = if (len(matches) > 0) matches[0] else if (len(fallback) > 0) fallback[0] else -1
        if (dom.selected_index(field) != selected) dom.set_selected_index(field,selected)
    } else if (c.has(p,"value") and dom.get_state(field,"value") != c.text(p.value)) {
        // an accepted edit already has the right native value and selection; leave both intact.
        dom.set_state(field,"value",c.text(p.value))
    }
}

pub pn rendered(node, evt) {
    let p = c.props(node)
    // a parent may change the controlled value without a native input event.
    if (evt.event_phase == 2 and (c.has(p,"value") or c.has(p,"checked"))) sync(node,evt.target)
    'pass'
}

pub fn text_area(node, family = "dtna") {
    let p = c.props(node);
    <textarea *:attrs(node,family), placeholder:p.placeholder, name:p.name,
        *:c.boolean_attr("readonly", p.readonly), *:c.boolean_attr("required", p.required),
        rows:c.option(p, "rows", 3), maxlength:p.maxlength,
        c.option(p, "value", c.option(p, "default_value", ""))>
}

pub fn select(node, family = "dtna") {
    let p = c.props(node);
    let chosen = c.option(p, "value", p.default_value);
    <select *:attrs(node,family), name:p.name, *:c.boolean_attr("required", p.required),
        *[for (option in p.options) <option value:c.text(option.value),
            *:c.boolean_attr("disabled", option.disabled), *:c.boolean_attr("selected", c.text(option.value) == c.text(chosen)), c.text(option.label)>]>
}

pub fn check(node, control_kind, family = "dtna") {
    let p = c.props(node);
    <label class:c.classes(control_kind, p, "", family), style:p.style,
    <input *:c.attrs(p), type:control_kind, name:p.name, value:p.value,
        *:c.boolean_attr("disabled", p.disabled), *:c.boolean_attr("checked", c.option(p, "checked", p.default_checked)),
        *:(if (control_kind == "radio" and c.has(p,"checked")) {['data-ui-controlled-checked']:c.aria(p.checked)} else {}),
        *:(if (p.label == null) {} else {'aria-label':p.label})>
    <span *c.contents(node)>>

}
pub fn switch(node, family = "dtna") {
    let p = c.props(node);
    <label class:c.classes('switch', p, "", family), style:p.style,
        <input *:c.attrs(p), type:"checkbox", role:"switch", *:c.boolean_attr("disabled", p.disabled),
            *:c.boolean_attr("checked", c.option(p, "checked", p.default_checked)), *:(if (p.label == null) {} else {'aria-label':p.label})>
        <span class:family ++ "-switch-track", ["aria-hidden"]:"true">>
}

pub pn select_change(node, evt, family = "dtna") {
    let p = c.props(node);
    let raw = dom.get_state(evt.target, "value")
    let selected = [for (option in p.options where c.text(option.value) == raw) option.value];
    notify_change(node,evt,'select',selected[0],family)
}

// skins register one native-control view and delegate the event policy here.
pub fn present(node, family = "dtna") {
    let kind = c.kind(node);
    if (kind == 'input') input(node,family)
    else if (kind == 'text-area') text_area(node,family)
    else if (kind == 'select') select(node,family)
    else if (kind == 'switch') switch(node,family)
    else check(node,c.text(kind),family)
}
pub pn edited(node, evt, family = "dtna") {
    if (evt.isComposing == true) { return }
    if (c.kind(node) == 'input' or c.kind(node) == 'text-area') {
        notify_change(node,evt,'input',dom.get_state(evt.target,"value"),family)
    }
}
pub pn changed(node, evt, family = "dtna") {
    let kind = c.kind(node);
    if (kind == 'select') { select_change(node,evt,family) }
    else if (kind == 'radio') { notify_change(node,evt,'select',c.props(node).value,family) }
    else if (kind == 'checkbox' or kind == 'switch') { notify_change(node,evt,'check',dom.get_state(evt.target,"checked"),family) }
    else if (kind == 'input') { emit("ui_action",c.action(node,'change',dom.get_state(evt.target,"value"))) }
}
