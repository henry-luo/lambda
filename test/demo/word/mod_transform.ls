// Object gestures and dialogs share model transactions and source metadata (S1.4, D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction
import image: lambda.editor.mod_image
import geom: lambda.editor.draw.geometry
import objects: .mod_objects
import shapes: .mod_shapes
import art: .mod_wordart

fn measure(n,key,fallback) => (float(attrs_get(n.attrs,key)) ^ { fallback }) or fallback
pub fn rotation(n) => measure(n,'data-word-rotation',0)
pub fn settings(n,fallback={width:0,height:0}) {
    let shape = shapes.settings(n)
    let wordart = art.settings(n)
    let kind = if (shape!=null) "shape" else if (wordart!=null) "wordart" else "picture"
    let stored = if (shape!=null) shape else if (wordart!=null) wordart else {
        width:measure(n,'width',fallback.width),height:measure(n,'height',fallback.height),rotation:rotation(n)}
    if (n.tag!='img' or objects.settings(n,"data-word-equation")!=null) null else {*:stored,kind:kind}
}
pub fn normalized(angle) => ((angle+180)%360+360)%360-180
pub fn rotation_style(angle) => if (angle==0) "" else "rotate(" ++ string(angle) ++ "deg)"
// screen-space handles follow the object's rotation in 45-degree sectors.
pub fn resize_cursor(handle,angle) => ["ew-resize","nwse-resize","ns-resize","nesw-resize"][
    ((round((angle+if (contains(["nw","se"],handle)) 45 else 135)/45)%4)+4)%4]
fn angle(x,y,cx,cy) => math.atan2(y-cy,x-cx)*180/math.pi
pub fn gesture_value(drag,x,y,snapping=false) {
    if (drag.handle=="rotate") {
        let value = normalized(drag.rotation+angle(x,y,drag.cx,drag.cy)-angle(drag.x,drag.y,drag.cx,drag.cy))
        {width:drag.width,height:drag.height,rotation:normalized(if (snapping) round(value/15)*15 else round(value))}
    } else {
        let local = geom.rotate_point({x:(x-drag.x)/drag.zoom,y:(y-drag.y)/drag.zoom},{x:0,y:0},-drag.rotation)
        let dx = local.x*(if (contains(drag.handle,"w")) -1 else 1)/drag.width
        let dy = local.y*(if (contains(drag.handle,"n")) -1 else 1)/drag.height
        let low = max((if (drag.kind=="wordart") 80 else 16)/drag.width,(if (drag.kind=="wordart") 40 else 16)/drag.height)
        let high = min(650/drag.width,(if (drag.kind=="wordart") 400 else 2000)/drag.height)
        let scale = min(high,max(low,1+if (abs(dx)>=abs(dy)) dx else dy))
        {width:round(drag.width*scale),height:round(drag.height*scale),rotation:drag.rotation}
    }
}
pub fn parameters(n,p) {
    let original = settings(n)
    let size = if (original.kind=="wordart" and p.scale_text==true) min(120,max(12,original.size*p.height/original.height)) else original.size
    let stored = if (original.kind=="shape") shapes.settings(n) else if (original.kind=="wordart") art.settings(n) else original
    {*:stored,width:p.width,height:p.height,rotation:p.rotation,*:if (original.kind=="wordart") {size:size} else {}}
}
pub fn preview_source(n,p) string | null {
    let value = parameters(n,p)
    if (settings(n).kind=="shape") objects.source(shapes.graphic(value))^ { null }
    else if (settings(n).kind=="wordart") objects.source(art.graphic(value))^ { null } else null
}
pub fn transaction(editor,p) map^ {
    let selected = image.selected(editor)
    if (selected==null or (p.path!=null and p.path!=selected.path) or settings(selected.node)==null)
        raise error("Word: select a picture, shape, or WordArt first")
    else if (not geom.finite_number(p.width) or not geom.finite_number(p.height) or not geom.finite_number(p.rotation) or
        p.width<16 or p.width>650 or p.height<16 or p.height>2000 or p.rotation< -180 or p.rotation>180)
        raise error("Word: use width 16–650 px, height 16–2000 px, and rotation −180–180°")
    else {
        let value = parameters(selected.node,p)
        if (settings(selected.node).kind=="shape") shapes.transaction(editor,value,selected)^
        else if (settings(selected.node).kind=="wordart") art.transaction(editor,value,selected)^
        else objects.rotate_transaction(image.resize_transaction(tx_begin(editor.doc,editor.selection),selected.path,p.width,p.height),selected.path,p.rotation)
    }
}
