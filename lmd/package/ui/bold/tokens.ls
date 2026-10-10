import c: lambda.ui.core.component

// hard shadows and ink borders define the family independently of its accent colors.
pub let defaults = {
    primary:"#facc15", secondary:"#c4b5fd", accent:"#fda4af",
    success:"#86efac", warning:"#fde68a", error:"#fca5a5", info:"#93c5fd",
    text:"#171717", text_secondary:"#525252", border:"#171717", shadow:"#171717",
    background:"#fffdf5", surface:"#ffffff", disabled_background:"#e5e5e5", disabled_text:"#737373",
    font_family:"Arial, Helvetica, sans-serif", font_size:16, line_height:1.5,
    control_height:44, radius:4, border_width:2, shadow_offset:4, spacing:12
}
let lengths = ["font_size","control_height","radius","border_width","shadow_offset","spacing"]
pub fn resolve(overrides = {}) map^ {
    let names = c.properties(overrides,[for (key,value in defaults) string(key)],"tokens",false,"bold")^
    let values = {*:defaults,*:overrides}
    let colors = [for (key,value in defaults where value is string and starts_with(value,"#")) string(key)];
    if (not all([for (key in colors) c.hex_color(values[key])])) raise c.fail("tokens","colors must be #rrggbb","bold")
    else if (not all([for (key in [*lengths,"line_height"]) c.finite(values[key]) and
        (if (contains(["radius","shadow_offset","spacing"],key)) values[key] >= 0 else values[key] > 0)]))
        raise c.fail("tokens","dimensions must be finite and positive (radius, shadow_offset and spacing may be zero)","bold")
    else if (not c.css_color(values.font_family)) raise c.fail("tokens","font_family must be a CSS font-family value","bold")
    else values
}
pub fn variables(overrides = {}, scoped = false) string^ {
    let values = resolve(overrides)^;
    // nested providers emit supplied fields only, retaining outer customizations.
    join([for (key,value in (if (scoped) overrides else values))
        "--bold-" ++ replace(string(key),"_","-") ++ ":" ++ string(value) ++
        (if (contains(lengths,string(key))) "px" else "") ++ ";"],"")
}
