import c: lambda.ui.core.component
import tokens: .tokens
import icon_parts: .icons

pub fn icon(props) element^ => icon_parts.render(props)^
fn layout(node) => <div *:c.styled(node, if (any([for (child in content(node)) child is element and name(child) == 'dtna.layout_sider'])) "dtna-layout-has-sider" else ""), *c.contents(node)>
fn breadcrumb(node) => <nav *:c.styled(node), ["aria-label"]:c.option(c.props(node), "label", "Breadcrumb"),
    <ol *[for (index, item in c.props(node).items) <li *[
        if (index > 0) <span class:"dtna-breadcrumb-separator", ["aria-hidden"]:"true", c.option(c.props(node), "separator", "/")> else null,
        if (item.href != null) <a href:item.href, c.render(item.title)> else
            <span ["aria-current"]:if (index == len(c.props(node).items) - 1) "page" else null, c.render(item.title)>]>]>>
view dtna_presentation: <dtna.icon> | <dtna.layout> | <dtna.layout_header> | <dtna.layout_footer> | <dtna.layout_content> | <dtna.layout_sider> | <dtna.breadcrumb> | <dtna.config_provider> {
    let p = c.props(~)
    let kind = c.kind(~);
    if (kind == 'icon') icon(p)^
    else if (kind == 'layout') layout(~)
    else if (contains(['layout-header', 'layout-footer', 'layout-content', 'layout-sider'], kind)) <div *:c.styled(~), *c.contents(~)>
    else if (kind == 'breadcrumb') breadcrumb(~)
    else if (kind == 'config-provider') <fieldset *:c.styled(~),
        *:(if (p.direction == null) {} else {dir:c.text(p.direction)}),
        *:c.boolean_attr("disabled", p.disabled), style:tokens.scoped_variables(c.option(p, "tokens", {}))^ ++ c.text(p.style), *c.contents(~)>
    else raise c.fail(kind, "component is not implemented")
}
