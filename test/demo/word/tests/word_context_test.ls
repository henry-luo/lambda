import focused: ~~.mod_context
import model: ~~.mod_model
import shapes: ~~.mod_shapes
import equation: ~~.mod_equation
import art: ~~.mod_wordart
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

fn offers(editor,command,region=null) => contains([for (a in focused.describe(editor,region).actions) a.command],command)
let doc = node('doc',[node('p',[text("Before")]),node('table',[
    node('tr',[node('td',[text("A")]),node('td',[text("B")])]),
    node('tr',[node('td',[text("C")]),node('td',[text("D")])])]),
    node('ul',[node('li',[node('p',[text("List")])])]),
    node('p',[node_attrs('a',[{name:'href',value:"#word-bookmark-intro"}],[text("Link")])]),node('hr',[])])
let selected = edit_set_selection(model.open_editor(doc),text_selection(pos([0,0],5),pos([0,0],1)))
focused.at_pointer(selected,pos([0,0],3)).selection==selected.selection
focused.at_pointer(selected,pos([0,0],3),false).selection==text_selection(pos([0,0],3),pos([0,0],3))
let cell = focused.at_pointer(selected,pos([1,1,0,0],1))
cell.selection==text_selection(pos([1,1,0,0],1),pos([1,1,0,0],1))
focused.describe(cell).kind=="table"
all([for (command in ["table.insertRowAbove","table.insertRowBelow","table.insertColumnLeft","table.insertColumnRight",
    "table.deleteRow","table.deleteColumn","table.deleteTable"]) offers(cell,command)])
let removed = model.execute(cell,"table.deleteRow")^
doc_text(model.current_node(removed,'table').node)=="AB"
model.execute(removed,"edit.undo")^.doc==doc
let item = focused.at_pointer(cell,pos([2,0,0,0],1))
focused.describe(item).kind=="list" and offers(item,"para.indent") and offers(item,"para.outdent")
not offers(item,"table.deleteRow")
let link = focused.at_pointer(item,pos([3,0,0],2))
focused.describe(link).kind=="link" and offers(link,"insert.removeLink") and offers(link,"ui.followLink")
let rule = focused.at_pointer(link,pos([4],0))
rule.selection==node_selection([4]) and focused.describe(rule).kind=="rule"
not focused.describe(rule).text
let empty = model.open_editor(node('doc',[node('p',[text("")])]))
let picture = model.execute(empty,"insert.picture",{src:"data:image/svg+xml;base64,PHN2Zy8+",alt:"Picture",width:160,height:100})^
focused.describe(picture).kind=="picture" and offers(picture,"ui.pictureSettings")
let rotate = [for (a in focused.describe(picture).actions where a.id=="rotate-left") a][0]
attrs_get(model.current_node(model.execute(picture,rotate.command,rotate.params)^,'img').node.attrs,'data-word-rotation')== -90
not focused.describe(picture).text
let shape = model.execute(empty,"insert.shape",shapes.defaults)^
focused.describe(shape).kind=="shape" and offers(shape,"ui.shapeSettings") and not offers(shape,"ui.pictureSettings")
let eq = model.execute(empty,"insert.equation",equation.defaults)^
focused.describe(eq).kind=="equation" and offers(eq,"ui.equationSettings") and not offers(eq,"object.transform")
let lettering = model.execute(empty,"insert.wordArt",art.defaults)^
focused.describe(lettering).kind=="wordart" and offers(lettering,"ui.wordArtSettings")
let selected_text = edit_set_selection(model.open_editor(doc),text_selection(pos([0,0],0),pos([0,0],6)))
let commented = model.execute(selected_text,"review.addComment",{author:"Me",text:"Check this"})^
offers(commented,"ui.commentText") and offers(commented,"review.resolveComment") and offers(commented,"review.deleteComment")
let tracking = model.execute(empty,"review.trackChanges",{enabled:true,author:"Me"})^
let changed = model.execute(tracking,"text.insert",{data:"New"})^
offers(changed,"review.accept") and offers(changed,"review.reject")
focused.describe(cell,"header").kind=="header" and offers(cell,"ui.headerFooter","header")
offers(cell,"ui.pageNumber","footer") and not offers(cell,"table.deleteRow","footer")
