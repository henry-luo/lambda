import dom
import c: lambda.ui.core.component
import tokens: .tokens

let icon_paths = {
    check:"M4 12l5 5L20 6", close:"M6 6l12 12M18 6L6 18", plus:"M12 5v14M5 12h14",
    search:"M21 21l-5-5M18 10a8 8 0 1 1-16 0 8 8 0 0 1 16 0",
    ["chevron-down"]:"M5 9l7 7 7-7", info:"M12 11v6M12 7v1M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0",
    warning:"M12 8v5M12 16v1M12 2L1 21h22z", loading:"M22 12a10 10 0 1 1-10-10"
}
pub fn icon(props) element^ {
    let key = string(c.option(props, "name", "info"));
    if (not contains(icon_paths, symbol(key))) raise c.fail("icon", "unsupported icon " ++ key)
    else <svg *:c.attrs(props), class:c.classes('icon', props), width:c.option(props, "width", "1em"),
        height:c.option(props, "height", "1em"), viewBox:"0 0 24 24", fill:"none", stroke:"currentColor",
        style:(if (props.width == null) "" else "width:" ++ (if (props.width is number) c.px(props.width) else c.text(props.width)) ++ ";") ++
            (if (props.height == null) "" else "height:" ++ (if (props.height is number) c.px(props.height) else c.text(props.height)) ++ ";") ++ c.text(props.style),
        ["stroke-width"]:"2", ["stroke-linecap"]:"round", ["stroke-linejoin"]:"round",
        role:if (props.label == null) null else "img", ["aria-label"]:props.label,
        ["aria-hidden"]:if (props.label == null) "true" else null,
        <path d:icon_paths[key]>>
}
fn styled(node, suffix = "") => {*:c.attrs(node.props), class:c.classes(node.kind, node.props, suffix), style:node.props.style}
fn style_with(props, extra) => extra ++ c.text(props.style)
fn direction(props) => if (c.text(props.direction) == "vertical") "column" else "row"
fn spacing(node) => "display:flex;flex-direction:" ++ direction(node.props) ++ ";gap:" ++
    (if (node.props.gap == null) "var(--dtna-spacing)" else c.px(node.props.gap)) ++ ";align-items:" ++ c.text(c.option(node.props, "align", "center")) ++
    ";justify-content:" ++ c.text(c.option(node.props, "justify", "flex-start")) ++ ";flex-wrap:" ++
    (if (node.props.wrap) "wrap" else "nowrap") ++ ";"
fn layout(node) => <div *:styled(node, if (any([for (child in content(node)) child is element and child.kind == 'layout-sider'])) "dtna-layout-has-sider" else ""), *c.contents(node)>
fn avatar(node) => <span *:styled(node), style:style_with(node.props,
    (if (node.props.dimension == null) "" else "width:" ++ c.px(node.props.dimension) ++ ";height:" ++ c.px(node.props.dimension) ++ ";")),
    *(if (node.props.src != null) [<img src:node.props.src, alt:c.text(node.props.label)>] else c.contents(node))>
fn badge(node) => <span *:styled(node), *[*c.contents(node),
    <sup class:"dtna-badge-count", ["aria-label"]:node.props.label,
        if (node.props.dot) "" else if (node.props.count > c.option(node.props, "overflow_count", 99))
            string(c.option(node.props, "overflow_count", 99)) ++ "+" else c.text(node.props.count)>]>
fn card(node) => <section *:styled(node),
    *[if (node.props.title != null or node.props.extra != null) <div class:"dtna-card-header",
        <div class:"dtna-card-title", c.render(node.props.title)> <div class:"dtna-card-extra", c.render(node.props.extra)>> else null,
    <div class:"dtna-card-body", *c.contents(node)>,
    if (node.props.actions != null) <div class:"dtna-card-actions", *[for (action in node.props.actions) c.render(action)]> else null]>
fn empty(node) => <div *:styled(node),
    *[<svg class:"dtna-empty-image", viewBox:"0 0 64 48", width:"64", height:"48", ["aria-hidden"]:"true",
        <path d:"M8 18L20 4h24l12 14v25H8z", fill:"#fafafa", stroke:"#d9d9d9">
        <path d:"M8 18h14l5 8h10l5-8h14", fill:"none", stroke:"#d9d9d9">>
    , <p c.render(c.option(node.props, "description", "No data"))>, *c.contents(node)]>
fn statistic(node) => <div *:styled(node),
    <div class:"dtna-statistic-title", c.render(node.props.title)>
    <div class:"dtna-statistic-value", <span c.render(node.props.prefix)> <span c.text(node.props.value)> <span class:"dtna-statistic-suffix",c.render(node.props.suffix)>>>
fn progress(node) {
    let percent = min(100, max(0, c.option(node.props, "percent", 0)));
    <div *:styled(node), role:"progressbar", ["aria-valuemin"]:"0", ["aria-valuemax"]:"100", ["aria-valuenow"]:string(percent),
        <div class:"dtna-progress-track", <div class:"dtna-progress-fill", style:"width:" ++ string(percent) ++ "%;">>
        if (node.props.show_info != false) <span class:"dtna-progress-label", string(percent) ++ "%">>
}
fn timeline(node) => <ol *:styled(node), *[for (item in node.props.items)
    <li class:"dtna-timeline-item", <span class:"dtna-timeline-dot", style:if (item.color == null) null else "border-color:" ++ string(item.color) ++ ";">
        <div class:"dtna-timeline-content", <div class:"dtna-timeline-label",c.render(item.label)> <div c.render(item.children)>>>]>
fn steps(node) => <ol *:styled(node), *[for (index, item in node.props.items)
    <li class:"dtna-step" ++ (if (index == c.option(node.props, "current", 0)) " dtna-step-current" else "") ++
        (if (index < c.option(node.props, "current", 0)) " dtna-step-finished" else ""),
        ["aria-current"]:if (index == c.option(node.props, "current", 0)) "step" else null,
        <span class:"dtna-step-number", if (index < c.option(node.props, "current", 0)) icon({name:"check"})^ else string(index + 1)>
        <div <div class:"dtna-step-title", c.render(item.title)> <div class:"dtna-step-description", c.render(item.description)>>>]>
fn breadcrumb(node) => <nav *:styled(node), ["aria-label"]:c.option(node.props, "label", "Breadcrumb"),
    <ol *[for (index, item in node.props.items) <li *[
        if (index > 0) <span class:"dtna-breadcrumb-separator", ["aria-hidden"]:"true", c.option(node.props, "separator", "/")> else null,
        if (item.href != null) <a href:item.href, c.render(item.title)> else
            <span ["aria-current"]:if (index == len(node.props.items) - 1) "page" else null, c.render(item.title)>]>]>>
fn descriptions(node) => <dl *:styled(node), *[for (item in node.props.items)
    <div class:"dtna-description", <dt c.render(item.label)> <dd c.render(item.children)>>]>
fn skeleton(node) => <div *:styled(node), ["aria-busy"]:"true", ["aria-label"]:c.option(node.props, "label", "Loading"),
    *[for (index in 0 to (c.option(node.props, "rows", 3) - 1)) <div class:"dtna-skeleton-line", style:if (index == 0) "width:38%;" else if (index == c.option(node.props, "rows", 3) - 1) "width:61%;" else null>]>
fn result(node) => <section *:styled(node),
    <div class:"dtna-result-icon", icon({name:if (c.text(node.props.status) == "success") "check" else if (c.text(node.props.status) == "error") "close" else "info"})^>
    <h2 c.render(node.props.title)> <p c.render(node.props.description)> <div class:"dtna-result-extra", *c.contents(node)>>
pub fn dismissible(node) => <div *:styled(node), role:if (node.kind == 'alert') "alert" else null,
    <span class:"dtna-dismissible-content", *[c.render(node.props.message), *c.contents(node)]>
    if (node.props.description != null) <div class:"dtna-alert-description", c.render(node.props.description)>
    if (node.props.closable) <button type:"button", class:"dtna-close", ["aria-label"]:"Close", icon({name:"close"})^>>

view dtna_alert: <dtna kind:'alert'> state closed:false {
    if (closed) <span hidden:""> else dismissible(~)
}
on click(evt) {
    if (~.props.closable and dom.closest(evt.target, ".dtna-close") != null) {
        closed = true
        emit("ui_action", c.action(~, 'close'))
    }
}
view dtna_tag: <dtna kind:'tag'> state closed:false {
    if (closed) <span hidden:""> else dismissible(~)
}
on click(evt) {
    if (~.props.closable and dom.closest(evt.target, ".dtna-close") != null) {
        closed = true
        emit("ui_action", c.action(~, 'close'))
    }
}

view dtna_presentation: <dtna> {
    let p = ~.props
    let kind = ~.kind;
    if (kind == 'icon') icon(p)^
    else if (kind == 'space' or kind == 'flex') <div *:styled(~), style:style_with(p, spacing(~)), *c.contents(~)>
    else if (kind == 'space-compact') <div *:styled(~), *c.contents(~)>
    else if (kind == 'layout') layout(~)
    else if (contains(['layout-header', 'layout-footer', 'layout-content', 'layout-sider'], kind)) <div *:styled(~), *c.contents(~)>
    else if (kind == 'title') <h2 *:styled(~), ["aria-level"]:c.option(p, "level", 2),
        style:style_with(p, "font-size:" ++ c.px([38, 30, 24, 20, 16][c.option(p, "level", 2) - 1]) ++ ";"), *c.contents(~)>
    else if (kind == 'text') <span *:styled(~), *c.contents(~)>
    else if (kind == 'paragraph') <p *:styled(~), *c.contents(~)>
    else if (kind == 'link') <a *:styled(~), href:p.href, target:p.target, *c.contents(~)>
    else if (kind == 'divider') <div *:styled(~), role:"separator", ["aria-orientation"]:c.text(c.option(p, "direction", "horizontal")), *c.contents(~)>
    else if (kind == 'row') <div *:styled(~), style:style_with(p, "display:flex;flex-wrap:wrap;gap:" ++ c.px(c.option(p, "gutter", 0)) ++ ";"), *c.contents(~)>
    else if (kind == 'col') <div *:styled(~), style:style_with(p, "flex:0 0 " ++ string(c.option(p, "span", 24) * 100.0 / 24) ++ "%;max-width:" ++ string(c.option(p, "span", 24) * 100.0 / 24) ++ "%;"), *c.contents(~)>
    else if (kind == 'avatar') avatar(~)
    else if (kind == 'badge') badge(~)
    else if (kind == 'card') card(~)
    else if (kind == 'empty') empty(~)
    else if (kind == 'statistic') statistic(~)
    else if (kind == 'progress') progress(~)
    else if (kind == 'timeline') timeline(~)
    else if (kind == 'steps') steps(~)
    else if (kind == 'breadcrumb') breadcrumb(~)
    else if (kind == 'descriptions') descriptions(~)
    else if (kind == 'skeleton') skeleton(~)
    else if (kind == 'spin') <div *:styled(~), ["aria-busy"]:"true", role:"status", *[icon({name:"loading"})^, *c.contents(~)]>
    else if (kind == 'result') result(~)
    else if (kind == 'config-provider') <fieldset *:styled(~),
        *:(if (p.direction == null) {} else {dir:c.text(p.direction)}),
        *:c.boolean_attr("disabled", p.disabled), style:tokens.scoped_variables(c.option(p, "tokens", {}))^ ++ c.text(p.style), *c.contents(~)>
    else raise c.fail(kind, "component is not implemented")
}
