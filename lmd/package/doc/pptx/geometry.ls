// Saved numeric DrawingML paths lower to SVG without evaluating guide expressions as code (S1.8).
import c: .common
fn coordinate(value,width,height) => c.value(
    {l:0.0,t:0.0,r:width,b:height,w:width,h:height,hc:width/2.0,vc:height/2.0}[value],c.num(value,null))
fn command(node,width,height,bounds) {
    let verb = {moveTo:"M",lnTo:"L",cubicBezTo:"C",quadBezTo:"Q",close:"Z"}[node.tag]
    let points = [for (pt in c.children(node,"pt",c.A))
        [coordinate(c.attr(pt,"x"),width,height),coordinate(c.attr(pt,"y"),width,height)]]
    let count = {moveTo:1,lnTo:1,cubicBezTo:3,quadBezTo:2,close:0}[node.tag];
    if (node.uri!=c.A or verb==null or len(points)!=count or any([for (pt in points) pt[0]==null or pt[1]==null])) null
    else verb ++ join([for (pt in points) string(pt[0]/width*bounds.width) ++ " " ++ string(pt[1]/height*bounds.height)]," ")
}
pub fn paths(geometry,bounds) {
    let paths = c.children(c.child(geometry,"pathLst",c.A),"path",c.A)
    let converted = [for (path in paths) {
        let width = c.num(c.attr(path,"w"),bounds.width*9525.0)
        let height = c.num(c.attr(path,"h"),bounds.height*9525.0)
        let commands = [for (node in c.children(path,null,c.A)) command(node,width,height,bounds)];
        if (width<=0.0 or height<=0.0 or len(commands)==0 or any([for (value in commands) value==null]) or
            not (c.value(c.attr(path,"fill"),"norm") in ["norm","none"])) null
        else {d:join(commands," "),fill:c.attr(path,"fill")!="none",stroke:not (c.attr(path,"stroke") in ["0","false"])}
    }];
    if (len(converted)==0 or any([for (path in converted) path==null])) null else converted
}
