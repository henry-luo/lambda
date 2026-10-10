// native control ownership is shared across style families (D7.2.5; S8.2.2v5).
import dom
import tree: lambda.dom.tree
import c: lambda.ui.core.component

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
    emit("ui_change", c.action(node,action,requested))
}
pub pn sync(node, target) {
    let p = c.props(node);
    if (c.kind(node) == 'radio') {
        // UA owns exclusivity; reassert only application-controlled peers in that same native group.
        for (peer in tree.radio_group(target) where dom.has_attribute(peer,"data-ui-controlled-checked")) {
            dom.set_state(peer,"checked",dom.get_attribute(peer,"data-ui-controlled-checked") == "true")
        }
    } else if (c.has(p,"checked")) { dom.set_state(target,"checked",p.checked) }
    else if (c.has(p,"value") and c.kind(node) == 'select') {
        let matches = [for (index,option in p.options where option.value == p.value) index]
        let fallback = [for (index,option in p.options where not option.disabled) index]
        dom.set_selected_index(target,if (len(matches) > 0) matches[0] else if (len(fallback) > 0) fallback[0] else -1)
    } else if (c.has(p,"value") and dom.get_state(target,"value") != c.text(p.value)) {
        // an accepted edit already has the right native value and selection; leave both intact.
        dom.set_state(target,"value",c.text(p.value))
    }
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
            *:c.boolean_attr("disabled", option.disabled), *:c.boolean_attr("selected", option.value == chosen), c.text(option.label)>]>
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
