import model: ~~.mod_model
import shapes: ~~.mod_shapes
import art: ~~.mod_wordart
import equation: ~~.mod_equation
import controller: lambda.edit.controller
import outcome: lambda.dom.edit_result
import image: lambda.editor.mod_image
import media: lambda.doc.media
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_step
import lambda.editor.mod_source_pos

let empty = model.open_editor(node('doc',[node('p',[])]))
let src = media.data_uri(input("test/ui/note/fixtures/clipboard.png",'binary')^,"image/png")^
let picture = model.execute(empty,"insert.picture",{src:src,alt:"A & B \"picture\"",width:160,height:100,rotation:30})^
let shape = model.execute(empty,"insert.shape",{*:shapes.defaults,kind:"diamond",text:"A & B",rotation:45})^
let lettering = model.execute(empty,"insert.wordArt",{*:art.defaults,text:"Copied words",rotation: -30})^
let formula = model.execute(empty,"insert.equation",{*:equation.defaults,source:"E=mc^2"})^
let editors = [picture,shape,lettering,formula]
fn copied(editor,input_type="copy",result=null) => controller.clipboard_result(editor,input_type,
    if (result!=null) result else outcome.model_applied(false,false,false,"",editor.selection,editor.model_revision))
fn pasted(editor) => edit_exec(empty,edit_cmd_paste_html(copied(editor).clipboard_html,""))
fn value(editor,key) => attrs_get(image.images(editor.doc)[0].node.attrs,key)

all([for (editor in editors) copied(editor).clipboard_image==value(editor,'src')])
all([for (editor in editors) copied(editor).clipboard_text==value(editor,'alt')])
all([for (editor in editors) not copied(editor).changed and not copied(editor).history_recorded])
all([for (editor in editors) copied(editor).selection_after==editor.selection and copied(editor).model_revision==editor.model_revision])
contains(copied(picture).clipboard_html,"&amp;") and contains(copied(picture).clipboard_html,"&quot;")
all([for (editor in editors) len(image.images(pasted(editor).doc))==1])
all([for (editor in editors) value(pasted(editor),'src')==value(editor,'src')])
all([for (editor in editors) value(pasted(editor),'alt')==value(editor,'alt')])
all([for (editor in editors) string(value(pasted(editor),'width'))==string(value(editor,'width'))])
all([for (editor in editors) string(value(pasted(editor),'height'))==string(value(editor,'height'))])
all([for (editor in [picture,shape,lettering]) value(pasted(editor),'style')==value(editor,'style')])
all([for (editor in [picture,shape,lettering]) string(value(pasted(editor),'data-word-rotation'))==string(value(editor,'data-word-rotation'))])
shapes.settings(image.images(pasted(shape).doc)[0].node)==shapes.settings(image.selected(shape).node)
art.settings(image.images(pasted(lettering).doc)[0].node)==art.settings(image.selected(lettering).node)
equation.settings(image.images(pasted(formula).doc)[0].node)==equation.settings(image.selected(formula).node)
all([for (editor in editors) len(pasted(editor).history.undo)==1])
all([for (editor in editors) edit_exec(pasted(editor),edit_cmd_history_undo()).doc==empty.doc])
let cut = edit_exec(shape,{input_type:"deleteByCut"})
let cut_result = copied(shape,"deleteByCut",outcome.model_applied(true,true,true,"clipboard",cut.selection,cut.model_revision));
len(image.images(cut.doc))==0 and cut_result.clipboard_image==value(shape,'src')
cut_result.changed and cut_result.history_recorded and cut_result.selection_after==cut.selection
edit_exec(cut,edit_cmd_history_undo()).doc==shape.doc
let plain_result = outcome.model_applied(false,false,false,"",empty.selection,empty.model_revision)
let declined = outcome.decline(true,false,"read-only",0);
copied(empty)==plain_result
copied(shape,"insertText",plain_result)==plain_result
copied(shape,"copy",declined)==declined
let hostile = edit_exec(empty,edit_cmd_paste_html("<img src=\"" ++ src ++ "\" onclick=\"bad()\" data-unrecognized=\"no\">",""));
value(hostile,'onclick')==null and value(hostile,'data-unrecognized')==null
// the shared controller also accepts the Markdown schema's image tag.
let markdown_image = edit_open(node('doc',[node('paragraph',[
    node_attrs('image',image.selected(picture).node.attrs,[])])]),editor_schemas.markdown,node_selection([0,0]));
contains(copied(markdown_image).clipboard_html,"<img ") and value(pasted(markdown_image),'src')==src
