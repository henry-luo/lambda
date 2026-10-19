// S9.1.2 / D7.2.4v3: the shared image tree is authoritative; rows only resolve identities.
import img: lambda.image
import sequence: lambda.editor.mod_doc
import paths: lambda.editor.mod_source_pos
import geometry: lambda.editor.draw.geometry
import common: lambda.slide.common
import affine: lambda.chart.svg_transform

pub fn value(v,fallback) => common.value(v,fallback)
pub fn finite(v) => geometry.finite_number(v)
pub fn composition(node) => name(node) in ['layer','stack']
pub fn position(node) => value(node.position,[0,0])
pub fn mask(node) => [for (child in content(node) where name(child) == 'mask') child][0]
pub fn root_path(image) => [[for (i,child in content(image) where name(child) == 'stack') i][0]]

pub fn rows(node,path=[],depth=0,parent="") =>
    [for (i,child in content(node) where composition(child))
        *[{id:child.id,node:child,path:[*path,i],parent:parent,depth:depth,index:i},
          *if (name(child) == 'stack') rows(child,[*path,i],depth+1,child.id) else []]]
pub fn layers(image) => rows(img.composition(image),root_path(image))
pub fn lookup(image,id) => [for (row in layers(image) where row.id == id) row][0]
pub fn identifiers(image) => [*[for (row in layers(image)) row.id],
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
    let ids = identifiers(image)
    if (len(unique(ids)) != len(ids) or not all([for (id in ids) id is string and id != ""]))
        raise error("Raster: duplicate or invalid document identities")
    else true
}
pub fn replace(image,id,node) any^ {
    let found = lookup(image,id)
    if (found == null) raise error("Raster: missing layer target")
    else img.replace_at(image,found.path,node)^
}
pub fn context(st) => {selected:st.selected,target:st.target,selection:st.selection}
pub fn restore(st,context) => if (context == null) st else
    {*:st,selected:[for (id in context.selected where lookup(st.doc,id) != null) id],
        target:context.target,selection:context.selection}
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
pub fn pixel_target(st,writing=true) any^ {
    let row = active(st)^
    let mask_node = if (st.target == "mask") mask(row.node) else null
    let raster = if (st.target == "mask") mask_node[0] else img.raster_of(row.node)
    if (name(row.node) != 'layer' or raster == null) raise error("Raster: select an editable pixel layer or mask")
    else if (row.node.transform != null and row.node.transform != affine.identity)
        raise error("Raster: rasterize the transformed layer before editing source pixels in canvas coordinates")
    else if (writing and locked(st.doc,[row.path])) raise error("Raster: target layer is locked")
    else {row:row,raster:raster,position:[position(row.node)[0]+value(mask_node.position,[0,0])[0],
        position(row.node)[1]+value(mask_node.position,[0,0])[1]]}
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
        let raster = img.raster_of(node)
        let m = value(node.transform,affine.identity)
        let corners = [for (x in [0,value(raster.width,0)]) for (y in [0,value(raster.height,0)]) affine.project(m,[x,y])]
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
