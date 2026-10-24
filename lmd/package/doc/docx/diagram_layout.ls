// Composite/linear SmartArt layouts are interpreted as bounded data, never executable source (S1.8).
import c: .common
import theme: .theme
import metrics: lambda.chart.text

fn child(node, tag) => c.child(node,tag,c.DGM)
fn children(node, tag = null) => c.children(node,tag,c.DGM)
fn attr(node, key, fallback = null) => c.get(node.attrs,"#" ++ key,fallback)
fn unsupported(message) any^ => raise c.fail("unsupported","SmartArt: " ++ message)
fn numeric(value, fallback) float^ {
    let parsed = if (value == null) fallback else float(value) ^ {null};
    if (parsed == null or parsed != parsed or abs(parsed) > 1000000) unsupported("Invalid layout numeric")^
    else parsed
}
fn descendants(graph, id, seen = []) array^ {
    if (contains(seen,id) or len(seen) >= 128) unsupported("Cyclic or over-deep data graph")^
    else [for (edge in graph.edges where edge.from == id)
        *([edge.to] ++ descendants(graph,edge.to,seen ++ [id])^)]
}
fn axis(graph, ids, kind) array^ => [for (id in ids)
    if (kind == "self") id else if (kind == "ch")
        *[for (edge in graph.edges where edge.from == id) edge.to]
    else if (kind == "desOrSelf") *([id] ++ descendants(graph,id)^)
    else unsupported("Unsupported selection axis " ++ kind)^]
fn select(graph, ids, source, index = 0) array^ {
    let axes = split(attr(source,"axis","")," ");
    if (index == len(axes) or axes[index] == "") ids
    else {
        let types = split(attr(source,"ptType","all")," ")
        let starts = split(attr(source,"st","1")," ")
        let counts = split(attr(source,"cnt","0")," ")
        let steps = split(attr(source,"step","1")," ")
        let start = numeric(c.get(starts,index,"1"),1)^-1
        let count = numeric(c.get(counts,index,"0"),0)^
        let step = numeric(c.get(steps,index,"1"),1)^
        let point_type = c.get(types,index,"all")
        let available = [for (id in axis(graph,ids,axes[index])^
            where point_type == "all" or graph.points[id].kind == point_type) id];
        if (start < 0 or step < 1 or count < 0 or int(start) != start or int(step) != step or int(count) != count)
            unsupported("Unsupported iterator bounds")^
        else select(graph,[for (i,id in available where i >= start and (i-start) % step == 0 and
            (count == 0 or (i-start)/step < count)) id],source,index+1)^
    }
}
fn effective(nodes, variables) array^ => [for (node in nodes)
    if (node.tag != "choose") node else {
        let conditions = children(node,"if")
        let invalid = any([for (test in conditions) attr(test,"func") != "var" or
            attr(test,"op") != "equ" or variables[attr(test,"arg")] == null]);
        if (invalid) unsupported("Unsupported layout condition")^
        else {
            let chosen = c.first([for (test in conditions where variables[attr(test,"arg")] == attr(test,"val")) test])
            let branch = if (chosen == null) child(node,"else") else chosen;
            *effective(children(branch),variables)^
        }
    }]
fn build_many(nodes, ids, variables, graph, depth) array^ {
    let unknown = [for (source in nodes where not contains(
        ["layoutNode","forEach","alg","shape","presOf","constrLst","ruleLst","varLst"],source.tag)) source.tag];
    if (len(unknown) > 0) unsupported("Unsupported layout instruction " ++ unknown[0])^
    else [for (source in nodes where contains(["layoutNode","forEach"],source.tag))
        if (source.tag == "layoutNode") build(source,ids,variables,graph,depth+1)^
        else *[for (id in select(graph,ids,source)^) *build_many(children(source),[id],variables,graph,depth+1)^]]
}
fn build(source, ids, inherited, graph, depth) map^ {
    let variables = {*:inherited,*:map([for (node in children(child(source,"varLst")))
        (node.tag,attr(node,"val",if (node.tag == "dir") "norm" else "0"))])}
    let nodes = effective(children(source),variables)^
    let algorithm = c.first([for (node in nodes where node.tag == "alg") node])
    let shape = child(source,"shape")
    let kind = attr(algorithm,"type")
    let invalid_constraints = any([for (rule in children(child(source,"constrLst")))
        if (attr(rule,"for") == null or attr(rule,"for") == "self")
            not contains(["lMarg","rMarg","tMarg","bMarg"],attr(rule,"type"))
        else not contains(["ch","des"],attr(rule,"for")) or attr(rule,"forName") == null or
            not contains(["w","h","l","t","primFontSz"],attr(rule,"type"))]);
    if (depth > 16 or not contains(["composite","lin","tx","sp"],kind) or
        (attr(shape,"type") != null and not contains(["","rect"],attr(shape,"type"))) or
        c.attr(shape,"blip",c.R) != null and c.attr(shape,"blip",c.R) != "" or invalid_constraints or
        len(children(child(shape,"adjLst"))) > 0)
        unsupported("Unsupported algorithm, shape or layout depth")^
    else {
        let nested = build_many(nodes,ids,variables,graph,depth)^
        let count = 1+sum([for (node in nested) node.count]);
        if (count > 256) unsupported("Layout expansion exceeds its node budget")^
        else {source:source,ids:ids,variables:variables,algorithm:algorithm,kind:kind,
            children:nested,count:count,shape:attr(shape,"type"),name:attr(source,"name","")}
    }
}
fn rules(node, box) => [for (rule in children(child(node.source,"constrLst"))) {source:rule,owner:node.source,box:box}]
fn applicable(rule, parent, name) => attr(rule.source,"forName","") == name and
    (attr(rule.source,"for") == "des" or attr(rule.source,"for") == "ch" and rule.owner == parent.source)
fn rule_value(rule, all_rules) float^ {
    let value = attr(rule.source,"val")
    let reference = attr(rule.source,"refForName")
    let kind = attr(rule.source,"refType")
    let referred = c.first([for (candidate in all_rules where attr(candidate.source,"forName") == reference and
        attr(candidate.source,"type") == kind and attr(candidate.source,"val") != null) candidate]);
    if (value != null) numeric(value,0)^
    else if (reference != null and referred != null) numeric(attr(referred.source,"val"),0)^*numeric(attr(rule.source,"fact"),1)^
    else if (contains(["w","h"],kind)) rule.box[kind]*numeric(attr(rule.source,"fact"),1)^
    else unsupported("Unsupported constraint reference")^
}
fn values(node, parent, all_rules) map^ => map([for (rule in all_rules where applicable(rule,parent,node.name) and
    contains(["w","h","l","t","primFontSz"],attr(rule.source,"type")))
    (attr(rule.source,"type"),rule_value(rule,all_rules)^)])
fn style_node(root, label) => c.first([for (node in children(root,"styleLbl") where attr(node,"name") == label) node])
fn color_list(node, key, colors) any^ {
    let palette = child(node,key)
    let entries = c.children(palette,null,c.A);
    if (len(entries) == 0) null
    else if (len(entries) != 1) unsupported("Unsupported multi-color interpolation")^
    else theme.drawing_color({children:entries},colors,null,true)
}
fn shape_style(node, context) map^ {
    let label = attr(node.source,"styleLbl","")
    let colors = style_node(context.colors,label)
    let style = child(style_node(context.style,label),"style")
    let line = c.child(style,"lnRef",c.A)
    let index = c.as_int(c.attr(line,"idx",""),0)
    let font = c.child(style,"fontRef",c.A)
    let effect = c.as_int(c.attr(c.child(style,"effectRef",c.A),"idx",""),0)
    let fill = color_list(colors,"fillClrLst",context.theme)^
    let stroke = color_list(colors,"linClrLst",context.theme)^
    let foreground = c.get({value:color_list(colors,"txFillClrLst",context.theme)^},"value",
        theme.drawing_color(font,context.theme,null,true));
    if (fill == null or effect != 0 or index > len(context.theme.lines) or index < 0)
        unsupported("Unsupported shape colors, effects or line style")^
    else {fill:fill,stroke:if (index == 0) "none" else stroke,
        line:if (index == 0) 0.0 else c.pixels(c.attr(context.theme.lines[index-1],"w","")),
        foreground:c.get({value:foreground},"value","#000000"),
        font:c.get(context.theme.fonts,c.attr(font,"idx",""),"Arial")}
}
fn text_insets(node, font_size) map^ => map([for (rule in children(child(node.source,"constrLst")),
    let kind = attr(rule,"type") where contains(["lMarg","rMarg","tMarg","bMarg"],kind))
    (kind,if (attr(rule,"val") != null) numeric(attr(rule,"val"),0)^*4.0/3.0
        else if (attr(rule,"refType") == "primFontSz") font_size*numeric(attr(rule,"fact"),1)^
        else unsupported("Unsupported text margin reference")^)])
fn draw_shape(node, box, context, all_rules) map^ {
    let presentation = child(node.source,"presOf")
    let ids = if (presentation == null or attr(presentation,"axis") == null) []
        else select(context.graph,node.ids,presentation)^
    let lines = [for (id in ids, line in context.graph.points[id].lines) line]
    let styling = shape_style(node,context)^
    let font_size = if (box.font > 0 and box.font <= 4096) box.font
        else unsupported("Invalid text font size")^
    let insets = text_insets(node,font_size)^
    let font = metrics.style({font_family:styling.font},"label",font_size,400)
    let measured = metrics.measure(lines,font) ^ {
        raise error({code:^.code,message:"SmartArt text metrics: " ++ ^.message})
    }
    let nominal_line = if (len(lines) == 0) font_size else max([for (m in measured) max(1.0,m.height)])
    let scale = if (len(lines) == 0) 1.0 else min([1.0,
        box.w/max(1.0,max([for (m in measured) m.width])+c.get(insets,"lMarg",0.0)+c.get(insets,"rMarg",0.0)),
        box.h/(nominal_line*len(lines)+c.get(insets,"tMarg",0.0)+c.get(insets,"bMarg",0.0))])
    let size = font_size*scale
    let line_height = nominal_line*scale
    let elements = [<rect x:string(box.x),y:string(box.y),width:string(box.w),height:string(box.h),
        fill:styling.fill,stroke:styling.stroke,["stroke-width"]:string(styling.line)>,
        *[for (i,line in lines) <text x:string(box.x+box.w/2+(c.get(insets,"lMarg",0.0)-c.get(insets,"rMarg",0.0))*scale/2),
            y:string(box.y+box.h/2+(c.get(insets,"tMarg",0.0)-c.get(insets,"bMarg",0.0))*scale/2+
                (i-(len(lines)-1)/2.0)*line_height-(measured[i].top+measured[i].bottom)*scale/2),
            ["text-anchor"]:"middle",["font-family"]:styling.font,["font-size"]:string(size),
            fill:styling.foreground,line>]];
    {elements:elements,ids:ids}
}
fn paint(node, box, context, inherited = []) map^ {
    let all_rules = inherited ++ rules(node,box)
    let own = if (node.shape == null or node.shape == "") {elements:[],ids:[]}
        else draw_shape(node,box,context,all_rules)^
    let direction = c.attr(c.first(c.children(node.algorithm,"param",c.DGM)),"val","")
    let horizontal = direction == null or contains(["fromL","fromR"],direction)
    let reverse = contains(["fromR","fromB"],direction)
    let sizes = [for (nested in node.children) values(nested,node,all_rules)^]
    let weights = [for (v in sizes) c.get(v,if (horizontal) "w" else "h",if (horizontal) box.w else box.h)]
    let total = sum(weights);
    if (node.kind == "lin" and direction != null and not contains(["fromL","fromR","fromT","fromB"],direction))
        unsupported("Unsupported linear direction")^
    else {
        let nested = [for (i,value in node.children, let props = sizes[i],
            let extent = if (node.kind != "lin" or total <= 0) null
                else (if (horizontal) box.w else box.h)*weights[i]/total,
            let offset = if (node.kind != "lin" or total <= 0) 0.0
                else (if (horizontal) box.w else box.h)*sum(slice(weights,0,i))/total,
            let linear_at = if (not reverse) offset else (if (horizontal) box.w else box.h)-offset-extent,
            let area = {x:box.x+(if (node.kind == "lin" and horizontal) linear_at else c.get(props,"l",0.0)),
                y:box.y+(if (node.kind == "lin" and not horizontal) linear_at else c.get(props,"t",0.0)),
                w:if (node.kind == "lin" and horizontal) extent else c.get(props,"w",box.w),
                h:if (node.kind == "lin" and not horizontal) extent else c.get(props,"h",box.h),
                font:if (props.primFontSz == null) c.get(box,"font",16.0) else props.primFontSz*4.0/3.0})
            if (area.w <= 0 or area.h <= 0 or area.w > 16384 or area.h > 16384)
                unsupported("Unsupported shape dimensions")^ else paint(value,area,context,all_rules)^];
        {elements:own.elements ++ c.collect(nested,"elements"),ids:own.ids ++ c.collect(nested,"ids")}
    }
}
fn render_layout(data, layout, colors, style, theme, width, height) map^ {
    let points = [for (node in children(child(data,"ptLst"),"pt")
        where attr(node,"type") == null or contains(["node","doc","asst"],attr(node,"type")))
        {id:attr(node,"modelId"),kind:attr(node,"type","node"),lines:[for (p in c.descendants(child(node,"t"),"p",c.A),
            let value = c.drawing_text(p) where value != "") value]}]
    let edges = sort([for (edge in children(child(data,"cxnLst"),"cxn")
        where attr(edge,"type") == null or attr(edge,"type") == "parOf")
        {from:attr(edge,"srcId"),to:attr(edge,"destId"),order:numeric(attr(edge,"srcOrd"),0)^}],{by:(edge)=>edge.order})
    let roots = [for (point in points where point.kind == "doc") point.id]
    let ids = [for (point in points) point.id]
    let root = child(layout,"layoutNode");
    if (layout.uri != c.DGM or layout.tag != "layoutDef" or colors.uri != c.DGM or colors.tag != "colorsDef" or
        style.uri != c.DGM or style.tag != "styleDef" or root == null or len(points) > 128 or len(edges) > 512 or
        width <= 0 or height <= 0 or width > 16384 or height > 16384 or
        len(roots) != 1 or len(unique(ids)) != len(ids) or any([for (point in points) point.id == null]) or
        any([for (i,edge in edges) not contains(ids,edge.from) or not contains(ids,edge.to) or
            any([for (j,prior in edges where j < i) prior.to == edge.to])]))
        unsupported("Unsupported or invalid layout/data parts")^
    else {
        let graph = {points:map([for (point in points) (point.id,point)]),edges:edges}
        let reachable = descendants(graph,roots[0])^
        let tree = build(root,roots,{dir:"norm"},graph,0)^
        let image = paint(tree,{x:0.0,y:0.0,w:width,h:height,font:16.0},
            {graph:graph,colors:colors,style:style,theme:theme})^
        let missing = [for (point in points where len(point.lines) > 0 and point.kind != "doc" and
            (not contains(image.ids,point.id) or not contains(reachable,point.id))) point.id];
        if (len(missing) > 0) unsupported("Layout does not display every saved text node")^
        else {svg:<svg xmlns:"http://www.w3.org/2000/svg",width:string(width),height:string(height),
            viewBox:("0 0 " ++ string(width) ++ " " ++ string(height)),*image.elements>}
    }
}
pub fn render(data, layout, colors, style, theme, width, height) any^ =>
    render_layout(data,layout,colors,style,theme,width,height) ^ {
        if (^.code == 7606) null else raise ^
    }
