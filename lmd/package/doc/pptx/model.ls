// Convert saved PresentationML data into lambda.slide objects; no source becomes executable code (S1.8).
import c: .common
import package: .package
import text: .text
import theme: lambda.doc.docx.theme
import drawing: lambda.doc.docx.drawing
import charts: lambda.doc.docx.charts
import images: .images
import fills: .paint
import geometry: .geometry

fn result(nodes=[],diagnostics=[]) => {nodes:nodes,diagnostics:diagnostics}
fn placeholder(node) => c.descendants(node,"ph",c.P)[0]
fn tree(owner) {
    let root = c.get(owner,"root");
    c.children(c.child(c.child(root,"cSld"),"spTree"),null,null)
}
fn inherited(node,owner,master=false) {
    let ph = placeholder(node)
    let index = c.value(c.attr(ph,"idx"),"0");
    let kind = c.value(c.attr(ph,"type"),"obj")
    let master_kind = if (kind=="ctrTitle") "title" else if (kind in ["subTitle","obj"]) "body" else kind;
    if (ph==null) null else [for (item in tree(owner),let other=placeholder(item) where other!=null and
        (if (master) c.value(c.attr(other,"type"),"obj")==master_kind else c.value(c.attr(other,"idx"),"0")==index)) item][0]
}
fn chain(node,ctx) {
    let layout = inherited(node,ctx.layout)
    let master = inherited(if (layout==null) node else layout,ctx.master,true);
    [for (item in [master,layout,node] where item!=null) item]
}
fn box(nodes,ctx,group=false) {
    let transforms = [for (node in nodes,let value=if (group) c.child(c.child(node,"grpSpPr"),"xfrm",c.A)
        else if (node.tag=="graphicFrame") c.child(node,"xfrm") else c.child(c.child(node,"spPr"),"xfrm",c.A) where value!=null) value]
    let value = transforms[last]
    let off = c.child(value,"off",c.A)
    let ext = c.child(value,"ext",c.A)
    let scale = c.get(ctx,"coordinates",{sx:1.0,sy:1.0,x:0.0,y:0.0})
    let width = c.pixels(c.attr(ext,"cx"),ctx.width)*scale.sx
    let height = c.pixels(c.attr(ext,"cy"),ctx.height)*scale.sy;
    {x:c.pixels(c.attr(off,"x"))*scale.sx+scale.x,y:c.pixels(c.attr(off,"y"))*scale.sy+scale.y,
        width:width,height:height,rotation:c.num(c.attr(value,"rot"))/60000.0,
        valid:value!=null and width>=0 and height>=0,source:value,
        style:if (c.truth(c.attr(value,"flipH")) or c.truth(c.attr(value,"flipV")))
            "transform:scale(" ++ (if (c.truth(c.attr(value,"flipH"))) "-1" else "1") ++ "," ++
            (if (c.truth(c.attr(value,"flipV"))) "-1" else "1") ++ ");" else ""}
}
fn placement(bounds,id,metadata={}) => {id:id,x:bounds.x,y:bounds.y,width:bounds.width,height:bounds.height,
    rotation:bounds.rotation,metadata:metadata}
fn properties(nodes,tag="spPr") => text.merge([for (node in nodes) c.child(node,tag)])
fn decoration(props,style,ctx) {
    let line = c.child(props,"ln",c.A)
    let line_ref = c.child(style,"lnRef",c.A)
    let ref_index = int(c.num(c.attr(line_ref,"idx")))
    let inherited_line = if (ref_index>0) ctx.theme.lines[ref_index-1] else null
    let stroke = theme.drawing_color(c.child(line,"solidFill",c.A),ctx.theme,null,true)
    let color = if (c.child(line,"noFill",c.A)!=null) "transparent" else if (stroke!=null) stroke
        else theme.drawing_color(c.child(inherited_line,"solidFill",c.A),ctx.theme,theme.drawing_color(line_ref,ctx.theme),true);
    {fill:theme.drawing_fill(props,style,ctx.theme),stroke:color,
        stroke_width:c.pixels(c.attr(line,"w"),c.pixels(c.attr(inherited_line,"w"),1.0))}
}
fn shape(node,nodes,bounds,id,ctx) map^ {
    let props = properties(nodes)
    let style = [for (item in nodes,let value=c.child(item,"style") where value!=null) value][last]
    let fill = fills.fill(props,style,ctx,bounds)^
    let paint = {*:decoration(props,style,ctx),fill:fill.color}
    let preset = c.attr(c.child(props,"prstGeom",c.A),"prst")
    let kind = if (preset in ["ellipse","arc"]) 'ellipse' else if (node.tag=="cxnSp" or preset=="line") 'line' else 'rect'
    let bodies = [for (item in nodes,let body=c.child(item,"txBody") where body!=null) body]
    let ph = c.attr(placeholder(node),"type")
    let styles = [ctx.default_text,c.child(c.child(ctx.master.root,"txStyles"),
        if (ph in ["title","ctrTitle"]) "titleStyle" else if (ph in ["body","obj"]) "bodyStyle" else "otherStyle")]
    let attrs = placement(bounds,id,{name:c.attr(c.descendants(node,"cNvPr",c.P)[0],"name")})
    let radius = if (preset=="roundRect") min(bounds.width,bounds.height)*0.1666666667 else 0.0
    let has_text = any([for (t in c.descendants(bodies[last],"t",c.A)) c.text(t)!=""]) or
        placeholder(node)!=null or c.truth(c.attr(c.child(c.child(node,"nvSpPr"),"cNvSpPr"),"txBox"))
    let custom = c.child(props,"custGeom",c.A)
    let paths = geometry.paths(custom,bounds)
    let custom_supported = paths!=null and fill.style=="" and not has_text
    let node_style = bounds.style ++ fill.style ++ c.css({["background-color"]:if (has_text) paint.fill else null,
        border:if (has_text and paint.stroke!=null) c.px(paint.stroke_width) ++ " solid " ++ paint.stroke else null,
        ["border-radius"]:if (kind=='ellipse') "50%" else if (radius>0) c.px(radius) else null})
    let unsupported_geometry = preset!=null and not (preset in ["rect","roundRect","ellipse","line"])
    let unsupported_effect = c.child(props,"effectDag",c.A)!=null or
        len(c.children(c.child(props,"effectLst",c.A),null,null))>0;
    let stroke_width = max(0.0,paint.stroke_width)
    // SVG line bounds include its stroke, including horizontal/vertical lines with a zero extent.
    let line_width = max(bounds.width,stroke_width)
    let line_height = max(bounds.height,stroke_width)
    let dx = (line_width-bounds.width)/2.0
    let dy = (line_height-bounds.height)/2.0;
    result([if (custom_supported) <content *:attrs,style:bounds.style,
            <svg viewBox:"0 0 " ++ string(bounds.width) ++ " " ++ string(bounds.height),width:"100%",height:"100%",preserveAspectRatio:"none",
                *[for (path in paths) <path d:path.d,fill:if (path.fill) paint.fill else "none",
                    stroke:if (path.stroke) c.value(paint.stroke,"none") else "none",'stroke-width':stroke_width>]>>
        else if (kind=='line') <content *:placement({*:bounds,x:bounds.x-dx,y:bounds.y-dy,width:line_width,height:line_height},id),style:bounds.style,
            <svg viewBox:"0 0 " ++ string(line_width) ++ " " ++ string(line_height),width:"100%",height:"100%",
                <line x1:dx,y1:dy,x2:dx+bounds.width,y2:dy+bounds.height,stroke:c.value(paint.stroke,c.value(paint.fill,"#000000")),
                    'stroke-width':stroke_width>>>
        else if (has_text) <text *:attrs,font_size:24.0,color:"#000000",style:node_style,text.body(bodies,styles,ctx)>
        else <shape *:attrs,kind:kind,fill:c.get(paint,"fill","transparent"),stroke:c.get(paint,"stroke","none"),
            stroke_width:paint.stroke_width,radius:radius,style:node_style>],
        fill.diagnostics ++ text.diagnostics(node,ctx) ++
        (if (unsupported_geometry) [c.warning(ctx,"unsupported-shape","Shape " ++ string(preset) ++ " uses rectangular bounds")] else []) ++
        (if (custom!=null and not custom_supported) [c.warning(ctx,"unsupported-geometry","A custom shape is outside the numeric solid-path profile")] else []) ++
        (if (unsupported_effect) [c.warning(ctx,"unsupported-effect","A shape effect is outside the import profile")] else []))
}
fn picture(node,bounds,id,ctx) map^ {
    let blip = c.descendants(node,"blip",c.A)[0]
    let desc = c.descendants(node,"cNvPr",c.P)[0]
    let attrs = placement(bounds,id,{name:c.attr(desc,"name"),description:c.attr(desc,"descr")})
    let image = images.embedded(blip,ctx,bounds)^;
    if (image.src==null) result([],image.diagnostics)
    else {
        let cropped = drawing.crop(node,bounds.width,bounds.height);
        result([if (cropped.value==null) <image *:attrs,src:image.src,fit:'fill',style:bounds.style>
            else <content *:attrs,style:bounds.style,<div style:cropped.value.box,
                <img src:image.src,alt:c.value(c.attr(desc,"descr"),""),style:cropped.value.image>>>],
            image.diagnostics ++ (if (cropped.unsupported) [c.warning(ctx,"unsupported-image-crop","Unsupported image crop was omitted")] else []))
    }
}
fn table(node,bounds,id,ctx) {
    let columns = c.children(c.child(node,"tblGrid",c.A),"gridCol",c.A)
    let rows = c.children(node,"tr",c.A)
    let merges = c.descendants(node,"tc",c.A)
    let unsupported = any([for (cell in merges) c.truth(c.attr(cell,"hMerge")) or c.truth(c.attr(cell,"vMerge")) or
        c.num(c.attr(cell,"gridSpan"),1.0)>1 or c.num(c.attr(cell,"rowSpan"),1.0)>1]);
    result([<content *:placement(bounds,id),<table style:"border-collapse:collapse;table-layout:fixed;width:100%;height:100%;color:#000000;font-size:24px;",
        <colgroup *[for (column in columns) <col style:"width:" ++ c.px(c.pixels(c.attr(column,"w"))) ++ ";">]>
        <tbody *[for (row in rows) <tr style:"height:" ++ c.px(c.pixels(c.attr(row,"h"))) ++ ";",
            *[for (cell in c.children(row,"tc",c.A),let props=c.child(cell,"tcPr",c.A))
                <td style:"border:1px solid #cbd5e1;padding:0;" ++
                    c.css({background:theme.drawing_color(c.child(props,"solidFill",c.A),ctx.theme,null,true),
                        ["vertical-align"]:c.value({ctr:"middle",b:"bottom"}[c.attr(props,"anchor")],"top")}),
                    text.body([c.child(cell,"txBody",c.A)],[],ctx,false)>]>]>>>],
        if (unsupported) [c.warning(ctx,"unsupported-table-merge","Merged table cells are shown as their saved grid cells")] else [])
}
fn graphic(node,bounds,id,ctx) map^ {
    let tbl = c.descendants(node,"tbl",c.A)[0]
    let chart = c.descendants(node,"chart","http://schemas.openxmlformats.org/drawingml/2006/chart")[0];
    if (tbl!=null) table(tbl,bounds,id,ctx)
    else if (chart!=null) {
        let rendered = charts.read(ctx,chart,bounds.width,bounds.height)^;
        result([for (item in rendered.nodes) <content *:placement(bounds,id),item.svg>],
            [for (issue in rendered.diagnostics) {*:issue,part:ctx.main}])
    } else result([],[c.warning(ctx,"unsupported-graphic","A graphic frame (such as SmartArt or an embedded object) was omitted")])
}
fn objects(items,ctx,prefix,depth=0) map^ {
    let converted = [for (i,node in items where not (node.tag in ["nvGrpSpPr","grpSpPr","extLst"])) {
        let id = prefix ++ "-" ++ string(i)
        let nodes = chain(node,ctx)
        let bounds = box(nodes,ctx,node.tag=="grpSp");
        if (depth>32) result([],[c.warning(ctx,"unsupported-group-depth","Group nesting exceeds 32 levels")])
        else if (node.uri==c.MC and node.tag=="AlternateContent") {
            let fallback = c.child(node,"Fallback",c.MC);
            if (fallback==null) result([],[c.warning(ctx,"unsupported-alternate-content","No supported compatibility fallback")])
            else objects(c.children(fallback,null,null),ctx,id,depth+1)^
        }
        else if (node.uri!=c.P or not (node.tag in ["sp","cxnSp","pic","graphicFrame","grpSp"]))
            result([],[c.warning(ctx,"unsupported-element","Unsupported slide element " ++ string(node.tag))])
        else if (node.tag=="grpSp") {
            let child_off = c.child(bounds.source,"chOff",c.A)
            let child_ext = c.child(bounds.source,"chExt",c.A)
            let cw = c.pixels(c.attr(child_ext,"cx"),bounds.width)
            let ch = c.pixels(c.attr(child_ext,"cy"),bounds.height)
            let sx = if (cw<=0) 1.0 else bounds.width/cw
            let sy = if (ch<=0) 1.0 else bounds.height/ch
            let nested = objects(c.children(node,null,c.P),{*:ctx,coordinates:{sx:sx,sy:sy,
                x:-c.pixels(c.attr(child_off,"x"))*sx,y:-c.pixels(c.attr(child_off,"y"))*sy},text_scale:sy},id,depth+1)^;
            if (bounds.width<=0 or bounds.height<=0) result([],[c.warning(ctx,"invalid-object-bounds","A group with empty bounds was omitted")])
            else result([<group *:placement(bounds,id),style:bounds.style,*nested.nodes>],nested.diagnostics ++
                (if (cw<=0 or ch<=0 or abs(sx-sy)>0.000001) [c.warning(ctx,"unsupported-group-transform","A nonuniform or invalid group transform uses scaled object bounds")] else []))
        } else if ((bounds.width<=0 or bounds.height<=0) and node.tag!="cxnSp" and
            c.attr(c.child(properties(nodes),"prstGeom",c.A),"prst")!="line") {
            result([],[c.warning(ctx,"invalid-object-bounds","An object with empty bounds was omitted")])
        } else {
            let converted = if (node.tag=="pic") picture(node,bounds,id,ctx)^ else if (node.tag=="graphicFrame") graphic(node,bounds,id,ctx)^
                else shape(node,nodes,bounds,id,ctx)^;
            {*:converted,diagnostics:converted.diagnostics ++
                (if (bounds.valid) [] else [c.warning(ctx,"missing-object-bounds","An object has missing or invalid bounds; slide bounds were used")])}
        }
    }];
    result([for (item in converted) *item.nodes],[for (item in converted) *item.diagnostics])
}
fn background(ctx) {
    let backgrounds = [for (owner in [ctx.master,ctx.layout,ctx],let bg=c.child(c.child(owner.root,"cSld"),"bg") where bg!=null) bg]
    let bg = backgrounds[last]
    let ref = c.child(bg,"bgRef")
    let idx = int(c.num(c.attr(ref,"idx")))
    let saved_fill = if (idx>=1001) ctx.theme.background_fills[idx-1001] else if (idx>0) ctx.theme.fills[idx-1] else null
    let fill = c.value(c.child(c.child(bg,"bgPr"),"solidFill",c.A),if (saved_fill.tag=="solidFill") saved_fill else null)
    let color = theme.drawing_color(fill,ctx.theme,theme.drawing_color(ref,ctx.theme,null,true),true);
    {color:c.value(color,"#ffffff"),
        diagnostics:if (bg!=null and color==null) [c.warning(ctx,"unsupported-background","Only solid slide backgrounds are imported")] else []}
}
fn notes(pkg,ctx) array^ {
    let owner = package.linked(pkg,ctx,"notesSlide","notes")^;
    [for (node in tree(owner),let ph=c.attr(placeholder(node),"type") where node.tag=="sp" and not (ph in ["sldImg","sldNum","dt","ftr","hdr"]))
        <notes join([for (p in c.children(c.child(node,"txBody"),"p",c.A))
            join([for (t in c.descendants(p,"t",c.A)) c.text(t)],"")],"\n")>]
}
pub fn build(pkg) map^ {
    let scenes = [for (i,item in pkg.slides) {
        let ctx = {*:package.context(pkg,item)^,types:pkg.types,default_text:c.child(pkg.root,"defaultTextStyle")}
        let sources = [*if (c.attr(ctx.root,"showMasterSp") in ["0","false"]) [] else [ctx.master,ctx.layout],ctx]
        let converted = [for (j,owner in sources where owner!=null)
            objects([for (node in tree(owner) where owner.main==ctx.main or placeholder(node)==null) node],
                {*:ctx,main:owner.main,relationships:owner.relationships},"s" ++ string(i) ++ "-" ++ string(j))^]
        let bg = background(ctx)
        let transition = c.child(ctx.root,"transition")
        let timing = c.child(ctx.root,"timing")
        let title = [for (node in tree(ctx),let ph=c.attr(placeholder(node),"type") where ph in ["title","ctrTitle"])
            join([for (t in c.descendants(node,"t",c.A)) c.text(t)],"")][0]
        let csld = c.child(ctx.root,"cSld")
        let speaker = notes(pkg,ctx)^;
        {node:<slide id:item.id,title:c.value(title,c.value(c.attr(csld,"name"),"Slide " ++ string(i+1))),background:bg.color,
            *[*[for (converted_part in converted) *converted_part.nodes],*speaker]>,
            diagnostics:[*[for (converted_part in converted) *converted_part.diagnostics],*bg.diagnostics,
                *if (transition==null) [] else [c.warning(ctx,"unsupported-transition","The saved slide transition was omitted")],
                *if (timing==null) [] else [c.warning(ctx,"unsupported-animation","Saved animation timing was omitted")]]}
    }]
    let document = <presentation title:c.get(pkg.metadata,"title","Imported presentation"),width:pkg.width,height:pkg.height,
        theme:{base:'light',foreground:"#000000",font_family:"Arial, sans-serif"},*[for (scene in scenes) scene.node]>;
    {document:document,text:join([for (scene in scenes)
        join([for (node in content(scene.node) where name(node) in ['text','content','group']) text.plain(node)],"\n")],"\n\n"),
        diagnostics:[for (scene in scenes) *scene.diagnostics]}
}
