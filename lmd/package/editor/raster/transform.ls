// S9.2.2: affine edits retain original pixels and enter the common transaction history.
import img: lambda.image
import model: .model
import affine: lambda.chart.svg_transform
import geometry: lambda.editor.draw.geometry
pub let identity = affine.identity
pub let handles = [
    {id:"nw",x:0,y:0},{id:"n",x:0.5,y:0},{id:"ne",x:1,y:0},
    {id:"w",x:0,y:0.5},{id:"e",x:1,y:0.5},
    {id:"sw",x:0,y:1},{id:"s",x:0.5,y:1},{id:"se",x:1,y:1}]
pub fn matrix(node) => model.value(node.transform,identity)
fn supported(node) => name(node) == 'layer' or (name(node) == 'stack' and
    not any([for (child in content(node)) name(child) in ['mask','effect','style']]) and
    all([for (child in content(node) where model.composition(child)) supported(child)]))
pub fn available(st) => target(st) ^ { false } ~ { true }
pub fn target(st) any^ {
    let rows = model.unlocked_roots(st)^;
    if (not all([for (row in rows) supported(row.node)]))
        raise error("Raster: group transforms require groups without group masks, effects or styles")
    else {rows:rows,node:if (len(rows)==1) rows[0].node else img.stack([for (row in rows) row.node])}
}
fn around_at(node,center,linear) {
    let p = model.position(node)
    let cx = center.x-p[0]
    let cy = center.y-p[1];
    affine.multiply([1,0,0,1,cx,cy],affine.multiply(linear,
        affine.multiply([1,0,0,1,-cx,-cy],matrix(node))))
}
pub fn around(node,angle,sx,sy) {
    let box = model.bounds(node)
    let rad = angle*math.pi/180
    let c = math.cos(rad)
    let s = math.sin(rad)
    around_at(node,geometry.rect_center(box),[c*sx,s*sx,-s*sy,c*sy,0,0])
}
fn checked_matrix(m) any^ {
    if (not (m is array) or len(m)!=6 or not all([for (v in m) model.finite(v)]) or
        abs(m[0]*m[3]-m[1]*m[2])<0.000000000001)
        raise error("Raster: provide a finite nonsingular six-number affine matrix")
    else m
}
pub fn command_matrix(node,command) any^ {
    if (command.matrix != null or command.delta != null) {
        if ((command.matrix != null and command.delta != null) or command.angle != null or command.scale_x != null or command.scale_y != null)
            raise error("Raster: provide an affine matrix or relative angle/scale")
        else checked_matrix(model.value(command.delta,command.matrix))^
    } else {
        let angle = model.value(command.angle,0)
        let sx = model.value(command.scale_x,1)
        let sy = model.value(command.scale_y,1);
        if (not all([for (v in [angle,sx,sy]) model.finite(v)]) or abs(angle)>36000 or abs(sx)<0.001 or abs(sy)<0.001 or abs(sx)>4096 or abs(sy)>4096)
            raise error("Raster: invalid rotation or scale")
        else around(node,angle,sx,sy)
    }
}
// stacks have document-space children; conjugation retains each leaf's integral placement.
fn changed(node,delta) any^ {
    if (delta == identity) node
    else if (name(node) == 'stack') img.with_children(node,[for (child in content(node))
        if (model.composition(child)) changed(child,delta)^ else child])^
    else {
        let p = model.position(node)
        let m = affine.multiply([1,0,0,1,-p[0],-p[1]],affine.multiply(delta,
            affine.multiply([1,0,0,1,p[0],p[1]],matrix(node))));
        img.with_properties(node,{transform:if (m == identity) null else m})^
    }
}
pub fn run_command(st,command) any^ {
    let selected = target(st)^
    let absolute = command.matrix != null
    let geometry = if (absolute) selected.node else img.stack([selected.node])
    let m = command_matrix(geometry,command)^;
    if (absolute and (len(selected.rows)!=1 or name(selected.node) != 'layer'))
        raise error("Raster: absolute matrix/reset requires one leaf; use relative transforms or undo for groups")
    else {*:st,doc:reduce([st.doc,*selected.rows],(image,row) => model.replace(image,row.id,
        if (absolute) (if (m == identity and row.node.transform == null) row.node
            else img.with_properties(row.node,{transform:if (m == identity) null else m})^)
        else changed(row.node,m)^)^)}
}
pub fn straighten(st,start,end,axis="horizontal") any^ {
    let selected = target(st)^
    if (not (axis in ["horizontal","vertical"]) or not all([for (v in [start.x,start.y,end.x,end.y]) model.finite(v)]))
        raise error("Raster: invalid straighten reference")
    else if (start == end) {id:"layer.transform",delta:identity}
    else {
        let raw = math.atan2(end.y-start.y,end.x-start.x)*180/math.pi-(if (axis == "vertical") 90 else 0)
        let angle = if (raw>90) raw-180 else if (raw< -90) raw+180 else raw;
        {id:"layer.transform",delta:around(img.stack([selected.node]),-angle,1,1)}
    }
}
pub fn gesture(st,start,end,rotate=false) any^ {
    let row = target(st)^
    let box = model.bounds(row.node)
    let cx = box.x+box.width/2
    let cy = box.y+box.height/2
    let initial = math.sqrt((start.x-cx)**2+(start.y-cy)**2)
    let final = math.sqrt((end.x-cx)**2+(end.y-cy)**2)
    let angle = (math.atan2(end.y-cy,end.x-cx)-math.atan2(start.y-cy,start.x-cx))*180/math.pi
    let scale = if (initial<1) 1 else max(0.01,min(100,final/initial));
    {id:"layer.transform",delta:around(img.stack([row.node]),if (rotate) angle else 0,if (rotate) 1 else scale,if (rotate) 1 else scale)}
}
fn checked_scale(value) => if (abs(value)<0.001) (if (value<0) -0.001 else 0.001) else max(-4096,min(4096,value))
pub fn handle_gesture(st,start,end,handle,shift=false,alt=false) any^ {
    let row = target(st)^
    let box = model.bounds(row.node)
    let h = [for (entry in handles where entry.id == handle) entry][0]
    if (not all([for (value in [start.x,start.y,end.x,end.y]) model.finite(value)]))
        raise error("Raster: transform coordinates must be finite")
    else if (handle == "rotate") {
        let center = geometry.rect_center(box)
        let degrees = if (geometry.v2_dist(start,center)<0.001 or geometry.v2_dist(end,center)<0.001) 0
            else (math.atan2(end.y-center.y,end.x-center.x)-math.atan2(start.y-center.y,start.x-center.x))*180/math.pi;
        {id:"layer.transform",delta:around(img.stack([row.node]),if (shift) round(degrees/15)*15 else degrees,1,1)}
    } else if (h == null or box.width<=0 or box.height<=0) raise error("Raster: invalid transform handle or empty bounds")
    else {
        let ax = if (alt or h.x == 0.5) 0.5 else 1-h.x
        let ay = if (alt or h.y == 0.5) 0.5 else 1-h.y
        let sx = if (h.x == 0.5) 1 else checked_scale(1+(end.x-start.x)/(box.width*(h.x-ax)))
        let sy = if (h.y == 0.5) 1 else checked_scale(1+(end.y-start.y)/(box.height*(h.y-ay)))
        let proportional = shift and h.x != 0.5 and h.y != 0.5
        let ratio = if (abs(sx-1)>=abs(sy-1)) sx else sy
        let center = {x:box.x+box.width*ax,y:box.y+box.height*ay};
        // premultiplying in document space keeps the opposite anchor fixed on existing affines.
        {id:"layer.transform",delta:around_at(img.stack([row.node]),center,
            [if (proportional) ratio else sx,0,0,if (proportional) ratio else sy,0,0])}
    }
}
pub fn rasterize(st,node) any^ {
    let box = model.bounds(node)
    let x = int(floor(box.x))
    let y = int(floor(box.y))
    let width = max(1,int(ceil(box.x+box.width))-x)
    let height = max(1,int(ceil(box.y+box.height))-y)
    let normalized = img.with_properties(node,{opacity:1,blend:'normal',clipping:false,position:[model.position(node)[0]-x,model.position(node)[1]-y]})^
    let pixels = model.render_layers(st.doc,[normalized],width,height)^
    let changed = img.with_properties(node,{kind:'raster',transform:null,position:[x,y]})^;
    img.with_children(changed,[pixels])^
}
