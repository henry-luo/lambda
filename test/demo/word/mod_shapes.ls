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
pub let defaults = {kind:"rect",width:160,height:100,fill:"#3478cc",stroke:"#245a9b",
    weight:2,rotation:0,text:"",text_color:"#ffffff",size:16,font:"Arial",bold:false,italic:false,
    align:"center",vertical:"middle",direction:0,inset:12,text_style:"solid",text_outline:"#243b57",
    dash:"solid",effect:"none",fill_style:"solid",fill2:"#bcd9ff",opacity:100,alt:""}
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
    p.text is string and len(split(p.text,""))<=200 and colors.hex_color(p.text_color) and p.size is number and p.size>=8 and p.size<=72 and
    p.font is string and trim(p.font)!="" and len(p.font)<=200 and p.bold is bool and p.italic is bool and
    contains(["left","center","right"],p.align) and contains(["top","middle","bottom"],p.vertical) and
    contains([0,90,-90],p.direction) and p.inset is number and p.inset>=0 and p.inset<=100 and
    contains(["solid","outlined","hollow","shadowed"],p.text_style) and colors.hex_color(p.text_outline) and
    contains(["solid","dashed","dotted","dash-dot"],p.dash) and contains(["none","shadow"],p.effect) and
    contains(["solid","gradient"],p.fill_style) and colors.hex_color(p.fill2) and
    p.opacity is number and p.opacity>=0 and p.opacity<=100 and p.alt is string and len(p.alt)<=1000
fn contour(p,b,style) => <g
    if (p.kind=="line") <line x1:b.x,y1:b.y+b.height/2,x2:b.x+b.width,y2:b.y+b.height/2,*:style>
    else if (contains(glyphs.kinds,p.kind)) <g transform:"translate(" ++ string(b.x) ++ " " ++ string(b.y) ++ ") scale(" ++
        string(b.width/100) ++ " " ++ string(b.height/100) ++ ")",*:style,glyphs.glyph(p.kind)>
    else shapes.graphic(p.kind,b,style)
    shapes.detail(p.kind,b,style)>
pub fn graphic(p,prefix="word-shape-") {
    // rotation belongs to the image transform; keep the artwork at its full size.
    // reserve only the shadow space available after the stroke at the minimum object size.
    let shadow = if (p.effect=="shadow") min(4,max(0,(min(p.width,p.height)-p.weight-3)/2)) else 0
    let pad = p.weight+2+shadow*2
    let fit = min((p.width-pad)/p.width,(p.height-pad)/p.height)
    let b = {x:(p.width-p.width*fit)/2,y:(p.height-p.height*fit)/2,width:p.width*fit,height:p.height*fit}
    let stroke = drawing.with_attrs(drawing.make_shape("inline",p.kind,0,0,""),
        {'stroke':p.stroke,'stroke-width':p.weight,'stroke-style':p.dash})
    let style = {*:paint.stroke(stroke),fill:if (p.fill=="none") "none" else if (p.fill_style=="gradient") "url(#" ++ prefix ++ "fill)" else p.fill}
    let label = drawing.with_attrs(drawing.make_shape("inline",p.kind,0,0,p.text),
        {'font-size':p.size,'text-color':p.text_color,'text-align':p.align,'font-weight':if (p.bold) "bold" else "normal"})
    // rotated text uses the swapped inner box, keeping its alignment relative to the shape.
    let text_box = if (p.direction==0) b else {x:(p.width-b.height)/2,y:(p.height-b.width)/2,width:b.height,height:b.width}
    let text_options = {font:p.font,italic:p.italic,vertical:p.vertical,inset:p.inset,
        fill:if (p.text_style=="hollow") "none" else p.text_color,
        stroke:if (contains(["outlined","hollow"],p.text_style)) p.text_outline else "none",weight:1};
    <svg xmlns:"http://www.w3.org/2000/svg",width:p.width,height:p.height,
        viewBox:"0 0 " ++ string(p.width) ++ " " ++ string(p.height),
        // solid previews need no paint server; their IDs must not shadow dialog inputs.
        if (p.fill_style=="gradient" and p.fill!="none") <defs <linearGradient id:prefix ++ "fill",x1:"0%",y1:"0%",x2:"100%",y2:"100%",
            <stop offset:"0%",'stop-color':p.fill><stop offset:"100%",'stop-color':p.fill2>>> else null;
        <g opacity:p.opacity/100,
            if (p.effect=="shadow") <g transform:"translate(" ++ string(shadow) ++ " " ++ string(shadow) ++ ")",opacity:0.2,
                contour(p,b,{fill:if (p.fill=="none") "none" else "#24334b",stroke:"#24334b",'stroke-width':p.weight})> else null
            contour(p,b,style)
            if (p.text=="") null else <g transform:"rotate(" ++ string(p.direction) ++ " " ++ string(p.width/2) ++ " " ++ string(p.height/2) ++ ")",
                if (p.text_style=="shadowed") <g transform:"translate(2 2)",opacity:0.3,
                    paint.text(label,text_box,p.text,false,{*:text_options,fill:p.text_outline})> else null
                paint.text(label,text_box,p.text,false,text_options)>>>
}
pub fn parameters(params) map^ {
    let p = {*:defaults,*:params}
    if (not valid(p)) raise error("Word: choose a shape, valid dimensions/colors, outline 0–12, rotation −180–180°, and text up to 200 characters")
    else p
}
pub fn transaction(editor,params,selected=null) map^ {
    let p = parameters(params)^;
    if (selected!=null and settings(selected.node)==null) raise error("Word: select a shape first")
    else {
        let label = [for (entry in catalog where entry.id==p.kind) entry.label][0]
        objects.transaction(editor,"data-word-shape",p,graphic(p),p.width,p.height,if (p.alt!="") p.alt else if (p.text=="") label else p.text,selected)^
    }
}
