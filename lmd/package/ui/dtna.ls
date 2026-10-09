// Shipped facade (D7.2.4); imported views retain their defining module.
import c: lambda.ui.core.component
import theme: .dtna.tokens
import styles: .dtna.style
import general: .dtna.general
import controls: .dtna.input
import navigation: .dtna.navigation
import forms: .dtna.form
import contract: .dtna.contract


fn component(kind, props, child, allowed = []) element^ {
    let styled = if (kind == 'button') ["size","disabled","variant","danger","shape","block","icon"]
        else if (contains(['input','text-area','select'],kind)) ["size","disabled","status"]
        else if (contains(['checkbox','radio','switch'],kind)) ["size","disabled"]
        else if (contains(['alert','tag','progress','form-item','result'],kind)) ["status"]
        else if (kind == 'config-provider') ["disabled"] else []
    let descriptor = c.node(kind,props,child,[*allowed,*styled])^
    let valid = contract.validate(kind,props)^;
    descriptor
}

pub fn tokens(overrides = {}) map^ => theme.resolve(overrides)^
pub fn stylesheet() => styles.css
pub fn render(root) => c.render(root)
pub fn page(root, options = {}) element^ {
    let names = c.properties(options,["tokens","title","locale","direction"],"page")^
    let valid = contract.validate('page',options)^
    let variables = theme.variables(c.option(options, "tokens", {}))^;
    <html lang:c.option(options, "locale", "en-US"), dir:c.option(options, "direction", "ltr"),
        <head <meta charset:"UTF-8"> <title c.option(options, "title", "Lambda UI")> <style styles.css>>
        <body style:"margin:0;", <div class:"dtna-root", style:variables, c.render(root)>>>
}
pub fn button(props = {}, child = null) element^ => component('button', props, child, ["type", "loading"])^
pub fn input(props = {}) element^ => component('input', props, null, ["type", "value", "default_value", "placeholder", "name", "readonly", "required", "maxlength", "autocomplete"])^
pub fn text_area(props = {}) element^ => component('text-area', props, null, ["value", "default_value", "placeholder", "name", "readonly", "required", "maxlength", "rows"])^
pub fn password(props = {}) element^ => input({*:props, type:"password"})^
pub fn search(props = {}) element^ => input({*:props, type:"search"})^
pub fn select(props = {}) element^ => component('select', props, null, ["options", "value", "default_value", "name", "required"])^
pub fn checkbox(props = {}, child = null) element^ => component('checkbox', props, child, ["name", "value", "checked", "default_checked"])^
pub fn radio(props = {}, child = null) element^ => component('radio', props, child, ["name", "value", "checked", "default_checked"])^
pub fn switch(props = {}) element^ => component('switch', props, null, ["checked", "default_checked"])^
pub fn icon(props = {}) element^ => component('icon', props, null, ["name", "width", "height"])^
pub fn title(props = {}, child = null) element^ => component('title', props, child, ["level"])^
pub fn text(props = {}, child = null) element^ => component('text', props, child)^
pub fn paragraph(props = {}, child = null) element^ => component('paragraph', props, child)^
pub fn link(props = {}, child = null) element^ => component('link', props, child, ["href", "target"])^
let flex_props = ["direction", "gap", "align", "justify", "wrap"]
pub fn space(props = {}, child = null) element^ => component('space', props, child, flex_props)^
pub fn flex(props = {}, child = null) element^ => component('flex', props, child, flex_props)^
pub fn space_compact(props = {}, child = null) element^ => component('space-compact', props, child)^
pub fn divider(props = {}, child = null) element^ => component('divider', props, child, ["direction"])^
pub fn row(props = {}, child = null) element^ => component('row', props, child, ["gutter"])^
pub fn col(props = {}, child = null) element^ => component('col', props, child, ["span"])^
pub fn layout(props = {}, child = null) element^ => component('layout', props, child)^
pub fn layout_header(props = {}, child = null) element^ => component('layout-header', props, child)^
pub fn layout_footer(props = {}, child = null) element^ => component('layout-footer', props, child)^
pub fn layout_content(props = {}, child = null) element^ => component('layout-content', props, child)^
pub fn layout_sider(props = {}, child = null) element^ => component('layout-sider', props, child)^
pub fn avatar(props = {}, child = null) element^ => component('avatar', props, child, ["src", "dimension"])^
pub fn badge(props = {}, child = null) element^ => component('badge', props, child, ["count", "overflow_count", "dot"])^
pub fn card(props = {}, child = null) element^ => component('card', props, child, ["extra", "actions"])^
pub fn empty(props = {}, child = null) element^ => component('empty', props, child, ["description"])^
pub fn statistic(props = {}) element^ => component('statistic', props, null, ["value", "prefix", "suffix"])^
pub fn progress(props = {}) element^ => component('progress', props, null, ["percent", "show_info"])^
pub fn timeline(props = {}) element^ => component('timeline', props, null, ["items"])^
pub fn steps(props = {}) element^ => component('steps', props, null, ["items", "current"])^
pub fn breadcrumb(props = {}) element^ => component('breadcrumb', props, null, ["items", "separator"])^
pub fn descriptions(props = {}) element^ => component('descriptions', props, null, ["items"])^
pub fn skeleton(props = {}) element^ => component('skeleton', props, null, ["rows"])^
pub fn spin(props = {}, child = null) element^ => component('spin', props, child)^
pub fn result(props = {}, child = null) element^ => component('result', props, child, ["description"])^
pub fn alert(props = {}, child = null) element^ => component('alert', props, child, ["message", "description", "closable"])^
pub fn tag(props = {}, child = null) element^ => component('tag', props, child, ["closable"])^
pub fn config_provider(props = {}, child = null) element^ => component('config-provider', props, child, ["tokens", "direction"])^

pub fn tabs(props) element^ => navigation.choice('tabs',props)^
pub fn segmented(props) element^ => navigation.choice('segmented',props)^
pub fn menu(props) element^ => navigation.choice('menu',props)^
pub fn pagination(props = {}) element^ => navigation.pagination(props)^
pub fn palette(seed) array^ => theme.palette(seed)^
pub fn form(props = {}, child = null) element^ => component('form',props,child)^
pub fn form_item(props = {}, child = null) element^ => component('form-item',props,child,["for","required","help"])^
