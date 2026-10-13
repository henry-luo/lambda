// ordinary source theme facade; native ownership and view state stay in the shared engine (D7.2.4; D7.2.5).
import c: lambda.ui.core.component
import base: lambda.ui.dtna
import catalog: .clay.catalog
import theme: .clay.tokens
import styles: .clay.style
import presentation: .clay.presentation

pub fn tokens(overrides = {}) map^ => theme.resolve(overrides)^
pub fn stylesheet() string => styles.css
pub fn component_names() array => [*catalog.kinds(),"config-provider"]
pub fn icon_names() array => base.icon_names()
pub fn palette(seed) array^ => base.palette(seed)^
pub fn render(root) any^ {
    if (root is array) [for (child in root) render(child)^] else (
        let known = if (root is element and starts_with(c.text(name(root)),"clay.") and
            not contains(["page","config-provider",*catalog.kinds()],c.text(c.kind(root))))
            raise c.fail(c.kind(root),"unknown component","clay") else true,
        c.render(root)^
    )
}
pub fn page(root, options = {}) element^ {
    let names = c.properties(options,["tokens","title","locale","direction"],"page",false,"clay")^
    let valid = presentation.direction(options,"page")^
    let variables = theme.variables(c.option(options,"tokens",{}))^;
    <html lang:c.option(options,"locale","en-US"),dir:c.text(c.option(options,"direction","ltr")),
        <head <meta charset:"UTF-8"> <title c.option(options,"title","Lambda UI · clay")> <style styles.css>>
        <body style:"margin:0;",<div class:"dtna-root clay-root",style:variables,*c.children(render(root)^)>>>
}
view clay_page: <clay.page> { page(content(~),c.props(~))^ }

pub fn button(props = {}, child = null) element^ => catalog.descriptor('button',props,child)^
pub fn button_group(props = {}, child = null) element^ => catalog.descriptor('button-group',props,child)^
pub fn input(props = {}) element^ => catalog.descriptor('input',props)^
pub fn text_area(props = {}) element^ => catalog.descriptor('text-area',props)^
pub fn select(props = {}) element^ => catalog.descriptor('select',props)^
pub fn checkbox(props = {}, child = null) element^ => catalog.descriptor('checkbox',props,child)^
pub fn radio(props = {}, child = null) element^ => catalog.descriptor('radio',props,child)^
pub fn switch(props = {}) element^ => catalog.descriptor('switch',props)^
pub fn icon(props = {}) element^ => catalog.descriptor('icon',props)^
pub fn title(props = {}, child = null) element^ => catalog.descriptor('title',props,child)^
pub fn text(props = {}, child = null) element^ => catalog.descriptor('text',props,child)^
pub fn paragraph(props = {}, child = null) element^ => catalog.descriptor('paragraph',props,child)^
pub fn link(props = {}, child = null) element^ => catalog.descriptor('link',props,child)^
pub fn space(props = {}, child = null) element^ => catalog.descriptor('space',props,child)^
pub fn flex(props = {}, child = null) element^ => catalog.descriptor('flex',props,child)^
pub fn space_compact(props = {}, child = null) element^ => catalog.descriptor('space-compact',props,child)^
pub fn divider(props = {}, child = null) element^ => catalog.descriptor('divider',props,child)^
pub fn row(props = {}, child = null) element^ => catalog.descriptor('row',props,child)^
pub fn col(props = {}, child = null) element^ => catalog.descriptor('col',props,child)^
pub fn layout(props = {}, child = null) element^ => catalog.descriptor('layout',props,child)^
pub fn layout_header(props = {}, child = null) element^ => catalog.descriptor('layout-header',props,child)^
pub fn layout_footer(props = {}, child = null) element^ => catalog.descriptor('layout-footer',props,child)^
pub fn layout_content(props = {}, child = null) element^ => catalog.descriptor('layout-content',props,child)^
pub fn layout_sider(props = {}, child = null) element^ => catalog.descriptor('layout-sider',props,child)^
pub fn avatar(props = {}, child = null) element^ => catalog.descriptor('avatar',props,child)^
pub fn badge(props = {}, child = null) element^ => catalog.descriptor('badge',props,child)^
pub fn badge_ribbon(props = {}, child = null) element^ => catalog.descriptor('badge-ribbon',props,child)^
pub fn card(props = {}, child = null) element^ => catalog.descriptor('card',props,child)^
pub fn card_grid(props = {}, child = null) element^ => catalog.descriptor('card-grid',props,child)^
pub fn card_meta(props = {}, child = null) element^ => catalog.descriptor('card-meta',props,child)^
pub fn empty(props = {}, child = null) element^ => catalog.descriptor('empty',props,child)^
pub fn statistic(props = {}) element^ => catalog.descriptor('statistic',props)^
pub fn progress(props = {}) element^ => catalog.descriptor('progress',props)^
pub fn timeline(props = {}) element^ => catalog.descriptor('timeline',props)^
pub fn steps(props = {}) element^ => catalog.descriptor('steps',props)^
pub fn breadcrumb(props = {}) element^ => catalog.descriptor('breadcrumb',props)^
pub fn descriptions(props = {}) element^ => catalog.descriptor('descriptions',props)^
pub fn skeleton(props = {}, child = null) element^ => catalog.descriptor('skeleton',props,child)^
pub fn skeleton_avatar(props = {}) element^ => catalog.descriptor('skeleton-avatar',props)^
pub fn skeleton_button(props = {}) element^ => catalog.descriptor('skeleton-button',props)^
pub fn skeleton_input(props = {}) element^ => catalog.descriptor('skeleton-input',props)^
pub fn skeleton_image(props = {}) element^ => catalog.descriptor('skeleton-image',props)^
pub fn skeleton_node(props = {}, child = null) element^ => catalog.descriptor('skeleton-node',props,child)^
pub fn spin(props = {}, child = null) element^ => catalog.descriptor('spin',props,child)^
pub fn result(props = {}, child = null) element^ => catalog.descriptor('result',props,child)^
pub fn alert(props = {}, child = null) element^ => catalog.descriptor('alert',props,child)^
pub fn tag(props = {}, child = null) element^ => catalog.descriptor('tag',props,child)^
pub fn checkable_tag(props = {}, child = null) element^ => catalog.descriptor('checkable-tag',props,child)^
pub fn rate(props = {}) element^ => catalog.descriptor('rate',props)^
pub fn tabs(props = {}) element^ => catalog.descriptor('tabs',props)^
pub fn segmented(props = {}) element^ => catalog.descriptor('segmented',props)^
pub fn menu(props = {}) element^ => catalog.descriptor('menu',props)^
pub fn pagination(props = {}) element^ => catalog.descriptor('pagination',props)^
pub fn form(props = {}, child = null) element^ => catalog.descriptor('form',props,child)^
pub fn form_item(props = {}, child = null) element^ => catalog.descriptor('form-item',props,child)^
pub fn collapse(props = {}) element^ => catalog.descriptor('collapse',props)^
pub fn tree(props = {}) element^ => catalog.descriptor('tree',props)^
pub fn table(props = {}) element^ => catalog.descriptor('table',props)^
pub fn password(props = {}) element^ => input({*:props,type:"password"})^
pub fn search(props = {}) element^ => input({*:props,type:"search"})^
pub fn description_item(props = {}, child = null) map^ => base.description_item(props,child)^
pub fn config_provider(props = {}, child = null) element^ => presentation.provider(props,child)^
