import shared: lambda.ui.core.theme

// desktop controls and chrome share one independently scoped palette.
pub let defaults = {
    primary:"#2864ad", text:"#24334b", text_secondary:"#667085", border:"#cbd5e1",
    background:"#eef1f5", surface:"#ffffff", disabled_background:"#f1f3f5", disabled_text:"#9aa3af",
    font_family:"-apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif",
    font_size:13, control_height:28, radius:4, spacing:8, line_height:1.4
}
let lengths = ["font_size","control_height","radius","spacing"]
pub fn resolve(overrides = {}) map^ => shared.resolve(defaults,overrides,lengths,["radius","spacing"],"workspace")^
pub fn variables(overrides = {}) string^ => shared.variables(resolve(overrides)^,lengths,"lambda-app")
