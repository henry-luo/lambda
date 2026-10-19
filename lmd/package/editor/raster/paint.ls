// one target mapper serves pixel painting and mask painting; native kernels own sample loops.
import img: lambda.image
import model: .model

pub fn local(point,origin) => [point[0]-origin[0],point[1]-origin[1],*if (len(point)>2) [point[2]] else []]
pub fn color(st,value=null) {
    let requested = model.value(value,st.foreground)
    if (st.target == "mask" and len(requested) == 4) [requested[0]*0.2126+requested[1]*0.7152+requested[2]*0.0722]
    else requested
}
pub fn extended(st,target,command,options) any^ {
    if (not (command.id in ["paint.stroke","retouch.stroke"])) target
    else {
        let radius = model.value(options.size,st.brush.size)/2
        let points = command.points
        if (not (points is array) or len(points)==0 or not model.finite(radius) or radius<=0 or radius>2048 or
            not all([for (p in points) p is array and len(p)>=2 and model.finite(p[0]) and model.finite(p[1])]))
            raise error("Raster: invalid stroke points or size")
        else {
            let p = target.position
            let left = min(0,max(-p[0],int(floor(min([for (point in points) point[0]])-p[0]-radius))))
            let top = min(0,max(-p[1],int(floor(min([for (point in points) point[1]])-p[1]-radius))))
            let right = max(target.raster.width,min(st.doc.width-p[0],int(ceil(max([for (point in points) point[0]])-p[0]+radius))))
            let bottom = max(target.raster.height,min(st.doc.height-p[1],int(ceil(max([for (point in points) point[1]])-p[1]+radius))))
            if ([left,top,right,bottom] == [0,0,target.raster.width,target.raster.height]) target
            else {
                let node = target.row.node
                let fill = if (st.target == "mask") [model.value(model.mask(node).default,1.0)] else [0.0,0.0,0.0,0.0]
                let raster = img.reframe(target.raster,[left,top,right,bottom],{fill:fill})^
                let changed = if (st.target == "pixels")
                    img.with_children(img.with_properties(node,{position:[p[0]+left,p[1]+top]})^,
                        [for (child in content(node)) if (name(child) == 'mask') img.with_properties(child,
                            {position:[model.position(child)[0]-left,model.position(child)[1]-top]})^ else child])^
                    else img.with_children(node,[for (child in content(node)) if (name(child) == 'mask')
                        img.with_properties(child,{position:[model.position(child)[0]+left,model.position(child)[1]+top]})^ else child])^;
                {*:target,raster:raster,position:[p[0]+left,p[1]+top],row:{*:target.row,node:changed}}
            }
        }
    }
}
pub fn run_command(st,command) any^ {
    let options = model.value(command.options,{})
    let target = extended(st,model.pixel_target(st)^,command,options)^
    let common = {selection:st.selection,origin:target.position,color:color(st,options.color),opacity:model.value(command.opacity,model.value(options.opacity,st.brush.opacity))}
    let raster = if (command.id == "paint.stroke") img.paint_stroke(target.raster,
        {*:st.brush,*:options,*:common,points:[for (point in command.points) local(point,target.position)]})^
    else if (command.id in ["paint.fill","pixels.fill","pixels.clear"]) img.paint_fill(target.raster,
        {*:common,color:color(st,command.color),mode:if (command.id == "pixels.clear") "erase" else "paint"})^
    else if (command.id == "paint.gradient") img.gradient(target.raster,
        {*:options,*:common,start:local(command.start,target.position),end:local(command.end,target.position),
            end_color:color(st,model.value(options.end_color,st.background)),kind:model.value(options.kind,"linear")})^
    else if (command.id == "paint.bucket") {
        let local_coverage = img.wand(target.raster,local(command.point,target.position),options)^;
        let p = target.position
        let selection = img.reframe(local_coverage,[-p[0],-p[1],st.doc.width-p[0],st.doc.height-p[1]])^;
        let clipped = if (st.selection == null) selection else img.combine_coverage(st.selection,selection,"intersect")^;
        img.paint_fill(target.raster,{*:common,selection:clipped})^
    }
    else if (command.id == "pixels.adjust") (
        if (st.target == "mask") raise error("Raster: color adjustments require a pixel target")
        else img.mix_samples(target.raster,img.adjust(target.raster,options)^,{selection:st.selection,origin:target.position})^)
    else raise error("Raster: unknown paint command");
    model.replace_pixels(st,target,raster)^
}
pub fn copy_pixels(st,merged=false) any^ {
    let target = if (merged) {raster:img.render_frame(st.doc)^,position:[0,0],row:{node:{name:"Merged"}}}
        else model.pixel_target({*:st,target:"pixels"},false)^
    let p = target.position
    let bounds = if (st.selection == null) [p[0],p[1],p[0]+target.raster.width,p[1]+target.raster.height]
        else img.coverage_bounds(st.selection)^
    let left = max(p[0],bounds[0])
    let top = max(p[1],bounds[1])
    let right = min(p[0]+target.raster.width,bounds[2])
    let bottom = min(p[1]+target.raster.height,bounds[3]);
    if (bounds == null or right<=left or bottom<=top) raise error("Raster: selection has no pixels to copy")
    else {
        let selected = img.extract_samples(target.raster,{selection:st.selection,origin:p})^
        let raster = img.reframe(selected,[left-p[0],top-p[1],right-p[0],bottom-p[1]])^;
        {*:st,clipboard:{raster:raster,position:[left,top],name:target.row.node.name ++ " copy"}}
    }
}
