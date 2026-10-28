// adjustments are immutable inherited values; sibling state never leaks (S12.1.1v3, PG12.3–4).
import c: ._common
import t: lambda.scene3d.transform
import colors: lambda.color
pub let FIELDS = ["position","rotate","rotation","scale","matrix","color","hue","saturation","brightness","opacity"]
pub fn matrix(n) array^ {
    let exclusive = c.require(not (n.rotate != null and n.rotation != null),"rotate and rotation are exclusive")^
    let checked = if (n.matrix != null) c.require(t.affine(n.matrix) and all([for (k in ["position","rotate","rotation","scale"]) n[k] == null]),"matrix and geometric adjustments are exclusive")^
    let scale = c.value(n.scale,1.0)
    let factors = if (scale is number) [scale,scale,scale] else if (scale is array and len(scale) == 2) [*scale,1.0] else scale
    let rotation = if (n.rotate is number) [0.0,0.0,n.rotate] else c.value(n.rotate,[0.0,0.0,0.0])
    let angles = if (n.rotation != null) n.rotation else if (t.vector(rotation,3)) [for (a in rotation) a*math.pi/180.0] else rotation;
    if (n.matrix != null) n.matrix else t.matrix(c.value(n.position,[0.0,0.0,0.0]),angles,factors)^
}
pub fn color_op(n) map^ {
    let rgba = if (n.color != null) colors.parse(n.color,"adjustment")^ else null
    let checked = c.require((n.color == null or n.color is string) and
        (n.hue == null or t.numeric(n.hue)) and
        all([for (k in ["saturation","brightness"]) n[k] == null or c.bounded(n[k],0.0,1000000.0)]) and
        (n.opacity == null or c.bounded(n.opacity,0.0,1.0)),"invalid color adjustment")^;
    {base:rgba,hue:c.value(n.hue,0.0),saturation:c.value(n.saturation,1.0),brightness:c.value(n.brightness,1.0),opacity:c.value(n.opacity,1.0)}
}
pub fn identity() map => {matrix:c.identity(),colors:[]}
pub fn compose(st,n) map^ => {matrix:t.multiply(st.matrix,matrix(n)^)^,colors:[*st.colors,color_op(n)^]}
fn color_step(rgba,op) array {
    let base = if (op.base != null) [*slice(op.base,0,3),rgba[3]*op.base[3]] else rgba
    let hsv = colors.hsv(base);
    colors.rgb(hsv.h+op.hue,min(1.0,hsv.s*op.saturation),min(1.0,hsv.v*op.brightness),base[3]*op.opacity)
}
pub fn paint(ops,intrinsic="#ffffff") map^ {
    let base = colors.parse(intrinsic,"intrinsic color")^
    let rgba = reduce([base,*ops],(current,op) => color_step(current,op));
    {color:colors.hex(rgba),opacity:rgba[3]}
}
pub fn frame_size(matrix) float => sqrt(sum([for (i in [0,1,2,4,5,6,8,9,10]) matrix[i]*matrix[i]]))
