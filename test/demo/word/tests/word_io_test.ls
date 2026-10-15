import files: ~~.mod_io
import document: ~~.mod_document
import model: ~~.mod_model
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let sample = document.sample()
let snapshot = files.archive(sample,"Studio")
files.restore(parse(format(snapshot,'json'),'json')^)^.doc == sample
files.import_text(files.export_text(sample,{title:"Studio"})^)^.envelope.title == "Studio"
let doc = node('doc',[node('p',[text("Hello & <world>")])])
let selected = edit_set_selection(model.open_editor(doc),text_selection(pos([0,0],0),pos([0,0],5)))
let colored = model.execute(selected,"format.color",{color:"#b43d47"})^
files.restore(parse(format(files.archive(colored.doc,"Colors"),'json'),'json')^)^.doc == colored.doc
contains(files.html_document(colored.doc,"<Title>")^,"&lt;Title&gt;")
contains(files.html_document(colored.doc,"Title")^,"&amp; &lt;world&gt;")
contains(files.html_document(colored.doc,"Title")^,"color: #b43d47")
files.restore({format:"lambda-word",version:2,title:"Bad",document:{}}) ^ { true } ~ { false }
files.restore({format:"lambda-word",version:1,title:"Bad",document:{kind:"node",tag:"script",attrs:[],content:[]}}) ^ { true } ~ { false }
files.restore({format:"lambda-word",version:1,title:"Empty",document:{kind:"node",tag:"doc",attrs:[],content:[]}}) ^ { true } ~ { false }
let imported = [for (ext in ["html","md","txt"]) files.read_document("test/demo/word/tests/import." ++ ext)^]
all([for (opened in imported) contains(model.plain_text(opened.editor.doc),"Imported heading\nA ")])
all([for (opened in imported) opened.session.path == ""])
all([for (opened in imported) files.restore(files.archive(opened.editor.doc,"Imported"))^.doc == opened.editor.doc])
let office = files.read_document("test/input/docx/minimal.docx")^
model.plain_text(office.editor.doc) == "Hello, DOCX!"
office.session.path == ""
files.read_document("test/input/docx/reference/single-paragraph.docx") ^ { contains(^.message,"raw_html") } ~ { false }
let picture_src = files.picture_source("test/demo/word/tests/picture.svg")^
starts_with(picture_src,"data:image/svg+xml;base64,")
let picture = model.execute(model.open_editor(doc),"insert.picture",{src:picture_src,alt:"Embedded picture",width:240,height:120})^
files.restore(files.archive(picture.doc,"Embedded"))^.doc == picture.doc
contains(files.html_document(picture.doc,"Embedded")^,picture_src)
