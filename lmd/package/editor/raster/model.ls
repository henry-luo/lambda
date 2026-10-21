// S9.1.2 / D7.2.4v3: the shared image tree is authoritative; rows only resolve identities.
import img: lambda.image
import sequence: lambda.editor.mod_doc
import paths: lambda.editor.mod_source_pos
import geometry: lambda.editor.draw.geometry
import common: lambda.slide.common
import affine: lambda.chart.svg_transform
import guides: .guides

pub fn value(v,fallback) => common.value(v,fallback)
pub fn finite(v) => geometry.finite_number(v)
pub fn composition(node) => name(node) in ['layer','stack']
pub fn position(node) => value(node.position,[0,0])
// an empty or multi-layer inspector has no single content owner.
pub fn primary(node) => if (node == null) null else [for (child in content(node) where name(child) in ['raster','text','vector']) child][0]
pub fn render_layers(image,nodes,width=null,height=null) any^ {
    let doc = img.document(img.stack(nodes),{width:value(width,image.width),height:value(height,image.height)})
    let scoped = img.with_children(doc,[*if (img.assets(image) == null) [] else [img.assets(image)],img.stack(nodes)])^;
    img.render_frame(scoped)^
}
pub fn mask(node) => if (node == null) null else [for (child in content(node) where name(child) == 'mask') child][0]
pub fn root_path(image) => [[for (i,child in content(image) where name(child) == 'stack') i][0]]

pub fn rows(node,path=[],depth=0,parent="") =>
    [for (i,child in content(node) where composition(child))
        *[{id:child.id,node:child,path:[*path,i],parent:parent,depth:depth,index:i},
          *if (name(child) == 'stack') rows(child,[*path,i],depth+1,child.id) else []]]
pub fn layers(image) => rows(img.composition(image),root_path(image))
pub fn lookup(image,id) => [for (row in layers(image) where row.id == id) row][0]
pub fn saved_channels(image) => [for (child in content(image) where name(child) == 'metadata' and child.role == 'photos_channels')
    for (channel in content(child)) channel]
pub fn identifiers(image) => [*[for (row in layers(image)) row.id],*[for (channel in saved_channels(image)) channel.id],
    *if (img.assets(image) == null) [] else [for (asset in content(img.assets(image))) asset.id]]
pub fn fresh_id(image,index=1) =>
    if (any([for (id in identifiers(image)) id == "raster-" ++ string(index) or
        starts_with(id,"raster-" ++ string(index) ++ "-")])) fresh_id(image,index+1)
    else "raster-" ++ string(index)

fn identify(node,path) any^ {
    let children = [for (i,child in content(node))
        if (composition(child)) identify(child,[*path,i])^ else child]
    let changed = img.with_children(node,children)^
    if (composition(node)) img.with_properties(changed,
        {id:if (node.id == null) "import-" ++ join([for (i in path) string(i)],"-") else string(node.id),
         name:if (node.name is string) node.name else if (name(node) == 'stack') "Group" else "Layer"})^
    else changed
}
pub fn prepare(image) any^ {
    if (not (image is element) or name(image) != 'image' or len(img.frames(image)) != 0 or
        len([for (child in content(image) where name(child) == 'stack') child]) != 1)
        raise error("Raster: expected one still-image root stack; choose an animation frame explicitly")
    else identify(image,[] )^
}
pub fn validate(image) any^ {
    let checked = img.validate(image)^
    let checked_guides = guides.validate(image)^
    let ids = identifiers(image)
    if (len(unique(ids)) != len(ids) or not all([for (id in ids) id is string and id != ""]))
        raise error("Raster: duplicate or invalid document identities")
    else if (len(saved_channels(image))>64 or not all([for (channel in saved_channels(image)) name(channel) == 'channel' and
        channel.name is string and len(channel.name)>0 and len(channel.name)<=256 and len(content(channel)) == 1 and
        name(channel[0]) == 'raster' and channel[0].channels == ["gray"] and channel[0].width == image.width and channel[0].height == image.height]))
        raise error("Raster: invalid saved coverage channels")
    else true
}
pub fn replace(image,id,node) any^ {
    let found = lookup(image,id)
    if (found == null) raise error("Raster: missing layer target")
    else img.replace_at(image,found.path,node)^
}
pub fn context(st) => {selected:st.selected,target:st.target,selection:st.selection,retouch:st.retouch}
pub fn restore(st,context) => if (context == null) st else
    {*:st,selected:[for (id in context.selected where lookup(st.doc,id) != null) id],
        channel_view:if (st.channel_view in ["composite","r","g","b","a"] or st.channel_view in [for (c in saved_channels(st.doc)) c.id]) st.channel_view else "composite",
        target:context.target,selection:context.selection,retouch:value(context.retouch,st.retouch)}
pub fn select(st,ids,target="pixels") any^ {
    if (not (ids is array) or not all([for (id in ids) lookup(st.doc,id) != null]) or
        not (target in ["pixels","mask"]) or
        (target == "mask" and (len(ids) != 1 or mask(lookup(st.doc,ids[0]).node) == null)))
        raise error("Raster: invalid layer or mask selection")
    else {*:st,selected:unique(ids),target:target}
}
pub fn active(st) any^ {
    if (len(st.selected) != 1) raise error("Raster: select one layer")
    else lookup(st.doc,st.selected[0])
}
pub fn source_target(st,writing=true) any^ {
    let row = active(st)^
    let mask_node = if (st.target == "mask") mask(row.node) else null
    let raster = if (st.target == "mask") mask_node[0] else primary(row.node)
    if (name(row.node) != 'layer' or raster == null) raise error("Raster: select a layer or mask")
    else if (row.node.transform != null and row.node.transform != affine.identity)
        raise error("Raster: rasterize the transformed layer before editing source pixels in canvas coordinates")
    else if (writing and locked(st.doc,[row.path])) raise error("Raster: target layer is locked")
    else {row:row,raster:raster,position:[position(row.node)[0]+value(mask_node.position,[0,0])[0],
        position(row.node)[1]+value(mask_node.position,[0,0])[1]]}
}
pub fn pixel_target(st,writing=true) any^ {
    let target = source_target(st,writing)^;
    if (name(target.raster) != 'raster') raise error("Raster: rasterize artwork before editing its source pixels") else target
}
pub fn replace_pixels(st,target,raster) any^ {
    let changed = img.with_children(target.row.node,[for (child in content(target.row.node))
        if (st.target == "pixels" and name(child) == 'raster') raster
        else if (st.target == "mask" and name(child) == 'mask') img.with_children(child,[raster])^ else child])^;
    {*:st,doc:replace(st.doc,target.row.id,changed)^}
}
pub fn selected_rows(st) any^ {
    let chosen = [for (row in layers(st.doc) where row.id in st.selected) row]
    if (len(chosen) == 0) raise error("Raster: select a layer") else chosen
}
pub fn locked(image,targets,descendants=false) =>
    any([for (row in layers(image) where row.node.locked == true)
        any([for (path in targets) paths.path_is_prefix(row.path,path) or
            (descendants and paths.path_is_prefix(path,row.path))])])
pub fn destination(st,parent=null) any^ {
    if (parent != null) {
        let row = lookup(st.doc,parent)
        if (row == null or name(row.node) != 'stack') raise error("Raster: destination is not a group")
        else ({path:row.path,index:0})
    } else if (len(st.selected) == 1) {
        let row = active(st)^;
        {path:sequence.parent_path(row.path),index:row.index}
    } else ({path:root_path(st.doc),index:0})
}
pub fn translated(node,dx,dy) any^ {
    if (name(node) == 'stack') img.with_children(node,[for (child in content(node))
        if (composition(child)) translated(child,dx,dy)^ else child])^
    else img.with_properties(node,{position:[position(node)[0]+dx,position(node)[1]+dy]})^
}
pub fn selected_roots(st) any^ {
    let chosen = selected_rows(st)^;
    // selecting a group and its child moves the child once, through its owning group.
    [for (row in chosen where not any([for (other in chosen)
        other.id != row.id and paths.path_is_prefix(other.path,row.path)])) row]
}
pub fn unlocked_roots(st) any^ {
    let roots = selected_roots(st)^;
    if (locked(st.doc,[for (row in roots) row.path],true)) raise error("Raster: target layer is locked")
    else roots
}
pub fn selection_bounds(st) => if (len(st.selected)==0) null else
    if (len(st.selected)==1) bounds(lookup(st.doc,st.selected[0]).node)
    else bounds(img.stack([for (row in selected_roots(st)^) row.node]))
pub fn owns_hit(st,id) {
    let hit = lookup(st.doc,id);
    hit != null and any([for (row in layers(st.doc) where row.id in st.selected)
        paths.path_is_prefix(row.path,hit.path)])
}
pub fn bounds(node) {
    if (name(node) == 'stack') {
        let boxes = [for (child in content(node) where composition(child)) bounds(child)]
        if (len(boxes) == 0) {x:0,y:0,width:0,height:0}
        else {
            let left = min([for (b in boxes) b.x])
            let top = min([for (b in boxes) b.y]);
            {x:left,y:top,width:max([for (b in boxes) b.x+b.width])-left,
                height:max([for (b in boxes) b.y+b.height])-top}
        }
    } else {
        let raster = primary(node)
        let m = value(node.transform,affine.identity)
        let pad = sum([0,*[for (style in content(node) where name(style) == 'style' and style.enabled != false) style.outset]])
        let corners = [for (x in [-pad,value(raster.width,0)+pad]) for (y in [-pad,value(raster.height,0)+pad]) affine.project(m,[x,y])]
        let left = min([for (p in corners) p[0]])
        let top = min([for (p in corners) p[1]]);
        {x:position(node)[0]+left,y:position(node)[1]+top,
            width:max([for (p in corners) p[0]])-left,height:max([for (p in corners) p[1]])-top}
    }
}
pub fn hit(image,point) => [for (row in layers(image) where name(row.node) == 'layer' and
    row.node.visible != false and all([for (ancestor in layers(image) where
        ancestor.id != row.id and paths.path_is_prefix(ancestor.path,row.path))
        ancestor.node.visible != false]) and geometry.rect_contains(bounds(row.node),point)) row.id][0]
