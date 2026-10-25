// S1.4 / D7.2.5: Home adapts shared formatting rules to authored slide values.
import m: .mod_model
import c: lambda.slide.common
import css: lambda.editor.mod_inline_format
import text_format: ~~.mod_text_format
import shape_presets: ~~.mod_shape_presets
import alignment: lambda.editor.mod_alignment
import ins: .mod_insert

let character_keys = ["font-family","font-weight","font-style","text-decoration","letter-spacing","background-color","line-height"]
pub fn value(st,key,fallback="") => c.value(css.css_get(m.chosen(st).source.style,key),fallback)
pub fn style(st,values) any^ => m.properties(st,{style:css.css_set_many(m.chosen(st).source.style,values)})^
pub fn family(st) => value(st,"font-family",c.value(m.chosen(st).font_family,st.plan.palette.font_family))
// editor-owned alignment wrappers keep paragraph operations inside the text body.
fn layout_wrapper(node) => node.class in ["slide-text-layout","slide-text-content"]
fn body_wrapper(node) => layout_wrapper(node) or name(node) in ['sup','sub']
fn body(children,change,scripts=false) {
    let child = children[0];
    if (len(children) == 1 and (layout_wrapper(child) or (scripts and body_wrapper(child))))
        [m.rebuild(child,map(child),body(content(child),change,scripts))]
    else change(children)
}
fn body_children(children) {
    let child = children[0];
    if (len(children) == 1 and body_wrapper(child)) body_children(content(child)) else children
}
fn script_kind(children) {
    let child = children[0];
    if (layout_wrapper(child)) script_kind(content(child)) else name(child)
}
pub fn pressed(st,action) {
    if (action == "bold") value(st,"font-weight") in ["700","bold"]
    else if (action == "italic") value(st,"font-style") == "italic"
    else if (action in ["underline","strike"]) contains(split(value(st,"text-decoration")," "),if (action == "underline") "underline" else "line-through")
    else if (starts_with(action,"align-")) value(st,"text-align","left") == slice(action,6)
    else if (action in ["superscript","subscript"]) script_kind(if (m.chosen(st) == null) [] else content(m.chosen(st).source)) == (if (action == "superscript") 'sup' else 'sub')
    else false
}
fn decoration(st,kind) {
    let values = [for (part in split(value(st,"text-decoration")," ") where part != "" and part != "none") part]
    let next = if (contains(values,kind)) [for (part in values where part != kind) part] else [*values,kind];
    if (len(next) == 0) "none" else join(next," ")
}
pub fn emphasis(st,action) any^ {
    if (action == "bold") style(st,{["font-weight"]:if (pressed(st,action)) "400" else "700"})^
    else if (action == "italic") style(st,{["font-style"]:if (pressed(st,action)) "normal" else "italic"})^
    else if (action in ["underline","strike"]) style(st,{["text-decoration"]:decoration(st,if (action == "underline") "underline" else "line-through")})^
    else if (starts_with(action,"align-")) style(st,{["text-align"]:slice(action,6)})^
    else {
        let obj = m.chosen(st)
        let script = if (action == "superscript") 'sup' else 'sub'
        let transform = (items) => {
            let child = items[0]
            let children = if (len(items) == 1 and name(child) in ['sup','sub']) content(child) else items;
            if (name(child) == script) children else if (script == 'sup') [<sup *children>] else [<sub *children>]
        };
        m.edit_object(st,(node) => m.rebuild(node,map(node),body(content(node),transform)))^
    }
}
fn clear_runs(children) => [for (child in children) {
    let nested = if (child is element and m.rebuildable(child)) clear_runs(content(child)) else null;
    *if (not (child is element) or nested == null) [child]
    else if (name(child) in ['b','strong','i','em','u','s','del','sup','sub','span']) nested
    else [m.rebuild(child,map(child),nested)]
}]
pub fn clear(st) any^ => m.edit_object(st,(node) => m.rebuild(node,
    {*:map(node),font_size:null,color:null,style:css.css_set_many(node.style,
        map([for (key in character_keys) (key,null)]))},clear_runs(content(node))))^

fn highlight_runs(children,color) => [for (child in children) {
    *if (child is string) [if (color == null) child else <span class:"slide-text-highlight",style:"background-color:" ++ color ++ ";",child>]
    else if (child.class == "slide-text-highlight") highlight_runs(content(child),color)
    else if (child is element and m.rebuildable(child)) [m.rebuild(child,map(child),highlight_runs(content(child),color))]
    else [child]
}]
pub fn highlight(st,color) any^ => m.edit_object(st,(node) => m.rebuild(node,map(node),highlight_runs(content(node),color)))^
pub fn highlight_color(node) => if (not (node is element)) null else [for (child in content(node),
    let color = if (child.class == "slide-text-highlight") css.css_get(child.style,"background-color")
        else if (child is element) highlight_color(child) else null where color != null) color][0]

fn case_children(children,mode,initial) => reduce([{children:[],state:initial},*children],(acc,child) => {
    let out = if (child is string) text_format.case_text(child,mode,acc.state) else null
    let nested = if (child is element and m.rebuildable(child)) case_children(content(child),mode,
        if (name(child) in ['li','p']) {word:true,sentence:true} else acc.state) else null;
    {children:[*acc.children,if (out != null) out.text else if (nested != null) m.rebuild(child,map(child),nested.children) else child],
        state:if (out != null) {word:out.word,sentence:out.sentence} else if (nested != null) nested.state else acc.state}
})
pub fn change_case(st,mode) any^ => m.edit_object(st,(node) => m.rebuild(node,map(node),
    case_children(content(node),mode,{word:true,sentence:true}).children))^

pub fn list_node(node) => if (not (node is element)) null else [for (child in content(node))
    *if (name(child) in ['ul','ol']) [child] else if (body_wrapper(child)) [list_node(child)] else []][0]
pub fn list_kind(st) => name(list_node(m.chosen(st).source))
// split inside rich inline runs, preserving the marks on each resulting paragraph.
fn line_tokens(children) => reduce([{lines:[[]],block:false},*children],(acc,child) => {
    let block = name(child) in ['p','div']
    let parts = if (child is string) [for (part in split(child,"\n")) [part]]
        else if (name(child) == 'br') [[],[]]
        else if (child is element and m.rebuildable(child)) [for (part in line_tokens(content(child))) [m.rebuild(child,map(child),part)]]
        else [[child]]
    let separate = (block or acc.block) and len(acc.lines[last])>0;
    {lines:if (separate) [*acc.lines,*parts] else [*slice(acc.lines,0,len(acc.lines)-1),[*acc.lines[last],*parts[0]],*slice(parts,1)],block:block}
}).lines
pub fn set_list(st,kind,preset=null) any^ {
    let obj = m.chosen(st)
    let existing = list_node(obj.source)
    let tag = if (kind == "numbering") 'ol' else 'ul'
    let off = name(existing) == tag and preset == null
    let children = if (existing != null) [for (item in content(existing)) content(item)] else line_tokens(body_children(content(obj.source)))
    let list_style = c.value(preset,if (tag == 'ol') "decimal" else "disc")
    let attrs = {style:"margin:0;padding-left:1.3em;white-space:normal;list-style-type:" ++ list_style ++ ";"}
    let items = [for (line in children) <li style:"white-space:pre-wrap;",*line>]
    let next = if (off) [for (i,line in children) *[*(if (i>0) ["\n"] else []),*line]]
        else if (tag == 'ol') [<ol *:attrs,*items>] else [<ul *:attrs,*items>];
    m.edit_object(st,(node) => m.rebuild(node,map(node),body(content(node),(items) => next,true)))^
}
pub fn indent(st,increase=true) any^ {
    let current = float(replace(value(st,"padding-left","0"),"px","")) ^ { 0.0 };
    style(st,{["padding-left"]:c.px(max(0.0,current+(if (increase) 24.0 else -24.0)))})^
}
pub fn vertical(st,position) any^ {
    let obj = m.chosen(st)
    let current = content(obj.source)[0]
    let children = if (current.class == "slide-text-layout") content(content(current)[0]) else content(obj.source)
    let justify = if (position == "middle") "center" else if (position == "bottom") "flex-end" else "flex-start";
    m.edit_object(st,(node) => m.rebuild(node,map(node),[<div class:"slide-text-layout",
        style:"width:100%;height:100%;display:flex;flex-direction:column;justify-content:" ++ justify ++ ";",
        <div class:"slide-text-content",style:"width:100%;",*children>>]))^
}
pub fn align(st,mode) any^ {
    let obj = m.chosen(st)
    let parent = m.parent(st)
    let bounds = {x:0.0,y:0.0,width:c.value(parent.width,st.plan.width),height:c.value(parent.height,st.plan.height)}
    let box = {id:obj.id,x:obj.x,y:obj.y,width:obj.width,height:obj.height}
    let offsets = alignment.offsets([box],bounds,mode);
    if (alignment.horizontal(mode)) m.properties(st,{x:obj.x+offsets[obj.id]})^ else m.properties(st,{y:obj.y+offsets[obj.id]})^
}
pub fn reset_layout(st) any^ {
    let objects = [for (obj in m.scene(st).objects) if (obj.source.role == null) obj.source
        else m.attrs(obj.source,{x:null,y:null,width:null,height:null,font_size:null,color:null,style:""})];
    if (not has_placeholders(st)) st else m.commit_doc(st,m.scene_doc(st,objects))^
}
pub fn has_placeholders(st) => any([for (obj in m.scene(st).objects) obj.source.role != null])
fn format_values(st) {
    let obj = m.chosen(st);
    if (obj.tag == 'text') {font_size:obj.font_size,color:obj.paint,style:css.css_set_many("",
        map([for (key in [*character_keys,"text-align","padding-left"]) (key,css.css_get(obj.source.style,key))]))}
    else {fill:obj.source.fill,stroke:obj.source.stroke,stroke_width:obj.source.stroke_width,radius:obj.source.radius,opacity:obj.opacity}
}
pub fn painter(st) => if (m.chosen(st) == null) st else {*:st,painter:st.painter != true,
    format_clipboard:{tag:m.chosen(st).tag,values:format_values(st),highlight:highlight_color(m.chosen(st).source)},status:"Choose an object to apply this formatting. Escape cancels."}
pub fn paint(st) any^ {
    let obj = m.chosen(st)
    if (obj == null) st
    else if (obj.tag != st.format_clipboard.tag) raise error("Slides: choose the same kind of object as the format source")
    else {*:m.edit_object(st,(node) => m.rebuild(node,{*:map(node),*:st.format_clipboard.values},
        if (obj.tag == 'text') highlight_runs(content(node),st.format_clipboard.highlight) else content(node)))^,
        painter:false,status:"Formatting applied."}
}
// SVG shapes retain their Word parameters; native shapes keep slide paint attributes.
pub fn drawing(st,values) any^ {
    let existing = ins.settings(m.chosen(st).source);
    if (existing.kind == "shape") ins.insert(st,"shape",{*:existing.params,*:values,
        *:if (values.stroke_width != null) {weight:values.stroke_width} else {}},st.selected)^
    else m.properties(st,values)^
}
pub fn shape_style(st,id) any^ {
    let preset = [for (p in shape_presets.styles where p.id == id) p][0];
    if (preset == null) raise error("Slides: choose a shape style")
    else drawing(st,{fill:preset.values.fill,stroke:preset.values.stroke,stroke_width:preset.values.weight,opacity:1.0})^
}
pub fn property(st,key,input) any^ {
    if (key == "font_size") {
        let size = float(input)^;
        if (size <= 0 or not c.finite(size)) raise error("Slides: choose a positive font size") else m.properties(st,{font_size:size})^
    }
    else if (key == "font-family") {
        if (trim(input) == "" or any([for (part in [";","{","}"]) contains(input,part)])) raise error("Slides: choose a font family")
        else style(st,{["font-family"]:input})^
    }
    else if (key == "line-height" or key == "letter-spacing") {
        let amount = float(input)^;
        if (not c.finite(amount) or (key == "line-height" and amount <= 0)) raise error("Slides: choose valid text spacing")
        else style(st,{[key]:c.fmt(amount) ++ (if (key == "letter-spacing") "px" else "")})^
    } else m.properties(st,{[key]:input})^
}
pub fn handles(action:string) => action in ["cut","paste-text","format-painter","paint-format","reset-layout","grow-font","shrink-font",
    "bold","italic","underline","strike","superscript","subscript","clear-format","bullets","numbering","indent","outdent",
    "align-left","align-center","align-right","align-justify"] or contains(action,":")
pub fn command(st,action:string) any^ {
    let obj = m.chosen(st)
    let parts = split(action,":")
    let kind = parts[0]
    let arg = parts[1]
    if (action == "reset-layout") { reset_layout(st)^ }
    else if (kind == "layout") { m.slide_properties(st,{layout:symbol(arg)})^ }
    else if (action == "paste-text") {
        let source = st.clipboard;
        if (name(source) != 'text') st else m.paste(st,m.rebuild(source,
            {*:map(source),font_size:null,color:null,style:""},[m.text(source)]))^
    }
    else if (obj == null) { st }
    else if (action == "cut") { m.remove_object({*:st,clipboard:m.copy_object(st)})^ }
    else if (action == "format-painter") { painter(st) }
    else if (action == "paint-format") { paint(st)^ }
    else if (kind == "object-align") { align(st,arg)^ }
    else if (kind == "rotate") { m.properties(st,{rotation:obj.rotation+float(arg)})^ }
    else if (kind == "shape-style") { shape_style(st,arg)^ }
    else if (kind in ["fill","outline","weight"]) {
        drawing(st,if (kind == "weight") {stroke_width:float(arg)^}
            else if (kind == "outline") {stroke:arg} else {fill:arg})^
    }
    else if (obj.tag != 'text') { st }
    else if (action in ["bold","italic","underline","strike","superscript","subscript"] or starts_with(action,"align-")) { emphasis(st,action)^ }
    else if (action == "clear-format") { clear(st)^ }
    else if (action == "grow-font" or action == "shrink-font") { m.properties(st,{font_size:text_format.font_step(obj.font_size,action == "grow-font")})^ }
    else if (action in ["bullets","numbering"] or kind in ["bullets","numbering"]) { set_list(st,kind,arg)^ }
    else if (action == "indent" or action == "outdent") { indent(st,action == "indent")^ }
    else if (kind == "case") { change_case(st,arg)^ }
    else if (kind == "text-color") { m.properties(st,{color:if (arg == "none") m.scene(st).palette.foreground else arg})^ }
    else if (kind == "highlight") { highlight(st,if (arg == "none") null else arg)^ }
    else if (kind == "spacing" or kind == "tracking") { property(st,if (kind == "spacing") "line-height" else "letter-spacing",arg)^ }
    else if (kind == "vertical") { vertical(st,arg)^ }
    else st
}
