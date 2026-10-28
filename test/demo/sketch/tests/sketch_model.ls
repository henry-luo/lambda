import m: ~~.mod_model
import art: ~~.mod_artwork
import scene: ~~.mod_scene

let st = m.initial(m.blank())
let p = {x:100.0,y:100.0}
let q = {x:180.0,y:160.0}
let begin = m.begin(st,p)
let preview = m.preview(begin,q)
preview.doc == st.doc and len(preview.hist.undo) == 0
let drawn = m.finish(preview)
len(m.objects(drawn.doc)) == 1 and len(drawn.hist.undo) == 1
m.chosen(drawn).kind == "path" and len(m.chosen(drawn).points) == 2
m.step_history(drawn).doc == st.doc
m.step_history(m.step_history(drawn),true).doc == drawn.doc
m.cancel(preview).doc == st.doc and len(m.cancel(preview).hist.undo) == 0
let dot = m.finish(m.begin(st,p))
len(m.chosen(dot).points) == 2
let rectangle = m.finish(m.preview(m.begin({*:drawn,tool:"rect"},p),q,true))
m.chosen(rectangle).width == 80 and m.chosen(rectangle).height == 80
let moved = m.preview(m.begin({*:rectangle,tool:"select"},{x:140.0,y:140.0}),{x:160.0,y:180.0})
len(moved.hist.undo) == 2 and m.chosen(moved).x == 120
let done = m.finish(moved)
len(done.hist.undo) == 3 and m.step_history(done).doc == rectangle.doc
m.cancel(moved).doc == rectangle.doc
let resized = m.finish(m.preview(m.begin({*:rectangle,tool:"select"},{x:180.0,y:180.0}),{x:210.0,y:220.0}))
m.chosen(resized).width == 110 and m.chosen(resized).height == 120
let deleted = m.remove(rectangle)
len(m.objects(deleted.doc)) == 1 and m.step_history(deleted).doc == rectangle.doc
let copied = m.duplicate(rectangle)
len(m.objects(copied.doc)) == 3 and m.chosen(copied).x == 120
let edited = m.property(m.property(rectangle,"fill","#7665d9")^,"fill","#418b80")^
len(edited.hist.undo) == 2
let committed = m.finish_edit(edited)
len(committed.hist.undo) == 3 and m.step_history(committed).doc == rectangle.doc
m.property(rectangle,"fill","url(unsafe)") ^ { true } ~ { false }
m.property(rectangle,"size","not a number") ^ { true } ~ { false }
m.property(rectangle,"opacity",2) ^ { true } ~ { false }
m.property(rectangle,"width",inf) ^ { true } ~ { false }
let branch = m.finish_edit(m.property(m.step_history(committed),"stroke","#ffffff")^)
len(branch.hist.redo) == 0
let pen = m.pen_point(m.pen_point(m.pen_point({*:st,tool:"pen"},p),q),{x:220.0,y:80.0})
len(pen.pen) == 3 and len(m.objects(pen.doc)) == 0 and len(pen.hist.undo) == 0
let closed = m.pen_point(pen,p)
m.chosen(closed).closed and m.chosen(closed).fill == st.fill and len(closed.pen) == 0
let open_path = m.finish_pen(pen)
not m.chosen(open_path).closed and m.chosen(open_path).fill == "none"
let node_drag = m.preview(m.begin({*:open_path,tool:"nodes"},p),{x:110.0,y:90.0})
m.points(m.chosen(node_drag))[0] == {x:110.0,y:90.0}
m.cancel(node_drag).doc == open_path.doc
let erased = m.preview(m.begin({*:rectangle,tool:"eraser"},{x:150.0,y:150.0}),p)
len(erased.hist.undo) == 2 and len(m.objects(erased.doc)) == 0
len(m.finish(erased).hist.undo) == 3
m.cancel(erased).doc == rectangle.doc
let pencil = m.finish(m.preview(m.begin({*:st,tool:"pencil",size:15.0},p),q))
m.chosen(pencil).size == 2
let reversed = m.new_shape(st,"line",q,p)
m.points(reversed) == [q,p]
m.hit(rectangle.doc,{x:140.0,y:140.0}) == m.chosen(rectangle).id
m.hit(st.doc,p) == ""
let hollow = m.finish_edit(m.property(rectangle,"fill","none")^)
m.hit(hollow.doc,{x:150.0,y:120.0}) == ""
let text = m.begin({*:st,tool:"text",text:"Hello & <world>"},p)
m.chosen(text).kind == "text" and m.chosen(text).text == "Hello & <world>"
let picked = m.begin({*:rectangle,tool:"eyedropper"},{x:140.0,y:140.0})
picked.stroke == m.chosen(rectangle).fill and picked.doc == rectangle.doc
m.decode(parse(format(m.encode(committed.doc),'json'),'json')^)^ == committed.doc
m.decode(m.encode(art.starter()))^ == art.starter()
contains(format(scene.svg(text.doc),'xml'),"&lt;world&gt;")
not contains(format(scene.svg(rectangle.doc),'xml'),"selection-box")
len(m.objects(art.starter())) == 13
let new_text = m.finish_edit(m.property(text,"text","A")^)
m.chosen(new_text).width == 48*.62 and m.chosen(new_text).height == 60
let larger = m.finish_edit(m.property(new_text,"font_size",96)^)
m.chosen(larger).width == 96*.62 and m.chosen(larger).height == 120
m.chosen(m.finish(m.begin({*:st,tool:"pencil",size:15.0},p))).size == 2
let node_outside = m.finish(m.preview(m.begin({*:open_path,tool:"nodes"},p),{x:30.0,y:20.0}))
m.chosen(node_outside).x == 30 and m.chosen(node_outside).y == 20
m.step_history(node_outside).doc == open_path.doc
let constrained_line = m.points(m.new_shape(st,"line",p,{x:200.0,y:110.0},true))
; (abs(constrained_line[0].y-constrained_line[1].y)<0.001)
m.cancel({*:st,pan:{x:50.0,y:80.0},drag:{mode:"pan",before:st.doc,pan:{x:0.0,y:0.0}}}).pan == {x:0.0,y:0.0}
