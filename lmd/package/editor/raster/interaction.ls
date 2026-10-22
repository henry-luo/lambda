// S12.1.3: sampled drafts render at the host's frame rate and commit one transaction.
import core: .core
import model: .model
import img: lambda.image
import registry: .commands
import transforms: .transform
import retouch: .retouch
import actions: .actions
import paint: .paint
import crop: .crop
import guides: .guides
import navigation: .navigation

fn xy(input) => [input.point.x,input.point.y,model.value(input.pressure,1.0)]
pub fn cancel(st) => if (st.gesture == null) st else
    model.restore({*:st,doc:st.gesture.before,pan:st.gesture.pan,crop:st.gesture.crop,gesture:null},st.gesture.context)
pub fn begin(st,input) any^ {
    let navigation_mode = if (input.navigator != null) "navigator" else if (input.ruler in ["x","y"]) "guide-new" else null
    if (st.gesture != null) raise error("Raster: a gesture is already active")
    else if (navigation_mode == "guide-new" and st.guides_locked) st
    else if (navigation_mode == null and st.tool in ["clone","heal"] and input.alt == true)
        core.exec(st,{id:"retouch.source",point:[input.point.x,input.point.y]})^
    else if (navigation_mode == null and st.tool in ["wand","bucket"]) core.exec(st,
        {id:if (st.tool == "wand") "selection.wand" else "paint.bucket",point:xy(input),
            options:{tolerance:st.tolerance,contiguous:true},
            *:if (st.tool == "wand") {mode:st.selection_mode} else {}})^
    else if (navigation_mode == null and st.tool == "eyedropper") {
        let x = int(floor(input.point.x))
        let y = int(floor(input.point.y));
        if (x<0 or y<0 or x>=st.doc.width or y>=st.doc.height) st
        else {*:st,foreground:[for (sample in img.sample(img.render_frame(st.doc)^,x,y)^) sample/255.0]}
    }
    else {
        let guide = if (navigation_mode == null and st.tool == "move" and st.guides_visible and not st.guides_locked) guides.lookup(st.doc,input.guide) else null
        // an explicitly selected adjustment mask owns its hit region without hiding lower pixel layers otherwise.
        let hit = if (navigation_mode == null and st.tool == "move" and guide == null) model.hit(st.doc,input.point,
            if (st.target == "mask" and len(st.selected)==1) st.selected[0] else null) else null
        let picked = if (navigation_mode == "guide-new") {*:st,guides_visible:true}
            else if (navigation_mode != null or st.tool != "move" or guide != null) st
            else if (input.shift) model.select(st,if (hit == null) st.selected
                else if (hit in st.selected) [for (id in st.selected where id != hit) id] else [*st.selected,hit])^
            else if (hit == null) model.select(st,[])^
            else if (model.owns_hit(st,hit)) st else model.select(st,[hit])^
        let target = if (navigation_mode == null and st.tool in ["brush","erase","gradient",*retouch.kinds]) model.pixel_target(st)^ else null
        let transform_target = if (navigation_mode == null and st.tool in ["transform","straighten"]) transforms.target(st)^ else null;
        if (navigation_mode == null and st.tool == "move" and guide == null and (input.shift or len(picked.selected) == 0)) picked
        else render(sample({*:picked,gesture:{before:picked.doc,context:model.context(picked),pan:picked.pan,
            mode:if (navigation_mode != null) navigation_mode else if (guide == null) st.tool else "guide",guide:guide.id,axis:input.ruler,
            pointer:input.pointer,start:input.point,screen:input.screen,end:input.point,crop:st.crop,
            snap_boxes:if (navigation_mode == null and st.tool == "move" and guide == null and picked.snap_enabled and picked.snap_layers) model.snap_boxes(picked) else [],
            points:[xy(input)],rotate:input.shift,handle:input.handle,crop_handle:input.crop_handle,shift:input.shift,alt:input.alt,
            selection_mode:if (input.shift) "add" else st.selection_mode}},input))^
    }
}
pub fn sample(st,input) {
    if (st.gesture == null) st
    else if (st.gesture.mode == "navigator") navigation.center(st,input.viewport,input.navigator)
    else if (st.gesture.mode == "pan")
        {*:st,pan:{x:st.gesture.pan.x+input.screen.x-st.gesture.screen.x,
            y:st.gesture.pan.y+input.screen.y-st.gesture.screen.y}}
    else {*:st,gesture:{*:st.gesture,end:input.point,shift:input.shift,alt:input.alt,
        points:if (st.gesture.mode in ["brush","erase","lasso",*retouch.kinds] and st.gesture.points[last] != xy(input))
            [*st.gesture.points,xy(input)] else st.gesture.points}}
}
pub fn render(st) any^ {
    if (st.gesture == null or st.gesture.mode in ["pan","navigator"]) st
    else if (st.gesture.mode == "crop") {
        let aspect = if (st.crop_aspect == "free" and st.gesture.shift == true) "1:1" else st.crop_aspect;
        {*:st,crop:if (st.gesture.crop_handle == null) crop.rectangle(st.doc,st.gesture.start,st.gesture.end,aspect,st.crop_ratio)^
            else crop.edit_rectangle(st.doc,st.gesture.crop,st.gesture.start,st.gesture.end,st.gesture.crop_handle,aspect,st.crop_ratio)^}
    }
    else {
        let g = st.gesture
        let before = model.restore({*:st,doc:g.before,gesture:null},g.context)
        let start = [g.start.x,g.start.y]
        let end = [g.end.x,g.end.y]
        let dx = if (g.mode == "move") int(round(g.end.x-g.start.x)) else 0
        let dy = if (g.mode == "move") int(round(g.end.y-g.start.y)) else 0
        // a click or return to the press point must not become a snap-induced edit.
        let snapped = if (g.mode == "move") guides.snap_move(before,model.selection_bounds(before),
            dx,dy,model.value(st.scale,1),g.alt == true or (dx == 0 and dy == 0),g.snap_boxes) else null
        let guide = if (g.mode == "guide") guides.lookup(before.doc,g.guide) else null
        let command = if (g.mode == "move") {id:"layer.move",dx:snapped.dx,dy:snapped.dy}
            else if (g.mode == "guide") {id:"guide.update",guide:g.guide,
                position:int(round(max(0,min(if (guide.axis == "x") before.doc.width else before.doc.height,
                    guide.position+g.end[guide.axis]-g.start[guide.axis]))))}
            else if (g.mode == "guide-new" and g.end.x>=0 and g.end.y>=0 and g.end.x<=before.doc.width and g.end.y<=before.doc.height)
                {id:"guide.add",axis:g.axis,position:int(round(g.end[g.axis]))}
            else if (g.mode == "straighten" and start != end) {id:"layer.straighten",start:g.start,end:g.end,axis:before.straighten_axis}
            else if (g.mode in retouch.kinds) {id:"retouch.stroke",kind:g.mode,points:g.points}
            else if (g.mode == "transform" and start != end)
                (if (g.handle == null) transforms.gesture(before,g.start,g.end,g.rotate)^
                    else transforms.handle_gesture(before,g.start,g.end,g.handle,g.shift == true,g.alt == true)^)
            else if (g.mode in ["brush","erase"]) {id:"paint.stroke",points:g.points,options:{mode:if (g.mode == "erase") "erase" else "paint"}}
            else if (g.mode == "gradient" and start != end)
                {id:"paint.gradient",start:start,end:end,options:{kind:st.gradient_kind}}
            else if (g.mode in ["rectangle","ellipse"]) {id:"selection.shape",mode:g.selection_mode,
                options:{shape:g.mode,start:start,end:end}}
            else if (g.mode == "lasso" and len(g.points)>=3) {id:"selection.shape",mode:g.selection_mode,
                options:{shape:"polygon",points:g.points}} else null
        let admitted = if (command == null) true else actions.admit(before,command)^
        let stroke = if (g.mode in ["brush","erase"]) paint.preview(before,command,g.stroke)^ else null
        let changed = if (stroke != null) stroke.state else if (command == null) before else core.apply_command(before,command)^;
        {*:st,doc:changed.doc,selection:changed.selection,retouch:changed.retouch,
            gesture:{*:g,command:command,snapped:snapped,stroke:stroke.continuation,
                repaint:if (stroke == null) null else {before:st.doc,rect:stroke.rect}}}
    }
}
pub fn preview(st,input) any^ => render(sample(st,input))^
pub fn finish(st) any^ {
    if (st.gesture == null) st
    else if (st.gesture.mode in ["pan","navigator","crop"]) {*:st,gesture:null}
    else {
        let checked = model.validate(st.doc)^
        let before = model.restore({*:st,doc:st.gesture.before,gesture:null},st.gesture.context)
        let id = if (st.gesture.mode == "move") "layer.move" else if (st.gesture.mode == "guide-new") "guide.add" else if (st.gesture.mode == "guide") "guide.update"
            else if (st.gesture.mode == "straighten") "layer.straighten" else if (st.gesture.mode == "transform") "layer.transform" else if (st.gesture.mode in retouch.kinds) "retouch.stroke" else if (st.gesture.mode in ["brush","erase"]) "paint.stroke"
            else if (st.gesture.mode == "gradient") "paint.gradient" else "selection.shape";
        let recorded = core.record(before,{*:st,gesture:null},registry.descriptor(id).label);
        if (st.gesture.command == null) recorded else actions.capture(before,st.gesture.command,recorded)
    }
}
