import dom
import c: lambda.ui.core.component

view dtna_form: <dtna kind:'form'> {
    <form *:c.attrs(~.props), class:c.classes('form',~.props), style:~.props.style, *c.contents(~)>
}
on submit(evt) {
    // UA validation and submitter policy run before this cancelable event.
    emit("ui_action",c.action(~,'submit',dom.form_entries(evt.target,evt.submitter)))
    'prevent-default'
}
on reset(evt) { emit("ui_action",c.action(~,'reset')); 'pass' }

view dtna_form_item: <dtna kind:'form-item'> {
    <div *:c.attrs(~.props), class:c.classes('form-item',~.props), style:~.props.style,
        *[if (~.props.label != null) <label class:"dtna-form-label", for:~.props.for,
            *[if (~.props.required) <span class:"dtna-required",["aria-hidden"]:"true","*"> else null,c.render(~.props.label)]> else null,
        <div class:"dtna-form-control", *c.contents(~)>,
        if (~.props.help != null) <div class:"dtna-form-help", role:if (~.props.status == 'error') "alert" else null,c.render(~.props.help)> else null]>
}
