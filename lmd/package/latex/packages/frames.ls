// framed and mdframed: framed, shaded and barred blocks on the frame-box model
// (boxes.frame_css; vibe/impl/Lambda_Impl_Latex_Packages.md §7).
import util: ~~.util
import color: ~~.elements.color
import boxes: ~~.elements.boxes

// ---- framed ---------------------------------------------------------------------
// \FrameSep is 3\fboxsep (9pt) and \FrameRule is \fboxrule (.4pt); the shaded forms
// take the shadecolor the document defines, and leftbar draws a 3pt bar 10pt from
// the text
pub let FRAMED = ["framed", "oframed", "shaded", "shaded*", "snugshade", "snugshade*", "leftbar"]

// the frame's CSS, or null when a shaded form finds no shadecolor
pub fn framed_css(tag, custom_colors) {
    let shade = color.resolve_color("shadecolor", custom_colors)
    if (tag == "leftbar") boxes.frame_css({width: "3pt", sides: ["left"], padding: "0 0 0 10pt"})
    else if (starts_with(tag, "shaded") or false) (if (shade == null) null
        else boxes.frame_css({background: shade, width: "0", padding: "9pt"}))
    else if (starts_with(tag, "snugshade") or false) (if (shade == null) null
        else boxes.frame_css({background: shade, width: "0"}))
    else boxes.frame_css({width: "0.4pt", padding: "9pt"})
}

// ---- mdframed -------------------------------------------------------------------
pub let MDFRAMED_KEYS = ["linecolor", "linewidth", "backgroundcolor", "topline", "bottomline",
    "leftline", "rightline", "hidealllines", "innertopmargin", "innerbottommargin",
    "innerleftmargin", "innerrightmargin", "innermargin", "roundcorner", "frametitle", "style",
    "skipabove", "skipbelow"]
let SIDES = [{key: "topline", side: "top"}, {key: "rightline", side: "right"},
    {key: "bottomline", side: "bottom"}, {key: "leftline", side: "left"}]

// keys outside the bounded profile
pub fn unknown_keys(keys) => [for (key, value at keys where not (contains(MDFRAMED_KEYS, string(key)) or false)) string(key)]

fn dimension(value, fallback) => if (value == null) fallback else (util.css_dimension(string(value)) or fallback)
fn off(value) => value != null and (value == "false" or value == false)

// {css, title} of an mdframed block, or null when a color does not resolve.
// mdframed's defaults: a .4pt black line on every side, a white background, 10pt
// inner side margins and .4\baselineskip (4.8pt) above and below.
pub fn mdframed_frame(keys, custom_colors) {
    let line = color.resolve_color(string(keys.linecolor or "black"), custom_colors)
    let background = color.resolve_color(string(keys.backgroundcolor or "white"), custom_colors)
    let hidden = keys.hidealllines != null and not off(keys.hidealllines)
    let sides = if (hidden) [] else [for (entry in SIDES where not off(keys[entry.key])) entry.side]
    let margin = if (keys.innermargin == null) null else dimension(keys.innermargin, null)
    let top = dimension(keys.innertopmargin, margin or "4.8pt")
    let bottom = dimension(keys.innerbottommargin, margin or "4.8pt")
    let left = dimension(keys.innerleftmargin, margin or "10pt")
    let right = dimension(keys.innerrightmargin, margin or "10pt")
    if (line == null or background == null) null
    else {css: boxes.frame_css({background: background, line: line, width: if (len(sides) == 0) "0"
            else dimension(keys.linewidth, "0.4pt"),
        sides: if (len(sides) == 4) null else sides,
        padding: top ++ " " ++ right ++ " " ++ bottom ++ " " ++ left,
        radius: if (keys.roundcorner == null) null else dimension(keys.roundcorner, null)}),
     title: keys.frametitle}
}
