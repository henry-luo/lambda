// Theme references are package data; only validated colors and quoted font names reach CSS (S1.8).
import c: .common
import package: .package
import hex: lambda.doc.pdf.util
import rgba: lambda.slide.color

pub fn valid_hex(value, count = 6) => value is string and len(value) == count and
    all([for (ch in split(value,"")) hex.is_hex_digit(ch)])

pub fn load(pkg) map^ {
    let root = package.related_xml(pkg,"theme")^;
    if (root != null and (root.uri != c.A or root.tag != "theme"))
        raise c.fail("invalid-package","Invalid document theme",pkg.main)
    else read(root)
}
pub fn read(root) {
    let elements = c.child(root,"themeElements",c.A)
    let scheme = c.child(elements,"clrScheme",c.A)
    let fonts = c.child(elements,"fontScheme",c.A);
    {fills:c.children(c.child(c.child(elements,"fmtScheme",c.A),"fillStyleLst",c.A),null,c.A),
        background_fills:c.children(c.child(c.child(elements,"fmtScheme",c.A),"bgFillStyleLst",c.A),null,c.A),
        lines:c.children(c.child(c.child(elements,"fmtScheme",c.A),"lnStyleLst",c.A),null,c.A),
        effects:c.children(c.child(c.child(elements,"fmtScheme",c.A),"effectStyleLst",c.A),null,c.A),
        colors:map([for (entry in c.children(scheme,null,c.A),
            let rgb = c.child(entry,"srgbClr",c.A), let system = c.child(entry,"sysClr",c.A),
            let value = if (rgb != null) c.attr(rgb,"val","") else c.attr(system,"lastClr","")
            where valid_hex(value)) (entry.tag,value)]),
        fonts:map([for (kind in ["major","minor"])
            (kind,c.attr(c.child(c.child(fonts,kind ++ "Font",c.A),"latin",c.A),"typeface",""))])}
}

// Office hue uses cyclic angles; saturation and luminance use clamped unit fractions.
fn to_hsl(rgb) {
    let hi = max(rgb)
    let lo = min(rgb)
    let delta = hi-lo
    let light = (hi+lo)/2.0
    let hue = if (delta==0.0) 0.0 else if (hi==rgb[0]) (rgb[1]-rgb[2])/delta
        else if (hi==rgb[1]) (rgb[2]-rgb[0])/delta+2.0 else (rgb[0]-rgb[1])/delta+4.0;
    [hue/6.0-floor(hue/6.0),if (delta==0.0) 0.0 else delta/(1.0-abs(2.0*light-1.0)),light]
}
fn from_hsl(hsl) {
    let hue = (hsl[0]-floor(hsl[0]))*6.0
    let chroma = (1.0-abs(2.0*hsl[2]-1.0))*hsl[1]
    let x = chroma*(1.0-abs(hue%2.0-1.0))
    let triples = [[chroma,x,0.0],[x,chroma,0.0],[0.0,chroma,x],[0.0,x,chroma],[x,0.0,chroma],[chroma,0.0,x]];
    [for (channel in triples[int(floor(hue))]) channel+hsl[2]-chroma/2.0]
}
fn hsl_tone(rgb,kind,value) {
    let hsl = to_hsl(rgb)
    let index = if (starts_with(kind,"hue")) 0 else if (starts_with(kind,"sat")) 1 else 2
    let adjusted = if (ends_with(kind,"Mod")) hsl[index]*value/100000.0
        else (if (ends_with(kind,"Off")) hsl[index] else 0.0)+value/(if (index==0) 21600000.0 else 100000.0);
    from_hsl([for (i,item in hsl) if (i!=index) item else if (index==0) adjusted else max(0.0,min(1.0,adjusted))])
}
// DrawingML transforms are ordered; preserve alpha separately from RGB/HSL operations.
fn drawing_tones(channels, tones, index = 0, alpha = 1.0) {
    if (index == len(tones)) {
        if (alpha==1.0) hex.fmt_rgb(channels[0],channels[1],channels[2]) else rgba.css([*channels,alpha])
    }
    else {
        let tone = tones[index]
        let value = c.as_int(c.attr(tone,"val",""),null)
        let hsl = tone.tag in ["hue","hueMod","hueOff","sat","satMod","satOff","lum","lumMod","lumOff"];
        if (tone.uri!=c.A or value==null) null
        else if (hsl) drawing_tones(hsl_tone(channels,tone.tag,value),tones,index+1,alpha)
        else if (not contains(["shade","tint","alpha","alphaMod","alphaOff"],tone.tag) or
            (value<0 and tone.tag!="alphaOff") or (value>100000 and tone.tag in ["shade","tint","alpha"])) null
        else if (tone.tag in ["alpha","alphaMod","alphaOff"])
            drawing_tones(channels,tones,index+1,max(0.0,min(1.0,
                if (tone.tag=="alpha") value/100000.0 else if (tone.tag=="alphaMod") alpha*value/100000.0 else alpha+value/100000.0)))
        // tint retains the specified input fraction and mixes the remainder with white.
        else drawing_tones([for (channel in channels)
            if (tone.tag == "shade") channel*value/100000.0 else 1.0-(1.0-channel)*value/100000.0],tones,index+1,alpha)
    }
}
pub fn drawing_color(node, theme, placeholder = null, allow_tones = false) {
    let colors = c.children(node,null,c.A)
    let color = c.first(colors)
    let value = c.attr(color,"val","")
    let rgb = if (color.tag == "srgbClr") value else if (color.tag == "sysClr") c.attr(color,"lastClr","")
        else if (color.tag == "schemeClr" and value != "phClr") theme.colors[value] else null;
    let tones = c.children(color,null,null)
    let base = if (valid_hex(rgb)) "#" ++ rgb else if (color.tag=="schemeClr" and value=="phClr") placeholder else null;
    if (len(colors) != 1 or len(tones) > 16 or len(tones) > 0 and not allow_tones) null
    else if (len(tones)>0 and base!=null) {
        let channels = rgba.parse(base,"drawing-color") ^ { null };
        if (channels==null) null else drawing_tones(slice(channels,0,3),tones,0,channels[3])
    } else base
}
pub fn drawing_fill(properties, style, theme) {
    let direct = c.child(properties,"solidFill",c.A)
    let ref = c.child(style,"fillRef",c.A)
    let index = c.as_int(c.attr(ref,"idx",""),-1)
    let fill = if (index > 0 and index <= len(theme.fills)) theme.fills[index-1] else null;
    if (c.child(properties,"noFill",c.A) != null) "transparent"
    else if (direct != null) drawing_color(direct,theme)
    else if (fill.tag == "solidFill") drawing_color(fill,theme,drawing_color(ref,theme)) else null
}

fn modified(rgb, shade, tint) {
    let channels = [for (index in [0,2,4]) hex.hex_byte_at(rgb,index)/255.0]
    let lightness = (min(channels)+max(channels))/2.0
    let shaded = if (valid_hex(shade,2)) lightness*hex.hex_byte_at(shade,0)/255.0 else lightness
    let target = if (valid_hex(tint,2)) 1.0-(1.0-shaded)*hex.hex_byte_at(tint,0)/255.0 else shaded
    let chroma_limit = min(lightness,1.0-lightness)
    // scaling distance from HSL lightness preserves hue/saturation across the midpoint.
    let scale = if (chroma_limit == 0) 0 else min(target,1.0-target)/chroma_limit;
    "rgb(" ++ join([for (channel in channels)
        string(int(round(max(0.0,min(1.0,target+(channel-lightness)*scale))*255.0)))],",") ++ ")"
}

pub fn color(node, theme, value_key = "val", theme_key = "themeColor",
    shade_key = "themeShade", tint_key = "themeTint", automatic = "#000000") {
    let name = c.attr(node,theme_key)
    let alias = {text1:"dk1",text2:"dk2",background1:"lt1",background2:"lt2",hyperlink:"hlink",followedHyperlink:"folHlink"}
    let base = theme.colors[c.get(alias,name,name)]
    let value = c.attr(node,value_key);
    if (valid_hex(base)) {
        let shade = c.attr(node,shade_key)
        let tint = c.attr(node,tint_key);
        if (valid_hex(shade,2) or valid_hex(tint,2)) modified(base,shade,tint) else "#" ++ base
    } else if (valid_hex(value)) "#" ++ value
    else if (value == "auto") automatic else null
}

pub fn font(node, theme) {
    let reference = c.get(node.attrs,c.W ++ "#asciiTheme",c.attr(node,"hAnsiTheme"))
    let themed = if (starts_with(reference,"major")) theme.fonts.major
        else if (starts_with(reference,"minor")) theme.fonts.minor else null
    let literal = c.get(node.attrs,c.W ++ "#ascii",c.attr(node,"hAnsi"));
    if (themed != null and themed != "") themed else literal
}
