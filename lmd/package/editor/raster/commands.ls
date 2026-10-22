// one descriptor table drives validation, enablement and human-readable history labels.
import model: .model
import history: lambda.editor.mod_history

pub let catalog = [
    {id:"view.configure",label:"View settings",fields:["rulers","grid","spacing","subdivisions","snap"],target:false},
    {id:"channel.view",label:"View channel",fields:["channel"],target:false},
    {id:"channel.save",label:"Save selection channel",fields:["name"],target:false},
    {id:"channel.capture",label:"Capture component channel",fields:["channel","name"],target:false},
    {id:"channel.load",label:"Load channel selection",fields:["channel"],target:false},
    {id:"channel.rename",label:"Rename saved channel",fields:["channel","name"],target:false},
    {id:"channel.delete",label:"Delete saved channel",fields:["channel"],target:false},
    {id:"channel.replace",label:"Replace pixel component",fields:["channel","component"],target:true},
    {id:"style.add",label:"Add layer style",fields:["options"],target:true},
    {id:"style.update",label:"Edit layer style",fields:["index","options"],target:true},
    {id:"style.toggle",label:"Toggle layer style",fields:["index"],target:true},
    {id:"style.remove",label:"Remove layer style",fields:["index"],target:true},
    {id:"font.embed",label:"Embed font",fields:["font","bytes","family"],target:false},
    {id:"text.insert",label:"Add text",fields:["options","name","position","parent"],target:false},
    {id:"text.update",label:"Edit text",fields:["options"],target:true},
    {id:"vector.insert",label:"Add shape or path",fields:["options","name","position","parent"],target:false},
    {id:"vector.update",label:"Edit shape or path",fields:["options"],target:true},
    {id:"action.start",label:"Record action",fields:["name"],target:true},
    {id:"action.stop",label:"Save recorded action",fields:[],target:false},
    {id:"action.cancel",label:"Cancel recording",fields:[],target:false},
    {id:"action.load",label:"Load action",fields:["action"],target:false},
    {id:"action.replay",label:"Replay action",fields:["action"],target:true},
    {id:"retouch.source",label:"Set retouch source",fields:["point"],target:false},
    {id:"retouch.configure",label:"Retouch settings",fields:["aligned","strength","radius"],target:false},
    {id:"retouch.stroke",label:"Retouch stroke",fields:["points","kind","options"],target:true},
    {id:"layer.clipping",label:"Toggle layer clipping",fields:["clipping"],target:true},
    {id:"layer.isolation",label:"Change group isolation",fields:["isolation"],target:true},
    {id:"layer.blend",label:"Change blend mode",fields:["blend"],target:true},
    {id:"layer.transform",label:"Transform layers",fields:["matrix","delta","angle","scale_x","scale_y"],target:true},
    {id:"layer.straighten",label:"Straighten layers",fields:["start","end","axis"],target:true},
    {id:"guide.add",label:"Add guide",fields:["axis","position"],target:false},
    {id:"guide.update",label:"Move guide",fields:["guide","position"],target:false},
    {id:"guide.remove",label:"Remove guide",fields:["guide"],target:false},
    {id:"guide.clear",label:"Clear guides",fields:[],target:false},
    {id:"guide.configure",label:"Guide view settings",fields:["visible","snap","layers"],target:false},
    {id:"layer.rasterize",label:"Rasterize layer",fields:[],target:true},
    {id:"effect.add",label:"Add editable effect",fields:["options","name"],target:true},
    {id:"effect.update",label:"Update effect",fields:["index","options","name"],target:true},
    {id:"effect.toggle",label:"Toggle effect",fields:["index"],target:true},
    {id:"effect.remove",label:"Remove effect",fields:["index"],target:true},
    {id:"effect.reorder",label:"Reorder effect",fields:["index","to"],target:true},
    {id:"tool.configure",label:"Tool settings",fields:["tool","brush","foreground","background","selection_mode","tolerance","gradient_kind","straighten_axis","crop_aspect"],target:false},
    {id:"layer.select",label:"Select layer",fields:["ids","target"],target:false},
    {id:"layer.insert",label:"Place image",fields:["raster","name","position","parent"],target:false},
    {id:"layer.new",label:"New layer",fields:["name","parent"],target:false},
    {id:"adjustment.insert",label:"New adjustment layer",fields:["options","name","parent"],target:false},
    {id:"layer.duplicate",label:"Duplicate layer",fields:[],target:true},
    {id:"layer.delete",label:"Delete layers",fields:[],target:true},
    {id:"layer.move",label:"Move layers",fields:["dx","dy"],target:true},
    {id:"layer.align",label:"Align layers",fields:["mode","relative"],target:true},
    {id:"layer.distribute",label:"Distribute layers",fields:["axis","relative"],target:true},
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
    {id:"mask.from_selection",label:"Mask from selection",fields:[],target:true},
    {id:"mask.invert",label:"Invert layer mask",fields:[],target:true},
    {id:"mask.apply",label:"Apply layer mask",fields:[],target:true},
    {id:"selection.from_mask",label:"Select layer mask",fields:[],target:true},
    {id:"clipboard.copy",label:"Copy pixels",fields:[],target:true},
    {id:"clipboard.copy_merged",label:"Copy merged",fields:[],target:false},
    {id:"clipboard.cut",label:"Cut pixels",fields:[],target:true},
    {id:"clipboard.paste",label:"Paste pixels",fields:[],target:false},
    {id:"document.crop",label:"Crop canvas",fields:["rect"],target:false},
    {id:"selection.clear",label:"Deselect",fields:[],target:false},
    {id:"selection.all",label:"Select all",fields:[],target:false},
    {id:"selection.invert",label:"Invert selection",fields:[],target:false},
    {id:"selection.feather",label:"Feather selection",fields:["radius"],target:false},
    {id:"selection.shape",label:"Select region",fields:["options","mode"],target:false},
    {id:"selection.wand",label:"Select similar color",fields:["point","options","mode"],target:false},
    {id:"paint.stroke",label:"Paint stroke",fields:["points","options"],target:true},
    {id:"paint.fill",label:"Fill selection",fields:["color","opacity"],target:true},
    {id:"paint.gradient",label:"Gradient",fields:["start","end","options"],target:true},
    {id:"paint.bucket",label:"Flood fill",fields:["point","options","opacity"],target:true},
    {id:"pixels.fill",label:"Fill target",fields:["color","opacity"],target:true},
    {id:"pixels.clear",label:"Clear target",fields:["opacity"],target:true},
    {id:"pixels.adjust",label:"Adjust layer",fields:["options"],target:true},
    {id:"document.flatten",label:"Flatten image",fields:[],target:false},
    {id:"document.resize_canvas",label:"Resize canvas",fields:["width","height","anchor"],target:false},
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
