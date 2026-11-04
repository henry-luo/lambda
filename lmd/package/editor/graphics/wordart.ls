// Shared Word Art content and SVG; applications own document insertion.
import colors: lambda.ui.core.component

pub let office_limits = {min_width:80,max_width:650,min_height:40,max_height:400}
pub let styles = ["solid","outlined","hollow","gradient","shadowed"]
pub let transforms = ["none","archUp","archDown","wave"]
pub let defaults = {text:"Your words",style:"gradient",transform:"none",width:360,height:110,size:48,
    font:"Georgia",fill:"#2864ad",outline:"#243b57",rotation:0,alt:""}
fn lettering(p,fill,stroke,prefix) {
    let attrs = {fill:fill,stroke:stroke,'stroke-width':if (stroke=="none") 0 else 1.2,
        'font-family':p.font,'font-size':p.size,'font-weight':"bold",'text-anchor':"middle",
        textLength:p.width-24,lengthAdjust:"spacingAndGlyphs"}
    if (p.transform=="none") <text x:p.width/2,y:p.height/2+p.size*0.35,*:attrs,p.text>
    else <text *:attrs,<textPath href:"#" ++ prefix ++ "curve",startOffset:"50%",p.text>>
}
pub fn graphic(p,prefix="wordart-") {
    let w = p.width
    let h = p.height
    let paths = {archUp:"M 12 " ++ string(h*0.72) ++ " Q " ++ string(w/2) ++ " " ++ string(-h*0.05) ++ " " ++ string(w-12) ++ " " ++ string(h*0.72),
        archDown:"M 12 " ++ string(h*0.35) ++ " Q " ++ string(w/2) ++ " " ++ string(h*1.1) ++ " " ++ string(w-12) ++ " " ++ string(h*0.35),
        wave:"M 12 " ++ string(h*0.6) ++ " C " ++ string(w/3) ++ " " ++ string(h*0.1) ++ " " ++ string(w*2/3) ++ " " ++ string(h*1.05) ++ " " ++ string(w-12) ++ " " ++ string(h*0.6)};
    <svg xmlns:"http://www.w3.org/2000/svg",width:w,height:h,viewBox:"0 0 " ++ string(w) ++ " " ++ string(h),
        <defs if (p.style=="gradient") <linearGradient id:prefix ++ "fill",x1:"0%",y1:"0%",x2:"100%",y2:"100%",
            <stop offset:"0%",'stop-color':p.fill><stop offset:"100%",'stop-color':p.outline>> else null
            if (p.transform=="none") null else <path id:prefix ++ "curve",d:paths[p.transform]>>
        if (p.style=="shadowed") <g transform:"translate(3 4)",opacity:0.3,lettering(p,p.outline,"none",prefix)> else null
        lettering(p,if (p.style=="hollow") "none" else if (p.style=="gradient") "url(#" ++ prefix ++ "fill)" else p.fill,
            if (contains(["outlined","hollow"],p.style)) p.outline else "none",prefix)>
}
pub fn parameters(params,limits,owner="Word Art") map^ {
    let p = {*:defaults,*:params}
    if (not (p.text is string) or trim(p.text)=="" or len(split(p.text,""))>120 or contains(p.text,"\n") or
        not contains(styles,p.style) or not contains(transforms,p.transform) or
        not (p.font is string) or trim(p.font)=="" or
        not (p.width is number) or p.width<limits.min_width or p.width>limits.max_width or not (p.height is number) or p.height<limits.min_height or p.height>limits.max_height or
        not (p.size is number) or p.size<12 or p.size>120 or not colors.hex_color(p.fill) or not colors.hex_color(p.outline) or
        not (p.rotation is number) or p.rotation< -180 or p.rotation>180 or not (p.alt is string) or len(p.alt)>1000)
        raise error(owner ++ ": enter 1–120 characters, valid dimensions and colors, and a supported Word Art style")
    else p
}
