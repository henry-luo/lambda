import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import guides: lambda.editor.raster.guides
import interaction: lambda.editor.raster.interaction
import render: lambda.editor.raster.render
import project: lambda.image.project
fn check(value) => value
let raster = img.blank({width:8,height:4,color:[1,0,0,1]})^
let st = editor.exec(editor.open_session(editor.new_document({width:100,height:80})^)^,
    {id:"layer.insert",raster:raster,position:[10,10]})^
let one = editor.exec(st,{id:"guide.add",axis:"x",position:40})^
let both = editor.exec(one,{id:"guide.add",axis:"y",position:33})^
check(len(guides.items(both.doc)) == 2 and guides.items(both.doc)[0].position == 40)
check(model.lookup(both.doc,st.selected[0]).node == model.lookup(st.doc,st.selected[0]).node)
check(len(both.hist.undo) == len(st.hist.undo)+2)
check(editor.exec(both,{id:"guide.add",axis:"y",position:33})^.hist == both.hist)
let saved = editor.mark_saved(both,both.doc)
let configured = editor.exec(saved,{id:"guide.configure",snap:true,visible:false})^
check(not editor.dirty(configured) and configured.hist == saved.hist and configured.snap_enabled and not configured.guides_visible)
check(guides.items(editor.open_session(project.decode(project.encode(both.doc)^)^)^.doc) == guides.items(both.doc))
check(editor.exec(st,{id:"guide.add",axis:"z",position:1}) ^ { true } ~ { false })
check(editor.exec(st,{id:"guide.add",axis:"x",position:100.5}) ^ { true } ~ { false })
check(editor.exec(st,{id:"guide.add",axis:"y",position:81}) ^ { true } ~ { false })
check(editor.exec(st,{id:"guide.configure",snap:1}) ^ { true } ~ { false })
let moved = editor.exec(both,{id:"guide.update",guide:"guide-1",position:45})^
check(guides.lookup(moved.doc,"guide-1").position == 45)
check(editor.exec(moved,{id:"history.undo"})^.doc == both.doc)
check(editor.exec(both,{id:"guide.update",guide:"guide-1",position:-1}) ^ { true } ~ { false })
check(editor.exec(both,{id:"guide.remove",guide:"missing"}) ^ { true } ~ { false })
check(len(guides.items(editor.exec(both,{id:"guide.remove",guide:"guide-1"})^.doc)) == 1)
check(len(guides.items(editor.exec(both,{id:"guide.clear"})^.doc)) == 0)
let scaled = editor.exec(both,{id:"document.resize_image",width:50,height:40})^
check([for (g in guides.items(scaled.doc)) g.position] == [20,17])
let clipped = editor.exec(both,{id:"document.resize_canvas",width:30,height:40})^
check([for (g in guides.items(clipped.doc)) g.position] == [33])
let cropped = editor.exec(both,{id:"document.crop",rect:[20,10,80,60]})^
check([for (g in guides.items(cropped.doc)) g.position] == [20,23])
check(editor.exec(cropped,{id:"history.undo"})^.doc == both.doc)
let enabled = editor.exec(both,{id:"guide.configure",snap:true})^
let box = model.selection_bounds(enabled)
check(guides.snap_move(enabled,box,27,0) == {dx:26,dy:0,x:40,y:null})
check(guides.snap_move(enabled,box,27,0,2).dx == 26)
check(guides.snap_move(enabled,box,27,0,8) == {dx:27,dy:0,x:null,y:null})
check(guides.snap_move(enabled,box,27,0,1,true) == {dx:27,dy:0,x:null,y:null})
check(guides.snap_move(configured,box,27,0).x == 50)
fn pointer(x,y,guide=null,alt=false) => {point:{x:x,y:y},screen:{x:x,y:y},pointer:1,guide:guide,alt:alt}
let draft = interaction.preview(interaction.begin(enabled,pointer(12,12))^,pointer(39,12))^
check(model.selection_bounds(draft).x == 36 and draft.gesture.snapped.x == 40)
check(interaction.cancel(draft).doc == both.doc)
check(len(interaction.finish(draft)^.hist.undo) == len(both.hist.undo)+1)
let bypass = interaction.preview(interaction.begin(enabled,pointer(12,12))^,pointer(39,12,null,true))^
check(model.selection_bounds(bypass).x == 37)
let near_edge = editor.exec(editor.exec(st,{id:"layer.move",dx:-8,dy:-8})^,{id:"guide.configure",snap:true})^
check(interaction.finish(interaction.begin(near_edge,pointer(3,3))^)^.doc == near_edge.doc)
let away = interaction.preview(interaction.begin(near_edge,pointer(3,3))^,pointer(20,20))^
check(interaction.finish(interaction.preview(away,pointer(3,3))^)^.hist == near_edge.hist)
let guide_drag = interaction.preview(interaction.begin(both,pointer(41,2,"guide-1"))^,pointer(51,2))^
check(guide_drag.gesture.mode == "guide" and guides.lookup(guide_drag.doc,"guide-1").position == 50)
check(guide_drag.hist == both.hist and interaction.cancel(guide_drag).doc == both.doc)
check(len(interaction.finish(guide_drag)^.hist.undo) == len(both.hist.undo)+1)
check(guides.lookup(interaction.preview(guide_drag,pointer(1000,2))^.doc,"guide-1").position == 100)
let cached = {*:guide_drag,painted_doc:both.doc,painted_raster:img.render_frame(both.doc)^}
check(not render.frame(cached)^.changed and render.frame(cached)^.raster == cached.painted_raster)
let after = editor.exec(both,{id:"pixels.fill",color:[0,0,1,1]})^
check(render.frame({*:after,painted_doc:both.doc,painted_raster:cached.painted_raster})^.changed)
let duplicate = img.with_children(st.doc,[*content(st.doc),<metadata role:'photos_guides',<guide id:"g",axis:"x",position:2> <guide id:"g",axis:"x",position:3>>])^
check(editor.open_session(duplicate) ^ { true } ~ { false })
