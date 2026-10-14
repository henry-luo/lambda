// Staff-space drawing records share their bounds with the SVG and event index.
import f: .fonts

pub fn bounds(left, top, right, bottom) => {left:left, top:top, right:right, bottom:bottom}
pub fn union(items) => if (len(items)==0) bounds(0.0,0.0,0.0,0.0) else
    bounds(min([for (b in items) b.left]),min([for (b in items) b.top]),
        max([for (b in items) b.right]),max([for (b in items) b.bottom]))
pub fn glyph(profile, key, x, y, size = 1.0, height_scale = null) map^ {
    let metric = f.glyph(profile,key)^
    let sy=height_scale or size;
    {kind:"glyph", key:key, path:metric.path, x:x, y:y, scale:size,scale_y:sy,
        bounds:bounds(x+metric.ink.left*size,y+metric.ink.top*sy,x+metric.ink.right*size,y+metric.ink.bottom*sy)}
}
pub fn line(x1,y1,x2,y2,thickness) => {kind:"line", x1:x1,y1:y1,x2:x2,y2:y2,thickness:thickness,
    bounds:bounds(min(x1,x2)-thickness/2,min(y1,y2)-thickness/2,max(x1,x2)+thickness/2,max(y1,y2)+thickness/2)}
pub fn polygon(points) => {kind:"polygon",points:points,
    bounds:bounds(min([for (p in points) p[0]]),min([for (p in points) p[1]]),
        max([for (p in points) p[0]]),max([for (p in points) p[1]]))}
pub fn curve(x1,y1,x2,y2,bend,thickness) => {kind:"curve",x1:x1,y1:y1,x2:x2,y2:y2,bend:bend,thickness:thickness,
    bounds:bounds(min(x1,x2),min([y1,y2,y1+bend,y2+bend]),max(x1,x2),max([y1,y2,y1+bend,y2+bend]))}
pub fn text(profile, value, x, y, size = 1.4, align = "start") map^ {
    let metric = f.measure(profile,[value],size)^[0]
    let left = x - (if (align=="middle") metric.width/2 else if (align=="end") metric.width else 0.0);
    {kind:"text",text:value,x:left,y:y,size:size,
        bounds:bounds(left+metric.ink.left,y+metric.ink.top,left+metric.ink.right,y+metric.ink.bottom)}
}
pub fn move(shape, x, y) => {*:shape, offset_x:(shape.offset_x or 0.0)+x,offset_y:(shape.offset_y or 0.0)+y,
    bounds:bounds(shape.bounds.left+x,shape.bounds.top+y,shape.bounds.right+x,shape.bounds.bottom+y)}
pub fn move_all(shapes,x,y) => [for (s in shapes) move(s,x,y)]
