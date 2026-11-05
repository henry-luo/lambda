// dtna owns its tag construction while core owns shared property validation.
import c: lambda.ui.core.component
import elements: .elements

pub fn node(kind, props, child, allowed = []) element^ {
    let names = c.properties(props,[*c.common_props,*allowed],kind,true)^
    let values = c.validate(kind,props)^;
    elements.create(kind,props,c.children(child))
}

