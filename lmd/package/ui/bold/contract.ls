import c: lambda.ui.core.component
import theme: .tokens
import elements: .elements

let fields = {
    page:["tokens","locale","direction"], 'config-provider':["tokens","direction","disabled"],
    button:["variant","size","disabled","block","type","href","target","rel"],
    input:["size","disabled","status","type","value","default_value","placeholder","name","readonly","required","maxlength","autocomplete"],
    'text-area':["size","disabled","status","value","default_value","placeholder","name","readonly","required","maxlength","rows"],
    select:["size","disabled","status","options","value","default_value","name","required"],
    checkbox:["disabled","name","value","checked","default_checked"],
    radio:["disabled","name","value","checked","default_checked"],
    switch:["disabled","checked","default_checked"],
    title:["level"], text:[], paragraph:[], link:["href","target","rel"],
    card:["description","footer"], badge:["variant"], alert:["status"],
    divider:[], flex:["direction","gap","align","justify","wrap"], space:["direction","gap","align","justify","wrap"],
    form:[], 'form-item':["for","required","help","status"], progress:["percent","status","show_info"]
}
pub fn validate(kind, props) bool^ {
    let known = if (not (kind at fields)) raise c.fail(kind,"component is not implemented","bold") else true
    let names = c.properties(props,[*c.common_props,*fields[kind]],kind,true,"bold")^
    let flags = c.boolean_props(props,["disabled","block","readonly","required","checked","default_checked","wrap","show_info"],kind,"bold")^
    let strings = [for (key in ["id","class","style","label","name","placeholder","autocomplete","href","target","rel","for","help","locale"]
        where c.has(props,key) and not (props[key] is string)) key];
    if (len(strings) > 0) raise c.fail(kind,strings[0] ++ " must be a string","bold")
    else if (not c.enum_valid(props.size,["small","middle","large"])^) raise c.fail(kind,"invalid size","bold")
    else if (not c.enum_valid(props.variant,if (kind == 'badge') ["default","secondary","outline","destructive"]
        else ["default","secondary","outline","destructive","ghost","link"])^) raise c.fail(kind,"invalid variant","bold")
    else if (not c.enum_valid(props.status,["success","warning","error","info"])^) raise c.fail(kind,"invalid status","bold")
    else if (not c.enum_valid(props.direction,if (kind == 'page' or kind == 'config-provider') ["ltr","rtl"] else ["horizontal","vertical"])^)
        raise c.fail(kind,"invalid direction","bold")
    else if (not c.enum_valid(props.align,["stretch","flex-start","center","flex-end","baseline"])^ or
        not c.enum_valid(props.justify,["flex-start","center","flex-end","space-between","space-around","space-evenly"])^)
        raise c.fail(kind,"invalid flex alignment","bold")
    else if (c.has(props,"value") and c.has(props,"default_value")) raise c.fail(kind,"value and default_value are mutually exclusive","bold")
    else if (c.has(props,"checked") and c.has(props,"default_checked")) raise c.fail(kind,"checked and default_checked are mutually exclusive","bold")
    else if (props.rows != null and (not (props.rows is int) or props.rows < 1)) raise c.fail(kind,"rows must be a positive int","bold")
    else if (props.maxlength != null and (not (props.maxlength is int) or props.maxlength < 0)) raise c.fail(kind,"maxlength must be a nonnegative int","bold")
    else if (kind == 'title' and c.has(props,"level") and (not (props.level is int) or props.level < 1 or props.level > 6))
        raise c.fail(kind,"level must be an int from 1 to 6","bold")
    else if (kind == 'button' and not c.enum_valid(props.type,["button","submit","reset"])^) raise c.fail(kind,"invalid native button type","bold")
    else if (kind == 'input' and not c.enum_valid(props.type,["text","password","search","email","url","tel"])^) raise c.fail(kind,"unsupported native input type","bold")
    else if (c.has(props,"gap") and not c.css_length(props.gap)) raise c.fail(kind,"gap must be a nonnegative CSS length","bold")
    else if (c.has(props,"percent") and (not c.finite(props.percent) or props.percent < 0 or props.percent > 100)) raise c.fail(kind,"percent must be in [0,100]","bold")
    else if (kind == 'select') c.select_options(props,"bold")^
    else if (kind == 'page' or kind == 'config-provider') (let values = theme.resolve(c.option(props,"tokens",{}))^, true)
    else true
}
pub fn node(kind, props, child = null) element^ {
    let valid = validate(kind,props)^;
    elements.create(kind,props,c.children(child))
}
// direct element authors receive the same diagnostics as constructor callers (S12.1.3).
pub fn checked(source) element^ => node(c.kind(source),c.props(source),content(source))^
