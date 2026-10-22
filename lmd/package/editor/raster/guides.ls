// S9.1.2 / S9.2.2: guides are authored metadata; snapping derives transient move offsets.
import img: lambda.image
import geometry: lambda.editor.draw.geometry
import navigation: .navigation

pub fn container(image) => [for (child in content(image) where name(child) == 'metadata' and child.role == 'photos_guides') child][0]
pub fn items(image) => if (container(image) == null) [] else content(container(image))
pub fn lookup(image,id) => [for (guide in items(image) where guide.id == id) guide][0]
fn extent(image,axis) => if (axis == "x") image.width else image.height
fn position_valid(image,axis,p) => axis in ["x","y"] and p is int and p>=0 and p<=extent(image,axis)
pub fn validate(image) any^ {
    let entries = items(image);
    if (len([for (child in content(image) where name(child) == 'metadata' and child.role == 'photos_guides') child])>1 or
        len(entries)>64 or len(unique([for (g in entries) g.id])) != len(entries) or
        not all([for (g in entries) name(g) == 'guide' and g.id is string and g.id != "" and
            position_valid(image,g.axis,g.position)])) raise error("Raster: invalid project guides")
    else true
}
fn replace(image,entries) any^ => img.with_children(image,
    [*[for (child in content(image) where child != container(image)) child],<metadata role:'photos_guides',*entries>])^
fn fresh(image,index=1) => if (lookup(image,"guide-" ++ string(index)) != null) fresh(image,index+1) else "guide-" ++ string(index)
pub fn resized(image,sx=1,sy=1,origin=[0,0]) any^ {
    let moved = [for (g in items(image)) img.with_properties(g,{position:int(round(g.position*(if (g.axis == "x") sx else sy)))-origin[if (g.axis == "x") 0 else 1]})^];
    if (len(moved)==0) image else replace(image,[for (g in moved where position_valid(image,g.axis,g.position)) g])^
}
pub fn run_command(st,command) any^ {
    let entries = items(st.doc)
    let guide = lookup(st.doc,command.guide)
    if (command.id == "guide.configure") {
        if (any([for (key in ["visible","snap","layers"]) command[key] != null and not (command[key] is bool)]))
            raise error("Raster: guide view settings require booleans")
        else {*:st,guides_visible:if (command.visible == null) st.guides_visible else command.visible,
            snap_enabled:if (command.snap == null) st.snap_enabled else command.snap,
            snap_layers:if (command.layers == null) st.snap_layers else command.layers}
    }
    else if (command.id == "guide.clear") (if (len(entries)==0) st else {*:st,doc:replace(st.doc,[])^})
    else if (command.id == "guide.add") {
        if (not position_valid(st.doc,command.axis,command.position)) raise error("Raster: enter an integer guide position inside the canvas")
        else if (any([for (g in entries) g.axis == command.axis and g.position == command.position])) st
        else if (len(entries)>=64) raise error("Raster: project guide limit reached")
        else {*:st,doc:replace(st.doc,[*entries,<guide id:fresh(st.doc),axis:command.axis,position:command.position>])^}
    }
    else if (guide == null) raise error("Raster: select a guide")
    else if (command.id == "guide.remove") {*:st,doc:replace(st.doc,[for (g in entries where g.id != guide.id) g])^}
    else if (command.id == "guide.update") (
        if (not position_valid(st.doc,guide.axis,command.position)) raise error("Raster: guide position must stay inside the canvas")
        else if (guide.position == command.position) st
        else {*:st,doc:replace(st.doc,[for (g in entries) if (g.id == guide.id) img.with_properties(g,{position:command.position})^ else g])^})
    else raise error("Raster: unknown guide command")
}
fn anchors(box,axis) => [box[axis],box[axis]+box[if (axis == "x") "width" else "height"]/2,
    box[axis]+box[if (axis == "x") "width" else "height"]]
fn snapped_axis(st,box,axis,delta,tolerance,boxes) {
    let targets = [0,extent(st.doc,axis)/2,extent(st.doc,axis),
        *if (st.guides_visible) [for (g in items(st.doc) where g.axis == axis) g.position] else [],
        *[for (other in boxes) *anchors(other,axis)],
        *navigation.grid_targets(st,anchors(box,axis),delta,extent(st.doc,axis))]
    // retain the first equally near target: canvas, authored guides, then foreground-first layers.
    let hit = reduce([null,*[for (target in targets) for (anchor in anchors(box,axis))
        {distance:abs(target-anchor-delta),offset:target-anchor,target:target}]],
        (best,candidate) => if (best == null or candidate.distance<best.distance) candidate else best);
    if (hit.distance<=tolerance) {delta:int(round(hit.offset)),target:hit.target} else {delta:delta,target:null}
}
pub fn snap_move(st,box,dx,dy,scale=1,bypass=false,boxes=[]) {
    if (not st.snap_enabled or bypass or box == null) {dx:dx,dy:dy,x:null,y:null}
    else {
        let tolerance = 6/(if (geometry.finite_number(scale) and scale>0) scale else 1)
        let x = snapped_axis(st,box,"x",dx,tolerance,if (st.snap_layers) boxes else [])
        let y = snapped_axis(st,box,"y",dy,tolerance,if (st.snap_layers) boxes else []);
        {dx:x.delta,dy:y.delta,x:x.target,y:y.target}
    }
}
