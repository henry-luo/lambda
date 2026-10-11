import dom
import c: lambda.ui.core.component
import control: lambda.ui.core.control
import contract: .contract

view bold_input: <bold.input> | <bold kind:'input'> { control.input(contract.checked(~)^,"bold") }
on input(evt) { control.notify_change(~,evt,'input',dom.get_state(evt.target,"value"),"bold") }
on change(evt) { emit("ui_action",c.action(~,'change',dom.get_state(evt.target,"value"))) }
on bold_control_sync(evt) { control.sync(~,evt.target) }

view bold_textarea: <bold.text_area> | <bold kind:'text-area'> { control.text_area(contract.checked(~)^,"bold") }
on input(evt) { control.notify_change(~,evt,'input',dom.get_state(evt.target,"value"),"bold") }
on bold_control_sync(evt) { control.sync(~,evt.target) }

view bold_select: <bold.select> | <bold kind:'select'> { control.select(contract.checked(~)^,"bold") }
on change(evt) { control.select_change(~,evt,"bold") }
on bold_control_sync(evt) { control.sync(~,evt.target) }

view bold_checkbox: <bold.checkbox> | <bold kind:'checkbox'> { control.check(contract.checked(~)^,"checkbox","bold") }
on change(evt) { control.notify_change(~,evt,'check',dom.get_state(evt.target,"checked"),"bold") }
on bold_control_sync(evt) { control.sync(~,evt.target) }

view bold_radio: <bold.radio> | <bold kind:'radio'> { control.check(contract.checked(~)^,"radio","bold") }
on change(evt) { control.notify_change(~,evt,'select',c.props(~).value,"bold") }
on bold_control_sync(evt) { control.sync(~,evt.target) }

view bold_switch: <bold.switch> | <bold kind:'switch'> { control.switch(contract.checked(~)^,"bold") }
on change(evt) { control.notify_change(~,evt,'check',dom.get_state(evt.target,"checked"),"bold") }
on bold_control_sync(evt) { control.sync(~,evt.target) }
