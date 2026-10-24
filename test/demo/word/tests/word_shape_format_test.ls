import model: ~~.mod_model
import shapes: ~~.mod_shapes
import art: ~~.mod_wordart
import formatting: ~~.mod_shape_format
import layout: ~~.mod_object_layout
import files: ~~.mod_io
import objects: ~~.mod_objects
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import image: lambda.editor.mod_image
import controller: lambda.edit.controller
import outcome: lambda.dom.edit_result

let base = model.open_editor(node('doc',[node('p',[text("Keep all surrounding text.")])]))
let original = model.execute(base,"insert.shape",{*:shapes.defaults,text:"Studio",rotation:25})^
let positioned = model.execute(model.execute(original,"object.layout",{values:{wrap:"square",side:"right",distance:12}})^,"object.flip",{axis:"x"})^
let styled = model.execute(positioned,"shape.format",{preset:"violet"})^
let settings = shapes.settings(image.selected(styled).node)
let values = {fill_style:"gradient",fill2:"#fff0d5",dash:"dashed",weight:3,effect:"shadow",opacity:75,
    font:"Georgia",size:20,bold:true,italic:true,align:"left",vertical:"top",direction:90,inset:8,
    text_style:"outlined",text_outline:"#24334b",alt:"Studio sign"}
let formatted = model.execute(styled,"shape.format",{values:values})^
let n = image.selected(formatted).node
let p = shapes.settings(n)
let svg = format(shapes.graphic(p),'xml')^
let resized = model.execute(formatted,"object.transform",{width:240,height:140,rotation:40})^
let exported = files.import_html_document(files.html_document(formatted.doc,"Shapes")^)^
let copied = controller.clipboard_result(formatted,"copy",outcome.model_applied(false,false,false,"",formatted.selection,formatted.model_revision))
let pasted = image.images(edit_exec(base,edit_cmd_paste_html(copied.clipboard_html,"")).doc)[0].node
let wordart = model.execute(base,"insert.wordArt",art.defaults)^
let edited_art = model.execute(wordart,"shape.format",{values:{style:"outlined",fill:"#805bc5",outline:"#3478cc",text:"Hello",alt:"Decorative hello"}})^

// partial updates preserve geometry, source selection, text, layout, and reflection.
settings.fill=="#805bc5" and settings.stroke=="#63439e"
settings.text=="Studio" and settings.rotation==25 and settings.width==160
layout.settings(image.selected(styled).node)==layout.settings(image.selected(positioned).node)
contains(attrs_get(n.attrs,'style'),"rotate(25deg) scale(-1,1)")
model.plain_text(formatted.doc)==model.plain_text(base.doc)
formatted.selection==positioned.selection
all([for (key,value in values) p[key]==value])
attrs_get(n.attrs,'alt')=="Studio sign"
attrs_get(n.attrs,'src')==objects.source(shapes.graphic(p))^
contains(svg,"linearGradient") and contains(svg,"url(#word-shape-fill)")
contains(svg,"stroke-dasharray=\"12 9\"")
contains(svg,"opacity=\"0.75\"") and contains(svg,"translate(4 4)")
contains(svg,"font-family=\"Georgia\"") and contains(svg,"font-weight=\"bold\"") and contains(svg,"font-style=\"italic\"")
contains(svg,"rotate(90 80 50)") and contains(svg,"text-anchor=\"start\"")
shapes.settings(image.selected(resized).node).width==240 and shapes.settings(image.selected(resized).node).font=="Georgia"
shapes.settings(image.selected(resized).node).alt=="Studio sign" and attrs_get(image.selected(resized).node.attrs,'alt')=="Studio sign"
model.execute(formatted,"edit.undo")^.doc==styled.doc
model.execute(model.execute(formatted,"edit.undo")^,"edit.redo")^.doc==formatted.doc
files.restore(files.archive(formatted.doc,"Shapes"))^.doc==formatted.doc
shapes.settings(image.images(exported)[0].node)==p
attrs_get(image.images(exported)[0].node.attrs,'src')==attrs_get(n.attrs,'src')
shapes.settings(pasted)==p and layout.settings(pasted)==layout.settings(n)
all([for (s in formatting.styles) shapes.settings(image.selected(model.execute(original,"shape.format",{preset:s.id})^).node).fill==s.values.fill])
all([for (d in [0,90,-90]) shapes.settings(image.selected(model.execute(original,"shape.format",{values:{direction:d}})^).node).direction==d])
all([for (s in ["solid","outlined","hollow","shadowed"]) model.execute(original,"shape.format",{values:{text_style:s}})^ is map])
art.settings(image.selected(edited_art).node).style=="outlined" and art.settings(image.selected(edited_art).node).text=="Hello"
attrs_get(image.selected(edited_art).node.attrs,'alt')=="Decorative hello"
model.execute(edited_art,"edit.undo")^.doc==wordart.doc
model.execute(base,"shape.format",{values:{fill:"#3478cc"}}) ^ {true} ~ {false}
model.execute(original,"shape.format",{preset:"unknown"}) ^ {true} ~ {false}
model.execute(wordart,"shape.format",{preset:"blue"}) ^ {true} ~ {false}
model.execute(original,"shape.format",{values:{unknown:1}}) ^ {true} ~ {false}
all([for (bad in [{width:0},{height:2001},{rotation:181},{fill:"url(file.svg)"},{weight:13},{opacity:-1},
    {opacity:101},{direction:45},{dash:"wavy"},{effect:"glow"},{inset:-1},{bold:"true"},{font:""},{text_style:"unknown"}])
    model.execute(original,"shape.format",{values:bad}) ^ {true} ~ {false}]
)
// older documents receive defaults while retaining their authored colors and dimensions.
let legacy = node_attrs('img',[{name:'data-word-shape',value:"{\"kind\":\"rect\",\"width\":100,\"fill\":\"#527f78\"}"}],[])
shapes.settings(legacy).fill=="#527f78" and shapes.settings(legacy).width==100 and shapes.settings(legacy).dash=="solid"
let dialog_edit = model.execute(formatted,"shape.set",{text:"Edited in settings",width:200})^
shapes.settings(image.selected(dialog_edit).node)=={*:p,text:"Edited in settings",width:200}
art.settings(image.selected(model.execute(edited_art,"wordArt.set",{text:"Edited WordArt"})^).node)==
    {*:art.settings(image.selected(edited_art).node),text:"Edited WordArt"}
let draft = {path:image.selected(formatted).path,revision:formatted.model_revision,values:{fill:"#12"}}
formatting.draft_values(formatted,draft)=={fill:"#12"}
formatting.draft_values(formatted,{*:draft,revision:draft.revision+1})=={} and
    formatting.draft_values(formatted,{*:draft,path:[99]})=={}
let tiny = format(shapes.graphic({*:shapes.defaults,width:16,height:16,weight:12,effect:"shadow"}),'xml')^
not contains(tiny,"scale(-") and contains(tiny,"translate(0.5 0.5)")
