// S9.2.2: selections are finite document-space coverage snapshots, independent of dirty pixels.
import img: lambda.image
import model: .model

pub fn empty(st) any^ => img.blank({width:st.doc.width,height:st.doc.height,channels:1})^
pub fn combine(st,coverage,mode="replace") any^ {
    if (mode == "replace") {*:st,selection:coverage}
    else {*:st,selection:img.combine_coverage(if (st.selection == null) empty(st)^ else st.selection,coverage,mode)^}
}
pub fn run_command(st,command) any^ {
    let id = command.id;
    if (id == "selection.clear") {*:st,selection:null}
    else if (id == "selection.all") {*:st,selection:img.selection(st.doc.width,st.doc.height,{shape:"all"})^}
    else if (id == "selection.invert") {*:st,selection:img.invert_coverage(if (st.selection == null) empty(st)^ else st.selection)^}
    else if (id == "selection.feather") (
        if (st.selection == null) raise error("Raster: create a selection to feather")
        else {*:st,selection:img.feather_coverage(st.selection,command.radius)^})
    else if (id == "selection.shape") {
        let coverage = img.selection(st.doc.width,st.doc.height,command.options)^;
        combine(st,coverage,model.value(command.mode,"replace"))^
    }
    else if (id == "selection.wand") {
        let source = img.render_frame(st.doc)^
        let coverage = img.wand(source,command.point,model.value(command.options,{}))^;
        combine(st,coverage,model.value(command.mode,"replace"))^
    }
    else if (id == "mask.from_selection") {
        let target = model.source_target({*:st,target:"pixels"})^
        if (st.selection == null) raise error("Raster: create a selection for the layer mask")
        else {
            let p = model.position(target.row.node)
            let mask = img.mask(st.selection,{position:[-p[0],-p[1]],default:0.0,enabled:true,invert:false})^
            let changed = img.with_children(target.row.node,[*[for (child in content(target.row.node) where name(child) != 'mask') child],mask])^;
            {*:st,doc:model.replace(st.doc,target.row.id,changed)^,target:"mask",selection:null}
        }
    }
    else if (id == "selection.from_mask" or id == "mask.invert" or id == "mask.apply") {
        let target = model.source_target({*:st,target:"pixels"},id != "selection.from_mask")^
        let mask = model.mask(target.row.node)
        if (mask == null) raise error("Raster: layer has no mask")
        else if (id == "selection.from_mask") {
            let origin = [target.position[0]+model.position(mask)[0],target.position[1]+model.position(mask)[1]]
            let coverage = img.reframe(mask[0],[-origin[0],-origin[1],st.doc.width-origin[0],st.doc.height-origin[1]],
                {fill:[model.value(mask.default,1.0)]})^;
            {*:st,selection:if (mask.invert == true) img.invert_coverage(coverage)^ else coverage}
        }
        else if (id == "mask.invert") {
            let inverted = img.with_children(img.with_properties(mask,{invert:false,
                default:if (mask.invert == true) model.value(mask.default,1.0) else 1.0-model.value(mask.default,1.0)})^,
                [if (mask.invert == true) mask[0] else img.invert_coverage(mask[0])^])^;
            {*:st,doc:model.replace(st.doc,target.row.id,img.with_children(target.row.node,
                [for (child in content(target.row.node)) if (name(child) == 'mask') inverted else child])^)^}
        }
        else if (name(target.raster) != 'raster') raise error("Raster: rasterize artwork before applying its mask to source pixels")
        else {
            // apply only alpha; retained effects must not be evaluated a second time afterward.
            let unfiltered = img.with_children(target.row.node,[for (child in content(target.row.node) where not (name(child) in ['effect','style'])) child])^
            let source = img.with_properties(unfiltered,{position:[0,0],opacity:1.0,visible:true,blend:'normal',clipping:false})^
            let pixels = img.render_frame(img.document(img.stack([source]),{width:target.raster.width,height:target.raster.height}))^
            let node = img.with_children(target.row.node,[for (child in content(target.row.node) where name(child) != 'mask')
                if (name(child) == 'raster') pixels else child])^;
            {*:st,doc:model.replace(st.doc,target.row.id,node)^,target:"pixels"}
        }
    }
    else raise error("Raster: unknown selection command")
}
