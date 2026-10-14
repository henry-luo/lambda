import dom
import c: lambda.ui.core.component
import surface: lambda.ui.core.surface
import contract: .contract
import theme: .tokens
import sketch: .sketch

view wired_button: <wired.button> {
    let node = contract.checked(~)^;
    surface.button(node,"wired",if (c.props(node).variant == 'text') null else sketch.outline(node))
}
on click(evt) {
    if (c.props(~).disabled) { return 'prevent-default' }
    emit("ui_action",c.action(~,'click'))
    'pass'
}

view wired_form: <wired.form> {
    let node = contract.checked(~)^;
    <form *:surface.attributes(node,"wired"),*c.contents(node)>
}
on submit(evt) { emit("ui_action",c.action(~,'submit',dom.form_entries(evt.target,evt.submitter))); 'prevent-default' }
on reset(evt) { emit("ui_action",c.action(~,'reset')); 'pass' }

view wired_presentation: <wired.config_provider> | <wired.flex> | <wired.space> | <wired.title> | <wired.text> | <wired.paragraph> | <wired.link> | <wired.badge> | <wired.card> | <wired.alert> | <wired.divider> | <wired.form_item> | <wired.progress> {
    let node = contract.checked(~)^
    let kind = c.kind(node);
    if (kind == 'divider') <div *:surface.attributes(node,"wired"),role:"separator",*[sketch.outline(node,true)]>
    else surface.present(node,"wired",
        if (kind == 'config-provider') theme.variables(c.option(c.props(node),"tokens",{}),true)^ else "",
        if (contains(['card','badge','alert','progress'],kind)) sketch.outline(node) else null)
}
