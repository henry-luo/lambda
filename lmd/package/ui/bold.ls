// ordinary shipped facade; presentation stays pure and handlers own effects (D7.2.4; S12.1.3).
import c: lambda.ui.core.component
import theme: .bold.tokens
import styles: .bold.style
import contract: .bold.contract
import controls: .bold.input
import presentation: .bold.presentation

pub fn tokens(overrides = {}) map^ => theme.resolve(overrides)^
pub fn stylesheet() string => styles.css
pub fn render(root) {
    if (root is array) [for (child in root) render(child)] else (
        let valid = if (root is element and (name(root) == 'bold' or starts_with(string(name(root)),"bold.")))
            contract.validate(c.kind(root),c.props(root))^ else true,
        c.render(root)
    )
}
pub fn page(root, options = {}) element^ {
    let valid = contract.validate('page',options)^
    let variables = theme.variables(c.option(options,"tokens",{}))^;
    <html lang:c.option(options,"locale","en-US"),dir:c.text(c.option(options,"direction","ltr")),
        <head <meta charset:"UTF-8"> <title c.option(options,"title","Lambda UI · bold")> <style styles.css>>
        <body style:"margin:0;",<div class:"bold-root",style:variables,*c.children(render(root)^)>>>
}
view bold_page: <bold.page> | <bold kind:'page'> { page(content(~),c.props(~))^ }

pub fn button(props = {}, child = null) element^ => contract.node('button',props,child)^
pub fn input(props = {}) element^ => contract.node('input',props)^
pub fn text_area(props = {}) element^ => contract.node('text-area',props)^
pub fn select(props) element^ => contract.node('select',props)^
pub fn checkbox(props = {}, child = null) element^ => contract.node('checkbox',props,child)^
pub fn radio(props = {}, child = null) element^ => contract.node('radio',props,child)^
pub fn switch(props = {}) element^ => contract.node('switch',props)^
pub fn title(props = {}, child = null) element^ => contract.node('title',props,child)^
pub fn text(props = {}, child = null) element^ => contract.node('text',props,child)^
pub fn paragraph(props = {}, child = null) element^ => contract.node('paragraph',props,child)^
pub fn link(props = {}, child = null) element^ => contract.node('link',props,child)^
pub fn card(props = {}, child = null) element^ => contract.node('card',props,child)^
pub fn badge(props = {}, child = null) element^ => contract.node('badge',props,child)^
pub fn alert(props = {}, child = null) element^ => contract.node('alert',props,child)^
pub fn divider(props = {}) element^ => contract.node('divider',props)^
pub fn flex(props = {}, child = null) element^ => contract.node('flex',props,child)^
pub fn space(props = {}, child = null) element^ => contract.node('space',props,child)^
pub fn form(props = {}, child = null) element^ => contract.node('form',props,child)^
pub fn form_item(props = {}, child = null) element^ => contract.node('form-item',props,child)^
pub fn progress(props = {}) element^ => contract.node('progress',props)^
pub fn config_provider(props = {}, child = null) element^ => contract.node('config-provider',props,child)^
