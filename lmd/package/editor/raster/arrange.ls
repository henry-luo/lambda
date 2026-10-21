// S9.2.2: selected roots move once through the same checked snapshot history as other edits.
import img: lambda.image
import model: .model
import alignment: lambda.editor.mod_alignment

pub fn run_command(st,command) any^ {
    let rows = model.unlocked_roots(st)^
    let relative = model.value(command.relative,"selection")
    let distributing = command.id == "layer.distribute"
    let mode = if (distributing) "distribute-" ++ string(command.axis) else command.mode
    if (not (relative in ["selection","canvas"]) or not (mode in alignment.modes) or
        (not distributing and starts_with(mode,"distribute-"))) raise error("Raster: invalid layer arrangement")
    else if (distributing and len(rows)<3) raise error("Raster: select at least three layer roots to distribute")
    else {
        let boxes = [for (row in rows) {*:model.bounds(row.node),id:row.id}]
        let reference = if (relative == "canvas") {x:0,y:0,width:st.doc.width,height:st.doc.height}
            else model.bounds(img.stack([for (row in rows) row.node]))
        let offsets = alignment.offsets(boxes,reference,mode)
        let horizontal = alignment.horizontal(mode);
        // raster placement follows the existing integral-pixel movement contract.
        {*:st,doc:reduce([st.doc,*rows],(image,row) => (
            let delta = int(round(offsets[row.id])),
            if (delta == 0) image else model.replace(image,row.id,
                model.translated(row.node,if (horizontal) delta else 0,if (horizontal) 0 else delta)^)^))}
    }
}
