import dom
import c: lambda.ui.core.component
import general: .general

fn control_attrs(node) => {*:c.attrs(node.props), class:c.classes(node.kind, node.props) ++ (if (node.props.danger) " dtna-danger" else "") ++
    (if (node.props.shape == null) "" else " dtna-shape-" ++ c.text(node.props.shape)) ++ (if (node.props.block) " dtna-block" else ""), style:node.props.style,
    *:c.boolean_attr("disabled", node.props.disabled), *:(if (node.props.label == null) {} else {'aria-label':node.props.label})}

view dtna_button: <dtna kind:'button'> {
    <button *:control_attrs(~), type:c.option(~.props, "type", "button"),
        ["aria-busy"]:c.aria(~.props.loading), *:c.boolean_attr("disabled", ~.props.disabled or ~.props.loading),
        *[if (~.props.loading) general.icon({name:"loading"})^ else c.render(~.props.icon), *c.contents(~)]>
}
on click(evt) {
    if (~.props.disabled or ~.props.loading) { return 'prevent-default' }
    emit("ui_action", c.action(~, 'click'))
    'pass'
}

fn native_input(node) => <input *:control_attrs(node), type:c.option(node.props, "type", "text"),
    value:c.option(node.props, "value", c.option(node.props, "default_value", "")),
    placeholder:node.props.placeholder, name:node.props.name,
    *:c.boolean_attr("readonly", node.props.readonly), *:c.boolean_attr("required", node.props.required),
    maxlength:node.props.maxlength, autocomplete:node.props.autocomplete>

view dtna_input: <dtna kind:'input'> { native_input(~) }
on input(evt) { emit("ui_change", c.action(~, 'input', dom.get_state(evt.target, "value"))) }
on change(evt) { emit("ui_action", c.action(~, 'change', dom.get_state(evt.target, "value"))) }

view dtna_textarea: <dtna kind:'text-area'> {
    <textarea *:control_attrs(~), placeholder:~.props.placeholder, name:~.props.name,
        *:c.boolean_attr("readonly", ~.props.readonly), *:c.boolean_attr("required", ~.props.required),
        rows:c.option(~.props, "rows", 3), maxlength:~.props.maxlength,
        c.option(~.props, "value", c.option(~.props, "default_value", ""))>
}
on input(evt) { emit("ui_change", c.action(~, 'input', dom.get_state(evt.target, "value"))) }

view dtna_select: <dtna kind:'select'> {
    let chosen = c.option(~.props, "value", ~.props.default_value);
    <select *:control_attrs(~), name:~.props.name, *:c.boolean_attr("required", ~.props.required),
        *[for (option in ~.props.options) <option value:c.text(option.value),
            *:c.boolean_attr("disabled", option.disabled), *:c.boolean_attr("selected", option.value == chosen), c.text(option.label)>]>
}
on change(evt) {
    let raw = dom.get_state(evt.target, "value")
    let selected = [for (option in ~.props.options where c.text(option.value) == raw) option.value]
    emit("ui_change", c.action(~, 'select', selected[0]))
}

fn check_control(node, control_kind) => <label class:c.classes(control_kind, node.props), style:node.props.style,
    <input *:c.attrs(node.props), type:control_kind, name:node.props.name, value:node.props.value,
        *:c.boolean_attr("disabled", node.props.disabled), *:c.boolean_attr("checked", c.option(node.props, "checked", node.props.default_checked)),
        *:(if (node.props.label == null) {} else {'aria-label':node.props.label})>
    <span *c.contents(node)>>

view dtna_checkbox: <dtna kind:'checkbox'> { check_control(~, "checkbox") }
on change(evt) { emit("ui_change", c.action(~, 'check', dom.get_state(evt.target, "checked"))) }
view dtna_radio: <dtna kind:'radio'> { check_control(~, "radio") }
on change(evt) { emit("ui_change", c.action(~, 'select', ~.props.value)) }

view dtna_switch: <dtna kind:'switch'> {
    <label class:c.classes('switch', ~.props), style:~.props.style,
        <input *:c.attrs(~.props), type:"checkbox", role:"switch", *:c.boolean_attr("disabled", ~.props.disabled),
            *:c.boolean_attr("checked", c.option(~.props, "checked", ~.props.default_checked)), *:(if (~.props.label == null) {} else {'aria-label':~.props.label})>
        <span class:"dtna-switch-track", ["aria-hidden"]:"true">>
}
on change(evt) { emit("ui_change", c.action(~, 'check', dom.get_state(evt.target, "checked"))) }
