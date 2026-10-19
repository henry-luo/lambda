// S12.1.3: sampled drafts render at the host's frame rate and commit one transaction.
import core: .core
import model: .model
import img: lambda.image
import registry: .commands

fn xy(input) => [input.point.x,input.point.y,model.value(input.pressure,1.0)]
pub fn cancel(st) => if (st.gesture == null) st else
    model.restore({*:st,doc:st.gesture.before,pan:st.gesture.pan,gesture:null},st.gesture.context)
pub fn begin(st,input) any^ {
    if (st.gesture != null) raise error("Raster: a gesture is already active")
    else if (st.tool in ["wand","bucket"]) core.exec(st,
        {id:if (st.tool == "wand") "selection.wand" else "paint.bucket",point:xy(input),
            options:{tolerance:st.tolerance,contiguous:true},
            *:if (st.tool == "wand") {mode:st.selection_mode} else {}})^
    else if (st.tool == "eyedropper") {
        let x = int(floor(input.point.x))
        let y = int(floor(input.point.y));
        if (x<0 or y<0 or x>=st.doc.width or y>=st.doc.height) st
        else {*:st,foreground:[for (sample in img.sample(img.render_frame(st.doc)^,x,y)^) sample/255.0]}
    }
    else {
        let hit = if (st.tool == "move") model.hit(st.doc,input.point) else null
        let picked = if (st.tool != "move") st
            else if (input.shift) model.select(st,if (hit == null) st.selected
                else if (hit in st.selected) [for (id in st.selected where id != hit) id] else [*st.selected,hit])^
            else if (hit == null) model.select(st,[])^
            else if (hit in st.selected) st else model.select(st,[hit])^
        let target = if (st.tool in ["brush","erase","gradient"]) model.pixel_target(st)^ else null;
        if (st.tool == "move" and (input.shift or len(picked.selected) == 0)) picked
        else render({*:picked,gesture:{before:picked.doc,context:model.context(picked),pan:picked.pan,
            mode:st.tool,pointer:input.pointer,start:input.point,screen:input.screen,end:input.point,
            points:[xy(input)],selection_mode:if (input.shift) "add" else st.selection_mode}})^
    }
}
pub fn sample(st,input) {
    if (st.gesture == null) st
    else if (st.gesture.mode == "pan")
        {*:st,pan:{x:st.gesture.pan.x+input.screen.x-st.gesture.screen.x,
            y:st.gesture.pan.y+input.screen.y-st.gesture.screen.y}}
    else {*:st,gesture:{*:st.gesture,end:input.point,
        points:if (st.gesture.mode in ["brush","erase","lasso"] and st.gesture.points[last] != xy(input))
            [*st.gesture.points,xy(input)] else st.gesture.points}}
}
pub fn render(st) any^ {
    if (st.gesture == null or st.gesture.mode == "pan") st
    else {
        let g = st.gesture
        let before = model.restore({*:st,doc:g.before,gesture:null},g.context)
        let start = [g.start.x,g.start.y]
        let end = [g.end.x,g.end.y]
        let command = if (g.mode == "move") {id:"layer.move",dx:int(round(g.end.x-g.start.x)),dy:int(round(g.end.y-g.start.y))}
            else if (g.mode in ["brush","erase"]) {id:"paint.stroke",points:g.points,options:{mode:if (g.mode == "erase") "erase" else "paint"}}
            else if (g.mode == "gradient" and start != end)
                {id:"paint.gradient",start:start,end:end,options:{kind:st.gradient_kind}}
            else if (g.mode in ["rectangle","ellipse"]) {id:"selection.shape",mode:g.selection_mode,
                options:{shape:g.mode,start:start,end:end}}
            else if (g.mode == "lasso" and len(g.points)>=3) {id:"selection.shape",mode:g.selection_mode,
                options:{shape:"polygon",points:g.points}} else null
        let changed = if (command == null) before else core.apply_command(before,command)^;
        {*:st,doc:changed.doc,selection:changed.selection}
    }
}
pub fn preview(st,input) any^ => render(sample(st,input))^
pub fn finish(st) any^ {
    if (st.gesture == null) st
    else if (st.gesture.mode == "pan") {*:st,gesture:null}
    else {
        let checked = model.validate(st.doc)^
        let before = model.restore({*:st,doc:st.gesture.before,gesture:null},st.gesture.context)
        let id = if (st.gesture.mode == "move") "layer.move" else if (st.gesture.mode in ["brush","erase"]) "paint.stroke"
            else if (st.gesture.mode == "gradient") "paint.gradient" else "selection.shape";
        core.record(before,{*:st,gesture:null},registry.descriptor(id).label)
    }
}
