// Theme references are package data; only validated colors and quoted font names reach CSS (S1.8).
import c: .common
import package: .package
import hex: lambda.doc.pdf.util

pub fn valid_hex(value, count = 6) => value is string and len(value) == count and
    all([for (ch in split(value,"")) hex.is_hex_digit(ch)])

pub fn load(pkg) map^ {
    let root = package.related_xml(pkg,"theme")^
    let elements = c.child(root,"themeElements",c.A)
    let scheme = c.child(elements,"clrScheme",c.A)
    let fonts = c.child(elements,"fontScheme",c.A);
    if (root != null and (root.uri != c.A or root.tag != "theme"))
        raise c.fail("invalid-package","Invalid document theme",pkg.main)
    else {fills:c.children(c.child(c.child(elements,"fmtScheme",c.A),"fillStyleLst",c.A),null,c.A),
        effects:c.children(c.child(c.child(elements,"fmtScheme",c.A),"effectStyleLst",c.A),null,c.A),
        colors:map([for (entry in c.children(scheme,null,c.A),
            let rgb = c.child(entry,"srgbClr",c.A), let system = c.child(entry,"sysClr",c.A),
            let value = if (rgb != null) c.attr(rgb,"val","") else c.attr(system,"lastClr","")
            where valid_hex(value)) (entry.tag,value)]),
        fonts:map([for (kind in ["major","minor"])
            (kind,c.attr(c.child(c.child(fonts,kind ++ "Font",c.A),"latin",c.A),"typeface",""))])}
}

// DrawingML colors use unqualified attributes; unsupported transforms must not become guessed colors.
pub fn drawing_color(node, theme, placeholder = null) {
    let colors = c.children(node,null,c.A)
    let color = c.first(colors)
    let value = c.attr(color,"val","")
    let rgb = if (color.tag == "srgbClr") value else if (color.tag == "sysClr") c.attr(color,"lastClr","")
        else if (color.tag == "schemeClr" and value != "phClr") theme.colors[value] else null;
    if (len(colors) != 1 or len(c.children(color,null,null)) > 0) null
    else if (color.tag == "schemeClr" and value == "phClr") placeholder
    else if (valid_hex(rgb)) "#" ++ rgb else null
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
