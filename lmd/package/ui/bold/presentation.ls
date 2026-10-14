import dom
import c: lambda.ui.core.component
import contract: .contract
import theme: .tokens
import surface: lambda.ui.core.surface

view bold_button: <bold.button> | <bold kind:'button'> {
    surface.button(contract.checked(~)^,"bold")
}

on click(evt) {
    if (c.props(~).disabled) { return 'prevent-default' }
    emit("ui_action",c.action(~,'click'))
    'pass'
}

view bold_form: <bold.form> | <bold kind:'form'> {
    let node = contract.checked(~)^;
    <form *:surface.attributes(node,"bold"),*c.contents(node)>
}
on submit(evt) { emit("ui_action",c.action(~,'submit',dom.form_entries(evt.target,evt.submitter))); 'prevent-default' }
on reset(evt) { emit("ui_action",c.action(~,'reset')); 'pass' }

view bold_presentation: <bold.config_provider> | <bold.flex> | <bold.space> | <bold.title> | <bold.text> | <bold.paragraph> | <bold.link> | <bold.badge> | <bold.card> | <bold.alert> | <bold.divider> | <bold.form_item> | <bold.progress> {
    let node = contract.checked(~)^;
    surface.present(node,"bold",if (c.kind(node) == 'config-provider') theme.variables(c.option(c.props(node),"tokens",{}),true)^ else "")
}
