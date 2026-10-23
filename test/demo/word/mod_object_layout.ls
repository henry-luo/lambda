// Object layout is source-owned; native geometry only supplies the anchor frame (D7.2.5).
import dom
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import image: lambda.editor.mod_image
import controller: lambda.edit.controller
import inline_format: lambda.editor.mod_inline_format
import geom: lambda.editor.draw.geometry
import objects: .mod_objects
import page: .mod_page

pub let key = 'data-word-layout'
pub let defaults = {wrap:"inline",side:"left",x:0,y:0,distance:8,z:1,hidden:false,flip_x:false,flip_y:false}
pub let wraps = [{id:"inline",label:"In Line with Text",icon:"wrap_inline"},
    {id:"square",label:"Square",icon:"wrap_square"},{id:"topBottom",label:"Top and Bottom",icon:"wrap_top_bottom"},
    {id:"behind",label:"Behind Text",icon:"wrap_behind"},{id:"front",label:"In Front of Text",icon:"wrap_front"}]
pub let positions = [for (v in [{id:"top",label:"Top"},{id:"middle",label:"Middle"},{id:"bottom",label:"Bottom"}])
    for (h in [{id:"left",label:"Left"},{id:"center",label:"Center"},{id:"right",label:"Right"}])
        {id:v.id ++ "-" ++ h.id,label:v.label ++ " " ++ h.label,h:h.id,v:v.id}]
pub fn settings(n) { let p=objects.settings(n,key,defaults); if (p!=null and valid(p)) p else defaults }
pub fn floating(n) { let w=settings(n).wrap; w=="front" or w=="behind" }
pub fn layer(p) => if (p.wrap=="behind") -p.z else p.z
pub fn anchor(doc,path) => [for (i in 1 to len(path)-1,let p=slice(path,0,i),let n=node_at(doc,p)
    where contains(['p','li','td','th','h1','h2','h3','h4','h5','h6','div','blockquote'],n.tag)) p][last]
pub fn sheet(doc,path) => len([for (start in page.starts(doc) where start<=path[0]) start])
pub fn entries(doc) => [for (i,e in image.images(doc)) {*:e,index:i,layout:settings(e.node),page:sheet(doc,e.path),
    label:attrs_get(e.node.attrs,'alt') or "Picture " ++ string(i+1)}]
fn px(n) => string(n) ++ "px"
pub fn style(n,p) {
    let values = {position:if (contains(["front","behind"],p.wrap)) "absolute" else "relative",
        float:if (p.wrap=="square") p.side else "none",display:if (p.wrap=="topBottom") "block" else "inline-block",
        clear:if (p.wrap=="topBottom") "both" else "none",
        left:if (contains(["front","behind"],p.wrap)) px(p.x) else null,
        top:if (contains(["front","behind"],p.wrap)) px(p.y) else null,
        margin:if (p.wrap=="square") (if (p.side=="left") "0 " ++ px(p.distance) ++ " " ++ px(p.distance) ++ " 0"
            else "0 0 " ++ px(p.distance) ++ " " ++ px(p.distance)) else if (p.wrap=="topBottom") px(p.distance) ++ " auto" else "0",
        ["z-index"]:string(if (p.wrap=="behind") -abs(p.z) else p.z),
        visibility:if (p.hidden) "hidden" else null}
    let css = inline_format.css_set_many(attrs_get(n.attrs,'style'),values)
    inline_format.css_set(css,"transform",objects.transform_style(n,null,p))
}
fn valid(p) => contains([for (w in wraps) w.id],p.wrap) and contains(["left","right"],p.side) and
    all([for (v in [p.x,p.y,p.distance,p.z]) geom.finite_number(v)]) and abs(p.x)<=10000 and abs(p.y)<=10000 and
    p.distance>=0 and p.distance<=100 and p.z>=1 and p.z<=100000 and
    all([for (v in [p.hidden,p.flip_x,p.flip_y]) v is bool])
pub fn set_transaction(tx,path,p) map^ {
    if (not valid(p)) raise error("Word: use a valid wrapping mode, position, and text distance (0–100 px)") else {
        let n = node_at(tx.doc_after,path)
        let parent = anchor(tx.doc_after,path)
        let anchored = if (not contains(["front","behind"],p.wrap) or parent==null) tx else
            tx_step(tx,step_set_attr(parent,'style',inline_format.css_set_many(attrs_get(node_at(tx.doc_after,parent).attrs,'style'),
                {position:"relative",["min-height"]:"1em"})))
        tx_step(tx_step(anchored,step_set_attr(path,key,format(p,'json')^)),step_set_attr(path,'style',style(n,p)))
    }
}
fn align_value(frame,size,mode) => if (mode=="left" or mode=="top") 0
    else if (mode=="center" or mode=="middle") (frame-size)/2 else frame-size
pub fn transaction(editor,id,p) map^ {
    let selected = image.selected(editor)
    let n = selected.node
    let old = settings(n)
    let frame = p.frame
    let tx = tx_begin(editor.doc,editor.selection)
    if (selected==null) raise error("Word: select a picture or illustration first")
    else if (id=="object.layer") {
        // wrapping determines the text plane; ordering operates within that plane.
        let peers = [for (e in entries(editor.doc) where e.page==sheet(editor.doc,selected.path) and
            (e.layout.wrap=="behind")== (old.wrap=="behind")) e]
        let ordered = sort(peers,(e)=>layer(e.layout))
        let index = [for (i,e in ordered where e.path==selected.path) i][0]
        let target = if (p.action=="front") len(ordered)-1 else if (p.action=="back") 0
            else if (p.action=="forward") min(len(ordered)-1,index+1) else max(0,index-1)
        let rest = [for (e in ordered where e.path!=selected.path) e]
        let order = [*slice(rest,0,target),ordered[index],*slice(rest,target)]
        if (not contains(["front","back","forward","backward"],p.action)) raise error("Word: choose a layer order")
        else layer_steps(tx,order,0)^
    }
    else {
        let position = [for (v in positions where v.id==p.preset) v][0]
        let moving = id=="object.position" or id=="object.align"
        let value = if (id=="object.layout") {*:old,*:p.values}
            else if (id=="object.flip") {*:old,flip_x:if (p.axis=="x") not old.flip_x else old.flip_x,
                flip_y:if (p.axis=="y") not old.flip_y else old.flip_y}
            else if (moving and frame!=null) {*:old,wrap:if (old.wrap=="behind") "behind" else "front",
                x:if (id=="object.align" and contains(["top","middle","bottom"],p.mode)) frame.x else
                    frame.left+align_value(frame.width,frame.object_width,position.h or p.mode),
                y:if (id=="object.align" and contains(["left","center","right"],p.mode)) frame.y else
                    frame.top+align_value(frame.height,frame.object_height,position.v or p.mode)}
            else old;
        if (id=="object.flip" and not contains(["x","y"],p.axis)) raise error("Word: choose a flip direction")
        else if (moving and (frame==null or (id=="object.position" and position==null) or
            (id=="object.align" and not contains(["left","center","right","top","middle","bottom"],p.mode))))
            raise error("Word: choose a position or alignment in the document")
        else set_transaction(tx,selected.path,value)^
    }
}
fn layer_steps(tx,order,i) map^ => if (i>=len(order)) tx else
    layer_steps(set_transaction(tx,order[i].path,{*:order[i].layout,z:if (order[i].layout.wrap=="behind") len(order)-i else i+1})^,order,i+1)^

// Read the real paragraph origin; absolute objects stay attached to their source paragraph in exports.
pub pn measure(editor,node,zoom) {
    let selected = image.selected(editor)
    let target = image.image_element(editor,controller.surface(node))
    let block = dom.closest(dom.parent_node(target),"p,li,td,th,h1,h2,h3,h4,h5,h6,div,blockquote")
    let box = dom.bounding_box(target)
    let origin = dom.bounding_box(block)
    let page_box = dom.bounding_box(dom.closest(target,".word-page"))
    let metrics = dom.scroll_metrics(target)
    let g = page.geometry(editor.doc,sheet(editor.doc,selected.path))
    return {x:(box.x+box.width/2-origin.x)/zoom-metrics.client_width/2,
        y:(box.y+box.height/2-origin.y)/zoom-metrics.client_height/2,
        left:(page_box.x-origin.x)/zoom+g.left,top:(page_box.y-origin.y)/zoom+g.top,
        width:g.width-g.left-g.right,height:g.height-g.top-g.bottom,
        object_width:metrics.client_width,object_height:metrics.client_height}
}
