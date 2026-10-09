import c: lambda.ui.core.component

pub fn validate(kind, props) bool^ {
    let positive = [for (key in ["dimension"] where props[key] != null and
        (not c.finite(props[key]) or props[key] <= 0)) key]
    let nonnegative = [for (key in ["gap","gutter","count","overflow_count","current"] where props[key] != null and
        (not c.finite(props[key]) or props[key] < 0)) key];
    if (len(positive) > 0) raise c.fail(kind,"invalid positive dimension " ++ positive[0])
    else if (len(nonnegative) > 0) raise c.fail(kind,"invalid nonnegative dimension " ++ nonnegative[0])
    else if (props.rows != null and (not (props.rows is int) or props.rows < 1)) raise c.fail(kind,"rows must be a positive int")
    else if (props.maxlength != null and (not (props.maxlength is int) or props.maxlength < 0)) raise c.fail(kind,"maxlength must be a nonnegative int")
    else if (kind == 'title' and props.level != null and (not (props.level is int) or props.level < 1 or props.level > 5)) raise c.fail(kind,"level must be an int from 1 to 5")
    else if (kind == 'col' and props.span != null and (not (props.span is int) or props.span < 1 or props.span > 24)) raise c.fail(kind,"span must be an int from 1 to 24")
    else if (props.percent != null and not c.finite(props.percent)) raise c.fail(kind,"percent must be finite")
    else if (kind == 'button' and not c.enum_valid(props.type,["button","submit","reset"])^) raise c.fail(kind,"invalid native button type")
    else if (kind == 'button' and not c.enum_valid(props.shape,["default","circle","round"])^) raise c.fail(kind,"invalid button shape")
    else if (kind == 'input' and not c.enum_valid(props.type,["text","password","search","email","url","tel"])^) raise c.fail(kind,"unsupported native input type")
    else if (not c.enum_valid(props.direction,if (kind == 'config-provider' or kind == 'page') ["ltr","rtl"] else ["horizontal","vertical"])^) raise c.fail(kind,"invalid direction")
    else if (kind == 'select') select_options(props)^
    else true
}
fn select_options(props) bool^ {
    if (not (props.options is array)) raise c.fail("select","options must be an array")
    else (
        let bad = [for (option in props.options where not (option is map) or
            not (option.value is string or option.value is symbol or option.value is int or option.value is bool) or
            (option.disabled != null and not (option.disabled is bool))) option],
        let names = [for (option in props.options) c.text(option.value)],
        if (len(bad) > 0) raise c.fail("select","options need scalar values and boolean disabled flags")
        else if (len(unique(names)) != len(names)) raise c.fail("select","option values must have distinct string representations")
        else true
    )
}
