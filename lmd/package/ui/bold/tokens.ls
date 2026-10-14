import shared: lambda.ui.core.theme

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
pub fn resolve(overrides = {}) map^ => shared.resolve(defaults,overrides,lengths,["radius","shadow_offset","spacing"],"bold")^
pub fn variables(overrides = {}, scoped = false) string^ {
    let values = resolve(overrides)^;
    // nested providers emit supplied fields only, retaining outer customizations.
    shared.variables(if (scoped) overrides else values,lengths,"bold")
}
