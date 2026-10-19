// S9.2.2: affine edits retain original pixels and enter the common transaction history.
import img: lambda.image
import model: .model
import affine: lambda.chart.svg_transform
pub let identity = affine.identity
pub fn matrix(node) => model.value(node.transform,identity)
pub fn around(node,angle,sx,sy) {
    let box = model.bounds(node)
    let cx = box.x+box.width/2-model.position(node)[0]
    let cy = box.y+box.height/2-model.position(node)[1]
    let rad = angle*math.pi/180
    let c = math.cos(rad)
    let s = math.sin(rad)
    affine.multiply([1,0,0,1,cx,cy],affine.multiply([c*sx,s*sx,-s*sy,c*sy,0,0],
        affine.multiply([1,0,0,1,-cx,-cy],matrix(node))))
}
pub fn command_matrix(node,command) any^ {
    if (command.matrix != null) {
        if (command.angle != null or command.scale_x != null or command.scale_y != null or
            not (command.matrix is array) or len(command.matrix)!=6 or not all([for (v in command.matrix) model.finite(v)]))
            raise error("Raster: provide a six-number affine matrix or relative angle/scale")
        else command.matrix
    } else {
        let angle = model.value(command.angle,0)
        let sx = model.value(command.scale_x,1)
        let sy = model.value(command.scale_y,1);
        if (not all([for (v in [angle,sx,sy]) model.finite(v)]) or abs(angle)>36000 or abs(sx)<0.001 or abs(sy)<0.001 or abs(sx)>4096 or abs(sy)>4096)
            raise error("Raster: invalid rotation or scale")
        else around(node,angle,sx,sy)
    }
}
pub fn gesture(st,start,end,rotate=false) any^ {
    let row = model.active(st)^
    let box = model.bounds(row.node)
    let cx = box.x+box.width/2
    let cy = box.y+box.height/2
    let initial = math.sqrt((start.x-cx)**2+(start.y-cy)**2)
    let final = math.sqrt((end.x-cx)**2+(end.y-cy)**2)
    let angle = (math.atan2(end.y-cy,end.x-cx)-math.atan2(start.y-cy,start.x-cx))*180/math.pi
    let scale = if (initial<1) 1 else max(0.01,min(100,final/initial));
    {id:"layer.transform",matrix:around(row.node,if (rotate) angle else 0,if (rotate) 1 else scale,if (rotate) 1 else scale)}
}
pub fn rasterize(st,node) any^ {
    let box = model.bounds(node)
    let x = int(floor(box.x))
    let y = int(floor(box.y))
    let width = max(1,int(ceil(box.x+box.width))-x)
    let height = max(1,int(ceil(box.y+box.height))-y)
    let normalized = img.with_properties(node,{opacity:1,blend:'normal',clipping:false,position:[model.position(node)[0]-x,model.position(node)[1]-y]})^
    let pixels = img.render_frame(img.document(img.stack([normalized]),{width:width,height:height}))^
    let changed = img.with_properties(node,{kind:'raster',transform:null,position:[x,y]})^;
    img.with_children(changed,[pixels])^
}
