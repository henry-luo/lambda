import c: lambda.ui.core.component
import locale: .locale

// format decimal text directly so large integers and decimal values never pass through float.
fn numeric_text(value) {
    let raw = c.text(value)
    let body = if (starts_with(raw,"-")) slice(raw,1,len(raw)) else raw
    let cells = split(body,".");
    len(body) > 0 and len(cells) <= 2 and all([for (cell in cells) len(cell) > 0 and
        all([for (index in 0 to (len(cell)-1)) contains("0123456789",slice(cell,index,index+1))])])
}
pub fn number_parts(value, props) {
    let raw = c.text(value)
    let negative = starts_with(raw,"-")
    let cells = split(if (negative) slice(raw,1,len(raw)) else raw,".")
    let whole = cells[0]
    let fraction = if (len(cells) > 1) cells[1] else ""
    let digits = c.option(props,"precision",len(fraction))
    let padded = fraction ++ join([for (index in len(fraction) to (digits-1)) "0"],"")
    let grouped = join([for (index in 0 to (len(whole)-1))
        (if (index > 0 and (len(whole)-index)%3 == 0) c.option(props,"group_separator",",") else "") ++ slice(whole,index,index+1)],"");
    {whole:(if (negative) "-" else "") ++ grouped,
        fraction:if (digits == 0) "" else c.option(props,"decimal_separator",".") ++ slice(padded,0,digits)}
}
pub fn descriptor(props) element^ {
    let flags = c.boolean_props(props,["loading"],"statistic")^
    let messages = locale.resolve(c.option(props,"locale","en-US"))^;
    if (props.precision != null and (not (props.precision is int) or props.precision < 0 or props.precision > 100))
        raise c.fail("statistic","precision must be an int from 0 to 100")
    else if (not all([for (key in ["group_separator","decimal_separator"] where props[key] != null) props[key] is string]))
        raise c.fail("statistic","separators must be strings")
    else if (props.formatter != null and not (props.formatter is fn)) raise c.fail("statistic","formatter must be a pure function")
    else if (props.value != null and not (props.value is number or props.value is string)) raise c.fail("statistic","value must be a number or string")
    else c.node('statistic',props,null,["value","prefix","suffix","precision","group_separator","decimal_separator","formatter","loading","value_style","locale"])^
}
fn formatted(props) {
    let value = c.option(props,"value",0);
    if (props.formatter is fn) c.render(props.formatter(value))
    else if (numeric_text(value)) (
        let parts = number_parts(value,props),
        [<span class:"dtna-statistic-integer",parts.whole>,if (parts.fraction != "") <span class:"dtna-statistic-fraction",parts.fraction> else null])
    else c.text(value)
}
view dtna_statistic: <dtna kind:'statistic'> {
    let p = ~.props;
    <div *:c.styled(~),["aria-busy"]:if (p.loading) "true" else null,
        *[if (p.title != null) <div class:"dtna-statistic-title",c.render(p.title)> else null,
        if (p.loading) <span class:"dtna-statistic-placeholder",["aria-hidden"]:"true"> else
        <div class:"dtna-statistic-value",style:p.value_style,*[
            if (p.prefix != null) <span class:"dtna-statistic-prefix",c.render(p.prefix)> else null,
            <span class:"dtna-statistic-number",*c.children(formatted(p))>,
            if (p.suffix != null) <span class:"dtna-statistic-suffix",c.render(p.suffix)> else null]>]>
}
pub let css = "
.dtna-statistic-title{color:var(--dtna-text-secondary);margin-bottom:4px}.dtna-statistic-value{font-size:24px}
.dtna-statistic-number,.dtna-statistic-prefix,.dtna-statistic-suffix{display:inline-block}.dtna-statistic-number{direction:ltr}.dtna-statistic-prefix{margin-inline-end:4px}.dtna-statistic-suffix{margin-inline-start:4px}
.dtna-statistic-placeholder{display:inline-block;height:28px;width:100%;background:var(--dtna-disabled-background);border-radius:4px}
"
