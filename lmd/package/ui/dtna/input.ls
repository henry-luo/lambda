import dom
import c: lambda.ui.core.component
import control: lambda.ui.core.control

view dtna_input: <dtna.input> { control.input(~) }
on input(evt) { control.notify_change(~,evt,'input',dom.get_state(evt.target,"value")) }
on change(evt) { emit("ui_action", c.action(~, 'change', dom.get_state(evt.target, "value"))) }
on dtna_control_sync(evt) { control.sync(~,evt.target) }

view dtna_textarea: <dtna.text_area> {
    control.text_area(~)
}
on input(evt) { control.notify_change(~,evt,'input',dom.get_state(evt.target,"value")) }
on dtna_control_sync(evt) { control.sync(~,evt.target) }

view dtna_select: <dtna.select> {
    control.select(~)
}
on change(evt) {
    control.select_change(~,evt)
}
on dtna_control_sync(evt) { control.sync(~,evt.target) }

view dtna_checkbox: <dtna.checkbox> { control.check(~, "checkbox") }
on change(evt) { control.notify_change(~,evt,'check',dom.get_state(evt.target,"checked")) }
on dtna_control_sync(evt) { control.sync(~,evt.target) }
view dtna_radio: <dtna.radio> { control.check(~, "radio") }
on change(evt) { control.notify_change(~,evt,'select',c.props(~).value) }
on dtna_control_sync(evt) { control.sync(~,evt.target) }

view dtna_switch: <dtna.switch> {
    control.switch(~)
}
on change(evt) { control.notify_change(~,evt,'check',dom.get_state(evt.target,"checked")) }
on dtna_control_sync(evt) { control.sync(~,evt.target) }
