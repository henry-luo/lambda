import model: ~~.mod_model
import equation: ~~.mod_equation
import catalog: ~~.mod_equation_catalog
import authoring: ~~.mod_equation_edit
import files: ~~.mod_io
import layout: ~~.mod_object_layout
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import image: lambda.editor.mod_image
import controller: lambda.edit.controller
import outcome: lambda.dom.edit_result
import mathdoc: lambda.doc.math.math

let base = edit_set_selection(model.open_editor(node('doc',[node('p',[text("BeforeAfter")])])),text_selection(pos([0,0],6),pos([0,0],6)))
let inserted = model.execute(base,"insert.equation",equation.defaults)^
let original = image.selected(inserted).node
let inline = model.execute(inserted,"equation.format",{display:false})^
let left = model.execute(inserted,"equation.format",{justify:"left"})^
let right = model.execute(inserted,"equation.format",{justify:"right"})^
let labeled = model.execute(inserted,"equation.format",{source:"\\frac{a+b}{c}",number:"(7)",size:26,color:"#805bc5"})^
let linear = model.execute(labeled,"equation.format",{linear:true})^
let saved = files.restore(files.archive(labeled.doc,"Equations"))^
let html = files.import_html_document(files.html_document(labeled.doc,"Equations")^)^
let copied = controller.clipboard_result(labeled,"copy",outcome.model_applied(false,false,false,"",labeled.selection,labeled.model_revision))
let pasted = image.images(edit_exec(base,edit_cmd_paste_html(copied.clipboard_html,"")).doc)[0].node

equation.settings(original)==equation.defaults
model.plain_text(inserted.doc)=="BeforeAfter" and len(image.images(inserted.doc))==1
model.css_get(attrs_get(original.attrs,'style'),"display")=="block"
layout.settings(original).wrap=="topBottom"
model.css_get(attrs_get(original.attrs,'style'),"margin-left")=="auto" and model.css_get(attrs_get(original.attrs,'style'),"margin-right")=="auto"
model.css_get(attrs_get(image.selected(inline).node.attrs,'style'),"display")=="inline-block" and layout.settings(image.selected(inline).node).wrap=="inline"
model.css_get(attrs_get(image.selected(left).node.attrs,'style'),"margin-left")=="0"
model.css_get(attrs_get(image.selected(right).node.attrs,'style'),"margin-right")=="0"
equation.settings(image.selected(labeled).node).number=="(7)"
equation.render({*:equation.defaults,source:"x",number:"(1)"})^.width>equation.render({*:equation.defaults,source:"x"})^.width
equation.settings(image.selected(linear).node).source==equation.settings(image.selected(labeled).node).source
attrs_get(image.selected(linear).node.attrs,'src')!=attrs_get(image.selected(labeled).node.attrs,'src')
model.execute(linear,"edit.undo")^.doc==labeled.doc
model.execute(model.execute(linear,"edit.undo")^,"edit.redo")^.doc==linear.doc
saved.doc==labeled.doc
equation.settings(image.images(html)[0].node)==equation.settings(image.selected(labeled).node)
attrs_get(image.images(html)[0].node.attrs,'src')==attrs_get(image.selected(labeled).node.attrs,'src')
equation.settings(pasted)==equation.settings(image.selected(labeled).node)
model.execute(inserted,"edit.undo")^.doc==base.doc
equation.settings(image.selected(model.execute(labeled,"equation.set",{source:"E=mc^2"})^).node).number=="(7)"

// Native galleries verify rendered thumbnails; all source spellings must resolve here too.
all([for (category in catalog.structures) for (entry in category.entries,
    let ast=parse(replace(entry.template,"#",entry.body or "x"),'math')^)
    len(mathdoc.unmapped_commands(ast))==0])
all([for (g in catalog.symbol_groups) for (s in g.entries) len(mathdoc.unmapped_commands(parse(s.token,'math')^))==0])
all([for (e in catalog.examples) len(mathdoc.unmapped_commands(parse(e.source,'math')^))==0])
catalog.insert("a+b",0,3,catalog.entry("fraction","stacked"))=={source:"\\frac{a+b}{b}",start:6,end:9}
catalog.insert("α+β",2,3,catalog.entry("radical","square"))=={source:"α+\\sqrt{β}",start:8,end:9}
catalog.insert("😀x",1,2,catalog.entry("script","superscript")).source=="😀{x}^{2}"
catalog.insert("x",1,1,{token:"\\alpha "})=={source:"x\\alpha ",start:8,end:8}
catalog.insert("a_b",0,3,authoring.fragment({category:"normal"})).source=="\\text{a\\_b}"
authoring.parameters({*:equation.defaults,size:"28"}).size==28
authoring.active({editor:inserted,equation_edit:{path:image.selected(inserted).path,revision:inserted.model_revision}})
not authoring.active({editor:inserted,equation_edit:{path:[99],revision:inserted.model_revision}})

model.execute(base,"equation.format",{source:"x"}) ^ {true} ~ {false}
all([for (bad in [{source:""},{source:"\\notSupported{x}"},{size:0},{size:73},{color:"red"},{justify:"justify"},{linear:"true"},{display:"true"},{number:"a\nb"}])
    model.execute(inserted,"equation.format",bad) ^ {true} ~ {false}])
equation.settings(node_attrs('img',[{name:'data-word-equation',value:"[]"}],[]))==null
model.execute(model.execute(labeled,"equation.format",{})^,"edit.undo")^.doc==inserted.doc
