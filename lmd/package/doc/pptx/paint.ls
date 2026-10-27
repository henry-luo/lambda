// Office fills become ordinary CSS paint; unknown operations retain an explicit diagnostic.
import c: .common
import theme: lambda.doc.docx.theme
import images: .images
import math

fn gradient(node,ctx,bounds,placeholder) {
    let stops = sort([for (stop in c.children(c.child(node,"gsLst",c.A),"gs",c.A))
        {position:c.num(c.attr(stop,"pos"),-1.0)/1000.0,color:theme.drawing_color(stop,ctx.theme,placeholder,true)}],{by:(stop)=>stop.position})
    let path = c.child(node,"path",c.A)
    let line = c.child(node,"lin",c.A)
    let rect = c.child(path,"fillToRect",c.A)
    let tile = c.child(node,"tileRect",c.A)
    let edges = [for (side in ["l","t","r","b"]) c.num(c.attr(rect,side))/1000.0]
    let angle = c.num(c.attr(line,"ang"))/60000.0
    let radians = angle*math.pi/180.0
    let scaled = c.truth(c.value(c.attr(line,"scaled"),"1"))
    let direction = if (scaled) math.atan2(bounds.height*math.sin(radians),bounds.width*math.cos(radians))*180.0/math.pi else angle
    let colors = join([for (stop in stops) stop.color ++ " " ++ string(stop.position) ++ "%"],",");
    if (len(stops)<2 or any([for (stop in stops) stop.color==null or stop.position<0.0 or stop.position>100.0]) or
        any([for (side in ["l","t","r","b"]) c.num(c.attr(tile,side))!=0.0]) or
        (path!=null and (c.attr(path,"path")!="circle" or abs(edges[0]+edges[2]-100.0)>0.001 or abs(edges[1]+edges[3]-100.0)>0.001))) null
    else if (path!=null) "radial-gradient(ellipse farthest-corner at " ++ string(edges[0]) ++ "% " ++ string(edges[1]) ++ "%," ++ colors ++ ")"
    else "linear-gradient(" ++ string(direction+90.0) ++ "deg," ++ colors ++ ")"
}
pub fn fill(props,style,ctx,bounds) map^ {
    let direct = [for (node in c.children(props,null,c.A) where node.tag in ["solidFill","noFill","gradFill","pattFill","blipFill","grpFill"]) node][0]
    let ref = c.child(style,"fillRef",c.A)
    let index = int(c.num(c.attr(ref,"idx")))
    let node = c.value(direct,if (index>0) ctx.theme.fills[index-1] else null)
    let placeholder = theme.drawing_color(ref,ctx.theme,null,true)
    let solid = if (node.tag=="solidFill") theme.drawing_color(node,ctx.theme,placeholder,true) else "transparent";
    if (node==null or node.tag=="noFill") {color:"transparent",style:"",diagnostics:[]}
    else if (node.tag=="solidFill") {color:c.value(solid,"transparent"),style:"",diagnostics:
        if (solid==null) [c.warning(ctx,"unsupported-color","A shape color transform could not be reproduced")] else []}
    else if (node.tag=="gradFill") {
        let value = gradient(node,ctx,bounds,placeholder);
        {color:"transparent",style:if (value==null) "" else "background:" ++ value ++ ";",diagnostics:
            if (value==null) [c.warning(ctx,"unsupported-gradient","A gradient is outside the supported linear/radial profile")] else []}
    } else if (node.tag=="blipFill") {
        let blip = c.child(node,"blip",c.A)
        let image = images.embedded(blip,ctx,bounds)^
        let value = if (image.src==null) null else images.background(node,image.src,bounds);
        {color:"transparent",style:c.value(value,""),diagnostics:image.diagnostics ++
            (if (image.src!=null and value==null) [c.warning(ctx,"unsupported-image-fill","A tiled or invalid picture fill was omitted")] else [])}
    } else {color:"transparent",style:"",diagnostics:[c.warning(ctx,"unsupported-fill","A shape fill is outside the import profile")]}
}
