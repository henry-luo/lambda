import c: lambda.ui.core.component
import shared: lambda.ui.core.contract
import theme: .tokens
import elements: .elements

pub fn validate(kind, props) bool^ {
    let valid = shared.validate(kind,props,shared.fields,"bold",
        ["default","secondary","outline","destructive","ghost","link"],["default","secondary","outline","destructive"])^;
    if (kind == 'page' or kind == 'config-provider') (let values = theme.resolve(c.option(props,"tokens",{}))^,true) else true
}
pub fn node(kind, props, child = null) element^ {
    let valid = validate(kind,props)^;
    elements.create(kind,props,c.children(child))
}
// direct element authors receive the same diagnostics as constructor callers (S12.1.3).
pub fn checked(source) element^ => node(c.kind(source),c.props(source),content(source))^
