import shared: lambda.ui.core.theme

pub let defaults = {
    ink:"#252525", primary:"#5546a3", secondary:"#e4dcfa", accent:"#fff0a6",
    success:"#d9efdf", warning:"#fff0bd", error:"#f8d9dc", info:"#dceaf8",
    text:"#252525", text_secondary:"#68646b", background:"#faf9f6", surface:"#ffffff",
    disabled_background:"#eeece8", disabled_text:"#8a8782",
    font_family:"Chalkboard SE, Comic Sans MS, cursive", font_size:16, line_height:1.5,
    control_height:42, stroke_width:1.2, spacing:12
}
let lengths = ["font_size","control_height","stroke_width","spacing"]
pub fn resolve(overrides = {}) map^ => shared.resolve(defaults,overrides,lengths,["spacing"],"wired")^
pub fn variables(overrides = {}, scoped = false) string^ {
    let values = resolve(overrides)^;
    shared.variables(if (scoped) overrides else values,lengths,"wired")
}
