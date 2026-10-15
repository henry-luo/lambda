import c: .component
import binding: .binding

pub let fields = {
    page:["tokens","locale","direction"], 'config-provider':["tokens","direction","disabled"],
    button:["variant","size","disabled","block","type","href","target","rel"],
    input:["size","disabled","status","type","value","default_value","placeholder","name","readonly","required","maxlength","autocomplete","on_change"],
    'text-area':["size","disabled","status","value","default_value","placeholder","name","readonly","required","maxlength","rows","on_change"],
    select:["size","disabled","status","options","value","default_value","name","required","on_change"],
    checkbox:["disabled","name","value","checked","default_checked","on_change"],
    radio:["disabled","name","value","checked","default_checked","on_change"],
    switch:["disabled","checked","default_checked","on_change"],
    title:["level"], text:[], paragraph:[], link:["href","target","rel"],
    card:["description","footer"], badge:["variant"], alert:["status"],
    divider:[], flex:["direction","gap","align","justify","wrap"], space:["direction","gap","align","justify","wrap"],
    form:[], 'form-item':["for","required","help","status"], progress:["percent","status","show_info"]
}
// compact source themes share native prop contracts and ownership checks.
pub fn validate(kind, props, fields, family, variants, badge_variants) bool^ {
    let known = if (not (kind at fields)) raise c.fail(kind,"component is not implemented",family) else true
    let names = c.properties(props,[*c.common_props,*fields[kind]],kind,true,family)^
    let flags = c.boolean_props(props,["disabled","block","readonly","required","checked","default_checked","wrap","show_info"],kind,family)^
    let action = binding.validate(props.on_change)^
    let strings = [for (key in ["id","class","style","label","name","placeholder","autocomplete","href","target","rel","for","help","locale"]
        where c.has(props,key) and not (props[key] is string)) key];
    if (len(strings) > 0) raise c.fail(kind,strings[0] ++ " must be a string",family)
    else if (not c.enum_valid(props.size,["small","middle","large"])^) raise c.fail(kind,"invalid size",family)
    else if (not c.enum_valid(props.variant,if (kind == 'badge') badge_variants
        else variants)^) raise c.fail(kind,"invalid variant",family)
    else if (not c.enum_valid(props.status,["success","warning","error","info"])^) raise c.fail(kind,"invalid status",family)
    else if (not c.enum_valid(props.direction,if (kind == 'page' or kind == 'config-provider') ["ltr","rtl"] else ["horizontal","vertical"])^)
        raise c.fail(kind,"invalid direction",family)
    else if (not c.enum_valid(props.align,["stretch","flex-start","center","flex-end","baseline"])^ or
        not c.enum_valid(props.justify,["flex-start","center","flex-end","space-between","space-around","space-evenly"])^)
        raise c.fail(kind,"invalid flex alignment",family)
    else if (c.has(props,"value") and c.has(props,"default_value")) raise c.fail(kind,"value and default_value are mutually exclusive",family)
    else if (c.has(props,"checked") and c.has(props,"default_checked")) raise c.fail(kind,"checked and default_checked are mutually exclusive",family)
    else if (props.rows != null and (not (props.rows is int) or props.rows < 1)) raise c.fail(kind,"rows must be a positive int",family)
    else if (props.maxlength != null and (not (props.maxlength is int) or props.maxlength < 0)) raise c.fail(kind,"maxlength must be a nonnegative int",family)
    else if (kind == 'title' and c.has(props,"level") and (not (props.level is int) or props.level < 1 or props.level > 6))
        raise c.fail(kind,"level must be an int from 1 to 6",family)
    else if (kind == 'button' and not c.enum_valid(props.type,["button","submit","reset"])^) raise c.fail(kind,"invalid native button type",family)
    else if (kind == 'input' and not c.enum_valid(props.type,["text","password","search","email","url","tel"])^) raise c.fail(kind,"unsupported native input type",family)
    else if (c.has(props,"gap") and not c.css_length(props.gap)) raise c.fail(kind,"gap must be a nonnegative CSS length",family)
    else if (c.has(props,"percent") and (not c.finite(props.percent) or props.percent < 0 or props.percent > 100)) raise c.fail(kind,"percent must be in [0,100]",family)
    else if (kind == 'select') c.select_options(props,family)^
    else true
}
