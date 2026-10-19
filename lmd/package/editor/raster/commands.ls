// one descriptor table drives validation, enablement and human-readable history labels.
import model: .model
import history: lambda.editor.mod_history

pub let catalog = [
    {id:"layer.select",label:"Select layer",fields:["ids","target"],target:false},
    {id:"layer.insert",label:"Place image",fields:["raster","name","position","parent"],target:false},
    {id:"layer.new",label:"New layer",fields:["name","parent"],target:false},
    {id:"layer.duplicate",label:"Duplicate layer",fields:[],target:true},
    {id:"layer.delete",label:"Delete layers",fields:[],target:true},
    {id:"layer.move",label:"Move layers",fields:["dx","dy"],target:true},
    {id:"layer.reorder",label:"Reorder layer",fields:["index"],target:true},
    {id:"layer.group",label:"Group layers",fields:["name"],target:true},
    {id:"layer.merge",label:"Merge layers",fields:["name"],target:true},
    {id:"layer.ungroup",label:"Ungroup layers",fields:[],target:true},
    {id:"layer.rename",label:"Rename layer",fields:["name"],target:true},
    {id:"layer.visibility",label:"Layer visibility",fields:["visible"],target:true},
    {id:"layer.opacity",label:"Layer opacity",fields:["opacity"],target:true},
    {id:"layer.lock",label:"Layer lock",fields:["locked"],target:true},
    {id:"mask.add",label:"Add layer mask",fields:["coverage"],target:true},
    {id:"mask.remove",label:"Remove layer mask",fields:[],target:true},
    {id:"mask.toggle",label:"Toggle layer mask",fields:[],target:true},
    {id:"pixels.fill",label:"Fill target",fields:["color"],target:true},
    {id:"pixels.clear",label:"Clear target",fields:[],target:true},
    {id:"pixels.adjust",label:"Adjust layer",fields:["options"],target:true},
    {id:"document.flatten",label:"Flatten image",fields:[],target:false},
    {id:"document.resize_canvas",label:"Resize canvas",fields:["width","height"],target:false},
    {id:"document.resize_image",label:"Resize image",fields:["width","height"],target:false},
    {id:"history.undo",label:"Undo",fields:[],target:false},
    {id:"history.redo",label:"Redo",fields:[],target:false}
]
pub fn descriptor(id) => [for (entry in catalog where entry.id == id) entry][0]
pub fn checked(command) any^ {
    let spec = descriptor(command.id)
    if (not (command is map) or spec == null) raise error("Raster: unknown command")
    else if (any([for (key,item in command) not (string(key) in ["id","group",*spec.fields])]))
        raise error("Raster: unknown command parameter")
    else command
}
pub fn command_state(st,id) any^ {
    let spec = descriptor(id)
    let reason = if (spec == null) "Unknown command" else if (st.gesture != null) "Finish the active gesture"
        else if (spec.target and len(st.selected)==0) "Select a layer"
        else if (id == "history.undo" and not history.can_undo(st.hist)^) "Nothing to undo"
        else if (id == "history.redo" and not history.can_redo(st.hist)^) "Nothing to redo" else "";
    {id:id,enabled:reason == "",reason:reason}
}
