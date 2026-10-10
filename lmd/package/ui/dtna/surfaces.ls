import c: lambda.ui.core.component
import collection: lambda.ui.core.collection
import icons: .icons
import skeleton: .skeleton
import navigation: .navigation
import locale: .locale

// imported XML belongs to Input; the per-instance SVG root carries decorative semantics.
let illustrations = map([for (name in ["empty-default","empty-simple","result-403","result-404","result-500"])
    (name,content(input(sys.lambda.home# ++ "/package/ui/dtna/illustrations/" ++ name ++ ".svg",'xml')^)[0])])
fn illustration(name, height) {
    let source = illustrations[name];
    <svg *:map(source),class:"dtna-illustration",["aria-hidden"]:"true",focusable:"false",
        viewBox:c.option(map(source),"viewBox","0 0 " ++ source.width ++ " " ++ source.height),
        style:"height:" ++ c.px(height) ++ ";width:auto;",*content(source)>
}
pub fn card(props, child) element^ {
    let flags = c.boolean_props(props,["bordered","hoverable","loading"],"card")^
    let tabs = if (props.tab_list == null) true else collection.validate(props.tab_list,"card tabs")^
    let identity = if (props.tab_list == null) true else collection.require_id(props,"card")^;
    if (not c.enum_valid(props.type,["inner"])^) raise c.fail("card","type must be inner")
    else if (props.actions != null and not (props.actions is array)) raise c.fail("card","actions must be an array")
    else if (props.active_tab != null and props.default_active_tab != null) raise c.fail("card","active_tab and default_active_tab are mutually exclusive")
    else (
        let selected = if (props.tab_list == null) null else card_tabs(props)^,
        c.node('card',props,child,["extra","actions","cover","bordered","hoverable","loading","size","type","head_style","body_style","tab_list","active_tab","default_active_tab"])^
    )
}
pub fn card_part(kind, props, child) element^ {
    let flags = c.boolean_props(props,["hoverable"],kind)^;
    c.node(kind,props,child,if (kind == 'card-meta') ["avatar","description"] else ["hoverable"])^
}
pub fn empty(props, child) element^ {
    let messages = locale.resolve(c.option(props,"locale","en-US"))^;
    if (props.image_height != null and (not c.finite(props.image_height) or props.image_height < 0)) raise c.fail("empty","image_height must be nonnegative")
    else if (props.image != null and not (props.image is string or props.image is symbol or props.image is element or props.image is bool)) raise c.fail("empty","image must be a preset, URL, element or false")
    else c.node('empty',props,child,["description","image","image_height","locale"])^
}
pub fn result(props, child) element^ => c.node('result',props,child,["status","description","subtitle","icon","extra"])^
fn card_tabs(p) element^ => navigation.choice('tabs',{
        id:p.id ++ "-tabs",items:p.tab_list,
        *:(if (c.has(p,"active_tab")) {value:p.active_tab} else if (c.has(p,"default_active_tab")) {default_value:p.default_active_tab} else {})})^
fn card_view(node) {
    let p = node.props
    let tabs = if (p.tab_list == null) null else card_tabs(p)^;
    <section *:c.styled(node,(if (p.bordered == false) "dtna-card-borderless " else "") ++
        (if (p.hoverable) "dtna-card-hoverable " else "") ++ (if (p.type == 'inner' or p.type == "inner") "dtna-card-inner" else "")),
        ["aria-busy"]:if (p.loading) "true" else null,
        *[if (p.title != null or p.extra != null) <div class:"dtna-card-header",style:p.head_style,
            <div class:"dtna-card-title",c.render(p.title)> <div class:"dtna-card-extra",c.render(p.extra)>> else null,
        if (p.cover != null) <div class:"dtna-card-cover",c.render(p.cover)> else null,
        <div class:"dtna-card-body",style:p.body_style,
            *(if (p.loading) [c.render(skeleton.descriptor({paragraph:{rows:3}},null)^)] else
                [if (tabs != null) c.render(tabs) else null,*c.contents(node)])>,
        if (p.actions != null and len(p.actions) > 0) <ul class:"dtna-card-actions",*[for (action in p.actions) <li c.render(action)>]> else null]>
}
fn empty_view(node) {
    let p = node.props
    let image = c.option(p,"image",'default')
    let preset = c.text(image)
    let height = c.option(p,"image_height",if (preset == "simple") 40 else 100)
    let description = c.option(p,"description",(locale.resolve(c.option(p,"locale","en-US"))^).empty_description);
    <div *:c.styled(node),*[
        if (image == null or image == false) null else <div class:"dtna-empty-image",style:"height:" ++ c.px(height) ++ ";",
            if (contains(["default","simple"],preset)) illustration("empty-" ++ preset,height)
            else if (image is element) c.render(image) else <img src:preset,alt:"",style:"height:100%;">>,
        if (description == null or description == false) null else <p class:"dtna-empty-description",c.render(description)>,
        if (len(content(node)) > 0) <div class:"dtna-empty-footer",*c.contents(node)> else null]>
}
fn result_view(node) {
    let p = node.props
    let status = c.text(c.option(p,"status",'info'))
    let exception = contains(["403","404","500"],status)
    let icon = c.option(p,"icon",if (exception) null else icons.render({name:
        if (status == "success") "check-circle" else if (status == "error") "close-circle" else if (status == "warning") "warning" else "exclamation-circle",theme:'filled',width:72,height:72})^);
    <section *:c.styled(node,"dtna-result-" ++ status),*[
        if (exception) <div class:"dtna-result-icon",illustration("result-" ++ status,294)>
        else if (icon == null or icon == false) null else <div class:"dtna-result-icon",c.render(icon)>,
        if (p.title != null) <h2 class:"dtna-result-title",c.render(p.title)> else null,
        if (p.subtitle != null or p.description != null) <p class:"dtna-result-subtitle",c.render(c.option(p,"subtitle",p.description))> else null,
        if (p.extra != null) <div class:"dtna-result-extra",c.render(p.extra)> else null,
        if (len(content(node)) > 0) <div class:"dtna-result-content",*c.contents(node)> else null]>
}
view dtna_surfaces: <dtna kind:'card' | 'card-grid' | 'card-meta' | 'empty' | 'result'> {
    if (~.kind == 'card') card_view(~)
    else if (~.kind == 'empty') empty_view(~)
    else if (~.kind == 'result') result_view(~)
    else if (~.kind == 'card-grid') <div *:c.styled(~,if (~.props.hoverable == false) "dtna-card-grid-static" else ""),*c.contents(~)>
    else <div *:c.styled(~),*[
        if (~.props.avatar != null) <div class:"dtna-card-meta-avatar",c.render(~.props.avatar)> else null,
        <div class:"dtna-card-meta-detail",*[
            if (~.props.title != null) <div class:"dtna-card-meta-title",c.render(~.props.title)> else null,
            if (~.props.description != null) <div class:"dtna-card-meta-description",c.render(~.props.description)> else null,*c.contents(~)]>]>
}
pub let css = "
.dtna-card.dtna-card-borderless{border:0}.dtna-card-hoverable:hover,.dtna-card-grid:hover{box-shadow:0 6px 16px rgba(0,0,0,0.08)}
.dtna-card-cover img{display:block;width:100%}.dtna-card-inner .dtna-card-header{background:var(--dtna-surface);font-size:14px;padding:12px 24px}
.dtna-card.dtna-size-small .dtna-card-header{padding:8px 12px}.dtna-card.dtna-size-small .dtna-card-body{padding:12px}
.dtna-card-actions{list-style:none;margin:0;justify-content:space-around}.dtna-card-actions>li{flex:1;text-align:center}.dtna-card-actions>li+li{border-inline-start:1px solid var(--dtna-border)}
.dtna-card-body:has(>.dtna-card-grid){display:flex;flex-wrap:wrap;padding:0}.dtna-card-grid{width:33.333333%;padding:24px;box-shadow:1px 0 0 var(--dtna-border),0 1px 0 var(--dtna-border)}
.dtna-card-grid-static:hover{box-shadow:1px 0 0 var(--dtna-border),0 1px 0 var(--dtna-border)}
.dtna-card-meta{display:flex;gap:16px}.dtna-card-meta-detail{flex:1;min-width:0}.dtna-card-meta-title{font-size:16px;font-weight:600;margin-bottom:8px}.dtna-card-meta-description{color:var(--dtna-text-secondary)}
.dtna-empty{margin:0 8px;padding:0;font-size:14px;line-height:1.57142857}.dtna-empty-image{margin-bottom:8px}.dtna-empty-description{margin:0;color:var(--dtna-text-secondary)}.dtna-empty-footer{margin-top:16px}
.dtna-illustration{display:inline-block;vertical-align:middle}.dtna-result{padding:48px 32px}.dtna-result-icon{margin-bottom:24px}.dtna-result-title{font-size:24px;font-weight:400;line-height:1.3333333;margin:0}
.dtna-result-subtitle{color:var(--dtna-text-secondary);margin:8px 0 0}.dtna-result-extra{margin-top:24px}.dtna-result-content{margin-top:24px;padding:24px 40px;background:var(--dtna-surface);text-align:start}
.dtna-result-success .dtna-result-icon{color:var(--dtna-success)}.dtna-result-error .dtna-result-icon{color:var(--dtna-error)}.dtna-result-warning .dtna-result-icon{color:var(--dtna-warning)}
"
