import c: .component
import engine: lambda.ui.dtna.tokens

// source themes share token validation and inheritance, while owning their palettes.
pub fn resolve(defaults, overrides, lengths, zero_lengths, family) map^ {
    let names = c.properties(overrides,[for (key,value in defaults) string(key)],"tokens",false,family)^
    let values = {*:defaults,*:overrides}
    let colors = [for (key,value in defaults where value is string and starts_with(value,"#")) string(key)];
    if (not all([for (key in colors) c.hex_color(values[key])])) raise c.fail("tokens","colors must be #rrggbb",family)
    else if (not all([for (key in [*lengths,"line_height"]) c.finite(values[key]) and
        (if (contains(zero_lengths,key)) values[key] >= 0 else values[key] > 0)]))
        raise c.fail("tokens","dimensions must be finite and positive (" ++ join(zero_lengths,", ") ++ " may be zero)",family)
    else if (not c.css_color(values.font_family)) raise c.fail("tokens","font_family must be a CSS font-family value",family)
    else values
}
pub fn variables(values, lengths, family) string => join([for (key,value in values)
    "--" ++ family ++ "-" ++ replace(string(key),"_","-") ++ ":" ++ string(value) ++
    (if (contains(lengths,string(key))) "px" else "") ++ ";"],"")

fn engine_values(values, extra) map => map([for (key,value in values where not contains(extra,c.text(key))) (key,value)])
pub fn skin_resolve(defaults, overrides, lengths, zero_lengths, extra, family) map^ {
    let values = resolve(defaults,overrides,lengths,zero_lengths,family)^
    let derived = engine.resolve(engine_values(values,extra)) ^ {raise error(replace(^.message,"dtna:",family ++ ":"))};
    {*:derived,*:values}
}
pub fn skin_variables(defaults, overrides, lengths, zero_lengths, extra, family, scoped = false) string^ {
    let values = skin_resolve(defaults,overrides,lengths,zero_lengths,extra,family)^
    let aliases = if (scoped) engine.scoped_variables(engine_values(overrides,extra))^
        else engine.variables(map([for (key,value in defaults where not contains(extra,c.text(key))) (key,values[key])]))^
    let decoration = map([for (key,value in (if (scoped) overrides else values) where contains(extra,c.text(key))) (key,value)]);
    replace(aliases,"--dtna-","--" ++ family ++ "-") ++ variables(decoration,lengths,family) ++ aliases
}
pub fn direction(props, owner, family) bool^ {
    if (not c.enum_valid(props.direction,["ltr","rtl"])^) raise c.fail(owner,"invalid direction",family) else true
}
pub fn provider(props, family, resolver) bool^ {
    let names = c.properties(props,[*c.common_props,"tokens","direction","disabled"],"config-provider",true,family)^
    let flags = c.boolean_props(props,["disabled"],"config-provider",family)^
    let valid = direction(props,"config-provider",family)^
    let tokens = resolver(c.option(props,"tokens",{}))^;
    true
}
