// S9.1.3: enabled and disabled effects retain checked parameters; baking is explicit.
import img: lambda.image
import model: .model
import sequence: lambda.editor.mod_doc
pub let controls = [
    {key:"exposure",label:"Exposure",value:0},{key:"brightness",label:"Brightness",value:0},
    {key:"contrast",label:"Contrast",value:1},{key:"saturation",label:"Saturation",value:1},
    {key:"temperature",label:"Temperature",value:0},{key:"gamma",label:"Gamma",value:1},
    {key:"shadows",label:"Shadows",value:0},{key:"highlights",label:"Highlights",value:0},
    {key:"softness",label:"Blur",value:0},{key:"sharpen",label:"Sharpen",value:0}]
pub fn items(node) => if (node == null) [] else [for (child in content(node) where name(child) == 'effect') child]
pub fn checked(options,label,adjustment=false) any^ {
    if (not (label is string) or len(label)<1 or len(label)>256) raise error("Raster: effect needs a name of 1 to 256 characters")
    else {
        let effect = <effect kind:'adjust',enabled:true,name:label,options:options>
        // validate the target profile even when a hidden layer would skip rendering.
        let node = if (adjustment) img.adjustment_layer(options) else (
            let pixels = img.blank({width:1,height:1})^,
            img.with_children(img.layer(pixels),[pixels,effect])^)
        let probe = img.document(img.stack([node]),{width:1,height:1})
        let valid = img.validate(probe)^;
        effect
    }
}
pub fn run_command(st,command) any^ {
    let row = model.active(st)^
    let items = items(row.node)
    let index = model.value(command.index,0)
    if (model.locked(st.doc,[row.path])) raise error("Raster: target layer is locked")
    else if (command.id != "effect.add" and (not (index is int) or index<0 or index>=len(items)))
        raise error("Raster: select an effect")
    else {
        let changed = if (command.id == "effect.add") [*items,checked(command.options,model.value(command.name,"Adjustment"),model.adjustment(row.node))^]
        else if (command.id == "effect.remove") sequence.list_splice(items,index,1,[])
        else if (command.id == "effect.toggle") sequence.list_set(items,index,img.with_properties(items[index],{enabled:items[index].enabled == false})^)
        else if (command.id == "effect.update") (
            let replacement = checked(command.options,model.value(command.name,items[index].name),model.adjustment(row.node))^,
            sequence.list_set(items,index,img.with_properties(replacement,{enabled:items[index].enabled != false})^))
        else if (command.id == "effect.reorder") (
            if (not (command.to is int) or command.to<0 or command.to>=len(items)) raise error("Raster: effect order is out of bounds")
            else sequence.list_splice(sequence.list_splice(items,index,1,[]),command.to,0,[items[index]]))
        else raise error("Raster: unknown effect command")
        let node = img.with_children(row.node,[*[for (child in content(row.node) where name(child) != 'effect') child],*changed])^;
        {*:st,doc:model.replace(st.doc,row.id,node)^}
    }
}
