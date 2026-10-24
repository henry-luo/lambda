// Layouts retain their text and arrangement alongside self-contained SVG (D7.2.5).
import objects: .mod_objects
import shapes: .mod_shapes
import colors: lambda.ui.core.component

pub let layouts = [{id:"process",label:"Process"},{id:"hierarchy",label:"Hierarchy"},{id:"cycle",label:"Cycle"},{id:"list",label:"List"}]
pub let defaults = {layout:"process",text:"Plan\nCreate\nShare",color:"#2563eb",width:520,height:240,rotation:0}
pub fn labels(p) => if (p.text is string) [for (line in split(p.text,"\n") where trim(line)!="") trim(line)] else []
fn valid(p) => contains([for (entry in layouts) entry.id],p.layout) and colors.hex_color(p.color) and
    objects.dimensions_valid(p) and p.text is string and len(p.text)<=1000 and
    len(labels(p))>=2 and len(labels(p))<=8 and all([for (label in labels(p)) len(split(label,""))<=60])
pub fn settings(n) {
    let p = objects.settings(n,"data-word-smartart",defaults);
    if (p!=null and valid(p)) p else null
}
fn box(layout,i,count) {
    if (layout=="process") {x:16+i*608/count,y:80,width:608/count-14,height:80}
    else if (layout=="list") {x:36,y:12+i*216/count,width:568,height:216/count-8}
    else if (layout=="hierarchy") {
        if (i==0) {x:250,y:12,width:140,height:64}
        else {x:12+(i-1)*616/(count-1),y:155,width:616/(count-1)-10,height:64}
    } else {
        let a = (i/count*360-90)*math.pi/180;
        {x:320+235*math.cos(a)-62,y:120+82*math.sin(a)-25,width:124,height:50}
    }
}
// Wrap at word boundaries; long words also fit the node rather than escaping its edge.
fn wrapped(label,columns) {
    if (len(split(label,""))<=columns) label else {
        let chars = split(label,"")
        let spaces = [for (i,c in take(chars,columns) where c==" ") i]
        let cut = if (len(spaces)>0) spaces[last] else columns;
        trim(join(take(chars,cut),"")) ++ "\n" ++ wrapped(trim(join(drop(chars,cut),"")),columns)
    }
}
pub fn graphic(p) {
    let text = labels(p)
    let nodes = [for (i,label in text) box(p.layout,i,len(text))]
    let links = if (p.layout=="list") [] else [for (i in 1 to len(text)-1) {from:if (p.layout=="hierarchy") 0 else i-1,to:i},
        *if (p.layout=="cycle") [{from:len(text)-1,to:0}] else []];
    <svg xmlns:"http://www.w3.org/2000/svg",width:p.width,height:p.height,viewBox:"0 0 640 240",
        <g fill:"none",stroke:p.color,'stroke-width':2,
            *[for (link in links,let a=nodes[link.from],let b=nodes[link.to])
                <path d:if (p.layout=="hierarchy") "M320 76V115H" ++ string(b.x+b.width/2) ++ "V155"
                    else "M" ++ string(a.x+a.width/2) ++ " " ++ string(a.y+a.height/2) ++ "L" ++ string(b.x+b.width/2) ++ " " ++ string(b.y+b.height/2)>]>;
        *[for (i,label in text,let b=nodes[i],let wrapped_text=wrapped(label,max(5,floor(b.width/9))),
            let size=min(if (len(text)>5) 11 else 14,(b.height-12)/(len(split(wrapped_text,"\n"))*1.3))) <g transform:"translate(" ++ string(b.x) ++ " " ++ string(b.y) ++ ")",
            shapes.graphic({*:shapes.defaults,kind:if (p.layout=="process") "arrow" else "rounded",width:b.width,height:b.height,
                fill:p.color,stroke:p.color,weight:1,size:size,text:wrapped_text,text_color:"#ffffff"})>]>
}
pub fn transaction(editor,params,selected=null) map^ {
    let p = {*:defaults,*:params}
    if (not valid(p)) raise error("Word: choose a layout, 2–8 lines of text (up to 60 characters each), a #RRGGBB color, and valid dimensions/rotation")
    else objects.transaction(editor,"data-word-smartart",p,graphic(p),p.width,p.height,"Diagram: " ++ join(labels(p)," → "),selected)^
}
