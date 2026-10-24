import page: ~~.mod_page
import model: ~~.mod_model
import files: ~~.mod_io
import ruler: ~~.mod_ruler
import columns: ~~.mod_columns
import stories: ~~.mod_stories
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('p',[text("First sheet")]),
    node_attrs('p',[{name:'data-word-page-break',value:"true"}],[text("Second sheet")]),
    node_attrs('p',[{name:'data-word-page-break',value:"true"}],[text("Third sheet")])])
let selected = text_selection(pos([1,0],3),pos([1,0],3))
let base = edit_set_selection(model.open_editor(doc),selected)
let presets = page.margin_presets
len(presets)==5 and [for (p in presets) p.id]==["normal","narrow","moderate","wide","mirrored"]
let changed = [for (preset in presets) model.execute(base,"layout.pageSetup",preset.values)^]
all([for (i,editor in changed) page.margin_preset_active(editor.doc,presets[i])])
all([for (editor in changed) editor.doc.content==doc.content and editor.selection==selected])
all([for (editor in changed) model.execute(editor,"edit.undo")^.doc==doc and len(editor.history.undo)==1])
page.geometry(changed[0].doc).left==96 and page.geometry(changed[0].doc).top==96
page.geometry(changed[1].doc).left==48 and page.geometry(changed[1].doc).bottom==48
page.geometry(changed[2].doc).left==72 and page.geometry(changed[2].doc).top==96
page.geometry(changed[3].doc).left==192 and page.geometry(changed[3].doc).right==192
let mirrored = changed[4]
page.settings(mirrored.doc).mirrored==true
page.geometry(mirrored.doc,1).left==120 and page.geometry(mirrored.doc,1).right==96
page.geometry(mirrored.doc,2).left==96 and page.geometry(mirrored.doc,2).right==120
page.geometry(mirrored.doc,3)==page.geometry(mirrored.doc,1)
page.margin_summary(presets[4].values)==["Top: 1\", Bottom: 1\"","Inside: 1.25\", Outside: 1\""]
page.margin_summary(presets[2].values)[1]=="Left: 0.75\", Right: 0.75\""
page.margin_key(mirrored.doc,"left",2)=="right" and page.margin_key(mirrored.doc,"right",2)=="left"
page.margin_key(mirrored.doc,"top",2)=="top" and page.margin_key(mirrored.doc,"left",1)=="left"
ruler.margin_value(mirrored.doc,"left",96,2)=={right:50.8}
ruler.margin_value(mirrored.doc,"right",0,2)=={left:31.75}
let outside = model.execute(mirrored,"layout.pageSetup",ruler.margin_value(mirrored.doc,"left",96,2))^
page.settings(outside.doc).right==50.8 and page.settings(outside.doc).left==31.75
model.execute(outside,"edit.undo")^.doc==mirrored.doc
let typed = model.execute(mirrored,"text.insert",{data:" new"})^
doc_text(typed.doc.content[1])=="Sec newond sheet" and page.settings(typed.doc)==page.settings(mirrored.doc)
let custom = model.execute(mirrored,"layout.pageSetup",{top:20,right:15,bottom:22,left:27})^
page.geometry(custom.doc,2).left==round(15*96/25.4) and page.geometry(custom.doc,2).right==round(27*96/25.4)
not any([for (preset in presets) page.margin_preset_active(custom.doc,preset)])
let normal = model.execute(custom,"layout.pageSetup",{mirrored:false})^
page.geometry(normal.doc,2)==page.geometry(normal.doc,1)
model.execute(custom,"layout.pageSetup",{mirrored:"true"}) ^ {true} ~ {false}
model.execute(custom,"layout.pageSetup",{left:81}) ^ {true} ~ {false}
model.execute(custom,"layout.pageSetup",{paper:"A5",left:80,right:80}) ^ {true} ~ {false}
let four = model.execute(base,"layout.columns",{count:4})^
model.execute(four,"layout.pageSetup",presets[3].values) ^ {true} ~ {false}
let headed = model.execute(mirrored,"layout.headerFooter",{*:stories.defaults,header:"Full width header",footer:"Page {PAGE}"})^
contains(format(stories.story(headed.doc,"header",2,3),'html')^,"left:96px;right:120px;")
contains(page.editor_style(mirrored.doc,2),"padding:96px 120px 96px 96px;")
contains(page.export_css(mirrored.doc),"@page:left {margin-left:25.4mm;margin-right:31.75mm;}")
contains(page.export_css(mirrored.doc),"@page:right {margin-left:31.75mm;margin-right:25.4mm;}")
files.restore(files.archive(custom.doc,"Facing pages"))^.doc==custom.doc
files.import_html_document(files.html_document(custom.doc,"Facing pages")^)^==custom.doc
let two_columns = model.execute(mirrored,"layout.columns",{count:2,gap:8})^
let pdf = files.pdf_document(two_columns.doc,"Facing columns")^
contains(pdf,"<r:page-master name=\"word-columns\"><r:region")
contains(pdf,"@page:left") and contains(pdf,"column-count:2;")
page.valid(page.defaults) and page.settings(doc)==page.defaults
