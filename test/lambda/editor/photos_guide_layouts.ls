import editor: lambda.editor.raster
import guides: lambda.editor.raster.guides
import registry: lambda.editor.raster.commands
import interaction: lambda.editor.raster.interaction
import model: lambda.editor.raster.model
import img: lambda.image
import project: lambda.image.project
fn check(value) => value
let st = editor.open_session(editor.new_document({width:120,height:90})^)^
let thirds = [{axis:"x",position:40},{axis:"x",position:80},{axis:"y",position:30},{axis:"y",position:60}]
check(guides.layout(st.doc,{})^ == thirds)
check(guides.layout(st.doc,{preset:"center"})^ == [{axis:"x",position:60},{axis:"y",position:45}])
check(len(guides.layout(st.doc,{preset:"quarters"})^) == 6)
check([for (p in guides.layout(st.doc,{preset:"custom",columns:3,rows:0,margin:10,gutter:8})^) p.position] == [10,38,46,74,82,110])
check([for (p in guides.layout(st.doc,{preset:"custom",columns:2,rows:2,margin:10,gutter:6})^) p.position] == [10,57,63,110,10,42,48,80])
check(guides.layout(st.doc,{preset:"custom",columns:1,rows:0})^ == [])
check([for (p in guides.layout({width:101,height:79},{preset:"thirds"})^) p.position] == [34,67,26,53])
check(all([for (bad in [{preset:"bad"},{preset:"center",columns:3},{preset:"custom",columns:-1},{preset:"custom",rows:1.5},
    {preset:"custom",columns:0,rows:0},{preset:"custom",margin:-1},{preset:"custom",gutter:1.5},{preset:"custom",margin:50},
    {preset:"custom",columns:3,rows:0,gutter:60},{preset:"custom",columns:66}])
    editor.exec(st,{id:"guide.layout",*:bad}) ^ { true } ~ { false }]))
let one = editor.exec(st,{id:"guide.add",axis:"x",position:40})^
let applied = editor.exec(one,{id:"guide.layout",preset:"thirds"})^
check([for (g in guides.items(applied.doc)) {axis:g.axis,position:g.position}] == thirds)
check(guides.lookup(applied.doc,"guide-1").position == 40)
check(len(applied.hist.undo) == len(one.hist.undo)+1)
check(editor.exec(applied,{id:"guide.layout",preset:"thirds"})^.hist == applied.hist)
check(editor.exec(applied,{id:"history.undo"})^.doc == one.doc)
check(editor.exec(editor.exec(applied,{id:"history.undo"})^,{id:"history.redo"})^.doc == applied.doc)
let appended = editor.exec(applied,{id:"guide.layout",preset:"center",replace:false})^
check(len(guides.items(appended.doc)) == 6)
check(guides.lookup(appended.doc,"guide-1") == guides.lookup(applied.doc,"guide-1"))
check(editor.exec(appended,{id:"guide.layout",preset:"center",replace:false})^.hist == appended.hist)
let replaced = editor.exec(appended,{id:"guide.layout",preset:"center"})^
check(len(guides.items(replaced.doc)) == 2 and guides.items(replaced.doc)[0].id == "guide-5")
check(editor.exec(st,{id:"guide.layout",preset:"custom",columns:1,rows:0})^.hist == st.hist)
check(editor.exec(st,{id:"guide.layout",replace:1}) ^ { true } ~ { false })
check(editor.exec(st,{id:"guide.layout",unknown:1}) ^ { true } ~ { false })
let reopened = editor.open_session(project.decode(project.encode(appended.doc)^)^)^
check(guides.items(reopened.doc) == guides.items(appended.doc))
let saved = editor.mark_saved(applied,applied.doc)
let locked = editor.exec(saved,{id:"guide.configure",locked:true,snap:true})^
check(locked.guides_locked and locked.hist == saved.hist and not editor.dirty(locked))
check(all([for (cmd in [{id:"guide.add",axis:"x",position:10},{id:"guide.update",guide:"guide-1",position:45},
    {id:"guide.remove",guide:"guide-1"},{id:"guide.clear"},{id:"guide.layout",preset:"center"}])
    editor.exec(locked,cmd) ^ { true } ~ { false }]))
check(not registry.command_state(locked,"guide.layout")^.enabled and registry.command_state(locked,"guide.configure")^.enabled)
check(editor.exec(locked,{id:"guide.configure",locked:1}) ^ { true } ~ { false })
check(guides.snap_move(locked,{x:10,y:10,width:8,height:4},27,0).x == 40)
fn pointer(x,y,extra={}) => {point:{x:x,y:y},screen:{x:x,y:y},pointer:1,*:extra}
check(interaction.begin(locked,pointer(-1,20,{ruler:"x"}))^ == locked)
let unlocked = editor.exec(locked,{id:"guide.configure",locked:false})^
check(not unlocked.guides_locked and unlocked.doc == locked.doc and unlocked.hist == locked.hist)
check(len(guides.items(editor.exec(unlocked,{id:"guide.clear"})^.doc)) == 0)
let resized = editor.exec(locked,{id:"document.resize_image",width:60,height:45})^
check(resized.guides_locked and [for (g in guides.items(resized.doc)) g.position] == [20,40,15,30])
check(editor.exec(resized,{id:"history.undo"})^.guides_locked)
let painted = editor.exec(locked,{id:"layer.insert",raster:img.blank({width:8,height:4,color:[1,0,0,1]})^,position:[38,10]})^
let draft = interaction.preview(interaction.begin(painted,pointer(40,12,{guide:"guide-1"}))^,pointer(50,12))^
check(draft.gesture.mode == "move" and model.selection_bounds(draft).x != 38)
check(guides.items(draft.doc) == guides.items(painted.doc))
let large = editor.open_session(editor.new_document({width:1000,height:90})^)^
let full = editor.exec(large,{id:"guide.layout",preset:"custom",columns:65,rows:0})^
check(len(guides.items(full.doc)) == 64 and len(full.hist.undo) == 1)
check(editor.exec(full,{id:"guide.layout",preset:"center",replace:false}) ^ { true } ~ { false })
check(editor.exec(large,{id:"guide.layout",preset:"custom",columns:65,rows:1,margin:1}) ^ { true } ~ { false })
check(editor.exec(full,{id:"history.undo"})^.doc == large.doc)
// metadata editing must preserve either authored block order, including during same-size resize.
let coverage = editor.exec(st,{id:"selection.all"})^
let channel_first = editor.exec(editor.exec(coverage,{id:"channel.save",name:"Coverage"})^,{id:"guide.add",axis:"x",position:40})^
let guide_first = editor.exec(editor.exec(coverage,{id:"guide.add",axis:"x",position:40})^,{id:"channel.save",name:"Coverage"})^
fn block_order(image) => [for (child in content(image)) {kind:name(child),role:child.role}]
check(all([for (snapshot in [channel_first,guide_first])
    editor.exec(snapshot,{id:"document.resize_canvas",width:120,height:90})^.hist == snapshot.hist]))
check(all([for (snapshot in [channel_first,guide_first]) (
    let renamed = editor.exec(snapshot,{id:"channel.rename",channel:"channel-1",name:"Renamed"})^,
    let moved = editor.exec(renamed,{id:"guide.update",guide:"guide-1",position:41})^,
    block_order(moved.doc) == block_order(snapshot.doc))]))
let coincident = editor.exec(editor.exec(applied,{id:"guide.add",axis:"x",position:41})^,
    {id:"guide.update",guide:"guide-5",position:40})^
check(editor.exec(coincident,{id:"guide.layout",preset:"thirds"})^.doc == applied.doc)
check(len(guides.items(editor.exec(coincident,{id:"guide.layout",preset:"center",replace:false})^.doc)) == 7)
