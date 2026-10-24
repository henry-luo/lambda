import dom
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import lambda.editor.mod_editor
import image: lambda.editor.mod_image
import controller: lambda.edit.controller
import html: lambda.edit.html
import geom: lambda.editor.draw.geometry
import objects: .mod_objects
import layout: .mod_object_layout
import group: .mod_group_data
import transform: .mod_transform

pub pn measure(editor,node,paths,zoom) {
    let ordered=sort(paths,(path)=>path)
    let first = image.image_element(editor,controller.surface(node),ordered[0])
    let origin = dom.bounding_box(dom.closest(dom.parent_node(first),"p,li,td,th,h1,h2,h3,h4,h5,h6,div,blockquote"))
    return [for (path in ordered) {
        let target = image.image_element(editor,controller.surface(node),path)
        let box = dom.bounding_box(target)
        let metrics = dom.scroll_metrics(target);
        {path:path,x:(box.x-origin.x)/zoom,y:(box.y-origin.y)/zoom,width:box.width/zoom,height:box.height/zoom,
            object_width:metrics.client_width,object_height:metrics.client_height}}]
}
fn remove_members(tx,paths,i) => if (i<=0) tx else
    remove_members(tx_step(tx,step_replace(parent_path(paths[i]),last_index(paths[i]),last_index(paths[i])+1,[])),paths,i-1)
fn member(editor,b,left,top) {
    let n=node_at(editor.doc,b.path)
    let p=layout.settings(n);
    let bounds=transform.settings(n);
    {source:html.export_text(node('doc',[node('p',[n])]),{document:false,head:<head>}),src:attrs_get(n.attrs,'src'),
        x:b.x+b.width/2-left,y:b.y+b.height/2-top,width:b.object_width,height:b.object_height,
        rotation:transform.rotation(n),flip_x:p.flip_x,flip_y:p.flip_y,hidden:p.hidden,
        min_width:bounds.min_width or (if (bounds.kind=="wordart") 80 else 16),
        min_height:bounds.min_height or (if (bounds.kind=="wordart") 40 else 16),
        max_width:bounds.max_width or 650,max_height:bounds.max_height or (if (bounds.kind=="wordart") 400 else 2000)}
}
pub fn transaction(editor,p) map^ {
    let paths = sort(unique(p.paths or []),(path)=>path)
    let boxes = [for (path in paths) [for (b in p.boxes where b.path==path) b][0]]
    let left=min([for (b in boxes) b.x])
    let top=min([for (b in boxes) b.y])
    let width=max([for (b in boxes) b.x+b.width])-left
    let height=max([for (b in boxes) b.y+b.height])-top
    if (len(paths)<2 or len(paths)>20 or not all([for (path in paths) transform.settings(node_at(editor.doc,path))!=null]) or
        len(unique([for (path in paths) layout.sheet(editor.doc,path)]))!=1 or
        not all([for (b in boxes) b is map and b.object_width>0 and b.object_height>0]))
        raise error("Word: select 2–20 objects on the same page in the Selection Pane")
    else if (not objects.dimensions_valid({width:width,height:height,rotation:0}))
        raise error("Word: the group must fit within 650 × 2000 px; move the objects closer together")
    else {
        let data={width:width,height:height,items:[for (b in sort(boxes,(b)=>layout.layer(layout.settings(node_at(editor.doc,b.path))))) member(editor,b,left,top)]}
        let src=objects.source(group.graphic(data))^
        let n=node_attrs('img',[{name:'src',value:src},{name:'alt',value:"Group of " ++ string(len(paths)) ++ " objects"},
            {name:'width',value:width},{name:'height',value:height},{name:group.key,value:format(data,'json')^}],[])
        let tx=remove_members(tx_begin(editor.doc,editor.selection),paths,len(paths)-1)
        let replaced=tx_step(tx,step_replace(parent_path(paths[0]),last_index(paths[0]),last_index(paths[0])+1,[n]))
        tx_set_selection(layout.set_transaction(replaced,paths[0],{*:layout.defaults,wrap:"front",x:left,y:top})^,node_selection(paths[0]))
    }
}
fn restore_member(editor,n,p,v,index) map^ {
    let restored = html.import_text(v.source)^
    let source = image.images(restored.doc)[0].node
    if (source==null) raise error("Word: this group contains an invalid member") else {
        let width = float(attrs_get(n.attrs,'width'))
        let height = float(attrs_get(n.attrs,'height'))
        let sx = if (p.flip_x) -width/group.data(n).width else width/group.data(n).width
        let sy = if (p.flip_y) -height/group.data(n).height else height/group.data(n).height
        let center = geom.rotate_point({x:(v.x-group.data(n).width/2)*sx,y:(v.y-group.data(n).height/2)*sy},{x:0,y:0},transform.rotation(n))
        let angle = transform.normalized(transform.rotation(n)+v.rotation*(if (p.flip_x!=p.flip_y) -1 else 1))
        let temporary = {*:editor,doc:node('doc',[node('p',[source])]),selection:node_selection([0,0])}
        let resized = transform.transaction(temporary,{width:v.width*abs(sx),height:v.height*abs(sy),rotation:angle})^
        let prepared = edit_apply(temporary,resized)
        let laid = layout.set_transaction(tx_begin(prepared.doc,prepared.selection),[0,0],
            {*:layout.settings(source),wrap:if (p.wrap=="behind") "behind" else "front",x:p.x+width/2+center.x-v.width*abs(sx)/2,
                y:p.y+height/2+center.y-v.height*abs(sy)/2,z:p.z+index,
                hidden:p.hidden or v.hidden,flip_x:v.flip_x!=p.flip_x,flip_y:v.flip_y!=p.flip_y})^
        node_at(laid.doc_after,[0,0])
    }
}
pub fn ungroup(editor,params={}) map^ {
    let selected=image.selected(editor)
    let n=selected.node
    let old=layout.settings(n)
    let p=if (layout.floating(n)) old else {*:old,wrap:"front",x:params.frame.x,y:params.frame.y}
    let data=group.data(n)
    if (group.settings(n)==null) raise error("Word: select a group first")
    else if (not layout.floating(n) and params.frame==null) raise error("Word: measure the group position before ungrouping")
    else {
        let children=[for (i,v in data.items) restore_member(editor,n,p,v,i)^]
        let anchored=layout.set_transaction(tx_begin(editor.doc,editor.selection),selected.path,p)^
        let tx=tx_step(anchored,step_replace(parent_path(selected.path),last_index(selected.path),last_index(selected.path)+1,children))
        tx_set_selection(tx,node_selection(selected.path))
    }
}
