// S9.1.3: styles evaluate after adjustments and source masks, before the layer affine.
import img: lambda.image
import model: .model
import sequence: lambda.editor.mod_doc
pub fn items(node) => if (node == null) [] else [for (child in content(node) where name(child) == 'style') child]
fn checked(options) any^ {
    let config = img.checked_options(options,["kind","color","radius","dx","dy","opacity","outset"])^
    let radius = model.value(config.radius,0)
    let dx = model.value(config.dx,0)
    let dy = model.value(config.dy,0)
    let node = <style enabled:true,*:config,outset:model.value(config.outset,if (config.kind == "overlay") 0 else int(ceil(radius+max(abs(dx),abs(dy)))))>
    let probe = img.document(img.stack([img.with_children(img.layer(img.blank({width:1,height:1})^),
        [img.blank({width:1,height:1})^,node])^]),{width:1,height:1})
    let valid = img.validate(probe)^;
    node
}
pub fn run_command(st,command) any^ {
    let row = model.active(st)^
    let entries = items(row.node)
    let index = model.value(command.index,0)
    if (name(row.node) != 'layer' or model.adjustment(row.node) or model.locked(st.doc,[row.path]))
        raise error("Raster: select an unlocked content layer for styles")
    else if (command.id != "style.add" and (not (index is int) or index<0 or index>=len(entries))) raise error("Raster: select a layer style")
    else {
        let changed = if (command.id == "style.add") [*entries,checked(command.options)^]
        else if (command.id == "style.remove") sequence.list_splice(entries,index,1,[])
        else if (command.id == "style.toggle") sequence.list_set(entries,index,img.with_properties(entries[index],{enabled:entries[index].enabled == false})^)
        else if (command.id == "style.update") (
            let replacement = checked(command.options)^,
            sequence.list_set(entries,index,img.with_properties(replacement,{enabled:entries[index].enabled != false})^))
        else raise error("Raster: unknown style command")
        let node = img.with_children(row.node,[*[for (child in content(row.node) where name(child) != 'style') child],*changed])^;
        {*:st,doc:model.replace(st.doc,row.id,node)^}
    }
}
