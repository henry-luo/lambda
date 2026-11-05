import c: lambda.ui.core.component
import shared: lambda.ui.core.contract
import elements: .elements

pub fn validate(kind, props) bool^ {
    if (kind at shared.fields)
        shared.validate(kind,props,shared.fields,"workspace",["default","primary","subtle","danger"],[])^
    else if (kind in ['toolbar','panes','sidebar','content','status'])
        c.properties(props,c.common_props,kind,true,"workspace")^
    else raise c.fail(kind,"component is not implemented","workspace")
}
pub fn node(kind, props, child = null) element^ {
    let valid = validate(kind,props)^;
    elements.create(kind,props,c.children(child))
}
pub fn checked(source) element^ => node(c.kind(source),c.props(source),content(source))^
