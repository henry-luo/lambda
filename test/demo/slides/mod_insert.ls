// S1.4 / D7.2.5: retained Insert settings lower to ordinary authored slide objects.
import m: .mod_model
import data: .mod_insert_data
import c: lambda.slide.common
import doc: lambda.editor.mod_doc
import commands: lambda.editor.mod_commands
import html: lambda.edit.html
import charts: ~~.word.mod_chart
import smartart: ~~.word.mod_smartart
import wordart: ~~.word.mod_wordart
import shapes: ~~.word.mod_shapes
import equations: ~~.word.mod_equation
import tables: ~~.word.mod_table_design

pub fn settings(node) {
    let saved = node.metadata.slide_insert;
    if (node is element and data.spec(saved.kind) != null and saved.params is map) saved else null
}
// inline SVG definitions need fresh IDs after edits, duplication and reopening.
fn graphic_id(st) => m.free_id([for (scene in st.plan.slides) for (obj in scene.targets,
    let id = settings(obj.source).params.graphic_id where id != null) id],"slides-svg-",st.serial)
fn parameters(st,kind,params) map^ {
    let spec = data.spec(kind);
    if (spec == null or not (params is map)) raise error("Slides: choose supported insert settings") else {*:spec.defaults,*:params,*:if (kind in ["wordart","shape"]) {graphic_id:graphic_id(st)} else {}}
}
fn stored(p) => map([for (key,value in p where string(key) != "preview") (string(key),value)])
fn authored(kind,p,existing=null) => {metadata:{*:c.value(existing,{}),slide_insert:{kind:kind,params:stored(p)}}}
// keep the table dialog's data synchronized with direct cell edits in the same undo transaction.
pub fn inline_source(node) {
    let saved = settings(node);
    if (saved.kind!="table") node else {
        let wrapper = [for (child in content(node) where child is element) child][0]
        let table = c.children(wrapper,'table')[0];
        if (table==null) m.attrs(node,{metadata:{*:node.metadata,slide_insert:null}}) else {
            let rows = [for (part in content(table) where part is element) *if (name(part)=='tr') [part] else c.children(part,'tr')]
            let cells = [for (row in rows) [for (cell in content(row) where cell is element and name(cell) in ['td','th']) cell]]
            let values = {*:saved.params,rows:len(rows),columns:len(cells[0]),
                data:join([for (row in cells) join([for (cell in row) trim(m.text(cell))],"\t")],"\n")};
            m.attrs(node,authored("table",values,node.metadata))
        }
    }
}
fn marker(kind,children,block=true) {
    if (block) <div ["data-slide-insert"]:kind,style:"width:100%;height:100%;position:relative;overflow:hidden;",*children>
    else <span ["data-slide-insert"]:kind,*children>
}
fn svg(graphic) => <svg *:map(graphic),width:"100%",height:"100%",*content(graphic)>
fn frame(st,kind,p,body,width,height,tag="content") {
    let w = min(width,st.plan.width*0.85)
    let h = min(height,st.plan.height*0.8)
    let attrs = {x:(st.plan.width-w)/2.0,y:(st.plan.height-h)/2.0,width:w,height:h,class:"slides-insert-" ++ kind,*:authored(kind,p)};
    if (tag == "text") <text *:attrs,font_size:28.0,marker(kind,body,false)>
    else <content *:attrs,marker(kind,body)>
}
pub fn fragment(source,format) array^ {
    if (format == "mark") content(parse("<fragment " ++ source ++ ">",'mark')^)
    else if (format == "svg") {
        let parsed = parse(source,'xml')^
        let graphic = if (name(parsed) == 'document') c.children(parsed,'svg')[0] else parsed;
        if (name(graphic) != 'svg') raise error("Slides: SVG content must have an svg root") else [graphic]
    }
    else {
        let root = parse(source,'html')^
        let document_html = if (name(root) == 'html') root else c.children(root,'html')[0]
        let body = c.children(document_html,'body')[0];
        if (body != null) content(body) else content(root)
    }
}
fn cell_text(p,r,col) {
    let rows = split(p.data,"\n")
    let row = if (rows[r] == null) [] else split(rows[r],"\t");
    c.value(row[col],"")
}
fn table(p) element^ {
    let count_rows = int(p.rows)^
    let count_cols = int(p.columns)^;
    if (p.rows != count_rows or p.columns != count_cols or count_rows<1 or count_rows>20 or count_cols<1 or count_cols>12)
        raise error("Slides: choose 1–20 rows and 1–12 columns")
    else if (not (p.data is string) or len(p.data)>12000 or tables.style(p.style) == null or not (p.header is bool))
        raise error("Slides: choose valid table data, header and style")
    else {
        let base = {doc:doc.node('doc',[]),selection:{kind:'all'},schema:html.schema}
        let tx = commands.cmd_insert_table(base,count_rows,count_cols,p.header)
        let table = tx.doc_after.content[0]
        let rows = [for (r,row in table.content) doc.with_content(row,[for (col,cell in row.content) {
            let paint = tables.cell_paint({style:p.style,header:p.header,bandedRows:true},r,col,count_rows,count_cols)
            let style = "border:1px solid " ++ paint.border ++ ";padding:12px;background:" ++ paint.fill ++ ";color:" ++ paint.color ++ ";font-weight:" ++ string(paint.weight) ++ ";";
            doc.node_attrs(cell.tag,[{name:'style',value:style}],[doc.text(cell_text(p,r,col))])
        }])]
        let styled = doc.node_attrs('table',[{name:'style',value:"border-collapse:collapse;width:100%;height:100%;font-family:Arial,sans-serif;font-size:22px;table-layout:fixed;"}],rows)
        let source = html.export_text(doc.node('doc',[styled]),{document:false,head:<head>});
        fragment(source,"html")^[0]
    }
}
fn destination(st,target) map^ {
    let matches = [for (i,scene in st.plan.slides where scene.id == target) {index:i,scene:scene}];
    if (len(matches) != 1) raise error("Slides: choose an existing destination slide") else matches[0]
}
// Persist generated target IDs before creating links, so reordering cannot retarget them.
fn pin_destination(st,target) any^ {
    let found = destination(st,target)^
    let raw = m.scenes(st.doc)[found.index];
    if (raw.id != null) st else m.commit_doc(st,m.replace_slide(st.doc,found.index,<slide *:map(raw),id:target,*content(raw)>))^
}
fn link_body(st,kind,p) array^ {
    let label = if (trim(p.label) == "") (if (kind == "action" and p.command != "go") "Next slide" else if (p.href != null and p.href != "") p.href else c.value(destination(st,p.target)^.scene.source.title,p.target)) else p.label
    let button_style = "width:100%;height:100%;font:inherit;white-space:pre-wrap;border:0;border-radius:8px;padding:12px;background:#c54c31;color:#ffffff;cursor:pointer;";
    if (kind == "action" and not (p.command in ["next","previous","home","end","go"])) raise error("Slides: choose a supported slide action")
    else if (kind == "action" and p.command != "go") [<button type:"button",["data-slide-command"]:p.command,style:button_style,label>]
    else if (kind == "link" and p.href != "") {
        let href = trim(p.href)
        let allowed = starts_with(href,"https://") or starts_with(href,"http://") or starts_with(href,"mailto:") or starts_with(href,"tel:") or starts_with(href,"file://");
        if (not allowed) raise error("Slides: use an http, https, mailto, tel or file URL")
        else [<a href:href,style:"color:inherit;text-decoration:underline;",label>]
    }
    else {
        let checked = destination(st,p.target)^;
        [<button type:"button",["data-slide-tool"]:"go",["data-slide-destination"]:p.target,style:button_style,label>]
    }
}
pub fn build_object(st,kind,params) element^ {
    let p = parameters(st,kind,params)^;
    if (kind == "table") frame(st,kind,p,[table(p)^],640.0,max(140.0,p.rows*56.0))
    else if (kind in ["picture","screenshot"]) {
        let src = c.value(p.preview,p.path);
        if (not (src is string) or trim(src) == "") raise error("Slides: choose an image file or URL")
        else <image x:st.plan.width*0.2,y:st.plan.height*0.18,width:st.plan.width*0.6,height:st.plan.height*0.6,
            src:src,fit:symbol(p.fit),*:authored(kind,p),class:"slides-insert-" ++ kind,marker(kind,[],false)>
    }
    else if (kind == "chart") frame(st,kind,p,[svg(charts.graphic(p)^)],p.width,p.height)
    else if (kind == "smartart") {
        let checked = smartart.parameters(p)^;
        frame(st,kind,p,[svg(smartart.graphic(checked))],p.width,p.height)
    }
    else if (kind == "wordart") {
        let checked = wordart.parameters(p)^;
        frame(st,kind,p,[svg(wordart.graphic(checked,p.graphic_id ++ "-"))],p.width,p.height)
    }
    else if (kind == "shape") {
        let checked = shapes.parameters(p)^;
        frame(st,kind,p,[svg(shapes.graphic(checked,p.graphic_id ++ "-"))],p.width,p.height)
    }
    else if (kind == "equation") {
        let measured = equations.render(p)^;
        frame(st,kind,p,[svg(measured.element)],measured.width,measured.height)
    }
    else if (kind == "zoom") {
        let found = destination(st,p.target)^
        let checked = if (not (p.preview is string) or not starts_with(p.preview,"data:image/")) raise error("Slides: slide preview was not rendered")
        let label = if (p.label == "") c.value(found.scene.source.title,found.scene.id) else p.label;
        frame(st,kind,p,[<button type:"button",["data-slide-tool"]:"go",["data-slide-destination"]:p.target,
            style:"width:100%;height:100%;padding:0;border:2px solid #d4dae3;background:#ffffff;border-radius:8px;overflow:hidden;cursor:pointer;",
            <img src:p.preview,alt:label,style:"display:block;width:100%;height:80%;object-fit:contain;">
            <span style:"display:block;padding:6px;color:#293445;font-family:Arial,sans-serif;font-size:18px;",label>>],320.0,210.0)
    }
    else if (kind in ["link","action"]) frame(st,kind,p,link_body(st,kind,p)^,380.0,100.0,"text")
    else if (kind in ["date","symbol"]) {
        if (not (p.text is string) or trim(p.text) == "") raise error("Slides: enter text to insert")
        else frame(st,kind,p,[p.text],400.0,80.0,"text")
    }
    else if (kind == "object") {
        if (not (p.source is string) or trim(p.source) == "") raise error("Slides: enter content or choose a file")
        else {
            let children = fragment(p.source,p.format)^;
            frame(st,kind,p,if (p.format == "svg") [svg(children[0])] else children,600.0,360.0)
        }
    }
    else if (kind == "video") {
        if (trim(p.path) == "") raise error("Slides: choose a video file or URL")
        else frame(st,kind,p,[<video src:p.path,poster:p.poster,controls:true,preload:"none",*:if (p.loop) {loop:true} else {},
            style:"display:block;width:100%;height:100%;background:#111827;object-fit:contain;">],600.0,338.0)
    }
    else if (kind == "audio") {
        if (trim(p.path) == "") raise error("Slides: choose an audio file or URL")
        else frame(st,kind,p,[<audio src:p.path,controls:true,preload:"none",*:if (p.loop) {loop:true} else {},style:"width:100%;height:100%;">],480.0,64.0)
    }
    else raise error("Slides: this insertion is not a canvas object")
}
fn note(st,p,id) {
    <notes id:id,["data-slide-comment"]:true,*:authored("comment",p),
        <aside ["data-slide-insert"]:"comment",
            <strong p.author>; <p p.text>>>
}
pub fn comments(st) => [for (note in m.scene(st).notes where m.is_comment(note)) {id:note.id,*:settings(note).params}]
pub fn remove_comment(st,id) any^ {
    let scene = m.scene(st);
    m.commit_doc(st,m.scene_doc(st,c.objects(scene.source),{},[for (child in content(scene.source)
        where not contains(c.objects(scene.source),child) and not (m.is_comment(child) and child.id == id)) child]))^
}
fn comment(st,p,target) any^ {
    if (trim(p.author) == "" or trim(p.text) == "") raise error("Slides: enter a comment and author")
    else {
        let ids = [for (entry in comments(st)) entry.id]
        let id = if (target != "") target else m.free_id(ids,"comment-")
        let existing = [for (child in content(m.scene(st).source) where not contains(c.objects(m.scene(st).source),child) and
            not (m.is_comment(child) and child.id == target)) child];
        {*:m.commit_doc(st,m.scene_doc(st,c.objects(m.scene(st).source),{},[*existing,note(st,p,id)]))^,serial:st.serial+1}
    }
}
fn field_object(st,kind,p,field,value,x,width) {
    let class = if (field == "number") "slides-page-number" else "slides-footer-" ++ field;
    let existing = [for (obj in m.scene(st).objects where obj.source.class == class) obj][0];
    <text id:c.value(existing.id,m.object_id(st,"field-" ++ field ++ "-")),class:class,*:authored(kind,p),x:x,y:if (field == "header") st.plan.height*0.035 else st.plan.height*0.93,
        width:width,height:st.plan.height*0.05,font_size:18.0,color:m.scene(st).palette.foreground,
        style:if (field in ["number","date"]) "text-align:right;" else "",
        marker(kind,[value],false)>
}
fn fields(st,kind,p) any^ {
    let classes = if (kind == "number") ["slides-page-number"] else ["slides-footer-header","slides-footer-footer","slides-footer-date","slides-page-number"]
    let changed = [for (i,scene in st.plan.slides) {
        let local = {*:st,index:i}
        let objects = [for (obj in scene.objects where not contains(classes,obj.source.class)) obj.source]
        let added = [
            *(if (kind == "footer" and p.header != "") [field_object(local,kind,p,"header",p.header,st.plan.width*0.0625,st.plan.width*0.875)] else []),
            *(if (kind == "footer" and p.footer != "") [field_object(local,kind,p,"footer",p.footer,st.plan.width*0.0625,st.plan.width*0.53)] else []),
            *(if (kind == "footer" and p.date_text != "") [field_object(local,kind,p,"date",p.date_text,st.plan.width*0.62,st.plan.width*0.23)] else []),
            *(if (kind == "number" or p.show_number) [field_object(local,kind,p,"number",string(i+1),st.plan.width*0.88,st.plan.width*0.06)] else [])
        ]
        let removed = [for (obj in scene.objects where contains(classes,obj.source.class) and not any([for (item in added) item.id == obj.id])) obj.id]
        let rest = [for (child in content(scene.source) where not contains(c.objects(scene.source),child))
            (let next = m.prune(child,removed), *if (next == null) [] else [next])];
        if (not p.all_slides and i != st.index) m.scenes(st.doc)[i]
        else <slide *:map(scene.source),master:false,*[*objects,*added,*rest]>
    }];
    m.commit_doc(st,m.with_slides(st.doc,changed),null,"")^
}
pub fn insert(st,kind,params,target="") any^ {
    let p = parameters(st,kind,params)^
    let prepared = if (kind == "zoom" or (kind == "link" and p.href == "") or (kind == "action" and p.command == "go")) pin_destination(st,p.target)^ else st;
    if (kind == "comment") comment(st,p,target)^
    else if (kind in ["footer","number"]) fields(st,kind,p)^
    else {
        let built = build_object(prepared,kind,p)^
        let obj = m.chosen(prepared)
        let same_tag = obj.tag == name(built)
        let changed = if (target != "" and obj != null and same_tag) m.edit_object(prepared,(node) =>
            m.rebuild(node,{*:map(node),*:authored(kind,p,node.metadata),*:if (obj.tag == 'image') {src:built.src,fit:built.fit} else if (kind == "equation") {width:built.width,height:built.height} else {}},content(built)))^
            else m.insert_object(prepared,built)^;
        // pinning a destination and inserting its link is one authored transaction.
        if (prepared.doc == st.doc) changed else m.commit_doc(st,changed.doc,changed.index,changed.selected)^
    }
}
