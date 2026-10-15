import control: lambda.ui.core.control

view dtna_controls: <dtna.input> | <dtna.text_area> | <dtna.select> | <dtna.checkbox> | <dtna.radio> | <dtna.switch> {
    control.present(~,"dtna")
}
on input(evt) { control.edited(~,evt,"dtna") }
on change(evt) { control.changed(~,evt,"dtna") }
on render(evt) { control.rendered(~,evt) }
on dtna_control_sync(evt) { control.sync(~,evt.target) }
