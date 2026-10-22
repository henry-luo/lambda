import model: .model
import colors: lambda.slide.color
import canvas: .canvas

pub let catalog = [
    {id:"move",label:"Move",key:"v",icon:"↖"},{id:"pan",label:"Hand",key:"h",icon:"✥"},
    {id:"rectangle",label:"Marquee",key:"m",icon:"▧"},{id:"ellipse",label:"Ellipse",key:"o",icon:"◯"},
    {id:"lasso",label:"Lasso",key:"l",icon:"⌁"},{id:"wand",label:"Wand",key:"w",icon:"✧"},
    {id:"brush",label:"Brush",key:"b",icon:"✎"},{id:"erase",label:"Erase",key:"e",icon:"▱"},
    {id:"bucket",label:"Fill",key:"f",icon:"▣"},{id:"gradient",label:"Gradient",key:"g",icon:"▥"},
    {id:"transform",label:"Transform",key:"t",icon:"⤢"},{id:"crop",label:"Crop",key:"c",icon:"⌗"},
    {id:"clone",label:"Clone",key:"s",icon:"♧"},{id:"heal",label:"Heal",key:"j",icon:"✚"},
    {id:"dodge",label:"Dodge",key:"d",icon:"☀"},{id:"burn",label:"Burn",key:"k",icon:"◕"},
    {id:"sponge",label:"Sponge",key:"r",icon:"▨"},{id:"blur",label:"Blur",key:"u",icon:"◉"},
    {id:"sharpen",label:"Sharpen",key:"a",icon:"△"},
    {id:"eyedropper",label:"Sample",key:"i",icon:"⌖"},{id:"straighten",label:"Straighten",key:"q",icon:"∠"}
]
pub fn configure(st,command) any^ {
    let tool = model.value(command.tool,st.tool)
    let mode = model.value(command.selection_mode,st.selection_mode)
    let gradient = model.value(command.gradient_kind,st.gradient_kind)
    let tolerance = model.value(command.tolerance,st.tolerance)
    let requested = model.value(command.brush,{})
    let straighten = model.value(command.straighten_axis,st.straighten_axis)
    let aspect = model.value(command.crop_aspect,st.crop_aspect)
    let custom = model.value(command.crop_ratio,st.crop_ratio)
    let checked_ratio = canvas.crop_ratio(st.doc,"custom",custom)^
    let limits = [{key:"size",low:0.5,high:4096},{key:"hardness",low:0,high:1},
        {key:"spacing",low:0.01,high:2},{key:"flow",low:0,high:1},{key:"opacity",low:0,high:1}]
    let brush = {*:st.brush,*:requested}
    if (not (tool in [for (entry in catalog) entry.id]) or not (mode in ["replace","add","subtract","intersect"]) or
        not (gradient in ["linear","radial"]) or not (straighten in ["horizontal","vertical"]) or
        not (aspect in [for (p in canvas.crop_presets) p.id]) or not model.finite(tolerance) or tolerance<0 or tolerance>1 or
        not (requested is map) or any([for (key,value in requested) not (string(key) in [for (limit in limits) limit.key])]) or
        not all([for (limit in limits) model.finite(brush[limit.key]) and brush[limit.key]>=limit.low and brush[limit.key]<=limit.high]))
        raise error("Raster: invalid tool setting")
    else {*:st,tool:tool,crop:if (tool == st.tool and aspect == st.crop_aspect and custom == st.crop_ratio) st.crop else null,crop_aspect:aspect,crop_ratio:custom,
        brush:brush,selection_mode:mode,tolerance:tolerance,gradient_kind:gradient,straighten_axis:straighten,
        foreground:colors.parse(model.value(command.foreground,st.foreground),"raster.foreground")^,
        background:colors.parse(model.value(command.background,st.background),"raster.background")^}
}
