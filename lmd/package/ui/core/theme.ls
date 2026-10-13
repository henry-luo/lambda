import c: .component

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
