import model: ~~.mod_model
import storage: ~~.mod_io
import sample: ~~.mod_sample
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import media: lambda.doc.media

let src = media.data_uri(input("test/ui/note/fixtures/clipboard.png",'binary')^,"image/png")^
let base = model.open_editor(model.blank())
let pasted = edit_exec(base,edit_cmd_paste_html("<img src=\"" ++ src ++ "\" width=\"160\" height=\"100\" alt=\"Picture\">",""))
let image = model.images(pasted.doc)[0];
len(model.images(pasted.doc))==1
attrs_get(image.node.attrs,'src')==src
attrs_get(image.node.attrs,'width')=="160" and attrs_get(image.node.attrs,'height')=="100"
let selected = model.select_image(pasted,image.path);
model.selected_image(selected).path==image.path
let sized = model.resize_image(selected,image.path,240,150)^;
attrs_get(node_at(sized.doc,image.path).attrs,'width')==240
attrs_get(node_at(sized.doc,image.path).attrs,'height')==150
sized.selection==node_selection(image.path)
edit_exec(sized,edit_cmd_history_undo()).doc==selected.doc
let natural = model.resize_image(sized,image.path,null,null)^;
attrs_get(node_at(natural.doc,image.path).attrs,'width')==null;
(model.resize_image(sized,image.path,0,0) ^ { null })==null
let book = sample.notebook()
let with_image = {*:book,notes:[{*:book.notes[0],doc:sized.doc}]};
storage.decode(storage.encode(with_image))^==with_image
let copied = edit_exec(base,edit_cmd_paste_html(model.image_html(model.selected_image(sized)),""));
attrs_get(model.images(copied.doc)[0].node.attrs,'src')==src
string(attrs_get(model.images(copied.doc)[0].node.attrs,'width'))=="240"
let removed = edit_exec(sized,{input_type:"deleteByCut"});
len(model.images(removed.doc))==0
edit_exec(removed,edit_cmd_history_undo()).doc==sized.doc
// atomic boundaries remain paste targets after deletion and when replacing a selected image.
pasted.selection==text_selection(pos([0],1),pos([0],1))
let repeated = edit_exec(pasted,edit_cmd_paste_html(model.image_html(image),""));
len(model.images(repeated.doc))==2
let repasted = edit_exec(removed,edit_cmd_paste_html(model.image_html(image),""));
len(model.images(repasted.doc))==1
let replaced = edit_exec(sized,edit_cmd_paste_html(model.image_html(image),""));
len(model.images(replaced.doc))==1
edit_exec(replaced,edit_cmd_history_undo()).doc==sized.doc
let adopted = edit_accept_dom_selection(pasted,{source_selection:text_selection(pos([0],0),pos([0],1)),selection_revision:1});
adopted.editor.selection==node_selection(image.path)
