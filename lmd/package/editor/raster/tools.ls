import model: .model
import colors: lambda.slide.color

pub let catalog = [
    {id:"move",label:"Move",key:"v",icon:"↖"},{id:"pan",label:"Hand",key:"h",icon:"✥"},
    {id:"rectangle",label:"Marquee",key:"m",icon:"▧"},{id:"ellipse",label:"Ellipse",key:"o",icon:"◯"},
    {id:"lasso",label:"Lasso",key:"l",icon:"⌁"},{id:"wand",label:"Wand",key:"w",icon:"✧"},
    {id:"brush",label:"Brush",key:"b",icon:"✎"},{id:"erase",label:"Erase",key:"e",icon:"▱"},
    {id:"bucket",label:"Fill",key:"f",icon:"▣"},{id:"gradient",label:"Gradient",key:"g",icon:"▥"},
    {id:"eyedropper",label:"Sample",key:"i",icon:"⌖"}
]
pub fn configure(st,command) any^ {
    let tool = model.value(command.tool,st.tool)
    let mode = model.value(command.selection_mode,st.selection_mode)
    let gradient = model.value(command.gradient_kind,st.gradient_kind)
    let tolerance = model.value(command.tolerance,st.tolerance)
    let requested = model.value(command.brush,{})
    let limits = [{key:"size",low:0.5,high:4096},{key:"hardness",low:0,high:1},
        {key:"spacing",low:0.01,high:2},{key:"flow",low:0,high:1},{key:"opacity",low:0,high:1}]
    let brush = {*:st.brush,*:requested}
    if (not (tool in [for (entry in catalog) entry.id]) or not (mode in ["replace","add","subtract","intersect"]) or
        not (gradient in ["linear","radial"]) or not model.finite(tolerance) or tolerance<0 or tolerance>1 or
        not (requested is map) or any([for (key,value in requested) not (string(key) in [for (limit in limits) limit.key])]) or
        not all([for (limit in limits) model.finite(brush[limit.key]) and brush[limit.key]>=limit.low and brush[limit.key]<=limit.high]))
        raise error("Raster: invalid tool setting")
    else {*:st,tool:tool,brush:brush,selection_mode:mode,tolerance:tolerance,gradient_kind:gradient,
        foreground:colors.parse(model.value(command.foreground,st.foreground),"raster.foreground")^,
        background:colors.parse(model.value(command.background,st.background),"raster.background")^}
}
