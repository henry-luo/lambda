import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import effects: lambda.editor.raster.effects
import tone: lambda.editor.raster.tone
import project: lambda.image.project
fn check(value) => value
fn rejected(options) => img.adjust(source,options) ^ { true } ~ { false }
let source = img.from_bytes(b'004080ff80804080ffffff40',{width:3,height:1})^
let invert = {rgb:[[0,1],[1,0]]}
let inverse = img.adjust(source,{curves:invert})^
check(img.sample(inverse,0,0)^ == [255,191,127,255])
check(img.sample(inverse,1,0)^[3] == 128 and img.sample(inverse,2,0)^[3] == 64)
let red_half = img.adjust(source,{curves:{r:[[0,0],[1,0.5]]}})^
check(img.sample(red_half,1,0)^ == [64,128,64,128])
check(img.to_bytes(img.adjust(source,{curves:{rgb:tone.identity,r:tone.identity,g:tone.identity,b:tone.identity}})^)^ == img.to_bytes(source)^)
let levels = img.adjust(source,{black:0.25,white:0.75,gamma:2,output_black:0.2,output_white:0.8})^
check(img.sample(levels,0,0)^[0] == 51 and img.sample(levels,2,0)^[0] == 204)
check(img.sample(levels,1,0)^[0]>=159 and img.sample(levels,1,0)^[0]<=161)
check(img.sample(levels,1,0)^[3] == 128)
let st = editor.open_session(img.of_raster(source))^
let graded = editor.exec(st,{id:"effect.add",name:"Levels",options:{white:0.8,gamma:1.5}})^
let curved = editor.exec(graded,{id:"effect.add",name:"Curves",options:{curves:invert}})^
check(model.primary(model.active(curved)^.node) == model.primary(model.active(st)^.node))
check(len(curved.hist.undo) == 2 and len(effects.items(model.active(curved)^.node)) == 2)
let reordered = editor.exec(curved,{id:"effect.reorder",index:1,to:0})^
check(img.to_bytes(img.render_frame(reordered.doc)^)^ != img.to_bytes(img.render_frame(curved.doc)^)^)
check(editor.exec(curved,{id:"history.undo"})^.doc == graded.doc)
let disabled = editor.exec(editor.exec(curved,{id:"effect.toggle",index:0})^,{id:"effect.toggle",index:1})^
check(img.to_bytes(img.render_frame(disabled.doc)^)^ == img.to_bytes(source)^)
let opened = project.decode(project.encode(curved.doc)^)^
check(effects.items(model.lookup(opened,curved.selected[0]).node)[1].options.curves == invert)
check(img.to_bytes(img.render_frame(opened)^)^ == img.to_bytes(img.render_frame(curved.doc)^)^)
check(rejected({curves:{rgb:[[0,0],[0.4,0.8],[0.3,0.5],[1,1]]}}))
check(rejected({curves:{rgb:[[0.1,0],[1,1]]}}))
check(rejected({curves:{r:[[0,0],[1,1.1]]}}))
check(rejected({curves:{a:tone.identity}}))
check(rejected({curves:{rgb:[]}}))
check(rejected({curves:{rgb:[[0,0],[1,nan]]}}))
check(rejected({curves:{rgb:[for (i in 0 to 32) [i/32,i/32]]}}))
check(rejected({output_black:0.8,output_white:0.2}))
check(rejected({black:0.8,white:0.2}))
let inserted = tone.insert({},"rgb",{x:0.5,y:0.7})^
check(inserted.index == 1 and tone.points(inserted.curves,"rgb") == [[0,0],[0.5,0.7],[1,1]])
let moved = tone.move(inserted.curves,"rgb",1,{x:0.75,y:1.2})^
check(moved.rgb[1] == [0.75,1])
check(tone.move(moved,"rgb",1,{x:1,y:0.4})^.rgb[1] == [0.75,0.4])
check(tone.move(moved,"rgb",0,{x:0.7,y:0.2})^.rgb[0] == [0,0.2])
check(tone.remove(moved,"rgb",1)^.rgb == tone.identity)
check(tone.remove(moved,"rgb",0) ^ { true } ~ { false })
check(tone.with_points({},"gray",tone.identity) ^ { true } ~ { false })
check(tone.checked([[0,0],[0.5,1],[0.5,0],[1,1]]) ^ { true } ~ { false })
let recorded = editor.exec(editor.exec(editor.exec(st,{id:"action.start",name:"Grade"})^,
    {id:"effect.add",name:"Curves",options:{curves:invert,output_black:0.1}})^,{id:"action.stop"})^
let replay = editor.exec(st,{id:"action.replay",action:recorded.actions[0]})^
check(len(replay.hist.undo) == 1 and effects.items(model.active(replay)^.node)[0].options.curves == invert)
