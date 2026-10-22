import model: ~~.mod_model
import shapes: ~~.mod_shapes
import stories: ~~.mod_stories
import equation: ~~.mod_equation
import art: ~~.mod_wordart
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step

let editor = model.open_editor(node('doc',[node('p',[text("Body")])]))
let p = {*:stories.defaults,header:"Studio",footer:"Page {PAGE} of {NUMPAGES}"}
let headed = model.execute(editor,"layout.headerFooter",p)^
stories.settings(headed.doc)==p
stories.field_text(p.footer,2,3)=="Page 2 of 3"
contains(files.html_document(headed.doc,"Stories")^,"counter(pages)")
model.execute(headed,"edit.undo")^.doc==editor.doc
stories.settings(files.import_html_document(files.html_document(headed.doc,"Stories")^)^)==p
let eq = model.execute(editor,"insert.equation",equation.defaults)^
equation.settings(model.current_node(eq,'img').node)==equation.defaults
starts_with(attrs_get(model.current_node(eq,'img').node.attrs,'src'),"data:image/svg+xml;base64,")
model.execute(eq,"edit.undo")^.doc==editor.doc
let wordart = model.execute(editor,"insert.wordArt",art.defaults)^
art.settings(model.current_node(wordart,'img').node)==art.defaults
all([for (kind in ["line","arrow","star","heart"]) shapes.settings(model.current_node(model.execute(editor,"insert.shape",{*:shapes.defaults,kind:kind})^,'img').node).kind==kind])
let labeled = model.execute(editor,"insert.shape",{*:shapes.defaults,text:"Decision",rotation:30,weight:3})^
attrs_get(model.current_node(labeled,'img').node.attrs,'alt')=="Decision"
files.restore(files.archive(labeled.doc,"Shape"))^.doc==labeled.doc
// Content remains editable after an HTML round trip, including source and style metadata.
let eq2 = model.execute(eq,"equation.set",{*:equation.defaults,source:"E=mc^2",size:28})^
attrs_get(model.current_node(eq2,'img').node.attrs,'alt')=="E=mc^2"
model.execute(eq2,"edit.undo")^.doc==eq.doc
equation.settings(files.import_html_document(files.html_document(eq2.doc,"Math")^)^.content[0].content[0])=={*:equation.defaults,source:"E=mc^2",size:28}
model.execute(editor,"insert.equation",{*:equation.defaults,source:"\\notSupported{x}"}) ^ { true } ~ { false }
model.execute(editor,"insert.equation",{*:equation.defaults,size:100}) ^ { true } ~ { false }
let warped = model.execute(wordart,"wordArt.set",{*:art.defaults,text:"秋 & Studio",transform:"archUp",style:"hollow"})^
art.settings(model.current_node(warped,'img').node).text=="秋 & Studio"
model.execute(warped,"edit.undo")^.doc==wordart.doc
files.restore(files.archive(warped.doc,"Art"))^.doc==warped.doc
art.settings(files.import_html_document(files.html_document(warped.doc,"Art")^)^.content[0].content[0]).transform=="archUp"
model.execute(editor,"insert.wordArt",{*:art.defaults,text:""}) ^ { true } ~ { false }
model.execute(headed,"layout.pageSetup",{top:0}) ^ { true } ~ { false }
model.execute(editor,"layout.headerFooter",{*:p,header:"One\nTwo\nThree"}) ^ { true } ~ { false }
let malformed = node_attrs('doc',[{name:'data-word-shape',value:"[]"},{name:'data-word-stories',value:"null"},{name:'data-word-tracking',value:"42"}],[])
shapes.settings(malformed)==null
stories.settings(malformed)==stories.defaults
let escaped = model.execute(editor,"layout.headerFooter",{*:p,header:"A \"quoted\" \\ line {NUMPAGES}/{PAGE}"})^
stories.field_text(stories.settings(escaped.doc).header,2,7)=="A \"quoted\" \\ line 7/2"
contains(stories.export_css(escaped.doc),"counter(pages)") and contains(stories.export_css(escaped.doc),"\\\"quoted\\\"")
