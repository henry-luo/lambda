import c: lambda.ui.core.component
import shared: lambda.ui.core.theme

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
let zero_lengths = ["radius","spacing","surface_radius","shadow_size"]
pub fn resolve(overrides = {}) map^ => shared.skin_resolve(defaults,overrides,lengths,zero_lengths,extra,"clay")^
pub fn variables(overrides = {}, scoped = false) string^ => shared.skin_variables(defaults,overrides,lengths,zero_lengths,extra,"clay",scoped)^
