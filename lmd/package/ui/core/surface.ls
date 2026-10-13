import c: .component

pub fn attributes(node, family) {
    let p = c.props(node);
    {*:c.styled(node,"",family),*:(if (p.label == null or c.has(p,"aria-label")) {} else {['aria-label']:p.label})}
}
pub fn link_attrs(props) => {*:map([for (key in ["href","target","rel"] where props[key] != null and (key != "href" or not props.disabled)) (key,props[key])]),
    *:(if (props.target == "_blank" and props.rel == null) {rel:"noopener noreferrer"} else {}),
    *:(if (props.disabled) {tabindex:-1,['aria-disabled']:"true"} else {})}

pub fn button(node, family, decoration = null) {
    let p = c.props(node)
    let attrs = {*:attributes(node,family),class:c.classes('button',p,if (p.block) family ++ "-block" else "",family)};
    if (p.href != null) <a *:attrs,*:link_attrs(p),*[ *c.contents(node),*c.children(decoration)]>
    else <button *:attrs,type:c.text(c.option(p,"type","button")),*:c.boolean_attr("disabled",p.disabled),*[ *c.contents(node),*c.children(decoration)]>
}
pub fn flex(node, family) {
    let p = c.props(node)
    let style = "display:flex;flex-direction:" ++ (if (p.direction == 'vertical') "column" else "row") ++ ";gap:" ++
        (if (p.gap == null) "var(--" ++ family ++ "-spacing)" else c.length(p.gap)) ++ ";align-items:" ++ c.text(c.option(p,"align","center")) ++
        ";justify-content:" ++ c.text(c.option(p,"justify","flex-start")) ++ ";flex-wrap:" ++ (if (p.wrap) "wrap" else "nowrap") ++ ";";
    <div *:attributes(node,family),style:c.style_with(p,style),*c.contents(node)>
}
pub fn card(node, family, decoration = null) => <section *:attributes(node,family), *[
    if (c.props(node).title != null or c.props(node).description != null) <header class:family ++ "-card-header",
        *[if (c.props(node).title != null) <h3 class:family ++ "-card-title",c.render(c.props(node).title)> else null,
        if (c.props(node).description != null) <p class:family ++ "-card-description",c.render(c.props(node).description)> else null]> else null,
    *c.children(decoration),
    <div class:family ++ "-card-body",*c.contents(node)>,
    if (c.props(node).footer != null) <footer class:family ++ "-card-footer",*c.children(c.render(c.props(node).footer))> else null]>
pub fn form_item(node, family) => <div *:attributes(node,family), *[
    if (c.props(node).label != null) <label class:family ++ "-form-label",for:c.props(node).for,
        *[c.props(node).label,if (c.props(node).required) <span class:family ++ "-required",["aria-hidden"]:"true"," *"> else null]> else null,
    <div class:family ++ "-form-control",*c.contents(node)>,
    if (c.props(node).help != null) <div class:family ++ "-form-help",role:if (c.props(node).status == 'error') "alert" else null,c.props(node).help> else null]>
pub fn progress(node, family, decoration = null) {
    let percent = c.option(c.props(node),"percent",0);
    <div *:attributes(node,family),role:"progressbar",["aria-valuemin"]:"0",["aria-valuemax"]:"100",["aria-valuenow"]:c.text(percent),
        *:(if (c.props(node).label == null) {} else {['aria-label']:c.props(node).label}),
        *[<div class:family ++ "-progress-track",*[<div class:family ++ "-progress-fill",style:"width:" ++ c.text(percent) ++ "%;">,*c.children(decoration)]>,
        if (c.option(c.props(node),"show_info",true)) <span class:family ++ "-progress-label",c.text(percent) ++ "%"> else null]>
}

pub fn present(node, family, variables = "", decoration = null) {
    let p = c.props(node)
    let kind = c.kind(node);
    if (kind == 'config-provider') <fieldset *:attributes(node,family),
        *:(if (p.direction == null) {} else {dir:c.text(p.direction)}),*:c.boolean_attr("disabled",p.disabled),
        style:variables ++ c.text(p.style),*c.contents(node)>
    else if (kind == 'flex' or kind == 'space') flex(node,family)
    else if (kind == 'title') c.heading(attributes(node,family),c.contents(node),c.option(p,"level",1))
    else if (kind == 'text' or kind == 'badge') <span *:attributes(node,family),*[ *c.contents(node),*c.children(decoration)]>
    else if (kind == 'paragraph') <p *:attributes(node,family),*c.contents(node)>
    else if (kind == 'link') <a *:attributes(node,family),*:link_attrs(p),*c.contents(node)>
    else if (kind == 'card') card(node,family,decoration)
    else if (kind == 'alert') <aside *:attributes(node,family),role:if (p.status == 'error' or p.status == 'warning') "alert" else "status",
        *[if (p.title != null) <strong class:family ++ "-alert-title",c.render(p.title)> else null,*c.contents(node),*c.children(decoration)]>
    else if (kind == 'divider') <hr *:attributes(node,family)>
    else if (kind == 'form-item') form_item(node,family)
    else if (kind == 'progress') progress(node,family,decoration)
    else raise c.fail(kind,"component is not implemented",family)
}
