// Shipped facade (D7.2.4); imported views retain their defining module.
import c: lambda.ui.core.component
import theme: .dtna.tokens
import styles: .dtna.style
import general: .dtna.general
import controls: .dtna.input
import navigation: .dtna.navigation
import forms: .dtna.form
import contract: .dtna.contract
import disclosure: .dtna.collapse
import trees: .dtna.tree
import tables: .dtna.table
import layout_parts: .dtna.layout
import display_parts: .dtna.display
import icon_parts: .dtna.icons
import progress_parts: .dtna.progress
import timeline_parts: .dtna.timeline
import statistic_parts: .dtna.statistic
import skeleton_parts: .dtna.skeleton
import surfaces: .dtna.surfaces
import typography: .dtna.typography
import avatar_parts: .dtna.avatar
import tag_parts: .dtna.tag
import rate_parts: .dtna.rate
import alert_parts: .dtna.alert
import spin_parts: .dtna.spin
import button_parts: .dtna.button


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
    // authored page children become sibling nodes rather than a nested array slot.
    <html lang:c.option(options, "locale", "en-US"), dir:c.option(options, "direction", "ltr"),
        <head <meta charset:"UTF-8"> <title c.option(options, "title", "Lambda UI")> <style styles.css>>
        <body style:"margin:0;", <div class:"dtna-root", style:variables, *c.children(c.render(root))>>>
}
// direct element documents share the constructor's shell and validation (S12.1.3).
view dtna_page: <dtna.page> {
    page(content(~),c.props(~))^
}
pub fn button(props = {}, child = null) element^ => button_parts.descriptor(props,child)^
pub fn button_group(props = {}, child = null) element^ => layout_parts.compact('button-group',props,child)^
pub fn input(props = {}) element^ => component('input', props, null, ["type", "value", "default_value", "placeholder", "name", "readonly", "required", "maxlength", "autocomplete"])^
pub fn text_area(props = {}) element^ => component('text-area', props, null, ["value", "default_value", "placeholder", "name", "readonly", "required", "maxlength", "rows"])^
pub fn password(props = {}) element^ => input({*:props, type:"password"})^
pub fn search(props = {}) element^ => input({*:props, type:"search"})^
pub fn select(props = {}) element^ => component('select', props, null, ["options", "value", "default_value", "name", "required"])^
pub fn checkbox(props = {}, child = null) element^ => component('checkbox', props, child, ["name", "value", "checked", "default_checked"])^
pub fn radio(props = {}, child = null) element^ => component('radio', props, child, ["name", "value", "checked", "default_checked"])^
pub fn switch(props = {}) element^ => component('switch', props, null, ["checked", "default_checked"])^
pub fn icon(props = {}) element^ => icon_parts.descriptor(props)^
pub fn icon_names() array => icon_parts.names()
pub fn title(props = {}, child = null) element^ => typography.descriptor('title',props,child)^
pub fn text(props = {}, child = null) element^ => typography.descriptor('text',props,child)^
pub fn paragraph(props = {}, child = null) element^ => typography.descriptor('paragraph',props,child)^
pub fn link(props = {}, child = null) element^ => typography.descriptor('link',props,child)^
pub fn space(props = {}, child = null) element^ => layout_parts.spacing('space',props,child)^
pub fn flex(props = {}, child = null) element^ => layout_parts.spacing('flex',props,child)^
pub fn space_compact(props = {}, child = null) element^ => layout_parts.compact('space-compact',props,child)^
pub fn divider(props = {}, child = null) element^ => layout_parts.divider(props,child)^
pub fn row(props = {}, child = null) element^ => layout_parts.row(props,child)^
pub fn col(props = {}, child = null) element^ => layout_parts.col(props,child)^
pub fn layout(props = {}, child = null) element^ => component('layout', props, child)^
pub fn layout_header(props = {}, child = null) element^ => component('layout-header', props, child)^
pub fn layout_footer(props = {}, child = null) element^ => component('layout-footer', props, child)^
pub fn layout_content(props = {}, child = null) element^ => component('layout-content', props, child)^
pub fn layout_sider(props = {}, child = null) element^ => component('layout-sider', props, child)^
pub fn avatar(props = {}, child = null) element^ => avatar_parts.descriptor(props,child)^
pub fn badge(props = {}, child = null) element^ => display_parts.badge(props,child)^
pub fn badge_ribbon(props = {}, child = null) element^ => display_parts.ribbon(props,child)^
pub fn card(props = {}, child = null) element^ => surfaces.card(props,child)^
pub fn card_grid(props = {}, child = null) element^ => surfaces.card_part('card-grid',props,child)^
pub fn card_meta(props = {}, child = null) element^ => surfaces.card_part('card-meta',props,child)^
pub fn empty(props = {}, child = null) element^ => surfaces.empty(props,child)^
pub fn statistic(props = {}) element^ => statistic_parts.descriptor(props)^
pub fn progress(props = {}) element^ => progress_parts.descriptor(props)^
pub fn timeline(props = {}) element^ => timeline_parts.descriptor(props)^
pub fn steps(props = {}) element^ => navigation.steps(props)^
pub fn breadcrumb(props = {}) element^ => component('breadcrumb', props, null, ["items", "separator"])^
pub fn descriptions(props = {}) element^ => display_parts.descriptions(props)^
pub fn description_item(props = {}, child = null) map^ => display_parts.description_item(props,child)^
pub fn skeleton(props = {}, child = null) element^ => skeleton_parts.descriptor(props,child)^
pub fn skeleton_avatar(props = {}) element^ => skeleton_parts.part('skeleton-avatar',props)^
pub fn skeleton_button(props = {}) element^ => skeleton_parts.part('skeleton-button',props)^
pub fn skeleton_input(props = {}) element^ => skeleton_parts.part('skeleton-input',props)^
pub fn skeleton_image(props = {}) element^ => skeleton_parts.part('skeleton-image',props)^
pub fn skeleton_node(props = {}, child = null) element^ => skeleton_parts.part('skeleton-node',props,child)^
pub fn spin(props = {}, child = null) element^ => spin_parts.descriptor(props,child)^
pub fn result(props = {}, child = null) element^ => surfaces.result(props,child)^
pub fn alert(props = {}, child = null) element^ => alert_parts.descriptor(props,child)^
pub fn tag(props = {}, child = null) element^ => tag_parts.descriptor('tag',props,child)^
pub fn checkable_tag(props = {}, child = null) element^ => tag_parts.descriptor('checkable-tag',props,child)^
pub fn rate(props = {}) element^ => rate_parts.descriptor(props)^
pub fn config_provider(props = {}, child = null) element^ => component('config-provider', props, child, ["tokens", "direction"])^

pub fn tabs(props) element^ => navigation.choice('tabs',props)^
pub fn segmented(props) element^ => navigation.choice('segmented',props)^
pub fn menu(props) element^ => navigation.choice('menu',props)^
pub fn pagination(props = {}) element^ => navigation.pagination(props)^
pub fn palette(seed) array^ => theme.palette(seed)^
pub fn form(props = {}, child = null) element^ => component('form',props,child)^
pub fn form_item(props = {}, child = null) element^ => component('form-item',props,child,["for","required","help"])^
pub fn collapse(props) element^ => disclosure.collapse(props)^
pub fn tree(props) element^ => trees.tree(props)^
pub fn table(props) element^ => tables.table(props)^
