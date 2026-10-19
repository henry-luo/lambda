// S1.4 / S9.2.2: commands publish checked replacements and reuse shared snapshot history.
import img: lambda.image
import history: lambda.editor.mod_history
import sequence: lambda.editor.mod_doc
import model: .model
import registry: .commands

pub fn new_document(options={}) any^ {
    let width = model.value(options.width,1024)
    let height = model.value(options.height,768)
    if (not (width is int) or not (height is int) or width<1 or height<1 or width>16000000/height)
        raise error("Raster: invalid document dimensions")
    else img.document(img.stack([]),{width:width,height:height,working_space:'srgb'})
}
pub fn open_session(image) any^ {
    let ready = model.prepare(image)^
    let checked = model.validate(ready)^
    let rows = model.layers(ready)
    {doc:ready,saved:ready,hist:history.history_new(),selected:if (len(rows)>0) [rows[0].id] else [],
        target:"pixels",selection:null,tool:"move",zoom:0.0,pan:{x:0.0,y:0.0},gesture:null}
}
pub fn dirty(st) => st.doc != st.saved
pub fn record(before,after,label,group=null) {
    if (before.doc == after.doc) after
    else {
        let tx = {doc_before:before.doc,doc_after:after.doc,steps:[],
            sel_before:model.context(before),sel_after:model.context(after),
            meta:[{name:"label",value:label},*if (group == null) [] else [{name:"historyGroup",value:group}]]}
        {*:after,hist:history.history_push(before.hist,tx)}
    }
}
pub fn step_history(st,redo=false) {
    let result = if (redo) history.history_redo(st.hist,st.doc) else history.history_undo(st.hist,st.doc)
    model.restore({*:st,doc:result.doc,hist:result.hist,gesture:null},result.sel)
}
pub fn mark_saved(st,snapshot) => {*:st,saved:snapshot}
fn unlocked(st) any^ {
    let roots = model.selected_roots(st)^
    if (model.locked(st.doc,[for (row in roots) row.path],true))
        raise error("Raster: target layer is locked")
    else roots
}
fn insert(st,node,parent=null) any^ {
    let dest = model.destination(st,parent)^
    // inserting changes the owning group, including when the target itself is new.
    if (model.locked(st.doc,[dest.path])) raise error("Raster: destination group is locked")
    else {
        let container = img.node_at(st.doc,dest.path)^
        let changed = img.replace_at(st.doc,dest.path,img.insert(container,dest.index,node)^)^;
        {*:st,doc:changed,selected:[node.id],target:"pixels"}
    }
}
fn duplicated(node,prefix) any^ {
    let renamed = img.with_properties(node,{id:prefix,name:model.value(node.name,"Layer") ++ " copy"})^
    if (name(node) == 'stack') img.with_children(renamed,[for (i,child in content(node))
        if (model.composition(child)) duplicated(child,prefix ++ "-" ++ string(i))^ else child])^
    else renamed
}
fn same_parent(st) any^ {
    let rows = unlocked(st)^
    let parent = sequence.parent_path(rows[0].path)
    if (not all([for (row in rows) sequence.parent_path(row.path) == parent]))
        raise error("Raster: select sibling layers for this operation")
    else {rows:rows,path:parent,node:img.node_at(st.doc,parent)^}
}
fn replace_selected(st,siblings,replacement) any^ {
    let chosen = [for (row in siblings.rows) row.id]
    let first = siblings.rows[0].index
    let children = [for (i,child in content(siblings.node))
        *if (i == first) [replacement] else if (child.id in chosen) [] else [child]];
    {*:st,doc:img.replace_at(st.doc,siblings.path,img.with_children(siblings.node,children)^)^,
        selected:[replacement.id],target:"pixels"}
}
fn scaled(node,sx,sy) any^ {
    let changed = if (name(node) in ['layer','mask']) img.with_properties(node,
        {position:[int(round(model.position(node)[0]*sx)),int(round(model.position(node)[1]*sy))]})^ else node;
    img.with_children(changed,[for (child in content(node))
        if (name(child) == 'raster') img.resize(child,max(1,int(round(child.width*sx))),max(1,int(round(child.height*sy))))^
        else if (name(child) in ['stack','layer','mask']) scaled(child,sx,sy)^ else child])^
}
fn edit_node(st,command) any^ {
    let row = model.active(st)^
    let checked = if (command.id != "layer.lock") unlocked(st)^ else []
    let node = row.node
    let changed = if (command.id == "layer.rename") (
        if (not (command.name is string) or len(command.name)<1 or len(command.name)>256)
            raise error("Raster: enter a layer name of 1 to 256 characters")
        else img.with_properties(node,{name:command.name})^)
    else if (command.id in ["layer.visibility","layer.lock"]) (
        let property = if (command.id == "layer.lock") "locked" else "visible",
        let requested = model.value(command[property],if (property == "visible") node.visible == false else node.locked != true),
        if (not (requested is bool)) raise error("Raster: visibility and lock require a boolean")
        else img.with_properties(node,{[property]:requested})^)
    else if (command.id == "layer.opacity") (
        if (not model.finite(command.opacity) or command.opacity<0 or command.opacity>1)
            raise error("Raster: opacity must be between zero and one")
        else img.with_properties(node,{opacity:command.opacity})^)
    else if (command.id == "mask.add") (
        if (name(node) != 'layer' or model.mask(node) != null or img.raster_of(node) == null)
            raise error("Raster: add a mask to an unmasked raster layer")
        else img.with_children(node,[*content(node),img.mask(img.blank({width:img.raster_of(node).width,
            height:img.raster_of(node).height,channels:1,color:[model.value(command.coverage,1.0)]})^,
            {position:[0,0],default:1.0,enabled:true,invert:false})])^)
    else if (command.id == "mask.remove") img.with_children(node,[for (child in content(node) where name(child) != 'mask') child])^
    else if (command.id == "mask.toggle") (
        if (model.mask(node) == null) raise error("Raster: layer has no mask")
        else img.with_children(node,[for (child in content(node)) if (name(child) == 'mask')
            img.with_properties(child,{enabled:child.enabled == false})^ else child])^)
    else if (command.id == "pixels.fill" or command.id == "pixels.adjust" or command.id == "pixels.clear") (
        let old = if (st.target == "mask") model.mask(node)[0] else img.raster_of(node),
        if (old == null) raise error("Raster: target has no editable samples")
        else {
            let raster = if (command.id == "pixels.fill") img.fill(old,command.color)^
                else if (command.id == "pixels.clear") img.clear(old)^
                else if (st.target == "mask") raise error("Raster: color adjustments require a pixel target")
                else img.adjust(old,command.options)^
            img.with_children(node,[for (child in content(node))
                if (st.target == "pixels" and name(child) == 'raster') raster
                else if (st.target == "mask" and name(child) == 'mask') img.with_children(child,[raster])^
                else child])^
        })
    else raise error("Raster: unknown layer edit")
    {*:st,doc:model.replace(st.doc,row.id,changed)^,target:if (command.id == "mask.remove") "pixels" else st.target}
}
pub fn apply_command(st,command) any^ {
    let id = command.id;
    if (id == "layer.select") model.select(st,command.ids,model.value(command.target,"pixels"))^
    else if (id == "layer.insert" or id == "layer.new") {
        let raster = if (id == "layer.insert") command.raster else img.blank({width:st.doc.width,height:st.doc.height})^
        if (name(raster) != 'raster') raise error("Raster: insert requires Raster content")
        else insert(st,img.layer(raster,{id:model.fresh_id(st.doc),name:model.value(command.name,"New layer"),
            position:model.value(command.position,[0,0]),visible:true,opacity:1.0}),command.parent)^
    }
    else if (id == "layer.duplicate") {
        let row = model.active(st)^
        insert(st,duplicated(row.node,model.fresh_id(st.doc))^ )^
    }
    else if (id == "layer.delete") {
        let selected = unlocked(st)^
        // remove deepest/rightmost siblings first so each remaining content path stays valid.
        let ordered = reverse(selected)
        let changed = reduce([st.doc,*ordered],(image,row) => (
            let container_path = sequence.parent_path(row.path),
            img.replace_at(image,container_path,img.remove(img.node_at(image,container_path)^,row.index)^)^));
        {*:st,doc:changed,selected:[],target:"pixels"}
    }
    else if (id == "layer.move") {
        let rows = unlocked(st)^
        if (not (command.dx is int) or not (command.dy is int)) raise error("Raster: movement requires integral pixel offsets")
        else if (command.dx == 0 and command.dy == 0) st
        else {*:st,doc:reduce([st.doc,*rows],(image,row) => model.replace(image,row.id,model.translated(row.node,command.dx,command.dy)^)^)}
    }
    else if (id == "layer.reorder") {
        let row = model.active(st)^
        let checked = unlocked(st)^
        let path = sequence.parent_path(row.path)
        let container = img.node_at(st.doc,path)^
        let entries = [for (i,child in content(container) where model.composition(child)) i]
        let current = [for (i,index in entries where index == row.index) i][0]
        let requested = command.index
        if (not (requested is int) or requested<0 or requested>=len(entries)) raise error("Raster: invalid layer-list index")
        else if (requested == current) st
        else {*:st,doc:img.replace_at(st.doc,path,img.reorder(container,row.index,entries[requested])^)^}
    }
    else if (id == "layer.group") {
        let siblings = same_parent(st)^
        let chosen = [for (row in siblings.rows) row.id]
        let group = img.stack([for (child in content(siblings.node) where child.id in chosen) child],
            {id:model.fresh_id(st.doc),name:model.value(command.name,"Group"),isolation:'isolated',visible:true,opacity:1.0});
        replace_selected(st,siblings,group)^
    }
    else if (id == "layer.merge") {
        let siblings = same_parent(st)^
        let chosen = [for (row in siblings.rows) row.id]
        let first = siblings.rows[0].index
        let last_index = siblings.rows[last].index
        let adjacent = all([for (i,child in content(siblings.node) where model.composition(child) and i>=first and i<=last_index) child.id in chosen])
        if (not adjacent) raise error("Raster: select adjacent sibling layers to merge")
        else {
            let raster = img.render_frame(img.document(img.stack([for (row in siblings.rows) row.node]),
                {width:st.doc.width,height:st.doc.height}))^;
            replace_selected(st,siblings,img.layer(raster,{id:model.fresh_id(st.doc),name:model.value(command.name,"Merged"),position:[0,0]}))^
        }
    }
    else if (id == "layer.ungroup") {
        let row = model.active(st)^
        let checked = unlocked(st)^
        if (name(row.node) != 'stack' or model.value(row.node.opacity,1)!=1 or row.node.visible == false or
            model.mask(row.node) != null or any([for (c in content(row.node)) name(c) == 'effect']))
            raise error("Raster: ungroup requires a visible, unmasked, full-opacity group")
        else {
            let path = sequence.parent_path(row.path)
            let parent = img.node_at(st.doc,path)^
            let children = [for (child in content(row.node) where model.composition(child)) child]
            {*:st,doc:img.replace_at(st.doc,path,img.splice_children(parent,row.index,1,children)^)^,
                selected:[for (child in children) child.id]}
        }
    }
    else if (id == "document.flatten") {
        let raster = img.render_frame(st.doc)^;
        {*:st,doc:img.with_children(st.doc,[*if (img.assets(st.doc) == null) [] else [img.assets(st.doc)],
            img.stack([img.layer(raster,{id:model.fresh_id(st.doc),name:"Flattened",position:[0,0]})])])^,
            selected:[model.fresh_id(st.doc)],target:"pixels"}
    }
    else if (id == "document.resize_canvas" or id == "document.resize_image") {
        let dimensions = new_document({width:command.width,height:command.height})^;
        let changed = if (id == "document.resize_image") img.replace_at(st.doc,model.root_path(st.doc),
            scaled(img.composition(st.doc),dimensions.width/st.doc.width,dimensions.height/st.doc.height)^)^ else st.doc;
        {*:st,doc:img.with_properties(changed,{width:dimensions.width,height:dimensions.height})^}
    }
    else edit_node(st,command)^
}
pub fn exec(st,command) any^ {
    let checked_command = registry.checked(command)^;
    if (st.gesture != null) raise error("Raster: finish or cancel the active gesture")
    else if (command.id == "history.undo") step_history(st)
    else if (command.id == "history.redo") step_history(st,true)
    else {
        let result = apply_command(st,command)^
        let checked = if (result.doc != st.doc) model.validate(result.doc)^ else true;
        record(st,result,registry.descriptor(command.id).label,command.group)
    }
}
