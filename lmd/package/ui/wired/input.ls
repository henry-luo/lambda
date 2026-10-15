import c: lambda.ui.core.component
import control: lambda.ui.core.control
import contract: .contract
import sketch: .sketch

view wired_controls: <wired.input> | <wired.text_area> | <wired.select> | <wired.checkbox> | <wired.radio> | <wired.switch> {
    let node = contract.checked(~)^
    let p = c.props(node)
    let kind = c.kind(node);
    if (kind == 'input' or kind == 'text-area' or kind == 'select')
        // the wrapper owns geometry; the native field retains identity and input state.
        <span class:"wired-field" ++ (if (p.disabled) " wired-field-disabled" else ""),style:p.style,
            *[control.present(contract.node(kind,{*:p,style:""})^,"wired"),sketch.outline(node)]>
    else control.present(node,"wired")
}
on input(evt) { control.edited(~,evt,"wired") }
on change(evt) { control.changed(~,evt,"wired") }
on render(evt) { control.rendered(~,evt) }
on wired_control_sync(evt) { control.sync(~,evt.target) }
