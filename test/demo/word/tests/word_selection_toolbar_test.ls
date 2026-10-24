import mini: ~~.mod_selection_toolbar
import model: ~~.mod_model
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let base = model.open_editor(node('doc',[node('p',[text("Selected text")])]))
let selected = edit_set_selection(base,text_selection(pos([0,0],0),pos([0,0],8)))
mini.available(selected)
not mini.available(base)
not mini.available(edit_set_selection(base,node_selection([0])))
not mini.available(edit_set_selection(base,all_selection()))
let st = {editor:selected,read_only:false,dialog:null,context:null,mini_hidden:false}
mini.visible(st)
not mini.visible({*:st,read_only:true})
not mini.visible({*:st,dialog:{kind:"paragraph"}})
not mini.visible({*:st,context:{x:1,y:1}})
not mini.visible({*:st,mini_hidden:true})
let viewport = {left:200,top:150,right:900,bottom:700}
let size = {width:340,height:64}
let rect = {left:300,top:400,right:450,bottom:420,width:150,height:20}
mini.placement([rect],viewport,size) == {left:300,top:328}
mini.placement([{*:rect,top:160,bottom:180}],viewport,size) == {left:300,top:188}
mini.placement([{*:rect,left:880,right:895}],viewport,size) == {left:560,top:328}
mini.placement([{*:rect,top:80,bottom:100}],viewport,size) == null
mini.placement([{*:rect,left:50,right:150}],viewport,size) == null
mini.placement([],viewport,size) == null
mini.placement([rect],{*:viewport,right:400},size) == null
mini.placement([{*:rect,top:80,bottom:100},rect],viewport,size) == {left:300,top:328}
mini.placement([{*:rect,top:155,bottom:690}],viewport,size) == {left:300,top:636}
