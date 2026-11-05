// desktop presentation composes shared controls without a visual-family dependency.
import c: lambda.ui.core.component
import surface: lambda.ui.core.surface
import native: lambda.ui.core.control
import shell: lambda.ui.core.presentation
import contract: .workspace.contract
import theme: .workspace.tokens
import styles: .workspace.style
import dialog: .workspace.dialog

pub fn tokens(overrides = {}) map^ => theme.resolve(overrides)^
pub fn stylesheet() => styles.css ++ dialog.css
pub fn render(root) => c.render(root)
pub fn page(root, options = {}) element^ {
    let valid = contract.validate('page',options)^
    let variables = theme.variables(c.option(options,"tokens",{}))^;
    shell.page(c.children(render(root)),options,"workspace",variables,stylesheet(),"workspace-root")
}
view workspace_page: <workspace.page> () any^ { page(content(~),c.props(~))^ }

pub fn button(props = {}, child = null) element^ => contract.node('button',props,child)^
pub fn input(props = {}) element^ => contract.node('input',props)^
pub fn text_area(props = {}) element^ => contract.node('text-area',props)^
pub fn select(props) element^ => contract.node('select',props)^
pub fn checkbox(props = {}, child = null) element^ => contract.node('checkbox',props,child)^
pub fn radio(props = {}, child = null) element^ => contract.node('radio',props,child)^
pub fn switch(props = {}) element^ => contract.node('switch',props)^
pub fn toolbar(props = {}, child = null) element^ => contract.node('toolbar',props,child)^
pub fn panes(props = {}, child = null) element^ => contract.node('panes',props,child)^
pub fn sidebar(props = {}, child = null) element^ => contract.node('sidebar',props,child)^
pub fn content_area(props = {}, child = null) element^ => contract.node('content',props,child)^
pub fn status(props = {}, child = null) element^ => contract.node('status',props,child)^
pub fn modal(props,child) element^ => dialog.modal(props,child)^

view workspace_button: <workspace.button> () any^ { surface.button(contract.checked(~)^,"workspace") }
on click(evt) {
    if ((c.props(~).disabled)^) { return 'prevent-default' }
    emit("ui_action",c.action(~,'click')^)
    'pass'
}

view workspace_controls: <workspace.input> | <workspace.text_area> | <workspace.select> |
    <workspace.checkbox> | <workspace.radio> | <workspace.switch> () any^ {
    native.present(contract.checked(~)^,"workspace")
}
on input(evt) { native.edited(~,evt,"workspace") }
on change(evt) { native.changed(~,evt,"workspace") }
on render(evt) { native.rendered(~,evt) }
on workspace_control_sync(evt) { native.sync(~,evt.target) }

view workspace_chrome: <workspace.toolbar> | <workspace.panes> | <workspace.sidebar> |
    <workspace.content> | <workspace.status> () any^ {
    let node = contract.checked(~)^
    let attrs = surface.attributes(node,"workspace")
    let kind = c.kind(node);
    if (kind == 'sidebar') <aside *:attrs,*c.contents(node)>
    else if (kind == 'content') <main *:attrs,*c.contents(node)>
    else if (kind == 'status') <footer *:attrs,role:"status",*c.contents(node)>
    else if (kind == 'toolbar') <div *:attrs,role:"toolbar",*c.contents(node)>
    else <div *:attrs,*c.contents(node)>
}
