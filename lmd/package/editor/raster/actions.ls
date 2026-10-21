// S9.1.3: a recorded action targets one declared active layer; replay is atomic and pure.
import model: .model
import registry: .commands
import paint: .paint
pub fn supported(id:string) => id in ["text.update","vector.update"] or starts_with(id,"style.") or starts_with(id,"effect.") or starts_with(id,"paint.") or starts_with(id,"pixels.") or
    starts_with(id,"selection.") or starts_with(id,"mask.") or starts_with(id,"retouch.") or
    id in ["layer.select","layer.move","tool.configure","layer.rename","layer.opacity","layer.visibility","layer.lock","layer.blend","layer.transform","layer.straighten","layer.rasterize"]
fn portable(value,depth=0) => if (depth>32) false
    else if (value is map) all([for (key,v in value) portable(v,depth+1)])
    else if (value is array) len(value)<=65536 and all([for (v in value) portable(v,depth+1)])
    else value == null or value is number or value is bool or value is string or value is symbol
pub fn validate(action) any^ {
    if (not (action is map) or not portable(action) or action.version != 1 or action.target != "active-layer" or
        not (action.name is string) or len(action.name)<1 or len(action.name)>256 or
        not (action.steps is array) or len(action.steps)<1 or len(action.steps)>512)
        raise error("Raster: invalid action header or step count")
    else {
        let checked = [for (step in action.steps) registry.checked(step)^];
        if (not all([for (step in action.steps) supported(step.id)])) raise error("Raster: action contains a nonportable command")
        else action
    }
}
pub fn admit(st,command) any^ {
    if (st.recording == null) true
    else if (len(st.recording.steps)>=512) raise error("Raster: action step limit reached; stop recording")
    else if (command.id == "layer.select") (
        if (command.ids != [st.recording.target]) raise error("Raster: stop recording before switching action targets") else true)
    else if (st.selected != [st.recording.target]) raise error("Raster: stop recording before switching action targets")
    else if (not portable(command)) raise error("Raster: actions accept plain command data only")
    else if (not supported(command.id)) raise error("Raster: this command is unavailable while recording a single-layer action")

    else true
}
fn resolved(st,command) {
    let options = model.value(command.options,{})
    if (command.id == "paint.stroke") {*:command,options:{*:st.brush,color:paint.color(st),*:options}}
    else if (command.id == "retouch.stroke") {*:command,options:{*:st.brush,strength:st.retouch.strength,radius:st.retouch.radius,*:options}}
    else if (command.id in ["paint.fill","pixels.fill","pixels.clear","paint.bucket"]) {*:command,opacity:model.value(command.opacity,st.brush.opacity),
        *:if (command.id in ["paint.fill","pixels.fill"]) {color:model.value(command.color,paint.color(st))} else {}}
    else if (command.id == "paint.gradient") {*:command,options:{color:paint.color(st),end_color:paint.color(st,st.background),
        opacity:st.brush.opacity,kind:st.gradient_kind,*:options}}
    else command
}
pub fn capture(before,command,after) => if (before.recording == null) after
    else {*:after,recording:{*:before.recording,steps:[*before.recording.steps,resolved(before,command)]}}
pub fn control(st,command) any^ {
    if (command.id == "action.start") {
        let row = model.active(st)^;
        if (st.recording != null) raise error("Raster: an action is already recording")
        else if (command.name != null and (not (command.name is string) or len(command.name)<1 or len(command.name)>256)) raise error("Raster: invalid action name")
        else {*:st,recording:{target:row.id,name:model.value(command.name,"Action"),facet:st.target,steps:[]}}
    }
    else if (command.id == "action.cancel") {*:st,recording:null}
    else if (command.id == "action.stop") (
        if (st.recording == null) raise error("Raster: no action is recording")
        else (let action = validate({version:1,target:"active-layer",facet:st.recording.facet,name:st.recording.name,steps:st.recording.steps})^,
            {*:st,recording:null,actions:[*st.actions,action]}))
    else if (command.id == "action.load") (
        let action = validate(command.action)^,
        {*:st,actions:[*st.actions,action]})
    else raise error("Raster: unknown action control")
}
pub fn replay(st,action,execute) any^ {
    let checked = validate(action)^
    let target = model.active(st)^;
    if (st.recording != null) raise error("Raster: stop recording before replaying an action")
    else {
        let initial = model.select(st,[target.id],model.value(checked.facet,"pixels"))^;
        reduce([initial,*checked.steps],(session,step) => execute(session,
            if (step.id == "layer.select") {*:step,ids:[target.id]} else step)^)
    }
}
