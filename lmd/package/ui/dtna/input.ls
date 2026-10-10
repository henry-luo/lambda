import dom
import tree: lambda.dom.tree
import c: lambda.ui.core.component

fn control_attrs(node) => {*:c.attrs(node.props), class:c.classes(node.kind, node.props) ++ (if (node.props.danger) " dtna-danger" else "") ++
    (if (node.props.shape == null) "" else " dtna-shape-" ++ c.text(node.props.shape)) ++ (if (node.props.block) " dtna-block" else ""), style:node.props.style,
    *:c.boolean_attr("disabled", node.props.disabled), *:(if (node.props.label == null) {} else {'aria-label':node.props.label})}

// enum symbols are serialized at the HTML boundary, as native getters read text attributes.
fn native_input(node) => <input *:control_attrs(node), type:c.text(c.option(node.props, "type", "text")),
    value:c.option(node.props, "value", c.option(node.props, "default_value", "")),
    placeholder:node.props.placeholder, name:node.props.name,
    *:c.boolean_attr("readonly", node.props.readonly), *:c.boolean_attr("required", node.props.required),
    maxlength:node.props.maxlength, autocomplete:node.props.autocomplete>

pn notify_change(node, evt, action, requested) {
    // settle after the application handles its request; the frame follows an existing DOM replacement.
    if (c.has(node.props,"value") or c.has(node.props,"checked") or node.kind == 'radio') {
        dom.request_frame(evt.target,"dtna_control_sync")
    }
    emit("ui_change", c.action(node,action,requested))
}
pn sync_control(node, target) {
    if (node.kind == 'radio') {
        // UA owns exclusivity; reassert only application-controlled peers in that same native group.
        for (peer in tree.radio_group(target) where dom.has_attribute(peer,"data-dtna-controlled-checked")) {
            dom.set_state(peer,"checked",dom.get_attribute(peer,"data-dtna-controlled-checked") == "true")
        }
    } else if (c.has(node.props,"checked")) { dom.set_state(target,"checked",node.props.checked) }
    else if (c.has(node.props,"value") and node.kind == 'select') {
        let matches = [for (index,option in node.props.options where option.value == node.props.value) index]
        let fallback = [for (index,option in node.props.options where not option.disabled) index]
        dom.set_selected_index(target,if (len(matches) > 0) matches[0] else if (len(fallback) > 0) fallback[0] else -1)
    } else if (c.has(node.props,"value") and dom.get_state(target,"value") != c.text(node.props.value)) {
        // an accepted edit already has the right native value and selection; leave both intact.
        dom.set_state(target,"value",c.text(node.props.value))
    }
}

view dtna_input: <dtna kind:'input'> { native_input(~) }
on input(evt) { notify_change(~,evt,'input',dom.get_state(evt.target,"value")) }
on change(evt) { emit("ui_action", c.action(~, 'change', dom.get_state(evt.target, "value"))) }
on dtna_control_sync(evt) { sync_control(~,evt.target) }

view dtna_textarea: <dtna kind:'text-area'> {
    <textarea *:control_attrs(~), placeholder:~.props.placeholder, name:~.props.name,
        *:c.boolean_attr("readonly", ~.props.readonly), *:c.boolean_attr("required", ~.props.required),
        rows:c.option(~.props, "rows", 3), maxlength:~.props.maxlength,
        c.option(~.props, "value", c.option(~.props, "default_value", ""))>
}
on input(evt) { notify_change(~,evt,'input',dom.get_state(evt.target,"value")) }
on dtna_control_sync(evt) { sync_control(~,evt.target) }

view dtna_select: <dtna kind:'select'> {
    let chosen = c.option(~.props, "value", ~.props.default_value);
    <select *:control_attrs(~), name:~.props.name, *:c.boolean_attr("required", ~.props.required),
        *[for (option in ~.props.options) <option value:c.text(option.value),
            *:c.boolean_attr("disabled", option.disabled), *:c.boolean_attr("selected", option.value == chosen), c.text(option.label)>]>
}
on change(evt) {
    let raw = dom.get_state(evt.target, "value")
    let selected = [for (option in ~.props.options where c.text(option.value) == raw) option.value]
    notify_change(~,evt,'select',selected[0])
}
on dtna_control_sync(evt) { sync_control(~,evt.target) }

fn check_control(node, control_kind) => <label class:c.classes(control_kind, node.props), style:node.props.style,
    <input *:c.attrs(node.props), type:control_kind, name:node.props.name, value:node.props.value,
        *:c.boolean_attr("disabled", node.props.disabled), *:c.boolean_attr("checked", c.option(node.props, "checked", node.props.default_checked)),
        *:(if (control_kind == "radio" and c.has(node.props,"checked")) {['data-dtna-controlled-checked']:c.aria(node.props.checked)} else {}),
        *:(if (node.props.label == null) {} else {'aria-label':node.props.label})>
    <span *c.contents(node)>>

view dtna_checkbox: <dtna kind:'checkbox'> { check_control(~, "checkbox") }
on change(evt) { notify_change(~,evt,'check',dom.get_state(evt.target,"checked")) }
on dtna_control_sync(evt) { sync_control(~,evt.target) }
view dtna_radio: <dtna kind:'radio'> { check_control(~, "radio") }
on change(evt) { notify_change(~,evt,'select',~.props.value) }
on dtna_control_sync(evt) { sync_control(~,evt.target) }

view dtna_switch: <dtna kind:'switch'> {
    <label class:c.classes('switch', ~.props), style:~.props.style,
        <input *:c.attrs(~.props), type:"checkbox", role:"switch", *:c.boolean_attr("disabled", ~.props.disabled),
            *:c.boolean_attr("checked", c.option(~.props, "checked", ~.props.default_checked)), *:(if (~.props.label == null) {} else {'aria-label':~.props.label})>
        <span class:"dtna-switch-track", ["aria-hidden"]:"true">>
}
on change(evt) { notify_change(~,evt,'check',dom.get_state(evt.target,"checked")) }
on dtna_control_sync(evt) { sync_control(~,evt.target) }
