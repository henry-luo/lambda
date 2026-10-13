import c: lambda.ui.core.component
import shared: lambda.ui.core.theme
import base: lambda.ui.dtna.tokens

pub let defaults = {
    primary:"#6b51db", success:"#287b63", warning:"#a56624", error:"#bd4561",
    text:"#202335", text_secondary:"#6e6888", border:"#ded6ed", background:"#f4f0f9",
    surface:"#fcfaff", disabled_background:"#e9e3f1", disabled_text:"#9790a7",
    lavender:"#e3d8fa", mint:"#d7eee5", peach:"#fbe0d5", rose:"#f8dfe6", highlight:"#ffffff", shadow:"#c6badb",
    font_family:"-apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif",
    font_size:14, control_height:42, radius:16, spacing:12, line_height:1.6,
    surface_radius:26, shadow_size:12
}
let lengths = ["font_size","control_height","radius","spacing","surface_radius","shadow_size"]
let extra = ["lavender","mint","peach","rose","highlight","shadow","surface_radius","shadow_size"]
pub fn resolve(overrides = {}) map^ {
    let values = shared.resolve(defaults,overrides,lengths,["radius","spacing","surface_radius","shadow_size"],"clay")^
    let engine = base.resolve(map([for (key,value in values where not contains(extra,c.text(key))) (key,value)]))^;
    {*:engine,*:values}
}
pub fn variables(overrides = {}, scoped = false) string^ {
    let values = resolve(overrides)^
    let engine_overrides = map([for (key,value in overrides where not contains(extra,c.text(key))) (key,value)])
    // keep engine aliases local to the same scope; never reset unrelated inherited tokens.
    let aliases = if (scoped) base.scoped_variables(engine_overrides)^
        else base.variables(map([for (key,value in defaults where not contains(extra,c.text(key))) (key,values[key])]))^;
    let decoration = map([for (key,value in (if (scoped) overrides else values) where contains(extra,c.text(key))) (key,value)]);
    replace(aliases,"--dtna-","--clay-") ++ shared.variables(decoration,lengths,"clay") ++ aliases
}
