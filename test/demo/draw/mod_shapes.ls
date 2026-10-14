// one contour supplies painting, hit testing and connector boundary anchors.
import geom: lambda.editor.mod_geom

pub let catalog = [
    {id:"rect",label:"Process"},{id:"ellipse",label:"Start / End"},{id:"diamond",label:"Decision"},
    {id:"rounded",label:"Rounded"},{id:"parallelogram",label:"Data"},{id:"trapezoid",label:"Manual input"},
    {id:"hexagon",label:"Preparation"},{id:"triangle",label:"Triangle"},{id:"cylinder",label:"Database"},
    {id:"document",label:"Document"},{id:"note",label:"Note"},{id:"text",label:"Text"}]
pub let kinds = [for (entry in catalog) entry.id]
pub fn title(kind) => [for (entry in catalog where entry.id == kind) entry.label][0]
let polygons = {
    diamond:[[0.5,0],[1,0.5],[0.5,1],[0,0.5]],triangle:[[0.5,0],[1,1],[0,1]],
    hexagon:[[0.18,0],[0.82,0],[1,0.5],[0.82,1],[0.18,1],[0,0.5]],
    parallelogram:[[0.18,0],[1,0],[0.82,1],[0,1]],trapezoid:[[0.2,0],[0.8,0],[1,1],[0,1]],
    note:[[0,0],[0.78,0],[1,0.22],[1,1],[0,1]]}
fn corner_radius(kind,b) => if (kind == "rounded") min(b.width,b.height)/2 else if (kind == "rect") min(8,min(b.width,b.height)/5) else 0
pub fn outline(kind,b) {
    let radius = corner_radius(kind,b)
    let rx = radius/b.width
    let ry = radius/b.height
    let points = if (kind == "ellipse") [for (i in 0 to 47)
        [0.5+0.5*math.cos(i*2*math.pi/48),0.5+0.5*math.sin(i*2*math.pi/48)]]
        else if (radius>0) [for (corner in [[rx,ry,math.pi],[1-rx,ry,math.pi*1.5],[1-rx,1-ry,0],[rx,1-ry,math.pi/2]])
            *[for (i in 0 to 8) [corner[0]+rx*math.cos(corner[2]+i*math.pi/16),corner[1]+ry*math.sin(corner[2]+i*math.pi/16)]]]
        else if (kind == "cylinder") [*[for (i in 0 to 24)
            [0.5+0.5*math.cos(math.pi+i*math.pi/24),0.16+0.16*math.sin(math.pi+i*math.pi/24)]],
            *[for (i in 0 to 24) [0.5+0.5*math.cos(i*math.pi/24),0.84+0.16*math.sin(i*math.pi/24)]]]
        else if (kind == "document") [[0,0],[1,0],*[for (i in 0 to 24)
            [1-i/24,0.82+0.1*math.sin((1-i/24)*2*math.pi)]]]
        else if (polygons[kind] != null) polygons[kind] else [[0,0],[1,0],[1,1],[0,1]];
    [for (p in points) geom.v2(b.x+p[0]*b.width,b.y+p[1]*b.height)]
}
pub fn point_list(points) => join([for (p in points) string(p.x) ++ "," ++ string(p.y)]," ")
pub fn graphic(kind,b,style) =>
    if (kind == "ellipse") <ellipse cx:b.x+b.width/2,cy:b.y+b.height/2,rx:b.width/2,ry:b.height/2,*:style>
    else if (contains(["rect","rounded","text"],kind)) <rect x:b.x,y:b.y,width:b.width,height:b.height,
        rx:corner_radius(kind,b),*:style>
    else <polygon points:point_list(outline(kind,b)),*:style>
pub fn detail(kind,b,style) =>
    if (kind == "cylinder") <path d:"M " ++ string(b.x) ++ " " ++ string(b.y+b.height*0.16) ++
        " A " ++ string(b.width/2) ++ " " ++ string(b.height*0.16) ++ " 0 0 0 " ++ string(b.x+b.width) ++ " " ++ string(b.y+b.height*0.16),*:style,fill:"none">
    else if (kind == "note") <path d:"M " ++ string(b.x+b.width*0.78) ++ " " ++ string(b.y) ++
        " V " ++ string(b.y+b.height*0.22) ++ " H " ++ string(b.x+b.width),*:style,fill:"none">
    else null
fn cross(a,b) => a.x*b.y-a.y*b.x
fn ray_intersection(center,direction,a,b) {
    let edge = geom.v2_sub(b,a)
    let denominator = cross(direction,edge);
    if (abs(denominator)<0.000001) null else (
        let delta = geom.v2_sub(a,center),
        let t = cross(delta,edge)/denominator,
        let u = cross(delta,direction)/denominator,
        if (t>=0 and u>=0 and u<=1) t else null)
}
pub fn anchor(kind,b,direction) {
    let center = geom.rect_center(b)
    let contour = outline(kind,b)
    let intersections = [for (i,p in contour) ray_intersection(center,direction,p,contour[(i+1)%len(contour)])]
    let distances = [for (t in intersections where t != null) t];
    geom.v2_add(center,geom.v2_scale(direction,if (len(distances)>0) min(distances) else 0))
}
