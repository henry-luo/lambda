import img: lambda.image
import editor: lambda.editor.raster
import navigation: lambda.editor.raster.navigation
import guides: lambda.editor.raster.guides
import interaction: lambda.editor.raster.interaction
import render: lambda.editor.raster.render
fn check(value) => value
let st = {*:editor.open_session(editor.new_document({width:1000,height:500})^)^,zoom:1.0}
let bounds = {width:200,height:100}
check(navigation.camera(st,bounds) == {scale:1.0,width:1000.0,height:500.0,x:-400.0,y:-200.0})
check(navigation.visible(st,bounds) == {x:400.0,y:200.0,width:200.0,height:100.0})
let centered = navigation.center(st,bounds,{x:800,y:400})
check(centered.pan == {x:-300.0,y:-150.0})
check(navigation.visible(centered,bounds) == {x:700.0,y:350.0,width:200.0,height:100.0})
check(navigation.center(st,bounds,{x:2000,y:-1}).pan == {x:-500.0,y:250.0})
check(navigation.visible({*:st,pan:{x:10000,y:10000}},bounds).width == 0)
check(navigation.visible({*:st,pan:{x:-10000,y:-10000}},bounds).height == 0)
check(navigation.visible({*:st,zoom:0.1},bounds) == {x:0,y:0,width:1000,height:500})
let fit = navigation.camera({*:st,zoom:0},{width:600,height:400})
check(abs(fit.scale-0.52)<0.00001 and abs(fit.x-40)<0.00001 and abs(fit.y-70)<0.00001)
let thumb = navigation.thumbnail(st.doc,{width:256,height:132})
check(thumb == {x:0.0,y:2.0,width:256,height:128})
check(navigation.thumbnail({width:200,height:800},{width:256,height:132}) == {x:111.5,y:0.0,width:33,height:132})
check(navigation.navigator_point(st.doc,thumb,{x:128,y:66}) == {x:500.0,y:250.0})
check(navigation.navigator_point(st.doc,thumb,{x:-20,y:200}) == {x:0,y:500})
check(navigation.positions(1,2,8) == [])
check(navigation.positions(8,24,8) == [8,16,24])
check(navigation.tick_step(1) == 100 and navigation.tick_step(2) == 50 and navigation.tick_step(8) == 10)
check(navigation.ticks(30,200,2) == [{position:30,value:0},{position:130,value:50}])
check(len(navigation.ticks(-400000,1000,8))<=20)
let configured = editor.exec(st,{id:"view.configure",rulers:true,grid:true,spacing:32,subdivisions:4,snap:true})^
check(configured.doc == st.doc and configured.hist == st.hist and not editor.dirty(configured))
check(configured.rulers_visible and configured.grid_visible and configured.snap_grid)
check(editor.exec(configured,{id:"view.configure"})^ == configured)
check(all([for (command in [{rulers:1},{grid:"yes"},{snap:0},{spacing:0},{spacing:65537},{spacing:1.5},{subdivisions:0},{subdivisions:17},{subdivisions:2.5},{unknown:true}])
    editor.exec(st,{id:"view.configure",*:command}) ^ { true } ~ { false }]))
let lines = navigation.grid_lines(configured,bounds)
check(len(lines) == 39 and all([for (line in lines) line.value%8 == 0]))
check(all([for (line in lines where line.major) line.value%32 == 0]))
check(navigation.grid_lines({*:configured,grid_visible:false},bounds) == [])
check(navigation.grid_lines({*:configured,pan:{x:10000,y:10000}},bounds) == [])
check(all([for (line in navigation.grid_lines({*:configured,zoom:0.5},bounds)) line.major]))
check(navigation.grid_lines({*:configured,zoom:0.05},bounds) == [])
check(len(navigation.grid_lines({*:configured,grid_spacing:1,grid_subdivisions:16,zoom:8},bounds))<60)
let snapping = editor.exec(configured,{id:"guide.configure",snap:true,layers:false})^
let box = {x:10,y:10,width:10,height:8}
check(guides.snap_move(snapping,box,47,0).dx == 46)
check(guides.snap_move({*:snapping,grid_visible:false},box,47,0).dx == 46)
check(guides.snap_move({*:snapping,snap_grid:false},box,47,0).dx == 47)
check(guides.snap_move(snapping,box,47,0,8).dx == 47)
check(guides.snap_move(snapping,box,47,0,1,true).dx == 47)
check(guides.snap_move({*:snapping,snap_enabled:false},box,47,0).dx == 47)
check(all([for (value in navigation.grid_targets(snapping,[0,3,10],10000,1000)) value<=1000]))
fn pointer(x,y,extra={}) => {point:{x:x,y:y},screen:{x:x,y:y},pointer:1,*:extra}
let empty_brush = editor.exec(st,{id:"tool.configure",tool:"brush"})^
let ruler = interaction.begin(empty_brush,pointer(100,-40,{ruler:"y"}))^
check(ruler.gesture.mode == "guide-new" and ruler.doc == st.doc)
let draft = interaction.preview(ruler,pointer(100,120))^
check(guides.items(draft.doc)[0].axis == "y" and guides.items(draft.doc)[0].position == 120)
check(draft.hist == st.hist and interaction.cancel(draft).doc == st.doc)
let finished = interaction.finish(draft)^
check(len(finished.hist.undo) == 1 and finished.gesture == null)
check(editor.exec(finished,{id:"history.undo"})^.doc == st.doc)
check(interaction.finish(interaction.preview(draft,pointer(100,600))^)^.hist == st.hist)
let vertical = interaction.finish(interaction.preview(interaction.begin(empty_brush,pointer(-50,50,{ruler:"x"}))^,pointer(44.6,150))^)^
check(guides.items(vertical.doc)[0].axis == "x" and guides.items(vertical.doc)[0].position == 45)
let duplicate = interaction.finish(interaction.preview(interaction.begin(finished,pointer(100,-40,{ruler:"y"}))^,pointer(100,120))^)^
check(duplicate.hist == finished.hist)
check(render.visual(finished.doc)^ == render.visual(st.doc)^)
let nav = interaction.begin(empty_brush,pointer(0,0,{navigator:{x:800,y:400},viewport:bounds}))^
check(nav.pan == centered.pan and nav.gesture.mode == "navigator" and nav.doc == st.doc)
let moved = interaction.preview(nav,pointer(1,1,{navigator:{x:1000,y:500},viewport:bounds}))^
check(moved.pan == {x:-500.0,y:-250.0})
check(interaction.cancel(moved).pan == st.pan)
let panned = interaction.finish(moved)^
check(panned.hist == st.hist and not editor.dirty(panned) and panned.gesture == null)
check(panned.selected == st.selected and panned.tool == "brush")
check(navigation.visible(panned,bounds) == {x:900.0,y:450.0,width:100.0,height:50.0})
