// Text effects remain editable parameters; SVG carries the exported appearance (D7.2.5).
import objects: .mod_objects
import colors: lambda.ui.core.component

pub let styles = ["solid","outlined","hollow","gradient","shadowed"]
pub let transforms = ["none","archUp","archDown","wave"]
pub let defaults = {text:"Your words",style:"gradient",transform:"none",width:360,height:110,size:48,
    font:"Georgia",fill:"#2864ad",outline:"#243b57"}
pub fn settings(n) => objects.settings(n,"data-word-wordart")
fn lettering(p,fill,stroke) {
    let attrs = {fill:fill,stroke:stroke,'stroke-width':if (stroke=="none") 0 else 1.2,
        'font-family':p.font,'font-size':p.size,'font-weight':"bold",'text-anchor':"middle",
        textLength:p.width-24,lengthAdjust:"spacingAndGlyphs"}
    if (p.transform=="none") <text x:p.width/2,y:p.height/2+p.size*0.35,*:attrs,p.text>
    else <text *:attrs,<textPath href:"#wordart-curve",startOffset:"50%",p.text>>
}
pub fn graphic(p) {
    let w = p.width
    let h = p.height
    let paths = {archUp:"M 12 " ++ string(h*0.72) ++ " Q " ++ string(w/2) ++ " " ++ string(-h*0.05) ++ " " ++ string(w-12) ++ " " ++ string(h*0.72),
        archDown:"M 12 " ++ string(h*0.35) ++ " Q " ++ string(w/2) ++ " " ++ string(h*1.1) ++ " " ++ string(w-12) ++ " " ++ string(h*0.35),
        wave:"M 12 " ++ string(h*0.6) ++ " C " ++ string(w/3) ++ " " ++ string(h*0.1) ++ " " ++ string(w*2/3) ++ " " ++ string(h*1.05) ++ " " ++ string(w-12) ++ " " ++ string(h*0.6)};
    <svg xmlns:"http://www.w3.org/2000/svg",width:w,height:h,viewBox:"0 0 " ++ string(w) ++ " " ++ string(h),
        <defs <linearGradient id:"wordart-fill",x1:"0%",y1:"0%",x2:"100%",y2:"100%",
            <stop offset:"0%",'stop-color':p.fill><stop offset:"100%",'stop-color':p.outline>>
            if (p.transform=="none") null else <path id:"wordart-curve",d:paths[p.transform]>>
        if (p.style=="shadowed") <g transform:"translate(3 4)",opacity:0.3,lettering(p,p.outline,"none")> else null
        lettering(p,if (p.style=="hollow") "none" else if (p.style=="gradient") "url(#wordart-fill)" else p.fill,
            if (contains(["outlined","hollow"],p.style)) p.outline else "none")>
}
pub fn transaction(editor,p,selected=null) map^ {
    if (not (p.text is string) or trim(p.text)=="" or len(split(p.text,""))>120 or contains(p.text,"\n") or
        not contains(styles,p.style) or not contains(transforms,p.transform) or
        not contains(["Georgia","Arial","Times New Roman"],p.font) or
        not (p.width is number) or p.width<80 or p.width>650 or not (p.height is number) or p.height<40 or p.height>400 or
        not (p.size is number) or p.size<12 or p.size>120 or not colors.hex_color(p.fill) or not colors.hex_color(p.outline))
        raise error("Word: enter 1–120 characters, valid dimensions and colors, and a supported WordArt style")
    else objects.transaction(editor,"data-word-wordart",p,graphic(p),p.width,p.height,p.text,selected)^
}
