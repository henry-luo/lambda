// S12.1.3: pointer drafts share one begin/update/finish/cancel transaction path.
import core: .core
import model: .model

pub fn cancel(st) => if (st.gesture == null) st else
    model.restore({*:st,doc:st.gesture.before,pan:st.gesture.pan,gesture:null},st.gesture.context)
pub fn begin(st,input) any^ {
    if (st.gesture != null) raise error("Raster: a gesture is already active")
    else {
        let hit = model.hit(st.doc,input.point)
        let picked = if (st.tool == "pan") st
            else if (input.shift) model.select(st,if (hit == null) st.selected
                else if (hit in st.selected) [for (id in st.selected where id != hit) id] else [*st.selected,hit])^
            else if (hit == null) model.select(st,[])^
            else if (hit in st.selected) st else model.select(st,[hit])^;
        if (st.tool != "pan" and (input.shift or len(picked.selected) == 0)) picked
        else {*:picked,gesture:{before:picked.doc,context:model.context(picked),pan:picked.pan,
            mode:st.tool,pointer:input.pointer,start:input.point,screen:input.screen}}
    }
}
pub fn preview(st,input) any^ {
    if (st.gesture == null) st
    else if (st.gesture.mode == "pan")
        {*:st,pan:{x:st.gesture.pan.x+input.screen.x-st.gesture.screen.x,
            y:st.gesture.pan.y+input.screen.y-st.gesture.screen.y}}
    else {
        let original = {*:st,doc:st.gesture.before,gesture:null}
        let moved = core.apply_command(original,{id:"layer.move",dx:int(round(input.point.x-st.gesture.start.x)),
            dy:int(round(input.point.y-st.gesture.start.y))})^;
        {*:st,doc:moved.doc}
    }
}
pub fn finish(st) any^ {
    if (st.gesture == null) st
    else if (st.gesture.mode == "pan") {*:st,gesture:null}
    else {
        let checked = model.validate(st.doc)^
        let before = model.restore({*:st,doc:st.gesture.before,gesture:null},st.gesture.context);
        core.record(before,{*:st,gesture:null},"layer.move")
    }
}
