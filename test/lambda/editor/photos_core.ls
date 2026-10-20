import editor: lambda.editor.raster
import img: lambda.image
import model: lambda.editor.raster.model
fn check(label,condition) => condition
let blank = editor.open_session(editor.new_document({width:4,height:3})^)^
check("blank",editor.layers(blank.doc) == [] and not editor.dirty(blank))
let red = img.blank({width:4,height:3,color:[1.0,0.0,0.0,1.0]})^
let blue = img.blank({width:2,height:2,color:[0.0,0.0,1.0,1.0]})^
let a = editor.exec(blank,{id:"layer.insert",raster:red,name:"Background"})^
let background = a.selected[0]
let b = editor.exec(a,{id:"layer.insert",raster:blue,name:"Subject",position:[1,0]})^
let subject = b.selected[0]
check("order",[for (row in editor.layers(b.doc)) row.node.name] == ["Subject","Background"])
check("pixels",img.render_frame(b.doc)^.b[0,1] == 255 and img.render_frame(b.doc)^.r[0,0] == 255)
check("history",len(b.hist.undo) == 2 and editor.dirty(b))
let moved = editor.exec(b,{id:"layer.move",dx:-1,dy:1})^
check("negative-offset",model.position(editor.lookup(moved.doc,subject).node) == [0,1])
check("snapshot",model.position(editor.lookup(b.doc,subject).node) == [1,0])
let undone = editor.exec(moved,{id:"history.undo"})^
check("undo",undone.doc == b.doc and undone.selected == [subject])
check("redo",editor.exec(undone,{id:"history.redo"})^.doc == moved.doc)
let renamed = editor.exec(b,{id:"layer.rename",name:"Sky"})^
check("rename",editor.lookup(renamed.doc,subject).node.name == "Sky")
let hidden = editor.exec(b,{id:"layer.visibility"})^
check("hide",img.render_frame(hidden.doc)^.r[0,1] == 255)
let alpha = editor.exec(b,{id:"layer.opacity",opacity:0.5})^
check("opacity",img.render_frame(alpha.doc)^.r[0,1] > 180 and img.render_frame(alpha.doc)^.b[0,1] > 180)
let masked = editor.exec(b,{id:"mask.add",coverage:0.0})^
check("mask",img.render_frame(masked.doc)^.r[0,1] == 255)
let target = editor.exec(masked,{id:"layer.select",ids:[subject],target:"mask"})^
let filled = editor.exec(target,{id:"pixels.fill",color:[1.0]})^
check("mask-paint",img.render_frame(filled.doc)^.b[0,1] == 255)
check("mask-snapshot",img.channel(model.mask(editor.lookup(masked.doc,subject).node)[0],"gray")^[0,0] == 0)
check("remove-mask",editor.exec(filled,{id:"mask.remove"})^.target == "pixels")
let selected = editor.exec(b,{id:"layer.select",ids:[subject,background]})^
let grouped = editor.exec(selected,{id:"layer.group",name:"Composite"})^
let group = grouped.selected[0]
check("group",len(editor.layers(grouped.doc)) == 3 and name(editor.layers(grouped.doc)[0].node) == 'stack')
check("group-appearance",img.render_frame(grouped.doc)^ == img.render_frame(b.doc)^)
let together = editor.exec(grouped,{id:"layer.move",dx:2,dy:-1})^
check("group-coordinates",model.position(editor.lookup(together.doc,subject).node) == [3,-1] and
    model.position(editor.lookup(together.doc,background).node) == [2,-1])
let ungrouped = editor.exec(grouped,{id:"layer.ungroup"})^
check("ungroup",ungrouped.doc == b.doc and ungrouped.selected == [subject,background])
let copy = editor.exec(grouped,{id:"layer.duplicate"})^
check("duplicate-ids",len(editor.layers(copy.doc)) == 6 and len(unique([for (row in editor.layers(copy.doc)) row.id])) == 6)
let overlap = editor.exec(grouped,{id:"layer.select",ids:[group,subject]})^
let once = editor.exec(overlap,{id:"layer.move",dx:1,dy:0})^
check("move-once",model.position(editor.lookup(once.doc,subject).node) == [2,0])
check("delete-descendants",editor.layers(editor.exec(overlap,{id:"layer.delete"})^.doc) == [])
let lower = editor.exec(b,{id:"layer.reorder",index:1})^
check("reorder",img.render_frame(lower.doc)^.r[0,1] == 255)
let saved = editor.mark_saved(b,b.doc)
check("saved",not editor.dirty(saved))
check("saved-undo",editor.dirty(editor.exec(saved,{id:"history.undo"})^))
check("saved-redo",not editor.dirty(editor.exec(editor.exec(saved,{id:"history.undo"})^,{id:"history.redo"})^))
let flattened = editor.exec(b,{id:"document.flatten"})^
check("flatten",len(editor.layers(flattened.doc)) == 1 and img.render_frame(flattened.doc)^ == img.render_frame(b.doc)^)
let locked = editor.exec(b,{id:"layer.lock"})^
check("locked",editor.exec(locked,{id:"layer.move",dx:1,dy:0}) ^ { true } ~ { false })
check("invalid-opacity",editor.exec(b,{id:"layer.opacity",opacity:2}) ^ { true } ~ { false })
check("missing-target",editor.exec(b,{id:"layer.select",ids:["missing"]}) ^ { true } ~ { false })
check("invalid-size",editor.new_document({width:0,height:4}) ^ { true } ~ { false })
check("unknown-command",editor.exec(b,{id:"unknown"}) ^ { true } ~ { false })
let roundtrip = editor.open_session(img.decode(img.encode(b.doc,{format:'psd'})^)^)^
check("psd-hierarchy",[for (row in editor.layers(roundtrip.doc)) row.node.name] == ["Subject","Background"])
check("psd-pixels",img.render_frame(roundtrip.doc)^ == img.render_frame(b.doc)^)
let merged = editor.exec(selected,{id:"layer.merge"})^
check("merge",len(editor.layers(merged.doc)) == 1 and img.render_frame(merged.doc)^ == img.render_frame(b.doc)^)
let resized = editor.exec(masked,{id:"document.resize_image",width:8,height:6})^
let large = editor.lookup(resized.doc,subject).node
check("resize",resized.doc.width == 8 and resized.doc.height == 6 and img.raster_of(large).width == 4 and
    model.mask(large)[0].width == 4 and model.position(large) == [2,0])
let canvas = editor.exec(b,{id:"document.resize_canvas",width:8,height:6})^
check("canvas",img.raster_of(editor.lookup(canvas.doc,subject).node).width == 2 and model.position(editor.lookup(canvas.doc,subject).node) == [1,0])
let locked_group = editor.exec(grouped,{id:"layer.lock"})^
check("locked-destination",editor.exec(locked_group,{id:"layer.insert",parent:group,raster:blue}) ^ { true } ~ { false })
let locked_child = editor.exec(locked_group,{id:"layer.select",ids:[subject]})^
check("locked-ancestor",editor.exec(locked_child,{id:"layer.move",dx:1,dy:0}) ^ { true } ~ { false })
check("invalid-lock",editor.exec(b,{id:"layer.lock",locked:"yes"}) ^ { true } ~ { false })
check("invalid-visibility",editor.exec(b,{id:"layer.visibility",visible:"yes"}) ^ { true } ~ { false })
