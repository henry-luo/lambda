import editor: lambda.editor.raster
import img: lambda.image
import interaction: lambda.editor.raster.interaction
import model: lambda.editor.raster.model
fn check(label,condition) => condition
let raster = img.blank({width:8,height:8,color:[1.0,0.0,0.0,1.0]})^
let st = editor.exec(editor.open_session(editor.new_document({width:16,height:16})^)^,
    {id:"layer.insert",raster:raster,position:[1,2]})^
let input = {point:{x:3.0,y:4.0},screen:{x:30.0,y:40.0},pointer:1,shift:false}
let begin = interaction.begin(st,input)^
let draft = interaction.preview(begin,{point:{x:8.0,y:10.0},screen:{x:80.0,y:100.0}})^
check("draft-no-history",len(draft.hist.undo) == len(st.hist.undo))
check("draft-position",model.position(editor.lookup(draft.doc,st.selected[0]).node) == [6,8])
check("cancel",interaction.cancel(draft).doc == st.doc and interaction.cancel(draft).gesture == null)
let final = interaction.finish(draft)^
check("one-transaction",len(final.hist.undo) == len(st.hist.undo)+1 and final.gesture == null)
check("undo",editor.exec(final,{id:"history.undo"})^.doc == st.doc)
check("no-op",len(interaction.finish(begin)^.hist.undo) == len(st.hist.undo))
check("gesture-command",editor.exec(begin,{id:"layer.delete"}) ^ { true } ~ { false })
let pan = interaction.preview(interaction.begin({*:st,tool:"pan"},input)^,
    {point:{x:8,y:10},screen:{x:80,y:100}})^
check("pan",pan.doc == st.doc and pan.pan == {x:50,y:60})
check("pan-no-history",len(interaction.finish(pan)^.hist.undo) == len(st.hist.undo))
check("pan-cancel",interaction.cancel(pan).pan == st.pan)
check("unknown-option",editor.exec(st,{id:"layer.move",dx:1,dy:2,typo:1}) ^ { true } ~ { false })
check("registry",editor.command_state(st,"layer.move")^.enabled and not editor.command_state(st,"unknown")^.enabled)
let imported = editor.open_session(img.of_raster(raster))^
let clicked = interaction.finish(interaction.preview(interaction.begin(imported,input)^,input)^)^
check("implicit-zero-position",not editor.dirty(clicked) and len(clicked.hist.undo) == 0)
