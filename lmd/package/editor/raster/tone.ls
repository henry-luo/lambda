// S9.1.2 / S12.1.3: tone-curve drafts are plain data until an effect command applies them.
import model: .model
import sequence: lambda.editor.mod_doc
import img: lambda.image

pub let channels = ["rgb","r","g","b"]
pub let identity = [[0,0],[1,1]]
// reuse the processing kernel for the smooth graph, keeping preview and export in agreement.
pub fn graph_points(curves,channel,mode="linear") any^ {
    if (mode == "linear") points(curves,channel)
    else {
        let ramp = img.from_array(reshape([for (x in 0 to 255) for (c in 0 to 3) u8(if (c == 3) 255 else x)],[1,256,4]))^
        let mapped = img.adjust(ramp,{curves:{rgb:points(curves,channel)},curve_mode:mode})^;
        [for (x in 0 to 255) [x/255,mapped.r[0,x]/255]]
    }
}
pub let levels = [
    {key:"black",label:"Input black (0–255)",value:0,scale:255},
    {key:"white",label:"Input white (0–255)",value:1,scale:255},
    {key:"gamma",label:"Midtone gamma",value:1,scale:1},
    {key:"output_black",label:"Output black (0–255)",value:0,scale:255},
    {key:"output_white",label:"Output white (0–255)",value:1,scale:255}]
pub fn points(curves,channel) => model.value(curves[channel],identity)
pub fn checked(points) any^ {
    if (not (points is array) or len(points)<2 or len(points)>32 or
        not all([for (p in points) p is array and len(p)==2 and all([for (n in p) model.finite(n) and n>=0 and n<=1])]))
        raise error("Raster: curves require 2–32 normalized [input,output] points")
    else if (points[0][0] != 0 or points[last][0] != 1 or
        not all([for (i in 1 to len(points)-1) points[i][0]>points[i-1][0]]))
        raise error("Raster: curve inputs must increase strictly from zero to one")
    else points
}
pub fn with_points(curves,channel,value) any^ {
    if (not (channel in channels)) raise error("Raster: unknown curve channel")
    else {*:curves,[channel]:checked(value)^}
}
pub fn move(curves,channel,index,point) any^ {
    let knots = checked(points(curves,channel))^
    if (not (index is int) or index<0 or index>=len(knots) or not model.finite(point.x) or not model.finite(point.y))
        raise error("Raster: invalid curve point")
    else {
        let x = if (index == 0) 0 else if (index == len(knots)-1) 1
            else if (point.x<=knots[index-1][0] or point.x>=knots[index+1][0]) knots[index][0] else point.x;
        with_points(curves,channel,sequence.list_set(knots,index,[x,max(0,min(1,point.y))]))^
    }
}
pub fn insert(curves,channel,point) any^ {
    let knots = checked(points(curves,channel))^
    if (not model.finite(point.x) or not model.finite(point.y)) raise error("Raster: invalid curve point")
    else {
        let x = max(0,min(1,point.x))
        let existing = [for (i,p in knots where p[0] == x) i][0]
        let added = if (existing != null) move(curves,channel,existing,point)^
            else with_points(curves,channel,sort([*knots,[x,max(0,min(1,point.y))]],(p) => p[0]))^
        let index = [for (i,p in points(added,channel) where p[0] == x) i][0];
        {curves:added,index:index}
    }
}
pub fn remove(curves,channel,index) any^ {
    let knots = checked(points(curves,channel))^
    if (not (index is int) or index<=0 or index>=len(knots)-1) raise error("Raster: select an interior curve point to remove")
    else with_points(curves,channel,sequence.list_splice(knots,index,1,[]))^
}
