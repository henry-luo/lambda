import dom
import c: lambda.ui.core.component
import contract: .contract
import theme: .tokens

fn attributes(node) {
    let p = c.props(node);
    {*:c.styled(node,"","bold"),*:(if (p.label == null or c.has(p,"aria-label")) {} else {['aria-label']:p.label})}
}
fn link_attrs(props) => {*:map([for (key in ["href","target","rel"] where props[key] != null and (key != "href" or not props.disabled)) (key,props[key])]),
    *:(if (props.target == "_blank" and props.rel == null) {rel:"noopener noreferrer"} else {}),
    *:(if (props.disabled) {tabindex:-1,['aria-disabled']:"true"} else {})}

view bold_button: <bold.button> | <bold kind:'button'> {
    let node = contract.checked(~)^
    let p = c.props(node)
    let attrs = {*:attributes(node),class:c.classes('button',p,if (p.block) "bold-block" else "","bold")};
    if (p.href != null) <a *:attrs,*:link_attrs(p),*c.contents(node)>
    else <button *:attrs,type:c.text(c.option(p,"type","button")),*:c.boolean_attr("disabled",p.disabled),*c.contents(node)>
}
on click(evt) {
    if (c.props(~).disabled) { return 'prevent-default' }
    emit("ui_action",c.action(~,'click'))
    'pass'
}

view bold_form: <bold.form> | <bold kind:'form'> {
    let node = contract.checked(~)^;
    <form *:attributes(node),*c.contents(node)>
}
on submit(evt) { emit("ui_action",c.action(~,'submit',dom.form_entries(evt.target,evt.submitter))); 'prevent-default' }
on reset(evt) { emit("ui_action",c.action(~,'reset')); 'pass' }

fn flex(node) {
    let p = c.props(node)
    let style = "display:flex;flex-direction:" ++ (if (p.direction == 'vertical') "column" else "row") ++ ";gap:" ++
        (if (p.gap == null) "var(--bold-spacing)" else c.length(p.gap)) ++ ";align-items:" ++ c.text(c.option(p,"align","center")) ++
        ";justify-content:" ++ c.text(c.option(p,"justify","flex-start")) ++ ";flex-wrap:" ++ (if (p.wrap) "wrap" else "nowrap") ++ ";";
    <div *:attributes(node),style:c.style_with(p,style),*c.contents(node)>
}
fn card(node) => <section *:attributes(node), *[
    if (c.props(node).title != null or c.props(node).description != null) <header class:"bold-card-header",
        *[if (c.props(node).title != null) <h3 class:"bold-card-title",c.render(c.props(node).title)> else null,
        if (c.props(node).description != null) <p class:"bold-card-description",c.render(c.props(node).description)> else null]> else null,
    <div class:"bold-card-body",*c.contents(node)>,
    if (c.props(node).footer != null) <footer class:"bold-card-footer",*c.children(c.render(c.props(node).footer))> else null]>
fn form_item(node) => <div *:attributes(node), *[
    if (c.props(node).label != null) <label class:"bold-form-label",for:c.props(node).for,
        *[c.props(node).label,if (c.props(node).required) <span class:"bold-required",["aria-hidden"]:"true"," *"> else null]> else null,
    <div class:"bold-form-control",*c.contents(node)>,
    if (c.props(node).help != null) <div class:"bold-form-help",role:if (c.props(node).status == 'error') "alert" else null,c.props(node).help> else null]>
fn progress(node) {
    let percent = c.option(c.props(node),"percent",0);
    <div *:attributes(node),role:"progressbar",["aria-valuemin"]:"0",["aria-valuemax"]:"100",["aria-valuenow"]:c.text(percent),
        *:(if (c.props(node).label == null) {} else {['aria-label']:c.props(node).label}),
        *[<div class:"bold-progress-track",<div class:"bold-progress-fill",style:"width:" ++ c.text(percent) ++ "%;">>,
        if (c.option(c.props(node),"show_info",true)) <span class:"bold-progress-label",c.text(percent) ++ "%"> else null]>
}
view bold_presentation: <bold.config_provider> | <bold.flex> | <bold.space> | <bold.title> | <bold.text> | <bold.paragraph> | <bold.link> | <bold.badge> | <bold.card> | <bold.alert> | <bold.divider> | <bold.form_item> | <bold.progress> {
    let node = contract.checked(~)^
    let p = c.props(node)
    let kind = c.kind(node);
    if (kind == 'config-provider') <fieldset *:attributes(node),
        *:(if (p.direction == null) {} else {dir:c.text(p.direction)}),*:c.boolean_attr("disabled",p.disabled),
        style:theme.variables(c.option(p,"tokens",{}),true)^ ++ c.text(p.style),*c.contents(node)>
    else if (kind == 'flex' or kind == 'space') flex(node)
    else if (kind == 'title') c.heading(attributes(node),c.contents(node),c.option(p,"level",1))
    else if (kind == 'text') <span *:attributes(node),*c.contents(node)>
    else if (kind == 'paragraph') <p *:attributes(node),*c.contents(node)>
    else if (kind == 'link') <a *:attributes(node),*:link_attrs(p),*c.contents(node)>
    else if (kind == 'badge') <span *:attributes(node),*c.contents(node)>
    else if (kind == 'card') card(node)
    else if (kind == 'alert') <aside *:attributes(node),role:if (p.status == 'error' or p.status == 'warning') "alert" else "status",
        *[if (p.title != null) <strong class:"bold-alert-title",c.render(p.title)> else null,*c.contents(node)]>
    else if (kind == 'divider') <hr *:attributes(node)>
    else if (kind == 'form-item') form_item(node)
    else if (kind == 'progress') progress(node)
    else raise c.fail(kind,"component is not implemented","bold")
}
