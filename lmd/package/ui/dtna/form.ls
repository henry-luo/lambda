import dom
import c: lambda.ui.core.component

view dtna_form: <dtna.form> {
    <form *:c.attrs(c.props(~)), class:c.classes('form',c.props(~)), style:c.props(~).style, *c.contents(~)>
}
on submit(evt) {
    // UA validation and submitter policy run before this cancelable event.
    emit("ui_action",c.action(~,'submit',dom.form_entries(evt.target,evt.submitter)))
    'prevent-default'
}
on reset(evt) { emit("ui_action",c.action(~,'reset')); 'pass' }

view dtna_form_item: <dtna.form_item> {
    <div *:c.attrs(c.props(~)), class:c.classes('form-item',c.props(~)), style:c.props(~).style,
        *[if (c.props(~).label != null) <label class:"dtna-form-label", for:c.props(~).for,
            *[if (c.props(~).required) <span class:"dtna-required",["aria-hidden"]:"true","*"> else null,c.render(c.props(~).label)]> else null,
        <div class:"dtna-form-control", *c.contents(~)>,
        if (c.props(~).help != null) <div class:"dtna-form-help", role:if (c.props(~).status == 'error') "alert" else null,c.render(c.props(~).help)> else null]>
}
