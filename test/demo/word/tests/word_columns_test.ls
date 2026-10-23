import columns: ~~.mod_columns
import model: ~~.mod_model
import files: ~~.mod_io
import page: ~~.mod_page
import ruler: ~~.mod_ruler
import stories: ~~.mod_stories
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('p',[text("Before and after")]),node('p',[text("Second paragraph")])])
let selection = text_selection(pos([0,0],7),pos([0,0],7))
let base = edit_set_selection(model.open_editor(doc),selection)
columns.settings(doc)==columns.defaults
columns.width(doc,columns.defaults)==172
let two = model.execute(base,"layout.columns",{count:2})^
columns.settings(two.doc)=={count:2,gap:12.7,line:false}
two.doc.content==doc.content and two.selection==selection
len(two.history.undo)==1 and model.execute(two,"edit.undo")^.doc==doc
model.execute(model.execute(two,"edit.undo")^,"edit.redo")^.doc==two.doc
let typed = model.execute(two,"text.insert",{data:"written "})^
model.plain_text(typed.doc)=="Before written and after\nSecond paragraph"
columns.settings(typed.doc)==columns.settings(two.doc)
let custom = model.execute(two,"layout.columns",{count:4,gap:6.5,line:true})^
columns.settings(custom.doc)=={count:4,gap:6.5,line:true}
contains(columns.style(custom.doc),"column-count:4;") and contains(columns.style(custom.doc),"column-gap:6.5mm;")
contains(columns.style(custom.doc),"column-rule:0.5pt solid #718397;")
let one = model.execute(custom,"layout.columns",{count:1})^
columns.settings(one.doc).count==1 and contains(columns.style(one.doc),"column-rule:none;")
one.doc.content==doc.content
model.execute(base,"layout.columns",{count:0}) ^ {true} ~ {false}
model.execute(base,"layout.columns",{count:7}) ^ {true} ~ {false}
model.execute(base,"layout.columns",{count:1.5}) ^ {true} ~ {false}
model.execute(base,"layout.columns",{count:"2"}) ^ {true} ~ {false}
model.execute(base,"layout.columns",{gap: -1}) ^ {true} ~ {false}
model.execute(base,"layout.columns",{gap:81}) ^ {true} ~ {false}
model.execute(base,"layout.columns",{line:"true"}) ^ {true} ~ {false}
model.execute(base,"layout.columns",{count:6}) ^ {true} ~ {false}
let six = model.execute(base,"layout.columns",{count:6,gap:4})^
columns.settings(six.doc).count==6 and columns.width(six.doc,columns.settings(six.doc))>=20
model.execute(custom,"layout.pageSetup",{paper:"A5",left:35,right:35}) ^ {true} ~ {false}
let narrow = model.execute(custom,"layout.pageSetup",{paper:"A5",left:19,right:19})^
columns.settings(narrow.doc)==columns.settings(custom.doc)
let max_margin = ruler.margin_value(custom.doc,"left",10000)
max_margin.left==min(80,floor((210-19-columns.minimum_width(custom.doc)-0.01)*10)/10)
columns.validate(model.execute(custom,"layout.pageSetup",max_margin)^.doc,columns.settings(custom.doc))^
let broken = model.execute(two,"insert.pageBreak")^
len(page.sheets(broken.doc))==2 and model.plain_text(broken.doc)=="Before \nand after\nSecond paragraph"
columns.settings(broken.doc)==columns.settings(two.doc)
let after_break = model.execute(broken,"text.insert",{data:"New "})^
model.plain_text(after_break.doc)=="Before \nNew and after\nSecond paragraph"
let header = model.execute(custom,"layout.headerFooter",{*:stories.defaults,header:"Full width header"})^
stories.settings(header.doc).header=="Full width header" and columns.settings(header.doc)==columns.settings(custom.doc)
files.restore(files.archive(header.doc,"Columns"))^.doc==header.doc
let html = files.html_document(header.doc,"Columns")^
contains(html,"column-count:4;") and contains(html,"column-gap:6.5mm;")
not contains(html,"<r:master-set")
let reopened = files.import_html_document(html)^
reopened==header.doc
let pdf = files.pdf_document(header.doc,"Columns")^
contains(pdf,"xmlns:r=\"urn:lambda:radiant:page\"") and contains(pdf,"<r:page-master") and contains(pdf,"column-count:4;")
contains(pdf,"<r:flow region-name=\"body\">") and contains(pdf,"Full width header")
columns.settings(node_attrs('doc',[{name:'data-word-columns',value:"bad"}],doc.content))==columns.defaults
columns.settings(node_attrs('doc',[{name:'data-word-columns',value:"{\"count\":0}"}],doc.content))==columns.defaults
// XML serialization preserves literal text and well-formed picture elements.
let image = model.execute(custom,"insert.picture",{src:files.picture_source("test/demo/word/tests/picture.svg")^,width:80,height:40,alt:"A & B"})^
let image_pdf = files.pdf_document(image.doc,"A < B")^
contains(image_pdf,"A &amp; B") and contains(image_pdf,"</img>") and contains(image_pdf,"A &lt; B");
parse(image_pdf,'xhtml')^ is element
let dense = model.execute(custom,"layout.columns",{count:5,gap:4})^
ruler.margin_value(dense.doc,"left",10000).left==74.9
columns.validate(model.execute(dense,"layout.pageSetup",ruler.margin_value(dense.doc,"left",10000))^.doc,columns.settings(dense.doc))^
