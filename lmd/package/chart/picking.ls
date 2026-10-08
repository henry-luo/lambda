// Hit records are sampled from displayed SVG geometry, including interrupted animation frames.
import svg: .svg
import paths: .path_geometry
import affine: .svg_transform
import geometry: .geometry
import util: .util

pub fn outlines(node) {
    let tag=name(node);
    if (tag=='path') paths.sample(node.d)
    else if (tag=='rect' or tag=='image') [{closed:true,points:[[node.x,node.y],[node.x+node.width,node.y],
        [node.x+node.width,node.y+node.height],[node.x,node.y+node.height]]}]
    else if (tag=='line') [{closed:false,points:[[node.x1,node.y1],[node.x2,node.y2]]}]
    else if (tag=='circle' or tag=='ellipse') [{closed:true,points:[for (i in 0 to 63,let a=util.TAU*float(i)/64.0)
        [node.cx+(if (tag=='circle') node.r else node.rx)*math.cos(a),node.cy+(if (tag=='circle') node.r else node.ry)*math.sin(a)]]}]
    else if (tag=='text' and node.x!=null and node.y!=null) [{closed:false,points:[[node.x,node.y]]}]
    else []
}
pub fn collect(node, inherited=affine.identity) {
    if (not (node is element) or node["pointer-events"]=="none") [] else {
        let matrix=affine.multiply(inherited,affine.matrix(node.transform));
        let shape=if (node["data-chart-row"]!=null) outlines(node) else [];
        [if (len(shape)>0 and not (shape is error)) {row:parse(node["data-chart-row"],'json') ^ {null},
            key:node["data-chart-key"],part:node["data-chart-part"],filled:node.fill!=null and node.fill!="none",
            lines:[for (line in shape) {*:line,points:line.points |> affine.project(matrix,~)}]},
         for (child in content(node) where not (child is element and child["data-chart-frame"]!=null))
            for (record in collect(child,matrix)) record] |: ~!=null
    }
}
pub fn refresh(node) {
    if (not (node is element)) node else {
        let children=content(node) |> refresh(~);
        let frame=if (node["data-chart-frame"]!=null) parse(node["data-chart-frame"],'json') ^ {null} else null;
        svg.rebuild(name(node),{*:map(node),*:(if (frame!=null) {'data-chart-frame':format({*:frame,
            geometry:[for (child in children) for (record in collect(child)) record]},'json')} else {})},children)
    }
}
pub fn distance(record, point) {
    let contained=record.filled and len([for (line in record.lines where line.closed and geometry.contains_point(line.points,point)) true])%2==1;
    if (contained) 0.0 else min([inf,for (line in record.lines)
        if (len(line.points)==1) paths.distance(point,line.points[0]) else
        for (i in 0 to (len(line.points)-(if (line.closed) 1 else 2)))
            geometry.distance_segment(point,line.points[i],line.points[(i+1)%len(line.points)])])
}
pub fn nearest(records, point) {
    let candidates=[for (record in records) {*:record,distance:distance(record,point)}];
    let matches=candidates |: ~.distance==min(candidates |> ~.distance) and ~.distance!=inf;
    matches[0]
}
