// S9.1.2 / S9.2.2: guides are authored metadata; snapping derives transient move offsets.
import img: lambda.image
import geometry: lambda.editor.draw.geometry
import navigation: .navigation
import metadata: .metadata

pub let limit = 64
pub let layouts = [{id:"center",label:"Center",columns:2,rows:2},
    {id:"thirds",label:"Rule of thirds",columns:3,rows:3},{id:"quarters",label:"Quarters",columns:4,rows:4},
    {id:"custom",label:"Custom"}]
pub fn container(image) => metadata.container(image,'photos_guides')
pub fn items(image) => metadata.items(image,'photos_guides')
pub fn lookup(image,id) => [for (guide in items(image) where guide.id == id) guide][0]
fn has_position(entries,axis,position) => any([for (g in entries) g.axis == axis and g.position == position])
fn extent(image,axis) => if (axis == "x") image.width else image.height
fn position_valid(image,axis,p) => axis in ["x","y"] and p is int and p>=0 and p<=extent(image,axis)
pub fn validate(image) any^ {
    let entries = items(image);
    if (len(metadata.containers(image,'photos_guides'))>1 or
        len(entries)>limit or len(unique([for (g in entries) g.id])) != len(entries) or
        not all([for (g in entries) name(g) == 'guide' and g.id is string and g.id != "" and
            position_valid(image,g.axis,g.position)])) raise error("Raster: invalid project guides")
    else true
}
fn replace(image,entries) any^ => metadata.replace(image,'photos_guides',entries)^
fn fresh(image,index=1) => if (lookup(image,"guide-" ++ string(index)) != null) fresh(image,index+1) else "guide-" ++ string(index)
fn added(image,axis,position) any^ {
    let entries = items(image);
    if (has_position(entries,axis,position)) image
    else if (len(entries)>=limit) raise error("Raster: project guide limit reached")
    else replace(image,[*entries,<guide id:fresh(image),axis:axis,position:position>])^
}
fn layout_axis(extent,count,margin,gutter) any^ {
    let cell = if (count == 0) 0 else (extent-2*margin-(count-1)*gutter)/count;
    if (count == 0) []
    else if (cell<1) raise error("Raster: guide layout needs at least one pixel per cell")
    else unique([*if (margin>0) [margin] else [],
        *[for (i in 1 to count-1) *[int(round(margin+i*cell+(i-1)*gutter)),
            *if (gutter>0) [int(round(margin+i*cell+i*gutter))] else []]],
        *if (margin>0) [extent-margin] else []])
}
pub fn layout(image,options) any^ {
    let preset = if (options.preset == null) "thirds" else options.preset
    let entry = [for (p in layouts where p.id == preset) p][0]
    let columns = if (preset != "custom") entry.columns else if (options.columns == null) 3 else options.columns
    let rows = if (preset != "custom") entry.rows else if (options.rows == null) 3 else options.rows
    let margin = if (options.margin == null) 0 else options.margin
    let gutter = if (options.gutter == null) 0 else options.gutter
    if (entry == null or (preset != "custom" and any([for (k in ["columns","rows","margin","gutter"]) options[k] != null])))
        raise error("Raster: choose a guide layout preset or custom layout settings")
    else if (not all([for (n in [columns,rows]) n is int and n>=0 and n<=limit+1]) or columns+rows == 0 or
        not all([for (n in [margin,gutter]) n is int and n>=0]))
        raise error("Raster: guide layout requires nonnegative whole counts, margin and gutter")
    else {
        let coordinates = [for (axis in ["x","y"]) for (position in layout_axis(extent(image,axis),
            if (axis == "x") columns else rows,margin,gutter)^) {axis:axis,position:position}];
        if (len(coordinates)>limit) raise error("Raster: project guide limit reached") else coordinates
    }
}
pub fn resized(image,sx=1,sy=1,origin=[0,0]) any^ {
    let moved = [for (g in items(image)) img.with_properties(g,{position:int(round(g.position*(if (g.axis == "x") sx else sy)))-origin[if (g.axis == "x") 0 else 1]})^];
    if (len(moved)==0) image else replace(image,[for (g in moved where position_valid(image,g.axis,g.position)) g])^
}
pub fn run_command(st,command) any^ {
    let entries = items(st.doc)
    let guide = lookup(st.doc,command.guide)
    if (command.id == "guide.configure") {
        if (any([for (key in ["visible","snap","layers","locked"]) command[key] != null and not (command[key] is bool)]))
            raise error("Raster: guide view settings require booleans")
        else {*:st,guides_visible:if (command.visible == null) st.guides_visible else command.visible,
            snap_enabled:if (command.snap == null) st.snap_enabled else command.snap,
            snap_layers:if (command.layers == null) st.snap_layers else command.layers,
            guides_locked:if (command.locked == null) st.guides_locked else command.locked}
    }
    else if (st.guides_locked) raise error("Raster: guides are locked")
    else if (command.id == "guide.layout") {
        let positions = layout(st.doc,command)^
        let replacing = if (command.replace == null) true else command.replace
        if (not (replacing is bool)) raise error("Raster: guide layout replace must be boolean")
        else {
            // one result expression keeps validation from becoming an extra null in the returned session.
            // replacement keeps the first stable ID at coincident positions; append preserves authored entries.
            let retained = if (not replacing) entries else [for (i,g in entries where
                has_position(positions,g.axis,g.position) and not has_position(slice(entries,0,i),g.axis,g.position)) g]
            let missing = [for (p in positions where not has_position(retained,p.axis,p.position)) p];
            if (len(retained)+len(missing)>limit) raise error("Raster: project guide limit reached")
            else {*:st,doc:reduce([replace(st.doc,retained)^,*missing],(image,p) => added(image,p.axis,p.position)^)}
        }
    }
    else if (command.id == "guide.clear") (if (len(entries)==0) st else {*:st,doc:replace(st.doc,[])^})
    else if (command.id == "guide.add") {
        if (not position_valid(st.doc,command.axis,command.position)) raise error("Raster: enter an integer guide position inside the canvas")
        else {*:st,doc:added(st.doc,command.axis,command.position)^}
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
