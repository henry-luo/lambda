// Column/paragraph anchors lower to floats or layered no-wrap boxes; page placement remains diagnosed.
import c: .common
import theme: .theme

fn offset(node) => if (node == null) null else int(c.text(node)) ^ { null }
fn zero_attribute(node, key) => c.attr(node,key,"") == null or c.as_int(c.attr(node,key,""),null) == 0

pub fn placement(node, width, height) {
    let anchor = c.first(c.descendants(node,"anchor",c.WP))
    let h = c.child(anchor,"positionH",c.WP)
    let v = c.child(anchor,"positionV",c.WP)
    let align = c.text(c.child(h,"align",c.WP))
    let h_offset = offset(c.child(h,"posOffset",c.WP))
    let v_offset = offset(c.child(v,"posOffset",c.WP))
    let square = c.child(anchor,"wrapSquare",c.WP)
    let effect = c.child(anchor,"effectExtent",c.WP)
    let gaps = [for (side in ["T","R","B","L"])
        c.pixels(c.attr(anchor,"dist" ++ side,"")) + c.pixels(c.attr(effect,lower(side),""))]
    let block_wrap = c.child(anchor,"wrapTopAndBottom",c.WP) != null
    let wrap = c.attr(square,"wrapText","")
    let aligned = h_offset == null and contains(["left","center","right"],align)
    // a positive image offset still wraps at the column edge when its clearance reaches that edge.
    let column_start = align == "" and h_offset != null and h_offset/9525.0 <= gaps[3]
    let vertical = (v_offset == null and c.text(c.child(v,"align",c.WP)) == "top") or
        (v_offset != null and c.child(v,"align",c.WP) == null)
    let relative = anchor != null and width > 0 and
        not contains(["1","true"],c.attr(anchor,"simplePos","")) and
        c.attr(h,"relativeFrom","") == "column" and c.attr(v,"relativeFrom","") == "paragraph"
    let layer = c.as_int(c.attr(anchor,"relativeHeight",""),-1)
    let absolute = relative and height > 0 and h_offset != null and v_offset != null and
        c.child(h,"align",c.WP) == null and c.child(v,"align",c.WP) == null and
        c.child(anchor,"wrapNone",c.WP) != null and layer >= 0 and layer <= 4294967295
    let supported = relative and
        not contains(["1","true"],c.attr(anchor,"behindDoc","")) and
        (aligned or column_start) and vertical and
        (block_wrap and aligned or square != null and
            (column_start and wrap == "bothSides" or
             align == "center" and wrap == "largest" or
             align == "left" and contains(["bothSides","right","largest"],wrap) or
             align == "right" and contains(["bothSides","left","largest"],wrap)));
    if (anchor == null) {value:null,unsupported:false}
    else if (absolute) {value:{absolute:true,order:layer,
        behind:contains(["1","true"],c.attr(anchor,"behindDoc","")),
        box:"position:absolute;display:block;left:" ++ string(h_offset/9525.0) ++ "px;top:" ++
            string(v_offset/9525.0) ++ "px;width:" ++ string(width) ++ "px;height:" ++ string(height) ++ "px;",
        image:"display:block;max-width:none;width:100%;height:100%;"},unsupported:false}
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

pub fn crop(node, width, height) {
    let rect = c.first(c.descendants(node,"srcRect",c.A))
    let values = [for (side in ["l","t","r","b"]) c.as_int(c.attr(rect,side,""),
        if (c.attr(rect,side,"") == null) 0 else -1)]
    let x = 100000-values[0]-values[2]
    let y = 100000-values[1]-values[3]
    let supported = width > 0 and height > 0 and x > 0 and y > 0 and
        all([for (v in values) v >= 0 and v <= 100000]) and
        len(c.descendants(node,"stretch",c.A)) == 1 and
        all([for (rect in c.descendants(node,"fillRect",c.A), side in ["l","t","r","b"]) zero_attribute(rect,side)]);
    if (rect == null or all([for (v in values) v == 0])) {value:null,unsupported:false}
    else if (not supported) {value:null,unsupported:true}
    else {value:{box:"position:relative;display:inline-block;overflow:hidden;width:" ++ string(width) ++
        "px;height:" ++ string(height) ++ "px;vertical-align:middle;",
        image:"position:absolute;max-width:none;width:" ++ string(10000000.0/x) ++ "%;height:" ++
            string(10000000.0/y) ++ "%;left:" ++ string(-100.0*values[0]/x) ++
            "%;top:" ++ string(-100.0*values[1]/y) ++ "%;"},unsupported:false}
}

// only flat preset rectangles without text, stroke, effects or transforms enter this shape profile.
pub fn shape(node, colors) {
    let shapes = c.descendants(node,"wsp",c.WPS)
    let shape = c.first(shapes)
    let props = c.child(shape,"spPr",c.WPS)
    let style = c.child(shape,"style",c.WPS)
    let transform = c.child(props,"xfrm",c.A)
    let origin = c.child(transform,"off",c.A)
    let geometry = c.child(props,"prstGeom",c.A)
    let effect_ref = c.child(style,"effectRef",c.A)
    let effect = c.as_int(c.attr(effect_ref,"idx",""),if (effect_ref == null) 0 else -1)
    let effect_style = if (effect > 0 and effect <= len(colors.effects)) colors.effects[effect-1] else null
    let fill = theme.drawing_fill(props,style,colors)
    let allowed = len(shapes) == 1 and c.attr(geometry,"prst","") == "rect" and fill != null and
        len(c.descendants(node,"txbxContent",c.W)) == 0 and len(c.descendants(node,"blip",c.A)) == 0 and
        c.child(c.child(props,"ln",c.A),"noFill",c.A) != null and
        zero_attribute(transform,"rot") and
        not contains(["1","true"],c.attr(transform,"flipH","")) and
        not contains(["1","true"],c.attr(transform,"flipV","")) and
        zero_attribute(origin,"x") and zero_attribute(origin,"y") and
        len(c.descendants(geometry,"gd",c.A)) == 0 and
        all([for (child in c.children(shape,null,null)) child.uri == c.WPS and
            contains(["cNvSpPr","spPr","style","bodyPr"],child.tag)]) and
        all([for (child in c.children(props,null,null)) child.uri == c.A and
            contains(["xfrm","prstGeom","solidFill","noFill","ln","effectLst"],child.tag)]) and
        len(c.children(c.child(props,"effectLst",c.A),null,null)) == 0 and
        (effect == 0 or len(c.children(effect_style,null,null)) == 1 and
            c.child(effect_style,"effectLst",c.A) != null and
            len(c.children(c.child(effect_style,"effectLst",c.A),null,null)) == 0);
    if (allowed) {kind:"shape",fill:fill} else null
}
pub fn shape_choice(choice, colors) {
    let nodes = c.children(choice,null,null);
    len(nodes) > 0 and all([for (node in nodes) node.uri == c.W and node.tag == "drawing" and shape(node,colors) != null])
}
pub fn raster_choice(choice) {
    let nodes = c.children(choice,null,null);
    len(nodes) > 0 and all([for (node in nodes) node.uri == c.W and node.tag == "drawing" and
        len(c.descendants(node,"blip",c.A)) == 1 and len(c.descendants(node,"txbxContent",c.W)) == 0 and
        len(c.descendants(node,"wsp",c.WPS)) == 0 and len(c.descendants(node,"chart",c.CH)) == 0 and
        len(c.descendants(node,"relIds",c.DGM)) == 0])
}
