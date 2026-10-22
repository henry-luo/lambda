import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import canvas: lambda.editor.raster.canvas
import guides: lambda.editor.raster.guides
import channels: lambda.editor.raster.channels
import project: lambda.image.project
fn check(value) => value
fn pixels(doc) => img.to_bytes(img.render_frame(doc)^)^
let red = img.blank({width:4,height:2,color:[1,0,0,1]})^
let local_mask = img.mask(img.from_bytes(b'ff80ff80ff80ff80',{width:4,height:2,channels:1})^,{default:0})
let group_mask = img.mask(img.selection(8,6,{shape:"rectangle",start:[2,2],end:[4,4]})^,{default:0})
let layer = img.with_children(img.layer(red,{id:"leaf",position:[2,2]}),[red,local_mask])^
let doc = img.document(img.stack([img.stack([layer,group_mask],{id:"group"})]),{width:8,height:6})
let initial = editor.open_session(doc)^
let selected = editor.exec(initial,{id:"selection.shape",options:{shape:"rectangle",start:[1,1],end:[5,4]}})^
let stored = editor.exec(selected,{id:"channel.save",name:"Coverage"})^
let st = editor.exec(editor.exec(stored,{id:"guide.add",axis:"x",position:3})^,{id:"guide.add",axis:"y",position:4})^
let original_pixels = img.render_frame(st.doc)^
let expected = [[0,0],[3,0],[6,0],[0,2],[3,2],[6,2],[0,4],[3,4],[6,4]]
check(all([for (i,anchor in canvas.anchors) canvas.offset(st.doc,{width:14,height:10},anchor.id)^ == expected[i]]))
check(all([for (i,anchor in canvas.anchors) (
    let resized = editor.exec(st,{id:"document.resize_canvas",width:14,height:10,anchor:anchor.id})^,
    let dx = expected[i][0],let dy = expected[i][1],
    pixels(resized.doc) == img.to_bytes(img.reframe(original_pixels,[-dx,-dy,14-dx,10-dy])^)^ and
        model.primary(model.lookup(resized.doc,"leaf").node) == red and
        model.mask(model.lookup(resized.doc,"leaf").node) == local_mask and
        model.position(model.mask(model.lookup(resized.doc,"group").node)) == [dx,dy] and
        model.mask(model.lookup(resized.doc,"group").node)[0] == group_mask[0])]))
let enlarged = editor.exec(st,{id:"document.resize_canvas",width:14,height:10,anchor:"bottom-right"})^
check(model.position(model.lookup(enlarged.doc,"leaf").node) == [8,6])
check(img.to_bytes(enlarged.selection)^ == img.to_bytes(img.reframe(st.selection,[-6,-4,8,6])^)^)
check(channels.items(enlarged.doc)[0][0] == enlarged.selection)
check([for (g in guides.items(enlarged.doc)) g.position] == [9,8])
check(len(enlarged.hist.undo) == len(st.hist.undo)+1)
check(editor.exec(enlarged,{id:"history.undo"})^.doc == st.doc and editor.exec(enlarged,{id:"history.undo"})^.selection == st.selection)
check(editor.exec(editor.exec(enlarged,{id:"history.undo"})^,{id:"history.redo"})^.doc == enlarged.doc)
let cropped = editor.exec(enlarged,{id:"document.crop",rect:[6,4,14,10]})^
check(pixels(cropped.doc) == pixels(st.doc) and model.position(model.mask(model.lookup(cropped.doc,"group").node)) == [0,0])
let shrunk = editor.exec(st,{id:"document.resize_canvas",width:4,height:3,anchor:"bottom-right"})^
check(pixels(shrunk.doc) == img.to_bytes(img.reframe(original_pixels,[4,3,8,6])^)^)
check(len(guides.items(shrunk.doc)) == 1 and guides.items(shrunk.doc)[0].position == 1)
check(shrunk.selection == img.reframe(st.selection,[4,3,8,6])^)
check(canvas.offset(st.doc,{width:11,height:9},"center")^ == [1,1])
check(canvas.offset({width:11,height:9},st.doc,"center")^ == [-2,-2])
check(editor.exec(st,{id:"document.resize_canvas",width:14,height:10})^.doc == editor.exec(st,{id:"document.resize_canvas",width:14,height:10,anchor:"top-left"})^.doc)
check(editor.exec(st,{id:"document.resize_canvas",width:8,height:6,anchor:"center"})^.hist == st.hist)
check(editor.exec(st,{id:"document.resize_canvas",width:14,height:10,anchor:"bad"}) ^ { true } ~ { false })
check(editor.exec(st,{id:"document.resize_image",width:14,height:10,anchor:"center"}) ^ { true } ~ { false })
check(project.decode(project.encode(enlarged.doc)^)^ == enlarged.doc)
let moved = editor.exec(initial,{id:"layer.move",dx:1,dy:1})^
check(pixels(moved.doc) == img.to_bytes(img.reframe(original_pixels,[-1,-1,7,5])^)^)
check(model.position(model.mask(model.lookup(moved.doc,"group").node)) == [1,1])
