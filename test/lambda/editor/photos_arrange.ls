import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
let raster = img.blank({width:2,height:2,color:[1,0,0,1]})^
let initial = editor.open_session(editor.new_document({width:20,height:16})^ )^
let a = editor.exec(initial,{id:"layer.insert",raster:img.blank({width:4,height:2})^,position:[2,3]})^
let aid = a.selected[0]
let b = editor.exec(a,{id:"layer.insert",raster:raster,position:[9,8]})^
let bid = b.selected[0]
let c = editor.exec(b,{id:"layer.insert",raster:raster,position:[15,4]})^
let cid = c.selected[0]
let left = editor.exec(c,{id:"layer.align",mode:"left",relative:"canvas"})^
model.position(model.lookup(left.doc,cid).node) == [0,4]
model.position(model.lookup(c.doc,cid).node) == [15,4]
len(left.hist.undo) == len(c.hist.undo)+1
editor.exec(left,{id:"history.undo"})^.doc == c.doc
editor.exec(editor.exec(left,{id:"history.undo"})^,{id:"history.redo"})^.doc == left.doc
let bottom = editor.exec(c,{id:"layer.align",mode:"bottom",relative:"canvas"})^
model.position(model.lookup(bottom.doc,cid).node) == [15,14]
let selected = editor.exec(c,{id:"layer.select",ids:[aid,bid,cid]})^
let joined = editor.exec(selected,{id:"layer.align",mode:"left"})^
all([for (id in [aid,bid,cid]) model.position(model.lookup(joined.doc,id).node)[0] == 2])
let centered = editor.exec(selected,{id:"layer.align",mode:"center",relative:"canvas"})^
; [for (id in [aid,bid,cid]) model.position(model.lookup(centered.doc,id).node)[0]] == [8,9,9]
let distributed = editor.exec(selected,{id:"layer.distribute",axis:"x",relative:"canvas"})^
; [for (id in [aid,bid,cid]) model.position(model.lookup(distributed.doc,id).node)[0]] == [0,10,18]
len(distributed.hist.undo) == len(c.hist.undo)+1
editor.exec(joined,{id:"layer.align",mode:"left"})^.hist == joined.hist
let grouped = editor.exec(editor.exec(c,{id:"layer.select",ids:[aid,bid]})^,{id:"layer.group"})^
let overlap = editor.exec(grouped,{id:"layer.select",ids:[grouped.selected[0],aid]})^
let once = editor.exec(overlap,{id:"layer.align",mode:"left",relative:"canvas"})^
model.position(model.lookup(once.doc,aid).node) == [0,3] and model.position(model.lookup(once.doc,bid).node) == [7,8]
let locked = editor.exec(c,{id:"layer.lock"})^
editor.exec(locked,{id:"layer.align",mode:"left"}) ^ { true } ~ { false }
editor.exec(c,{id:"layer.distribute",axis:"x"}) ^ { true } ~ { false }
editor.exec(selected,{id:"layer.align",mode:"invalid"}) ^ { true } ~ { false }
editor.exec(selected,{id:"layer.distribute",axis:"z"}) ^ { true } ~ { false }
editor.exec(selected,{id:"layer.align",mode:"top",relative:"other"}) ^ { true } ~ { false }
let hidden = editor.exec(c,{id:"layer.visibility"})^
let moved_hidden = editor.exec(hidden,{id:"layer.align",mode:"right",relative:"canvas"})^
model.position(model.lookup(moved_hidden.doc,cid).node) == [18,4] and model.lookup(moved_hidden.doc,cid).node.visible == false
