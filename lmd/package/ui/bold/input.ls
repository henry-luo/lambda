import control: lambda.ui.core.control
import contract: .contract

view bold_controls: <bold.input> | <bold.text_area> | <bold.select> | <bold.checkbox> | <bold.radio> | <bold.switch> | <bold kind:'input'> | <bold kind:'text-area'> | <bold kind:'select'> | <bold kind:'checkbox'> | <bold kind:'radio'> | <bold kind:'switch'> {
    control.present(contract.checked(~)^,"bold")
}
on input(evt) { control.edited(~,evt,"bold") }
on change(evt) { control.changed(~,evt,"bold") }
on bold_control_sync(evt) { control.sync(~,evt.target) }
