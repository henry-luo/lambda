// S12.1.3: camera and precision overlays derive from session state, never authored pixels.
import math
import viewport: lambda.editor.draw.viewport

pub let defaults = {rulers_visible:false,grid_visible:false,grid_spacing:32,grid_subdivisions:4,snap_grid:false}
pub fn configure(st,command) any^ {
    if (any([for (key in ["rulers","grid","snap"]) command[key] != null and not (command[key] is bool)]))
        raise error("Raster: view toggles require booleans")
    else if (command.spacing != null and (not (command.spacing is int) or command.spacing<1 or command.spacing>65536))
        raise error("Raster: grid spacing must be 1–65536 whole pixels")
    else if (command.subdivisions != null and (not (command.subdivisions is int) or command.subdivisions<1 or command.subdivisions>16))
        raise error("Raster: grid subdivisions must be an integer from 1 to 16")
    else {*:st,rulers_visible:if (command.rulers == null) st.rulers_visible else command.rulers,
        grid_visible:if (command.grid == null) st.grid_visible else command.grid,
        grid_spacing:if (command.spacing == null) st.grid_spacing else command.spacing,
        grid_subdivisions:if (command.subdivisions == null) st.grid_subdivisions else command.subdivisions,
        snap_grid:if (command.snap == null) st.snap_grid else command.snap}
}
pub fn camera(st,bounds) {
    let scale = if (st.zoom == 0) viewport.fit(st.doc,bounds,40.0) else st.zoom;
    {scale:scale,width:st.doc.width*scale,height:st.doc.height*scale,
        x:(bounds.width-st.doc.width*scale)/2+st.pan.x,y:(bounds.height-st.doc.height*scale)/2+st.pan.y}
}
pub fn center(st,bounds,point) {
    let camera_box = camera(st,bounds);
    {*:st,pan:{x:(st.doc.width/2-max(0,min(st.doc.width,point.x)))*camera_box.scale,
        y:(st.doc.height/2-max(0,min(st.doc.height,point.y)))*camera_box.scale}}
}
pub fn visible(st,bounds) {
    let camera_box = camera(st,bounds)
    let x = max(0,min(st.doc.width,-camera_box.x/camera_box.scale))
    let y = max(0,min(st.doc.height,-camera_box.y/camera_box.scale));
    {x:x,y:y,width:max(0,min(st.doc.width,(bounds.width-camera_box.x)/camera_box.scale)-x),
        height:max(0,min(st.doc.height,(bounds.height-camera_box.y)/camera_box.scale)-y)}
}
pub fn thumbnail(size,bounds) {
    let scale = min(bounds.width/size.width,bounds.height/size.height)
    let width = max(1,int(floor(size.width*scale)))
    let height = max(1,int(floor(size.height*scale)));
    {x:(bounds.width-width)/2,y:(bounds.height-height)/2,width:width,height:height}
}
pub fn navigator_point(size,box,point) => {
    x:max(0,min(size.width,(point.x-box.x)*size.width/box.width)),
    y:max(0,min(size.height,(point.y-box.y)*size.height/box.height))}
// limit line generation to the visible interval, including when a large image is deeply zoomed.
pub fn positions(start,end,step) => if (step<=0 or end<start) [] else
    [for (i in int(ceil(start/step)) to int(floor(end/step))) i*step]
pub fn tick_step(scale) {
    let requested = 64/scale
    let decade = math.pow(10,floor(math.log10(requested)))
    let fraction = requested/decade;
    max(1,decade*(if (fraction<=1) 1 else if (fraction<=2) 2 else if (fraction<=5) 5 else 10))
}
pub fn ticks(origin,length,scale) {
    let step = tick_step(scale);
    [for (value in positions(-origin/scale,(length-origin)/scale,step))
        {position:origin+value*scale,value:value}]
}
pub fn grid_lines(st,bounds) {
    let camera_box = camera(st,bounds)
    let rect = visible(st,bounds)
    let minor = st.grid_spacing/st.grid_subdivisions
    let step = if (minor*camera_box.scale>=6) minor else st.grid_spacing;
    if (not st.grid_visible or rect.width<=0 or rect.height<=0 or step*camera_box.scale<6) [] else
        [for (axis in ["x","y"]) for (value in positions(rect[axis],rect[axis]+rect[if (axis == "x") "width" else "height"],step))
            {axis:axis,value:value,major:abs(value/st.grid_spacing-round(value/st.grid_spacing))<0.000001}]
}
pub fn grid_targets(st,anchors,delta,extent) => if (not st.snap_grid) [] else (
    let step = st.grid_spacing/st.grid_subdivisions,
    unique([for (anchor in anchors)
        max(0,min(floor(extent/step)*step,round((anchor+delta)/step)*step))]))
