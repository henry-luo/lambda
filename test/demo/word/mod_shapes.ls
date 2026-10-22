// Inline SVG images reuse drawing contours and the editor's image transactions (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import shapes: lambda.editor.draw.shapes
import drawing: lambda.editor.draw.model
import paint: lambda.editor.draw.paint
import glyphs: lambda.doc.shapes
import objects: .mod_objects
import colors: lambda.ui.core.component

pub let catalog = [
    {id:"rect",label:"Rectangle"},{id:"rounded",label:"Rounded rectangle"},{id:"ellipse",label:"Oval"},
    {id:"triangle",label:"Triangle"},{id:"diamond",label:"Diamond"},{id:"hexagon",label:"Hexagon"},
    {id:"parallelogram",label:"Parallelogram"},{id:"trapezoid",label:"Trapezoid"},
    {id:"cylinder",label:"Cylinder"},{id:"document",label:"Document"},{id:"note",label:"Note"},
    {id:"line",label:"Line"},{id:"arrow",label:"Arrow"},{id:"star",label:"Star"},{id:"heart",label:"Heart"}]
pub let defaults = {kind:"rect",width:160,height:100,fill:"#527f78",stroke:"#284e47",
    weight:2,rotation:0,text:"",text_color:"#ffffff",size:16}
pub fn settings(n) {
    let raw = objects.settings(n,"data-word-shape")
    if (not (raw is map)) null else {
        let p = {*:defaults,*:raw}
        if (valid(p)) p else null
    }
}
fn valid(p) => contains([for (entry in catalog) entry.id],p.kind) and
    p.width is number and p.width>=16 and p.width<=650 and
    p.height is number and p.height>=16 and p.height<=2000 and (p.fill=="none" or colors.hex_color(p.fill)) and colors.hex_color(p.stroke) and
    p.weight is number and p.weight>=0 and p.weight<=12 and p.rotation is number and p.rotation>=-180 and p.rotation<=180 and
    p.text is string and len(split(p.text,""))<=200 and colors.hex_color(p.text_color) and p.size is number and p.size>=8 and p.size<=72
pub fn graphic(p) {
    // inset the rotated box so its corners remain inside the embedded image viewport.
    let angle = p.rotation*math.pi/180
    let c = abs(math.cos(angle))
    let s = abs(math.sin(angle))
    let fit = min((p.width-p.weight-2)/(p.width*c+p.height*s),(p.height-p.weight-2)/(p.width*s+p.height*c))
    let b = {x:(p.width-p.width*fit)/2,y:(p.height-p.height*fit)/2,width:p.width*fit,height:p.height*fit}
    let style = {fill:p.fill,stroke:p.stroke,'stroke-width':p.weight}
    let label = drawing.with_attrs(drawing.make_shape("inline",p.kind,0,0,p.text),
        {'font-size':p.size,'text-color':p.text_color,'text-align':"center",'font-weight':"normal"});
    <svg xmlns:"http://www.w3.org/2000/svg",width:p.width,height:p.height,
        viewBox:"0 0 " ++ string(p.width) ++ " " ++ string(p.height),
        <g transform:"rotate(" ++ string(p.rotation) ++ " " ++ string(p.width/2) ++ " " ++ string(p.height/2) ++ ")",
            if (p.kind=="line") <line x1:b.x,y1:b.y+b.height/2,x2:b.x+b.width,y2:b.y+b.height/2,*:style>
            else if (contains(glyphs.kinds,p.kind)) <g transform:"translate(" ++ string(b.x) ++ " " ++ string(b.y) ++ ") scale(" ++
                string(b.width/100) ++ " " ++ string(b.height/100) ++ ")",*:style,glyphs.glyph(p.kind)>
            else shapes.graphic(p.kind,b,style)
            shapes.detail(p.kind,b,style)
            if (p.text=="") null else paint.text(label,b,p.text)>>
}
pub fn transaction(editor,params,selected=null) map^ {
    let p = {*:defaults,*:params}
    if (not valid(p)) raise error("Word: choose a shape, valid dimensions/colors, outline 0–12, rotation −180–180°, and text up to 200 characters")
    else if (selected!=null and settings(selected.node)==null) raise error("Word: select a shape first")
    else {
        let label = [for (entry in catalog where entry.id==p.kind) entry.label][0]
        objects.transaction(editor,"data-word-shape",p,graphic(p),p.width,p.height,if (p.text=="") label else p.text,selected)^
    }
}
