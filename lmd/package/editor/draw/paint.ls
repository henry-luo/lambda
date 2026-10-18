// SVG paint servers remain part of the native document and standalone export.
import model: .model
import shapes: .shapes

pub fn stroke(node) {
    let weight = model.attr(node,"stroke-width")
    let kind = model.attr(node,"stroke-style")
    let units = {dashed:[4,3],dotted:[0,2.5],'dash-dot':[4,2,0,2]}
    let dash = if (units[kind] == null) "" else join([for (unit in units[kind]) string(unit*max(1,weight))]," ");
    {stroke:if (kind == "none" or weight == 0) "none" else model.attr(node,"stroke"),'stroke-width':weight,
        'stroke-dasharray':dash,'stroke-linecap':if (kind == "dotted" or kind == "dash-dot") "round" else "butt",'stroke-linejoin':"round"}
}
let pattern_paths = {diagonal:"M -2 2 L 2 -2 M 0 8 L 8 0 M 6 10 L 10 6",
    crosshatch:"M 0 0 L 8 8 M 0 8 L 8 0",grid:"M 8 0 H 0 V 8",horizontal:"M 0 4 H 8"}
pub fn pattern(node,prefix="") => <pattern id:prefix ++ "fill-" ++ model.id(node),width:8,height:8,patternUnits:"userSpaceOnUse",
    <rect width:8,height:8,fill:model.attr(node,"fill")>
    ; (if (model.attr(node,"pattern") == "dots") <circle cx:2,cy:2,r:1,fill:model.attr(node,"pattern-color")>
        else <path d:pattern_paths[model.attr(node,"pattern")],fill:"none",stroke:model.attr(node,"pattern-color"),'stroke-width':0.8>)>
pub fn graphic(node,prefix="") {
    let style = {*:stroke(node),fill:if (model.attr(node,"pattern") == "solid") model.attr(node,"fill") else "url(#" ++ prefix ++ "fill-" ++ model.id(node) ++ ")"};
    shapes.graphic(model.attr(node,"variant"),model.box(node),style,model.attr(node,"descriptor"))
}
// previews and diagram connectors share the same marker geometry.
fn arrow_marker(kind,target) {
    let paths = {triangle:"M 0 0 L 12 6 L 0 12 Z",open:"M 1 1 L 11 6 L 1 11",
        diamond:"M 0 6 L 6 0 L 12 6 L 6 12 Z",bar:"M 11 0 V 12"};
    if (kind == "none") null else <marker id:target,viewBox:"0 0 12 12",
        refX:12,refY:6,markerWidth:14,markerHeight:14,markerUnits:"userSpaceOnUse",orient:"auto-start-reverse",overflow:"visible",
        (if (kind == "circle") <circle cx:6,cy:6,r:5,fill:"context-stroke">
        else <path d:paths[kind],fill:if (kind == "open" or kind == "bar") "none" else "context-stroke",
            stroke:"context-stroke",'stroke-width':1.4,'stroke-linecap':"round",'stroke-linejoin':"round">)>
}
pub fn marker(node,side,prefix="") => arrow_marker(model.attr(node,side ++ "-arrow"),prefix ++ "arrow-" ++ side ++ "-" ++ model.id(node))
pub fn arrow_preview(kind,side,target) => <svg class:"arrow-preview",width:58,height:22,viewBox:"0 0 64 24",'aria-hidden':"true",
    <defs (arrow_marker(kind,target))>
    <path d:"M 10 12 H 54",fill:"none",stroke:"currentColor",'stroke-width':1.6,
        ["marker-" ++ side]:if (kind == "none") "none" else "url(#" ++ target ++ ")">>
pub fn text(node,b,label,edge=false) {
    let lines = split(label,"\n")
    let size = model.attr(node,"font-size")
    let align = model.attr(node,"text-align")
    let x = if (align == "left") b.x+12 else if (align == "right") b.x+b.width-12 else b.x+b.width/2;
    <g *[for (i,line in lines) <text x:if (edge) b.x+b.width/2 else x,y:b.y+b.height/2+size*0.35+(i-(len(lines)-1)/2)*size*1.3,
        'font-family':"Arial, sans-serif",'font-size':size,'font-weight':model.attr(node,"font-weight"),
        'text-anchor':if (edge or align == "center") "middle" else if (align == "left") "start" else "end",fill:model.attr(node,"text-color"),line>]>
}
