// Paragraph-relative anchors lower to CSS floats; page-relative placement remains diagnosed.
import c: .common

fn offset(node) => if (node == null) null else int(c.text(node)) ^ { null }

pub fn placement(node, width) {
    let anchor = c.first(c.descendants(node,"anchor",c.WP))
    let h = c.child(anchor,"positionH",c.WP)
    let v = c.child(anchor,"positionV",c.WP)
    let align = c.text(c.child(h,"align",c.WP))
    let h_offset = offset(c.child(h,"posOffset",c.WP))
    let v_offset = offset(c.child(v,"posOffset",c.WP))
    let square = c.child(anchor,"wrapSquare",c.WP)
    let block_wrap = c.child(anchor,"wrapTopAndBottom",c.WP) != null
    let wrap = c.attr(square,"wrapText","")
    let aligned = h_offset == null and contains(["left","center","right"],align)
    let column_start = align == "" and h_offset != null and h_offset <= 0
    let vertical = (v_offset == null and c.text(c.child(v,"align",c.WP)) == "top") or
        (v_offset != null and c.child(v,"align",c.WP) == null)
    let supported = anchor != null and width > 0 and
        not contains(["1","true"],c.attr(anchor,"behindDoc","")) and
        not contains(["1","true"],c.attr(anchor,"simplePos","")) and
        c.attr(h,"relativeFrom","") == "column" and (aligned or column_start) and
        c.attr(v,"relativeFrom","") == "paragraph" and vertical and
        (block_wrap and aligned or square != null and
            (column_start and wrap == "bothSides" or
             align == "center" and wrap == "largest" or
             align == "left" and contains(["bothSides","right","largest"],wrap) or
             align == "right" and contains(["bothSides","left","largest"],wrap)))
    let effect = c.child(anchor,"effectExtent",c.WP)
    let gaps = [for (side in ["T","R","B","L"])
        c.pixels(c.attr(anchor,"dist" ++ side,"")) + c.pixels(c.attr(effect,lower(side),""))];
    if (anchor == null) {value:null,unsupported:false}
    else if (not supported) {value:null,unsupported:true}
    else {
        // offsets place the picture, while the float's padding reserves its wrap distances.
        let box = if (h_offset != null) "display:block;float:left;margin-left:" ++
                string(h_offset/9525.0-gaps[3]) ++ "px;"
            else if (block_wrap) "display:block;clear:both;width:100%;text-align:" ++ align ++ ";"
            else if (align == "center")
                "display:block;float:left;width:calc(50% + " ++ string(width/2.0+gaps[1]) ++ "px);max-width:100%;"
            else "display:block;float:" ++ align ++ ";"
        let vertical_style = if (v_offset == null) "" else "margin-top:" ++
            string(v_offset/9525.0-gaps[0]) ++ "px;"
        let spacing = "padding:" ++ join([for (gap in gaps) string(max(0.0,gap)) ++ "px"]," ") ++ ";"
        let image = if (not block_wrap and align == "center") "display:block;margin-left:auto;height:auto;"
            else "height:auto;";
        {value:{box:"box-sizing:border-box;" ++ box ++ vertical_style ++ spacing,image:image},unsupported:false}
    }
}
