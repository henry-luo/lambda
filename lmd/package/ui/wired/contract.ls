import c: lambda.ui.core.component
import shared: lambda.ui.core.contract
import theme: .tokens
import elements: .elements

let fields = {*:shared.fields,
    button:[*shared.fields.button,"elevation","seed"],
    card:[*shared.fields.card,"elevation","seed"],
    input:[*shared.fields.input,"seed"], 'text-area':[*shared.fields['text-area'],"seed"],
    select:[*shared.fields.select,"seed"], badge:[*shared.fields.badge,"seed"],
    alert:[*shared.fields.alert,"seed"], divider:["seed"], progress:[*shared.fields.progress,"seed"]}
pub fn validate(kind, props) bool^ {
    let valid = shared.validate(kind,props,fields,"wired",["default","primary","filled","text"],["default","filled","primary"])^;
    if (c.has(props,"elevation") and (not (props.elevation is int) or props.elevation < 1 or props.elevation > 5))
        raise c.fail(kind,"elevation must be an int from 1 to 5","wired")
    else if (c.has(props,"seed") and (not (props.seed is int) or props.seed < 0 or props.seed > 2147483646))
        raise c.fail(kind,"seed must be an int in [0,2147483646]","wired")
    else if (kind == 'page' or kind == 'config-provider') (let values = theme.resolve(c.option(props,"tokens",{}))^,true)
    else true
}
pub fn node(kind, props, child = null) element^ {
    let valid = validate(kind,props)^;
    elements.create(kind,props,c.children(child))
}
pub fn checked(source) element^ => node(c.kind(source),c.props(source),content(source))^
