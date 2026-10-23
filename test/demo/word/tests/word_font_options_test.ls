import ui: ~~.mod_ui
import model: ~~.mod_model
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

ui.font_options(["Verdana","Arial","Arial"],"Georgia") ==
    [{value:"Arial",label:"Arial"},{value:"Georgia",label:"Georgia"},{value:"Verdana",label:"Verdana"}]
ui.font_options(["Verdana"],"Missing Font") ==
    [{value:"Missing Font",label:"Missing Font"},{value:"Verdana",label:"Verdana"}]
ui.font_options([],"Georgia") == [{value:"Georgia",label:"Georgia"}]
let base = model.open_editor(node('doc',[node('p',[text("Native")])]))
let selected = edit_set_selection(base,text_selection(pos([0,0],0),pos([0,0],6)))
let applied = model.execute(selected,"format.font",{name:"Baskerville"})^
model.char_value(applied,"font-family","Georgia") == "Baskerville"
let unicode = model.execute(applied,"format.font",{name:"思源宋体"})^
model.char_value(unicode,"font-family","Georgia") == "思源宋体"
let empty_name = model.execute(selected,"format.font",{name:""}) ^ {null}
empty_name == null
let invalid_name = model.execute(selected,"format.font",{name:"serif; color:red"}) ^ {null}
invalid_name == null
